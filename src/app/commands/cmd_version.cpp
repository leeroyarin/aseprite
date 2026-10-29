// Aseprite 2.0
// Copyright (C) 2026 Antigravity / Aseprite 2.0 contributors
//
// This program is distributed under the terms of
// the End-User License Agreement for Aseprite.

#ifdef HAVE_CONFIG_H
  #include "config.h"
#endif

#include "app/app.h"
#include "app/commands/command.h"
#include "app/commands/commands.h"
#include "app/commands/params.h"
#include "app/context.h"
#include "app/doc.h"
#include "app/i18n/strings.h"
#include "app/site.h"
#include "app/ui/status_bar.h"
#include "app/versions/version_store.h"
#include "base/fs.h"
#include "fmt/format.h"
#include "os/sampling.h"
#include "ui/alert.h"
#include "ui/box.h"
#include "ui/button.h"
#include "ui/entry.h"
#include "ui/graphics.h"
#include "ui/label.h"
#include "ui/listbox.h"
#include "ui/listitem.h"
#include "ui/manager.h"
#include "ui/paint_event.h"
#include "ui/scale.h"
#include "ui/separator.h"
#include "ui/size_hint_event.h"
#include "ui/view.h"
#include "ui/window.h"

#include <algorithm>
#include <memory>

namespace app {

namespace {

class SaveVersionWindow : public ui::Window {
public:
  SaveVersionWindow()
    : ui::Window(ui::Window::WithTitleBar, "Save Version")
    , m_box()
    , m_label("Note / description for this version:")
    , m_entry(256, "")
    , m_btnBox()
    , m_okBtn("&OK")
    , m_cancelBtn("&Cancel")
    , m_confirmed(false)
  {
    m_box.addChild(&m_label);
    m_entry.setExpansive(true);
    m_box.addChild(&m_entry);

    m_btnBox.addChild(&m_okBtn);
    m_btnBox.addChild(&m_cancelBtn);
    m_box.addChild(&m_btnBox);

    addChild(&m_box);

    m_okBtn.Click.connect([this] {
      m_confirmed = true;
      closeWindow(&m_okBtn);
    });
    m_cancelBtn.Click.connect([this] {
      m_confirmed = false;
      closeWindow(&m_cancelBtn);
    });
  }

  bool isConfirmed() const { return m_confirmed; }
  std::string note() const { return m_entry.text(); }

private:
  ui::VBox m_box;
  ui::Label m_label;
  ui::Entry m_entry;
  ui::HBox m_btnBox;
  ui::Button m_okBtn;
  ui::Button m_cancelBtn;
  bool m_confirmed;
};

class VersionDiffWidget : public ui::Widget {
public:
  VersionDiffWidget(int prefW, int prefH)
    : ui::Widget(ui::kGenericWidget)
    , m_prefW(prefW)
    , m_prefH(prefH)
  {
    setMinSize(gfx::Size(m_prefW, m_prefH));
  }

  void setSurfaces(const os::SurfaceRef& verSurf,
                   const os::SurfaceRef& curSurf,
                   const os::SurfaceRef& diffSurf,
                   int verId,
                   const VersionDiffResult& stats)
  {
    m_verSurf = verSurf;
    m_curSurf = curSurf;
    m_diffSurf = diffSurf;
    m_verId = verId;
    m_stats = stats;
    invalidate();
  }

  void clearSurfaces()
  {
    m_verSurf.reset();
    m_curSurf.reset();
    m_diffSurf.reset();
    m_verId = 0;
    m_stats = VersionDiffResult();
    invalidate();
  }

protected:
  void onSizeHint(ui::SizeHintEvent& ev) override
  {
    ev.setSizeHint(gfx::Size(m_prefW, m_prefH));
  }

