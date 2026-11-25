// Aseprite 2.0
// Copyright (C) 2026 Aseprite 2.0 contributors
//
// This program is distributed under the terms of
// the End-User License Agreement for Aseprite.

#include "app/easymetric/model_studio.h"

#include "app/cmd/add_tag.h"
#include "app/doc.h"
#include "app/doc_api.h"
#include "app/easymetric/model.h"
#include "app/easymetric/model_project.h"
#include "app/file_selector.h"
#include "app/i18n/strings.h"
#include "app/job.h"
#include "app/tx.h"
#include "app/ui/button_set.h"
#include "app/ui/doc_view.h"
#include "app/ui/editor/editor.h"
#include "app/ui_context.h"
#include "app/util/conversion_to_surface.h"
#include "base/file_handle.h"
#include "base/fs.h"
#include "base/scoped_value.h"
#include "doc/image.h"
#include "doc/image_spec.h"
#include "doc/layer.h"
#include "doc/primitives.h"
#include "doc/sprite.h"
#include "doc/tag.h"
#include "doc/tags.h"
#include "fmt/format.h"
#include "os/sampling.h"
#include "os/surface.h"
#include "os/system.h"
#include "ui/alert.h"
#include "ui/box.h"
#include "ui/button.h"
#include "ui/combobox.h"
#include "ui/entry.h"
#include "ui/graphics.h"
#include "ui/label.h"
#include "ui/message.h"
#include "ui/paint_event.h"
#include "ui/scale.h"
#include "ui/slider.h"
#include "ui/timer.h"
#include "ui/view.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <future>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>

namespace app::easymetric {
namespace {

#define MODEL_TEXT(name) Strings::easymetric_model_##name()

using Clock = std::chrono::steady_clock;

std::string localizedError(const std::exception& error)
{
  if (const auto* issue = dynamic_cast<const ModelIssue*>(&error)) {
    if (!issue->detail.empty())
      return Strings::Format(issue->what(), issue->detail);
    return Strings::Translate(issue->what());
  }
  return error.what();
}

doc::ImageRef documentImage(const Image& src)
{
  doc::ImageRef out(doc::Image::create(IMAGE_RGB, src.width, src.height));
  for (int y = 0; y < src.height; ++y) {
    for (int x = 0; x < src.width; ++x) {
      const auto off = (std::size_t(y) * src.width + x) * 4;
      doc::put_pixel(
        out.get(),
        x,
        y,
        doc::rgba(src.rgba[off], src.rgba[off + 1], src.rgba[off + 2], src.rgba[off + 3]));
    }
  }
  return out;
}

class WorkJob : public Job {
public:
  using Work = std::function<void(const Canceled&, const std::function<void(double)>&)>;
  WorkJob(const std::string& title, Work work) : Job(title, true), m_work(std::move(work)) {}

  bool run()
  {
    startJob();
    waitJob();
    if (m_error && !isCanceled())
      std::rethrow_exception(m_error);
    return !isCanceled();
  }

private:
  void onJob() override
  {
    try {
      m_work([this] { return isCanceled(); }, [this](double value) { jobProgress(value); });
    }
    catch (...) {
      m_error = std::current_exception();
    }
  }

  Work m_work;
  std::exception_ptr m_error;
};

class Preview : public ui::Widget {
public:
  explicit Preview(const Settings& settings, const int& light)
    : ui::Widget(ui::kGenericWidget)
    , m_settings(settings)
    , m_light(light)
  {
    setExpansive(true);
  }

  std::function<void(float yaw, float pitch)> orbit;
  std::function<void(Vec3)> moveLight;
  std::function<void()> finish;

  void setImage(const Image& image)
  {
    auto doc = documentImage(image);
    m_surface = os::System::instance()->makeRgbaSurface(image.width, image.height);
    convert_image_to_surface(doc.get(),
                             nullptr,
                             m_surface.get(),
                             0,
                             0,
                             0,
                             0,
                             image.width,
                             image.height);
    invalidate();
  }

  void clear()
  {
    m_surface.reset();
    invalidate();
  }

  void zoom(float factor)
  {
    m_zoom = std::clamp(m_zoom * factor, 0.125f, 16.0f);
    invalidate();
  }

  void resetZoom()
  {
    m_pan = gfx::Point();
    m_zoom = 1;
    invalidate();
  }

protected:
  void onPaint(ui::PaintEvent& ev) override
  {
    auto* graphics = ev.graphics();
    const auto bounds = clientBounds();
    const int tile = 8 * ui::guiscale();
    for (int y = bounds.y; y < bounds.y2(); y += tile) {
      for (int x = bounds.x; x < bounds.x2(); x += tile) {
        const bool even = ((x - bounds.x) / tile + (y - bounds.y) / tile) % 2 == 0;
        const int gray = even ? 67 : 84;
        graphics->fillRect(
          gfx::rgba(gray, gray, gray),
          gfx::Rect(x, y, std::min(tile, bounds.x2() - x), std::min(tile, bounds.y2() - y)));
      }
    }
    if (!m_surface)
      return;
    const auto image = imageRect();
    const ui::Paint composite;
    graphics->drawSurface(m_surface.get(),
                          gfx::Rect(0, 0, m_surface->width(), m_surface->height()),
                          image,
                          os::Sampling(os::Sampling::Filter::Nearest),
                          &composite);
    if (const auto* light = selectedLight()) {
      const auto point = marker(*light);
      const auto color = gfx::rgba(255, 219, 112);
      if (light->point) {
        const int r = 5 * ui::guiscale();
        graphics->drawRect(color, gfx::Rect(point.x - r, point.y - r, 2 * r + 1, 2 * r + 1));
        graphics->drawLine(color, point + gfx::Point(-r, 0), point + gfx::Point(r, 0));
        graphics->drawLine(color, point + gfx::Point(0, -r), point + gfx::Point(0, r));
      }
      else {
        const auto center = markerAt(m_settings.target);
        const auto delta = point - center;
        graphics->drawLine(color, point, center);
        graphics->drawLine(
          color,
          center,
          center + gfx::Point(delta.x / 6 - delta.y / 12, delta.y / 6 + delta.x / 12));
        graphics->drawLine(
          color,
          center,
          center + gfx::Point(delta.x / 6 + delta.y / 12, delta.y / 6 - delta.x / 12));
      }
    }
  }

