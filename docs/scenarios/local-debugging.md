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

The adapter defaults `target` to CMake Tools' **launch target** (the executable
shown in its status bar). VS Code builds it and, if none is selected yet, prompts
you to pick one. You can also set it ahead of time with **CMake: Set Launch/Debug
Target**. Without CMake Tools installed, the session fails with a message telling
you to set `target` explicitly.

To pass command-line arguments, set a working directory, or change any other
behavior, see **[launch attributes](../reference/launch.md)**.
