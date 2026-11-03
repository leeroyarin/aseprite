// Aseprite 2.0
// Copyright (C) 2026 Antigravity / Aseprite 2.0 contributors
//
// This program is distributed under the terms of
// the End-User License Agreement for Aseprite.

#ifdef HAVE_CONFIG_H
  #include "config.h"
#endif

#include "app/app.h"
#include "app/cmd/add_slice.h"
#include "app/cmd/add_tag.h"
#include "app/cmd/set_slice_key.h"
#include "app/commands/command.h"
#include "app/commands/commands.h"
#include "app/commands/easymetric_model.h"
#include "app/commands/easymetric_render.h"
#include "app/commands/easymetric_sheets.h"
#include "app/commands/easymetric_ui.h"
#include "app/commands/easymetric_volume.h"
#include "app/console.h"
#include "app/context.h"
#include "app/doc.h"
#include "app/doc_api.h"
#include "app/extra_cel.h"
#include "app/file/file.h"
#include "app/file_selector.h"
#include "app/pref/preferences.h"
#include "app/site.h"
#include "app/tx.h"
#include "app/ui/doc_view.h"
#include "app/ui/editor/editor.h"
#include "app/ui/editor/editor_view.h"
#include "app/ui/editor/standby_state.h"
#include "app/ui/skin/skin_theme.h"
#include "app/ui/status_bar.h"
#include "app/ui_context.h"
#include "app/util/conversion_to_surface.h"
#include "base/fs.h"
#include "doc/cel.h"
#include "doc/cels_range.h"
#include "doc/color.h"
#include "doc/frames_sequence.h"
#include "doc/image.h"
#include "doc/layer.h"
#include "doc/layer_smart_object.h"
#include "doc/mask.h"
#include "doc/primitives.h"
#include "doc/slice.h"
#include "doc/sprite.h"
#include "doc/tag.h"
#include "doc/tags.h"
#include "fmt/format.h"
#include "os/sampling.h"
#include "os/surface.h"
#include "os/system.h"
#include "render/render.h"
#include "ui/box.h"
#include "ui/button.h"
#include "ui/combobox.h"
#include "ui/graphics.h"
#include "ui/label.h"
#include "ui/manager.h"
#include "ui/message.h"
#include "ui/paint_event.h"
#include "ui/resize_event.h"
#include "ui/scale.h"
#include "ui/separator.h"
#include "ui/size_hint_event.h"
#include "ui/slider.h"
#include "ui/system.h"
#include "ui/timer.h"
#include "ui/view.h"
#include "ui/widget.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace app {

namespace {

using namespace easymetric;

class LinkToggleButton : public ui::Button {
public:
  LinkToggleButton() : ui::Button(""), m_linked(false) { setFocusStop(false); }

  bool isLinked() const { return m_linked; }

  void setLinked(bool linked)
  {
    if (m_linked != linked) {
      m_linked = linked;
      setSelected(linked);
      invalidate();
    }
  }

protected:
  void onSizeHint(ui::SizeHintEvent& ev) override
  {
    const int s = ui::guiscale();
    ev.setSizeHint(gfx::Size(20 * s, 14 * s));
  }

  void onPaint(ui::PaintEvent& ev) override
  {
    ui::Graphics* g = ev.graphics();
    const gfx::Rect rc = clientBounds();
    const int s = ui::guiscale();
    auto* theme = skin::SkinTheme::instance();

    gfx::Color bg, borderCol, iconCol;
    if (m_linked) {
      bg = hasMouse() ? gfx::rgba(14, 165, 233) : gfx::rgba(2, 132, 199);
      borderCol = gfx::rgba(56, 189, 248);
      iconCol = gfx::rgba(255, 255, 255);
    }
    else {
      bg = hasMouse() ? gfx::rgba(100, 116, 139) : gfx::rgba(64, 78, 94);
      borderCol = hasMouse() ? gfx::rgba(148, 163, 184) : gfx::rgba(45, 55, 66);
      iconCol = hasMouse() ? gfx::rgba(241, 245, 249) : gfx::rgba(180, 195, 210);
    }

    g->fillRect(bg, rc);
    g->drawRect(borderCol, rc);

    // Draw link icon from theme part
    os::Surface* icon = nullptr;
    if (theme) {
      const auto& part = theme->parts.iconAspectRatio();
      if (part)
        icon = part->bitmap(0);
    }

    if (icon) {
      const int ix = rc.x + (rc.w - icon->width()) / 2;
      const int iy = rc.y + (rc.h - icon->height()) / 2;
      g->drawColoredRgbaSurface(icon, iconCol, ix, iy);
    }
    else {
      const int cx = rc.x + rc.w / 2;
      const int cy = rc.y + rc.h / 2;
      g->drawRect(iconCol, gfx::Rect(cx - 5 * s, cy - 2 * s, 5 * s, 4 * s));
      g->drawRect(iconCol, gfx::Rect(cx - 1 * s, cy - 2 * s, 5 * s, 4 * s));
      g->drawHLine(iconCol, cx - 2 * s, cy, 3 * s);
    }
  }

private:
  bool m_linked;
};

// Folder-tab ear header bar matching reference UI
class PaneHeader : public ui::Widget {
public:
  PaneHeader(const std::string& title, gfx::Color accentCol = gfx::ColorNone)
    : ui::Widget(ui::kGenericWidget)
    , m_title(title)
    , m_accentCol(accentCol)
    , m_active(false)
    , m_linkBtn(nullptr)
  {
    setFocusStop(false);
  }

  void setTitle(const std::string& title)
  {
    m_title = title;
    invalidate();
  }

  const std::string& title() const { return m_title; }

  void setActive(bool active)
  {
    if (m_active != active) {
      m_active = active;
      invalidate();
    }
  }

  void setLinkButton(LinkToggleButton* btn)
  {
    m_linkBtn = btn;
    if (btn)
      addChild(btn);
  }

protected:
  void onSizeHint(ui::SizeHintEvent& ev) override
  {
    ev.setSizeHint(gfx::Size(64 * ui::guiscale(), 16 * ui::guiscale()));
  }

  void onResize(ui::ResizeEvent& ev) override
  {
    setBoundsQuietly(ev.bounds());
    if (m_linkBtn) {
      const gfx::Rect rc = childrenBounds();
      const int s = ui::guiscale();
      const int btnW = 20 * s;
      const int btnH = 14 * s;
      m_linkBtn->setBounds(
        gfx::Rect(rc.x + rc.w - btnW - 3 * s, rc.y + (rc.h - btnH) / 2, btnW, btnH));
    }
  }

  void onPaint(ui::PaintEvent& ev) override
  {
    ui::Graphics* g = ev.graphics();
    const gfx::Rect rc = clientBounds();
    const int s = ui::guiscale();

    const gfx::Size textSz = g->measureText(m_title);
    const int tabW = std::min(rc.w, std::max(textSz.w + 14 * s, 48 * s));
    const gfx::Rect tabRc(rc.x, rc.y, tabW, rc.h);

    // Slate blue header bar background
    g->fillRect(gfx::rgba(92, 108, 122), rc);
    // Raised folder tab ear
    g->fillRect(m_active ? gfx::rgba(125, 142, 160) : gfx::rgba(105, 122, 138), tabRc);
    g->drawRect(gfx::rgba(28, 35, 43), rc);

    if (m_accentCol != gfx::ColorNone) {
      g->fillRect(m_accentCol, gfx::Rect(tabRc.x + 1, tabRc.y + 1, 3 * s, tabRc.h - 2));
    }

    // Grip dots ":::"
    const int rightReserve = (m_linkBtn ? 26 * s : 4 * s);
    if (rc.w > tabW + rightReserve + 16 * s) {
      const int availableSpace = rc.w - tabW - rightReserve;
      const int gx = rc.x + tabW + availableSpace / 2 - 4 * s;
      const int gy = rc.y + rc.h / 2 - 1 * s;
      const gfx::Color dotCol = gfx::rgba(45, 55, 66);
      for (int dx = 0; dx < 3; ++dx) {
        g->fillRect(dotCol, gfx::Rect(gx + dx * 3 * s, gy, s, s));
        g->fillRect(dotCol, gfx::Rect(gx + dx * 3 * s, gy + 2 * s, s, s));
      }
    }

    g->drawText(m_title,
                gfx::rgba(255, 255, 255),
                gfx::ColorNone,
                gfx::Point(tabRc.x + (m_accentCol != gfx::ColorNone ? 6 * s : 4 * s),
                           tabRc.y + (rc.h - textSz.h) / 2));
  }

private:
  std::string m_title;
  gfx::Color m_accentCol;
  bool m_active;
  LinkToggleButton* m_linkBtn;
};

class FacePaneBox : public ui::VBox {
public:
  FacePaneBox(int faceIndex, DocView* docView, Editor* editor)
    : m_faceIndex(faceIndex)
    , m_docView(docView)
    , m_editor(editor)
  {
  }

protected:
  bool onProcessMessage(ui::Message* msg) override
  {
    if (msg->type() == ui::kMouseDownMessage) {
      if (m_docView && m_docView->document() && m_docView->document()->sprite()) {
        doc::Sprite* spr = m_docView->document()->sprite();
        // Shared sheets: every panel edits the same Texture (or
        // Base) layer; only the editor's clip rect (its UV rect,
        // set in initLayersAndFraming) differs per panel.
        SharedSurfaceLayers shared = findSharedLayers(spr);
        doc::LayerImage* lyr = nullptr;
        if (shared.textureLayer && !shared.textureLayer->isSmartObject())
          lyr = shared.textureLayer;
        else if (shared.baseLayer)
          lyr = shared.baseLayer;
        else if (shared.textureLayer)
          lyr = shared.textureLayer;
        if (!lyr) {
          SurfaceLayerPair sp = findSurfaceLayers(spr, m_faceIndex);
          if (sp.texLayer && !sp.texLayer->isSmartObject())
            lyr = sp.texLayer;
          else if (sp.baseLayer)
            lyr = sp.baseLayer;
          else if (sp.texLayer)
            lyr = sp.texLayer;
          if (!lyr) {
            const std::string lyrName = std::string("Surface_") + kFaceSpecs[m_faceIndex].sliceName;
            lyr = findLayerByName(spr, lyrName);
          }
        }
        if (lyr) {
          m_editor->setLayer(lyr);
          if (m_docView->editor() != m_editor) {
            m_docView->setActivePaneEditor(m_editor);
          }
          if (UIContext* ctx = UIContext::instance())
            ctx->setActiveView(m_docView);
        }
      }
    }
    return ui::VBox::onProcessMessage(msg);
  }

private:
  int m_faceIndex;
  DocView* m_docView;
  Editor* m_editor;
};

// Checkerboard helper
static void drawCheckerboardPattern(ui::Graphics* g, const gfx::Rect& rc)
{
  const int tile = 12 * ui::guiscale();
  const gfx::Color c1 = gfx::rgba(128, 128, 128);
  const gfx::Color c2 = gfx::rgba(192, 192, 192);
  g->fillRect(c1, rc);
  for (int y = rc.y; y < rc.y2(); y += tile) {
    const int row = (y - rc.y) / tile;
    const int h = std::min(tile, rc.y2() - y);
    for (int x = rc.x; x < rc.x2(); x += tile) {
      const int col = (x - rc.x) / tile;
      if (((row + col) & 1) == 1) {
        const int w = std::min(tile, rc.x2() - x);
        g->fillRect(c2, gfx::Rect(x, y, w, h));
      }
    }
  }
}

// 3D Preview Widget (Top of Column 4)
class EasymetricPreviewWidget : public ui::Widget {
public:
  EasymetricPreviewWidget() : ui::Widget(ui::kGenericWidget) { setExpansive(true); }

  obs::signal<void(int, int)> OrbitDrag;
  obs::signal<void(double)> ZoomChanged;

  // Zoom of the rendered model, relative to the fit-to-pane size.
  // 1.0 = fit, 0.25 = quarter size, 8.0 = 8x. Wheel, +/- buttons and
  // double-click (reset) all go through setZoom() so the value is
  // always clamped and observers always see the same value.
  double zoom() const { return m_zoom; }

  void setZoom(double zoom)
  {
    const double clamped = std::clamp(zoom, 0.25, 8.0);
    if (std::abs(clamped - m_zoom) < 1e-9)
      return;
    m_zoom = clamped;
    ZoomChanged(m_zoom);
    invalidate();
  }

  void zoomIn() { setZoom(m_zoom * 1.25); }
  void zoomOut() { setZoom(m_zoom / 1.25); }

  void resetZoom()
  {
    m_panX = 0;
    m_panY = 0;
    setZoom(1.0);
    // setZoom() skips invalidate() when already at 1.0, but the pan
    // reset above still needs a repaint in that case.
    invalidate();
  }

