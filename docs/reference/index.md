# Configuration reference

This section is the precise, lookup-style reference for everything you can put in
a `windbg` debug configuration in `launch.json`.

- **[launch attributes](launch.md)** - options for `"request": "launch"` (start a
  program).
- **[attach attributes](attach.md)** - options for `"request": "attach"` (connect
  to a process, remote target, or kernel).
- **[Supported features](features.md)** - which debugging features the adapter
  supports (breakpoints, stepping, evaluation, …) and which it does not.

## Common shape of a configuration

Every configuration is an object inside the `configurations` array of
`.vscode/launch.json`:

```json
{
  "version": "0.2.0",
  "configurations": [
    {
      "name": "A human-readable name shown in the dropdown",
      "type": "windbg",
      "request": "launch",
      "...": "scenario-specific options"
    }
  ]
}
```

| Field | Meaning |
| --- | --- |
| `name` | The label shown in the Run and Debug dropdown. |
| `type` | Always `windbg` for this adapter. |
| `request` | `launch` or `attach`. Determines which option set applies. |

## A note on paths and variables

- **Escape backslashes** in JSON (`C:\\path`) or use **forward slashes**
  (`C:/path`).
- You can use VS Code
  [variables](https://code.visualstudio.com/docs/editor/variables-reference) such
  as `${workspaceFolder}` and `${input:...}` in string values. The adapter's own
  defaults are written in terms of the workspace root.
