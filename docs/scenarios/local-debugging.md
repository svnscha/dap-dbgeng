# Debug a local program

The debugger **starts** your program and debugs it from launch. Use
`"request": "launch"`. You can point it at the executable two ways: set `program`
yourself, or let the **CMake Tools** extension supply it.

## Configure it manually

Set `program` to the executable you want to debug:

```json title=".vscode/launch.json"
{
  "name": "Debug myapp",
  "type": "dbgeng",
  "request": "launch",
  "program": "${workspaceFolder}/build/Debug/myapp.exe"
}
```

`program` is the only required field - that's a complete launch configuration:

- `dbgeng.dll` is found automatically; set `dbgengPath` only to override it.
- `cwd` (the working directory) defaults to the program's directory.
- Add `args`, `stopAtEntry`, `sources`, or `symbolPath` as needed.

See **[launch attributes](../reference/launch.md)** for the full list.

## Using CMake Tools

If you use the **CMake Tools** extension, you can omit `program` entirely:

```json title=".vscode/launch.json"
{
  "name": "Debug (CMake target)",
  "type": "dbgeng",
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
    "type": "dbgeng",
    "request": "launch"
  }
}
```

Now **CMake: Debug** uses this adapter against the selected launch target - CMake
Tools fills in `program`, `cwd`, and `name` from the target, and any extra keys you
add to `cmake.debugConfig` (such as `stopAtEntry` or `args`) are passed through.