  void setSurfaces(const os::SurfaceRef& diffSurf,
                   const os::SurfaceRef& normSurf,
                   const os::SurfaceRef& deptSurf,
                   int yawDeg,
                   int pitchDeg,
                   int pivScreenX,
                   int pivScreenY,
                   bool showDiff,
                   bool showNorm,
                   bool showDept)
  {
    m_diffSurf = diffSurf;
    m_normSurf = normSurf;
    m_deptSurf = deptSurf;
    m_yawDeg = yawDeg;
    m_pitchDeg = pitchDeg;
    m_pivScreenX = pivScreenX;
    m_pivScreenY = pivScreenY;
    m_showDiff = showDiff;
    m_showNorm = showNorm;
    m_showDept = showDept;
    invalidate();
  }

protected:
  bool onProcessMessage(ui::Message* msg) override
  {
    switch (msg->type()) {
      case ui::kMouseDownMessage: {
        auto* mouseMsg = static_cast<ui::MouseMessage*>(msg);
        if (mouseMsg->left()) {
          m_dragging = true;
          m_dragStart = mouseMsg->position();
          m_startYaw = m_yawDeg;
          m_startPitch = m_pitchDeg;
          captureMouse();
          return true;
        }
        if (mouseMsg->right() || mouseMsg->middle()) {
          // Pan the zoomed rendering; left-drag stays reserved for orbit.
          m_panning = true;
          m_panStart = mouseMsg->position();
          m_startPanX = m_panX;
          m_startPanY = m_panY;
          captureMouse();
          return true;
        }
        break;
      }
      case ui::kMouseMoveMessage: {
        if (m_panning) {
          auto* mouseMsg = static_cast<ui::MouseMessage*>(msg);
          const gfx::Point pos = mouseMsg->position();
          m_panX = m_startPanX + (pos.x - m_panStart.x);
          m_panY = m_startPanY + (pos.y - m_panStart.y);
          invalidate();
          return true;
        }
        if (m_dragging) {
          auto* mouseMsg = static_cast<ui::MouseMessage*>(msg);
          const gfx::Point pos = mouseMsg->position();
          const int s = std::max(1, ui::guiscale());
          const int dYaw = (pos.x - m_dragStart.x) / s;
          const int dPitch = (pos.y - m_dragStart.y) / s;
          int newYaw = m_startYaw + dYaw;
          while (newYaw > 180)
            newYaw -= 360;
          while (newYaw < -180)
            newYaw += 360;
          int newPitch = std::clamp(m_startPitch + dPitch, -75, 75);
          OrbitDrag(newYaw, newPitch);
          return true;
        }
        break;
      }
      case ui::kMouseUpMessage: {
        if (m_dragging || m_panning) {
          m_dragging = false;
          m_panning = false;
          releaseMouse();
          return true;
        }
        break;
      }
      case ui::kMouseWheelMessage: {
        auto* mouseMsg = static_cast<ui::MouseMessage*>(msg);
        const gfx::Point delta = mouseMsg->wheelDelta();
        // Shift+wheel scrolls (pans) instead of zooming, so the
        // preview can be scrolled as well as zoomed.
        if (mouseMsg->shiftPressed()) {
          m_panX += delta.x * 8;
          m_panY -= delta.y * 8;
          invalidate();
          return true;
        }
        // Same sign convention as palette_view: delta.x - delta.y > 0
        // means wheel-up, which zooms in.
        const int steps = delta.x - delta.y;
        if (steps != 0) {
          setZoom(m_zoom * std::pow(1.25, steps));
          return true;
        }
        break;
      }
      case ui::kDoubleClickMessage: {
        resetZoom();
        return true;
      }
    }
    return ui::Widget::onProcessMessage(msg);
  }

