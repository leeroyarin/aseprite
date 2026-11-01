// Aseprite
// Copyright (c) 2026 Igara Studio S.A.
//
// This program is distributed under the terms of
// the End-User License Agreement for Aseprite.

#ifdef HAVE_CONFIG_H
  #include "config.h"
#endif

#include "app/app.h"
#include "app/cmd/add_layer.h"
#include "app/commands/command.h"
#include "app/commands/commands.h"
#include "app/commands/new_params.h"
#include "app/commands/params.h"
#include "app/console.h"
#include "app/context.h"
#include "app/context_access.h"
#include "app/doc.h"
#include "app/doc_api.h"
#include "app/file/file.h"
#include "app/file_selector.h"
#include "app/i18n/strings.h"
#include "app/modules/gui.h"
#include "app/tx.h"
#include "app/smart_object_manager.h"
#include "app/ui/editor/editor.h"
#include "app/ui/main_window.h"
#include "app/ui/status_bar.h"
#include "base/fs.h"
#include "base/string.h"
#include "doc/cel.h"
#include "doc/image.h"
#include "doc/layer.h"
#include "doc/layer_smart_object.h"
#include "doc/primitives.h"
#include "doc/sprite.h"
#include "render/dithering.h"
#include "render/quantization.h"
#include "render/render.h"

#include <chrono>
#include <filesystem>
#include <memory>
#include <string>

