// Aseprite
// Copyright (c) 2026 Igara Studio S.A.
//
// This file is released under the terms of the MIT license.
// Read LICENSE.txt for more information.

#ifndef APP_SMART_OBJECT_MANAGER_H_INCLUDED
#define APP_SMART_OBJECT_MANAGER_H_INCLUDED
#pragma once

#include "app/context_observer.h"
#include "app/docs_observer.h"
#include "base/time.h"
#include "ui/timer.h"

#include <memory>
#include <string>
#include <vector>

namespace doc {
class LayerSmartObject;
}

namespace app {

class Doc;

class SmartObjectManager : public ContextObserver
                         , public DocsObserver {
public:
  static SmartObjectManager* instance();
  static void destroyInstance();

  SmartObjectManager();
  ~SmartObjectManager() override;

  // Manual & automatic tracking
  void trackDocument(Doc* doc);
  void untrackDocument(Doc* doc);

  // Core update check methods
  bool checkAllUpdates();
  bool checkDocUpdates(Doc* hostDoc);
  bool reloadLayer(Doc* hostDoc, doc::LayerSmartObject* layer, bool force = false);

  bool hasSmartObjects(Doc* doc) const;
  std::vector<doc::LayerSmartObject*> getSmartLayers(Doc* doc) const;

  // ContextObserver
  void onActiveSiteChange(const Site& site) override;

  // DocsObserver
  void onAddDocument(Doc* doc) override;
  void onRemoveDocument(Doc* doc) override;

  // Timer control
  void enableTimer(bool enable);
  void updateTimerState();

private:
  void onTimerTick();

  static SmartObjectManager* s_instance;

  std::vector<Doc*> m_trackedDocs;
  ui::Timer m_timer;
  bool m_isChecking;
};

} // namespace app

#endif
