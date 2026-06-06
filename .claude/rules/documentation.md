---
paths:
  - "docs/**"
---

# Documentation (user guide) authoring

Authoring rules for the user guide under `docs/`. Keep edits consistent with what
is already there; the current length, phrasing, and tone are the target. The
repo-wide Markdown typography rules (plain hyphens, no emojis) also apply here, see
`markdown.md`.

The guide is a usage guide. Keep it short and scenario-first.

- Lead with the thing the reader copies, the `launch.json` block or command, then
  explain only what is specific to it.
- Link to the reference, do not re-explain. Option meaning, defaults, and per-field
  detail live in `docs/reference/`. Scenario pages point there instead of repeating
  it.
- Do not over-explain the obvious. Trust the reader; skip generic VS Code or
  Windows hand-holding.
- Plain, simple language. Short sentences. Sentence-case headings. American
  English.
- Parallel titles for the use-case pages: `Debug a local program`, `Debug a
  running process`, `Debug a remote process`, `Debug a Windows driver`.
- Admonitions sparingly (`!!! note|tip|warning|danger`) and only for a genuinely
  non-obvious caveat, not as decoration.
- Tables for option/field lists; fenced code blocks with a language for every
  snippet.
- Prefer keeping the current word count. When in doubt, cut rather than add.

Capability facts come from `vscode/package.json` and
`src/service/dap_server_initialize.cpp`. The adapter is documented as bundled in
the VS Code extension (no separate exe path).

## Before committing

```powershell
pwsh scripts/Format-Docs.ps1            # typography (dashes, emoji)
python -m mkdocs build --strict         # builds clean, links resolve
```
