// Aseprite 2.0
// Copyright (C) 2026 Antigravity / Aseprite 2.0 contributors
//
// This program is distributed under the terms of
// the End-User License Agreement for Aseprite.

#include "app/commands/command.h"
#include "app/commands/commands.h"
#include "app/commands/easymetric_ui.h"
#include "app/context.h"

namespace app {
namespace {

class EasymetricStudioCommand : public Command {
public:
  EasymetricStudioCommand() : Command(CommandId::EasymetricStudio()) {}

protected:
  void onLoadParams(const Params& params) override { m_params = params; }
  bool onEnabled(Context* context) override { return context && context->activeDocument(); }
  bool onChecked(Context* context) override { return easymetricStudioChecked(context); }
  void onExecute(Context* context) override { executeEasymetricStudio(context, m_params); }

private:
  Params m_params;
};

} // anonymous namespace

Command* CommandFactory::createEasymetricStudioCommand()
{
  return new EasymetricStudioCommand;
}

} // namespace app
