// Drives dap-dbgeng.exe through one scripted DAP session per feature and
// records each via the adapter's --trace flag, writing raw captures under
// recordings/. Turn a capture into a replay fixture with
// scripts/Normalize-DapRecording.ps1. Requires a built Debug tree
// (build/windows-x64) including the test-targets debuggees.
//
//   node scripts/Record-FeatureFixtures.mjs
import { spawn } from "node:child_process";
import path from "node:path";

const repo = path.resolve(import.meta.dirname, "..");
const adapter = path.join(repo, "build", "windows-x64", "src", "dap-dbgeng.exe");
const targets = path.join(repo, "build", "windows-x64", "test-targets");
const sources = path.join(repo, "test-targets", "testapp");

class Session {
    constructor(traceFile) {
        this.child = spawn(adapter, ["--trace", traceFile], { stdio: ["pipe", "pipe", "inherit"] });
        this.seq = 0;
        this.pending = new Map();
        this.eventWaiters = [];
        this.bufferedEvents = []; // events that arrived before anyone waited
        this.buffer = Buffer.alloc(0);
        this.child.stdout.on("data", (chunk) => this.onData(chunk));
    }

    onData(chunk) {
        this.buffer = Buffer.concat([this.buffer, chunk]);
        for (;;) {
            const headerEnd = this.buffer.indexOf("\r\n\r\n");
            if (headerEnd < 0) return;
            const m = /Content-Length:\s*(\d+)/i.exec(this.buffer.slice(0, headerEnd).toString("utf8"));
            if (!m) { this.buffer = this.buffer.slice(headerEnd + 4); continue; }
            const len = parseInt(m[1], 10);
            const start = headerEnd + 4;
            if (this.buffer.length < start + len) return;
            const msg = JSON.parse(this.buffer.slice(start, start + len).toString("utf8"));
            this.buffer = this.buffer.slice(start + len);
            if (msg.type === "response") {
                const resolve = this.pending.get(msg.request_seq);
                if (resolve) { this.pending.delete(msg.request_seq); resolve(msg); }
            } else if (msg.type === "event") {
                const idx = this.eventWaiters.findIndex((w) => w.event === msg.event);
                if (idx >= 0) this.eventWaiters.splice(idx, 1)[0].resolve(msg);
                else this.bufferedEvents.push(msg); // keep for a later waiter
            }
        }
    }

    send(command, args) {
        this.seq += 1;
        const json = JSON.stringify({ seq: this.seq, type: "request", command, arguments: args });
        const buf = Buffer.from(json, "utf8");
        this.child.stdin.write(`Content-Length: ${buf.length}\r\n\r\n`);
        this.child.stdin.write(buf);
        return new Promise((resolve) => this.pending.set(this.seq, resolve));
    }

    waitEvent(event) {
        const idx = this.bufferedEvents.findIndex((m) => m.event === event);
        if (idx >= 0) return Promise.resolve(this.bufferedEvents.splice(idx, 1)[0]);
        return new Promise((resolve) => this.eventWaiters.push({ event, resolve }));
    }

    async close() {
        this.child.stdin.end();
        await new Promise((resolve) => { this.child.on("exit", resolve); setTimeout(resolve, 5000); });
    }
}

function expectOk(name, resp) {
    if (!resp.success) throw new Error(`${name} failed: ${JSON.stringify(resp.body)}`);
    return resp;
}

async function begin(session, program, extra = {}) {
    await session.send("initialize", { adapterID: "dbgeng", pathFormat: "path", linesStartAt1: true, columnsStartAt1: true });
    await session.waitEvent("initialized");
    await session.send("launch", {
        name: "Debug", type: "dbgeng", request: "launch",
        program, cwd: targets, sources: [sources], symbolPath: [targets],
        stopAtEntry: false, ...extra,
    });
}

async function stopAt(session, sourceName, line) {
    expectOk("setBreakpoints", await session.send("setBreakpoints", {
        source: { name: sourceName, path: path.join(sources, sourceName) },
        lines: [line], breakpoints: [{ line }], sourceModified: false,
    }));
    await session.send("configurationDone");
    const stopped = await session.waitEvent("stopped");
    return stopped.body.threadId;
}

