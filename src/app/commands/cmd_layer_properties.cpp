// Aseprite
// Copyright (C) 2020-2025  Igara Studio S.A.
// Copyright (C) 2001-2018  David Capello
//
// This program is distributed under the terms of
// the End-User License Agreement for Aseprite.

#ifdef HAVE_CONFIG_H
  #include "config.h"
#endif

#include "app/app.h"
#include "app/cmd/set_layer_blend_mode.h"
#include "app/cmd/set_layer_name.h"
#include "app/cmd/set_layer_opacity.h"
#include "app/cmd/set_layer_tileset.h"
#include "app/cmd/set_smart_object_transform.h"
#include "app/cmd/set_tileset_base_index.h"
#include "app/cmd/set_tileset_match_flags.h"
#include "app/cmd/set_tileset_name.h"
#include "app/cmd/set_user_data.h"
#include "app/commands/command.h"
#include "app/console.h"
#include "app/context_access.h"
#include "app/doc.h"
#include "app/doc_event.h"
#include "app/i18n/strings.h"
#include "app/modules/gui.h"
#include "app/tx.h"
#include "app/ui/main_window.h"
#include "app/ui/separator_in_view.h"
#include "app/ui/tileset_selector.h"
#include "app/ui/timeline/timeline.h"
#include "app/ui/user_data_view.h"
#include "app/ui_context.h"
#include "base/convert_to.h"
#include "base/scoped_value.h"
#include "doc/document.h"
#include "doc/layer.h"
#include "doc/layer_smart_object.h"
#include "doc/layer_tilemap.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include "doc/sprite.h"
#include "doc/tag.h"
#include "doc/tags.h"
#include "ui/int_entry.h"
#include <sstream>
#include "doc/tileset.h"
#include "doc/tilesets.h"
#include "doc/user_data.h"
#include "ui/ui.h"

#include "layer_properties.xml.h"
#include "tileset_selector_window.xml.h"