  void onPaint(ui::PaintEvent& ev) override
  {
    ui::Graphics* g = ev.graphics();
    gfx::Rect rc = clientBounds();
    int s = ui::guiscale();

    g->fillRect(gfx::rgba(24, 24, 32), rc);

    if (!m_verSurf || !m_curSurf || !m_diffSurf) {
      g->drawText("Select version to compare",
                  gfx::rgba(160, 160, 175),
                  gfx::ColorNone,
                  gfx::Point(rc.x + 6 * s, rc.y + rc.h / 2 - 6 * s));
      return;
    }

    int pad = 4 * s;
    int headerH = 14 * s;
    int boxW = (rc.w - pad * 4) / 3;
    int boxH = rc.h - headerH - pad;
    if (boxW < 12 || boxH < 12)
      return;

    int dispFrame = m_stats.comparedFrame + 1;

    auto drawPanel = [&](int index, const std::string& title, os::Surface* surf) {
      int bx = rc.x + pad + index * (boxW + pad);
      int by = rc.y + headerH;
      gfx::Rect boxRc(bx, by, boxW, boxH);

      g->drawText(title,
                  gfx::rgba(220, 225, 235),
                  gfx::ColorNone,
                  gfx::Point(bx, rc.y + 1 * s));

      // Fast 4-quadrant checkered backdrop (zero nested loops -> zero UI lag)
      int halfW = boxW / 2;
      int halfH = boxH / 2;
      gfx::Color darkCol = gfx::rgba(34, 34, 44);
      gfx::Color lightCol = gfx::rgba(44, 44, 56);
      g->fillRect(darkCol, boxRc);
      g->fillRect(lightCol, gfx::Rect(bx + halfW, by, boxW - halfW, halfH));
      g->fillRect(lightCol, gfx::Rect(bx, by + halfH, halfW, boxH - halfH));

      if (surf && surf->width() > 0 && surf->height() > 0) {
        int sw = surf->width();
        int sh = surf->height();
        int innerW = std::max(1, boxW - 4 * s);
        int innerH = std::max(1, boxH - 4 * s);
        double scale = std::min(double(innerW) / sw, double(innerH) / sh);
        if (scale >= 1.0)
          scale = std::floor(scale);
        int dw = std::max(1, int(sw * scale));
        int dh = std::max(1, int(sh * scale));
        int dx = bx + (boxW - dw) / 2;
        int dy = by + (boxH - dh) / 2;

        g->drawSurface(surf,
                       gfx::Rect(0, 0, sw, sh),
                       gfx::Rect(dx, dy, dw, dh),
                       os::Sampling(os::Sampling::Filter::Nearest),
                       nullptr);
      }

      g->drawRect(gfx::rgba(82, 88, 112), boxRc);
    };

    drawPanel(0, fmt::format("v{:04d} [F{}]", m_verId, dispFrame), m_verSurf.get());
    drawPanel(1, fmt::format("Now [F{}]", dispFrame), m_curSurf.get());
    drawPanel(2, fmt::format("Diff ({}px)", m_stats.changedPixels), m_diffSurf.get());
  }

private:
  int m_prefW;
  int m_prefH;
  os::SurfaceRef m_verSurf;
  os::SurfaceRef m_curSurf;
  os::SurfaceRef m_diffSurf;
  int m_verId = 0;
  VersionDiffResult m_stats;
};

class VersionHistoryWindow : public ui::Window {
public:
  VersionHistoryWindow(Context* ctx, Doc* doc)
    : ui::Window(ui::Window::WithTitleBar, "Version History & Visual Diff")
    , m_ctx(ctx)
    , m_doc(doc)
    , m_selectedFrame(ctx ? ctx->activeSite().frame() : doc::frame_t(0))
    , m_maxFrames(doc && doc->sprite() ? std::max(1, int(doc->sprite()->totalFrames())) : 1)
    , m_cachedVerId(-1)
    , m_mainBox()
    , m_splitBox()
    , m_view()
    , m_listbox()
    , m_rightBox()
    , m_frameBox()
    , m_frameTitleLabel("Frame:")
    , m_prevFrameBtn("<")
    , m_frameValueLabel("1 / 1")
    , m_nextFrameBtn(">")
    , m_diffWidget(calcRightWidth(), calcDiffHeight())
    , m_detailsLabel("Select a version to inspect")
    , m_actionBox()
    , m_restoreBtn("&Restore")
    , m_openCopyBtn("&Open Copy")
    , m_deleteBtn("&Delete")
    , m_closeBtn("&Close")
  {
    int s = ui::guiscale();
    int leftW = calcLeftWidth();
    int leftH = calcDiffHeight() + 54 * s;

    m_view.attachToView(&m_listbox);
    m_view.setMinSize(gfx::Size(leftW, leftH));
    m_view.setExpansive(true);

    m_splitBox.addChild(&m_view);

    // Frame selector row at top of right comparison pane
    m_prevFrameBtn.setMinSize(gfx::Size(22 * s, 16 * s));
    m_nextFrameBtn.setMinSize(gfx::Size(22 * s, 16 * s));
    m_frameBox.addChild(&m_frameTitleLabel);
    m_frameBox.addChild(&m_prevFrameBtn);
    m_frameBox.addChild(&m_frameValueLabel);
    m_frameBox.addChild(&m_nextFrameBtn);

    m_rightBox.addChild(&m_frameBox);
    m_rightBox.addChild(&m_diffWidget);
    m_rightBox.addChild(&m_detailsLabel);

    m_actionBox.addChild(&m_restoreBtn);
    m_actionBox.addChild(&m_openCopyBtn);
    m_actionBox.addChild(&m_deleteBtn);
    m_actionBox.addChild(&m_closeBtn);
    m_rightBox.addChild(&m_actionBox);

    m_splitBox.addChild(&m_rightBox);
    m_mainBox.addChild(&m_splitBox);

    addChild(&m_mainBox);

    m_listbox.Change.connect([this] { onVersionSelected(); });
    m_prevFrameBtn.Click.connect([this] { onStepFrame(-1); });
    m_nextFrameBtn.Click.connect([this] { onStepFrame(1); });

    m_restoreBtn.Click.connect([this] { onRestore(); });
    m_openCopyBtn.Click.connect([this] { onOpenCopy(); });
    m_deleteBtn.Click.connect([this] { onDelete(); });
    m_closeBtn.Click.connect([this] { closeWindow(&m_closeBtn); });

    refreshList();
  }

