# Session recorder

Aseprite 2.0 keeps a local record of what you do, so that what you did and expected can be compared
with what the app did (by you, or by an agent reading the files). Nothing is uploaded or committed.

## Using it

- **On by default.** Help > **Record Session** turns it off and on. The choice is saved
  (preferences section `[session_recorder]`: `enabled`, `screenshots`, `max_size_mb`).
- **Mark a problem:** Help > **Mark a Problem...** or **Ctrl+Alt+M**. Type what you expected; it is
  saved with a screenshot and the current file, layer, frame and tool.
- **Find the files:** Help > **Open Session Recordings Folder**, i.e.
  `%APPDATA%\Aseprite2\usage\<YYYYMMDD-HHMMSS>\` (one folder per app start):
  - `events.jsonl`: one JSON object per line.
  - `shots\NNNNNN.png`: window screenshots.
- **Size limit:** at each start the oldest sessions are deleted until the folder is under
  `max_size_mb` (default 1000 MB).

## Reading it

```
python tools/usage/summarize_session.py              # latest session as a timeline
python tools/usage/summarize_session.py --notes      # only marked problems + the 25 events before each
python tools/usage/summarize_session.py --list       # all sessions: size, events, notes
python tools/usage/summarize_session.py <dir> --no-drag
```

## What is recorded

Every line has `type`, `t` (ms since the session started) and `time` (local clock).

| type | when | fields |
|---|---|---|
| `session_start` / `session_end` | app start / normal close | `app`, `version`, `screen` [w, h, scale] |
| `mouse_down` / `mouse_up` / `double_click` | any button | `button`, `pos` (window px), `mods`, `widget` (short ancestor path), `editor` {`sprite` [x, y], `zoom`, `active`, `face` (Easymetric panel)} |
| `drag` | mouse moves while a button is held, at most every 60 ms | `pos`, `widget`, `editor` |
| `wheel` / `magnify` | wheel, pinch | `delta`, `precise`, `pos`, `editor` |
| `key` | key press (not release) | `key` ("Ctrl+Z"), `char`, `repeat`, `focus` |
| `command` | before any command runs (menus, shortcuts, scripts) | `id`, `name`, `params` |
| `edit` | an undoable change was committed | `label` (e.g. "Pencil Tool", "Align Right View") |
| `tool` | active tool changed | `tool` |
| `site` | active document / layer / frame changed | `context` {`file`, `size`, `layer`, `frame`, `tool`} |
| `view` | an editor's zoom or scroll changed | `zoom`, `scroll`, `face` |
| `shot` | screenshot taken | `file`, `reason`, `size` |
| `note` | Mark a Problem | `text`, `context` |
| `recorder_on` / `recorder_off` | Help > Record Session | |

**Not recorded:** mouse movement with no button held, and key releases. Text typed into fields **is**
recorded, as key presses.

**Screenshots:** taken 150 ms after a mouse release, a key press, a wheel turn, a command or a note.
At most one every 500 ms; a note always gets one. They are captured on the UI thread (one copy of
the window surface) and written as PNG by a background thread, so saving never blocks the UI. If the
disk falls behind, at most 8 are queued and the rest are skipped.

## For developers

- Code: `src/app/session_recorder.{h,cpp}`. Commands (`ToggleSessionRecorder`, `MarkProblem`,
  `OpenRecordingsFolder`): `src/app/commands/cmd_session_recorder.cpp`.
- **Hooks:**
  - input: a hidden widget registered with `ui::Manager::addMessageFilter`; it always returns false, so it only observes.
  - commands: `UIContext` `Before/AfterCommandExecution`.
  - tools: `ActiveToolObserver`.
  - documents: `ContextObserver` + `DocUndoObserver`.
  - Easymetric panel names: `easymetric_face_of_editor()`.
- The timer only runs while something is pending, because `ui::Manager` does not collect
  deferred-deleted widgets while any timer runs.
- **Off under the UI test harness** (`ASEPRITE_UITEST_DIR`) unless `ASEPRITE_RECORD=1`.
  Test: `tests/ui/recorder_test.py`. The harness ends the app with a hard kill, so `session_end` is
  only written on a normal close.
