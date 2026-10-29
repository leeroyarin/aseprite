# AGENTS.md — Aseprite 2.0 fork

This repo = Aseprite source being extended into **Aseprite 2.0**:
AI-agent control via MCP, PixelLab extension integration, built-in project versioning.

**Any agent/model continuing work: read these first, in order.**

1. [docs/aseprite2/HANDOFF.md](docs/aseprite2/HANDOFF.md) — current status, what is done, what is next, gotchas. **Update it before you stop.**
2. [docs/aseprite2/PLAN.md](docs/aseprite2/PLAN.md) — architecture + phase plan + decisions.
3. [docs/aseprite2/PROTOCOL.md](docs/aseprite2/PROTOCOL.md) — WebSocket JSON contract between MCP server (Node) and in-app Lua bridge. Contract is shared by Phase 3 and Phase 4; change both sides + this doc together.
4. [docs/aseprite2/VERSIONING.md](docs/aseprite2/VERSIONING.md) — on-disk format of `<name>.versions/`.

## Rules for agents

- Keep changes scoped to phases in PLAN.md. Mark progress in HANDOFF.md (checkboxes + "Log" section, newest first, with date + model name).
- C++ style: follow `.clang-format` (2-space indent, 100 cols). New files need the Aseprite header comment block (see any `src/app/*.cpp`).
- Build: `cmake --build build` from repo root (Ninja, RelWithDebInfo, MSVC). Must open "x64 Native Tools" env or run via `build.cmd`-style vcvars if `cl` not on PATH. See HANDOFF.md "Build".
- Do NOT commit the PixelLab extension or any of its files (proprietary, contains license secret). It is installed at runtime from the user's own copy.
- Do NOT touch `third_party/` (submodules) except when a phase explicitly says so.
- New user-visible strings go in `data/strings/en.ini`; new menu items in `data/gui.xml`.