  ~VersionHistoryWindow() override
  {
    while (ui::Widget* child = m_listbox.firstChild()) {
      m_listbox.removeChild(child);
      delete child;
    }
    m_cachedVerDoc.reset();
  }

private:
  static int availScreenWidth()
  {
    int s = ui::guiscale();
    if (auto* mgr = ui::Manager::getDefault()) {
      int w = mgr->bounds().w;
      if (w > 200 * s)
        return w;
    }
    return 440 * s;
  }

  static int calcLeftWidth()
  {
    int s = ui::guiscale();
    int totalBudget = int(availScreenWidth() * 0.86);
    int leftW = int(totalBudget * 0.36);
    return std::clamp(leftW, 115 * s, 175 * s);
  }

  static int calcRightWidth()
  {
    int s = ui::guiscale();
    int totalBudget = int(availScreenWidth() * 0.86);
    int rightW = int(totalBudget * 0.56);
    return std::clamp(rightW, 195 * s, 270 * s);
  }

  static int calcDiffHeight()
  {
    int s = ui::guiscale();
    return 96 * s;
  }

  void refreshList()
  {
    while (ui::Widget* child = m_listbox.firstChild()) {
      m_listbox.removeChild(child);
      delete child;
    }

    m_versions.clear();
    m_diffSurfaceCache.clear();
    m_cachedVerDoc.reset();
    m_cachedVerId = -1;

    if (m_doc && m_doc->isAssociatedToFile()) {
      VersionStore::listVersions(m_doc->filename(), m_versions);
    }

    for (const auto& v : m_versions) {
      std::string shortTime = (v.created.size() >= 16) ? v.created.substr(5, 11) : v.created;
      std::string labelText = fmt::format("v{:04d} {} {}",
                                          v.id,
                                          shortTime,
                                          v.note.empty() ? "" : ("- " + v.note));
      ui::ListItem* item = new ui::ListItem(labelText);
      m_listbox.addChild(item);
    }

    if (!m_versions.empty()) {
      m_listbox.selectIndex(static_cast<int>(m_versions.size()) - 1);
    }
    onVersionSelected();
  }

  void onStepFrame(int delta)
  {
    if (m_maxFrames <= 1)
      return;
    int next = (int(m_selectedFrame) + delta) % m_maxFrames;
    if (next < 0)
      next += m_maxFrames;
    m_selectedFrame = doc::frame_t(next);
    updateDiffDisplay();
  }