  void onPaint(ui::PaintEvent& ev) override
  {
    ui::Graphics* g = ev.graphics();
    const gfx::Rect rc = clientBounds();
    const int s = ui::guiscale();

    drawCheckerboardPattern(g, rc);
    g->drawRect(gfx::rgba(28, 35, 43), rc);

    std::vector<os::SurfaceRef> activeSurfs;
    std::vector<const char*> activeLabels;
    if (m_showDiff && m_diffSurf) {
      activeSurfs.push_back(m_diffSurf);
      activeLabels.push_back("Diffuse");
    }
    if (m_showNorm && m_normSurf) {
      activeSurfs.push_back(m_normSurf);
      activeLabels.push_back("Normal");
    }
    if (m_showDept && m_deptSurf) {
      activeSurfs.push_back(m_deptSurf);
      activeLabels.push_back("Depth");
    }
    if (activeSurfs.empty() && m_diffSurf) {
      activeSurfs.push_back(m_diffSurf);
      activeLabels.push_back("Diffuse");
    }

    if (!activeSurfs.empty()) {
      const int count = int(activeSurfs.size());
      const int cellW = rc.w / count;

      for (int i = 0; i < count; ++i) {
        const gfx::Rect cellRc(rc.x + i * cellW, rc.y, cellW, rc.h);
        if (i > 0)
          g->drawVLine(gfx::rgba(28, 35, 43, 180), cellRc.x, cellRc.y, cellRc.h);

        const int sw = activeSurfs[i]->width();
        const int sh = activeSurfs[i]->height();
        if (sw > 0 && sh > 0) {
          const double scaleX = double(cellRc.w - 8 * s) / double(sw);
          const double scaleY = double(cellRc.h - 8 * s) / double(sh);
          const double scale = std::max(0.5, std::min(scaleX, scaleY)) * m_zoom;
          const int dw = std::max(1, int(sw * scale));
          const int dh = std::max(1, int(sh * scale));
          const int dx = cellRc.x + (cellRc.w - dw) / 2 + m_panX;
          const int dy = cellRc.y + (cellRc.h - dh) / 2 + m_panY;

          // Keep a zoomed rendering inside its own cell instead of
          // painting over the neighbouring map cells.
          ui::IntersectClip clip(g, cellRc);
          if (clip) {
            g->drawSurface(activeSurfs[i].get(),
                           gfx::Rect(0, 0, sw, sh),
                           gfx::Rect(dx, dy, dw, dh),
                           os::Sampling(os::Sampling::Filter::Nearest),
                           nullptr);

            if (i == 0) {
              const int px = dx + int(m_pivScreenX * scale);
              const int py = dy + int(m_pivScreenY * scale);
              if (cellRc.contains(gfx::Point(px, py))) {
                const gfx::Color pivCol = gfx::rgba(250, 204, 21, 230);
                g->drawHLine(pivCol, px - 4 * s, py, 9 * s);
                g->drawVLine(pivCol, px, py - 4 * s, 9 * s);
              }
            }
          }
        }

        const gfx::Size lblSz = g->measureText(activeLabels[i]);
        const gfx::Rect badgeRc(cellRc.x + 3 * s,
                                cellRc.y + 3 * s,
                                lblSz.w + 6 * s,
                                lblSz.h + 2 * s);
        g->fillRect(gfx::rgba(28, 35, 43, 195), badgeRc);
        g->drawText(activeLabels[i],
                    gfx::rgba(255, 255, 255),
                    gfx::ColorNone,
                    gfx::Point(badgeRc.x + 3 * s, badgeRc.y + 1 * s));
      }
    }

    if (std::abs(m_zoom - 1.0) > 1e-9) {
      const std::string zoomText = fmt::format("{}%", int(std::round(m_zoom * 100.0)));
      const gfx::Size zoomSz = g->measureText(zoomText);
      const gfx::Rect zoomRc(rc.x + rc.w - zoomSz.w - 9 * s,
                             rc.y + 3 * s,
                             zoomSz.w + 6 * s,
                             zoomSz.h + 2 * s);
      g->fillRect(gfx::rgba(28, 35, 43, 195), zoomRc);
      g->drawText(zoomText,
                  gfx::rgba(255, 255, 255),
                  gfx::ColorNone,
                  gfx::Point(zoomRc.x + 3 * s, zoomRc.y + 1 * s));
    }
  }

private:
  os::SurfaceRef m_diffSurf;
  os::SurfaceRef m_normSurf;
  os::SurfaceRef m_deptSurf;
  int m_yawDeg = 45;
  int m_pitchDeg = 30;
  int m_pivScreenX = 40;
  int m_pivScreenY = 40;
  bool m_showDiff = true;
  bool m_showNorm = false;
  bool m_showDept = false;
  bool m_dragging = false;
  gfx::Point m_dragStart;
  int m_startYaw = 45;
  int m_startPitch = 30;
  double m_zoom = 1.0;
  int m_panX = 0;
  int m_panY = 0;
  bool m_panning = false;
  gfx::Point m_panStart;
  int m_startPanX = 0;
  int m_startPanY = 0;
};

// Sleek dark-slate container matching reference UI controls
class FieldsPanelBox : public ui::VBox {
public:
  FieldsPanelBox() { setExpansive(true); }

protected:
  void onPaint(ui::PaintEvent& ev) override
  {
    ui::Graphics* g = ev.graphics();
    const gfx::Rect rc = clientBounds();
    g->fillRect(gfx::rgba(45, 55, 66), rc);
    g->drawRect(gfx::rgba(28, 35, 43), rc);
  }
};

static void setupSixSurfacesOnDoc(Doc* doc, bool seedStarterIfBlank)
{
  // Entering the studio now sets up the shared UV sheets, not
  // per-face layers. setupSharedSurfaceLayers migrates any existing
  // per-face Base/Texture images into their UV cells and removes
  // the old layers, so opening an old document upgrades it in
  // place instead of leaving 12+ layers behind.
  setupSharedSurfaceLayers(doc, seedStarterIfBlank);
}

static bool extractSixSurfaces(Doc* doc,
                               doc::frame_t frame,
                               SixSurfaceData& out,
                               bool useFaceDepth = true,
                               DocView* docView = nullptr,
                               const std::array<bool, 3>* colLinked = nullptr)
{
  if (!doc || !doc->sprite())
    return false;
  doc::Sprite* spr = doc->sprite();
  const int sw = spr->width();
  const int sh = spr->height();
  out.init(std::max(sw, sh));

  doc::ImageRef depthSrcImg;
  if (useFaceDepth) {
    if (doc::LayerImage* depthLyr = findLayerByName(spr, "Surface_DEPTH")) {
      if (const doc::Cel* dCel = depthLyr->cel(frame)) {
        if (dCel->image() && dCel->image()->pixelFormat() == doc::IMAGE_RGB) {
          depthSrcImg.reset(doc::Image::create(doc::IMAGE_RGB, sw, sh));
          depthSrcImg->clear(0);
          doc::copy_image(depthSrcImg.get(), dCel->image(), dCel->x(), dCel->y());
          out.hasCustomDepthLayer = true;
        }
      }
    }
  }

  ExtraCelRef extra = doc->extraCel();

  // Shared sheets, if present, decide both the layers and the rect:
  // each face samples its own UV rect of the same Base/Texture
  // sheets, instead of its own per-face layers.
  const SharedSurfaceLayers shared = findSharedLayers(spr);
  const bool useShared = (shared.baseLayer || shared.textureLayer);

  for (int f = 0; f < kFaceCount; ++f) {
    doc::Slice* slice = findSliceForFace(spr, f);
    gfx::Rect faceRect(0, 0, sw, sh);
    if (slice) {
      const auto* key = slice->getByFrame(frame);
      if (key && !key->bounds().isEmpty()) {
        faceRect = key->bounds();
      }
    }
    if (useShared) {
      // Recompute from the grid so a slice left at (0, 0) by the
      // old layout does not make every face sample the same cell.
      // sharedFaceRect keeps the slice's face size, only the
      // origin moves to the face's UV cell.
      faceRect = sharedFaceRect(spr, f);
    }
    const int fw = faceRect.w;
    const int fh = faceRect.h;
    out.bounds[f] = faceRect;
    out.setFaceSize(f, fw, fh);
    out.hasSlices = true;

    doc::LayerImage* baseLyr = shared.baseLayer;
    doc::LayerImage* texLyr = shared.textureLayer;
    if (!useShared) {
      SurfaceLayerPair sp = findSurfaceLayers(spr, f);
      baseLyr = sp.baseLayer;
      texLyr = sp.texLayer;
    }

    // If no dedicated surface layer found, check shared layer
    if (!baseLyr && !texLyr) {
      for (doc::Layer* l : spr->allLayers()) {
        if (l->isImage() && l->isVisible()) {
          baseLyr = static_cast<doc::LayerImage*>(l);
          break;
        }
      }
    }

    const doc::Cel* baseCel = baseLyr ? baseLyr->cel(frame) : nullptr;
    const doc::Cel* texCel = texLyr ? texLyr->cel(frame) : nullptr;
    const doc::Image* baseImg = baseCel ? baseCel->image() : nullptr;
    const doc::Image* texImg = texCel ? texCel->image() : nullptr;

    const int baseOx = baseCel ? baseCel->x() : 0;
    const int baseOy = baseCel ? baseCel->y() : 0;
    const int texOx = texCel ? texCel->x() : 0;
    const int texOy = texCel ? texCel->y() : 0;

    auto getRgbaPixel = [&](const doc::Image* img, int x, int y) -> uint32_t {
      if (!img || x < 0 || x >= img->width() || y < 0 || y >= img->height())
        return 0;
      switch (img->pixelFormat()) {
        case doc::IMAGE_RGB:       return doc::get_pixel_fast<doc::RgbTraits>(img, x, y);
        case doc::IMAGE_GRAYSCALE: {
          const doc::color_t c = doc::get_pixel_fast<doc::GrayscaleTraits>(img, x, y);
          const int val = doc::graya_getv(c);
          return doc::rgba(val, val, val, doc::graya_geta(c));
        }
        case doc::IMAGE_INDEXED: {
          const doc::color_t idx = doc::get_pixel_fast<doc::IndexedTraits>(img, x, y);
          if (idx == spr->transparentColor())
            return 0;
          if (const doc::Palette* pal = spr->palette(frame))
            return pal->getEntry(idx);
          return 0;
        }
        default: return 0;
      }
    };

    bool hasPixels = false;

    for (int v = 0; v < fh; ++v) {
      const int canvasY = faceRect.y + v;
      for (int u = 0; u < fw; ++u) {
        const int canvasX = faceRect.x + u;

        uint32_t baseCol = getRgbaPixel(baseImg, canvasX - baseOx, canvasY - baseOy);
        uint32_t texCol = getRgbaPixel(texImg, canvasX - texOx, canvasY - texOy);

        uint32_t finalCol = 0;
        if (baseImg && texImg) {
          // Two-layer clipping mask & smart object structure:
          // Base layer defines alpha clipping mask.
          // When base alpha > 16: texture layer overrides base content.
          // When texture is transparent: falls back to base content.
          // When texture is semi-transparent: alpha-blends over base content.
          // When base alpha <= 16: masked out (transparent).
          if (doc::rgba_geta(baseCol) > 16) {
            const int aT = doc::rgba_geta(texCol);
            if (aT == 255) {
              finalCol = texCol;
            }
            else if (aT > 0) {
              const int invA = 255 - aT;
              finalCol = doc::rgba(
                (doc::rgba_getr(texCol) * aT + doc::rgba_getr(baseCol) * invA) / 255,
                (doc::rgba_getg(texCol) * aT + doc::rgba_getg(baseCol) * invA) / 255,
                (doc::rgba_getb(texCol) * aT + doc::rgba_getb(baseCol) * invA) / 255,
                doc::rgba_geta(baseCol));
            }
            else {
              finalCol = baseCol;
            }
          }
        }
        else if (texImg) {
          finalCol = texCol;
        }
        else if (baseImg) {
          finalCol = baseCol;
        }

        if (doc::rgba_geta(finalCol) > 0) {
          out.pixels[f][v * fw + u] = finalCol;
          hasPixels = true;
        }
      }
    }

    // Check if extra cel applies to this face editor
    if (extra && extra->type() != render::ExtraType::NONE &&
        extra->purpose() != ExtraCel::Purpose::BrushPreview && extra->image() && extra->cel() &&
        docView && docView->editor() &&
        (docView->editor()->layer() == baseLyr || docView->editor()->layer() == texLyr)) {
      const doc::Image* eImg = extra->image();
      const int ox = extra->cel()->x();
      const int oy = extra->cel()->y();
      const bool editingTex = (docView->editor()->layer() == texLyr && baseImg);

      for (int v = 0; v < fh; ++v) {
        const int canvasY = faceRect.y + v;
        const int sy = canvasY - oy;
        if (sy < 0 || sy >= eImg->height())
          continue;
        for (int u = 0; u < fw; ++u) {
          const int canvasX = faceRect.x + u;
          const int sx = canvasX - ox;
          if (sx < 0 || sx >= eImg->width())
            continue;
          const uint32_t ec = getRgbaPixel(eImg, sx, sy);
          if (doc::rgba_geta(ec) > 0) {
            if (editingTex) {
              const int bx = canvasX - baseOx;
              const int by = canvasY - baseOy;
              uint32_t bc = getRgbaPixel(baseImg, bx, by);
              if (doc::rgba_geta(bc) > 16) {
                const int aT = doc::rgba_geta(ec);
                if (aT == 255) {
                  out.pixels[f][v * fw + u] = ec;
                }
                else if (aT > 0) {
                  const int invA = 255 - aT;
                  out.pixels[f][v * fw + u] = doc::rgba(
                    (doc::rgba_getr(ec) * aT + doc::rgba_getr(bc) * invA) / 255,
                    (doc::rgba_getg(ec) * aT + doc::rgba_getg(bc) * invA) / 255,
                    (doc::rgba_getb(ec) * aT + doc::rgba_getb(bc) * invA) / 255,
                    doc::rgba_geta(bc));
                }
                else {
                  out.pixels[f][v * fw + u] = bc;
                }
                hasPixels = true;
              }
            }
            else {
              out.pixels[f][v * fw + u] = ec;
              hasPixels = true;
            }
          }
        }
      }
    }

    // If custom depth layer is present, extract depth for this face
    if (depthSrcImg) {
      for (int v = 0; v < fh; ++v) {
        const int canvasY = faceRect.y + v;
        if (canvasY < 0 || canvasY >= depthSrcImg->height())
          continue;
        for (int u = 0; u < fw; ++u) {
          const int canvasX = faceRect.x + u;
          if (canvasX < 0 || canvasX >= depthSrcImg->width())
            continue;
          const uint32_t dc =
            doc::get_pixel_fast<doc::RgbTraits>(depthSrcImg.get(), canvasX, canvasY);
          out.depthPixels[f][v * fw + u] = clampByte(doc::rgba_getr(dc) * 0.299f +
                                                     doc::rgba_getg(dc) * 0.587f +
                                                     doc::rgba_getb(dc) * 0.114f);
        }
      }
    }

    if (!hasPixels) {
      for (int v = 0; v < fh; ++v) {
        for (int u = 0; u < fw; ++u) {
          out.pixels[f][v * fw + u] = starterSurfacePixel(f, u, v, fw, fh);
        }
      }
    }
  }

  // Unify linked opposing face pairs (Top/Down, Front/Back, Right/Left)
  if (colLinked) {
    for (int c = 0; c < 3; ++c) {
      out.colLinked[c] = (*colLinked)[c];
      if ((*colLinked)[c]) {
        const int topFace = c * 2;
        const int btmFace = c * 2 + 1;
        out.pixels[btmFace] = out.pixels[topFace];
        out.depthPixels[btmFace] = out.depthPixels[topFace];
        out.w[btmFace] = out.w[topFace];
        out.h[btmFace] = out.h[topFace];
        out.bounds[btmFace] = out.bounds[topFace];
      }
    }
  }

  return true;
}

static void bake3DToDoc(Doc* doc, int yawDeg = 45, int pitchDeg = 30)
{
  if (!doc || !doc->sprite())
    return;
  SixSurfaceData data;
  extractSixSurfaces(doc, doc::frame_t(0), data, true);
  Render3DResult res = compute3DPreviewImages(data, yawDeg, pitchDeg, 50, 50, 50, 0, true, true);
  if (!res.diffImg || !res.normImg || !res.deptImg)
    return;

  doc::Sprite* spr = doc->sprite();
  const doc::frame_t fr = doc::frame_t(0);
  try {
    Tx tx(doc, "Render Easymetric 3D + Normal + Depth Layers", ModifyDocument);
    DocApi api = doc->getApi(tx);

    auto writeLayerImg = [&](const char* layerName, const doc::Image* srcImg) {
      doc::LayerImage* lyr = findOrCreateLayer(doc, api, spr, layerName);
      doc::ImageRef copyImg(doc::Image::createCopy(srcImg));
      if (doc::Cel* cel = lyr->cel(fr))
        api.replaceImage(spr, cel->imageRef(), copyImg);
      else
        api.addCel(lyr, fr, copyImg);
    };

    writeLayerImg("Iso_Depth", res.deptImg.get());
    writeLayerImg("Iso_Normal", res.normImg.get());
    writeLayerImg("Iso_Diffuse", res.diffImg.get());

    tx.commit();
    doc->notifyGeneralUpdate();
  }
  catch (const std::exception& e) {
    // Ignore
  }
}

// Integrated 4-Column 2-Row Workspace Multi-Pane Widget
// Panel editor state: wheel = zoom, never View scroll, regardless
// of the global "zoom with wheel" preference or a customised
// mouse-wheel shortcut table (both of which made the old
// preference-forcing fix unreliable in practice: wheel over a
// surface panel still scrolled the panel and did not zoom).
// StandbyState is kept as the base, so drawing, tools and the
// temporary moving/drawing states all work as before; only
// onMouseWheel is overridden. Zoom is anchored at the mouse
// (not at the centre), so the View scroll only follows the
// zoom anchor instead of jumping, and Shift+wheel (or a
// horizontal wheel) is still horizontal scroll, so scrolling
// is still possible on purpose. Alt+wheel keeps the base
// colour-change behaviour.
class EasymetricWheelZoomState : public StandbyState {
public:
  bool onMouseWheel(Editor* editor, ui::MouseMessage* msg) override
  {
    if (msg->altPressed())
      return StandbyState::onMouseWheel(editor, msg);
    gfx::Point delta = msg->wheelDelta();
    if (msg->shiftPressed() || delta.x != 0) {
      processWheelAction(editor,
                         WheelAction::HScroll,
                         msg->position(),
                         gfx::Point(delta.x, 0),
                         double(delta.x),
                         ScrollBigSteps::Off,
                         (msg->preciseWheel() ? PreciseWheel::On : PreciseWheel::Off),
                         FromMouseWheel::On);
      return true;
    }
    double dz = double(delta.y);
    if (msg->preciseWheel()) {
      dz /= 1.5;
      if (dz < -1.0)
        dz = -1.0;
      else if (dz > 1.0)
        dz = 1.0;
    }
    render::Zoom zoom = render::Zoom::fromLinearScale(editor->zoom().linearScale() - int(dz));
    editor->setZoomAndCenterInMouse(zoom, msg->position(), Editor::ZoomBehavior::MOUSE);
    return true;
  }
};

class EasymetricMultiPaneView : public ui::Widget {
public:
  EasymetricMultiPaneView(DocView* docView)
    : ui::Widget(ui::kGenericWidget)
    , m_docView(docView)
    , m_timer(33, this)
  {
    // Wheel = zoom, not scroll, while the studio is open.
    // The preference-forcing below is kept as a fallback for
    // any editor that is not a surface panel, but the surface
    // panels themselves now use EasymetricWheelZoomState (set
    // below), which forces wheel = zoom regardless of the
    // preference or a customised shortcut table. Save the old
    // value and restore it when the studio closes (destructor
    // and the Exit button both restore, idempotently), so normal
    // editing outside Easymetric is untouched. The Preview
    // widget already zooms on wheel and returns the message as
    // handled, so it never scrolls the UI either; Shift+wheel
    // still pans it.
    m_savedZoomWithWheel = Preferences::instance().editor.zoomWithWheel();
    if (!m_savedZoomWithWheel) {
      Preferences::instance().editor.zoomWithWheel(true);
      m_restoreZoomWithWheel = true;
    }

    setExpansive(true);

    // Remember the document selection so the per-face mask
    // confinement in pollAndRender can be undone on exit.
    if (Doc* doc0 = m_docView->document()) {
      if (doc0->mask() && !doc0->mask()->isEmpty())
        m_savedMask.reset(new doc::Mask(*doc0->mask()));
    }

    Doc* doc = m_docView->document();
    doc::Sprite* spr = doc ? doc->sprite() : nullptr;

    // 1. Create the 6 Surface Editors (Top, Bottom, Front, Back, Right, Left)
    for (int f = 0; f < kFaceCount; ++f) {
      m_headers[f] = new PaneHeader(kFaceSpecs[f].displayName,
                                    gfx::rgba(kFaceSpecs[f].r, kFaceSpecs[f].g, kFaceSpecs[f].b));
      Editor* ed = nullptr;
      ui::View* view = m_docView->createPaneEditorView(&ed);
      // Scrollbars on: each surface panel must be scrollable
      // horizontally and vertically on its own, instead of only
      // fitting the whole sprite into the pane. Hiding the bars
      // hid that affordance without disabling it, so show them.
      // The wheel is reserved for zooming (see above), so
      // scrolling needs the bars and drag, not the wheel.
      // fitting the whole sprite into the pane. The editor itself
      // already zooms/scrolls on wheel input; hiding the bars hid
      // that affordance without disabling it, so show them.
      view->showScrollBars();
      view->setExpansive(true);

      m_editors[f] = ed;
      m_views[f] = view;
      // Wheel = zoom in this panel, never View scroll (see
      // EasymetricWheelZoomState). Set after createPaneEditorView
      // so the custom state is the base state the temporary
      // drawing/moving states return to.
      ed->setState(EditorStatePtr(new EasymetricWheelZoomState));

      if (spr) {
        SharedSurfaceLayers shared = findSharedLayers(spr);
        if (shared.baseLayer || shared.textureLayer) {
          // One sheet, one UV window per panel.
          ed->setClipSpriteBounds(sharedFaceRect(spr, f));
        }
        else if (doc::Slice* sl = findSliceForFace(spr, f)) {
          if (sl->getByFrame(0) && !sl->getByFrame(0)->bounds().isEmpty()) {
            ed->setClipSpriteBounds(sl->getByFrame(0)->bounds());
          }
          else {
            ed->setClipSpriteBounds(gfx::Rect(0, 0, spr->width(), spr->height()));
          }
        }
        else {
          ed->setClipSpriteBounds(gfx::Rect(0, 0, spr->width(), spr->height()));
        }
        doc::LayerImage* lyr = shared.textureLayer ? shared.textureLayer : shared.baseLayer;
        if (!lyr) {
          SurfaceLayerPair sp = findSurfaceLayers(spr, f);
          lyr = sp.texLayer ? sp.texLayer : sp.baseLayer;
          if (!lyr) {
            const std::string lyrName = std::string("Surface_") + kFaceSpecs[f].sliceName;
            lyr = findLayerByName(spr, lyrName);
          }
        }
        if (lyr)
          ed->setLayer(lyr);
      }
      ed->setEditorFlags(
        Editor::EditorFlags((ed->editorFlags() | Editor::kShowOutside) & ~Editor::kShowSlices));

      m_panes[f] = new FacePaneBox(f, m_docView, ed);
      m_panes[f]->addChild(m_headers[f]);
      m_panes[f]->addChild(m_views[f]);
      addChild(m_panes[f]);
    }

    // Attach link toggle buttons to the top face header of each column (Col 0: Top, Col 1: Front,
    // Col 2: Right)
    for (int c = 0; c < 3; ++c) {
      m_linkButtons[c] = new LinkToggleButton();
      m_linkButtons[c]->Click.connect([this, c] { toggleColumnLink(c); });
    }
    m_headers[0]->setLinkButton(m_linkButtons[0]);
    m_headers[2]->setLinkButton(m_linkButtons[1]);
    m_headers[4]->setLinkButton(m_linkButtons[2]);

    // 2. Column 4 Row 0: Preview Pane
    m_headers[kFaceCount] = new PaneHeader("Preview");
    m_previewWidget = new EasymetricPreviewWidget;
    m_previewWidget->setExpansive(true);

    m_panes[kFaceCount] = new ui::VBox;
    m_panes[kFaceCount]->addChild(m_headers[kFaceCount]);
    m_panes[kFaceCount]->addChild(m_previewWidget);
    addChild(m_panes[kFaceCount]);

    // 3. Column 4 Row 1: Fields Pane, scrollable. Previously the
    // Fields box was added directly to the pane, so when its
    // controls grew taller than the pane the bottom ones were
    // simply hidden with no way to reach them. Wrapping it in a
    // View with scrollbars (and wheel scrolling, since this is a
    // control panel, not a canvas) makes every field reachable.
    m_headers[kFaceCount + 1] = new PaneHeader("Fields");
    m_fieldsBox = new FieldsPanelBox;
    m_fieldsView = new ui::View;
    m_fieldsView->showScrollBars();
    m_fieldsView->setExpansive(true);
    m_fieldsView->attachToView(m_fieldsBox);

    buildFieldsControls();

    m_panes[kFaceCount + 1] = new ui::VBox;
    m_panes[kFaceCount + 1]->addChild(m_headers[kFaceCount + 1]);
    m_panes[kFaceCount + 1]->addChild(m_fieldsView);
    addChild(m_panes[kFaceCount + 1]);

    // Timer for real-time 3D model update
    m_timer.Tick.connect([this] { pollAndRender(false); });
    m_timer.start();

    m_lastHash = 0;
    pollAndRender(true);
  }

  ~EasymetricMultiPaneView() override
  {
    m_timer.stop();
    restoreWheelZoomPref();
    if (Doc* doc = m_docView->document()) {
      if (m_savedMask) {
        doc->setMask(m_savedMask.get());
        doc->setMaskVisible(true);
      }
      else {
        doc::Mask empty;
        doc->setMask(&empty);
        doc->setMaskVisible(false);
      }
      doc->notifySelectionChanged();
    }
  }