  bool onProcessMessage(ui::Message* msg) override
  {
    switch (msg->type()) {
      case ui::kMouseDownMessage: {
        auto* mouse = static_cast<ui::MouseMessage*>(msg);
        if (!m_surface)
          break;
        if (mouse->right() || mouse->middle())
          m_drag = Drag::Pan;
        else if (mouse->left()) {
          const auto* light = selectedLight();
          if (light && light->point) {
            const auto p = marker(*light) + bounds().origin();
            const int r = 10 * ui::guiscale();
            if (gfx::Rect(p.x - r, p.y - r, 2 * r + 1, 2 * r + 1).contains(mouse->position())) {
              m_drag = Drag::Light;
              m_lightStart = light->position;
            }
          }
          if (m_drag == Drag::None)
            m_drag = Drag::Orbit;
        }
        if (m_drag != Drag::None) {
          m_start = mouse->position();
          m_panStart = m_pan;
          m_yaw = m_settings.yaw;
          m_pitch = m_settings.pitch;
          captureMouse();
          return true;
        }
        break;
      }
      case ui::kMouseMoveMessage: {
        if (m_drag == Drag::None)
          break;
        const auto pos = static_cast<ui::MouseMessage*>(msg)->position() - m_start;
        if (m_drag == Drag::Pan) {
          m_pan = m_panStart + pos;
          invalidate();
        }
        else if (m_drag == Drag::Orbit) {
          orbit(m_yaw - float(pos.x) / ui::guiscale(), m_pitch + float(pos.y) / ui::guiscale());
        }
        else {
          const auto image = imageRect();
          moveLight(moveInView(m_settings,
                               m_lightStart,
                               float(pos.x) * m_settings.width / image.w,
                               float(pos.y) * m_settings.height / image.h));
        }
        return true;
      }
      case ui::kMouseUpMessage:
        if (m_drag != Drag::None) {
          const auto previous = m_drag;
          m_drag = Drag::None;
          releaseMouse();
          if (previous != Drag::Pan)
            finish();
          return true;
        }
        break;
      case ui::kMouseWheelMessage: {
        const auto delta = static_cast<ui::MouseMessage*>(msg)->wheelDelta();
        zoom(std::pow(1.25f, float(-std::clamp(delta.y, -4, 4))));
        return true;
      }
      case ui::kDoubleClickMessage: resetZoom(); return true;
      default:                      break;
    }
    return Widget::onProcessMessage(msg);
  }

private:
  enum class Drag { None, Orbit, Pan, Light };

  gfx::Rect imageRect() const
  {
    const auto bounds = clientBounds();
    const float fit = std::max(0.05f,
                               std::min(float(bounds.w - 12) / m_surface->width(),
                                        float(bounds.h - 12) / m_surface->height()));
    const int w = std::max(1, int(std::round(m_surface->width() * fit * m_zoom)));
    const int h = std::max(1, int(std::round(m_surface->height() * fit * m_zoom)));
    return gfx::Rect(bounds.x + (bounds.w - w) / 2 + m_pan.x,
                     bounds.y + (bounds.h - h) / 2 + m_pan.y,
                     w,
                     h);
  }

  const Light* selectedLight() const
  {
    return m_light >= 0 && std::size_t(m_light) < m_settings.lights.size() ?
             &m_settings.lights[m_light] :
             nullptr;
  }

  gfx::Point markerAt(Vec3 point) const
  {
    const auto p = project(m_settings, point);
    const auto rect = imageRect();
    const auto clip = [](float value) { return int(std::clamp(value, -20000.0f, 20000.0f)); };
    return { rect.x + clip(p.x * rect.w / m_settings.width),
             rect.y + clip(p.y * rect.h / m_settings.height) };
  }

  gfx::Point marker(const Light& light) const
  {
    if (light.point)
      return markerAt(light.position);
    const auto d = lightDirection(light);
    const auto p = m_settings.target;
    const float scale = 0.35f * m_settings.framing;
    return markerAt({ p.x + d.x * scale, p.y + d.y * scale, p.z + d.z * scale });
  }

  const Settings& m_settings;
  const int& m_light;
  os::SurfaceRef m_surface;
  float m_zoom = 1, m_yaw = 0, m_pitch = 0;
  gfx::Point m_start, m_pan, m_panStart;
  Vec3 m_lightStart;
  Drag m_drag = Drag::None;
};

struct Number {
  ui::Entry* entry;
  float min, max;

  float value() const
  {
    const auto text = entry->text();
    char* end = nullptr;
    const float result = std::strtof(text.c_str(), &end);
    if (end == text.c_str() || *end || !std::isfinite(result) || result < min || result > max)
      throw std::runtime_error(Strings::easymetric_model_number_range(min, max));
    return result;
  }

  int integer() const
  {
    const auto v = value();
    if (std::floor(v) != v)
      throw std::runtime_error(MODEL_TEXT(whole_number));
    return int(v);
  }

