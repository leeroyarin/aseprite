// Aseprite 2.0
// Copyright (C) 2026 Aseprite 2.0 contributors
//
// This program is distributed under the terms of
// the End-User License Agreement for Aseprite.

#ifdef HAVE_CONFIG_H
  #include "config.h"
#endif

#include "app/session_recorder.h"

#include "app/app.h"
#include "app/commands/cmd_easymetric_state.h"
#include "app/commands/command.h"
#include "app/context.h"
#include "app/context_observer.h"
#include "app/doc.h"
#include "app/doc_undo.h"
#include "app/doc_undo_observer.h"
#include "app/pref/preferences.h"
#include "app/resource_finder.h"
#include "app/site.h"
#include "app/tools/active_tool.h"
#include "app/tools/active_tool_observer.h"
#include "app/tools/tool.h"
#include "app/ui/editor/editor.h"
#include "app/ui_context.h"
#include "base/time.h"
#include "doc/layer.h"
#include "doc/sprite.h"
#include "json11.hpp"
#include "os/surface.h"
#include "os/surface_format.h"
#include "os/window.h"
#include "ui/display.h"
#include "ui/entry.h"
#include "ui/manager.h"
#include "ui/message.h"
#include "ui/shortcut.h"
#include "ui/timer.h"
#include "ui/view.h"
#include "ui/widget.h"
#include "ver/info.h"

#include "zlib.h"

#include <algorithm>
#include <condition_variable>
#include <cstdlib>
#include <ctime>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <typeinfo>
#include <vector>

