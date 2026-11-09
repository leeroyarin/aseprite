// Aseprite 2.0
// Copyright (C) 2026 Aseprite 2.0 contributors
//
// This program is distributed under the terms of
// the End-User License Agreement for Aseprite.

#ifndef APP_COMMANDS_CMD_EASYMETRIC_STATE_H_INCLUDED
#define APP_COMMANDS_CMD_EASYMETRIC_STATE_H_INCLUDED
#pragma once

#include <string>

namespace app {

class Context;

// Machine-readable JSON state of the active document and (when open) the
// Easymetric studio. Same text as
// app.command.EasymetricStudio{ action = "state", out = <file> }.
// See docs/aseprite2/UITEST.md.
std::string easymetric_state_json(Context* context);

} // namespace app

#endif
