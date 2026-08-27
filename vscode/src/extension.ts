// VS Code extension entry point for the WinDbg (dbgeng) debug adapter.
//
// Responsibilities:
//   1. Launch the adapter executable for "dbgeng" debug sessions via a
//      DebugAdapterDescriptorFactory, resolving its path from the
//      "dap-dbgeng.adapterPath" setting or the bundled binary.
//   2. Resolve config defaults (sources, and program/cwd from CMake Tools).
//   3. Log adapter lifecycle/errors to a "dap-dbgeng" output channel so a failed
//      session is visible instead of silently doing nothing.
//   4. Provide the process picker command used as
//        "processId": "${command:dap-dbgeng.pickProcess}"
//   5. Run the launch config's target hooks around the session: beforeSession
//      here (a failure aborts before the adapter starts), the rest in
//      lifecycle.ts.

import * as vscode from "vscode";
import { ADAPTER_NOT_FOUND, resolveAdapterPath } from "./adapter";
import { initHooks, runHooks, scriptsPath } from "./hooks";
import { createHookTracker, runSessionEndHooks } from "./lifecycle";
import { pickProcess } from "./processPicker";
import { getTarget, hookCommands, validateTarget } from "./targetConfig";
import { registerTargetCommands } from "./commands";

const DEBUG_TYPE = "dbgeng";

export function activate(context: vscode.ExtensionContext): void {
    const output = vscode.window.createOutputChannel("dap-dbgeng");
    context.subscriptions.push(output);
    initHooks(context.extensionPath);

    context.subscriptions.push(
        vscode.commands.registerCommand("dap-dbgeng.pickProcess", (config?: unknown) =>
            pickProcess(context.extensionPath, config)
        ),
        // Usable as "${command:dap-dbgeng.scriptsPath}" anywhere VS Code
        // substitutes command variables (tasks.json included) to reach the
        // bundled general-purpose scripts.
        vscode.commands.registerCommand("dap-dbgeng.scriptsPath", () => scriptsPath())
    );

    // Apply config defaults that VS Code does not inject from the schema (schema
    // "default" values are only editor hints), and resolve a launch target from
    // CMake Tools when none is given. Runs before variable substitution, so
    // ${workspaceFolder} is expanded normally.
    context.subscriptions.push(
        vscode.debug.registerDebugConfigurationProvider(DEBUG_TYPE, {
            resolveDebugConfiguration(folder, config) {
                if (!config || config.type !== DEBUG_TYPE) {
                    return config;
                }

                if (!Array.isArray(config.sources) || config.sources.length === 0) {
                    config.sources = ["${workspaceFolder}"];
                }

                // program is optional: when omitted, default to the CMake Tools
                // launch target and let VS Code resolve it (it builds the target
                // and prompts for a selection if none is set). We only inject this
                // when CMake Tools is installed, so the ${command:...} variable is
                // never left unresolved; otherwise fail with a clear message.
                if (config.request === "launch" && (typeof config.program !== "string" || !config.program.trim())) {
                    if (!vscode.extensions.getExtension("ms-vscode.cmake-tools")) {
                        void vscode.window.showErrorMessage(
                            "No 'program' is set. Set 'program' to the executable to debug, or install the CMake " +
                                "Tools extension (ms-vscode.cmake-tools) and select a launch target."
                        );
                        return undefined; // abort the session; the user has been told why
                    }
                    config.program = "${command:cmake.launchTargetPath}";
                    // Run from the launch target's directory unless the user set one.
                    if (typeof config.cwd !== "string" || !config.cwd.trim()) {
                        config.cwd = "${command:cmake.launchTargetDirectory}";
                    }
                }

                return config;
            },
        })
    );

    context.subscriptions.push(
        vscode.debug.registerDebugAdapterDescriptorFactory(DEBUG_TYPE, {
            // Async on purpose: the beforeSession hook runs here because this
            // factory fires after the preLaunchTask (unlike the config-resolve
            // hooks), so it always ships the binary that task just built, and a
            // failure aborts the session before the adapter starts.
            async createDebugAdapterDescriptor(session) {
                const target = getTarget(session.configuration);
                for (const problem of target ? validateTarget(target) : []) {
                    output.appendLine(`[${session.name}] ${problem}`);
                    void vscode.window.showWarningMessage(`dap-dbgeng: ${problem}`);
                }

                if (target && hookCommands(target, "beforeSession").length > 0) {
                    try {
                        await runHooks(target, ["beforeSession"], `${session.name}: beforeSession`, output);
                    } catch (err) {
                        const message = err instanceof Error ? err.message : String(err);
                        output.appendLine(`[${session.name}] ${message}`);
                        output.show(true);
                        void vscode.window.showErrorMessage(`dap-dbgeng: ${message}`);
                        throw new Error(message);
                    }
                }

                const adapterPath = resolveAdapterPath(context.extensionPath, session.workspaceFolder, output);
                if (!adapterPath) {
                    output.appendLine(ADAPTER_NOT_FOUND);
                    output.show(true);
                    void vscode.window.showErrorMessage(ADAPTER_NOT_FOUND);
                    throw new Error(ADAPTER_NOT_FOUND);
                }
                output.appendLine(`Starting adapter: ${adapterPath}`);
                return new vscode.DebugAdapterExecutable(adapterPath, []);
            },
        })
    );

    // Surface adapter errors/exit, and let the session hooks watch the traffic
    // for the moments they bind to.
    context.subscriptions.push(
        vscode.debug.registerDebugAdapterTrackerFactory(DEBUG_TYPE, {
            createDebugAdapterTracker(session) {
                return {
                    ...createHookTracker(session, output),
                    onError: (e) => output.appendLine(`[${session.name}] adapter error: ${e && e.message}`),
                    onExit: (code, signal) =>
                        output.appendLine(`[${session.name}] adapter exited (code=${code}, signal=${signal})`),
                };
            },
        })
    );

    context.subscriptions.push(
        vscode.debug.onDidTerminateDebugSession((session) => {
            if (session.type === DEBUG_TYPE) {
                runSessionEndHooks(session, output);
            }
        })
    );

    registerTargetCommands(context, output);
}

export function deactivate(): void {}