  void restoreWheelZoomPref()
  {
    if (m_restoreZoomWithWheel) {
      Preferences::instance().editor.zoomWithWheel(m_savedZoomWithWheel);
      m_restoreZoomWithWheel = false;
    }
  }

  std::vector<Editor*> surfaceEditors() const
  {
    return std::vector<Editor*>(m_editors.begin(), m_editors.end());
  }

  void syncOpposingCels(int srcFace, int dstFace)
  {
    Doc* doc = m_docView->document();
    if (!doc || !doc->sprite())
      return;
    doc::Sprite* spr = doc->sprite();

    SharedSurfaceLayers shared = findSharedLayers(spr);
    if (shared.baseLayer || shared.textureLayer) {
      // Shared sheets: opposing faces are different UV rects of
      // the SAME layer, so syncing means copying the source rect
      // into the destination rect, not replacing the whole sheet
      // (which would also overwrite the source itself).
      try {
        Tx tx(doc, "Sync Opposing Faces", ModifyDocument);
        DocApi api = doc->getApi(tx);
        const gfx::Rect srcRect = sharedFaceRect(spr, srcFace);
        const gfx::Rect dstRect = sharedFaceRect(spr, dstFace);
        auto syncRect = [&](doc::LayerImage* lyr) {
          if (!lyr)
            return;
          for (doc::frame_t fr = doc::frame_t(0); fr <= spr->lastFrame(); ++fr) {
            doc::Cel* cel = lyr->cel(fr);
            if (!cel || !cel->image())
              continue;
            doc::ImageRef copyImg(doc::Image::createCopy(cel->image()));
            for (int v = 0; v < srcRect.h && v < dstRect.h; ++v)
              for (int u = 0; u < srcRect.w && u < dstRect.w; ++u)
                doc::put_pixel(copyImg.get(),
                               dstRect.x + u,
                               dstRect.y + v,
                               doc::get_pixel(copyImg.get(), srcRect.x + u, srcRect.y + v));
            api.replaceImage(spr, cel->imageRef(), copyImg);
          }
        };
        syncRect(shared.baseLayer);
        syncRect(shared.textureLayer);
        syncRect(shared.depthLayer);
        tx.commit();
        doc->notifyGeneralUpdate();
      }
      catch (const std::exception& e) {
        Console::showException(e);
      }
      return;
    }

    SurfaceLayerPair srcPair = findSurfaceLayers(spr, srcFace);
    SurfaceLayerPair dstPair = findSurfaceLayers(spr, dstFace);

    const std::string srcName = std::string("Surface_") + kFaceSpecs[srcFace].sliceName;
    const std::string dstName = std::string("Surface_") + kFaceSpecs[dstFace].sliceName;
    doc::LayerImage* srcLyr = srcPair.baseLayer ? srcPair.baseLayer : findLayerByName(spr, srcName);
    doc::LayerImage* dstLyr = dstPair.baseLayer ? dstPair.baseLayer : findLayerByName(spr, dstName);
    if (!srcLyr || !dstLyr)
      return;

    try {
      Tx tx(doc, "Sync Opposing Faces", ModifyDocument);
      DocApi api = doc->getApi(tx);

      auto syncLayers = [&](doc::LayerImage* sL, doc::LayerImage* dL) {
        if (!sL || !dL)
          return;
        for (doc::frame_t fr = doc::frame_t(0); fr <= spr->lastFrame(); ++fr) {
          doc::Cel* srcCel = sL->cel(fr);
          doc::Cel* dstCel = dL->cel(fr);
          if (srcCel && srcCel->image()) {
            doc::ImageRef copyImg(doc::Image::createCopy(srcCel->image()));
            if (dstCel) {
              api.replaceImage(spr, dstCel->imageRef(), copyImg);
              api.setCelPosition(spr, dstCel, srcCel->x(), srcCel->y());
            }
            else {
              api.addCel(dL, fr, copyImg);
            }
          }
        }
      };

      syncLayers(srcLyr, dstLyr);
      if (srcPair.texLayer && dstPair.texLayer)
        syncLayers(srcPair.texLayer, dstPair.texLayer);

      tx.commit();
      doc->notifyGeneralUpdate();
    }
    catch (const std::exception& e) {
      Console::showException(e);
    }
  }

  void syncAllLinkedCels()
  {
    for (int c = 0; c < 3; ++c) {
      if (m_colLinked[c]) {
        syncOpposingCels(c * 2, c * 2 + 1);
      }
    }
  }

  void toggleColumnLink(int c)
  {
    if (c < 0 || c >= 3)
      return;
    const int topFace = c * 2;
    const int btmFace = c * 2 + 1;

    m_colLinked[c] = !m_colLinked[c];
    const bool linked = m_colLinked[c];

    if (m_linkButtons[c])
      m_linkButtons[c]->setLinked(linked);

    Doc* doc = m_docView->document();
    doc::Sprite* spr = doc ? doc->sprite() : nullptr;

    if (linked) {
      if (Editor::activeEditor() == m_editors[btmFace]) {
        Editor::_setActiveEditor(m_editors[topFace]);
      }
      if (m_docView->editor() == m_editors[btmFace]) {
        m_docView->setActivePaneEditor(m_editors[topFace]);
      }
      if (spr) {
        SharedSurfaceLayers shared = findSharedLayers(spr);
        if (shared.textureLayer)
          m_editors[topFace]->setLayer(shared.textureLayer);
        else {
          const std::string topLyrName = std::string("Surface_") + kFaceSpecs[topFace].sliceName;
          if (doc::LayerImage* lyr = findLayerByName(spr, topLyrName))
            m_editors[topFace]->setLayer(lyr);
        }
      }

      syncOpposingCels(topFace, btmFace);

      std::string unifiedTitle;
      switch (c) {
        case 0: unifiedTitle = "Top / Bottom"; break;
        case 1: unifiedTitle = "Front / Back"; break;
        case 2: unifiedTitle = "Right / Left"; break;
      }
      m_headers[topFace]->setTitle(unifiedTitle);
    }
    else {
      m_headers[topFace]->setTitle(kFaceSpecs[topFace].displayName);
      m_headers[btmFace]->setTitle(kFaceSpecs[btmFace].displayName);
      syncOpposingCels(topFace, btmFace);
    }

    layout();
    for (int f = 0; f < kFaceCount; ++f) {
      if (m_editors[f] && m_panes[f]->isVisible())
        m_editors[f]->fitClipSpriteBounds();
    }
    invalidate();
    m_lastHash = 0;
    pollAndRender(true);
  }

  void initLayersAndFraming()
  {
    Doc* doc = m_docView->document();
    doc::Sprite* spr = doc ? doc->sprite() : nullptr;
    if (!spr)
      return;

    for (int f = 0; f < kFaceCount; ++f) {
      if (m_editors[f]) {
        SharedSurfaceLayers shared = findSharedLayers(spr);
        doc::LayerImage* lyr = shared.textureLayer ? shared.textureLayer : shared.baseLayer;
        if (!lyr) {
          SurfaceLayerPair sp = findSurfaceLayers(spr, f);
          lyr = sp.texLayer ? sp.texLayer : sp.baseLayer;
          if (!lyr) {
            const std::string lyrName = std::string("Surface_") + kFaceSpecs[f].sliceName;
            lyr = findLayerByName(spr, lyrName);
          }
        }
        if (lyr)
          m_editors[f]->setLayer(lyr);

        if (shared.baseLayer || shared.textureLayer) {
          m_editors[f]->setClipSpriteBounds(sharedFaceRect(spr, f));
        }
        else if (doc::Slice* sl = findSliceForFace(spr, f)) {
          if (sl->getByFrame(0) && !sl->getByFrame(0)->bounds().isEmpty()) {
            m_editors[f]->setClipSpriteBounds(sl->getByFrame(0)->bounds());
          }
          else {
            m_editors[f]->setClipSpriteBounds(gfx::Rect(0, 0, spr->width(), spr->height()));
          }
        }
        else {
          m_editors[f]->setClipSpriteBounds(gfx::Rect(0, 0, spr->width(), spr->height()));
        }
        m_editors[f]->fitClipSpriteBounds();
      }
    }
  }

protected:
  // One layout source for onResize, onPaint and splitter hit-testing,
  // so the painted handles always line up with the dragged ones.
  // colW[3] is the remainder, so the four columns always fill rc.
  // Splitter gaps are 6*guiscale wide (was 4*): the gap itself
  // is the only area that actually reaches this widget (the panes
  // cover everything else), so a 4*guiscale gap was easy to miss
  // and its cursor indication flickered at the edges. 6*guiscale
  // is the grab area, not just the painted bar.
  // Columns are stored in PIXELS, not fractions of totalW.
  // That is what makes them independent: with fractions, making
  // the window wider (or dragging the Preview boundary, whose
  // width was the remainder of a fraction sum) scaled every
  // column at once, so stretching Preview shrank/grew all
  // three surface columns. With pixel widths, the three
  // surface columns keep exactly their stored widths when
  // the window is resized, and only the Preview column
  // (col 3, the remainder) absorbs the difference. Dragging
  // a boundary changes only the two columns either side of
  // it (see onProcessMessage). The top row height is stored
  // the same way; the Fields row absorbs window height
  // changes. First layout: quarter / half, as before.
  void computePaneLayout(const gfx::Rect& rc,
                         int colX[4],
                         int colW[4],
                         int rowY[2],
                         int rowH[2]) const
  {
    const int s = std::max(1, ui::guiscale());
    const int bw = 6 * s;
    const int bh = 6 * s;
    const int totalW = std::max(16, rc.w - 3 * bw);
    const int totalH = std::max(16, rc.h - bh);
    const int minCol = std::min(64 * s, totalW / 4);
    const int minRow = std::min(64 * s, totalH / 2);

    if (!m_colInit) {
      m_colWpx[0] = totalW / 4;
      m_colWpx[1] = totalW / 4;
      m_colWpx[2] = totalW / 4;
      m_rowHpx = totalH / 2;
      m_colInit = true;
    }

    colW[0] = std::clamp(m_colWpx[0], minCol, std::max(minCol, totalW - 3 * minCol));
    colW[1] = std::clamp(m_colWpx[1], minCol, std::max(minCol, totalW - colW[0] - 2 * minCol));
    colW[2] =
      std::clamp(m_colWpx[2], minCol, std::max(minCol, totalW - colW[0] - colW[1] - minCol));
    // Narrow window: surface columns alone already exceed the
    // width, so shrink them from the rightmost backwards
    // (Preview keeps its minimum) instead of overflowing.
    int excess = colW[0] + colW[1] + colW[2] + minCol - totalW;
    for (int c = 2; excess > 0 && c >= 0; --c) {
      const int shrink = std::min(excess, colW[c] - minCol);
      colW[c] -= shrink;
      excess -= shrink;
    }
    colW[3] = std::max(minCol, totalW - colW[0] - colW[1] - colW[2]);

    colX[0] = rc.x;
    colX[1] = rc.x + colW[0] + bw;
    colX[2] = rc.x + colW[0] + bw + colW[1] + bw;
    colX[3] = rc.x + colW[0] + bw + colW[1] + bw + colW[2] + bw;

    rowH[0] = std::clamp(m_rowHpx, minRow, std::max(minRow, totalH - minRow));
    rowH[1] = std::max(minRow, totalH - rowH[0]);
    rowY[0] = rc.y;
    rowY[1] = rc.y + rowH[0] + bh;
  }

  // Splitter under the cursor, or 0: 1..3 = vertical boundary before
  // column c, 4 = horizontal boundary between the two rows.
  int splitterAt(const gfx::Point& pos) const
  {
    const gfx::Rect rc = clientBounds();
    const int s = std::max(1, ui::guiscale());
    const int bw = 6 * s;
    const int bh = 6 * s;
    int colX[4], colW[4], rowY[2], rowH[2];
    computePaneLayout(rc, colX, colW, rowY, rowH);
    if (!rc.contains(pos))
      return 0;
    // Vertical gap for column c is [colX[c]-bw, colX[c]), so its
    // centre is colX[c]-bw/2, not colX[c] (the old code centred
    // the hit area on the gap's right edge, so the resize cursor
    // appeared up to half a bar inside the next pane and missed
    // the left half of the painted bar).
    for (int c = 1; c < 4; ++c) {
      if (std::abs(pos.x - (colX[c] - bw / 2)) <= bw / 2 + s)
        return c;
    }
    const int midY = rowY[1] - bh / 2;
    if (std::abs(pos.y - midY) <= bh / 2 + s)
      return 4;
    return 0;
  }