  void set(float value) const { entry->setText(fmt::format("{:.6g}", value)); }
};

class ModelStudio : public ui::Box {
public:
  ModelStudio(DocView& view, std::function<void()> surfaces, std::function<void()> close)
    : ui::Box(ui::HORIZONTAL)
    , m_view(view)
    , m_timer(30, this)
  {
    setChildSpacing(6 * ui::guiscale());
    m_preview = new Preview(m_settings, m_marker);
    m_preview->orbit = [this](float yaw, float pitch) {
      m_settings.yaw = std::remainder(yaw, 360.0f);
      m_settings.pitch = std::clamp(pitch, -75.0f, 75.0f);
      m_yaw->setValue(int(std::round(std::remainder(m_settings.yaw - m_settings.heading, 360.0f))));
      m_pitch->setValue(int(std::round(m_settings.pitch)));
      requestPreview();
    };
    m_preview->moveLight = [this](Vec3 point) {
      if (m_marker < 0 || std::size_t(m_marker) >= m_settings.lights.size())
        return;
      auto& light = m_settings.lights[m_marker];
      light.position = { std::clamp(point.x, -100.0f, 100.0f),
                         std::clamp(point.y, -100.0f, 100.0f),
                         std::clamp(point.z, -100.0f, 100.0f) };
      setVector(m_lightPosition, light.position);
      requestPreview();
    };
    m_preview->finish = [this] { action([this] { commit(); }); };
    addChild(m_preview);

    m_sidebar = new ui::VBox;
    m_sidebar->setChildSpacing(4 * ui::guiscale());
    m_sidebar->setMinSize(gfx::Size(270 * ui::guiscale(), 0));
    m_sidebar->setMaxSize(gfx::Size(270 * ui::guiscale(), std::numeric_limits<int>::max()));
    addChild(m_sidebar);

    auto* scroll = new ui::View;
    scroll->setExpansive(true);
    auto* controls = new ui::VBox;
    controls->setChildSpacing(4 * ui::guiscale());
    scroll->attachToView(controls);
    m_sidebar->addChild(scroll);

    auto& source = row(*controls);
    auto* sourceChoice = combo(source, { MODEL_TEXT(six_surfaces), MODEL_TEXT(imported_model) });
    sourceChoice->setSelectedItemIndex(1);
    sourceChoice->Change.connect([this, sourceChoice, surfaces = std::move(surfaces)] {
      if (sourceChoice->getSelectedItemIndex() == 0)
        action([&] {
          commit();
          surfaces();
        });
    });
    button(source, MODEL_TEXT(exit), [this, close = std::move(close)] {
      commit();
      close();
    });

    button(*controls, MODEL_TEXT(import), [this] { importModel(); });
    m_assetLabel = new ui::Label(MODEL_TEXT(no_model));
    controls->addChild(m_assetLabel);
    button(*controls, MODEL_TEXT(import_report), [this] {
      ui::Alert dialog;
      dialog.setTitle(MODEL_TEXT(import_report));
      if (!m_model || m_model->warnings().empty())
        dialog.addLabel(MODEL_TEXT(no_warnings), ui::LEFT);
      else {
        for (const auto& warning : m_model->warnings())
          dialog.addLabel(Strings::Translate(warning.c_str()), ui::LEFT);
      }
      dialog.addButton(Strings::general_ok());
      dialog.show();
    });

    m_tools = new ui::VBox;
    m_tools->setChildSpacing(4 * ui::guiscale());
    controls->addChild(m_tools);
    auto* tabs = new ButtonSet(3, true);
    tabs->addItem(MODEL_TEXT(model))->initTheme();
    tabs->addItem(MODEL_TEXT(lighting))->initTheme();
    tabs->addItem(MODEL_TEXT(animation))->initTheme();
    m_tools->addChild(tabs);
    tabs->ItemChange.connect([this, tabs, scroll](ButtonSet::Item* item) {
      const int selected = tabs->getItemIndex(item);
      for (int i = 0; i < 3; ++i)
        m_panels[i]->setVisible(i == selected);
      m_marker = selected == 1 ? m_lightIndex : -1;
      m_preview->invalidate();
      m_tools->layout();
      scroll->updateView();
    });

    for (auto*& panel : m_panels) {
      panel = new ui::VBox;
      panel->setChildSpacing(3 * ui::guiscale());
      m_tools->addChild(panel);
    }
    buildModel(*m_panels[0]);
    buildLighting(*m_panels[1]);
    buildAnimation(*m_panels[2]);
    tabs->setSelectedItem(0, false);
    m_panels[1]->setVisible(false);
    m_panels[2]->setVisible(false);

    m_output = new ui::VBox;
    m_output->setChildSpacing(3 * ui::guiscale());
    m_sidebar->addChild(m_output);
    buildOutput(*m_output);
    m_status = new ui::Label("");
    m_sidebar->addChild(m_status);
    m_scroll = scroll;

    syncProject(true);
    m_timer.Tick.connect([this] { tick(); });
    m_timer.start();
  }

  ~ModelStudio() override
  {
    prepareForExit();
    if (m_previewFuture.valid())
      m_previewFuture.wait();
  }

  void prepareForExit()
  {
    m_timer.stop();
    ++m_generation;
  }

private:
  using Callback = std::function<void()>;
  using Text = std::string;

  doc::Sprite& sprite() const { return *m_view.document()->sprite(); }

  void action(const Callback& work)
  {
    try {
      work();
    }
    catch (const std::exception& ex) {
      setStatus(localizedError(ex));
    }
  }

  ui::HBox& row(ui::Box& parent)
  {
    auto* box = new ui::HBox;
    box->setChildSpacing(4 * ui::guiscale());
    parent.addChild(box);
    return *box;
  }

  ui::Button* button(ui::Box& parent, const Text& title, Callback work)
  {
    auto* widget = new ui::Button(title);
    parent.addChild(widget);
    widget->Click.connect([this, work = std::move(work)] { action(work); });
    return widget;
  }

  ui::ComboBox* combo(ui::Box& parent, std::initializer_list<Text> items)
  {
    auto* widget = new ui::ComboBox;
    widget->setExpansive(true);
    for (const auto& item : items)
      widget->addItem(item);
    if (!widget->empty())
      widget->setSelectedItemIndex(0);
    parent.addChild(widget);
    return widget;
  }

  ui::CheckBox* check(ui::Box& parent, const Text& name)
  {
    auto* widget = new ui::CheckBox(name);
    parent.addChild(widget);
    return widget;
  }

  void label(ui::Box& parent, const Text& name) { parent.addChild(new ui::Label(name)); }

  Number number(ui::Box& parent, const Text& name, float min, float max)
  {
    if (!name.empty())
      label(parent, name);
    auto* entry = new ui::Entry(16, "%s", "0");
    entry->setExpansive(true);
    parent.addChild(entry);
    return { entry, min, max };
  }

  std::array<Number, 3> vectorFields(ui::Box& parent, const Text& title, float min, float max)
  {
    label(parent, title);
    auto& box = row(parent);
    return { number(box, MODEL_TEXT(x), min, max),
             number(box, MODEL_TEXT(y), min, max),
             number(box, MODEL_TEXT(z), min, max) };
  }

  static void setVector(const std::array<Number, 3>& fields, Vec3 value)
  {
    fields[0].set(value.x);
    fields[1].set(value.y);
    fields[2].set(value.z);
  }

  static Vec3 vector(const std::array<Number, 3>& fields)
  {
    return { fields[0].value(), fields[1].value(), fields[2].value() };
  }

