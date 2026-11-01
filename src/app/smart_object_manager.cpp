// Aseprite
// Copyright (c) 2026 Igara Studio S.A.
//
// This file is released under the terms of the MIT license.
// Read LICENSE.txt for more information.

#if defined(HAVE_CONFIG_H) && !defined(ASEPRITE_CONFIG_H_INCLUDED)
  #include "config.h"
#endif

#include "app/smart_object_manager.h"

#include "app/app.h"
#include "app/context.h"
#include "app/doc.h"
#include "app/doc_access.h"
#include "app/modules/gui.h"
#include "app/site.h"
#include "base/fs.h"
#include "doc/layer_smart_object.h"
#include "doc/sprite.h"

#include <algorithm>

namespace app {

SmartObjectManager* SmartObjectManager::s_instance = nullptr;

SmartObjectManager* SmartObjectManager::instance()
{
  if (!s_instance)
    s_instance = new SmartObjectManager();
  return s_instance;
}

void SmartObjectManager::destroyInstance()
{
  delete s_instance;
  s_instance = nullptr;
}

SmartObjectManager::SmartObjectManager()
  : m_timer(1000)
  , m_isChecking(false)
{
  m_timer.Tick.connect(&SmartObjectManager::onTimerTick, this);

  if (App* app = App::instance()) {
    if (Context* ctx = app->context()) {
      ctx->add_observer(this);
      ctx->documents().add_observer(this);
      for (Doc* doc : ctx->documents()) {
        trackDocument(doc);
      }
    }
  }
}

SmartObjectManager::~SmartObjectManager()
{
  m_timer.stop();
  if (App* app = App::instance()) {
    if (Context* ctx = app->context()) {
      ctx->remove_observer(this);
      ctx->documents().remove_observer(this);
    }
  }
}

void SmartObjectManager::trackDocument(Doc* doc)
{
  if (!doc)
    return;
  auto it = std::find(m_trackedDocs.begin(), m_trackedDocs.end(), doc);
  if (it == m_trackedDocs.end()) {
    m_trackedDocs.push_back(doc);
  }
  updateTimerState();
}

void SmartObjectManager::untrackDocument(Doc* doc)
{
  auto it = std::find(m_trackedDocs.begin(), m_trackedDocs.end(), doc);
  if (it != m_trackedDocs.end()) {
    m_trackedDocs.erase(it);
    updateTimerState();
  }
}

void SmartObjectManager::onAddDocument(Doc* doc)
{
  trackDocument(doc);
}

void SmartObjectManager::onRemoveDocument(Doc* doc)
{
  untrackDocument(doc);
}

void SmartObjectManager::onActiveSiteChange(const Site& site)
{
  if (site.document()) {
    checkDocUpdates(static_cast<Doc*>(site.document()));
  }
}

std::vector<doc::LayerSmartObject*> SmartObjectManager::getSmartLayers(Doc* doc) const
{
  std::vector<doc::LayerSmartObject*> smartLayers;
  if (!doc || !doc->sprite())
    return smartLayers;

  for (doc::Layer* layer : doc->sprite()->allLayers()) {
    if (layer && layer->isSmartObject()) {
      smartLayers.push_back(static_cast<doc::LayerSmartObject*>(layer));
    }
  }
  return smartLayers;
}

bool SmartObjectManager::hasSmartObjects(Doc* doc) const
{
  if (!doc || !doc->sprite())
    return false;
  for (doc::Layer* layer : doc->sprite()->allLayers()) {
    if (layer && layer->isSmartObject())
      return true;
  }
  return false;
}

void SmartObjectManager::enableTimer(bool enable)
{
  if (enable && !m_timer.isRunning())
    m_timer.start();
  else if (!enable && m_timer.isRunning())
    m_timer.stop();
}

void SmartObjectManager::updateTimerState()
{
  bool hasAnySmartObjects = false;
  for (Doc* doc : m_trackedDocs) {
    if (hasSmartObjects(doc)) {
      hasAnySmartObjects = true;
      break;
    }
  }

  if (hasAnySmartObjects && !m_timer.isRunning()) {
    m_timer.start();
  }
  else if (!hasAnySmartObjects && m_timer.isRunning()) {
    m_timer.stop();
  }
}

void SmartObjectManager::onTimerTick()
{
  checkAllUpdates();
}

bool SmartObjectManager::checkAllUpdates()
{
  if (m_isChecking)
    return false;
  m_isChecking = true;

  bool anyUpdated = false;
  for (Doc* doc : m_trackedDocs) {
    if (checkDocUpdates(doc))
      anyUpdated = true;
  }

  m_isChecking = false;
  return anyUpdated;
}

bool SmartObjectManager::checkDocUpdates(Doc* hostDoc)
{
  if (!hostDoc || !hostDoc->sprite())
    return false;

  auto smartLayers = getSmartLayers(hostDoc);
  if (smartLayers.empty())
    return false;

  bool docUpdated = false;

  for (doc::LayerSmartObject* smartLayer : smartLayers) {
    const std::string& filepath = smartLayer->filepath();
    if (filepath.empty())
      continue;

    // Self-reference protection
    if (!hostDoc->filename().empty() &&
        base::get_canonical_path(hostDoc->filename()) == base::get_canonical_path(filepath)) {
      continue;
    }

    if (smartLayer->isDiskFileNewer()) {
      if (smartLayer->reloadSource(false)) {
        docUpdated = true;
      }
    }
  }

  if (docUpdated) {
    if (App::instance() && App::instance()->isGui()) {
      update_screen_for_document(hostDoc);
    }
    else {
      hostDoc->notifyGeneralUpdate();
    }
  }

  return docUpdated;
}

bool SmartObjectManager::reloadLayer(Doc* hostDoc, doc::LayerSmartObject* layer, bool force)
{
  if (!hostDoc || !layer)
    return false;

  bool ok = layer->reloadSource(force);
  if (ok) {
    if (App::instance() && App::instance()->isGui()) {
      update_screen_for_document(hostDoc);
    }
    else {
      hostDoc->notifyGeneralUpdate();
    }
  }
  return ok;
}

} // namespace app