  bool onProcessMessage(ui::Message* msg) override
  {
    switch (msg->type()) {
      case ui::kSetCursorMessage: {
        if (m_dragSplitter != 0) {
          ui::set_mouse_cursor(m_dragSplitter == 4 ? ui::kSizeNSCursor : ui::kSizeWECursor);
          return true;
        }
        auto* mouseMsg = static_cast<ui::MouseMessage*>(msg);
        const int hit = splitterAt(mouseMsg->position());
        if (hit != 0) {
          ui::set_mouse_cursor(hit == 4 ? ui::kSizeNSCursor : ui::kSizeWECursor);
          return true;
        }
        break;
      }
      case ui::kMouseDownMessage: {
        auto* mouseMsg = static_cast<ui::MouseMessage*>(msg);
        const int hit = splitterAt(mouseMsg->position());
        if (hit != 0 && mouseMsg->left()) {
          m_dragSplitter = hit;
          m_dragStart = mouseMsg->position();
          {
            int cx[4], cw[4], ry[2], rh[2];
            computePaneLayout(clientBounds(), cx, cw, ry, rh);
            for (int i = 0; i < 3; ++i)
              m_dragColWpx[i] = cw[i];
            m_dragPreviewW = cw[3];
            m_dragRowHpx = rh[0];
          }
          captureMouse();
          invalidate();
          return true;
        }
        break;
      }
      case ui::kMouseMoveMessage: {
        auto* mouseMsg = static_cast<ui::MouseMessage*>(msg);
        if (m_dragSplitter != 0) {
          const gfx::Rect rc = clientBounds();
          const int s = std::max(1, ui::guiscale());
          const int bw = 6 * s;
          const int bh = 6 * s;
          const int totalW = std::max(16, rc.w - 3 * bw);
          const int totalH = std::max(16, rc.h - bh);
          // Pixel drag, adjacent pair only: the other columns'
          // stored pixel widths are not touched, so stretching
          // Preview (boundary 3) changes only Right/Left + Preview,
          // never the other two surface columns, and a window
          // resize (which only changes totalW) changes only
          // Preview via computePaneLayout.
          const int minColPx = std::min(64 * s, totalW / 4);
          const int minRowPx = std::min(64 * s, totalH / 2);
          if (m_dragSplitter < 4) {
            const int deltaPx = mouseMsg->position().x - m_dragStart.x;
            const int left = m_dragSplitter - 1;
            int pair = 0;
            if (m_dragSplitter < 3)
              pair = m_dragColWpx[left] + m_dragColWpx[m_dragSplitter];
            else
              pair = m_dragColWpx[left] + m_dragPreviewW;
            const int newLeft = std::clamp(m_dragColWpx[left] + deltaPx, minColPx, pair - minColPx);
            m_colWpx[left] = newLeft;
            if (m_dragSplitter < 3)
              m_colWpx[m_dragSplitter] = pair - newLeft;
            // Boundary 3: Preview is the remainder, so it follows
            // automatically from the new left width.
          }
          else {
            const int deltaPx = mouseMsg->position().y - m_dragStart.y;
            m_rowHpx = std::clamp(m_dragRowHpx + deltaPx, minRowPx, totalH - minRowPx);
          }
          layout();
          invalidate();
          return true;
        }
        const int hit = splitterAt(mouseMsg->position());
        if (hit != m_hoverSplitter) {
          m_hoverSplitter = hit;
          invalidate();
        }
        break;
      }
      case ui::kMouseUpMessage: {
        if (m_dragSplitter != 0) {
          m_dragSplitter = 0;
          releaseMouse();
          invalidate();
          return true;
        }
        break;
      }
      case ui::kMouseLeaveMessage: {
        if (m_hoverSplitter != 0) {
          m_hoverSplitter = 0;
          invalidate();
        }
        break;
      }
    }
    return ui::Widget::onProcessMessage(msg);
  }

  void onResize(ui::ResizeEvent& ev) override
  {
    setBoundsQuietly(ev.bounds());
    const gfx::Rect rc = childrenBounds();
    const int s = ui::guiscale();
    const int bh = 6 * s;
    int colX[4], colW[4], rowY[2], rowH[2];
    computePaneLayout(rc, colX, colW, rowY, rowH);
    const int totalH = std::max(16, rc.h - bh);

    // Cols 0..2: Opposing face pairs
    for (int c = 0; c < 3; ++c) {
      const int topFace = c * 2;
      const int btmFace = c * 2 + 1;
      if (m_colLinked[c]) {
        m_panes[btmFace]->setVisible(false);
        m_panes[topFace]->setVisible(true);
        m_panes[topFace]->setBounds(gfx::Rect(colX[c], rc.y, colW[c], totalH + bh));
      }
      else {
        m_panes[topFace]->setVisible(true);
        m_panes[btmFace]->setVisible(true);
        m_panes[topFace]->setBounds(gfx::Rect(colX[c], rowY[0], colW[c], rowH[0]));
        m_panes[btmFace]->setBounds(gfx::Rect(colX[c], rowY[1], colW[c], rowH[1]));
      }
    }

    // Col 3: Preview (6), Fields (7)
    m_panes[6]->setVisible(true);
    m_panes[7]->setVisible(true);
    m_panes[6]->setBounds(gfx::Rect(colX[3], rowY[0], colW[3], rowH[0]));
    m_panes[7]->setBounds(gfx::Rect(colX[3], rowY[1], colW[3], rowH[1]));

    // NOTE: deliberately no fitClipSpriteBounds() here. Fitting on
    // every resize reset each surface editor's zoom/scroll whenever
    // a splitter was dragged or the window was resized, which is
    // what made the panels feel unscrollable/unzoomable. Initial
    // fitting still happens in initLayersAndFraming().
  }

  void onPaint(ui::PaintEvent& ev) override
  {
    ui::Graphics* g = ev.graphics();
    const gfx::Rect rc = clientBounds();
    const int s = ui::guiscale();
    const int bw = 6 * s;
    const int bh = 6 * s;
    int colX[4], colW[4], rowY[2], rowH[2];
    computePaneLayout(rc, colX, colW, rowY, rowH);
    const int midY = rowY[1] - bh / 2;

    // Dark charcoal surround & separator lines
    g->fillRect(gfx::rgba(28, 35, 43), rc);

    const gfx::Color sepCol = gfx::rgba(45, 55, 66);
    const gfx::Color gripCol = gfx::rgba(80, 95, 110);
    const gfx::Color activeCol = gfx::rgba(120, 170, 220);

    // Horizontal separators only for columns that are NOT linked (and Col 3)
    for (int c = 0; c < 4; ++c) {
      if (c < 3 && m_colLinked[c])
        continue;
      const bool active = (m_hoverSplitter == 4 || m_dragSplitter == 4);
      g->fillRect(active ? activeCol : sepCol, gfx::Rect(colX[c], midY - bh / 2, colW[c], bh));
    }

    // Vertical column separators, highlighted when hovered/dragged
    for (int c = 1; c < 4; ++c) {
      const int sepX = colX[c] - bw / 2;
      const bool active = (m_hoverSplitter == c || m_dragSplitter == c);
      g->fillRect(active ? activeCol : sepCol, gfx::Rect(sepX - bw / 2, rc.y, bw, rc.h));
      g->fillRect(active ? activeCol : gripCol, gfx::Rect(sepX - 2 * s, midY - 6 * s, 4 * s, s));
      g->fillRect(active ? activeCol : gripCol, gfx::Rect(sepX - 2 * s, midY + 5 * s, 4 * s, s));
    }
  }

private:
  void buildFieldsControls()
  {
    const int s = ui::guiscale();
    m_fieldsBox->setBorder(gfx::Border(4 * s));
    m_fieldsBox->setChildSpacing(3 * s);

    // Row 1: Rotation on Y-axis + Cam Angle
    auto* rotRow = new ui::HBox;
    rotRow->setChildSpacing(4 * s);
    m_yawSlider = new ui::Slider(-180, 180, 45);
    m_pitchSlider = new ui::Slider(-75, 75, 30);
    m_yawSlider->setExpansive(true);
    m_pitchSlider->setExpansive(true);
    rotRow->addChild(new ui::Label("Y-Axis Rot:"));
    rotRow->addChild(m_yawSlider);
    rotRow->addChild(new ui::Label("Cam Angle:"));
    rotRow->addChild(m_pitchSlider);
    m_fieldsBox->addChild(rotRow);

    // Row 2: Pivot X/Y/Z + Center + Base
    auto* pivRow = new ui::HBox;
    pivRow->setChildSpacing(4 * s);
    m_pivXSlider = new ui::Slider(0, 100, 50);
    m_pivYSlider = new ui::Slider(0, 100, 50);
    m_pivZSlider = new ui::Slider(0, 100, 50);
    m_pivXSlider->setExpansive(true);
    m_pivYSlider->setExpansive(true);
    m_pivZSlider->setExpansive(true);
    auto* pivCenterBtn = new ui::Button("Center");
    auto* pivBaseBtn = new ui::Button("Base");
    pivRow->addChild(new ui::Label("Pivot X/Y/Z:"));
    pivRow->addChild(m_pivXSlider);
    pivRow->addChild(m_pivYSlider);
    pivRow->addChild(m_pivZSlider);
    pivRow->addChild(pivCenterBtn);
    pivRow->addChild(pivBaseBtn);
    m_fieldsBox->addChild(pivRow);

    // Row 3: Tag / Angle Preset Dropdown + Save Tag
    auto* tagRow = new ui::HBox;
    tagRow->setChildSpacing(4 * s);
    m_tagCombo = new ui::ComboBox;
    m_tagCombo->setExpansive(true);
    populateTagCombo();
    auto* saveTagBtn = new ui::Button("Save Tag");
    tagRow->addChild(new ui::Label("Tag / Angle:"));
    tagRow->addChild(m_tagCombo);
    tagRow->addChild(saveTagBtn);
    m_fieldsBox->addChild(tagRow);

    // Row 4: Maps Toggles + Render Type Dropdown
    auto* mapRow = new ui::HBox;
    mapRow->setChildSpacing(4 * s);
    m_mapDiffuseCheck = new ui::CheckBox("Diffuse");
    m_mapNormalCheck = new ui::CheckBox("Normal");
    m_mapDepthCheck = new ui::CheckBox("Depth");
    m_mapDiffuseCheck->setSelected(true);

    m_renderTypeCombo = new ui::ComboBox;
    m_renderTypeCombo->addItem("Render: Textured (Lit)");
    m_renderTypeCombo->addItem("Render: Textured (Unlit)");
    m_renderTypeCombo->addItem("Render: Colored (Faces)");
    m_renderTypeCombo->addItem("Render: Normal Shaded");
    m_renderTypeCombo->addItem("Render: Depth Shaded");
    m_renderTypeCombo->addItem("Render: Wireframe + Tex");
    m_renderTypeCombo->setSelectedItemIndex(0);
    m_renderTypeCombo->setExpansive(true);

    mapRow->addChild(new ui::Label("Maps:"));
    mapRow->addChild(m_mapDiffuseCheck);
    mapRow->addChild(m_mapNormalCheck);
    mapRow->addChild(m_mapDepthCheck);
    mapRow->addChild(m_renderTypeCombo);
    m_fieldsBox->addChild(mapRow);

    // Row 5: Stroke Outline + Cutouts + Lock Borders + Symmetry X/Y
    auto* symRow = new ui::HBox;
    symRow->setChildSpacing(4 * s);
    m_strokeCheck = new ui::CheckBox("Stroke Outline");
    m_cutoutCheck = new ui::CheckBox("Cutouts");
    m_lockBordersCheck = new ui::CheckBox("Lock Borders");
    m_symXCheck = new ui::CheckBox("Sym X");
    m_symYCheck = new ui::CheckBox("Sym Y");
    auto* centerSymBtn = new ui::Button("Center Sym");
    m_strokeCheck->setSelected(true);
    m_cutoutCheck->setSelected(true);

    m_linkDimensionsCheck = new ui::CheckBox("Link Dims");
    m_linkDimensionsCheck->setSelected(true);

    symRow->addChild(m_strokeCheck);
    symRow->addChild(m_cutoutCheck);
    symRow->addChild(m_lockBordersCheck);
    symRow->addChild(m_linkDimensionsCheck);
    symRow->addChild(m_symXCheck);
    symRow->addChild(m_symYCheck);
    symRow->addChild(centerSymBtn);
    m_fieldsBox->addChild(symRow);

    // Row 5b: Linked Dimension Controls
    auto* dimRow = new ui::HBox;
    dimRow->setChildSpacing(4 * s);
    auto* faceDimCombo = new ui::ComboBox;
    faceDimCombo->addItem("Top (X/Z)");
    faceDimCombo->addItem("Bottom (X/Z)");
    faceDimCombo->addItem("Front (X/Y)");
    faceDimCombo->addItem("Back (X/Y)");
    faceDimCombo->addItem("Right (Z/Y)");
    faceDimCombo->addItem("Left (Z/Y)");
    faceDimCombo->setSelectedItemIndex(0);

    auto* dimWSlider = new ui::Slider(4, 128, 24);
    auto* dimHSlider = new ui::Slider(4, 128, 24);
    dimWSlider->setExpansive(true);
    dimHSlider->setExpansive(true);
    auto* applyDimBtn = new ui::Button("Apply Size");

    dimRow->addChild(new ui::Label("Size:"));
    dimRow->addChild(faceDimCombo);
    dimRow->addChild(dimWSlider);
    dimRow->addChild(dimHSlider);
    dimRow->addChild(applyDimBtn);
    m_fieldsBox->addChild(dimRow);

    applyDimBtn->Click.connect([this, faceDimCombo, dimWSlider, dimHSlider] {
      Doc* doc = m_docView->document();
      if (!doc || !doc->sprite())
        return;
      int selFace = faceDimCombo->getSelectedItemIndex();
      int nw = dimWSlider->getValue();
      int nh = dimHSlider->getValue();
      SixSurfaceData data;
      extractSixSurfaces(doc, 0, data);
      propagateLinkedDimensions(doc, data, selFace, nw, nh);
      initLayersAndFraming();
      m_lastHash = 0;
      pollAndRender(true);
      if (auto* sb = StatusBar::instance())
        sb->showTip(1200, "Propagated linked dimensions across surfaces");
    });

    // Row 6: Per-Face Editable Depth Maps + Live Sync
    auto* depthRow = new ui::HBox;
    depthRow->setChildSpacing(4 * s);
    auto* genFaceDepthBtn = new ui::Button("Render Face Depths (Edit)");
    m_useFaceDepthCheck = new ui::CheckBox("Use Face Depth Layer");
    m_liveBakeCheck = new ui::CheckBox("Live Sync");
    m_useFaceDepthCheck->setSelected(true);

    depthRow->addChild(genFaceDepthBtn);
    depthRow->addChild(m_useFaceDepthCheck);
    depthRow->addChild(m_liveBakeCheck);
    m_fieldsBox->addChild(depthRow);

    // Row 6b: 2-Layer Smart Object & Clipping Structure
    auto* smartRow = new ui::HBox;
    smartRow->setChildSpacing(4 * s);
    auto* setupSmartObjectsBtn = new ui::Button("Setup 2-Layer Smart Objects");
    auto* convertToGroupBtn = new ui::Button("Convert to Groups");
    setupSmartObjectsBtn->setExpansive(true);
    convertToGroupBtn->setExpansive(true);
    smartRow->addChild(setupSmartObjectsBtn);
    smartRow->addChild(convertToGroupBtn);
    m_fieldsBox->addChild(smartRow);

    // Zoom row: buttons + label mirror the preview widget's zoom, so
    // the rendering can also be zoomed without the mouse wheel.
    auto* zoomRow = new ui::HBox;
    zoomRow->setChildSpacing(4 * s);
    auto* zoomOutBtn = new ui::Button("-");
    auto* zoomResetBtn = new ui::Button("100%");
    auto* zoomInBtn = new ui::Button("+");
    m_zoomLabel = new ui::Label("100%");
    zoomRow->addChild(new ui::Label("Preview Zoom:"));
    zoomRow->addChild(zoomOutBtn);
    zoomRow->addChild(zoomInBtn);
    zoomRow->addChild(zoomResetBtn);
    zoomRow->addChild(m_zoomLabel);
    m_fieldsBox->addChild(zoomRow);

    // Row 7: Action Buttons (Render, Export .OBJ, Exit)
    auto* actRow = new ui::HBox;
    actRow->setChildSpacing(4 * s);
    auto* renderBtn = new ui::Button("&Render");
    auto* exportObjBtn = new ui::Button("Export .&OBJ...");
    auto* exitBtn = new ui::Button("E&xit Studio");
    renderBtn->setExpansive(true);
    exportObjBtn->setExpansive(true);

    actRow->addChild(renderBtn);
    actRow->addChild(exportObjBtn);
    actRow->addChild(exitBtn);
    m_fieldsBox->addChild(actRow);

    // Event connections
    auto onParam = [this] {
      m_lastHash = 0;
      pollAndRender(true);
    };

    m_yawSlider->Change.connect(onParam);
    m_pitchSlider->Change.connect(onParam);
    m_pivXSlider->Change.connect(onParam);
    m_pivYSlider->Change.connect(onParam);
    m_pivZSlider->Change.connect(onParam);
    m_mapDiffuseCheck->Click.connect(onParam);
    m_mapNormalCheck->Click.connect(onParam);
    m_mapDepthCheck->Click.connect(onParam);
    m_renderTypeCombo->Change.connect(onParam);
    m_strokeCheck->Click.connect(onParam);
    m_cutoutCheck->Click.connect(onParam);
    m_useFaceDepthCheck->Click.connect(onParam);
    m_liveBakeCheck->Click.connect(onParam);

    m_previewWidget->OrbitDrag.connect([this](int newYaw, int newPitch) {
      m_yawSlider->setValue(newYaw);
      m_pitchSlider->setValue(newPitch);
      m_lastHash = 0;
      pollAndRender(true);
    });

    zoomOutBtn->Click.connect([this] { m_previewWidget->zoomOut(); });
    zoomInBtn->Click.connect([this] { m_previewWidget->zoomIn(); });
    zoomResetBtn->Click.connect([this] { m_previewWidget->resetZoom(); });
    m_previewWidget->ZoomChanged.connect([this](double zoom) {
      if (m_zoomLabel)
        m_zoomLabel->setText(fmt::format("{}%", int(std::round(zoom * 100.0))));
    });

    pivCenterBtn->Click.connect([this] {
      m_pivXSlider->setValue(50);
      m_pivYSlider->setValue(50);
      m_pivZSlider->setValue(50);
      m_lastHash = 0;
      pollAndRender(true);
    });

    pivBaseBtn->Click.connect([this] {
      m_pivXSlider->setValue(50);
      m_pivYSlider->setValue(100);
      m_pivZSlider->setValue(50);
      m_lastHash = 0;
      pollAndRender(true);
    });

    m_tagCombo->Change.connect([this] { onTagSelected(); });
    saveTagBtn->Click.connect([this] { saveCurrentAngleAsTag(); });

    m_lockBordersCheck->Click.connect([this] {
      const bool locked = m_lockBordersCheck->isSelected();
      Doc* doc = m_docView->document();
      if (doc && doc->sprite()) {
        for (int f = 0; f < kFaceCount; ++f) {
          if (doc::Slice* sl = findSliceForFace(doc->sprite(), f))
            sl->userData().setText(locked ? "locked" : "");
        }
        for (Editor* ed : m_editors) {
          if (ed)
            ed->invalidate();
        }
      }
      if (auto* sb = StatusBar::instance())
        sb->showTip(1200, locked ? "Surface borders locked" : "Surface borders unlocked");
    });

    m_linkDimensionsCheck->Click.connect([this] {
      if (auto* sb = StatusBar::instance()) {
        sb->showTip(1200,
                    m_linkDimensionsCheck->isSelected() ? "Linked surface dimensions active" :
                                                          "Linked surface dimensions disabled");
      }
    });

    setupSmartObjectsBtn->Click.connect([this] {
      Doc* doc = m_docView->document();
      if (doc) {
        setupTwoLayerStructure(doc, true);
        initLayersAndFraming();
        m_lastHash = 0;
        pollAndRender(true);
        if (auto* sb = StatusBar::instance())
          sb->showTip(1500, "Initialized 2-layer smart object surface hierarchy");
      }
    });

    convertToGroupBtn->Click.connect([this] {
      Doc* doc = m_docView->document();
      if (doc) {
        convertSmartObjectsToGroups(doc);
        initLayersAndFraming();
        m_lastHash = 0;
        pollAndRender(true);
        if (auto* sb = StatusBar::instance())
          sb->showTip(1500, "Converted smart objects to layer groups");
      }
    });

    auto applySymmetry = [this](bool recenter) {
      Doc* doc = m_docView->document();
      if (!doc || !doc->sprite())
        return;
      auto& pref = Preferences::instance();
      auto& docPref = pref.document(doc);
      int mode = int(app::gen::SymmetryMode::NONE);
      if (m_symXCheck->isSelected())
        mode |= int(app::gen::SymmetryMode::HORIZONTAL);
      if (m_symYCheck->isSelected())
        mode |= int(app::gen::SymmetryMode::VERTICAL);
      if (mode != int(app::gen::SymmetryMode::NONE))
        pref.symmetryMode.enabled(true);
      docPref.symmetry.mode(static_cast<app::gen::SymmetryMode>(mode));

      Editor* curEd = m_docView->editor();
      int activeFace = 0;
      for (int f = 0; f < kFaceCount; ++f) {
        if (m_editors[f] == curEd) {
          activeFace = f;
          break;
        }
      }
      const int sprW = doc->sprite()->width();
      const int sprH = doc->sprite()->height();
      if (recenter || docPref.symmetry.xAxis() < 0 || docPref.symmetry.xAxis() > sprW ||
          docPref.symmetry.yAxis() < 0 || docPref.symmetry.yAxis() > sprH) {
        docPref.symmetry.xAxis(sprW * 0.5);
        docPref.symmetry.yAxis(sprH * 0.5);
      }
      for (Editor* ed : m_editors) {
        if (ed)
          ed->invalidate();
      }
    };

    m_symXCheck->Click.connect([applySymmetry] { applySymmetry(false); });
    m_symYCheck->Click.connect([applySymmetry] { applySymmetry(false); });
    centerSymBtn->Click.connect([applySymmetry] { applySymmetry(true); });

    genFaceDepthBtn->Click.connect([this] { generateEditableFaceDepthLayer(); });
    renderBtn->Click.connect([this] { bakeToActiveDocLayers(); });
    exportObjBtn->Click.connect([this] { exportToWavefrontObj(); });
    exitBtn->Click.connect([this] {
      syncAllLinkedCels();
      restoreWheelZoomPref();
      m_docView->exitMultiPaneMode();
    });
  }