  void buildModel(ui::Box& panel)
  {
    m_orientation = vectorFields(panel, MODEL_TEXT(orientation), -360, 360);
    auto& setup = row(panel);
    m_modelScale = number(setup, MODEL_TEXT(scale), 0.01f, 100);
    m_basePivot = check(setup, MODEL_TEXT(base_pivot));
    button(panel, MODEL_TEXT(apply_model), [this] {
      Settings next = m_settings;
      next.orientation = vector(m_orientation);
      next.modelScale = m_modelScale.value();
      next.basePivot = m_basePivot->isSelected();
      apply(std::move(next));
    });

    auto& sides = row(panel);
    int quarter = 0;
    for (const auto& side :
         { MODEL_TEXT(front), MODEL_TEXT(right), MODEL_TEXT(back), MODEL_TEXT(left) }) {
      button(sides, side, [this, quarter] {
        m_settings.yaw = std::remainder(m_settings.heading + quarter * 90, 360.0f);
        m_settings.pitch = 0;
        commit();
        refreshCamera();
        requestPreview();
      });
      ++quarter;
    }
    button(panel, MODEL_TEXT(set_front), [this] {
      m_settings.heading = m_settings.yaw;
      commit();
      refreshCamera();
      setStatus(MODEL_TEXT(front_saved));
    });
    m_heading = new ui::Label("");
    panel.addChild(m_heading);

    auto& yawRow = row(panel);
    label(yawRow, MODEL_TEXT(yaw));
    m_yaw = new ui::Slider(-180, 180, 45);
    m_yaw->setExpansive(true);
    yawRow.addChild(m_yaw);
    auto& pitchRow = row(panel);
    label(pitchRow, MODEL_TEXT(pitch));
    m_pitch = new ui::Slider(-75, 75, 30);
    m_pitch->setExpansive(true);
    pitchRow.addChild(m_pitch);
    const auto change = [this] {
      m_settings.yaw = std::remainder(m_settings.heading + m_yaw->getValue(), 360.0f);
      m_settings.pitch = float(m_pitch->getValue());
      requestPreview();
      if (!m_yaw->hasCapture() && !m_pitch->hasCapture())
        action([this] { commit(); });
    };
    m_yaw->Change.connect(change);
    m_pitch->Change.connect(change);
    m_yaw->SliderReleased.connect([this] { action([this] { commit(); }); });
    m_pitch->SliderReleased.connect([this] { action([this] { commit(); }); });

    auto& framing = row(panel);
    m_framing = number(framing, MODEL_TEXT(framing), 1e-12f, 1e15f);
    button(framing, MODEL_TEXT(apply), [this] {
      Settings next = m_settings;
      next.framing = m_framing.value();
      apply(std::move(next));
    });
    auto& fit = row(panel);
    button(fit, MODEL_TEXT(fit_model), [this] { fitScene(false); });
    button(fit, MODEL_TEXT(fit_sequence), [this] { fitScene(true); });
    auto& zoom = row(panel);
    label(zoom, MODEL_TEXT(preview_zoom));
    button(zoom, MODEL_TEXT(zoom_out), [this] { m_preview->zoom(0.8f); });
    button(zoom, MODEL_TEXT(zoom_in), [this] { m_preview->zoom(1.25f); });
    button(zoom, MODEL_TEXT(reset), [this] { m_preview->resetZoom(); });
  }

  void buildLighting(ui::Box& panel)
  {
    auto& select = row(panel);
    m_lights = combo(select, {});
    m_lights->Change.connect([this] {
      if (m_refreshing)
        return;
      m_lightIndex = m_lights->getSelectedItemIndex();
      m_marker = m_lightIndex;
      refreshLight();
    });
    auto& actions = row(panel);
    button(actions, MODEL_TEXT(add_point), [this] { addLight(true); });
    button(actions, MODEL_TEXT(add_directional), [this] { addLight(false); });
    button(panel, MODEL_TEXT(remove_light), [this] {
      if (m_lightIndex >= 0) {
        Settings next = m_settings;
        next.lights.erase(next.lights.begin() + m_lightIndex);
        --m_lightIndex;
        apply(std::move(next));
      }
    });
    auto& ambient = row(panel);
    m_ambient = number(ambient, MODEL_TEXT(ambient), 0, 2);
    button(ambient, MODEL_TEXT(apply), [this] {
      Settings next = m_settings;
      next.ambient = m_ambient.value();
      apply(std::move(next));
    });

    m_lightControls = new ui::VBox;
    panel.addChild(m_lightControls);
    auto& main = row(*m_lightControls);
    m_lightEnabled = check(main, MODEL_TEXT(enabled));
    m_lightIntensity = number(main, MODEL_TEXT(intensity), 0, 8);
    auto& color = row(*m_lightControls);
    label(color, MODEL_TEXT(color));
    m_lightColor = { number(color, MODEL_TEXT(red), 0, 255),
                     number(color, MODEL_TEXT(green), 0, 255),
                     number(color, MODEL_TEXT(blue), 0, 255) };

    m_pointControls = new ui::VBox;
    m_lightControls->addChild(m_pointControls);
    m_lightPosition = vectorFields(*m_pointControls, MODEL_TEXT(light_position), -100, 100);
    m_lightRange = number(row(*m_pointControls), MODEL_TEXT(range), 0.01f, 1000);

    m_directionControls = new ui::VBox;
    m_lightControls->addChild(m_directionControls);
    m_lightAzimuth = number(row(*m_directionControls), MODEL_TEXT(azimuth), -360, 360);
    m_lightElevation = number(row(*m_directionControls), MODEL_TEXT(elevation), -90, 90);
    button(*m_lightControls, MODEL_TEXT(apply_light), [this] {
      if (m_lightIndex < 0)
        return;
      Settings next = m_settings;
      auto& light = next.lights[m_lightIndex];
      light.enabled = m_lightEnabled->isSelected();
      light.intensity = m_lightIntensity.value();
      const auto color = vector(m_lightColor);
      const auto channel = [](float x) {
        x /= 255;
        return x <= 0.04045f ? x / 12.92f : std::pow((x + 0.055f) / 1.055f, 2.4f);
      };
      light.color = { channel(color.x), channel(color.y), channel(color.z) };
      if (light.point) {
        light.position = vector(m_lightPosition);
        light.range = m_lightRange.value();
      }
      else {
        light.azimuth = m_lightAzimuth.value();
        light.elevation = m_lightElevation.value();
      }
      apply(std::move(next));
    });
  }

