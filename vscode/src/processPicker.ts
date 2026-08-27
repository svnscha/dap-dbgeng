// The process picker command used as
//   "processId": "${command:dap-dbgeng.pickProcess}"

import * as vscode from "vscode";
import { ADAPTER_NOT_FOUND, listProcesses, resolveAdapterPath } from "./adapter";

interface ProcessQuickPickItem extends vscode.QuickPickItem {
    pid: number;
}

export async function pickProcess(extensionPath: string, rawConfig?: unknown): Promise<string | undefined> {
    const config = (rawConfig ?? {}) as { dbgengPath?: unknown; connectionString?: unknown };
    const adapterPath = resolveAdapterPath(extensionPath);
    if (!adapterPath) {
        void vscode.window.showErrorMessage(ADAPTER_NOT_FOUND);
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
        const message = err instanceof Error ? err.message : String(err);
        void vscode.window.showErrorMessage(`dap-dbgeng: could not list processes - ${message}`);
        return undefined;
    }

    const items: ProcessQuickPickItem[] = processes
        .map((p) => ({
            label: p.name && p.name.length ? p.name : "<unknown>",
            description: `PID ${p.systemId}`,
            detail: p.description && p.description !== p.name ? p.description : undefined,
            pid: p.systemId,
        }))
        .sort((a, b) => a.label.localeCompare(b.label) || a.pid - b.pid);

    const picked = await vscode.window.showQuickPick(items, {
        matchOnDescription: true,
        matchOnDetail: true,
        placeHolder: connectionString ? `Select a process on ${connectionString}` : "Select a process to attach to",
    });
    return picked ? String(picked.pid) : undefined;
}