async function topFrame(session, threadId) {
    const stack = expectOk("stackTrace", await session.send("stackTrace", { threadId, startFrame: 0, levels: 1 }));
    return stack.body.stackFrames[0];
}

async function localsRef(session, frameId) {
    const scopes = expectOk("scopes", await session.send("scopes", { frameId }));
    return scopes.body.scopes.find((s) => s.name === "Locals").variablesReference;
}

function findVar(resp, name) {
    const v = resp.body.variables.find((x) => x.name === name);
    if (!v) throw new Error(`variable '${name}' missing`);
    return v;
}

// --- scenario: modules + readMemory + writeMemory (struct target) -----------
async function recordModulesMemory(trace) {
    const s = new Session(trace);
    await begin(s, path.join(targets, "test_struct_1.exe"), { trace });
    const threadId = await stopAt(s, "struct-1.cpp", 37);
    const frame = await topFrame(s, threadId);
    const locals = expectOk("variables", await s.send("variables", { variablesReference: await localsRef(s, frame.id) }));
    const p = findVar(locals, "p");
    if (!p.memoryReference) throw new Error("p has no memoryReference");

    expectOk("modules", await s.send("modules", {}));
    expectOk("readMemory", await s.send("readMemory", { memoryReference: p.memoryReference, count: 8 }));
    expectOk("writeMemory", await s.send("writeMemory", { memoryReference: p.memoryReference, data: "KgAAAA==" })); // 42 LE
    expectOk("readMemory", await s.send("readMemory", { memoryReference: p.memoryReference, count: 4 }));

    await s.send("continue", { threadId });
    await s.waitEvent("exited");
    await s.send("disconnect", { restart: false, terminateDebuggee: false });
    await s.close();
}

// --- scenario: setExpression (struct target) ---------------------------------
async function recordSetExpression(trace) {
    const s = new Session(trace);
    await begin(s, path.join(targets, "test_struct_1.exe"), { trace });
    const threadId = await stopAt(s, "struct-1.cpp", 37);
    const frame = await topFrame(s, threadId);
    expectOk("variables", await s.send("variables", { variablesReference: await localsRef(s, frame.id) }));

    const set = expectOk("setExpression",
        await s.send("setExpression", { expression: "t.origin.x", value: "123", frameId: frame.id }));
    if (set.body.value !== "123") throw new Error(`setExpression returned ${set.body.value}`);

    // Variable containers are snapshots of the stop; re-request scopes for a
    // fresh read that observes the assignment (what a client refresh does).
    const frame2 = await topFrame(s, threadId);
    const locals = expectOk("variables", await s.send("variables", { variablesReference: await localsRef(s, frame2.id) }));
    const t = findVar(locals, "t");
    const fields = expectOk("variables", await s.send("variables", { variablesReference: t.variablesReference }));
    const origin = findVar(fields, "origin");
    const scalars = expectOk("variables", await s.send("variables", { variablesReference: origin.variablesReference }));
    if (findVar(scalars, "x").value !== "123") throw new Error("setExpression did not stick");

    await s.send("continue", { threadId });
    await s.waitEvent("exited");
    await s.send("disconnect", { restart: false, terminateDebuggee: false });
    await s.close();
}

// --- scenario: function breakpoints (launch target) ---------------------------
async function recordFunctionBreakpoints(trace) {
    const s = new Session(trace);
    await begin(s, path.join(targets, "test_launch.exe"), { trace });
    const resp = expectOk("setFunctionBreakpoints", await s.send("setFunctionBreakpoints", {
        breakpoints: [{ name: "test_launch!main" }],
    }));
    if (!resp.body.breakpoints[0].verified) throw new Error("function bp not verified");
    await s.send("configurationDone");
    const stopped = await s.waitEvent("stopped");
    const frame = await topFrame(s, stopped.body.threadId);
    if (!frame.name.includes("main")) throw new Error(`stopped in ${frame.name}, not main`);

    await s.send("continue", { threadId: stopped.body.threadId });
    await s.waitEvent("exited");
    await s.send("disconnect", { restart: false, terminateDebuggee: false });
    await s.close();
}

