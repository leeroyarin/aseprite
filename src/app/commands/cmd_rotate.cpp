// Aseprite
// Copyright (C) 2019-2024  Igara Studio S.A.
// Copyright (C) 2001-2018  David Capello
//
// This program is distributed under the terms of
// the End-User License Agreement for Aseprite.

#ifdef HAVE_CONFIG_H
  #include "config.h"
#endif

#include "app/app.h"
#include "app/cmd/set_cel_bounds.h"
#include "app/cmd/set_smart_object_transform.h"
#include "app/commands/cmd_rotate.h"
#include "app/commands/params.h"
#include "app/doc_api.h"
#include "app/i18n/strings.h"
#include "app/modules/gui.h"
#include "app/sprite_job.h"
#include "app/tools/tool_box.h"
#include "app/tx.h"
#include "app/ui/editor/editor.h"
#include "app/ui/status_bar.h"
#include "app/ui/timeline/timeline.h"
#include "app/ui/toolbar.h"
#include "base/convert_to.h"
#include "doc/cel.h"
#include "doc/cels_range.h"
#include "doc/image.h"
#include "doc/layer.h"
#include "doc/layer_smart_object.h"
#include "doc/mask.h"
#include "doc/sprite.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace app {

namespace {

void collect_smart_objects_recursive(doc::LayerGroup* group,
                                     std::vector<doc::LayerSmartObject*>& out)
{
  if (!group)
    return;
  for (doc::Layer* lay : group->layers()) {
    if (lay->isSmartObject()) {
      out.push_back(static_cast<doc::LayerSmartObject*>(lay));
    }
    else if (lay->isGroup()) {
      collect_smart_objects_recursive(static_cast<doc::LayerGroup*>(lay), out);
    }
  }
}

double wrap_angle_deg(double deg)
{
  double wrapped = std::fmod(deg, 360.0);
  if (std::abs(wrapped) < 1e-9 || std::abs(std::abs(wrapped) - 360.0) < 1e-9)
    return 0.0;
  return wrapped;
}

} // namespace

class RotateJob : public SpriteJob {
  int m_angle;
  CelList m_cels;
  std::vector<doc::LayerSmartObject*> m_smartLayers;
  bool m_rotateSprite;

public:
  RotateJob(Context* ctx,
            Doc* doc,
            const std::string& jobName,
            int angle,
            const CelList& cels,
            const std::vector<doc::LayerSmartObject*>& smartLayers,
            const bool rotateSprite,
            const bool showProgress)
    : SpriteJob(ctx, doc, jobName, showProgress)
    , m_cels(cels)
    , m_smartLayers(smartLayers)
    , m_rotateSprite(rotateSprite)
  {
    m_angle = angle;
  }

protected:
  template<typename T>
  void rotate_rect(gfx::RectT<T>& newBounds)
  {
    const gfx::RectT<T> bounds = newBounds;
    switch (m_angle) {
      case 180:
        newBounds.x = sprite()->width() - bounds.x - bounds.w;
        newBounds.y = sprite()->height() - bounds.y - bounds.h;
        break;
      case 90:
        newBounds.x = sprite()->height() - bounds.y - bounds.h;
        newBounds.y = bounds.x;
        newBounds.w = bounds.h;
        newBounds.h = bounds.w;
        break;
      case -90:
        newBounds.x = bounds.y;
        newBounds.y = sprite()->width() - bounds.x - bounds.w;
        newBounds.w = bounds.h;
        newBounds.h = bounds.w;
        break;
    }
  }

