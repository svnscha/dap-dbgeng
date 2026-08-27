// Hook execution. A hook is an ordered list of PowerShell command lines from
// the launch config's "target.hooks.<hook>"; the extension binds them to
// session-lifecycle moments (before the adapter starts, when the attach
// request goes out, after configurationDone, after the session ends) and runs
// them verbatim - there is no builtin behavior beyond that. The bundled
// general-purpose scripts (scripts/ in the extension) are the recommended
// building blocks, reachable via the ${dbgengScripts} token here or the
// ${command:dap-dbgeng.scriptsPath} variable anywhere else (e.g. tasks.json).
//
// Tokens expanded in commands: ${dbgengScripts}, ${host}. Everything else
// (${workspaceFolder}, ...) is substituted by VS Code before the config
// reaches us. Commands signal failure by throwing or exiting non-zero.
//
// Marker contract: a command may print "TESTSIGNING=off" to have the
// extension surface the test-signing warning notification (Deploy-Binary.ps1
// -CheckTestSigning does).
//
// Each command runs inside a small wrapper that reports a failure as one
// ERROR_MARKER line on stdout. Without it the failure detail would be whatever
// PowerShell put on stderr, which for a thrown error is a multi-line record
// (and CLIXML when the stream is redirected) - unreadable in a notification.

import * as vscode from "vscode";
import * as cp from "child_process";
import * as path from "path";
import { HookName, hookCommands, Target } from "./targetConfig";

const ERROR_MARKER = "DAPDBGENG_ERROR=";

let scriptsDir = "";

// Called once at activation; hooks need the bundled scripts location and
// threading it through every call chain is not worth it.
export function initHooks(extensionPath: string): void {
    scriptsDir = path.join(extensionPath, "scripts");
}

export function scriptsPath(): string {
    return scriptsDir;
}

// A hook's commands as a short "Sign-Driver.ps1, Deploy-Binary.ps1" label.
export function describeHook(target: Target, hook: HookName): string {
    return hookCommands(target, hook).map(describeCommand).join(", ");
}

// Runs the given hooks in order under one progress notification. Rejects on the
// first failing command; callers decide whether that aborts anything.
export function runHooks(
    target: Target,
    hooks: HookName[],
    title: string,
    output: vscode.OutputChannel
): Thenable<void> {
    return vscode.window.withProgress(
        { location: vscode.ProgressLocation.Notification, title },
        async (progress) => {
            for (const hook of hooks) {
                await runHook(target, hook, output, progress);
            }
        }
    );
}

// Runs a hook's commands sequentially; throws on the first failure. Progress
// (when given) shows the running command's script name.
export async function runHook(
    target: Target,
    hook: HookName,
    output: vscode.OutputChannel,
    progress?: vscode.Progress<{ message?: string }>
): Promise<void> {
    const commands = hookCommands(target, hook);
    for (let i = 0; i < commands.length; i++) {
        const command = expandTokens(commands[i], target);
        progress?.report({ message: describeCommand(command) });
        const stdout = await runCommand(`${hook}[${i}]`, command, output);
        if (/^TESTSIGNING=off$/m.test(stdout)) {
            const warning =
                `test signing is OFF on ${target.host} - the driver will not load ` +
                "(bcdedit /set testsigning on + reboot)";
            output.appendLine(`Warning: ${warning}`);
            void vscode.window.showWarningMessage(`dap-dbgeng: ${warning}`);
        }
    }
}