  void onVersionSelected()
  {
    int idx = m_listbox.getSelectedIndex();
    if (idx >= 0 && idx < static_cast<int>(m_versions.size())) {
      const auto& v = m_versions[idx];
      if (m_cachedVerId != v.id || !m_cachedVerDoc) {
        m_cachedVerDoc.reset(VersionStore::loadVersionSnapshot(m_doc->filename(), v.id));
        m_cachedVerId = v.id;
      }
      int curFrames = (m_doc && m_doc->sprite()) ? int(m_doc->sprite()->totalFrames()) : 1;
      int verFrames = (m_cachedVerDoc && m_cachedVerDoc->sprite())
        ? int(m_cachedVerDoc->sprite()->totalFrames())
        : v.frames;
      m_maxFrames = std::max({ 1, curFrames, verFrames });
      if (m_selectedFrame >= m_maxFrames)
        m_selectedFrame = doc::frame_t(m_maxFrames - 1);
    }
    else {
      m_cachedVerDoc.reset();
      m_cachedVerId = -1;
    }
    updateDiffDisplay();
  }

  void updateDiffDisplay()
  {
    m_frameValueLabel.setText(fmt::format(" {} / {} ", int(m_selectedFrame) + 1, m_maxFrames));
    m_prevFrameBtn.setEnabled(m_maxFrames > 1);
    m_nextFrameBtn.setEnabled(m_maxFrames > 1);

    int idx = m_listbox.getSelectedIndex();
    if (idx >= 0 && idx < static_cast<int>(m_versions.size())) {
      const auto& v = m_versions[idx];

      os::SurfaceRef verSurf, curSurf, diffSurf;
      VersionDiffResult diffStats;
      bool hasDiff = false;

      // Fast lookup in per-(versionId, frame) surface cache
      for (const auto& entry : m_diffSurfaceCache) {
        if (entry.verId == v.id && entry.frame == m_selectedFrame) {
          verSurf = entry.verSurf;
          curSurf = entry.curSurf;
          diffSurf = entry.diffSurf;
          diffStats = entry.stats;
          hasDiff = true;
          break;
        }
      }

      if (!hasDiff && m_cachedVerDoc) {
        hasDiff = VersionStore::computeVisualDiff(
          m_doc, m_cachedVerDoc.get(), m_selectedFrame, verSurf, curSurf, diffSurf, diffStats);
        if (hasDiff) {
          if (m_diffSurfaceCache.size() >= 32)
            m_diffSurfaceCache.erase(m_diffSurfaceCache.begin());
          m_diffSurfaceCache.push_back(
            CachedDiffSurfaces{ v.id, m_selectedFrame, verSurf, curSurf, diffSurf, diffStats });
        }
      }

      if (hasDiff) {
        m_diffWidget.setSurfaces(verSurf, curSurf, diffSurf, v.id, diffStats);
      }
      else {
        m_diffWidget.clearSurfaces();
      }

      double pct = (diffStats.totalPixels > 0)
        ? (100.0 * double(diffStats.changedPixels) / double(diffStats.totalPixels))
        : 0.0;

      std::string noteStr = v.note.empty() ? "(none)" : v.note;
      if (noteStr.size() > 36)
        noteStr = noteStr.substr(0, 33) + "...";

      std::string info = fmt::format(
        "v{:04d} ({}) [{}] | {}x{} | {}f/{}L | {:.1f}KB\n"
        "Note: {}\n"
        "Frame {} Diff: {} px ({:.1f}%) [+{}/-{}/~{}]",
        v.id,
        v.created,
        v.author,
        v.width, v.height,
        v.frames, v.layers,
        v.bytes / 1024.0,
        noteStr,
        int(m_selectedFrame) + 1,
        diffStats.changedPixels,
        pct,
        diffStats.addedPixels,
        diffStats.removedPixels,
        diffStats.modifiedPixels);

      m_detailsLabel.setText(info);
      m_restoreBtn.setEnabled(true);
      m_openCopyBtn.setEnabled(true);
      m_deleteBtn.setEnabled(true);
    }
    else {
      m_diffWidget.clearSurfaces();
      m_detailsLabel.setText("No versions saved for this file yet.");
      m_restoreBtn.setEnabled(false);
      m_openCopyBtn.setEnabled(false);
      m_deleteBtn.setEnabled(false);
    }
    layout();
  }