  // [working thread]
  void onSpriteJob(Tx& tx) override
  {
    DocApi api = document()->getApi(tx);

    // 1) Rotate cel positions
    for (Cel* cel : m_cels) {
      Image* image = cel->image();
      if (!image)
        continue;

      if (cel->layer()->isReference()) {
        gfx::RectF bounds = cel->boundsF();
        rotate_rect(bounds);
        if (cel->boundsF() != bounds)
          tx(new cmd::SetCelBoundsF(cel, bounds));
      }
      else {
        gfx::Rect bounds = cel->bounds();
        rotate_rect(bounds);
        if (bounds.origin() != cel->bounds().origin())
          api.setCelPosition(sprite(), cel, bounds.x, bounds.y);
      }
    }

    // 2) Rotate images
    int i = 0;
    for (Cel* cel : m_cels) {
      Image* image = cel->image();
      if (image) {
        ImageRef new_image(Image::create(image->pixelFormat(),
                                         m_angle == 180 ? image->width() : image->height(),
                                         m_angle == 180 ? image->height() : image->width()));
        new_image->setMaskColor(image->maskColor());

        doc::rotate_image(image, new_image.get(), m_angle);
        api.replaceImage(sprite(), cel->imageRef(), new_image);
      }

      jobProgress(m_cels.empty() ? 1.0f : (float)i / m_cels.size());
      ++i;

      // cancel all the operation?
      if (isCanceled())
        return; // Tx destructor will undo all operations
    }

    // 3) Rotate Smart Object transforms non-destructively
    for (doc::LayerSmartObject* sm : m_smartLayers) {
      if (!sm)
        continue;

      const double newAngle = wrap_angle_deg(sm->angle() + m_angle);
      gfx::PointF newPos = sm->position();

      if (m_rotateSprite) {
        double srcW = static_cast<double>(sm->sourceSize().w);
        double srcH = static_cast<double>(sm->sourceSize().h);
        if ((srcW <= 0.0 || srcH <= 0.0) && !sm->cels().empty()) {
          if (doc::Cel* c0 = sm->cels().front()) {
            if (c0->image()) {
              srcW = static_cast<double>(c0->image()->width());
              srcH = static_cast<double>(c0->image()->height());
            }
          }
        }
        const double ox = sm->pivot().x * srcW;
        const double oy = sm->pivot().y * srcH;
        const double px = sm->position().x + ox;
        const double py = sm->position().y + oy;
        double npx = px;
        double npy = py;
        switch (m_angle) {
          case 180:
            npx = sprite()->width() - px;
            npy = sprite()->height() - py;
            break;
          case 90:
            npx = sprite()->height() - py;
            npy = px;
            break;
          case -90:
            npx = py;
            npy = sprite()->width() - px;
            break;
        }
        newPos = gfx::PointF(npx - ox, npy - oy);
      }

      tx(new cmd::SetSmartObjectTransform(sm,
                                          newPos,
                                          sm->scale(),
                                          newAngle,
                                          sm->flipH(),
                                          sm->flipV(),
                                          sm->pivot()));
    }

    // rotate mask
    if (document()->isMaskVisible()) {
      Mask* origMask = document()->mask();
      std::unique_ptr<Mask> new_mask(new Mask());
      const gfx::Rect& origBounds = origMask->bounds();
      int x = 0, y = 0;

      switch (m_angle) {
        case 180:
          x = sprite()->width() - origBounds.x - origBounds.w;
          y = sprite()->height() - origBounds.y - origBounds.h;
          break;
        case 90:
          x = sprite()->height() - origBounds.y - origBounds.h;
          y = origBounds.x;
          break;
        case -90:
          x = origBounds.y;
          y = sprite()->width() - origBounds.x - origBounds.w;
          break;
      }

      // create the new rotated mask
      new_mask->replace(gfx::Rect(x,
                                  y,
                                  m_angle == 180 ? origBounds.w : origBounds.h,
                                  m_angle == 180 ? origBounds.h : origBounds.w));
      doc::rotate_image(origMask->bitmap(), new_mask->bitmap(), m_angle);

      // Copy new mask
      api.copyToCurrentMask(new_mask.get());
    }

    // change the sprite's size
    if (m_rotateSprite && m_angle != 180)
      api.setSpriteSize(sprite(), sprite()->height(), sprite()->width());
  }
};

