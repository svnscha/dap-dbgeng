# Debug a local program

The debugger **starts** your program and debugs it from launch. Use
`"request": "launch"`.

```json title=".vscode/launch.json"
{
  "name": "Debug myapp",
  "type": "windbg",
  "request": "launch",
  "program": "${workspaceFolder}/build/Debug/myapp.exe"
}
```

`program` (the executable to run) is the only required field - that's a complete
launch configuration. The adapter finds `dbgeng.dll` automatically; set `dbgengPath`
only to override it.

## Using CMake Tools

If you use the **CMake Tools** extension, you can omit `program` entirely:

```json title=".vscode/launch.json"
{
  "name": "Debug (CMake target)",
  "type": "windbg",
  "request": "launch"
}
```

The adapter defaults `program` to CMake Tools' **launch target** (the executable
shown in its status bar) and `cwd` to that target's directory. VS Code builds it
and, if none is selected yet, prompts you to pick one. You can also set it ahead of
time with **CMake: Set Launch/Debug Target**. Without CMake Tools installed, the
session fails with a message telling you to set `program` explicitly.

### Let CMake Tools drive it entirely

Because the configuration uses the standard `program` / `cwd` fields, you don't even
need a `launch.json`: point CMake Tools' own debug command at this adapter by setting
`cmake.debugConfig` in your settings to just the debugger type:

```json title=".vscode/settings.json"
{
  "cmake.debugConfig": {
    "type": "windbg",
    "request": "launch"
  }
}
```

Now **CMake: Debug** uses this adapter against the selected launch target - CMake
Tools fills in `program`, `cwd`, and `name` from the target, and any extra keys you
add to `cmake.debugConfig` (such as `stopAtEntry` or `args`) are passed through.

To pass command-line arguments, set a working directory, or change any other
behavior, see **[launch attributes](../reference/launch.md)**.