  struct PresetItem {
    std::string label;
    int yaw;
    int pitch;
    int frame;
  };
  std::vector<PresetItem> m_presets;
  bool m_updatingCombo = false;

  void populateTagCombo()
  {
    m_updatingCombo = true;
    m_tagCombo->deleteAllItems();
    m_presets.clear();

    auto add = [&](const std::string& lbl, int yaw, int pitch, int fr = -1) {
      m_presets.push_back({ lbl, yaw, pitch, fr });
      m_tagCombo->addItem(lbl);
    };

    add("Fast Rotation", 45, 30);
    add("Iso 2:1 (Yaw 45°, Cam 30°)", 45, 30);
    add("Front View (Yaw 0°, Cam 0°)", 0, 0);
    add("Back View (Yaw 180°, Cam 0°)", 180, 0);
    add("Right View (Yaw -90°, Cam 0°)", -90, 0);
    add("Left View (Yaw 90°, Cam 0°)", 90, 0);
    add("Top View (Yaw 45°, Cam 60°)", 45, 60);
    add("Bottom View (Yaw 45°, Cam -60°)", 45, -60);
    add("Dimetric (Yaw 30°, Cam 26°)", 30, 26);
    add("Trimetric (Yaw 60°, Cam 35°)", 60, 35);

    Doc* doc = m_docView->document();
    if (doc && doc->sprite()) {
      for (const doc::Tag* tag : doc->sprite()->tags()) {
        int ty = 45, tp = 30;
        const std::string& udata = tag->userData().text();
        if (std::sscanf(udata.c_str(), "easymetric:yaw=%d,pitch=%d", &ty, &tp) != 2) {
          std::sscanf(tag->name().c_str(), "Yaw%d_Cam%d", &ty, &tp);
        }
        add(fmt::format("Tag: {} [f{}..{}] ({}°, {}°)",
                        tag->name(),
                        tag->fromFrame() + 1,
                        tag->toFrame() + 1,
                        ty,
                        tp),
            ty,
            tp,
            int(tag->fromFrame()));
      }
    }

    m_tagCombo->setSelectedItemIndex(0);
    m_updatingCombo = false;
  }

  void onTagSelected()
  {
    if (m_updatingCombo)
      return;
    const int idx = m_tagCombo->getSelectedItemIndex();
    if (idx < 0 || idx >= int(m_presets.size()))
      return;
    const auto& p = m_presets[idx];
    m_yawSlider->setValue(p.yaw);
    m_pitchSlider->setValue(p.pitch);
    if (p.frame >= 0 && m_docView->editor())
      m_docView->editor()->setFrame(doc::frame_t(p.frame));
    m_lastHash = 0;
    pollAndRender(true);
  }

  void saveCurrentAngleAsTag()
  {
    Doc* doc = m_docView->document();
    if (!doc || !doc->sprite())
      return;
    const doc::frame_t fr = (m_docView->editor() ? m_docView->editor()->frame() : doc::frame_t(0));
    const int yaw = m_yawSlider->getValue();
    const int pitch = m_pitchSlider->getValue();
    const std::string tagName = fmt::format("Yaw{}_Cam{}", yaw, pitch);
    const std::string meta = fmt::format("easymetric:yaw={},pitch={}", yaw, pitch);

    try {
      Tx tx(doc, "Save Camera Tag", ModifyDocument);
      doc::Tag* tag = new doc::Tag(fr, fr);
      tag->setName(tagName);
      tag->userData().setText(meta);
      tx(new cmd::AddTag(doc->sprite(), tag));
      tx.commit();
      doc->notifyGeneralUpdate();
      populateTagCombo();
      m_updatingCombo = true;
      m_tagCombo->setSelectedItemIndex(int(m_presets.size()) - 1);
      m_updatingCombo = false;
      if (auto* sb = StatusBar::instance())
        sb->showTip(1500, fmt::format("Saved tag '{}'", tagName));
    }
    catch (const std::exception& e) {
      Console::showException(e);
    }
  }

  void generateEditableFaceDepthLayer()
  {
    Doc* doc = m_docView->document();
    if (!doc || !doc->sprite())
      return;
    doc::Sprite* spr = doc->sprite();
    const doc::frame_t fr = (m_docView->editor() ? m_docView->editor()->frame() : doc::frame_t(0));

    SixSurfaceData data;
    extractSixSurfaces(doc, fr, data);

    try {
      Tx tx(doc, "Render Editable Face Depths", ModifyDocument);
      DocApi api = doc->getApi(tx);

      doc::LayerImage* depthLayer = findOrCreateLayer(doc, api, spr, "Surface_DEPTH");
      depthLayer->setVisible(true);
      depthLayer->userData().setColor(doc::rgba(148, 163, 184, 255));

      doc::ImageRef depthImg(doc::Image::create(doc::IMAGE_RGB, spr->width(), spr->height()));
      depthImg->clear(0);

      for (int f = 0; f < kFaceCount; ++f) {
        const int fw = data.w[f];
        const int fh = data.h[f];
        if (fw <= 0 || fh <= 0)
          continue;

        const float cx = (fw - 1) * 0.5f;
        const float cy = (fh - 1) * 0.5f;
        const float maxR = std::max(1.0f, std::sqrt(cx * cx + cy * cy));

        // Shared sheets: write each face into its own UV cell
        // (data.bounds[f] is that cell when shared layers exist),
        // instead of writing every face at (0, 0) where each
        // face would overwrite the previous one.
        const int ox = (data.bounds[f].w > 0) ? data.bounds[f].x : 0;
        const int oy = (data.bounds[f].w > 0) ? data.bounds[f].y : 0;
        for (int v = 0; v < fh; ++v) {
          if (oy + v < 0 || oy + v >= depthImg->height())
            continue;
          for (int u = 0; u < fw; ++u) {
            if (ox + u < 0 || ox + u >= depthImg->width())
              continue;
            const uint32_t col = data.sample(f, u, v);
            if (doc::rgba_geta(col) <= 16)
              continue;

            const float luma = pixelLuma(col);
            const float dist = std::sqrt((u - cx) * (u - cx) + (v - cy) * (v - cy)) / maxR;
            const float dome = std::clamp(1.0f - dist * 0.35f, 0.0f, 1.0f);
            const uint8_t hByte = clampByte((0.35f + luma * 0.45f + dome * 0.20f) * 255.0f);
            doc::put_pixel(depthImg.get(), ox + u, oy + v, doc::rgba(hByte, hByte, hByte, 255));
          }
        }
      }

      if (doc::Cel* cel = depthLayer->cel(fr))
        api.replaceImage(spr, cel->imageRef(), depthImg);
      else
        api.addCel(depthLayer, fr, depthImg);

      tx.commit();
      doc->notifyGeneralUpdate();
      if (auto* sb = StatusBar::instance())
        sb->showTip(2000, "Rendered face depths to 'Surface_DEPTH' layer");
    }
    catch (const std::exception& e) {
      Console::showException(e);
    }
    m_lastHash = 0;
    pollAndRender(true);
  }

  bool extractSixSurfaces(Doc* doc, doc::frame_t frame, SixSurfaceData& out)
  {
    const bool useDepth = (m_useFaceDepthCheck && m_useFaceDepthCheck->isSelected());
    return app::extractSixSurfaces(doc, frame, out, useDepth, m_docView, &m_colLinked);
  }