  void buildAnimation(ui::Box& panel)
  {
    m_clips = combo(panel, {});
    m_clips->Change.connect([this] {
      if (m_refreshing || !m_model)
        return;
      action([this] {
        Settings next = m_settings;
        next.clip = m_clips->getSelectedItemIndex() - 1;
        next.start = 0;
        next.end = next.clip < 0 ? 1000 : m_model->clips()[next.clip].duration;
        next.turntable = false;
        m_time = 0;
        apply(std::move(next));
      });
    });
    auto& trim = row(panel);
    m_start = number(trim, MODEL_TEXT(from_ms), 0, 599999);
    m_end = number(trim, MODEL_TEXT(to_ms), 1, 600000);
    auto& speed = row(panel);
    m_speed = number(speed, MODEL_TEXT(speed), 0.25f, 4);
    m_fps = number(speed, MODEL_TEXT(fps), 1, 60);
    m_loop = check(panel, MODEL_TEXT(loop));
    m_loop->Click.connect([this] {
      action([this] {
        m_settings.loop = m_loop->isSelected();
        commit();
      });
    });
    button(panel, MODEL_TEXT(apply_timing), [this] {
      Settings next = m_settings;
      next.start = m_start.integer();
      next.end = m_end.integer();
      next.speed = m_speed.value();
      next.fps = m_fps.integer();
      apply(std::move(next));
    });
    button(panel, MODEL_TEXT(turntable), [this] {
      Settings next = m_settings;
      next.turntable = !next.turntable;
      if (next.turntable) {
        next.clip = -1;
        next.start = 0;
        next.end = 1000;
        next.speed = 1;
      }
      m_time = 0;
      apply(std::move(next));
      setStatus(m_settings.turntable ? MODEL_TEXT(turntable_on) : MODEL_TEXT(turntable_off));
    });

    auto& play = row(panel);
    m_play = button(play, MODEL_TEXT(play), [this] {
      m_playing = !m_playing;
      if (m_playing && m_time >= duration(m_settings))
        m_time = 0;
      m_lastPlay = Clock::now();
      refreshTime();
      requestPreview();
    });
    m_scrub = new ui::Slider(0, 1000, 0);
    m_scrub->setExpansive(true);
    play.addChild(m_scrub);
    m_scrub->Change.connect([this] {
      m_playing = false;
      m_time = m_scrub->getValue();
      refreshTime();
      requestPreview();
    });

    m_sequence = new ui::Label("");
    panel.addChild(m_sequence);
    m_tracks = combo(
      panel,
      { MODEL_TEXT(position_keys), MODEL_TEXT(rotation_keys), MODEL_TEXT(scale_keys) });
    m_tracks->Change.connect([this] { refreshKeys(); });
    m_keys = combo(panel, {});
    m_keys->Change.connect([this] {
      if (m_refreshing)
        return;
      const int index = m_keys->getSelectedItemIndex();
      if (index < 0 || index >= int(track().size()))
        return;
      const auto& key = track()[index];
      m_playing = false;
      m_time = std::min(key.time, duration(m_settings));
      setVector(m_keyValues, key.value);
      refreshTime();
      requestPreview();
    });
    m_keyValues = vectorFields(panel, MODEL_TEXT(key_value), -10000, 10000);
    button(panel, MODEL_TEXT(set_key), [this] { changeKey(KeyAction::Set); });
    auto& keys = row(panel);
    button(keys, MODEL_TEXT(move_key), [this] { changeKey(KeyAction::Move); });
    button(keys, MODEL_TEXT(delete_key), [this] { changeKey(KeyAction::Remove); });
  }

  void buildOutput(ui::Box& panel)
  {
    auto& resolution = row(panel);
    label(resolution, MODEL_TEXT(output_size));
    m_preset = combo(resolution,
                     { MODEL_TEXT(size16),
                       MODEL_TEXT(size32),
                       MODEL_TEXT(size64),
                       MODEL_TEXT(size128),
                       MODEL_TEXT(size256),
                       MODEL_TEXT(custom) });
    m_preset->Change.connect([this] {
      action([this] {
        if (m_refreshing)
          return;
        const int selected = m_preset->getSelectedItemIndex();
        if (selected >= 0 && selected < 5) {
          Settings next = m_settings;
          next.width = next.height = 16 << selected;
          apply(std::move(next));
        }
      });
    });
    auto& custom = row(panel);
    m_width = number(custom, MODEL_TEXT(width), kMinSize, kMaxSize);
    m_height = number(custom, MODEL_TEXT(height), kMinSize, kMaxSize);
    button(custom, MODEL_TEXT(apply), [this] {
      Settings next = m_settings;
      next.width = m_width.integer();
      next.height = m_height.integer();
      apply(std::move(next));
    });
    m_destination = combo(panel, { MODEL_TEXT(new_sprite), MODEL_TEXT(active_sprite) });
    auto& bake = row(panel);
    button(bake, MODEL_TEXT(render_pose), [this] { renderOutput(false); });
    button(bake, MODEL_TEXT(bake_sequence), [this] { renderOutput(true); });
  }

  void setStatus(const Text& status)
  {
    m_status->setText(status);
    m_sidebar->layout();
  }

  void syncProject(bool force = false)
  {
    auto& docSprite = sprite();
    if (!force && m_seenVersion == docSprite.version())
      return;
    m_seenVersion = docSprite.version();
    try {
      const auto* asset = embeddedModel(docSprite);
      const auto id = asset ? std::optional(assetId(*asset)) : std::nullopt;
      const auto serialized = savedSettings(docSprite);
      if (!force && id == m_assetId && serialized == m_serialized)
        return;
      if (id != m_assetId || (asset && !m_model)) {
        m_model = asset ? std::make_shared<Model>(decodeAsset(*asset)) : nullptr;
        m_name = asset ? assetName(*asset) : "";
        m_assetId = id;
        m_time = 0;
        m_playing = false;
        m_preview->clear();
      }
      m_settings = asset ? decodeSettings(serialized) : Settings{};
      if (m_model)
        m_model->validate(m_settings);
      m_serialized = serialized;
      refresh();
      requestPreview();
    }
    catch (const std::exception& ex) {
      m_model.reset();
      m_assetId.reset();
      m_settings = Settings{};
      m_preview->clear();
      refresh();
      requestPreview();
      setStatus(localizedError(ex));
    }
  }

