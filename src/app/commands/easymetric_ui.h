// Aseprite 2.0
// Copyright (C) 2026 Antigravity / Aseprite 2.0 contributors
//
// This program is distributed under the terms of
// the End-User License Agreement for Aseprite.

#ifndef APP_COMMANDS_EASYMETRIC_UI_H_INCLUDED
#define APP_COMMANDS_EASYMETRIC_UI_H_INCLUDED

namespace app {
class Context;
class Params;
bool easymetricStudioChecked(Context* context);
void executeEasymetricStudio(Context* context, const Params& params);
} // namespace app

#endif
