// Aseprite 2.0
// Copyright (C) 2026 Aseprite 2.0 contributors
//
// This program is distributed under the terms of
// the End-User License Agreement for Aseprite.

#ifdef HAVE_CONFIG_H
  #include "config.h"
#endif

#include "app/ui_test_server.h"

#include "app/app.h"
#include "app/commands/cmd_easymetric_state.h"
#include "app/context.h"
#include "app/doc.h"
#include "app/file/file.h"
#include "app/script/engine.h"
#include "app/ui_context.h"
#include "base/time.h"
#include "doc/cel.h"
#include "doc/color.h"
#include "doc/image.h"
#include "doc/layer.h"
#include "doc/sprite.h"
#include "json11.hpp"
#include "os/event.h"
#include "os/event_queue.h"
#include "os/keys.h"
#include "os/surface.h"
#include "os/system.h"
#include "os/window.h"
#include "ui/cursor_type.h"
#include "ui/display.h"
#include "ui/manager.h"
#include "ui/system.h"
#include "ui/timer.h"
#include "ui/widget.h"
#include "ui/window.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <atomic>
#include <sstream>
#include <thread>
#include <string>
#include <stdexcept>
#include <typeinfo>
#include <vector>

#if defined(_WIN32)
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #include <windows.h>
  #include <dbghelp.h>
  #pragma comment(lib, "dbghelp.lib")
#else
  #include <unistd.h>
#endif