namespace app {

namespace fs = std::filesystem;
using json11::Json;

namespace {

constexpr base::tick_t kShotDelayMs = 150;    // let the UI repaint before capturing
constexpr base::tick_t kShotMinGapMs = 500;   // at most two screenshots per second
constexpr base::tick_t kDragEveryMs = 60;     // drag samples
constexpr base::tick_t kIdleStopMs = 1500;    // stop the timer when nothing is pending

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

std::string shortText(const std::string& s, size_t max = 40)
{
  if (s.size() <= max)
    return s;
  return s.substr(0, max) + "...";
}

std::string wallClock(const char* fmt)
{
  std::time_t t = std::time(nullptr);
  std::tm tm{};
#ifdef _WIN32
  localtime_s(&tm, &t);
#else
  localtime_r(&t, &tm);
#endif
  char buf[64];
  std::strftime(buf, sizeof(buf), fmt, &tm);
  return buf;
}

const char* buttonName(ui::MouseButton b)
{
  switch (b) {
    case ui::kButtonLeft:   return "left";
    case ui::kButtonRight:  return "right";
    case ui::kButtonMiddle: return "middle";
    case ui::kButtonX1:     return "x1";
    case ui::kButtonX2:     return "x2";
    default:                return "none";
  }
}

// ---------------------------------------------------------------- PNG writer

void writeU32(std::vector<uint8_t>& out, uint32_t v)
{
  out.push_back(uint8_t(v >> 24));
  out.push_back(uint8_t(v >> 16));
  out.push_back(uint8_t(v >> 8));
  out.push_back(uint8_t(v));
}

void writeChunk(std::vector<uint8_t>& out, const char* type, const std::vector<uint8_t>& data)
{
  writeU32(out, uint32_t(data.size()));
  const size_t start = out.size();
  out.insert(out.end(), type, type + 4);
  out.insert(out.end(), data.begin(), data.end());
  const uLong crc = crc32(0L, out.data() + start, uInt(out.size() - start));
  writeU32(out, uint32_t(crc));
}

// 8-bit RGB, no filter, fast compression: UI screenshots are mostly flat colour.
bool writePng(const std::string& path, int w, int h, const std::vector<uint8_t>& rgb)
{
  std::vector<uint8_t> raw;
  raw.reserve(size_t(h) * (size_t(w) * 3 + 1));
  for (int y = 0; y < h; ++y) {
    raw.push_back(0);
    const uint8_t* row = rgb.data() + size_t(y) * size_t(w) * 3;
    raw.insert(raw.end(), row, row + size_t(w) * 3);
  }
  uLongf zlen = compressBound(uLong(raw.size()));
  std::vector<uint8_t> z(zlen);
  if (compress2(z.data(), &zlen, raw.data(), uLong(raw.size()), 1) != Z_OK)
    return false;
  z.resize(zlen);

  std::vector<uint8_t> png = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n' };
  std::vector<uint8_t> ihdr;
  writeU32(ihdr, uint32_t(w));
  writeU32(ihdr, uint32_t(h));
  ihdr.insert(ihdr.end(), { 8, 2, 0, 0, 0 }); // 8 bit, RGB, deflate, no filter, no interlace
  writeChunk(png, "IHDR", ihdr);
  writeChunk(png, "IDAT", z);
  writeChunk(png, "IEND", {});
  std::ofstream f(path, std::ios::binary);
  f.write(reinterpret_cast<const char*>(png.data()), std::streamsize(png.size()));
  return bool(f);
}

// ---------------------------------------------------------------- recorder

class SessionRecorder : public ui::Widget,
                        public ContextObserver,
                        public tools::ActiveToolObserver,
                        public DocUndoObserver {
public:
  SessionRecorder() : ui::Widget(ui::kGenericWidget), m_timer(100)
  {
    setVisible(false);
    const char* uitest = std::getenv("ASEPRITE_UITEST_DIR");
    const char* force = std::getenv("ASEPRITE_RECORD");
    m_blockedByTests = (uitest && *uitest) && !(force && std::string(force) == "1");

    ui::Manager* manager = ui::Manager::getDefault();
    for (ui::MessageType t : { ui::kKeyDownMessage, ui::kMouseDownMessage, ui::kMouseUpMessage,
                               ui::kDoubleClickMessage, ui::kMouseMoveMessage,
                               ui::kMouseWheelMessage, ui::kTouchMagnifyMessage })
      manager->addMessageFilter(t, this);

    UIContext* ctx = UIContext::instance();
    m_before = ctx->BeforeCommandExecution.connect(
      [this](CommandExecutionEvent& ev) { onCommand(ev, true); });
    m_after = ctx->AfterCommandExecution.connect(
      [this](CommandExecutionEvent& ev) { onCommand(ev, false); });
    ctx->add_observer(this);
    App::instance()->activeToolManager()->add_observer(this);
    m_timer.Tick.connect([this] { onTick(); });
    attachDoc(static_cast<Doc*>(ctx->activeDocument()));
  }

  // Called while the UI still exists (App::~App), never from a static
  // destructor: it detaches from the manager, the context and the tools.
  void shutdown()
  {
    if (m_shutdown)
      return;
    m_shutdown = true;
    m_timer.stop();
    if (m_out.is_open())
      writeRaw(Json::object{ { "type", "session_end" } }); // active() is false from here on
    ui::Manager::getDefault()->removeMessageFilterFor(this);
    m_before.disconnect();
    m_after.disconnect();
    if (UIContext* ctx = UIContext::instance())
      ctx->remove_observer(this);
    if (App::instance() && App::instance()->activeToolManager())
      App::instance()->activeToolManager()->remove_observer(this);
    attachDoc(nullptr);
    stopWorker();
    m_out.close();
  }

  bool active() const
  {
    return !m_shutdown && !m_blockedByTests &&
           Preferences::instance().sessionRecorder.enabled();
  }

  // Called after the preference changed.
  void enabledChanged(bool enabled)
  {
    if (enabled)
      write(Json::object{ { "type", "recorder_on" } });
    else if (m_out.is_open())
      writeRaw(Json::object{ { "type", "recorder_off" } }); // active() is already false
  }

  bool addNote(const std::string& text)
  {
    if (!active())
      return false;
    write(Json::object{ { "type", "note" }, { "text", text }, { "context", contextJson() } });
    requestShot("note", true);
    return true;
  }

  std::string root() const
  {
    ResourceFinder rf(false);
    rf.includeUserDir("usage");
    return rf.defaultFilename();
  }

protected:
  bool onProcessMessage(ui::Message* msg) override
  {
    if (!active())
      return false;
    try {
      logMessage(msg);
    }
    catch (...) {
      // Recording must never break input handling.
    }
    return false; // observe only: the message continues unchanged
  }

private:
  // ---------------------------------------------------------- input

  void logMessage(ui::Message* msg)
  {
    const base::tick_t now = base::current_tick();
    switch (msg->type()) {
      case ui::kKeyDownMessage: {
        auto* k = static_cast<ui::KeyMessage*>(msg);
        const bool modifierOnly = (k->scancode() >= ui::kKeyFirstModifierScancode);
        Json::object o{ { "type", "key" },
                        { "key", ui::Shortcut(k->modifiers(), k->scancode(), k->unicodeChar())
                                   .toString() } };
        if (k->unicodeChar() >= 32)
          o["char"] = std::string(1, char(k->unicodeChar() < 128 ? k->unicodeChar() : '?'));
        if (k->repeat())
          o["repeat"] = k->repeat();
        if (ui::Widget* f = ui::Manager::getDefault()->getFocus())
          o["focus"] = widgetJson(f);
        write(o);
        if (!modifierOnly && k->repeat() == 0)
          requestShot("key", false);
        break;
      }
      case ui::kMouseDownMessage:
      case ui::kMouseUpMessage:
      case ui::kDoubleClickMessage: {
        auto* m = static_cast<ui::MouseMessage*>(msg);
        const bool down = msg->type() == ui::kMouseDownMessage;
        const bool up = msg->type() == ui::kMouseUpMessage;
        Json::object o{
          { "type", down ? "mouse_down" : up ? "mouse_up" : "double_click" },
          { "button", buttonName(m->button()) },
          { "pos", Json::array{ m->position().x, m->position().y } },
        };
        addMods(o, msg);
        addTarget(o, msg->recipient(), m->position());
        write(o);
        if (down)
          m_buttonDown = true;
        if (up) {
          m_buttonDown = false;
          requestShot("mouse_up", false);
        }
        touch(now);
        break;
      }
      case ui::kMouseMoveMessage: {
        if (!m_buttonDown || now - m_lastDrag < kDragEveryMs)
          return;
        m_lastDrag = now;
        auto* m = static_cast<ui::MouseMessage*>(msg);
        Json::object o{ { "type", "drag" },
                        { "pos", Json::array{ m->position().x, m->position().y } } };
        addTarget(o, msg->recipient(), m->position());
        write(o);
        break;
      }
      case ui::kMouseWheelMessage: {
        auto* m = static_cast<ui::MouseMessage*>(msg);
        Json::object o{ { "type", "wheel" },
                        { "delta", Json::array{ m->wheelDelta().x, m->wheelDelta().y } },
                        { "precise", m->preciseWheel() },
                        { "pos", Json::array{ m->position().x, m->position().y } } };
        addMods(o, msg);
        addTarget(o, msg->recipient(), m->position());
        write(o);
        requestShot("wheel", false);
        touch(now);
        break;
      }
      case ui::kTouchMagnifyMessage: {
        auto* t = static_cast<ui::TouchMessage*>(msg);
        write(Json::object{ { "type", "magnify" }, { "amount", t->magnification() } });
        touch(now);
        break;
      }
      default: break;
    }
  }

  void addMods(Json::object& o, ui::Message* msg)
  {
    Json::array mods;
    if (msg->ctrlPressed())
      mods.push_back("ctrl");
    if (msg->shiftPressed())
      mods.push_back("shift");
    if (msg->altPressed())
      mods.push_back("alt");
    if (msg->cmdPressed())
      mods.push_back("cmd");
    if (!mods.empty())
      o["mods"] = mods;
  }

  // The widget and a short ancestor path ("Window:Grid > Button:Confirm"),
  // plus sprite coordinates when the target is (inside) an editor.
  void addTarget(Json::object& o, ui::Widget* w, const gfx::Point& pos)
  {
    if (!w)
      return;
    o["widget"] = widgetJson(w);
    for (ui::Widget* p = w; p; p = p->parent()) {
      if (auto* ed = dynamic_cast<Editor*>(p)) {
        if (ed->sprite()) {
          const gfx::Point sp = ed->screenToEditor(pos);
          Json::object e{ { "sprite", Json::array{ sp.x, sp.y } },
                          { "zoom", ed->zoom().scale() },
                          { "active", ed->isActive() } };
          const std::string face = easymetric_face_of_editor(ed);
          if (!face.empty())
            e["face"] = face;
          o["editor"] = e;
        }
        m_lastEditor = ed;
        break;
      }
    }
  }

  Json widgetJson(ui::Widget* w)
  {
    std::string path;
    int depth = 0;
    for (ui::Widget* p = w; p && depth < 4; p = p->parent(), ++depth) {
      std::string name = demangle(typeid(*p).name());
      const size_t ns = name.rfind("::");
      if (ns != std::string::npos)
        name = name.substr(ns + 2);
      if (!p->id().empty())
        name += "#" + p->id();
      else if (!p->text().empty() && !dynamic_cast<ui::Entry*>(p)) // typed text is in the key events
        name += ":" + shortText(p->text(), 24);
      path = path.empty() ? name : name + " > " + path;
    }
    return path;
  }

  // ---------------------------------------------------------- app events

  void onCommand(CommandExecutionEvent& ev, bool before)
  {
    if (!active() || !ev.command())
      return;
    if (before) {
      Json::object params;
      for (const auto& kv : ev.params())
        params[kv.first] = kv.second;
      Json::object o{ { "type", "command" },
                      { "id", ev.command()->id() },
                      { "name", ev.command()->friendlyName() } };
      if (!params.empty())
        o["params"] = params;
      if (ev.isCanceled())
        o["canceled"] = true;
      write(o);
    }
    else {
      requestShot("command", false);
      touch(base::current_tick());
    }
  }

  void onActiveSiteChange(const Site& site) override
  {
    attachDoc(static_cast<Doc*>(const_cast<Site&>(site).document()));
    if (!active())
      return;
    Json::object o{ { "type", "site" }, { "context", contextJson() } };
    const std::string key = o["context"].dump();
    if (key == m_lastSite)
      return;
    m_lastSite = key;
    write(o);
  }

  void onActiveToolChange(tools::Tool* tool) override
  {
    if (active() && tool)
      write(Json::object{ { "type", "tool" }, { "tool", tool->getId() } });
  }

  void onAddUndoState(DocUndo* history) override
  {
    if (active() && history)
      write(Json::object{ { "type", "edit" }, { "label", history->nextUndoLabel() } });
  }

  void attachDoc(Doc* doc)
  {
    DocUndo* history = doc ? doc->undoHistory() : nullptr;
    if (history == m_history)
      return;
    if (m_history && m_historyDoc && m_docAlive(m_historyDoc))
      m_history->remove_observer(this);
    m_history = history;
    m_historyDoc = doc;
    if (m_history)
      m_history->add_observer(this);
  }

  // A doc pointer is only dereferenced while the context still lists it.
  bool m_docAlive(Doc* doc) const
  {
    if (UIContext* ctx = UIContext::instance())
      for (Doc* d : ctx->documents())
        if (d == doc)
          return true;
    return false;
  }

  Json contextJson()
  {
    Json::object c;
    UIContext* ctx = UIContext::instance();
    Site site = ctx->activeSite();
    if (Doc* doc = static_cast<Doc*>(site.document())) {
      c["file"] = doc->filename();
      if (doc->sprite())
        c["size"] = Json::array{ doc->sprite()->width(), doc->sprite()->height() };
    }
    if (site.layer())
      c["layer"] = site.layer()->name();
    c["frame"] = int(site.frame()) + 1;
    if (tools::Tool* t = App::instance()->activeToolManager()->activeTool())
      c["tool"] = t->getId();
    return c;
  }

  // Editor zoom / scroll changes are checked a little after input, once the
  // editor has handled it (filters see a message before its target does).
  void checkViews()
  {
    for (Editor* ed : { Editor::activeEditor(), m_lastEditor }) {
      if (!ed || !ed->sprite() || !ui::View::getView(ed))
        continue;
      // m_lastEditor may be stale: only use it while it is still the editor
      // under the mouse or the active one.
      if (ed == m_lastEditor && ed != Editor::activeEditor() &&
          ui::Manager::getDefault()->getMouse() != ed)
        continue;
      const double zoom = ed->zoom().scale();
      const gfx::Point scroll = ui::View::getView(ed)->viewScroll();
      auto& last = m_views[ed];
      if (last.valid && last.zoom == zoom && last.scroll == scroll)
        continue;
      if (last.valid) {
        Json::object o{ { "type", "view" },
                        { "zoom", zoom },
                        { "scroll", Json::array{ scroll.x, scroll.y } } };
        const std::string face = easymetric_face_of_editor(ed);
        if (!face.empty())
          o["face"] = face;
        write(o);
      }
      last.valid = true;
      last.zoom = zoom;
      last.scroll = scroll;
    }
  }

  // ---------------------------------------------------------- timer

  void touch(base::tick_t now)
  {
    m_lastInput = now;
    if (!m_timer.isRunning())
      m_timer.start();
  }

  void requestShot(const char* reason, bool force)
  {
    if (!Preferences::instance().sessionRecorder.screenshots())
      return;
    const base::tick_t now = base::current_tick();
    base::tick_t due = now + kShotDelayMs;
    if (!force)
      due = std::max(due, m_lastShot + kShotMinGapMs);
    if (m_shotDue == 0 || force || due < m_shotDue) {
      m_shotDue = due;
      m_shotReason = reason;
    }
    touch(now);
  }

  void onTick()
  {
    const base::tick_t now = base::current_tick();
    if (active()) {
      checkViews();
      if (m_shotDue && now >= m_shotDue) {
        m_shotDue = 0;
        m_lastShot = now;
        captureShot(m_shotReason);
      }
    }
    else {
      m_shotDue = 0;
    }
    // Never keep a timer running while idle: ui::Manager only collects
    // deferred-deleted widgets when no timer runs.
    if (m_shotDue == 0 && now - m_lastInput > kIdleStopMs)
      m_timer.stop();
  }

  void captureShot(const std::string& reason)
  {
    ui::Display* display = ui::Manager::getDefault()->display();
    os::Window* window = display ? display->nativeWindow() : nullptr;
    os::Surface* surface = window ? window->surface() : nullptr;
    if (!surface || !ensureSession())
      return;
    os::SurfaceFormatData fd;
    surface->getFormat(&fd);
    if (fd.bitsPerPixel != 32)
      return;
    const int w = surface->width(), h = surface->height();
    auto job = std::make_unique<ShotJob>();
    job->w = w;
    job->h = h;
    job->rgb.resize(size_t(w) * size_t(h) * 3);
    {
      os::SurfaceLock lock(surface);
      for (int y = 0; y < h; ++y) {
        const uint32_t* row = reinterpret_cast<const uint32_t*>(surface->getData(0, y));
        uint8_t* dst = job->rgb.data() + size_t(y) * size_t(w) * 3;
        for (int x = 0; x < w; ++x) {
          const uint32_t c = row[x];
          dst[x * 3 + 0] = uint8_t((c & fd.redMask) >> fd.redShift);
          dst[x * 3 + 1] = uint8_t((c & fd.greenMask) >> fd.greenShift);
          dst[x * 3 + 2] = uint8_t((c & fd.blueMask) >> fd.blueShift);
        }
      }
    }
    char name[32];
    std::snprintf(name, sizeof(name), "%06d.png", ++m_shotCount);
    job->path = (fs::path(m_sessionDir) / "shots" / name).string();
    write(Json::object{ { "type", "shot" },
                        { "file", std::string("shots/") + name },
                        { "reason", reason },
                        { "size", Json::array{ w, h } } });
    pushJob(std::move(job));
  }

  // ---------------------------------------------------------- output

  bool ensureSession()
  {
    if (m_out.is_open())
      return true;
    if (m_sessionFailed)
      return false;
    try {
      const fs::path usage = fs::u8path(root());
      fs::create_directories(usage);
      prune(usage);
      std::string stamp = wallClock("%Y%m%d-%H%M%S");
      fs::path dir = usage / stamp;
      for (int i = 2; fs::exists(dir); ++i)
        dir = usage / (stamp + "-" + std::to_string(i));
      fs::create_directories(dir / "shots");
      m_sessionDir = dir.string();
      m_out.open(dir / "events.jsonl", std::ios::binary | std::ios::app);
      if (!m_out)
        throw std::runtime_error("cannot open events.jsonl");
      m_t0 = base::current_tick();
      ui::Display* display = ui::Manager::getDefault()->display();
      os::Window* window = display ? display->nativeWindow() : nullptr;
      Json::object start{ { "type", "session_start" },
                          { "app", get_app_name() },
                          { "version", get_app_version() } };
      if (window && window->surface())
        start["screen"] = Json::array{ window->surface()->width(), window->surface()->height(),
                                       window->scale() };
      writeRaw(start);
      startWorker();
      return true;
    }
    catch (...) {
      m_sessionFailed = true;
      return false;
    }
  }

  // Keeps the usage folder under the size limit by deleting the oldest
  // sessions (folder names sort by date). The new session is not created yet.
  void prune(const fs::path& usage)
  {
    const int limitMb = std::max(50, Preferences::instance().sessionRecorder.maxSizeMb());
    std::vector<std::pair<std::string, uintmax_t>> sessions;
    uintmax_t total = 0;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(usage, ec)) {
      if (!entry.is_directory(ec))
        continue;
      uintmax_t size = 0;
      for (const auto& f : fs::recursive_directory_iterator(entry.path(), ec))
        if (f.is_regular_file(ec))
          size += f.file_size(ec);
      sessions.emplace_back(entry.path().string(), size);
      total += size;
    }
    std::sort(sessions.begin(), sessions.end());
    const uintmax_t limit = uintmax_t(limitMb) * 1024 * 1024;
    for (const auto& s : sessions) {
      if (total <= limit)
        break;
      fs::remove_all(s.first, ec);
      total -= s.second;
    }
  }