  void commit()
  {
    if (!m_model)
      return;
    m_model->validate(m_settings);
    const auto serialized = encodeSettings(m_settings);
    if (serialized == m_serialized)
      return;
    auto* document = m_view.document();
    Tx tx(Tx::LockDoc, document->context(), document, MODEL_TEXT(edit_setup));
    setProjectSettings(tx, sprite(), serialized);
    tx.commit();
    m_serialized = serialized;
    m_seenVersion = sprite().version();
    document->notifyGeneralUpdate();
  }

  void apply(Settings next)
  {
    if (!m_model)
      return;
    m_model->validate(next);
    const auto previous = m_settings;
    m_settings = std::move(next);
    try {
      commit();
    }
    catch (...) {
      m_settings = previous;
      refresh();
      requestPreview();
      throw;
    }
    refresh();
    requestPreview();
  }

  void importModel()
  {
    base::paths selection;
    if (!show_file_selector(MODEL_TEXT(import), "", { "glb" }, FileSelectorType::Open, selection) ||
        selection.empty())
      return;

    const auto path = selection.front();
    const auto name = base::get_file_name(path);
    if (!base::has_file_extension(path, { "glb" }))
      throw std::runtime_error(MODEL_TEXT(glb_only));
    std::shared_ptr<Model> next;
    Settings settings;
    doc::UserData::Properties encoded;
    const auto tooLarge = MODEL_TEXT(model_too_large);
    const auto readFailed = MODEL_TEXT(read_failed);
    WorkJob import(MODEL_TEXT(import_job), [&](const Canceled& canceled, const auto& progress) {
      auto file = base::open_file_with_exception(path, "rb");
      std::array<uint8_t, 64 * 1024> buffer;
      std::vector<uint8_t> bytes;
      for (;;) {
        if (canceled())
          return;
        const auto count = std::fread(buffer.data(), 1, buffer.size(), file.get());
        if (count > kMaxGlbBytes - bytes.size())
          throw std::runtime_error(tooLarge);
        bytes.insert(bytes.end(), buffer.data(), buffer.data() + count);
        if (count < buffer.size()) {
          if (std::ferror(file.get()))
            throw std::runtime_error(readFailed);
          break;
        }
      }
      progress(0.25);
      next = std::make_shared<Model>(std::move(bytes), canceled);
      next->fit(settings, false, canceled);
      progress(0.75);
      if (!canceled())
        encoded = encodeAsset(name, *next);
      progress(1);
    });
    try {
      if (!import.run())
        return;
    }
    catch (const std::exception& ex) {
      throw std::runtime_error(Strings::easymetric_model_import_failed(name, localizedError(ex)));
    }
    const auto newId = assetId(encoded);
    const auto serialized = encodeSettings(settings);
    auto* document = m_view.document();
    Tx tx(Tx::LockDoc, document->context(), document, MODEL_TEXT(import_job));
    setProjectAsset(tx, sprite(), std::move(encoded));
    setProjectSettings(tx, sprite(), serialized);
    tx.commit();
    m_assetId = newId;
    m_model = std::move(next);
    m_settings = std::move(settings);
    m_serialized = serialized;
    m_name = name;
    m_time = 0;
    m_playing = false;
    m_seenVersion = sprite().version();
    m_preview->clear();
    refresh();
    requestPreview();
    document->notifyGeneralUpdate();
    setStatus(m_model->warnings().empty() ? MODEL_TEXT(imported) : MODEL_TEXT(imported_warnings));
  }

  void refreshCamera()
  {
    m_yaw->setValue(int(std::round(std::remainder(m_settings.yaw - m_settings.heading, 360.0f))));
    m_pitch->setValue(int(std::round(m_settings.pitch)));
    m_heading->setText(
      Strings::Format("easymetric_model.heading", fmt::format("{:.1f}", m_settings.heading)));
    m_framing.set(m_settings.framing);
  }

  void refresh()
  {
    base::ScopedValue guard(m_refreshing, true);
    m_tools->setVisible(bool(m_model));
    m_output->setVisible(bool(m_model));
    m_sidebar->layout();
    m_assetLabel->setText(m_model ? m_name : MODEL_TEXT(no_model));
    if (!m_model) {
      m_scroll->updateView();
      return;
    }
    setVector(m_orientation, m_settings.orientation);
    m_modelScale.set(m_settings.modelScale);
    m_basePivot->setSelected(m_settings.basePivot);
    refreshCamera();

    m_lights->deleteAllItems();
    for (std::size_t i = 0; i < m_settings.lights.size(); ++i) {
      const auto& light = m_settings.lights[i];
      const auto type = light.point ? MODEL_TEXT(point_light) : MODEL_TEXT(directional_light);
      m_lights->addItem(Strings::easymetric_model_light_number(i + 1, type));
    }
    m_lightIndex = m_settings.lights.empty() ?
                     -1 :
                     std::clamp(m_lightIndex, 0, int(m_settings.lights.size()) - 1);
    m_lights->setSelectedItemIndex(m_lightIndex);
    if (m_lightIndex < 0)
      m_lights->getEntryWidget()->setText("");
    if (m_panels[1]->isVisible())
      m_marker = m_lightIndex;
    refreshLight();
    m_ambient.set(m_settings.ambient);

    m_clips->deleteAllItems();
    m_clips->addItem(MODEL_TEXT(rest_pose));
    for (const auto& clip : m_model->clips())
      m_clips->addItem(clip.name);
    m_clips->setSelectedItemIndex(m_settings.clip + 1);
    m_start.set(float(m_settings.start));
    m_end.set(float(m_settings.end));
    m_speed.set(m_settings.speed);
    m_fps.set(float(m_settings.fps));
    m_loop->setSelected(m_settings.loop);
    refreshTime();
    refreshKeys();
    m_width.set(float(m_settings.width));
    m_height.set(float(m_settings.height));
    int preset = 5;
    for (int i = 0; i < 5; ++i)
      if (m_settings.width == (16 << i) && m_settings.height == m_settings.width)
        preset = i;
    m_preset->setSelectedItemIndex(preset);
    m_tools->layout();
    m_scroll->updateView();
  }

