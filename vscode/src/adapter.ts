// Adapter executable resolution and the `--list-processes` helper.

import * as vscode from "vscode";
import * as cp from "child_process";
import * as path from "path";
import * as fs from "fs";

export const ADAPTER_NOT_FOUND =
    "dap-dbgeng adapter not found. Reinstall the packaged extension (it bundles the adapter), or set the " +
    "'dap-dbgeng.adapterPath' setting (or a launch 'program') to a built dap-dbgeng.exe.";

export interface ProcessInfo {
    systemId: number;
    name?: string;
    description?: string;
}

// VS Code substitutes ${workspaceFolder} in launch configs but NOT in
// extension settings, so the "adapterPath" setting needs manual expansion.
function expandWorkspaceFolder(value: string, workspaceFolder?: vscode.WorkspaceFolder): string {
    const folder = workspaceFolder ?? (vscode.workspace.workspaceFolders ?? [])[0];
    return folder ? value.replace(/\$\{workspaceFolder\}/g, folder.uri.fsPath) : value;
}

// Resolve the adapter executable, in precedence order:
//   1. the "dap-dbgeng.adapterPath" setting (explicit override),
//   2. the adapter bundled in the extension (vscode/bin, the default).
// Returns undefined when none exist; callers surface ADAPTER_NOT_FOUND.
// (Note: a launch config's "program" is the debuggee, not the adapter.)
export function resolveAdapterPath(
    extensionPath: string,
    workspaceFolder?: vscode.WorkspaceFolder,
    output?: vscode.OutputChannel
): string | undefined {
    const scope = workspaceFolder ? workspaceFolder.uri : undefined;
    const configured = vscode.workspace.getConfiguration("dap-dbgeng", scope).get<string>("adapterPath");
    if (typeof configured === "string" && configured.trim()) {
        const resolved = expandWorkspaceFolder(configured.trim(), workspaceFolder);
        if (fs.existsSync(resolved)) {
            return resolved;
        }
        // A configured path must not fall back silently to a stale bundled
        // adapter; say what was looked for.
        output?.appendLine(`Configured dap-dbgeng.adapterPath not found, using the bundled adapter: ${resolved}`);
    }

    const bundled = path.join(extensionPath, "bin", "dap-dbgeng.exe");
    if (fs.existsSync(bundled)) {
        return bundled;
    }

    return undefined;
}

// Spawn `dap-dbgeng --list-processes` and parse its JSON output. Resolves to an
// array of { systemId, name, description }.
export function listProcesses(
    adapterPath: string,
    dbgengPath?: string,
    connectionString?: string
): Promise<ProcessInfo[]> {
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
            let parsed: unknown;
            try {
                parsed = JSON.parse(stdout.trim());
            } catch {
                reject(new Error(stderr.trim() || `process listing failed (exit ${code})`));
                return;
            }
            if (parsed && typeof parsed === "object" && "error" in parsed) {
                reject(new Error(String((parsed as { error: unknown }).error)));
                return;
            }
            if (!Array.isArray(parsed)) {
                reject(new Error("unexpected process-listing output"));
                return;
            }
            resolve(parsed as ProcessInfo[]);
        });
    });
}
