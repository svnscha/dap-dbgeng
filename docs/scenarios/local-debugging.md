# Debug a local program

The debugger **starts** your program and debugs it from launch. Use
`"request": "launch"`.

```json title=".vscode/launch.json"
{
  "name": "Debug myapp",
  "type": "windbg",
  "request": "launch",
  "target": "${workspaceFolder}/build/Debug/myapp.exe"
}
```

`target` (the program to run) is the only required field - that's a complete launch
configuration. The adapter finds `dbgeng.dll` automatically; set `dbgengPath` only
to override it.

## Using CMake Tools

If you use the **CMake Tools** extension, you can omit `target` entirely:

```json title=".vscode/launch.json"
{
  "name": "Debug (CMake target)",
  "type": "windbg",
  "request": "launch"
}
```

The adapter launches CMake Tools' selected **launch target** (the executable shown
in its status bar). Pick one with **CMake: Set Launch/Debug Target**. If no target
is set - or CMake Tools is not installed - the session fails with a message telling
you to select a launch target or set `target` explicitly.

To pass command-line arguments, set a working directory, or change any other
behavior, see **[launch attributes](../reference/launch.md)**.