  void refreshLight()
  {
    m_lightControls->setVisible(m_lightIndex >= 0);
    if (m_lightIndex >= 0) {
      const auto& light = m_settings.lights[m_lightIndex];
      m_lightEnabled->setSelected(light.enabled);
      m_lightIntensity.set(light.intensity);
      const auto channel = [](float x) {
        x = std::clamp(x, 0.0f, 1.0f);
        return std::round(255 *
                          (x <= 0.0031308f ? 12.92f * x : 1.055f * std::pow(x, 1 / 2.4f) - 0.055f));
      };
      setVector(m_lightColor,
                { channel(light.color.x), channel(light.color.y), channel(light.color.z) });
      m_pointControls->setVisible(light.point);
      m_directionControls->setVisible(!light.point);
      setVector(m_lightPosition, light.position);
      m_lightRange.set(light.range);
      m_lightAzimuth.set(light.azimuth);
      m_lightElevation.set(light.elevation);
    }
    m_preview->invalidate();
    m_tools->layout();
    m_scroll->updateView();
  }

  void refreshTime()
  {
    const int total = duration(m_settings);
    m_time = std::clamp(m_time, 0.0, double(total));
    m_scrub->setRange(0, total);
    m_scrub->setValue(int(m_time));
    m_play->setText(m_playing ? MODEL_TEXT(pause) : MODEL_TEXT(play));
    const int count = int(std::ceil(total * double(m_settings.fps) / 1000));
    m_sequence->setText(Strings::easymetric_model_sequence_info(int(m_time), total, count));
  }

  std::vector<MotionKey>& track(Settings& settings) const
  {
    switch (m_tracks->getSelectedItemIndex()) {
      case 1:  return settings.rotation;
      case 2:  return settings.scale;
      default: return settings.position;
    }
  }

  const std::vector<MotionKey>& track() { return track(m_settings); }

  void refreshKeys()
  {
    base::ScopedValue guard(m_refreshing, true);
    const auto& keys = track();
    const auto selected = m_keys->getSelectedItemIndex();
    m_keys->deleteAllItems();
    for (const auto& key : keys)
      m_keys->addItem(Strings::easymetric_model_key_time(key.time));
    if (keys.empty()) {
      m_keys->getEntryWidget()->setText("");
      setVector(m_keyValues, m_tracks->getSelectedItemIndex() == 2 ? Vec3{ 1, 0, 0 } : Vec3{});
    }
    else {
      const int index = std::clamp(selected, 0, int(keys.size()) - 1);
      m_keys->setSelectedItemIndex(index);
      setVector(m_keyValues, keys[index].value);
    }
  }

  enum class KeyAction { Set, Move, Remove };

  void changeKey(KeyAction operation)
  {
    Settings next = m_settings;
    auto& keys = track(next);
    const int time = int(std::round(m_time));
    const int selected = m_keys->getSelectedItemIndex();
    Vec3 value;
    if (operation == KeyAction::Set) {
      value = vector(m_keyValues);
      if (m_tracks->getSelectedItemIndex() == 2)
        value.y = value.z = 0;
    }
    else {
      if (selected < 0 || selected >= int(keys.size()))
        return;
      value = keys[selected].value;
      keys.erase(keys.begin() + selected);
    }
    int newIndex = -1;
    if (operation != KeyAction::Remove) {
      const auto at = std::lower_bound(keys.begin(),
                                       keys.end(),
                                       time,
                                       [](const MotionKey& key, int t) { return key.time < t; });
      newIndex = int(at - keys.begin());
      if (at != keys.end() && at->time == time) {
        if (operation == KeyAction::Move)
          throw std::runtime_error(MODEL_TEXT(key_exists));
        at->value = value;
      }
      else
        keys.insert(at, { time, value });
    }
    apply(std::move(next));
    if (newIndex >= 0) {
      base::ScopedValue guard(m_refreshing, true);
      m_keys->setSelectedItemIndex(newIndex);
      setVector(m_keyValues, value);
    }
  }

  void addLight(bool point)
  {
    if (m_settings.lights.size() >= 4)
      throw std::runtime_error(MODEL_TEXT(light_limit));
    Settings next = m_settings;
    Light light;
    light.point = point;
    if (point) {
      light.position = next.target;
      light.position.x += 0.25f;
      light.position.y += 0.25f;
      light.position.z += 0.5f;
    }
    next.lights.push_back(light);
    m_lightIndex = int(next.lights.size()) - 1;
    apply(std::move(next));
  }

  void fitScene(bool sequence)
  {
    if (!m_model)
      return;
    const auto model = m_model;
    Settings next = m_settings;
    if (sequence) {
      WorkJob job(MODEL_TEXT(fit_sequence), [&](const Canceled& canceled, const auto& progress) {
        model->fit(next, true, canceled);
        progress(1);
      });
      if (!job.run())
        return;
    }
    else
      model->fit(next, false);
    apply(std::move(next));
  }

