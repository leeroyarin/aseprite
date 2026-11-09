# UITEST — testing Aseprite 2.0 without looking at pixels

Two tools, both built into the app. They exist so an agent (or CI) can *know* what the UI did from
structured output, instead of guessing from screenshots or hijacking the desktop mouse.

## 1. `EasymetricStudio{ action = "state" }` — Lua / headless

```lua
app.command.EasymetricStudio{ action = "state", out = "state.json" }   -- file
app.command.EasymetricStudio{ action = "state" }                       -- stdout (headless)
```

Works with `aseprite -b --script x.lua`. JSON (`version` 1):

| key | meaning |
|---|---|
| `sprite` | width, height, frames, frame, filename |
| `layers[]` | name, kind, visible, editable, active, hasCel, cel `[x,y,w,h]` |
| `activeLayer`, `tool` | current layer name, active tool id |
| `faces.<TOP\|DOWN\|FRONT\|BACK\|RIGHT\|LEFT>` | `uv` rect in the sheet; `base`/`texture`/`depth` = `{opaque, sum, hash}` of that face's pixels on that sheet layer (hash changes iff a pixel changes) |
| `hasSurfaces`, `hasDepthLayer` | |
| `studio.open` | true while the 6-panel studio is open; when open also: `toolMode`, `previewFloating`, `activeFace`, `columnsPx`, `rowsPx`, `bounds`, and per face: `editor`/`header` rects, `zoom`, `layer`, `idle`, `symX`, `symY`, `animLinked`, `grid{}`, `buttons[]` (name, text, on, visible, bounds) |

Rects are `[x, y, w, h]` in **UI (logical) coordinates** (what injected mouse events use). The real
screen pixels are `x * scale` (the screenshot reports `scale`).

## 2. The in-app UI test server — real input, structured output

Start the app with `ASEPRITE_UITEST_DIR=<dir>`. Without that variable nothing runs. A client writes
`<dir>/cmd.json` (one command), the app answers in `<dir>/res.json` with the same `id`.
`tools/ui-test/uitest.py` is the ready-made Python client:

```python
from uitest import UiTest
with UiTest(files=["x.aseprite"]) as t:      # private copy of build/bin, isolated user folder
    t.lua("app.command.EasymetricStudio()")
    st = t.state()
    t.click_widget(text="Grid")              # by label
    t.drag(x1, y1, x2, y2)                   # real mouse events inside the app window
    t.key("z", mods="ctrl")
    t.screenshot("out.png")
```

Ops: `ping`, `state`, `ui` (full widget tree: class, id, text, rect, visible, enabled, selected,
focus, mouse/capture), `find` (`text`/`id`/`cls`, `nth`), `cursor`, `mouse_move`, `mouse_down`,
`mouse_up`, `click`, `double_click`, `click_widget`, `drag` (`via` waypoints), `wheel`, `key`
(`mods`: shift/ctrl/alt), `type`, `sleep`, `screenshot`, `lua` (returns print output), `quit`,
`kill`. Every op waits `settle` ms (default 60) after injecting so the UI has processed it.

Events are posted to the app's own event queue (the same queue the OS fills), so the whole UI path
runs: hit-testing, capture, tools, shortcuts, cursor. They never touch the desktop mouse/keyboard,
so a test can run while you keep working. The window does not need focus.

## Self-test

`python tests/ui/studio_selftest.py` — opens a sprite, opens the studio, draws with a real drag,
undoes with Ctrl+Z, opens/cancels the Grid pop-up, drags a panel border, checks the resize cursor,
screenshots, exits. Passing means the harness and the studio state report are healthy.

## Known limits

* Single main display only (floating panels drawn inside the main window are fine).
* `type` handles letters, digits, space, `-`, `=`, `.`.
* The user's PixelLab extension may open a dialog on launch; `UiTest.dismiss_dialogs()` closes it.
