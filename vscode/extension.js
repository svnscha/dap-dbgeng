// VS Code extension entry point for the WinDbg (dbgeng) debug adapter.
//
// Its only job today is the process picker: the command "dap-dbgeng.pickProcess"
// is meant to be used in a launch configuration as
//   "processId": "${command:dap-dbgeng.pickProcess}"
// When VS Code resolves that variable it invokes the command with the debug
// configuration as its argument, so the picker can read 'connectionString' (to
// list processes on a dbgsrv host instead of locally) and 'dbgengPath'. It shells
// out to the adapter's "--list-processes" mode, which enumerates processes through
// the debug engine and works the same for local and remote (process-server)
// targets. The selected PID is returned as a string for the processId field.

const vscode = require("vscode");
const cp = require("child_process");
const path = require("path");
const fs = require("fs");

function activate(context) {
    context.subscriptions.push(vscode.commands.registerCommand("dap-dbgeng.pickProcess", pickProcess));
}

function deactivate() {}

module.exports = { activate, deactivate };

async function pickProcess(config) {
    config = config || {};
    const adapterPath = resolveAdapterPath(config);
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

// The adapter executable: an explicit config.program if it exists, otherwise the
// default Ninja build output under a workspace folder.
function resolveAdapterPath(config) {
    if (typeof config.program === "string" && fs.existsSync(config.program)) {
        return config.program;
    }
    const folders = vscode.workspace.workspaceFolders || [];
    for (const folder of folders) {
        const candidate = path.join(folder.uri.fsPath, "build", "windows-x64", "src", "dap-dbgeng.exe");
        if (fs.existsSync(candidate)) {
            return candidate;
        }
    }
    if (folders.length) {
        return path.join(folders[0].uri.fsPath, "build", "windows-x64", "src", "dap-dbgeng.exe");
    }
    return "dap-dbgeng.exe";
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
