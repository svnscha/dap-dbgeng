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
const ADAPTER_NOT_FOUND =
    "dap-dbgeng adapter not found. Reinstall the packaged extension (it bundles the adapter), or set the " +
    "'dap-dbgeng.adapterPath' setting (or a launch 'program') to a built dap-dbgeng.exe.";
let output;
let extensionPath;

function activate(context) {
    extensionPath = context.extensionPath;
    output = vscode.window.createOutputChannel("dap-dbgeng");
    context.subscriptions.push(output);

    context.subscriptions.push(
        vscode.commands.registerCommand("dap-dbgeng.pickProcess", pickProcess)
    );

    // Apply config defaults that VS Code does not inject from the schema (schema
    // "default" values are only editor hints). Runs before variable substitution,
    // so ${workspaceFolder} is expanded normally.
    context.subscriptions.push(
        vscode.debug.registerDebugConfigurationProvider(DEBUG_TYPE, {
            resolveDebugConfiguration(folder, config) {
                if (config && config.type === DEBUG_TYPE) {
                    if (!Array.isArray(config.sources) || config.sources.length === 0) {
                        config.sources = ["${workspaceFolder}"];
                    }
                }
                return config;
            },
        })
    );

    context.subscriptions.push(
        vscode.debug.registerDebugAdapterDescriptorFactory(DEBUG_TYPE, {
            createDebugAdapterDescriptor(session) {
                const adapterPath = resolveAdapterPath(session.configuration, session.workspaceFolder);
                if (!adapterPath) {
                    output.appendLine(ADAPTER_NOT_FOUND);
                    output.show(true);
                    vscode.window.showErrorMessage(ADAPTER_NOT_FOUND);
                    throw new Error(ADAPTER_NOT_FOUND);
                }
                output.appendLine(`Starting adapter: ${adapterPath}`);
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
    if (!adapterPath) {
        vscode.window.showErrorMessage(ADAPTER_NOT_FOUND);
        return undefined;
    }
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
//   3. the adapter bundled in the extension (vscode/bin, the default).
// Returns undefined when none exist; callers surface ADAPTER_NOT_FOUND.
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

    return undefined;
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
