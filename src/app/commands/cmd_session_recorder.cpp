// Aseprite 2.0
// Copyright (C) 2026 Aseprite 2.0 contributors
//
// This program is distributed under the terms of
// the End-User License Agreement for Aseprite.

#ifdef HAVE_CONFIG_H
  #include "config.h"
#endif

#include "app/commands/command.h"
#include "app/commands/commands.h"
#include "app/context.h"
#include "app/launcher.h"
#include "app/pref/preferences.h"
#include "app/session_recorder.h"
#include "app/ui/status_bar.h"
#include "ui/box.h"
#include "ui/button.h"
#include "ui/entry.h"
#include "ui/label.h"
#include "ui/scale.h"
#include "ui/window.h"

#include <filesystem>

namespace app {

// Help > Record Session: turns the local session recorder on / off.
class ToggleSessionRecorderCommand : public Command {
public:
  ToggleSessionRecorderCommand() : Command(CommandId::ToggleSessionRecorder()) {}

protected:
  bool onChecked(Context*) override { return Preferences::instance().sessionRecorder.enabled(); }

  void onExecute(Context*) override
  {
    auto& pref = Preferences::instance().sessionRecorder;
    const bool on = !pref.enabled();
    pref.enabled(on);
    session_recorder_enabled_changed(on);
    if (auto* sb = StatusBar::instance())
      sb->showTip(2500, on ? "Session recording ON (local only, Help > Open Session Recordings Folder)"
                           : "Session recording OFF");
  }
};

// Help > Mark a Problem (Ctrl+Alt+M): asks "what did you expect?" and stores
// the answer with a screenshot in the current recording.
class MarkProblemCommand : public Command {
public:
  MarkProblemCommand() : Command(CommandId::MarkProblem()) {}

protected:
  void onExecute(Context* ctx) override
  {
    if (!ctx->isUIAvailable())
      return;
    if (!Preferences::instance().sessionRecorder.enabled()) {
      if (auto* sb = StatusBar::instance())
        sb->showTip(3000, "Session recording is off (Help > Record Session)");
      return;
    }
    const int s = ui::guiscale();
    ui::Window win(ui::Window::WithTitleBar, "Mark a Problem");
    auto* box = new ui::VBox;
    box->setBorder(gfx::Border(4 * s));
    box->setChildSpacing(3 * s);
    win.addChild(box);
    box->addChild(new ui::Label("What did you expect, and what happened instead?"));
    auto* entry = new ui::Entry(512, "");
    entry->setMinSize(gfx::Size(320 * s, 0));
    entry->setExpansive(true);
    box->addChild(entry);
    auto* row = new ui::HBox;
    row->setChildSpacing(4 * s);
    auto* ok = new ui::Button("Save");
    auto* cancel = new ui::Button("Cancel");
    row->addChild(ok);
    row->addChild(cancel);
    box->addChild(row);
    bool saved = false;
    ok->Click.connect([&] {
      saved = true;
      win.closeWindow(ok);
    });
    cancel->Click.connect([&] { win.closeWindow(cancel); });
    entry->requestFocus();
    win.remapWindow();
    win.centerWindow();
    win.openWindowInForeground();
    if (!saved)
      return;
    const bool ok2 = session_recorder_add_note(entry->text());
    if (auto* sb = StatusBar::instance())
      sb->showTip(2500, ok2 ? "Problem marked in the session recording"
                            : "Could not write the note (recording is off)");
  }
};

// Help > Open Session Recordings Folder.
class OpenRecordingsFolderCommand : public Command {
public:
  OpenRecordingsFolderCommand() : Command(CommandId::OpenRecordingsFolder()) {}

protected:
  void onExecute(Context*) override
  {
    const std::string dir = session_recorder_root();
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::u8path(dir), ec);
    launcher::open_folder(dir);
  }
};

Command* CommandFactory::createToggleSessionRecorderCommand()
{
  return new ToggleSessionRecorderCommand;
}

Command* CommandFactory::createMarkProblemCommand()
{
  return new MarkProblemCommand;
}

Command* CommandFactory::createOpenRecordingsFolderCommand()
{
  return new OpenRecordingsFolderCommand;
}

} // namespace app