namespace app {

namespace {

using json11::Json;
namespace fs = std::filesystem;

// Collects Lua print()/error text instead of opening the app console.
class CaptureDelegate : public script::EngineDelegate {
public:
  void onConsoleError(const char* text) override { errors += text; errors += "\n"; }
  void onConsolePrint(const char* text) override { output += text; output += "\n"; }
  std::string output, errors;
};

std::string demangle(const char* raw)
{
  std::string s = raw ? raw : "";
  for (const char* prefix : { "class ", "struct " }) {
    const size_t n = std::char_traits<char>::length(prefix);
    if (s.compare(0, n, prefix) == 0)
      s.erase(0, n);
  }
  return s;
}

Json rectToJson(const gfx::Rect& r)
{
  return Json::array{ r.x, r.y, r.w, r.h };
}

const char* cursorName(ui::CursorType c)
{
  switch (c) {
    case ui::kOutsideDisplay: return "outside";
    case ui::kNoCursor: return "none";
    case ui::kArrowCursor: return "arrow";
    case ui::kArrowPlusCursor: return "arrowPlus";
    case ui::kCrosshairCursor: return "crosshair";
    case ui::kForbiddenCursor: return "forbidden";
    case ui::kHandCursor: return "hand";
    case ui::kScrollCursor: return "scroll";
    case ui::kMoveCursor: return "move";
    case ui::kSizeNSCursor: return "sizeNS";
    case ui::kSizeWECursor: return "sizeWE";
    case ui::kSizeNCursor: return "sizeN";
    case ui::kSizeNECursor: return "sizeNE";
    case ui::kSizeECursor: return "sizeE";
    case ui::kSizeSECursor: return "sizeSE";
    case ui::kSizeSCursor: return "sizeS";
    case ui::kSizeSWCursor: return "sizeSW";
    case ui::kSizeWCursor: return "sizeW";
    case ui::kSizeNWCursor: return "sizeNW";
    case ui::kCustomCursor: return "custom";
    default: return "?";
  }
}

// ---------------------------------------------------------------- widget dump

Json widgetToJson(ui::Widget* w, int depth, int maxDepth, bool visibleOnly)
{
  Json::object o;
  o["cls"] = demangle(typeid(*w).name());
  o["type"] = int(w->type());
  if (!w->id().empty())
    o["id"] = w->id();
  if (!w->text().empty())
    o["text"] = w->text();
  o["rect"] = rectToJson(w->bounds());
  o["screen"] = rectToJson(w->boundsOnScreen());
  o["visible"] = w->isVisible();
  o["enabled"] = w->isEnabled();
  if (w->isSelected())
    o["selected"] = true;
  if (w->hasFocus())
    o["focus"] = true;
  if (w->hasCapture())
    o["capture"] = true;
  if (w->hasMouse())
    o["mouse"] = true;
  if (depth < maxDepth) {
    Json::array kids;
    for (ui::Widget* c : w->children()) {
      if (visibleOnly && !c->isVisible())
        continue;
      kids.push_back(widgetToJson(c, depth + 1, maxDepth, visibleOnly));
    }
    if (!kids.empty())
      o["children"] = kids;
  }
  return o;
}

struct Selector {
  std::string text, id, cls;
  int nth = 0;
  bool matches(const ui::Widget* w) const
  {
    if (!w->isVisible())
      return false;
    if (!text.empty() && w->text() != text)
      return false;
    if (!id.empty() && w->id() != id)
      return false;
    if (!cls.empty() && demangle(typeid(*w).name()).find(cls) == std::string::npos)
      return false;
    return !(text.empty() && id.empty() && cls.empty());
  }
};

void findWidgets(ui::Widget* w, const Selector& sel, std::vector<ui::Widget*>& out)
{
  if (sel.matches(w))
    out.push_back(w);
  for (ui::Widget* c : w->children())
    findWidgets(c, sel, out);
}

Selector selectorFrom(const Json& cmd)
{
  Selector s;
  s.text = cmd["text"].string_value();
  s.id = cmd["id"].string_value();
  s.cls = cmd["cls"].string_value();
  s.nth = cmd["nth"].int_value();
  return s;
}

// ------------------------------------------------------------------ key names

bool keyFromName(const std::string& rawName, os::KeyScancode& sc, base::codepoint_t& uc)
{
  std::string n = rawName;
  std::string lower = n;
  std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
    return char(std::tolower(c));
  });
  uc = 0;
  if (lower.size() == 1) {
    const char c = lower[0];
    if (c >= 'a' && c <= 'z') {
      sc = os::KeyScancode(os::kKeyA + (c - 'a'));
      uc = base::codepoint_t(n[0]);
      return true;
    }
    if (c >= '0' && c <= '9') {
      sc = os::KeyScancode(os::kKey0 + (c - '0'));
      uc = base::codepoint_t(c);
      return true;
    }
    if (c == ' ') { sc = os::kKeySpace; uc = ' '; return true; }
    if (c == '-') { sc = os::kKeyMinus; uc = '-'; return true; }
    if (c == '=') { sc = os::kKeyEquals; uc = '='; return true; }
  }
  if (lower.size() >= 2 && lower[0] == 'f') {
    const int f = std::atoi(lower.c_str() + 1);
    if (f >= 1 && f <= 12) {
      sc = os::KeyScancode(os::kKeyF1 + (f - 1));
      return true;
    }
  }
  struct Named { const char* name; os::KeyScancode sc; base::codepoint_t uc; };
  static const Named named[] = {
    { "enter", os::kKeyEnter, 13 },     { "return", os::kKeyEnter, 13 },
    { "esc", os::kKeyEsc, 27 },         { "escape", os::kKeyEsc, 27 },
    { "tab", os::kKeyTab, 9 },          { "space", os::kKeySpace, ' ' },
    { "backspace", os::kKeyBackspace, 8 }, { "delete", os::kKeyDel, 0 },
    { "del", os::kKeyDel, 0 },          { "home", os::kKeyHome, 0 },
    { "end", os::kKeyEnd, 0 },          { "pageup", os::kKeyPageUp, 0 },
    { "left", os::kKeyLeft, 0 },        { "right", os::kKeyRight, 0 },
    { "up", os::kKeyUp, 0 },            { "down", os::kKeyDown, 0 },
    { "minus", os::kKeyMinus, '-' },    { "equals", os::kKeyEquals, '=' },
  };
  for (const auto& e : named) {
    if (lower == e.name) {
      sc = e.sc;
      uc = e.uc;
      return true;
    }
  }
  return false;
}