  void pollAndRender(bool force)
  {
    if (!isVisible())
      return;
    Doc* doc = m_docView->document();
    const doc::frame_t frame = (m_docView->editor() ? m_docView->editor()->frame() :
                                                      doc::frame_t(0));

    // Canvas <-> UV reconciliation, and per-face editing
    // confinement (see below). Both run before extraction so
    // the Preview samples the reconciled UV layout.
    if (doc && doc->sprite()) {
      doc::Sprite* spr = doc->sprite();
      SharedSurfaceLayers shared = findSharedLayers(spr);
      if (shared.baseLayer || shared.textureLayer) {
        // Canvas resize -> UV mapping: if the canvas was
        // resized by hand (sprite size no longer equals the
        // exact sheet size for the current face sizes), scale
        // each face size by the same ratio and rebuild the
        // sheets, so the canvas ends up exactly the sum of
        // the new UV rects (rounding may move it a few pixels
        // from the hand-resized size). If the canvas already
        // matches the exact sheet size, nothing happens here,
        // so a face resize via the dimension controls is not
        // double-applied.
        const gfx::Size exact = sharedSheetSize(spr);
        if (exact.w > 0 && (spr->width() != exact.w || spr->height() != exact.h)) {
          int oldW[kFaceCount], oldH[kFaceCount];
          sharedFaceSizes(spr, oldW, oldH);
          const double sx = (exact.w > 0 && spr->width() != exact.w) ?
                              double(spr->width()) / double(exact.w) :
                              1.0;
          const double sy = (exact.h > 0 && spr->height() != exact.h) ?
                              double(spr->height()) / double(exact.h) :
                              1.0;
          int newW[kFaceCount], newH[kFaceCount];
          for (int f = 0; f < kFaceCount; ++f) {
            newW[f] = std::clamp(int(std::round(oldW[f] * sx)), 4, 256);
            newH[f] = std::clamp(int(std::round(oldH[f] * sy)), 4, 256);
          }
          rebuildSharedSheets(doc, newW, newH);
          initLayersAndFraming();
          force = true;
        }

        // Per-face independence: when the active panel changes
        // to a different surface (and that panel is in its
        // standby state, not mid-stroke/mid-move), set the
        // document selection to that face's UV rect, without
        // adding an undo step. Brush painting and a Move are
        // then confined to that face's pixels: moving Top's
        // content does not move Front's, Back's, etc., because
        // the other faces' pixels are outside the selection.
        // It is set only on a face change, so a user selection
        // inside a face (e.g. a sub-region to move) is not
        // overwritten every poll. Limitation, stated honestly:
        // a sub-selection dragged far enough to cross into a
        // neighbouring UV rect will still land in that rect's
        // sampled area, because the sheet is one image; what
        // is guaranteed is that the other faces' pixels do
        // not move with it. On studio exit the original
        // selection is restored (destructor).
        Editor* curEd = m_docView->editor();
        int activeFace = -1;
        for (int f = 0; f < kFaceCount; ++f)
          if (curEd == m_editors[f])
            activeFace = f;
        if (activeFace != m_lastActiveFace) {
          if (activeFace >= 0 && curEd && dynamic_cast<StandbyState*>(curEd->getState().get())) {
            doc::Mask mask;
            mask.replace(sharedFaceRect(spr, activeFace));
            doc->setMask(&mask);
            doc->setMaskVisible(true);
            doc->notifySelectionChanged();
            m_lastActiveFace = activeFace;
          }
          else if (activeFace < 0) {
            m_lastActiveFace = -1;
          }
        }
      }
    }

    SixSurfaceData data;
    if (!extractSixSurfaces(doc, frame, data)) {
      data.init(24);
      for (int f = 0; f < kFaceCount; ++f)
        for (int v = 0; v < 24; ++v)
          for (int u = 0; u < 24; ++u)
            data.pixels[f][v * 24 + u] = starterSurfacePixel(f, u, v, 24, 24);
    }

    // Check if any face slice dimensions changed and Link Dims is active
    if (m_linkDimensionsCheck && m_linkDimensionsCheck->isSelected()) {
      int changedFace = -1;
      for (int f = 0; f < kFaceCount; ++f) {
        if (m_lastFaceW[f] > 0 && (data.w[f] != m_lastFaceW[f] || data.h[f] != m_lastFaceH[f])) {
          changedFace = f;
          break;
        }
      }
      if (changedFace >= 0) {
        propagateLinkedDimensions(doc, data, changedFace, data.w[changedFace], data.h[changedFace]);
        initLayersAndFraming();
        for (int f = 0; f < kFaceCount; ++f) {
          m_lastFaceW[f] = data.w[f];
          m_lastFaceH[f] = data.h[f];
        }
        force = true;
      }
      else {
        for (int f = 0; f < kFaceCount; ++f) {
          m_lastFaceW[f] = data.w[f];
          m_lastFaceH[f] = data.h[f];
        }
      }
    }
    else {
      for (int f = 0; f < kFaceCount; ++f) {
        m_lastFaceW[f] = data.w[f];
        m_lastFaceH[f] = data.h[f];
      }
    }

    const int yawDeg = m_yawSlider->getValue();
    const int pitchDeg = m_pitchSlider->getValue();
    const int pivX = m_pivXSlider->getValue();
    const int pivY = m_pivYSlider->getValue();
    const int pivZ = m_pivZSlider->getValue();
    const int renderStyle = std::max(0, m_renderTypeCombo->getSelectedItemIndex());
    const bool addOutline = m_strokeCheck->isSelected();
    const bool allowCutouts = m_cutoutCheck->isSelected();
    const bool showDiff = m_mapDiffuseCheck->isSelected();
    const bool showNorm = m_mapNormalCheck->isSelected();
    const bool showDept = m_mapDepthCheck->isSelected();

    uint64_t h = 14695981039346656037ULL;
    auto mix = [&h](uint64_t v) {
      h ^= v;
      h *= 1099511628211ULL;
    };
    mix(uint64_t(yawDeg + 360));
    mix(uint64_t(pitchDeg + 360));
    mix(uint64_t(pivX));
    mix(uint64_t(pivY));
    mix(uint64_t(pivZ));
    mix(uint64_t(renderStyle));
    mix(uint64_t(addOutline ? 1 : 0));
    mix(uint64_t(allowCutouts ? 1 : 0));
    mix(uint64_t(showDiff ? 1 : 0));
    mix(uint64_t(showNorm ? 1 : 0));
    mix(uint64_t(showDept ? 1 : 0));

    for (int f = 0; f < kFaceCount; ++f) {
      mix(uint64_t(data.w[f]));
      mix(uint64_t(data.h[f]));
      const uint32_t* p = data.pixels[f].data();
      const size_t n = data.pixels[f].size();
      for (size_t i = 0; i < n; ++i)
        mix(p[i]);
      if (data.hasCustomDepthLayer) {
        const uint8_t* dp = data.depthPixels[f].data();
        for (size_t i = 0; i < n; ++i)
          mix(dp[i]);
      }
    }

    if (!force && h == m_lastHash)
      return;
    m_lastHash = h;

    render3DModel(data,
                  yawDeg,
                  pitchDeg,
                  pivX,
                  pivY,
                  pivZ,
                  renderStyle,
                  addOutline,
                  allowCutouts,
                  showDiff,
                  showNorm,
                  showDept);

    if (m_liveBakeCheck->isSelected() && doc && doc->sprite())
      bakeToActiveDocLayers();
  }

  void render3DModel(const SixSurfaceData& data,
                     int yawDeg,
                     int pitchDeg,
                     int pivX,
                     int pivY,
                     int pivZ,
                     int renderStyle,
                     bool addOutline,
                     bool allowCutouts,
                     bool showDiff,
                     bool showNorm,
                     bool showDept)
  {
    Render3DResult res = compute3DPreviewImages(data,
                                                yawDeg,
                                                pitchDeg,
                                                pivX,
                                                pivY,
                                                pivZ,
                                                renderStyle,
                                                addOutline,
                                                allowCutouts);
    m_lastDiffImg = res.diffImg;
    m_lastNormImg = res.normImg;
    m_lastDeptImg = res.deptImg;

    if (!m_previewWidget)
      return;

    auto makeSurface = [](const doc::Image* img) -> os::SurfaceRef {
      if (!img)
        return nullptr;
      const int w = img->width();
      const int h = img->height();
      os::SurfaceRef surf = os::System::instance()->makeRgbaSurface(w, h);
      convert_image_to_surface(img, nullptr, surf.get(), 0, 0, 0, 0, w, h);
      return surf;
    };

    m_previewWidget->setSurfaces(makeSurface(m_lastDiffImg.get()),
                                 makeSurface(m_lastNormImg.get()),
                                 makeSurface(m_lastDeptImg.get()),
                                 yawDeg,
                                 pitchDeg,
                                 int(res.cxScreen),
                                 int(res.cyScreen),
                                 showDiff,
                                 showNorm,
                                 showDept);
  }

  void bakeToActiveDocLayers()
  {
    Doc* doc = m_docView->document();
    if (!doc || !doc->sprite() || !m_lastDiffImg || !m_lastNormImg || !m_lastDeptImg)
      return;

    doc::Sprite* spr = doc->sprite();
    const doc::frame_t fr = (m_docView->editor() ? m_docView->editor()->frame() : doc::frame_t(0));

    try {
      Tx tx(doc, "Render Easymetric 3D + Normal + Depth Layers", ModifyDocument);
      DocApi api = doc->getApi(tx);

      auto writeLayerImg = [&](const char* layerName, const doc::Image* srcImg) {
        doc::LayerImage* lyr = findOrCreateLayer(doc, api, spr, layerName);
        doc::ImageRef copyImg(doc::Image::createCopy(srcImg));
        if (doc::Cel* cel = lyr->cel(fr))
          api.replaceImage(spr, cel->imageRef(), copyImg);
        else
          api.addCel(lyr, fr, copyImg);
      };

      const bool anyChecked = m_mapDiffuseCheck->isSelected() || m_mapNormalCheck->isSelected() ||
                              m_mapDepthCheck->isSelected();
      if (!anyChecked || m_mapDepthCheck->isSelected())
        writeLayerImg("Iso_Depth", m_lastDeptImg.get());
      if (!anyChecked || m_mapNormalCheck->isSelected())
        writeLayerImg("Iso_Normal", m_lastNormImg.get());
      if (!anyChecked || m_mapDiffuseCheck->isSelected())
        writeLayerImg("Iso_Diffuse", m_lastDiffImg.get());

      for (int c = 0; c < 3; ++c) {
        if (m_colLinked[c]) {
          const int srcFace = c * 2;
          const int dstFace = c * 2 + 1;
          const std::string srcName = std::string("Surface_") + kFaceSpecs[srcFace].sliceName;
          const std::string dstName = std::string("Surface_") + kFaceSpecs[dstFace].sliceName;
          doc::LayerImage* srcLyr = findLayerByName(spr, srcName);
          doc::LayerImage* dstLyr = findLayerByName(spr, dstName);
          if (srcLyr && dstLyr) {
            doc::Cel* srcCel = srcLyr->cel(fr);
            doc::Cel* dstCel = dstLyr->cel(fr);
            if (srcCel && srcCel->image()) {
              doc::ImageRef copyImg(doc::Image::createCopy(srcCel->image()));
              if (dstCel) {
                api.replaceImage(spr, dstCel->imageRef(), copyImg);
                api.setCelPosition(spr, dstCel, srcCel->x(), srcCel->y());
              }
              else {
                api.addCel(dstLyr, fr, copyImg);
              }
            }
          }
        }
      }

      tx.commit();
      doc->notifyGeneralUpdate();
      if (auto* sb = StatusBar::instance())
        sb->showTip(1500, "Rendered 3D model to Iso_Diffuse / Iso_Normal / Iso_Depth layers");
    }
    catch (const std::exception& e) {
      Console::showException(e);
    }
  }