  void renderOutput(bool sequence)
  {
    if (!m_model)
      return;
    m_playing = false;
    refreshTime();
    commit();
    const auto model = m_model;
    const Settings setup = m_settings;
    const bool append = m_destination->getSelectedItemIndex() == 1;
    if (append && (sprite().pixelFormat() != IMAGE_RGB || sprite().width() != setup.width ||
                   sprite().height() != setup.height))
      throw std::runtime_error(MODEL_TEXT(destination_size));

    const auto frames = sequence ? samples(setup) :
                                   std::vector<Sample>{
                                     { m_time, 0 }
    };
    if (append && sequence && std::size_t(int(sprite().totalFrames())) + frames.size() > 65535)
      throw std::runtime_error(MODEL_TEXT(frame_limit));

    std::vector<doc::ImageRef> images;
    WorkJob job(sequence ? MODEL_TEXT(bake_sequence) : MODEL_TEXT(render_pose),
                [&](const Canceled& canceled, const auto& progress) {
                  for (const auto& frame : frames) {
                    if (canceled())
                      return;
                    images.push_back(documentImage(model->render(setup, frame.time, canceled)));
                    progress(double(images.size()) / frames.size());
                  }
                });
    if (!job.run())
      return;

    std::unique_ptr<Doc> newDocument;
    Doc* destination = m_view.document();
    if (!append) {
      auto output = std::make_unique<doc::Sprite>(
        doc::ImageSpec(doc::ColorMode::RGB, setup.width, setup.height));
      copyProject(sprite(), *output);
      newDocument = std::make_unique<Doc>(output.release());
      newDocument->setFilename(base::replace_extension(m_name, "aseprite"));
      destination = newDocument.get();
    }
    auto* destSprite = destination->sprite();
    const auto start = !append  ? doc::frame_t(0) :
                       sequence ? destSprite->totalFrames() :
                                  m_view.primaryEditor()->frame();
    {
      Tx tx(Tx::LockDoc,
            m_view.document()->context(),
            destination,
            sequence ? MODEL_TEXT(bake_sequence) : MODEL_TEXT(render_pose));
      DocApi api = destination->getApi(tx);
      const auto end = start + doc::frame_t(images.size()) - 1;
      if (end >= destSprite->totalFrames())
        api.setTotalFrames(destSprite, end + 1);
      auto* layer = api.newLayer(destSprite->root(), MODEL_TEXT(rendered_layer));
      for (std::size_t i = 0; i < images.size(); ++i) {
        const auto frame = start + doc::frame_t(i);
        api.addCel(layer, frame, images[i]);
        if (sequence)
          api.setFrameDuration(destSprite, frame, frames[i].duration);
      }
      if (sequence) {
        const auto name = setup.turntable ? MODEL_TEXT(turntable_name) :
                          setup.clip >= 0 ? model->clips()[setup.clip].name :
                                            MODEL_TEXT(motion_name);
        auto* tag = new doc::Tag(start, end);
        auto uniqueName = name;
        for (int i = 2; destSprite->tags().getByName(uniqueName); ++i)
          uniqueName = fmt::format("{} {}", name, i);
        tag->setName(uniqueName);
        tx(new cmd::AddTag(destSprite, tag));
      }
      tx.commit();
    }
    if (newDocument) {
      newDocument->setContext(m_view.document()->context());
      newDocument.release();
    }
    else {
      destination->notifyGeneralUpdate();
      m_view.primaryEditor()->setFrame(start);
    }
    setStatus(Strings::easymetric_model_baked(images.size()));
  }

  void requestPreview()
  {
    ++m_generation;
    m_previewPending = bool(m_model);
    m_preview->invalidate();
  }

  void tick()
  {
    if (!isVisible())
      return;
    syncProject();
    if (m_playing && m_model) {
      const auto now = Clock::now();
      m_time += std::chrono::duration<double, std::milli>(now - m_lastPlay).count();
      m_lastPlay = now;
      const auto total = duration(m_settings);
      if (m_time >= total) {
        if (m_settings.loop && total > 0)
          m_time = std::fmod(m_time, total);
        else {
          m_time = total;
          m_playing = false;
        }
      }
      refreshTime();
      // Let a frame that is already rendering finish during playback.
      m_previewPending = true;
    }

    if (m_previewFuture.valid()) {
      if (m_previewFuture.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
        return;
      const bool newest = m_previewGeneration == m_generation.load();
      try {
        auto image = m_previewFuture.get();
        if (newest && m_model)
          m_preview->setImage(image);
      }
      catch (const std::exception& ex) {
        if (newest) {
          m_playing = false;
          m_previewPending = false;
          refreshTime();
          setStatus(localizedError(ex));
        }
      }
    }
    if (m_previewPending && m_model) {
      m_previewPending = false;
      const auto generation = m_generation.load();
      m_previewGeneration = generation;
      const auto model = m_model;
      const auto settings = m_settings;
      const double time = m_time;
      m_previewFuture = std::async(std::launch::async, [this, model, settings, time, generation] {
        return model->render(settings, time, [this, generation] {
          return m_generation != generation;
        });
      });
    }
  }

  DocView& m_view;
  ui::Timer m_timer;
  Settings m_settings;
  std::shared_ptr<Model> m_model;
  std::optional<base::Uuid> m_assetId;
  std::string m_name, m_serialized;
  doc::ObjectVersion m_seenVersion{};
  Preview* m_preview = nullptr;
  ui::View* m_scroll = nullptr;
  ui::VBox *m_tools = nullptr, *m_sidebar = nullptr, *m_output = nullptr;
  std::array<ui::VBox*, 3> m_panels{};
  ui::Label *m_assetLabel = nullptr, *m_status = nullptr, *m_heading = nullptr,
            *m_sequence = nullptr;
  ui::Slider *m_yaw = nullptr, *m_pitch = nullptr, *m_scrub = nullptr;
  ui::CheckBox *m_basePivot = nullptr, *m_lightEnabled = nullptr, *m_loop = nullptr;
  ui::ComboBox *m_lights = nullptr, *m_clips = nullptr, *m_tracks = nullptr, *m_keys = nullptr,
               *m_preset = nullptr, *m_destination = nullptr;
  ui::VBox *m_lightControls = nullptr, *m_pointControls = nullptr, *m_directionControls = nullptr;
  ui::Button* m_play = nullptr;
  std::array<Number, 3> m_orientation{}, m_lightPosition{}, m_lightColor{}, m_keyValues{};
  Number m_modelScale{}, m_framing{}, m_ambient{}, m_lightIntensity{}, m_lightRange{},
    m_lightAzimuth{}, m_lightElevation{}, m_start{}, m_end{}, m_speed{}, m_fps{}, m_width{},
    m_height{};
  int m_lightIndex = -1, m_marker = -1;
  bool m_playing = false, m_previewPending = false, m_refreshing = false;
  double m_time = 0;
  Clock::time_point m_lastPlay;
  std::atomic<unsigned> m_generation{ 0 };
  unsigned m_previewGeneration = 0;
  std::future<Image> m_previewFuture;
};

#undef MODEL_TEXT

} // namespace

ui::Widget* createModelStudio(DocView& view,
                              std::function<void()> surfaces,
                              std::function<void()> close)
{
  return new ModelStudio(view, std::move(surfaces), std::move(close));
}

void prepareModelStudioForExit(ui::Widget* studio)
{
  if (auto* model = dynamic_cast<ModelStudio*>(studio))
    model->prepareForExit();
}

} // namespace app::easymetric
