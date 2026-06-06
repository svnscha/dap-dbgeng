# Debug a local program

The debugger **starts** your program and debugs it from launch. Use
`"request": "launch"`.

```json title=".vscode/launch.json"
{
  "name": "Debug myapp",
  "type": "windbg",
  "request": "launch",
  "target": "${workspaceFolder}/build/Debug/myapp.exe",
  "dbgengPath": "C:/Program Files (x86)/Windows Kits/10/Debuggers/x64/dbgeng.dll"
}
```

`target` is the program to run; `dbgengPath` points at `dbgeng.dll`. Both are
required - that's a complete launch configuration.

To pass command-line arguments, set a working directory, or change any other
behavior, see **[launch attributes](../reference/launch.md)**.