function expandTokens(command: string, target: Target): string {
    let expanded = command.replace(/\$\{host\}/g, target.host ?? "");
    // A command that STARTS with a ${dbgengScripts} script becomes a proper
    // call-operator invocation, so a scripts path containing spaces still
    // parses; elsewhere the token is a plain path substitution.
    const leading = /^\$\{dbgengScripts\}[\\/]([^\s'"]+\.ps1)/i.exec(expanded);
    if (leading) {
        const full = path.join(scriptsDir, leading[1]);
        expanded = `& '${full.replace(/'/g, "''")}'` + expanded.slice(leading[0].length);
    }
    return expanded.replace(/\$\{dbgengScripts\}/g, scriptsDir);
}

// The first line carrying a message, skipping PowerShell's CLIXML framing and
// the "At line:N" / "+ ..." trailer. Only reached when a command fails without
// reporting through the marker, so a bare `ssh ...` still says something.
function firstMeaningfulLine(text: string): string | undefined {
    return text
        .split(/\r?\n/)
        .map((line) => line.trim())
        .find(
            (line) =>
                line.length > 0 &&
                !line.startsWith("#< CLIXML") &&
                !line.startsWith("<Objs") &&
                !line.startsWith("+") &&
                !/^At line:\d+/.test(line)
        )
        ?.slice(0, 300);
}

function describeCommand(command: string): string {
    const script = /([^\\/'"\s]+\.ps1)/i.exec(command);
    return script ? script[1] : command.slice(0, 40);
}

// Runs one command line in a local PowerShell (-EncodedCommand, so the
// command needs no re-quoting) and returns its stdout for marker scanning.
// The effective command, its output, and its duration go to the channel.
function runCommand(label: string, command: string, output: vscode.OutputChannel): Promise<string> {
    output.appendLine(`[${label}] ${command}`);
    const started = Date.now();
    // A native command that exits non-zero does not throw in PowerShell, so the
    // wrapper checks $LASTEXITCODE too - otherwise a failing `ssh ...` hook
    // would pass for a success.
    const script = [
        "$ErrorActionPreference = 'Stop'",
        // Progress records come back as CLIXML noise when stderr is redirected.
        "$ProgressPreference = 'SilentlyContinue'",
        "$global:LASTEXITCODE = 0",
        "try {",
        command,
        "    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }",
        "} catch {",
        `    Write-Output "${ERROR_MARKER}$(($_.Exception.Message -replace '[\\r\\n]+', ' ').Trim())"`,
        "    exit 1",
        "}",
        // Explicit, because powershell.exe exits non-zero when the last command
        // merely wrote an error record - including one the command deliberately
        // suppressed, as any "ignore if not running" teardown step does.
        "exit 0",
    ].join("\n");
    const encoded = Buffer.from(script, "utf16le").toString("base64");

    // The editor's PSModulePath points at PowerShell 7; inheriting it into
    // Windows PowerShell breaks module discovery there, down to Cert:\ not
    // existing. Dropping it lets each host compute its own default.
    const env = { ...process.env };
    delete env.PSModulePath;

    return new Promise((resolve, reject) => {
        const child = cp.spawn(
            "powershell.exe",
            ["-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-EncodedCommand", encoded],
            { windowsHide: true, env }
        );
        let stdout = "";
        let stderr = "";
        child.on("error", reject);
        child.stdout.on("data", (d) => (stdout += d.toString()));
        child.stderr.on("data", (d) => (stderr += d.toString()));
        child.on("close", (code) => {
            const seconds = ((Date.now() - started) / 1000).toFixed(1);
            const reported = stdout
                .split(/\r?\n/)
                .filter((line) => line.startsWith(ERROR_MARKER))
                .map((line) => line.slice(ERROR_MARKER.length).trim());
            const visibleOutput = stdout
                .split(/\r?\n/)
                .filter((line) => !line.startsWith(ERROR_MARKER))
                .join("\n")
                .trim();
            if (visibleOutput) {
                output.appendLine(visibleOutput);
            }
            if (code === 0) {
                output.appendLine(`[${label}] done in ${seconds} s`);
                resolve(stdout);
                return;
            }
            // The wrapper's one-line report first; a command killed before it
            // could report (or one writing only to stderr) falls back to that.
            const detail = reported[0] || firstMeaningfulLine(stderr) || `exit ${code}`;
            output.appendLine(`[${label}] failed after ${seconds} s (exit ${code})`);
            if (stderr.trim()) {
                output.appendLine(stderr.trim());
            }
            reject(new Error(`${label} failed - ${detail}`));
        });
    });
}