RotateCommand::RotateCommand() : Command(CommandId::Rotate())
{
  m_ui = true;
  m_flipMask = false;
  m_angle = 0;
}

void RotateCommand::onLoadParams(const Params& params)
{
  if (params.has_param("ui"))
    m_ui = params.get_as<bool>("ui");
  else
    m_ui = true;

  std::string target = params.get("target");
  m_flipMask = (target == "mask");

  if (params.has_param("angle")) {
    m_angle = strtol(params.get("angle").c_str(), NULL, 10);
  }
}

bool RotateCommand::onEnabled(Context* context)
{
  if (!context->checkFlags(ContextFlags::ActiveDocumentIsWritable |
                             ContextFlags::HasActiveSprite))
    return false;

  // Transforming a visible marquee selection needs an active editor/UI
  if (m_flipMask && !context->isUIAvailable()) {
    const Site site = context->activeSite();
    if (site.document() && site.document()->isMaskVisible())
      return false;
  }

  return true;
}

void RotateCommand::onExecute(Context* context)
{
  {
    Site site = context->activeSite();
    Doc* doc = site.document();
    CelList cels;
    std::vector<doc::LayerSmartObject*> smartLayers;
    bool rotateSprite = false;

    Timeline* timeline = App::instance()->timeline();
    LockTimelineRange lockRange(timeline);

    // Rotate the mask, current cel, or Smart Object layer
    if (m_flipMask) {
      // If we want to rotate the visible mask, we can go to
      // MovingPixelsState (even when the range is enabled, because
      // now PixelsMovement support ranges).
      if (doc->isMaskVisible() && context->isUIAvailable()) {
        // Select marquee tool
        if (tools::Tool* tool = App::instance()->toolBox()->getToolById(
              tools::WellKnownTools::RectangularMarquee)) {
          ToolBar::instance()->selectTool(tool);
          if (auto editor = Editor::activeEditor())
            editor->startSelectionTransformation(gfx::Point(0, 0), m_angle);
          return;
        }
      }

      cels = site.selectedUniqueCelsToEditPixels();

      auto addSmartIfEditable = [&](doc::Layer* lay) {
        if (lay && lay->isSmartObject() && lay->isEditable() &&
            std::find(smartLayers.begin(), smartLayers.end(), lay) == smartLayers.end()) {
          smartLayers.push_back(static_cast<doc::LayerSmartObject*>(lay));
        }
      };
      if (site.range().enabled()) {
        for (doc::Layer* lay : site.range().selectedLayers())
          addSmartIfEditable(lay);
      }
      else {
        addSmartIfEditable(const_cast<doc::Layer*>(site.layer()));
      }

      if (cels.empty() && smartLayers.empty()) {
        if (context->isUIAvailable())
          StatusBar::instance()->showTip(1000, Strings::statusbar_tips_all_layers_are_locked());
        return;
      }
    }
    // Rotate the whole sprite (even locked layers)
    else if (site.sprite()) {
      for (Cel* c : site.sprite()->uniqueCels()) {
        if (c && c->layer() && !c->layer()->isSmartObject())
          cels.push_back(c);
      }
      collect_smart_objects_recursive(site.sprite()->root(), smartLayers);
      rotateSprite = true;
    }

    {
      RotateJob job(context, doc, friendlyName(), m_angle, cels, smartLayers, rotateSprite, m_ui);
      job.startJob();
      job.waitJob();
    }
    update_screen_for_document(doc);
  }
}

std::string RotateCommand::onGetFriendlyName() const
{
  std::string content;
  if (m_flipMask)
    content = Strings::commands_Rotate_Selection();
  else
    content = Strings::commands_Rotate_Sprite();
  return Strings::commands_Rotate(content, base::convert_to<std::string>(m_angle));
}

Command* CommandFactory::createRotateCommand()
{
  return new RotateCommand;
}

} // namespace app
