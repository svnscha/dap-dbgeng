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

To pass command-line arguments, set a working directory, or change any other
behavior, see **[launch attributes](../reference/launch.md)**.
