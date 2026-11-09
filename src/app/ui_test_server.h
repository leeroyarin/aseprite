// Aseprite 2.0
// Copyright (C) 2026 Aseprite 2.0 contributors
//
// This program is distributed under the terms of
// the End-User License Agreement for Aseprite.

#ifndef APP_UI_TEST_SERVER_H_INCLUDED
#define APP_UI_TEST_SERVER_H_INCLUDED
#pragma once

namespace app {

// In-app UI test server (docs/aseprite2/UITEST.md).
//
// Enabled ONLY when the environment variable ASEPRITE_UITEST_DIR names a
// directory. The app then watches <dir>/cmd.json; a client (tools/ui-test/)
// writes one JSON command at a time and reads <dir>/res.json. Commands:
// dump the widget tree / studio state, inject real mouse and keyboard events
// into the app window (no desktop input, no focus stealing), take
// screenshots of the app window, evaluate Lua, quit.
//
// Nothing here runs in normal use: without the env variable start() returns
// immediately and no timer exists.
void start_ui_test_server_if_requested();

} // namespace app

#endif
