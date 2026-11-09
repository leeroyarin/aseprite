# AGENT_SYNC — local coordination board (any agent: read before you edit, append when you start/stop)

Newest first. One line per claim. Release a claim when you stop. This is the local fallback for
the agent-mesh hub (project `aseprite2`); if the hub is reachable, also post there.

## Active claims

| Agent | Since | Files / areas claimed | Branch |
|---|---|---|---|
| Claude Sonnet 5.5 (arin-pc-claude-avinto) | 2026-11-09 | `src/app/commands/cmd_easymetric.cpp` (studio: flip, depth tools, tool strip), `src/app/ui/toolbar.cpp`, `src/app/ui_test_server.*` (new), `data/gui.xml` tools, `tools/ui-test/` (new), theme tool icons | `Aseprite-2.0` |

If you need `cmd_easymetric.cpp` at the same time, add a row below and pick DIFFERENT functions,
or wait for the claim to be released. Do not reformat the file (merge pain).

## Goal in progress (William, 2026-11-09) — order matters

1. **Testing without looking at pixels.** An in-app UI test server (`ASEPRITE_UITEST_DIR`) that can
   dump the widget tree + Easymetric studio state as JSON, inject real mouse/keyboard events into
   the app window, and take screenshots. Plus `app.command.EasymetricStudio{action="state"}` for
   headless Lua tests. Perfect this first, with a self-test, before anything below.
2. **Flip Horizontal / Flip Vertical** per panel: make them really work (all sheet layers, right
   coordinates, one undo step), with icons instead of the text "FH"/"FV".
3. **Depth tools in the left toolbar** while the Easymetric studio is open: Depth Brush (raise /
   lower, uses the normal brush size), Depth Pencil (paints a depth value = FG gray), Depth Fill
   (bucket). Own icons (placeholders where I can't draw them — listed at the end of HANDOFF).
4. **Layer/tool switching must be smooth**, brush strokes must not lag in the panels.
5. Quality issues William listed (first click in an inactive panel draws nothing, FV button
   disappears when a panel is narrow).

## Log

- 2026-11-09 Claude Sonnet 5.5: claim opened (see table). Last pushed head before this: `cdd26ec8c`.