os::KeyModifiers modsFromString(const std::string& s)
{
  int m = 0;
  if (s.find("shift") != std::string::npos) m |= os::kKeyShiftModifier;
  if (s.find("ctrl") != std::string::npos) m |= os::kKeyCtrlModifier;
  if (s.find("alt") != std::string::npos) m |= os::kKeyAltModifier;
  return os::KeyModifiers(m);
}

os::Event::MouseButton buttonFromString(const std::string& s)
{
  if (s == "right") return os::Event::RightButton;
  if (s == "middle") return os::Event::MiddleButton;
  return os::Event::LeftButton;
}

// ------------------------------------------------------------------ watchdog
//
// If the UI thread stops ticking the test-server timer for a few seconds the
// app is hung (that is what a test sees as "timeout waiting for ..."). A
// background thread then suspends the UI thread, walks its stack with
// DbgHelp (symbols from aseprite.pdb next to the exe) and writes
// <dir>/hang-N.txt, so a hang is diagnosed from the first occurrence instead
// of being guessed at. Windows only; elsewhere it is a no-op.
#if defined(_WIN32)
class HangWatchdog {
public:
  HangWatchdog(const std::string& dir) : m_dir(dir)
  {
    ::DuplicateHandle(::GetCurrentProcess(), ::GetCurrentThread(), ::GetCurrentProcess(),
                      &m_uiThread, 0, FALSE, DUPLICATE_SAME_ACCESS);
    m_lastTick = base::current_tick();
    m_thread = std::thread([this] { run(); });
    m_thread.detach();
  }
  void tick() { m_lastTick = base::current_tick(); }

private:
  void run()
  {
    bool dumped = false;
    int n = 0;
    for (;;) {
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
      const double idle = base::current_tick() - m_lastTick.load();
      if (idle < 6000) {
        dumped = false;
        continue;
      }
      if (dumped)
        continue;
      dumped = true;
      dump(++n, idle);
    }
  }

  void dump(int n, double idleMs)
  {
    std::ostringstream out;
    out << "UI thread silent for " << idleMs << " ms\n";
    HANDLE proc = ::GetCurrentProcess();
    static bool symInit = false;
    if (!symInit) {
      ::SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME);
      ::SymInitialize(proc, nullptr, TRUE);
      symInit = true;
    }
    if (::SuspendThread(m_uiThread) == (DWORD)-1) {
      out << "SuspendThread failed\n";
    }
    else {
      CONTEXT ctx;
      std::memset(&ctx, 0, sizeof(ctx));
      ctx.ContextFlags = CONTEXT_FULL;
      if (::GetThreadContext(m_uiThread, &ctx)) {
        STACKFRAME64 frame;
        std::memset(&frame, 0, sizeof(frame));
        frame.AddrPC.Offset = ctx.Rip;
        frame.AddrPC.Mode = AddrModeFlat;
        frame.AddrFrame.Offset = ctx.Rbp;
        frame.AddrFrame.Mode = AddrModeFlat;
        frame.AddrStack.Offset = ctx.Rsp;
        frame.AddrStack.Mode = AddrModeFlat;
        for (int i = 0; i < 64; ++i) {
          if (!::StackWalk64(IMAGE_FILE_MACHINE_AMD64, proc, m_uiThread, &frame, &ctx, nullptr,
                             ::SymFunctionTableAccess64, ::SymGetModuleBase64, nullptr))
            break;
          if (frame.AddrPC.Offset == 0)
            break;
          char buf[sizeof(SYMBOL_INFO) + 256];
          auto* sym = reinterpret_cast<SYMBOL_INFO*>(buf);
          std::memset(sym, 0, sizeof(buf));
          sym->SizeOfStruct = sizeof(SYMBOL_INFO);
          sym->MaxNameLen = 255;
          DWORD64 disp = 0;
          out << "  #" << i << " ";
          if (::SymFromAddr(proc, frame.AddrPC.Offset, &disp, sym)) {
            out << sym->Name;
            IMAGEHLP_LINE64 line;
            line.SizeOfStruct = sizeof(line);
            DWORD ld = 0;
            if (::SymGetLineFromAddr64(proc, frame.AddrPC.Offset, &ld, &line))
              out << "  (" << line.FileName << ":" << line.LineNumber << ")";
          }
          else {
            out << "0x" << std::hex << frame.AddrPC.Offset << std::dec;
          }
          out << "\n";
        }
      }
      ::ResumeThread(m_uiThread);
    }
    std::ofstream f(fs::path(m_dir) / ("hang-" + std::to_string(n) + ".txt"));
    f << out.str();
  }

  std::string m_dir;
  HANDLE m_uiThread = nullptr;
  std::atomic<double> m_lastTick{ 0 };
  std::thread m_thread;
};
#else
class HangWatchdog {
public:
  explicit HangWatchdog(const std::string&) {}
  void tick() {}
};
#endif