namespace app {

using namespace ui;

class LayerPropertiesCommand : public Command {
public:
  LayerPropertiesCommand();

protected:
  bool onEnabled(Context* context) override;
  void onExecute(Context* context) override;
};

class LayerPropertiesWindow;
static LayerPropertiesWindow* g_window = nullptr;

class LayerPropertiesWindow : public app::gen::LayerProperties,
                              public ContextObserver,
                              public DocObserver {
public:
  class BlendModeItem : public ListItem {
  public:
    BlendModeItem(const std::string& name, const doc::BlendMode mode) : ListItem(name), m_mode(mode)
    {
    }
    doc::BlendMode mode() const { return m_mode; }

  private:
    doc::BlendMode m_mode;
  };

  LayerPropertiesWindow()
    : m_timer(250, this)
    , m_userDataView(Preferences::instance().layers.userDataVisibility)
  {
    name()->setMinSize(gfx::Size(128, 0));
    name()->setExpansive(true);

    mode()->addItem(new BlendModeItem(Strings::layer_properties_normal(), doc::BlendMode::NORMAL));
    mode()->addItem(new SeparatorInView);
    mode()->addItem(new BlendModeItem(Strings::layer_properties_darken(), doc::BlendMode::DARKEN));
    mode()->addItem(
      new BlendModeItem(Strings::layer_properties_multiply(), doc::BlendMode::MULTIPLY));
    mode()->addItem(
      new BlendModeItem(Strings::layer_properties_color_burn(), doc::BlendMode::COLOR_BURN));
    mode()->addItem(new SeparatorInView);
    mode()->addItem(
      new BlendModeItem(Strings::layer_properties_lighten(), doc::BlendMode::LIGHTEN));
    mode()->addItem(new BlendModeItem(Strings::layer_properties_screen(), doc::BlendMode::SCREEN));
    mode()->addItem(
      new BlendModeItem(Strings::layer_properties_color_dodge(), doc::BlendMode::COLOR_DODGE));
    mode()->addItem(
      new BlendModeItem(Strings::layer_properties_addition(), doc::BlendMode::ADDITION));
    mode()->addItem(new SeparatorInView);
    mode()->addItem(
      new BlendModeItem(Strings::layer_properties_overlay(), doc::BlendMode::OVERLAY));
    mode()->addItem(
      new BlendModeItem(Strings::layer_properties_soft_light(), doc::BlendMode::SOFT_LIGHT));
    mode()->addItem(
      new BlendModeItem(Strings::layer_properties_hard_light(), doc::BlendMode::HARD_LIGHT));
    mode()->addItem(new SeparatorInView);
    mode()->addItem(
      new BlendModeItem(Strings::layer_properties_difference(), doc::BlendMode::DIFFERENCE));
    mode()->addItem(
      new BlendModeItem(Strings::layer_properties_exclusion(), doc::BlendMode::EXCLUSION));
    mode()->addItem(
      new BlendModeItem(Strings::layer_properties_subtract(), doc::BlendMode::SUBTRACT));
    mode()->addItem(new BlendModeItem(Strings::layer_properties_divide(), doc::BlendMode::DIVIDE));
    mode()->addItem(new SeparatorInView);
    mode()->addItem(new BlendModeItem(Strings::layer_properties_hue(), doc::BlendMode::HSL_HUE));
    mode()->addItem(
      new BlendModeItem(Strings::layer_properties_saturation(), doc::BlendMode::HSL_SATURATION));
    mode()->addItem(
      new BlendModeItem(Strings::layer_properties_color(), doc::BlendMode::HSL_COLOR));
    mode()->addItem(
      new BlendModeItem(Strings::layer_properties_luminosity(), doc::BlendMode::HSL_LUMINOSITY));

    name()->Change.connect([this] { onStartTimer(); });
    mode()->Change.connect([this] { onStartTimer(); });
    opacity()->Change.connect([this] { onStartTimer(); });
    smFrame()->Change.connect([this] {
      if (!m_layer || !m_layer->isSmartObject())
        return;
      const int frame = smFrame()->textInt();
      if (frame < 1 || frame > m_layer->sprite()->totalFrames())
        return;
      onCommitChange();
      m_transformFrame = doc::frame_t(frame - 1);
      smFrame()->setText(std::to_string(frame));
      const auto transform =
        static_cast<const doc::LayerSmartObject*>(m_layer)->transformForFrame(m_transformFrame);
      smAngle()->setText(formatNumber(transform.angle));
      smScaleX()->setText(formatNumber(transform.scale.x));
      smScaleY()->setText(formatNumber(transform.scale.y));
      smFlipH()->setSelected(transform.flipH);
      smFlipV()->setSelected(transform.flipV);
    });
    smAngle()->Change.connect([this] { onStartTimer(); });
    smScaleX()->Change.connect([this] { onStartTimer(); });
    smScaleY()->Change.connect([this] { onStartTimer(); });
    smFlipH()->Click.connect([this] { onStartTimer(); });
    smFlipV()->Click.connect([this] { onStartTimer(); });
    smStatesApply()->Click.connect([this] { applyAnimationStates(smStates()->text()); });
    smStatesNew()->Click.connect([this] { onNewAnimationState(); });
    smReset()->Click.connect([this] { onResetSmartTransform(); });
    m_timer.Tick.connect([this] { onCommitChange(); });
    userData()->Click.connect([this] { onToggleUserData(); });
    tileset()->Click.connect([this] { onTileset(); });
    tileset()->setVisible(false);

    m_userDataView.UserDataChange.connect([this] { onStartTimer(); });

    remapWindow();
    centerWindow();

    gfx::Rect originalBounds = bounds();

    load_window_pos(this, "LayerProperties");

    // Queue a remap for after the user data view is configured
    // if the window size has been reset and user data is visible
    if (originalBounds == bounds() && Preferences::instance().layers.userDataVisibility())
      m_remapAfterConfigure = true;

    UIContext::instance()->add_observer(this);
  }

  ~LayerPropertiesWindow() { UIContext::instance()->remove_observer(this); }

  void setLayer(Doc* doc, Layer* layer)
  {
    if (m_layer) {
      m_document->remove_observer(this);
      m_layer = nullptr;
    }

    m_timer.stop();
    m_document = doc;
    m_layer = layer;
    m_range = UIContext::instance()->range();

    if (m_document)
      m_document->add_observer(this);

    if (countLayers() > 0) {
      m_userDataView.configureAndSet(m_layer->userData(), propertiesGrid());
      if (m_remapAfterConfigure) {
        remapWindow();
        centerWindow();
        m_remapAfterConfigure = false;
      }
    }

    updateFromLayer();
  }

private:
  std::string nameValue() const { return name()->text(); }

  doc::BlendMode blendModeValue() const
  {
    BlendModeItem* item = dynamic_cast<BlendModeItem*>(mode()->getSelectedItem());
    if (item)
      return item->mode();
    else
      return doc::BlendMode::NORMAL;
  }

  int opacityValue() const { return opacity()->getValue(); }

  int countLayers() const
  {
    if (!m_document)
      return 0;
    else if (m_layer && !m_range.enabled())
      return 1;
    else if (m_range.enabled())
      return m_range.layers();
    else
      return 0;
  }

  bool onProcessMessage(ui::Message* msg) override
  {
    switch (msg->type()) {
      case kKeyDownMessage:
        if (name()->hasFocus() || opacity()->hasFocus() || mode()->hasFocus() ||
            smAngle()->hasFocus() || smScaleX()->hasFocus() || smScaleY()->hasFocus()) {
          KeyScancode scancode = static_cast<KeyMessage*>(msg)->scancode();
          if (scancode == kKeyEnter || scancode == kKeyEsc) {
            onCommitChange();
            closeWindow(this);
            return true;
          }
        }
        break;

      case kCloseMessage:
        // Save changes before we close the window
        setLayer(nullptr, nullptr);
        save_window_pos(this, "LayerProperties");

        deferDelete();
        g_window = nullptr;
        break;
    }
    return Window::onProcessMessage(msg);
  }

  void onStartTimer()
  {
    if (m_selfUpdate)
      return;

    m_timer.start();
    m_pendingChanges = true;
  }

  void onCommitChange()
  {
    // Nothing to change
    if (!m_pendingChanges)
      return;

    // Nothing to do here, as there is no layer selected.
    if (!m_layer)
      return;

    base::ScopedValue switchSelf(m_selfUpdate, true);

    m_timer.stop();

    const int count = countLayers();

    std::string newName = nameValue();
    int newOpacity = opacityValue();
    const doc::UserData newUserData = m_userDataView.userData();
    doc::BlendMode newBlendMode = blendModeValue();

    bool smartChanged = false;
    gfx::PointF newSmScale(1.0, 1.0);
    double newSmAngle = 0.0;
    bool newSmFlipH = false;
    bool newSmFlipV = false;
    if (count == 1 && m_layer && m_layer->isSmartObject() && m_layer->isEditable()) {
      const auto* sm = static_cast<const doc::LayerSmartObject*>(m_layer);
      const auto transform = sm->transformForFrame(m_transformFrame);
      newSmAngle = parseDoubleOr(smAngle()->text(), transform.angle);
      double sx = parseDoubleOr(smScaleX()->text(), transform.scale.x);
      double sy = parseDoubleOr(smScaleY()->text(), transform.scale.y);
      if (std::abs(sx) < 0.01)
        sx = transform.scale.x;
      if (std::abs(sy) < 0.01)
        sy = transform.scale.y;
      newSmScale = gfx::PointF(sx, sy);
      newSmFlipH = smFlipH()->isSelected();
      newSmFlipV = smFlipV()->isSelected();
      smartChanged = (std::abs(newSmAngle - transform.angle) > 1e-6 ||
                      std::abs(newSmScale.x - transform.scale.x) > 1e-6 ||
                      std::abs(newSmScale.y - transform.scale.y) > 1e-6 ||
                      newSmFlipH != transform.flipH ||
                      newSmFlipV != transform.flipV);
    }

    if ((count > 1) ||
        (count == 1 && m_layer &&
         (newName != m_layer->name() || newUserData != m_layer->userData() ||
          smartChanged ||
          ((m_layer->isImage() ||
            (m_layer->isGroup() && Preferences::instance().experimental.composeGroups())) &&
           (newOpacity != m_layer->opacity() || newBlendMode != m_layer->blendMode()))))) {
      try {
        ContextWriter writer(UIContext::instance());
        Tx tx(writer, "Set Layer Properties");

        view::RealRange range;
        if (m_range.enabled())
          range = m_range;
        else {
          range.startRange(m_layer, -1, view::Range::kLayers);
          range.endRange(m_layer, -1);
        }

        const bool shouldChangeProperties = m_layer->isImage() ||
                                            (m_layer->isGroup() &&
                                             Preferences::instance().experimental.composeGroups());

        const bool nameChanged = (newName != m_layer->name());
        const bool userDataChanged = (newUserData != m_layer->userData());
        const bool opacityChanged = (shouldChangeProperties && newOpacity != m_layer->opacity());
        const bool blendModeChanged = (shouldChangeProperties &&
                                       newBlendMode != m_layer->blendMode());

        for (Layer* layer : range.selectedLayers()) {
          if (nameChanged && newName != layer->name())
            tx(new cmd::SetLayerName(layer, newName));

          if (userDataChanged && newUserData != layer->userData())
            tx(new cmd::SetUserData(layer, newUserData, m_document));

          if (layer->isImage() ||
              (layer->isGroup() && Preferences::instance().experimental.composeGroups())) {
            if (opacityChanged && newOpacity != static_cast<LayerImage*>(layer)->opacity())
              tx(new cmd::SetLayerOpacity(static_cast<LayerImage*>(layer), newOpacity));

            if (blendModeChanged && newBlendMode != static_cast<LayerImage*>(layer)->blendMode())
              tx(new cmd::SetLayerBlendMode(static_cast<LayerImage*>(layer), newBlendMode));
          }
        }

        if (smartChanged && m_layer && m_layer->isSmartObject()) {
          auto* sm = static_cast<doc::LayerSmartObject*>(m_layer);
          auto transform = sm->transformForFrame(m_transformFrame);
          transform.angle = newSmAngle;
          transform.scale = newSmScale;
          transform.flipH = newSmFlipH;
          transform.flipV = newSmFlipV;
          auto data = sm->userData();
          sm->writeFrameTransform(data, m_transformFrame, transform);
          tx(new cmd::SetUserData(sm, data, m_document));
          m_userDataView.configureAndSet(sm->userData(), propertiesGrid());
        }

        // Redraw timeline because the layer's name/user data/color
        // might have changed.
        App::instance()->timeline()->invalidate();

        tx.commit();
      }
      catch (const std::exception& e) {
        Console::showException(e);
      }

      update_screen_for_document(m_document);
    }

    // We indicate that there are no more pending changes in both
    // cases 1) if we were able to commit the transaction or 2) if an
    // exception ocurred (e.g. the sprite was locked). This is because
    // sometimes if a big operation with multiple modifications
    // (e.g. deleting a lot of cels at the same time) is going to
    // happen, we'll receive a lot of onActiveSiteChange() events +
    // onCommitChange() calls.
    //
    // TODO this is similar to CelPropertiesWindow::onCommitChange()
    m_pendingChanges = false;
  }

  // ContextObserver impl
  void onActiveSiteChange(const Site& site) override
  {
    onCommitChange();
    m_transformFrame = site.frame();
    if (isVisible())
      setLayer(const_cast<Doc*>(site.document()), const_cast<Layer*>(site.layer()));
    else if (m_layer)
      setLayer(nullptr, nullptr);
  }

  // DocObserver impl
  void onLayerNameChange(DocEvent& ev) override
  {
    if (m_layer == ev.layer())
      updateFromLayer();
  }

  void onLayerOpacityChange(DocEvent& ev) override
  {
    if (m_layer == ev.layer())
      updateFromLayer();
  }

  void onLayerBlendModeChange(DocEvent& ev) override
  {
    if (m_layer == ev.layer())
      updateFromLayer();
  }

  void onUserDataChange(DocEvent& ev) override
  {
    if (m_layer == ev.withUserData())
      updateFromLayer();
  }

  void onToggleUserData()
  {
    if (m_layer) {
      m_userDataView.toggleVisibility();
      expandWindow(gfx::Size(std::max(bounds().w, sizeHint().w), sizeHint().h));
    }
  }

  void onTileset()
  {
    if (!m_layer || !m_layer->isTilemap())
      return;

    auto tilemap = static_cast<LayerTilemap*>(m_layer);
    expandWindow(gfx::Size(std::max(bounds().w, sizeHint().w), sizeHint().h));

    auto tileset = tilemap->tileset();

    // Information about the tileset to be used for new tilemaps
    TilesetSelector::Info tilesetInfo;
    tilesetInfo.allowNewTileset = false;
    tilesetInfo.newTileset = false;
    tilesetInfo.grid = tileset->grid();
    tilesetInfo.name = tileset->name();
    tilesetInfo.baseIndex = tileset->baseIndex();
    tilesetInfo.matchFlags = tileset->matchFlags();
    tilesetInfo.tsi = tilemap->tilesetIndex();

    try {
      gen::TilesetSelectorWindow window;
      TilesetSelector tilesetSel(tilemap->sprite(), tilesetInfo);
      window.tilesetOptions()->addChild(&tilesetSel);
      window.openWindowInForeground();
      if (window.closer() != window.ok())
        return;

      // Save "advanced" options
      tilesetSel.saveAdvancedPreferences();

      tilesetInfo = tilesetSel.getInfo();

      if (tileset->name() != tilesetInfo.name || tileset->baseIndex() != tilesetInfo.baseIndex ||
          tileset->matchFlags() != tilesetInfo.matchFlags ||
          tilesetInfo.tsi != tilemap->tilesetIndex()) {
        ContextWriter writer(UIContext::instance());
        Tx tx(writer, "Set Tileset Properties");
        // User changed tilemap's tileset
        if (tilesetInfo.tsi != tilemap->tilesetIndex()) {
          tileset = tilemap->sprite()->tilesets()->get(tilesetInfo.tsi);
          tx(new cmd::SetLayerTileset(tilemap, tilesetInfo.tsi));
        }
        if (tileset->name() != tilesetInfo.name)
          tx(new cmd::SetTilesetName(tileset, tilesetInfo.name));
        if (tileset->baseIndex() != tilesetInfo.baseIndex)
          tx(new cmd::SetTilesetBaseIndex(tileset, tilesetInfo.baseIndex));
        if (tileset->matchFlags() != tilesetInfo.matchFlags)
          tx(new cmd::SetTilesetMatchFlags(tileset, tilesetInfo.matchFlags));
        // TODO catch the tileset base index modification from the editor
        App::instance()->mainWindow()->invalidate();
        tx.commit();
      }
    }
    catch (const std::exception& e) {
      Console::showException(e);
    }
  }

  static double parseDoubleOr(const std::string& s, double fallback)
  {
    if (s.empty())
      return fallback;
    char* end = nullptr;
    const double val = std::strtod(s.c_str(), &end);
    if (end == s.c_str() || !std::isfinite(val))
      return fallback;
    while (*end == ' ' || *end == '\t')
      ++end;
    if (*end != '\0')
      return fallback;
    return val;
  }

  static std::string formatNumber(double val)
  {
    char buf[32];
    if (std::abs(val) < 1e-6)
      val = 0.0;
    std::snprintf(buf, sizeof(buf), "%.4g", val);
    return std::string(buf);
  }

  bool applyAnimationStates(const std::string& states)
  {
    if (!m_layer || !m_layer->isSmartObject() || !m_layer->isEditable())
      return false;
    auto* smart = static_cast<doc::LayerSmartObject*>(m_layer);
    if (!smart->validateAnimationStates(states)) {
      smStatesHelp()->setText(Strings::layer_properties_sm_states_error());
      return false;
    }
    try {
      ContextWriter writer(UIContext::instance());
      auto data = smart->userData();
      data.properties("smart_object")["animation_states"] = states;
      Tx tx(writer, "Set Smart Object Animation States");
      tx(new cmd::SetUserData(smart, data, m_document));
      tx.commit();
      m_userDataView.configureAndSet(smart->userData(), propertiesGrid());
      smStates()->setText(smart->animationStates());
      smStatesHelp()->setText(Strings::layer_properties_sm_states_help());
      update_screen_for_document(m_document);
      return true;
    }
    catch (const std::exception& e) {
      Console::showException(e);
      return false;
    }
  }

  void onNewAnimationState()
  {
    if (!m_layer || !m_layer->isSmartObject() || !m_layer->isEditable())
      return;
    auto* smart = static_cast<doc::LayerSmartObject*>(m_layer);
    const auto* source = smart->linkedDoc();
    if (!source || !source->sprite() ||
        source->sprite()->tags().begin() == source->sprite()->tags().end()) {
      smStatesHelp()->setText(Strings::layer_properties_sm_states_no_tags());
      return;
    }

    ui::Window window(ui::Window::WithTitleBar, Strings::layer_properties_sm_states_title());
    ui::VBox rows;
    ui::Label firstLabel(Strings::layer_properties_sm_states_first());
    ui::Label lastLabel(Strings::layer_properties_sm_states_last());
    ui::Label tagLabel(Strings::layer_properties_sm_states_tag());
    ui::Label error("");
    const int frames = smart->sprite()->totalFrames();
    ui::IntEntry first(1, frames);
    ui::IntEntry last(1, frames);
    first.useSlider(false);
    last.useSlider(false);
    first.setId("sm_state_first");
    last.setId("sm_state_last");
    first.setValue(1);
    last.setValue(frames);
    ui::ComboBox tags;
    tags.setId("sm_state_tag");
    std::vector<std::string> names;
    for (const auto* tag : source->sprite()->tags()) {
      names.push_back(tag->name());
      tags.addItem(tag->name());
    }
    tags.setSelectedItemIndex(0);
    ui::CheckBox loop(Strings::layer_properties_sm_states_loop());
    loop.setSelected(true);
    ui::Button save(Strings::layer_properties_sm_states_save());
    for (ui::Widget* widget : std::initializer_list<ui::Widget*>{
           &firstLabel, &first, &lastLabel, &last, &tagLabel, &tags, &loop, &error, &save })
      rows.addChild(widget);
    window.addChild(&rows);
    save.Click.connect([&] {
      const int start = first.getValue();
      const int end = last.getValue();
      const int selected = tags.getSelectedItemIndex();
      if (start > end || selected < 0 || selected >= int(names.size())) {
        error.setText(Strings::layer_properties_sm_states_invalid_range());
        window.layout();
        return;
      }

      std::map<int, std::string> states;
      std::istringstream existing(smart->animationStates());
      for (std::string cue; std::getline(existing, cue, ';');)
        states.emplace(std::stoi(cue), cue);
      states[start] = std::to_string(start) + "-" + std::to_string(end) +
                      (loop.isSelected() ? "L=" : "H=") + names[selected];
      std::string schedule;
      for (const auto& pair : states) {
        if (!schedule.empty())
          schedule += ';';
        schedule += pair.second;
      }
      if (applyAnimationStates(schedule))
        window.closeWindow(&save);
      else {
        error.setText(Strings::layer_properties_sm_states_invalid_range());
        window.layout();
      }
    });
    window.openWindowInForeground();
  }

  void onResetSmartTransform()
  {
    if (!m_layer || !m_layer->isSmartObject() || !m_layer->isEditable())
      return;

    m_timer.stop();
    m_pendingChanges = false;
    auto* sm = static_cast<doc::LayerSmartObject*>(m_layer);
    try {
      base::ScopedValue switchSelf(m_selfUpdate, true);
      ContextWriter writer(UIContext::instance());
      Tx tx(writer, "Reset Smart Object Transform");
      auto data = sm->userData();
      auto transform = sm->transformForFrame(m_transformFrame);
      transform.scale = gfx::PointF(1, 1);
      transform.angle = 0;
      transform.flipH = transform.flipV = false;
      sm->writeFrameTransform(data, m_transformFrame, transform);
      tx(new cmd::SetUserData(sm, data, m_document));
      m_userDataView.configureAndSet(sm->userData(), propertiesGrid());
      tx.commit();
    }
    catch (const std::exception& e) {
      Console::showException(e);
    }
    updateFromLayer();
    update_screen_for_document(m_document);
  }

  void updateFromLayer()
  {
    if (m_selfUpdate)
      return;

    m_timer.stop(); // Cancel current editions (just in case)

    base::ScopedValue switchSelf(m_selfUpdate, true);

    const bool tilemapVisibility = (m_layer && m_layer->isTilemap());
    const bool smartVisibility = (m_layer && m_layer->isSmartObject());
    if (m_layer) {
      name()->setText(m_layer->name().c_str());
      name()->setEnabled(true);

      const bool imageProps =
        ((m_layer->isImage() ||
          (m_layer->isGroup() && Preferences::instance().experimental.composeGroups())));

      if (imageProps) {
        mode()->setSelectedItem(nullptr);
        for (auto item : *mode()) {
          if (auto blendModeItem = dynamic_cast<BlendModeItem*>(item)) {
            if (blendModeItem->mode() == m_layer->blendMode()) {
              mode()->setSelectedItem(item);
              break;
            }
          }
        }
        mode()->setEnabled(!m_layer->isBackground());
        opacity()->setValue(m_layer->opacity());
        opacity()->setEnabled(!m_layer->isBackground());
      }

      modeLabel()->setVisible(imageProps);
      mode()->setVisible(imageProps);
      opacityLabel()->setVisible(imageProps);
      opacity()->setVisible(imageProps);

      color_t c = m_layer->userData().color();
      m_userDataView.color()->setColor(
        Color::fromRgb(rgba_getr(c), rgba_getg(c), rgba_getb(c), rgba_geta(c)));
      m_userDataView.textEdit()->setText(m_layer->userData().text());
    }
    else {
      name()->setText(Strings::layer_properties_no_layer());
      name()->setEnabled(false);
      mode()->setEnabled(false);
      opacity()->setEnabled(false);
      m_userDataView.setVisible(false, false);
    }

    bool uuidVisible = m_document && m_document->sprite() && m_document->sprite()->useLayerUuids();
    uuidLabel()->setVisible(uuidVisible);
    uuid()->setVisible(uuidVisible);

    if (uuidVisible)
      uuid()->setText(m_layer ? base::convert_to<std::string>(m_layer->uuid()) : std::string());

    smStatesLabel()->setVisible(smartVisibility);
    smStatesBox()->setVisible(smartVisibility);
    smStatesHelp()->setVisible(smartVisibility);
    smFrameLabel()->setVisible(smartVisibility);
    smFrame()->setVisible(smartVisibility);
    smAngleLabel()->setVisible(smartVisibility);
    smAngle()->setVisible(smartVisibility);
    smScaleLabel()->setVisible(smartVisibility);
    smScaleBox()->setVisible(smartVisibility);
    smActionsLabel()->setVisible(smartVisibility);
    smActionsBox()->setVisible(smartVisibility);

    if (smartVisibility) {
      const auto* sm = static_cast<const doc::LayerSmartObject*>(m_layer);
      const bool editable = m_layer->isEditable();
      const auto transform = sm->transformForFrame(m_transformFrame);
      smFrame()->setText(std::to_string(int(m_transformFrame) + 1));
      smStates()->setText(sm->animationStates());
      smStates()->setEnabled(editable);
      smStatesApply()->setEnabled(editable);
      smStatesNew()->setEnabled(editable);
      smAngle()->setText(formatNumber(transform.angle));
      smAngle()->setEnabled(editable);
      smScaleX()->setText(formatNumber(transform.scale.x));
      smScaleX()->setEnabled(editable);
      smScaleY()->setText(formatNumber(transform.scale.y));
      smScaleY()->setEnabled(editable);
      smFlipH()->setSelected(transform.flipH);
      smFlipH()->setEnabled(editable);
      smFlipV()->setSelected(transform.flipV);
      smFlipV()->setEnabled(editable);
      smReset()->setEnabled(editable);
    }

    if (tileset()->isVisible() != tilemapVisibility) {
      tileset()->setVisible(tilemapVisibility);
      tileset()->parent()->layout();
    }

    expandWindow(gfx::Size(std::max(bounds().w, sizeHint().w), sizeHint().h));
  }

  doc::frame_t m_transformFrame = UIContext::instance()->activeSite().frame();
  Timer m_timer;
  bool m_pendingChanges = false;
  Doc* m_document = nullptr;
  Layer* m_layer = nullptr;
  view::RealRange m_range;
  bool m_selfUpdate = false;
  UserDataView m_userDataView;
  bool m_remapAfterConfigure = false;
};

LayerPropertiesCommand::LayerPropertiesCommand() : Command(CommandId::LayerProperties())
{
}

bool LayerPropertiesCommand::onEnabled(Context* context)
{
  return context->isUIAvailable() &&
         context->checkFlags(ContextFlags::ActiveDocumentIsWritable | ContextFlags::HasActiveLayer);
}

void LayerPropertiesCommand::onExecute(Context* context)
{
  ContextReader reader(context);
  Doc* doc = static_cast<Doc*>(reader.document());
  LayerImage* layer = static_cast<LayerImage*>(reader.layer());

  if (!g_window)
    g_window = new LayerPropertiesWindow;

  g_window->setLayer(doc, layer);
  g_window->openWindow();

  // Focus layer name
  g_window->name()->requestFocus();
}

Command* CommandFactory::createLayerPropertiesCommand()
{
  return new LayerPropertiesCommand;
}

} // namespace app