// --- scenario: instruction breakpoints (struct target) ------------------------
async function recordInstructionBreakpoints(trace) {
    const s = new Session(trace);
    await begin(s, path.join(targets, "test_struct_1.exe"), { trace });
    const threadId = await stopAt(s, "struct-1.cpp", 37);
    const frame = await topFrame(s, threadId);

    const disassembly = expectOk("disassemble", await s.send("disassemble", {
        memoryReference: frame.instructionPointerReference, instructionCount: 3, resolveSymbols: false,
    }));
    const next = disassembly.body.instructions[1].address;
    const resp = expectOk("setInstructionBreakpoints", await s.send("setInstructionBreakpoints", {
        breakpoints: [{ instructionReference: next }],
    }));
    if (!resp.body.breakpoints[0].verified) throw new Error("instruction bp not verified");

    await s.send("continue", { threadId });
    await s.waitEvent("stopped");
    await s.send("stackTrace", { threadId, startFrame: 0, levels: 1 });

    await s.send("continue", { threadId });
    await s.waitEvent("exited");
    await s.send("disconnect", { restart: false, terminateDebuggee: false });
    await s.close();
}

// --- scenario: exception filter + exceptionInfo (exception target) ------------
async function recordExceptionFilter(trace) {
    const s = new Session(trace);
    await begin(s, path.join(targets, "test_exception_1.exe"), { trace });
    expectOk("setExceptionBreakpoints", await s.send("setExceptionBreakpoints", { filters: ["cpp"] }));
    await s.send("configurationDone");
    const stopped = await s.waitEvent("stopped");
    if (stopped.body.reason !== "exception") throw new Error(`stopped with ${stopped.body.reason}, not exception`);

    const info = expectOk("exceptionInfo", await s.send("exceptionInfo", { threadId: stopped.body.threadId }));
    if (info.body.exceptionId !== "0xE06D7363") throw new Error(`unexpected exceptionId ${info.body.exceptionId}`);

    await s.send("continue", { threadId: stopped.body.threadId });
    await s.waitEvent("exited");
    await s.send("disconnect", { restart: false, terminateDebuggee: false });
    await s.close();
}

// --- scenario: data breakpoints (data target) ---------------------------------
async function recordDataBreakpoints(trace) {
    const s = new Session(trace);
    await begin(s, path.join(targets, "test_data_1.exe"), { trace });
    const threadId = await stopAt(s, "data-1.cpp", 15);
    const frame = await topFrame(s, threadId);
    const ref = await localsRef(s, frame.id);
    expectOk("variables", await s.send("variables", { variablesReference: ref }));

    const info = expectOk("dataBreakpointInfo", await s.send("dataBreakpointInfo", {
        variablesReference: ref, name: "watched",
    }));
    expectOk("setDataBreakpoints", await s.send("setDataBreakpoints", {
        breakpoints: [{ dataId: info.body.dataId, accessType: "write" }],
    }));

    await s.send("continue", { threadId });
    await s.waitEvent("stopped");
    const frame2 = await topFrame(s, threadId);
    const after = expectOk("variables", await s.send("variables", { variablesReference: await localsRef(s, frame2.id) }));
    if (findVar(after, "watched").value !== "4") throw new Error(`watched is ${findVar(after, "watched").value}, not 4`);

    // Clear the watchpoint (replace-all with an empty set) before resuming:
    // the stack slot is reused after main returns and would re-fire forever.
    expectOk("setDataBreakpoints", await s.send("setDataBreakpoints", { breakpoints: [] }));

    await s.send("continue", { threadId });
    await s.waitEvent("exited");
    await s.send("disconnect", { restart: false, terminateDebuggee: false });
    await s.close();
}

const scenarios = {
    "modules-memory": recordModulesMemory,
    "set-expression": recordSetExpression,
    "function-breakpoints": recordFunctionBreakpoints,
    "instruction-breakpoints": recordInstructionBreakpoints,
    "exception-filter": recordExceptionFilter,
    "data-breakpoints": recordDataBreakpoints,
};

for (const [name, run] of Object.entries(scenarios)) {
    const trace = path.join(repo, "recordings", `${name}.session.json`);
    process.stdout.write(`recording ${name}... `);
    await run(trace);
    console.log("ok");
}
console.log("all scenarios recorded");