// ------------------------------------------------------------------- server

class UiTestServer {
public:
  explicit UiTestServer(const std::string& dir) : m_dir(dir), m_timer(15), m_watchdog(dir)
  {
    fs::create_directories(m_dir);
    m_timer.Tick.connect([this] { onTick(); });
    m_timer.start();
    Json::object ready{ { "ready", true }, { "pid", int(getPid()) } };
    writeFile("ready.json", Json(ready).dump());
  }

private:
  struct Step {
    double delayMs;                 // wait BEFORE running this step
    std::function<void()> fn;
  };

  static int getPid()
  {
#if defined(_WIN32)
    return int(::GetCurrentProcessId());
#else
    return int(::getpid());
#endif
  }

  void writeFile(const std::string& name, const std::string& text)
  {
    const fs::path tmp = fs::path(m_dir) / (name + ".tmp");
    const fs::path dst = fs::path(m_dir) / name;
    {
      std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
      f << text;
    }
    // The client may have the old file open for a moment (Windows refuses to
    // replace it then): retry a few times instead of leaving a stale result.
    // (Windows Defender / the client can hold a file for a few ms.)
    for (int attempt = 0; attempt < 100; ++attempt) {
      std::error_code ec;
      fs::remove(dst, ec);
      fs::rename(tmp, dst, ec);
      if (!ec)
        return;
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    // Last resort: write the destination directly (the client retries on a
    // half-written file), so a result is never silently lost.
    std::ofstream f(dst, std::ios::binary | std::ios::trunc);
    f << text;
  }

  void log(const std::string& line)
  {
    std::ofstream f(fs::path(m_dir) / "server.log", std::ios::app);
    f << base::current_tick() << " " << line << "\n";
  }

  void onTick()
  {
    m_watchdog.tick();
    const double now = base::current_tick();
    // UI stall metric: the longest gap between two timer ticks while a
    // command runs (the timer fires every 15 ms when the UI thread is free).
    if (m_lastTickAt > 0)
      m_maxGapMs = std::max(m_maxGapMs, now - m_lastTickAt);
    m_lastTickAt = now;
    // Run due steps of the current command.
    if (m_busy) {
      while (!m_steps.empty() && now >= m_nextAt) {
        Step st = std::move(m_steps.front());
        m_steps.pop_front();
        try {
          st.fn();
        }
        catch (const std::exception& e) {
          m_error = e.what();
        }
        if (!m_steps.empty())
          m_nextAt = now + m_steps.front().delayMs;
      }
      if (m_steps.empty() && now >= m_nextAt)
        finish();
      return;
    }
    const fs::path cmdPath = fs::path(m_dir) / "cmd.json";
    std::error_code ec;
    if (!fs::exists(cmdPath, ec))
      return;
    std::ifstream f(cmdPath, std::ios::binary);
    std::stringstream ss;
    if (f)
      ss << f.rdbuf();
    f.close();
    std::string err;
    Json cmd = ss.str().empty() ? Json() : Json::parse(ss.str(), err);
    if (ss.str().empty() || !err.empty() || !cmd.is_object()) {
      // The file can be visible before the client's write is complete or
      // while Windows still holds it: keep it and retry on the next tick;
      // only give up (and answer, with id -1) after ~3 s.
      if (++m_badReads < 200)
        return;
      m_badReads = 0;
      fs::remove(cmdPath, ec);
      log("bad cmd.json: " + err);
      writeResult(Json::object{ { "id", -1 }, { "ok", false }, { "error", "bad json: " + err } });
      return;
    }
    m_badReads = 0;
    fs::remove(cmdPath, ec);
    begin(cmd);
  }

  void begin(const Json& cmd)
  {
    m_id = cmd["id"];
    log("begin id=" + std::to_string(cmd["id"].int_value()) + " op=" + cmd["op"].string_value());
    m_result = Json::object();
    m_maxGapMs = 0;
    m_error.clear();
    m_steps.clear();
    m_busy = true;
    m_nextAt = base::current_tick();
    const std::string op = cmd["op"].string_value();
    try {
      dispatch(op, cmd);
    }
    catch (const std::exception& e) {
      m_error = e.what();
    }
    // Always settle so the UI processes what we injected before the
    // response is written.
    const double settle = cmd["settle"].is_number() ? cmd["settle"].number_value() : 60.0;
    m_steps.push_back(Step{ settle, [] {} });
    if (!m_steps.empty())
      m_nextAt = base::current_tick() + m_steps.front().delayMs;
  }

  void finish()
  {
    Json::object o = m_result.object_items();
    o["id"] = m_id;
    o["uiMaxGapMs"] = m_maxGapMs;
    o["ok"] = m_error.empty();
    // Cursor as it is right after the injected input (the app re-evaluates
    // it on every real mouse event, so read it with the move, not later).
    o["cursor"] = cursorName(ui::get_mouse_cursor());
    if (!m_error.empty())
      o["error"] = m_error;
    m_busy = false;
    log("finish id=" + std::to_string(m_id.int_value()) + (m_error.empty() ? " ok" : " ERR " + m_error));
    writeResult(Json(o));
  }

  void writeResult(const Json& j) { writeFile("res.json", j.dump()); }

  void setResult(const std::string& key, const Json& v)
  {
    Json::object o = m_result.object_items();
    o[key] = v;
    m_result = Json(o);
  }

  // ---- event injection
  void queue(const os::Event& ev) { os::System::instance()->eventQueue()->queueEvent(ev); }

  void addStep(double delayMs, std::function<void()> fn)
  {
    m_steps.push_back(Step{ delayMs, std::move(fn) });
  }

  void mouseEvent(os::Event::Type type,
                  int x,
                  int y,
                  os::Event::MouseButton button,
                  os::KeyModifiers mods)
  {
    os::Event ev;
    ev.setType(type);
    ev.setPosition(gfx::Point(x, y));
    ev.setButton(button);
    ev.setModifiers(mods);
    ev.setPointerType(os::PointerType::Mouse);
    queue(ev);
  }

  // The UI sends kSetCursorMessage / updates the widget under the mouse
  // after a move; a lone first move to a new place does not refresh the
  // cursor until a second event arrives (a real mouse always sends many).
  void moveTwice(int x, int y, os::KeyModifiers mods, double stepMs)
  {
    mouseEvent(os::Event::MouseMove, x, y, os::Event::NoneButton, mods);
    addStep(stepMs, [this, x, y, mods] {
      mouseEvent(os::Event::MouseMove, x, y, os::Event::NoneButton, mods);
    });
  }

  void keyEvent(os::Event::Type type,
                os::KeyScancode sc,
                base::codepoint_t uc,
                os::KeyModifiers mods)
  {
    os::Event ev;
    ev.setType(type);
    ev.setScancode(sc);
    ev.setUnicodeChar(uc);
    ev.setModifiers(mods);
    ev.setRepeat(0);
    queue(ev);
  }

  gfx::Point centerOf(const Json& cmd)
  {
    if (cmd["x"].is_number() && cmd["y"].is_number())
      return gfx::Point(cmd["x"].int_value(), cmd["y"].int_value());
    Selector sel = selectorFrom(cmd);
    std::vector<ui::Widget*> found;
    findWidgets(ui::Manager::getDefault(), sel, found);
    if (sel.nth < 0 || sel.nth >= int(found.size()))
      throw std::runtime_error("widget not found (matches: " + std::to_string(found.size()) + ")");
    setResult("widget", Json::object{ { "cls", demangle(typeid(*found[sel.nth]).name()) },
                                      { "text", found[sel.nth]->text() },
                                      { "rect", rectToJson(found[sel.nth]->bounds()) } });
    return found[sel.nth]->bounds().center();
  }

  // ---- ops
  void dispatch(const std::string& op, const Json& cmd)
  {
    const os::KeyModifiers mods = modsFromString(cmd["mods"].string_value());
    const auto button = buttonFromString(cmd["button"].string_value());
    const double stepMs = cmd["stepMs"].is_number() ? cmd["stepMs"].number_value() : 16.0;

    if (op == "ping") {
      setResult("pong", true);
    }
    else if (op == "state") {
      std::string err;
      Json st = Json::parse(easymetric_state_json(UIContext::instance()), err);
      setResult("state", st);
    }
    else if (op == "ui") {
      const bool visibleOnly = !cmd["all"].bool_value();
      const int maxDepth = cmd["maxDepth"].is_number() ? cmd["maxDepth"].int_value() : 40;
      Json::array wins;
      for (ui::Widget* w : ui::Manager::getDefault()->children())
        if (!visibleOnly || w->isVisible())
          wins.push_back(widgetToJson(w, 0, maxDepth, visibleOnly));
      setResult("windows", wins);
      setResult("cursor", cursorName(ui::get_mouse_cursor()));
      setResult("screen", rectToJson(ui::Manager::getDefault()->bounds()));
    }
    else if (op == "find") {
      Selector sel = selectorFrom(cmd);
      std::vector<ui::Widget*> found;
      findWidgets(ui::Manager::getDefault(), sel, found);
      Json::array arr;
      for (ui::Widget* w : found)
        arr.push_back(Json::object{ { "cls", demangle(typeid(*w).name()) },
                                    { "text", w->text() },
                                    { "id", w->id() },
                                    { "selected", w->isSelected() },
                                    { "enabled", w->isEnabled() },
                                    { "rect", rectToJson(w->bounds()) } });
      setResult("matches", arr);
    }
    else if (op == "cursor") {
      setResult("cursor", cursorName(ui::get_mouse_cursor()));
    }
    else if (op == "mouse_move") {
      const gfx::Point p = centerOf(cmd);
      moveTwice(p.x, p.y, mods, stepMs);
    }
    else if (op == "mouse_down") {
      const gfx::Point p = centerOf(cmd);
      moveTwice(p.x, p.y, mods, stepMs);
      addStep(stepMs, [this, p, button, mods] { mouseEvent(os::Event::MouseDown, p.x, p.y, button, mods); });
    }
    else if (op == "mouse_up") {
      const gfx::Point p = centerOf(cmd);
      moveTwice(p.x, p.y, mods, stepMs);
      addStep(stepMs, [this, p, button, mods] { mouseEvent(os::Event::MouseUp, p.x, p.y, button, mods); });
    }
    else if (op == "click" || op == "click_widget" || op == "double_click") {
      const gfx::Point p = centerOf(cmd);
      moveTwice(p.x, p.y, mods, stepMs);
      addStep(stepMs * 2, [this, p, button, mods] { mouseEvent(os::Event::MouseDown, p.x, p.y, button, mods); });
      addStep(stepMs * 2, [this, p, button, mods] { mouseEvent(os::Event::MouseUp, p.x, p.y, button, mods); });
      if (op == "double_click") {
        addStep(stepMs, [this, p, button, mods] { mouseEvent(os::Event::MouseDown, p.x, p.y, button, mods); });
        addStep(stepMs, [this, p, button, mods] { mouseEvent(os::Event::MouseUp, p.x, p.y, button, mods); });
      }
    }
    else if (op == "drag") {
      // {x1,y1,x2,y2, steps, button, mods, stepMs}  (and optional via:[[x,y],...])
      std::vector<gfx::Point> pts;
      pts.push_back(gfx::Point(cmd["x1"].int_value(), cmd["y1"].int_value()));
      if (cmd["via"].is_array())
        for (const Json& v : cmd["via"].array_items())
          pts.push_back(gfx::Point(v[0].int_value(), v[1].int_value()));
      pts.push_back(gfx::Point(cmd["x2"].int_value(), cmd["y2"].int_value()));
      const int perSeg = std::max(1, cmd["steps"].is_number() ? cmd["steps"].int_value() : 8);
      const gfx::Point a = pts.front();
      moveTwice(a.x, a.y, mods, stepMs);
      addStep(stepMs * 3, [this, a, button, mods] { mouseEvent(os::Event::MouseDown, a.x, a.y, button, mods); });
      for (size_t i = 1; i < pts.size(); ++i) {
        for (int s = 1; s <= perSeg; ++s) {
          const gfx::Point p0 = pts[i - 1], p1 = pts[i];
          const int x = p0.x + (p1.x - p0.x) * s / perSeg;
          const int y = p0.y + (p1.y - p0.y) * s / perSeg;
          addStep(stepMs, [this, x, y, button, mods] { mouseEvent(os::Event::MouseMove, x, y, button, mods); });
        }
      }
      const gfx::Point b = pts.back();
      addStep(stepMs * 3, [this, b, button, mods] { mouseEvent(os::Event::MouseUp, b.x, b.y, button, mods); });
    }
    else if (op == "wheel") {
      const gfx::Point p = centerOf(cmd);
      moveTwice(p.x, p.y, mods, stepMs);
      const int dx = cmd["dx"].int_value(), dy = cmd["dy"].int_value();
      addStep(stepMs * 2, [this, p, dx, dy, mods] {
        os::Event ev;
        ev.setType(os::Event::MouseWheel);
        ev.setPosition(p);
        ev.setWheelDelta(gfx::Point(dx, dy));
        ev.setModifiers(mods);
        ev.setPointerType(os::PointerType::Mouse);
        queue(ev);
      });
    }
    else if (op == "key") {
      os::KeyScancode sc;
      base::codepoint_t uc;
      if (!keyFromName(cmd["key"].string_value(), sc, uc))
        throw std::runtime_error("unknown key '" + cmd["key"].string_value() + "'");
      os::KeyModifiers m = mods;
      if (std::isupper((unsigned char)cmd["key"].string_value()[0]) &&
          cmd["key"].string_value().size() == 1)
        m = os::KeyModifiers(int(m) | os::kKeyShiftModifier);
      keyEvent(os::Event::KeyDown, sc, uc, m);
      addStep(stepMs * 2, [this, sc, uc, m] { keyEvent(os::Event::KeyUp, sc, uc, m); });
    }
    else if (op == "type") {
      // Types text into the focused widget (letters, digits, space, - = .).
      const std::string text = cmd["text"].string_value();
      for (char ch : text) {
        os::KeyScancode sc;
        base::codepoint_t uc;
        const std::string one(1, ch);
        if (ch == '.') {
          sc = os::kKeyStop;
          uc = '.';
        }
        else if (!keyFromName(one, sc, uc))
          continue;
        os::KeyModifiers m = std::isupper((unsigned char)ch) ? os::kKeyShiftModifier
                                                             : os::KeyModifiers(0);
        addStep(stepMs, [this, sc, uc, m] { keyEvent(os::Event::KeyDown, sc, uc, m); });
        addStep(stepMs, [this, sc, uc, m] { keyEvent(os::Event::KeyUp, sc, uc, m); });
      }
    }
    else if (op == "screenshot") {
      const std::string path = cmd["path"].string_value();
      if (path.empty())
        throw std::runtime_error("screenshot needs 'path'");
      // Take the shot AFTER the injected events had time to repaint.
      addStep(80, [this, path] { screenshot(path); });
    }
    else if (op == "lua") {
      CaptureDelegate cap;
      Context* ctx = UIContext::instance();
      auto* engine = App::instance()->scriptEngine();
      bool ok = false;
      {
        script::ScopedEngineDelegate scoped(engine, &cap);
        ok = engine->evalCode(cmd["code"].string_value(), "uitest");
      }
      (void)ctx;
      setResult("output", cap.output);
      if (!cap.errors.empty())
        setResult("luaErrors", cap.errors);
      setResult("luaOk", ok && cap.errors.empty());
    }
    else if (op == "sleep") {
      addStep(cmd["ms"].number_value(), [] {});
    }
    else if (op == "quit") {
      addStep(10, [] {
        os::Event ev;
        ev.setType(os::Event::CloseApp);
        os::System::instance()->eventQueue()->queueEvent(ev);
      });
    }
    else if (op == "kill") {
      addStep(10, [] { std::_Exit(0); });
    }
    else {
      throw std::runtime_error("unknown op '" + op + "'");
    }
  }

  void screenshot(const std::string& path)
  {
    os::Window* window = ui::Manager::getDefault()->display()->nativeWindow();
    os::Surface* surface = window->surface();
    doc::ImageSpec spec(doc::ColorMode::RGB, surface->width(), surface->height());
    doc::Sprite* spr = doc::Sprite::MakeStdSprite(spec);
    doc::Image* img = spr->firstLayer()->cel(0)->image();
    for (int y = 0; y < img->height(); ++y) {
      for (int x = 0; x < img->width(); ++x) {
        const gfx::Color c = surface->getPixel(x, y);
        img->putPixel(x, y, doc::rgba(gfx::getr(c), gfx::getg(c), gfx::getb(c), 255));
      }
    }
    std::unique_ptr<Doc> doc(new Doc(spr));
    doc->setFilename(path);
    Context* ctx = UIContext::instance();
    doc->setContext(ctx);
    const int r = save_document(ctx, doc.get());
    setResult("screenshot", Json::object{ { "path", path },
                                          { "width", surface->width() },
                                          { "height", surface->height() },
                                          { "scale", window->scale() },
                                          { "saveResult", r } });
  }

  std::string m_dir;
  ui::Timer m_timer;
  HangWatchdog m_watchdog;
  bool m_busy = false;
  int m_badReads = 0;
  double m_lastTickAt = 0;
  double m_maxGapMs = 0;
  double m_nextAt = 0;
  std::deque<Step> m_steps;
  Json m_id;
  Json m_result;
  std::string m_error;
};

} // namespace

void start_ui_test_server_if_requested()
{
  static std::unique_ptr<UiTestServer> server;
  if (server)
    return;
  const char* dir = std::getenv("ASEPRITE_UITEST_DIR");
  if (!dir || !*dir)
    return;
  server = std::make_unique<UiTestServer>(dir);
  // A test must not be disturbed by the person's own mouse over the window.
  // ASEPRITE_UITEST_REAL_INPUT=1 keeps the real input (to watch a test live).
  const char* real = std::getenv("ASEPRITE_UITEST_REAL_INPUT");
  if (!(real && *real == '1'))
    ui::Manager::setIgnoreNativeInput(true);
}

} // namespace app