  void write(Json::object o)
  {
    if (!active() || !ensureSession())
      return;
    writeRaw(std::move(o));
  }

  void writeRaw(Json::object o)
  {
    if (!m_out.is_open())
      return;
    o["t"] = int(base::current_tick() - m_t0);
    o["time"] = wallClock("%H:%M:%S");
    m_out << Json(o).dump() << '\n';
    m_out.flush();
  }

  // ---------------------------------------------------------- PNG worker

  struct ShotJob {
    std::string path;
    int w = 0, h = 0;
    std::vector<uint8_t> rgb;
  };

  void startWorker()
  {
    if (m_worker.joinable())
      return;
    m_stopWorker = false;
    m_worker = std::thread([this] {
      for (;;) {
        std::unique_ptr<ShotJob> job;
        {
          std::unique_lock<std::mutex> lock(m_mutex);
          m_cv.wait(lock, [this] { return m_stopWorker || !m_jobs.empty(); });
          if (m_jobs.empty())
            return;
          job = std::move(m_jobs.front());
          m_jobs.pop_front();
        }
        writePng(job->path, job->w, job->h, job->rgb);
      }
    });
  }

  void pushJob(std::unique_ptr<ShotJob> job)
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_jobs.size() >= 8) // never let a slow disk pile up memory
      return;
    m_jobs.push_back(std::move(job));
    m_cv.notify_one();
  }

  void stopWorker()
  {
    {
      std::lock_guard<std::mutex> lock(m_mutex);
      m_stopWorker = true;
    }
    m_cv.notify_all();
    if (m_worker.joinable())
      m_worker.join(); // drains the queue first
  }

  struct ViewState {
    bool valid = false;
    double zoom = 0;
    gfx::Point scroll;
  };

  bool m_blockedByTests = false;
  bool m_shutdown = false;
  bool m_sessionFailed = false;
  std::ofstream m_out;
  std::string m_sessionDir;
  base::tick_t m_t0 = 0;
  ui::Timer m_timer;
  obs::connection m_before, m_after;
  DocUndo* m_history = nullptr;
  Doc* m_historyDoc = nullptr;
  std::string m_lastSite;
  bool m_buttonDown = false;
  base::tick_t m_lastDrag = 0;
  base::tick_t m_lastInput = 0;
  base::tick_t m_lastShot = 0;
  base::tick_t m_shotDue = 0;
  std::string m_shotReason;
  int m_shotCount = 0;
  Editor* m_lastEditor = nullptr;
  std::map<Editor*, ViewState> m_views;

  std::thread m_worker;
  std::mutex m_mutex;
  std::condition_variable m_cv;
  std::deque<std::unique_ptr<ShotJob>> m_jobs;
  bool m_stopWorker = false;
};

SessionRecorder* g_recorder = nullptr;

} // anonymous namespace

void start_session_recorder()
{
  if (g_recorder || !ui::Manager::getDefault())
    return;
  g_recorder = new SessionRecorder;
}

void stop_session_recorder()
{
  if (!g_recorder)
    return;
  g_recorder->shutdown();
  // Not deleted: it is a ui::Widget and the manager may still be tearing
  // down; a few bytes at exit are not worth a crash.
  g_recorder = nullptr;
}

void session_recorder_enabled_changed(bool enabled)
{
  if (g_recorder)
    g_recorder->enabledChanged(enabled);
}

bool session_recorder_add_note(const std::string& text)
{
  return g_recorder && g_recorder->addNote(text);
}

std::string session_recorder_root()
{
  ResourceFinder rf(false);
  rf.includeUserDir("usage");
  return rf.defaultFilename();
}

} // namespace app
