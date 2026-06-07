// VS Code extension entry point for the WinDbg (dbgeng) debug adapter.
//
// Responsibilities:
//   1. Launch the adapter executable for "windbg" debug sessions via a
//      DebugAdapterDescriptorFactory (explicit and reliable; does not depend on
//      the legacy contributes.debuggers "program" field resolving correctly).
//   2. Log adapter lifecycle/errors to a "dap-dbgeng" output channel so a failed
//      session is visible instead of silently doing nothing.
//   3. Provide the process picker command used as
//        "processId": "${command:dap-dbgeng.pickProcess}"

const vscode = require("vscode");
const cp = require("child_process");
const path = require("path");
const fs = require("fs");

const DEBUG_TYPE = "windbg";
let output;
let extensionPath;

function activate(context) {
    extensionPath = context.extensionPath;
    output = vscode.window.createOutputChannel("dap-dbgeng");
    context.subscriptions.push(output);

    context.subscriptions.push(
        vscode.commands.registerCommand("dap-dbgeng.pickProcess", pickProcess)
    );

    context.subscriptions.push(
        vscode.debug.registerDebugAdapterDescriptorFactory(DEBUG_TYPE, {
            createDebugAdapterDescriptor(session) {
                const adapterPath = resolveAdapterPath(session.configuration, session.workspaceFolder);
                output.appendLine(`Starting adapter: ${adapterPath}`);
                if (!fs.existsSync(adapterPath)) {
                    const message = `dap-dbgeng adapter not found at ${adapterPath}. Build it (npm run build) or set "program" in the launch config.`;
                    output.appendLine(message);
                    output.show(true);
                    vscode.window.showErrorMessage(message);
                    throw new Error(message);
                }
                return new vscode.DebugAdapterExecutable(adapterPath, []);
            },
        })
    );

    // Surface adapter errors/exit and (optionally) protocol traffic.
    context.subscriptions.push(
        vscode.debug.registerDebugAdapterTrackerFactory(DEBUG_TYPE, {
            createDebugAdapterTracker(session) {
                return {
                    onError: (e) => output.appendLine(`[${session.name}] adapter error: ${e && e.message}`),
                    onExit: (code, signal) =>
                        output.appendLine(`[${session.name}] adapter exited (code=${code}, signal=${signal})`),
                };
            },
        })
    );
}

function deactivate() {}

module.exports = { activate, deactivate };

// ---------------------------------------------------------------------------
// Process picker
// ---------------------------------------------------------------------------
async function pickProcess(config) {
    config = config || {};
    const adapterPath = resolveAdapterPath(config, undefined);
    const dbgengPath = typeof config.dbgengPath === "string" ? config.dbgengPath : undefined;
    const connectionString =
        typeof config.connectionString === "string" && config.connectionString.trim()
            ? config.connectionString.trim()
            : undefined;

    let processes;
    try {
        processes = await listProcesses(adapterPath, dbgengPath, connectionString);
    } catch (err) {
        vscode.window.showErrorMessage(`dap-dbgeng: could not list processes - ${err.message}`);
        return undefined;
    }

    const items = processes
        .map((p) => ({
            label: p.name && p.name.length ? p.name : `(pid ${p.systemId})`,
            description: `PID ${p.systemId}`,
            detail: p.description && p.description !== p.name ? p.description : undefined,
            pid: p.systemId,
        }))
        .sort((a, b) => a.label.localeCompare(b.label) || a.pid - b.pid);

    const picked = await vscode.window.showQuickPick(items, {
        matchOnDescription: true,
        matchOnDetail: true,
        placeHolder: connectionString
            ? `Select a process on ${connectionString}`
            : "Select a process to attach to",
    });
    return picked ? String(picked.pid) : undefined;
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Resolve the adapter executable, in precedence order:
//   1. the "dap-dbgeng.adapterPath" setting (explicit override),
//   2. a "program" in the launch configuration,
//   3. the adapter bundled in the extension (vscode/bin, the default),
//   4. a Ninja build output under the workspace (for repo development).
function resolveAdapterPath(config, workspaceFolder) {
    const scope = workspaceFolder ? workspaceFolder.uri : undefined;
    const configured = vscode.workspace.getConfiguration("dap-dbgeng", scope).get("adapterPath");
    if (typeof configured === "string" && configured.trim() && fs.existsSync(configured.trim())) {
        return configured.trim();
    }

    if (config && typeof config.program === "string" && config.program && fs.existsSync(config.program)) {
        return config.program;
    }

    if (extensionPath) {
        const bundled = path.join(extensionPath, "bin", "dap-dbgeng.exe");
        if (fs.existsSync(bundled)) {
            return bundled;
        }
    }

    const relative = path.join("build", "windows-x64", "src", "dap-dbgeng.exe");
    const folders = [];
    if (workspaceFolder) {
        folders.push(workspaceFolder.uri.fsPath);
    }
    for (const folder of vscode.workspace.workspaceFolders || []) {
        folders.push(folder.uri.fsPath);
    }
    for (const base of folders) {
        const candidate = path.join(base, relative);
        if (fs.existsSync(candidate)) {
            return candidate;
        }
    }
    return folders.length ? path.join(folders[0], relative) : "dap-dbgeng.exe";
}

// Spawn `dap-dbgeng --list-processes` and parse its JSON output. Resolves to an
// array of { systemId, name, description }.
function listProcesses(adapterPath, dbgengPath, connectionString) {
    return new Promise((resolve, reject) => {
        if (!fs.existsSync(adapterPath)) {
            reject(new Error(`adapter not found at ${adapterPath}; build it first (npm run build)`));
            return;
        }

        const args = ["--list-processes"];
        if (dbgengPath) {
            args.push("--dbgeng", dbgengPath);
        }
        if (connectionString) {
            args.push("--connection", connectionString);
        }

        let stdout = "";
        let stderr = "";
        const child = cp.spawn(adapterPath, args, { windowsHide: true });
        child.on("error", reject);
        child.stdout.on("data", (d) => (stdout += d.toString()));
        child.stderr.on("data", (d) => (stderr += d.toString()));
        child.on("close", (code) => {
            let parsed;
            try {
                parsed = JSON.parse(stdout.trim());
            } catch (e) {
                reject(new Error(stderr.trim() || `process listing failed (exit ${code})`));
                return;
            }
            if (parsed && parsed.error) {
                reject(new Error(parsed.error));
                return;
            }
            if (!Array.isArray(parsed)) {
                reject(new Error("unexpected process-listing output"));
                return;
            }
            resolve(parsed);
        });
    });
}
