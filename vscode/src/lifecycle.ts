// Binds a session's target hooks to the moments only the client can observe:
//   - onAttachRequest: the attach request is on the wire (a user-mode service
//     started here exists in time for the adapter's processName poll),
//   - afterConfigurationDone: breakpoints are armed (a kernel driver started
//     here hits its DriverEntry breakpoint),
//   - afterSessionEnd: the session terminated (teardown).
// The beforeSession hook runs from the adapter descriptor factory in
// extension.ts, the one place where a failure can still abort the session.

import * as vscode from "vscode";
import { runHook } from "./hooks";
import { getTarget, HookName } from "./targetConfig";

interface DapMessage {
    type?: string;
    command?: string;
    success?: boolean;
}

// The hook-firing half of the session's adapter tracker; extension.ts adds its
// diagnostics to the same tracker object.
export function createHookTracker(
    session: vscode.DebugSession,
    output: vscode.OutputChannel
): Pick<vscode.DebugAdapterTracker, "onWillReceiveMessage" | "onDidSendMessage"> {
    return {
        // Client -> adapter.
        onWillReceiveMessage: (message: DapMessage) => {
            if (message?.type === "request" && message.command === "attach") {
                fire(session, "onAttachRequest", output);
            }
        },
        // Adapter -> client.
        onDidSendMessage: (message: DapMessage) => {
            if (message?.type === "response" && message.command === "configurationDone" && message.success) {
                fire(session, "afterConfigurationDone", output);
            }
        },
    };
}

export function runSessionEndHooks(session: vscode.DebugSession, output: vscode.OutputChannel): void {
    // Teardown is cleanup: a failure (service already gone, host down) is
    // logged by the hook runner, but nothing is left to abort.
    fire(session, "afterSessionEnd", output, { surfaceErrors: false });
}

function fire(
    session: vscode.DebugSession,
    hook: HookName,
    output: vscode.OutputChannel,
    options: { surfaceErrors: boolean } = { surfaceErrors: true }
): void {
    const target = getTarget(session.configuration);
    if (!target) {
        return;
    }
    void runHook(target, hook, output).catch((err) => {
        if (!options.surfaceErrors) {
            return;
        }
        const message = err instanceof Error ? err.message : String(err);
        void vscode.window.showErrorMessage(`dap-dbgeng: [${session.name}] ${message}`);
    });
}