  void onRestore()
  {
    int idx = m_listbox.getSelectedIndex();
    if (idx < 0 || idx >= static_cast<int>(m_versions.size()))
      return;

    int verId = m_versions[idx].id;
    int res = ui::Alert::show(fmt::format(
      "Restore Version<<Restore version #{:04d}?\nCurrent unsaved changes will be backed up automatically before restoring.||&Restore||&Cancel",
      verId));
    if (res != 1)
      return;

    int backupId = 0;
    if (VersionStore::restoreVersion(m_ctx, m_doc, verId, backupId)) {
      StatusBar::instance()->setStatusText(2000, fmt::format("Restored v{:04d} (backup v{:04d} created)", verId, backupId));
      closeWindow(&m_restoreBtn);
    }
    else {
      ui::Alert::show("Error<<Failed to restore version.||&OK");
    }
  }

  void onOpenCopy()
  {
    int idx = m_listbox.getSelectedIndex();
    if (idx < 0 || idx >= static_cast<int>(m_versions.size()))
      return;

    int verId = m_versions[idx].id;
    if (VersionStore::openVersion(m_ctx, m_doc->filename(), verId)) {
      closeWindow(&m_openCopyBtn);
    }
    else {
      ui::Alert::show("Error<<Failed to open version as copy.||&OK");
    }
  }

  void onDelete()
  {
    int idx = m_listbox.getSelectedIndex();
    if (idx < 0 || idx >= static_cast<int>(m_versions.size()))
      return;

    int verId = m_versions[idx].id;
    int res = ui::Alert::show(fmt::format(
      "Delete Version<<Are you sure you want to delete version #{:04d}?||&Delete||&Cancel",
      verId));
    if (res != 1)
      return;

    VersionStore::deleteVersion(m_doc->filename(), verId);
    refreshList();
  }

  struct CachedDiffSurfaces {
    int verId = 0;
    doc::frame_t frame = 0;
    os::SurfaceRef verSurf;
    os::SurfaceRef curSurf;
    os::SurfaceRef diffSurf;
    VersionDiffResult stats;
  };

  Context* m_ctx;
  Doc* m_doc;
  doc::frame_t m_selectedFrame;
  int m_maxFrames;
  int m_cachedVerId;
  std::unique_ptr<Doc> m_cachedVerDoc;
  std::vector<VersionEntry> m_versions;
  std::vector<CachedDiffSurfaces> m_diffSurfaceCache;

  // IMPORTANT: Parent widgets MUST be declared BEFORE their child widgets
  // so C++ reverse-order destruction destroys children first (removing them from
  // their parent's m_children list before parent ~Widget() runs).
  ui::VBox m_mainBox;
  ui::HBox m_splitBox;
  ui::View m_view;
  ui::ListBox m_listbox;
  ui::VBox m_rightBox;
  ui::HBox m_frameBox;
  ui::Label m_frameTitleLabel;
  ui::Button m_prevFrameBtn;
  ui::Label m_frameValueLabel;
  ui::Button m_nextFrameBtn;
  VersionDiffWidget m_diffWidget;
  ui::Label m_detailsLabel;
  ui::HBox m_actionBox;
  ui::Button m_restoreBtn;
  ui::Button m_openCopyBtn;
  ui::Button m_deleteBtn;
  ui::Button m_closeBtn;
};

} // anonymous namespace

class SaveVersionCommand : public Command {
public:
  SaveVersionCommand() : Command(CommandId::SaveVersion()) {}

protected:
  void onLoadParams(const Params& params) override
  {
    m_note = params.get("note");
    m_author = params.get("author");
    if (params.has_param("ui"))
      m_ui = (params.get("ui") == "true" || params.get("ui") == "1");
    else
      m_ui = true;
  }

  bool onEnabled(Context* context) override
  {
    return context && context->checkFlags(ContextFlags::ActiveDocumentIsReadable);
  }

