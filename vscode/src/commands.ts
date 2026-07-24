// Palette commands over the target hooks:
//   - "Run Target Hook": pick a target and one of its hooks, run it.
//   - "Redeploy and Restart Target": the inner dev loop - teardown
//     (afterSessionEnd) first, then the session-ordered rest (deploy, start),
//     skipping empty hooks. Works with or without an active session.
// Both operate on the hooks of a dbgeng launch configuration; an active
// dbgeng session's target wins, otherwise launch.json is searched.

import * as vscode from "vscode";
import { describeHook, runHooks } from "./hooks";
import { getTarget, HookName, populatedHooks, Target } from "./targetConfig";

interface TargetEntry {
    name: string;
    folderName?: string;
    target: Target;
}

function listTargets(): TargetEntry[] {
    const entries: TargetEntry[] = [];
    for (const folder of vscode.workspace.workspaceFolders ?? []) {
        const configurations =
            vscode.workspace.getConfiguration("launch", folder.uri).get<vscode.DebugConfiguration[]>("configurations") ??
            [];
        for (const config of configurations) {
            const target = config.type === "dbgeng" ? getTarget(config) : undefined;
            if (target && populatedHooks(target).length > 0) {
                entries.push({ name: config.name, folderName: folder.name, target });
            }
        }
    }
    return entries;
}

async function pickTarget(): Promise<TargetEntry | undefined> {
    const session = vscode.debug.activeDebugSession;
    if (session?.type === "dbgeng") {
        const target = getTarget(session.configuration);
        if (target && populatedHooks(target).length > 0) {
            return { name: session.name, folderName: session.workspaceFolder?.name, target };
        }
    }

    const entries = listTargets();
    if (entries.length === 0) {
        void vscode.window.showErrorMessage(
            "dap-dbgeng: no dbgeng launch configuration with 'target.hooks' found in launch.json."
        );
        return undefined;
    }
    if (entries.length === 1) {
        return entries[0];
    }
    const picked = await vscode.window.showQuickPick(
        entries.map((entry) => ({
            label: entry.name,
            description: entry.target.host,
            detail: entry.folderName,
            entry,
        })),
        { placeHolder: "Select a debug target" }
    );
    return picked?.entry;
}

async function pickHook(entry: TargetEntry): Promise<HookName | undefined> {
    const available = populatedHooks(entry.target);
    if (available.length === 1) {
        return available[0];
    }
    const picked = await vscode.window.showQuickPick(
        available.map((hook) => ({ label: hook, description: describeHook(entry.target, hook), hook })),
        { placeHolder: "Select the hook to run" }
    );
    return picked?.hook;
}

function run(entry: TargetEntry, hooks: HookName[], title: string, output: vscode.OutputChannel): void {
    void Promise.resolve(runHooks(entry.target, hooks, title, output)).catch((err) => {
        const message = err instanceof Error ? err.message : String(err);
        void vscode.window.showErrorMessage(`dap-dbgeng: ${message}`);
    });
}

export function registerTargetCommands(context: vscode.ExtensionContext, output: vscode.OutputChannel): void {
    context.subscriptions.push(
        vscode.commands.registerCommand("dap-dbgeng.runHook", async () => {
            const entry = await pickTarget();
            if (!entry) {
                return;
            }
            const hook = await pickHook(entry);
            if (hook) {
                run(entry, [hook], `${entry.name}: ${hook}`, output);
            }
        }),

        vscode.commands.registerCommand("dap-dbgeng.redeployAndRestart", async () => {
            const entry = await pickTarget();
            if (!entry) {
                return;
            }
            const hooks = populatedHooks(entry.target);
            // Teardown first, then the session order (deploy, then the starts).
            const ordered = [
                ...hooks.filter((hook) => hook === "afterSessionEnd"),
                ...hooks.filter((hook) => hook !== "afterSessionEnd"),
            ];
            output.appendLine(`Redeploy and restart '${entry.name}' (${ordered.join(" -> ")})`);
            run(entry, ordered, `${entry.name}: redeploy and restart`, output);
        })
    );
}
