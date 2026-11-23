// Aseprite 2.0
// Copyright (C) 2026 Aseprite 2.0 contributors
//
// This program is distributed under the terms of
// the End-User License Agreement for Aseprite.

#ifndef APP_SESSION_RECORDER_H_INCLUDED
#define APP_SESSION_RECORDER_H_INCLUDED
#pragma once

#include <string>

namespace app {

// Local session recorder (docs/aseprite2/RECORDER.md).
//
// While enabled (preference [session_recorder] enabled, default on; Help >
// Record Session), every key press, mouse click / drag / wheel, command,
// tool change, edit, document switch and editor zoom/scroll change is
// appended as one JSON line to
//   <user folder>/usage/<YYYYMMDD-HHMMSS>/events.jsonl
// with a throttled screenshot of the window after actions in shots/.
// Nothing leaves the machine. Mouse movement without a held button is not
// recorded.
//
// Off while the UI test server runs (ASEPRITE_UITEST_DIR) unless
// ASEPRITE_RECORD=1.
void start_session_recorder();
void stop_session_recorder();

// Called by the Help > Record Session command after the preference changes.
void session_recorder_enabled_changed(bool enabled);

// Stores a "what did you expect?" note plus a screenshot. False if the
// recorder is off.
bool session_recorder_add_note(const std::string& text);

// Folder that holds all recorded sessions (created on demand).
std::string session_recorder_root();

} // namespace app

#endif