  void onExecute(Context* context) override
  {
    Doc* doc = context->activeDocument();
    if (!doc)
      return;

    // Unsaved new documents: prompt Save As first
    if (!doc->isAssociatedToFile() || doc->filename().empty()) {
      if (context->isUIAvailable()) {
        Command* saveAsCmd = Commands::instance()->byId(CommandId::SaveFileAs());
        if (saveAsCmd)
          context->executeCommand(saveAsCmd);
      }
      if (!doc->isAssociatedToFile() || doc->filename().empty())
        return;
    }

    std::string note = m_note;
    std::string author = m_author.empty() ? "user" : m_author;

    if (m_ui && context->isUIAvailable()) {
      SaveVersionWindow win;
      win.centerWindow();
      win.openWindowInForeground();
      if (!win.isConfirmed())
        return;
      note = win.note();
    }

    VersionEntry entry;
    if (VersionStore::saveVersion(context, doc, note, author, entry)) {
      if (context->isUIAvailable()) {
        StatusBar::instance()->setStatusText(2000, fmt::format("Version #{:04d} saved", entry.id));
      }
    }
    else {
      if (context->isUIAvailable()) {
        ui::Alert::show("Error<<Failed to save project version.||&OK");
      }
    }
  }

private:
  std::string m_note;
  std::string m_author;
  bool m_ui = true;
};

class VersionHistoryCommand : public Command {
public:
  VersionHistoryCommand() : Command(CommandId::VersionHistory()) {}

protected:
  bool onEnabled(Context* context) override
  {
    return context && context->checkFlags(ContextFlags::ActiveDocumentIsReadable);
  }

  void onExecute(Context* context) override
  {
    Doc* doc = context->activeDocument();
    if (!doc)
      return;

    if (!doc->isAssociatedToFile() || doc->filename().empty()) {
      if (context->isUIAvailable()) {
        ui::Alert::show("Version History<<Please save this document before viewing version history.||&OK");
      }
      return;
    }

    if (context->isUIAvailable()) {
      VersionHistoryWindow win(context, doc);
      win.centerWindow();
      win.openWindowInForeground();
    }
  }
};

class RestoreVersionCommand : public Command {
public:
  RestoreVersionCommand() : Command(CommandId::RestoreVersion()) {}

protected:
  void onLoadParams(const Params& params) override
  {
    m_id = std::atoi(params.get("id").c_str());
  }

  bool onEnabled(Context* context) override
  {
    return context && context->checkFlags(ContextFlags::ActiveDocumentIsReadable);
  }

  void onExecute(Context* context) override
  {
    Doc* doc = context->activeDocument();
    if (!doc || m_id <= 0)
      return;

    int backupId = 0;
    VersionStore::restoreVersion(context, doc, m_id, backupId);
  }

private:
  int m_id = 0;
};

class OpenVersionCommand : public Command {
public:
  OpenVersionCommand() : Command(CommandId::OpenVersion()) {}

protected:
  void onLoadParams(const Params& params) override
  {
    m_id = std::atoi(params.get("id").c_str());
  }

  bool onEnabled(Context* context) override
  {
    return context && context->checkFlags(ContextFlags::ActiveDocumentIsReadable);
  }

  void onExecute(Context* context) override
  {
    Doc* doc = context->activeDocument();
    if (!doc || m_id <= 0)
      return;

    VersionStore::openVersion(context, doc->filename(), m_id);
  }

private:
  int m_id = 0;
};

class DeleteVersionCommand : public Command {
public:
  DeleteVersionCommand() : Command(CommandId::DeleteVersion()) {}

protected:
  void onLoadParams(const Params& params) override
  {
    m_id = std::atoi(params.get("id").c_str());
  }

  bool onEnabled(Context* context) override
  {
    return context && context->checkFlags(ContextFlags::ActiveDocumentIsReadable);
  }

  void onExecute(Context* context) override
  {
    Doc* doc = context->activeDocument();
    if (!doc || m_id <= 0)
      return;

    VersionStore::deleteVersion(doc->filename(), m_id);
  }

private:
  int m_id = 0;
};

Command* CommandFactory::createSaveVersionCommand()
{
  return new SaveVersionCommand;
}

Command* CommandFactory::createVersionHistoryCommand()
{
  return new VersionHistoryCommand;
}

Command* CommandFactory::createRestoreVersionCommand()
{
  return new RestoreVersionCommand;
}

Command* CommandFactory::createOpenVersionCommand()
{
  return new OpenVersionCommand;
}

Command* CommandFactory::createDeleteVersionCommand()
{
  return new DeleteVersionCommand;
}

} // namespace app