  void exportToWavefrontObj()
  {
    Doc* doc = m_docView->document();
    if (!doc || !doc->sprite())
      return;

    std::string defaultPath = doc->filename();
    if (defaultPath.empty())
      defaultPath = "easymetric_model.obj";
    else
      defaultPath = base::replace_extension(defaultPath, "obj");

    base::paths exts = { "obj" };
    base::paths selected;
    if (!show_file_selector("Export Easymetric 3D Model as Wavefront .OBJ",
                            defaultPath,
                            exts,
                            FileSelectorType::Save,
                            selected) ||
        selected.empty()) {
      return;
    }

    const std::string objPath = base::replace_extension(selected.front(), "obj");
    const std::string mtlPath = base::replace_extension(objPath, "mtl");
    const std::string texPath = base::replace_extension(objPath, "png");
    const std::string mtlFileName = base::get_file_name(mtlPath);
    const std::string texFileName = base::get_file_name(texPath);

    const doc::frame_t fr = (m_docView->editor() ? m_docView->editor()->frame() : doc::frame_t(0));
    SixSurfaceData data;
    extractSixSurfaces(doc, fr, data);

    // Texture atlas
    {
      doc::FramesSequence seq;
      seq.insert(fr);
      std::unique_ptr<FileOp> fop(FileOp::createSaveDocumentOperation(
        UIContext::instance(),
        FileOpROI(doc, doc->sprite()->bounds(), "", "", seq, false),
        texPath,
        "",
        false));
      if (fop && !fop->hasError()) {
        fop->operate();
        fop->done();
      }
    }

    // MTL file
    {
      std::ofstream mtl(mtlPath);
      if (mtl.is_open()) {
        mtl << "# Aseprite 2.0 Easymetric Material\n"
            << "newmtl EasymetricMat\n"
            << "Ka 1.000 1.000 1.000\n"
            << "Kd 1.000 1.000 1.000\n"
            << "Ks 0.000 0.000 0.000\n"
            << "d 1.0\n"
            << "illum 1\n"
            << "map_Kd " << texFileName << "\n";
      }
    }

    // OBJ file
    {
      std::ofstream obj(objPath);
      if (!obj.is_open())
        return;

      const int pivX = m_pivXSlider ? m_pivXSlider->getValue() : 50;
      const int pivY = m_pivYSlider ? m_pivYSlider->getValue() : 50;
      const int pivZ = m_pivZSlider ? m_pivZSlider->getValue() : 50;
      SculptedVoxelVolume vol = buildSculptedVolume(data, pivX, pivY, pivZ);

      const float sprW = std::max(1.0f, float(doc->sprite()->width()));
      const float sprH = std::max(1.0f, float(doc->sprite()->height()));
      const float halfX =
        std::max({ data.w[kFaceFront], data.w[kFaceBack], data.w[kFaceTop], data.w[kFaceDown] }) *
        0.5f;
      const float halfY =
        std::max({ data.h[kFaceFront], data.h[kFaceBack], data.h[kFaceLeft], data.h[kFaceRight] }) *
        0.5f;
      const float halfZ =
        std::max({ data.w[kFaceLeft], data.w[kFaceRight], data.h[kFaceTop], data.h[kFaceDown] }) *
        0.5f;

      const float cellSizeX = (2.0f * halfX) / float(vol.vx);
      const float cellSizeY = (2.0f * halfY) / float(vol.vy);
      const float cellSizeZ = (2.0f * halfZ) / float(vol.vz);

      obj << "# Aseprite 2.0 Easymetric Sculpted 3D Model\n"
          << "mtllib " << mtlFileName << "\n"
          << "usemtl EasymetricMat\n"
          << "o EasymetricSculptedModel\n";

      int vIdx = 1, vtIdx = 1, vnIdx = 1;

      auto emitQuad = [&](float x0,
                          float y0,
                          float z0,
                          float x1,
                          float y1,
                          float z1,
                          float x2,
                          float y2,
                          float z2,
                          float x3,
                          float y3,
                          float z3,
                          float nx,
                          float ny,
                          float nz,
                          float u0,
                          float v0,
                          float u1,
                          float v1) {
        obj << "v " << x0 << " " << y0 << " " << z0 << "\n"
            << "v " << x1 << " " << y1 << " " << z1 << "\n"
            << "v " << x2 << " " << y2 << " " << z2 << "\n"
            << "v " << x3 << " " << y3 << " " << z3 << "\n";
        obj << "vt " << u0 << " " << v0 << "\n"
            << "vt " << u1 << " " << v0 << "\n"
            << "vt " << u1 << " " << v1 << "\n"
            << "vt " << u0 << " " << v1 << "\n";
        obj << "vn " << nx << " " << ny << " " << nz << "\n";
        obj << "f " << (vIdx + 0) << "/" << (vtIdx + 0) << "/" << vnIdx << " " << (vIdx + 1) << "/"
            << (vtIdx + 1) << "/" << vnIdx << " " << (vIdx + 2) << "/" << (vtIdx + 2) << "/"
            << vnIdx << " " << (vIdx + 3) << "/" << (vtIdx + 3) << "/" << vnIdx << "\n";
        vIdx += 4;
        vtIdx += 4;
        vnIdx += 1;
      };

      for (int vy = 0; vy < vol.vy; ++vy) {
        const float yMax = halfY - float(vy) * cellSizeY;
        const float yMin = halfY - float(vy + 1) * cellSizeY;

        for (int vz = 0; vz < vol.vz; ++vz) {
          const float zMin = -halfZ + float(vz) * cellSizeZ;
          const float zMax = -halfZ + float(vz + 1) * cellSizeZ;

          for (int vx = 0; vx < vol.vx; ++vx) {
            if (!vol.isSolid(vx, vy, vz))
              continue;

            const float xMin = -halfX + float(vx) * cellSizeX;
            const float xMax = -halfX + float(vx + 1) * cellSizeX;

            // 1. Front face (+Z)
            if (vz + 1 >= vol.vz || !vol.isSolid(vx, vy, vz + 1)) {
              const float u0 = float(vx) / float(vol.vx);
              const float u1 = float(vx + 1) / float(vol.vx);
              const float v0 = 1.0f - float(vy + 1) / float(vol.vy);
              const float v1 = 1.0f - float(vy) / float(vol.vy);
              emitQuad(xMin,
                       yMin,
                       zMax,
                       xMax,
                       yMin,
                       zMax,
                       xMax,
                       yMax,
                       zMax,
                       xMin,
                       yMax,
                       zMax,
                       0.0f,
                       0.0f,
                       1.0f,
                       u0,
                       v0,
                       u1,
                       v1);
            }

            // 2. Back face (-Z)
            if (vz - 1 < 0 || !vol.isSolid(vx, vy, vz - 1)) {
              const float u0 = float(vol.vx - 1 - vx) / float(vol.vx);
              const float u1 = float(vol.vx - vx) / float(vol.vx);
              const float v0 = 1.0f - float(vy + 1) / float(vol.vy);
              const float v1 = 1.0f - float(vy) / float(vol.vy);
              emitQuad(xMax,
                       yMin,
                       zMin,
                       xMin,
                       yMin,
                       zMin,
                       xMin,
                       yMax,
                       zMin,
                       xMax,
                       yMax,
                       zMin,
                       0.0f,
                       0.0f,
                       -1.0f,
                       u0,
                       v0,
                       u1,
                       v1);
            }

            // 3. Top face (+Y)
            if (vy - 1 < 0 || !vol.isSolid(vx, vy - 1, vz)) {
              const float u0 = float(vx) / float(vol.vx);
              const float u1 = float(vx + 1) / float(vol.vx);
              const float v0 = 1.0f - float(vz + 1) / float(vol.vz);
              const float v1 = 1.0f - float(vz) / float(vol.vz);
              emitQuad(xMin,
                       yMax,
                       zMax,
                       xMax,
                       yMax,
                       zMax,
                       xMax,
                       yMax,
                       zMin,
                       xMin,
                       yMax,
                       zMin,
                       0.0f,
                       1.0f,
                       0.0f,
                       u0,
                       v0,
                       u1,
                       v1);
            }

            // 4. Bottom face (-Y)
            if (vy + 1 >= vol.vy || !vol.isSolid(vx, vy + 1, vz)) {
              const float u0 = float(vx) / float(vol.vx);
              const float u1 = float(vx + 1) / float(vol.vx);
              const float v0 = 1.0f - float(vol.vz - vz) / float(vol.vz);
              const float v1 = 1.0f - float(vol.vz - 1 - vz) / float(vol.vz);
              emitQuad(xMin,
                       yMin,
                       zMin,
                       xMax,
                       yMin,
                       zMin,
                       xMax,
                       yMin,
                       zMax,
                       xMin,
                       yMin,
                       zMax,
                       0.0f,
                       -1.0f,
                       0.0f,
                       u0,
                       v0,
                       u1,
                       v1);
            }

            // 5. Right face (+X)
            if (vx + 1 >= vol.vx || !vol.isSolid(vx + 1, vy, vz)) {
              const float u0 = float(vol.vz - 1 - vz) / float(vol.vz);
              const float u1 = float(vol.vz - vz) / float(vol.vz);
              const float v0 = 1.0f - float(vy + 1) / float(vol.vy);
              const float v1 = 1.0f - float(vy) / float(vol.vy);
              emitQuad(xMax,
                       yMin,
                       zMax,
                       xMax,
                       yMin,
                       zMin,
                       xMax,
                       yMax,
                       zMin,
                       xMax,
                       yMax,
                       zMax,
                       1.0f,
                       0.0f,
                       0.0f,
                       u0,
                       v0,
                       u1,
                       v1);
            }

            // 6. Left face (-X)
            if (vx - 1 < 0 || !vol.isSolid(vx - 1, vy, vz)) {
              const float u0 = float(vz) / float(vol.vz);
              const float u1 = float(vz + 1) / float(vol.vz);
              const float v0 = 1.0f - float(vy + 1) / float(vol.vy);
              const float v1 = 1.0f - float(vy) / float(vol.vy);
              emitQuad(xMin,
                       yMin,
                       zMin,
                       xMin,
                       yMin,
                       zMax,
                       xMin,
                       yMax,
                       zMax,
                       xMin,
                       yMax,
                       zMin,
                       -1.0f,
                       0.0f,
                       0.0f,
                       u0,
                       v0,
                       u1,
                       v1);
            }
          }
        }
      }
    }

    if (auto* sb = StatusBar::instance())
      sb->showTip(2500, fmt::format("Exported 3D Wavefront OBJ to {}", objPath));
  }

  DocView* m_docView;
  std::array<PaneHeader*, 8> m_headers;
  std::array<ui::VBox*, 8> m_panes;

  // 6 Surface Editors
  std::array<Editor*, kFaceCount> m_editors;
  std::array<ui::View*, kFaceCount> m_views;
  std::array<LinkToggleButton*, 3> m_linkButtons;
  std::array<bool, 3> m_colLinked = { false, false, false };

  // Preview & Fields
  EasymetricPreviewWidget* m_previewWidget;
  FieldsPanelBox* m_fieldsBox;
  ui::View* m_fieldsView = nullptr;
  bool m_savedZoomWithWheel = true;
  bool m_restoreZoomWithWheel = false;

  // Fields controls
  ui::Slider* m_yawSlider;
  ui::Slider* m_pitchSlider;
  ui::Slider* m_pivXSlider;
  ui::Slider* m_pivYSlider;
  ui::Slider* m_pivZSlider;
  ui::ComboBox* m_tagCombo;
  ui::CheckBox* m_mapDiffuseCheck;
  ui::CheckBox* m_mapNormalCheck;
  ui::CheckBox* m_mapDepthCheck;
  ui::ComboBox* m_renderTypeCombo;
  ui::Label* m_zoomLabel = nullptr;
  ui::CheckBox* m_strokeCheck;
  ui::CheckBox* m_cutoutCheck;
  ui::CheckBox* m_lockBordersCheck;
  ui::CheckBox* m_symXCheck;
  ui::CheckBox* m_symYCheck;
  ui::CheckBox* m_useFaceDepthCheck;
  ui::CheckBox* m_liveBakeCheck;
  ui::CheckBox* m_linkDimensionsCheck;

  int m_lastFaceW[kFaceCount] = { 0, 0, 0, 0, 0, 0 };
  int m_lastFaceH[kFaceCount] = { 0, 0, 0, 0, 0, 0 };

  // Splitter state: pixel widths for cols 0..2 (col 3 is the
  // remainder), top-row pixel height, and hover/drag tracking.
  // 0 in m_hoverSplitter/m_dragSplitter means no splitter.
  // m_colWpx/m_rowHpx are mutable because computePaneLayout
  // (const, called from paint/hit-testing) lazily initialises
  // them on first layout; after that only a drag changes them.
  mutable int m_colWpx[3] = { 0, 0, 0 };
  mutable int m_rowHpx = 0;
  mutable bool m_colInit = false;
  int m_hoverSplitter = 0;
  int m_dragSplitter = 0;
  gfx::Point m_dragStart;
  int m_dragColWpx[3] = { 0, 0, 0 };
  int m_dragPreviewW = 0;
  int m_dragRowHpx = 0;
  int m_lastActiveFace = -1;
  doc::MaskRef m_savedMask;

  ui::Timer m_timer;
  uint64_t m_lastHash = 0;
  doc::ImageRef m_lastDiffImg;
  doc::ImageRef m_lastNormImg;
  doc::ImageRef m_lastDeptImg;
};

} // anonymous namespace

bool easymetricStudioChecked(Context* context)
{
  if (!UIContext::instance())
    return false;
  DocView* view = UIContext::instance()->activeView();
  return view && view->isMultiPane();
}

void executeEasymetricStudio(Context* context, const Params& m_params)
{
  Doc* doc = context ? static_cast<Doc*>(context->activeDocument()) : nullptr;
  if (!doc || !doc->sprite())
    return;

  // Headless / Parameterized Execution
  if (m_params.has_param("action")) {
    const std::string action = m_params.get("action");
    if (action == "linkDims") {
      std::string faceStr = m_params.has_param("face") ? m_params.get("face") : "top";
      int w = m_params.has_param("width") ? std::stoi(m_params.get("width")) : 32;
      int h = m_params.has_param("height") ? std::stoi(m_params.get("height")) : 32;
      int faceIdx = kFaceTop;
      if (faceStr == "top" || faceStr == "TOP")
        faceIdx = kFaceTop;
      else if (faceStr == "down" || faceStr == "bottom" || faceStr == "DOWN")
        faceIdx = kFaceDown;
      else if (faceStr == "front" || faceStr == "FRONT")
        faceIdx = kFaceFront;
      else if (faceStr == "back" || faceStr == "BACK")
        faceIdx = kFaceBack;
      else if (faceStr == "right" || faceStr == "RIGHT")
        faceIdx = kFaceRight;
      else if (faceStr == "left" || faceStr == "LEFT")
        faceIdx = kFaceLeft;

      propagateLinkedDimensions(doc, faceIdx, w, h);
    }
    else if (action == "setupSmartObjects") {
      setupTwoLayerStructure(doc, true);
    }
    else if (action == "convertToGroups") {
      convertSmartObjectsToGroups(doc);
    }
    else if (action == "setupSurfaces") {
      setupSixSurfacesOnDoc(doc, true);
    }
    else if (action == "setupGroups") {
      setupTwoLayerStructure(doc, false);
    }
    else if (action == "bake3D" || action == "render3D") {
      int yaw = m_params.has_param("yaw") ? std::stoi(m_params.get("yaw")) : 45;
      int pitch = m_params.has_param("pitch") ? std::stoi(m_params.get("pitch")) : 30;
      bake3DToDoc(doc, yaw, pitch);
    }
    return;
  }

  if (!context->isUIAvailable())
    return;

  UIContext* uiCtx = UIContext::instance();
  DocView* docView = uiCtx ? uiCtx->activeView() : nullptr;
  if (!docView || !docView->document())
    return;

  if (docView->isMultiPane()) {
    if (auto* multiPane = dynamic_cast<EasymetricMultiPaneView*>(docView->multiPaneLayout())) {
      multiPane->syncAllLinkedCels();
    }
    docView->exitMultiPaneMode();
    if (auto* sb = StatusBar::instance())
      sb->showTip(1200, "Easymetric 6-Surface Studio: Switched to standard view");
  }
  else {
    setupSixSurfacesOnDoc(docView->document(), true);
    auto* multiPane = new EasymetricMultiPaneView(docView);
    std::vector<Editor*> editors = multiPane->surfaceEditors();
    docView->enterMultiPaneMode(multiPane, editors);
    multiPane->initLayersAndFraming();
    if (auto* sb = StatusBar::instance())
      sb->showTip(1500, "Easymetric 6-Surface Studio: 4-Column Workspace Grid Active");
  }
}

} // namespace app