namespace app {

struct PlaceSmartObjectParams : public NewParams {
  Param<std::string> filename{ this, std::string(), "filename" };
  Param<std::string> name{ this, std::string(), "name" };
  Param<int> x{ this, 0, "x" };
  Param<int> y{ this, 0, "y" };
  Param<bool> ui{ this, true, "ui" };
};

class PlaceSmartObjectCommand : public CommandWithNewParams<PlaceSmartObjectParams> {
public:
  PlaceSmartObjectCommand();

protected:
  bool onEnabled(Context* context) override;
  void onExecute(Context* context) override;
};

PlaceSmartObjectCommand::PlaceSmartObjectCommand()
  : CommandWithNewParams<PlaceSmartObjectParams>(CommandId::PlaceSmartObject())
{
}

bool PlaceSmartObjectCommand::onEnabled(Context* context)
{
  return context->checkFlags(ContextFlags::ActiveDocumentIsWritable |
                             ContextFlags::HasActiveSprite);
}

void PlaceSmartObjectCommand::onExecute(Context* context)
{
  std::string filename;
  if (params().filename.isSet() && !params().filename().empty()) {
    filename = params().filename();
  }
  else if (params().ui() && context->isUIAvailable()) {
    std::string initialDir;
    Doc* activeDoc = context->activeDocument();
    if (activeDoc && !activeDoc->filename().empty()) {
      initialDir = base::get_file_path(activeDoc->filename());
      if (!initialDir.empty() && !base::is_path_separator(initialDir.back()))
        initialDir.push_back(base::path_separator);
    }

    base::paths files;
    if (!app::show_file_selector(
          Strings::commands_PlaceSmartObject(),
          initialDir,
          { "ase", "aseprite" },
          FileSelectorType::Open,
          files)) {
      return;
    }
    if (files.empty())
      return;
    filename = files.front();
  }
  else {
    Console(context).printf("PlaceSmartObject: No filename specified\n");
    return;
  }

  std::string fullpath = base::get_canonical_path(filename);
  if (!base::is_file(fullpath)) {
    fullpath = filename;
  }
  if (!base::is_file(fullpath)) {
    Console(context).printf("PlaceSmartObject: File not found: %s\n", filename.c_str());
    return;
  }

  // Prevent placing host document into itself
  Doc* activeDoc = context->activeDocument();
  if (activeDoc && !activeDoc->filename().empty()) {
    std::string activeFullpath = base::get_canonical_path(activeDoc->filename());
    if (activeFullpath == fullpath) {
      Console(context).printf("PlaceSmartObject: Cannot place document into itself\n");
      return;
    }
  }

  // Check if already in detached docs cache to share instance across multiple layers
  uint64_t diskTime = doc::LayerSmartObject::fileModificationEpoch(fullpath);
  doc::Document* cachedDoc = LayerSmartObject::getDetachedDoc(fullpath);
  uint64_t cachedTime = doc::LayerSmartObject::getDetachedDocTimestamp(fullpath);

  // If disk file is newer than cached document, invalidate cache hit to reload fresh
  if (cachedDoc && diskTime > 0 && cachedTime > 0 && diskTime > cachedTime) {
    cachedDoc = nullptr;
  }

  std::unique_ptr<Doc> loadedDoc;
  doc::Document* srcDoc = cachedDoc;

  if (!srcDoc) {
    // Detached offscreen load: passing nullptr avoids UI tabs / window attachment
    loadedDoc.reset(load_document(nullptr, fullpath));
    if (!loadedDoc || !loadedDoc->sprite()) {
      Console(context).printf("PlaceSmartObject: Failed to load file: %s\n", fullpath.c_str());
      return;
    }
    srcDoc = loadedDoc.get();
  }

  Sprite* srcSpr = srcDoc->sprite();
  std::string layerName;
  if (params().name.isSet() && !params().name().empty()) {
    layerName = params().name();
  }
  else {
    layerName = base::get_file_title(fullpath);
  }

  ContextWriter writer(context);
  Doc* document = writer.document();
  Sprite* sprite = writer.sprite();
  if (!document || !sprite)
    return;

  int initialX = params().x.isSet() ? params().x() : (sprite->width() / 2 - srcSpr->width() / 2);
  int initialY = params().y.isSet() ? params().y() : (sprite->height() / 2 - srcSpr->height() / 2);

  Tx tx(writer, Strings::commands_PlaceSmartObject());

  Layer* activeLayer = writer.layer();
  Layer* parent = (activeLayer ? activeLayer->parent() : sprite->root());
  if (!parent)
    parent = sprite->root();

  auto smartLayer = new LayerSmartObject(sprite, fullpath);
  smartLayer->setName(layerName);
  smartLayer->setPosition(gfx::PointF(initialX, initialY));
  smartLayer->setSourceSize(srcSpr->size());
  if (loadedDoc) {
    smartLayer->setLinkedDoc(loadedDoc.release());
  }
  else {
    smartLayer->setLinkedDoc(srcDoc);
  }

  smartLayer->setLastModifiedTime(diskTime);
  smartLayer->syncPropertiesToUserData();

  tx(new cmd::AddLayer(parent, smartLayer, activeLayer));

  smartLayer->syncFrames(sprite->totalFrames());

  tx.commit();

  SmartObjectManager::instance()->trackDocument(document);

  context->setActiveLayer(smartLayer);
  if (context->isUIAvailable()) {
    if (auto editor = Editor::activeEditor())
      editor->setLayer(smartLayer);
    update_screen_for_document(document);
  }
}

struct RefreshSmartObjectParams : public NewParams {
  Param<bool> force{ this, false, "force" };
  Param<bool> all{ this, false, "all" };
  Param<std::string> filename{ this, std::string(), "filename" };
};

class RefreshSmartObjectCommand : public CommandWithNewParams<RefreshSmartObjectParams> {
public:
  RefreshSmartObjectCommand();

protected:
  bool onEnabled(Context* context) override;
  void onExecute(Context* context) override;
};

RefreshSmartObjectCommand::RefreshSmartObjectCommand()
  : CommandWithNewParams<RefreshSmartObjectParams>("RefreshSmartObject")
{
}

bool RefreshSmartObjectCommand::onEnabled(Context* context)
{
  Doc* doc = context->activeDocument();
  if (!doc) {
    for (Doc* d : context->documents()) {
      if (d && d->sprite()) {
        for (Layer* lay : d->sprite()->allLayers()) {
          if (lay->isSmartObject())
            return true;
        }
      }
    }
    return false;
  }
  if (!doc->sprite())
    return false;
  for (Layer* lay : doc->sprite()->allLayers()) {
    if (lay->isSmartObject())
      return true;
  }
  return false;
}

void RefreshSmartObjectCommand::onExecute(Context* context)
{
  bool force = params().force();
  bool all = params().all();
  std::string targetFile = params().filename();

  std::vector<Doc*> docs;
  Doc* activeDoc = context->activeDocument();
  if (activeDoc)
    docs.push_back(activeDoc);
  if (all || !activeDoc) {
    for (Doc* d : context->documents()) {
      if (d && d != activeDoc)
        docs.push_back(d);
    }
  }

  for (Doc* doc : docs) {
    if (!doc || !doc->sprite())
      continue;

    Sprite* sprite = doc->sprite();
    std::vector<doc::LayerSmartObject*> targets;
    Layer* active = (doc == activeDoc ? context->activeSite().layer() : nullptr);

    if (all || !active || !active->isSmartObject()) {
      for (Layer* lay : sprite->allLayers()) {
        if (lay->isSmartObject()) {
          auto sm = static_cast<doc::LayerSmartObject*>(lay);
          if (targetFile.empty() || sm->filepath() == targetFile)
            targets.push_back(sm);
        }
      }
    }
    else {
      auto sm = static_cast<doc::LayerSmartObject*>(active);
      if (targetFile.empty() || sm->filepath() == targetFile)
        targets.push_back(sm);
    }

    int refreshed = 0;
    for (doc::LayerSmartObject* sm : targets) {
      if (sm->reloadSource(force))
        refreshed++;
    }

    if (refreshed > 0) {
      doc->notifyGeneralUpdate();
      if (context->isUIAvailable())
        update_screen_for_document(doc);
    }
  }
}

Command* CommandFactory::createPlaceSmartObjectCommand()
{
  return new PlaceSmartObjectCommand;
}

Command* CommandFactory::createRefreshSmartObjectCommand()
{
  return new RefreshSmartObjectCommand;
}

} // namespace app
