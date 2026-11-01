// Aseprite
// Copyright (C) 2019-2023  Igara Studio S.A.
// Copyright (C) 2001-2018  David Capello
//
// This program is distributed under the terms of
// the End-User License Agreement for Aseprite.

#ifdef HAVE_CONFIG_H
  #include "config.h"
#endif

#include "app/cmd/flatten_layers.h"
#include "app/commands/command.h"
#include "app/context_access.h"
#include "app/doc_range.h"
#include "app/i18n/strings.h"
#include "app/modules/gui.h"
#include "app/pref/preferences.h"
#include "app/tx.h"
#include "app/ui/color_bar.h"
#include "doc/layer.h"
#include "doc/sprite.h"

namespace app {

class FlattenLayersCommand : public Command {
public:
  FlattenLayersCommand();

protected:
  void onLoadParams(const Params& params) override;
  bool onEnabled(Context* context) override;
  void onExecute(Context* context) override;
  std::string onGetFriendlyName() const override;

  bool m_visibleOnly;
};

FlattenLayersCommand::FlattenLayersCommand() : Command(CommandId::FlattenLayers())
{
  m_visibleOnly = false;
}

void FlattenLayersCommand::onLoadParams(const Params& params)
{
  m_visibleOnly = params.get_as<bool>("visibleOnly");
}

bool FlattenLayersCommand::onEnabled(Context* context)
{
  if (!context->checkFlags(ContextFlags::ActiveDocumentIsWritable))
    return false;

  const ContextReader reader(context);
  const Sprite* sprite = reader.sprite();
  if (!sprite)
    return false;

  const Site& site = context->activeSite();
  const DocRange& range = site.range();

  if (m_visibleOnly) {
    for (const Layer* layer : sprite->allVisibleLayers()) {
      if (layer->isSmartObject())
        return false;
    }
  }
  else if (range.enabled() && range.selectedLayers().size() > 1) {
    for (const Layer* layer : range.selectedLayers()) {
      if (layer->isSmartObject())
        return false;
      LayerList childLayers;
      layer->allLayers(childLayers);
      for (const Layer* child : childLayers) {
        if (child->isSmartObject())
          return false;
      }
    }
  }
  else {
    for (const Layer* layer : sprite->allLayers()) {
      if (layer->isSmartObject())
        return false;
    }
  }

  return true;
}

void FlattenLayersCommand::onExecute(Context* context)
{
  ContextWriter writer(context);
  const Site& site = writer.site();
  Sprite* sprite = site.sprite();
  {
    Tx tx(writer, "Flatten Layers");

    view::Range range;

    if (m_visibleOnly) {
      for (auto layer : sprite->root()->layers())
        if (layer->isVisible())
          range.selectLayer(layer);
    }
    else {
      range = site.range();

      // If the range is not selected or we have only one image layer
      // selected, we'll flatten all layers.
      if (!range.enabled() ||
          (range.selectedLayers().size() == 1 && (*range.selectedLayers().begin())->isImage())) {
        for (auto layer : sprite->root()->layers())
          range.selectLayer(layer);
      }
    }

    for (Layer* layer : range.selectedLayers()) {
      if (layer->isSmartObject())
        return;
      LayerList childLayers;
      layer->allLayers(childLayers);
      for (Layer* child : childLayers) {
        if (child->isSmartObject())
          return;
      }
    }

    const bool newBlend = Preferences::instance().experimental.newBlend();
    cmd::FlattenLayers::Options options;
    options.newBlendMethod = newBlend;
    tx(new cmd::FlattenLayers(sprite, range.selectedLayers(), options));
    tx.commit();
  }

  update_screen_for_document(writer.document());
}

std::string FlattenLayersCommand::onGetFriendlyName() const
{
  if (m_visibleOnly)
    return Strings::commands_FlattenLayers_Visible();
  else
    return Strings::commands_FlattenLayers();
}

Command* CommandFactory::createFlattenLayersCommand()
{
  return new FlattenLayersCommand;
}

} // namespace app
