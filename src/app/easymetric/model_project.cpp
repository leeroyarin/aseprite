// Aseprite 2.0
// Copyright (C) 2026 Aseprite 2.0 contributors
//
// This program is distributed under the terms of
// the End-User License Agreement for Aseprite.

#include "app/easymetric/model_project.h"

#include "app/cmd/add_tag.h"
#include "app/cmd/set_user_data_property.h"
#include "app/doc.h"
#include "app/doc_api.h"
#include "app/tx.h"
#include "base/base64.h"
#include "doc/image.h"
#include "doc/layer.h"
#include "doc/primitives.h"
#include "doc/sprite.h"
#include "doc/tag.h"
#include "doc/tags.h"
#include "fmt/format.h"
#include "json11.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace app::easymetric {
namespace {

using json11::Json;
using Props = doc::UserData::Properties;
constexpr int kVersion = 1;
constexpr std::size_t kBlockSize = 36 * 1024; // The encoded string must fit a 16-bit length.
constexpr std::size_t kSettingsLimit = 60000;
const char* const kGroup = "aseprite2.easymetric.model";
const char* const kAsset = "asset";
const char* const kSettings = "scene";

[[noreturn]] void invalid()
{
  throw ModelIssue("easymetric_model.issue_the_saved_easymetric_model_or_settings_are_unr");
}

const Props* properties(const doc::Sprite& sprite)
{
  const auto& groups = sprite.userData().propertiesMaps();
  const auto it = groups.find(kGroup);
  return it == groups.end() ? nullptr : &it->second;
}

template<class T>
const T* find(const Props& p, const char* key)
{
  const auto it = p.find(key);
  if (it == p.end())
    return nullptr;
  return std::get_if<T>(static_cast<const doc::UserData::VariantBase*>(&it->second));
}

template<class T>
const T& field(const Props& p, const char* key)
{
  const auto* value = find<T>(p, key);
  if (!value)
    invalid();
  return *value;
}

int32_t integerProperty(const Props& properties, const char* key)
{
  const auto it = properties.find(key);
  if (it == properties.end())
    invalid();
  const auto& variant = static_cast<const doc::UserData::VariantBase&>(it->second);
  // The Aseprite encoder narrows integers to the smallest on-disk property type.
  return std::visit(
    [](const auto& value) -> int32_t {
      using T = std::decay_t<decltype(value)>;
      if constexpr (std::is_integral_v<T> && !std::is_same_v<T, bool>) {
        if (uint64_t(value) <= uint64_t(std::numeric_limits<int32_t>::max()))
          return int32_t(value);
      }
      invalid();
    },
    variant);
}

Json vec(Vec3 x)
{
  return Json::array{ x.x, x.y, x.z };
}

float number(const Json& field)
{
  const double value = field.number_value();
  if (!field.is_number() || !std::isfinite(value) || std::abs(value) > 1e15)
    invalid();
  return float(value);
}

int integer(const Json& field)
{
  const float value = number(field);
  if (std::floor(value) != value || std::abs(value) > 10000000)
    invalid();
  return int(value);
}

bool boolean(const Json& field)
{
  if (!field.is_bool())
    invalid();
  return field.bool_value();
}

Vec3 vec(const Json& x)
{
  if (x.array_items().size() != 3)
    invalid();
  return { number(x[0]), number(x[1]), number(x[2]) };
}

Json encodeColor(const std::array<uint8_t, 4>& c)
{
  return Json::array{ int(c[0]), int(c[1]), int(c[2]), int(c[3]) };
}

std::array<uint8_t, 4> decodeColor(const Json& x)
{
  if (!x.is_array() || x.array_items().size() != 4)
    invalid();
  std::array<uint8_t, 4> out{};
  for (int i = 0; i < 4; ++i) {
    const int v = integer(x[i]);
    if (v < 0 || v > 255)
      invalid();
    out[i] = uint8_t(v);
  }
  return out;
}

Json encodeKeys(const std::vector<MotionKey>& keys)
{
  Json::array value;
  for (const auto& key : keys)
    value.emplace_back(Json::array{ key.time, vec(key.value) });
  return value;
}

std::vector<MotionKey> decodeKeys(const Json& array)
{
  if (!array.is_array() || array.array_items().size() > 256)
    invalid();
  std::vector<MotionKey> result;
  for (const auto& key : array.array_items()) {
    if (key.array_items().size() != 2)
      invalid();
    result.push_back({ integer(key[0]), vec(key[1]) });
  }
  return result;
}

std::vector<Light> decodeLights(const Json& lights)
{
  if (!lights.is_array() || lights.array_items().size() > 4)
    invalid();
  std::vector<Light> result;
  for (const auto& item : lights.array_items()) {
    if (!item.is_object())
      invalid();
    Light light;
    light.enabled = boolean(item["enabled"]);
    light.point = boolean(item["point"]);
    light.color = vec(item["color"]);
    light.intensity = number(item["intensity"]);
    light.position = vec(item["position"]);
    light.range = number(item["range"]);
    light.azimuth = number(item["azimuth"]);
    light.elevation = number(item["elevation"]);
    result.push_back(light);
  }
  return result;
}

Json::array encodeLights(const std::vector<Light>& src)
{
  Json::array lights;
  for (const auto& light : src) {
    lights.emplace_back(Json::object{
      { "enabled",   light.enabled       },
      { "point",     light.point         },
      { "color",     vec(light.color)    },
      { "intensity", light.intensity     },
      { "position",  vec(light.position) },
      { "range",     light.range         },
      { "azimuth",   light.azimuth       },
      { "elevation", light.elevation     }
    });
  }
  return lights;
}

void writePrettyJson(std::string& out, const Json& value, int indent)
{
  if (value.is_object()) {
    const auto& items = value.object_items();
    if (items.empty()) {
      out += "{}";
      return;
    }
    out += "{\n";
    std::size_t idx = 0;
    for (const auto& kv : items) {
      out.append(indent + 2, ' ');
      out += Json(kv.first).dump();
      out += ": ";
      writePrettyJson(out, kv.second, indent + 2);
      if (++idx < items.size())
        out += ',';
      out += '\n';
    }
    out.append(indent, ' ');
    out += '}';
  }
  else if (value.is_array()) {
    const auto& items = value.array_items();
    const bool simple = std::all_of(items.begin(), items.end(), [](const Json& item) {
      return !item.is_object() && !item.is_array();
    });
    if (items.empty()) {
      out += "[]";
    }
    else if (simple) {
      out += '[';
      for (std::size_t i = 0; i < items.size(); ++i) {
        if (i > 0)
          out += ", ";
        out += items[i].dump();
      }
      out += ']';
    }
    else {
      out += "[\n";
      for (std::size_t i = 0; i < items.size(); ++i) {
        out.append(indent + 2, ' ');
        writePrettyJson(out, items[i], indent + 2);
        if (i + 1 < items.size())
          out += ',';
        out += '\n';
      }
      out.append(indent, ' ');
      out += ']';
    }
  }
  else {
    out += value.dump();
  }
}

void applyModelFields(Settings& s, const Json::object& obj)
{
  if (obj.count("orientation"))
    s.orientation = vec(obj.at("orientation"));
  if (obj.count("heading"))
    s.heading = number(obj.at("heading"));
  if (obj.count("modelScale"))
    s.modelScale = number(obj.at("modelScale"));
  if (obj.count("basePivot"))
    s.basePivot = boolean(obj.at("basePivot"));
}

void applyCameraFields(Settings& s, const Json::object& obj)
{
  if (obj.count("yaw"))
    s.yaw = number(obj.at("yaw"));
  if (obj.count("pitch"))
    s.pitch = number(obj.at("pitch"));
  if (obj.count("target"))
    s.target = vec(obj.at("target"));
  if (obj.count("framing"))
    s.framing = number(obj.at("framing"));
}

void applyLightingFields(Settings& s, const Json::object& obj)
{
  if (obj.count("ambient"))
    s.ambient = number(obj.at("ambient"));
  if (obj.count("shadowStrength"))
    s.shadowStrength = number(obj.at("shadowStrength"));
  if (obj.count("groundShadow"))
    s.groundShadow = boolean(obj.at("groundShadow"));
  if (obj.count("shadesEnabled"))
    s.shadesEnabled = boolean(obj.at("shadesEnabled"));
  if (obj.count("shades"))
    s.shades = integer(obj.at("shades"));
  if (obj.count("lights"))
    s.lights = decodeLights(obj.at("lights"));
}

void applyOutputFields(Settings& s, const Json::object& obj)
{
  if (obj.count("width"))
    s.width = integer(obj.at("width"));
  if (obj.count("height"))
    s.height = integer(obj.at("height"));
  if (obj.count("fps"))
    s.fps = integer(obj.at("fps"));
  if (obj.count("exportFrames"))
    s.exportFrames = integer(obj.at("exportFrames"));
  if (obj.count("fitSequenceOnBake"))
    s.fitSequenceOnBake = boolean(obj.at("fitSequenceOnBake"));
  if (obj.count("outlineEnabled"))
    s.outlineEnabled = boolean(obj.at("outlineEnabled"));
  if (obj.count("outlineWidth"))
    s.outlineWidth = integer(obj.at("outlineWidth"));
  if (obj.count("outlineColor"))
    s.outlineColor = decodeColor(obj.at("outlineColor"));
  if (obj.count("outlineSeparateLayer"))
    s.outlineSeparateLayer = boolean(obj.at("outlineSeparateLayer"));
}

} // namespace

bool hasEmbeddedModel(const doc::Sprite& sprite)
{
  const auto* props = properties(sprite);
  if (!props)
    return false;
  const auto it = props->find(kAsset);
  return it != props->end() && it->second.type() != USER_DATA_PROPERTY_TYPE_NULLPTR;
}

const Props* embeddedModel(const doc::Sprite& sprite)
{
  if (!hasEmbeddedModel(sprite))
    return nullptr;
  const auto* result = find<Props>(*properties(sprite), kAsset);
  if (!result)
    invalid();
  return result;
}

base::Uuid assetId(const Props& asset)
{
  return field<base::Uuid>(asset, "id");
}
std::string assetName(const Props& asset)
{
  return field<std::string>(asset, "name");
}

std::vector<uint8_t> decodeAsset(const Props& asset)
{
  if (integerProperty(asset, "version") != kVersion)
    throw ModelIssue("easymetric_model.issue_this_easymetric_model_uses_an_unsupported_proj");
  const auto bytes = integerProperty(asset, "size");
  const auto& chunks = field<doc::UserData::Vector>(asset, "glb");
  if (bytes < 20 || std::size_t(bytes) > kMaxGlbBytes ||
      chunks.size() != (std::size_t(bytes) + kBlockSize - 1) / kBlockSize)
    invalid();
  std::vector<uint8_t> result;
  result.reserve(bytes);
  for (const auto& chunk : chunks) {
    const auto* encoded = std::get_if<std::string>(
      static_cast<const doc::UserData::VariantBase*>(&chunk));
    const auto expected = std::min(kBlockSize, std::size_t(bytes) - result.size());
    if (!encoded || encoded->size() != 4 * ((expected + 2) / 3))
      invalid();
    const auto decoded = base::decode_base64(*encoded);
    if (decoded.size() != expected)
      invalid();
    result.insert(result.end(), decoded.begin(), decoded.end());
  }
  return result;
}

Props encodeAsset(const std::string& name, const Model& model)
{
  const auto& bytes = model.bytes();
  doc::UserData::Vector chunks;
  for (std::size_t i = 0; i < bytes.size(); i += kBlockSize) {
    std::string encoded;
    base::encode_base64(reinterpret_cast<const char*>(bytes.data() + i),
                        std::min(kBlockSize, bytes.size() - i),
                        encoded);
    chunks.emplace_back(std::move(encoded));
  }
  return {
    { "version", int32_t(kVersion)      },
    { "id",      base::Uuid::Generate() },
    { "name",    name                   },
    { "size",    int32_t(bytes.size())  },
    { "glb",     std::move(chunks)      }
  };
}

std::string savedSettings(const doc::Sprite& sprite)
{
  if (const auto* props = properties(sprite)) {
    if (const auto* json = find<std::string>(*props, kSettings))
      return *json;
  }
  return {};
}

Settings decodeSettings(const std::string& source)
{
  if (source.size() > kSettingsLimit)
    invalid();
  std::string error;
  const auto json = Json::parse(source, error);
  if (!error.empty() || !json.is_object())
    invalid();
  if (integer(json["version"]) != kVersion)
    throw ModelIssue("easymetric_model.issue_the_saved_easymetric_setup_uses_an_unsupported");

  Settings s;
  s.orientation = vec(json["orientation"]);
  s.heading = number(json["heading"]);
  s.modelScale = number(json["modelScale"]);
  s.basePivot = boolean(json["basePivot"]);
  s.yaw = number(json["yaw"]);
  s.pitch = number(json["pitch"]);
  s.target = vec(json["target"]);
  s.framing = number(json["framing"]);
  s.width = integer(json["width"]);
  s.height = integer(json["height"]);
  s.ambient = number(json["ambient"]);
  const auto& obj = json.object_items();
  if (obj.count("shadowStrength"))
    s.shadowStrength = number(json["shadowStrength"]);
  if (obj.count("groundShadow"))
    s.groundShadow = boolean(json["groundShadow"]);
  if (obj.count("shadesEnabled"))
    s.shadesEnabled = boolean(json["shadesEnabled"]);
  if (obj.count("shades"))
    s.shades = integer(json["shades"]);
  if (obj.count("outlineEnabled"))
    s.outlineEnabled = boolean(json["outlineEnabled"]);
  if (obj.count("outlineWidth"))
    s.outlineWidth = integer(json["outlineWidth"]);
  if (obj.count("outlineColor"))
    s.outlineColor = decodeColor(json["outlineColor"]);
  if (obj.count("outlineSeparateLayer"))
    s.outlineSeparateLayer = boolean(json["outlineSeparateLayer"]);
  const auto& lights = json["lights"];
  s.lights = decodeLights(lights);
  s.clip = integer(json["clip"]);
  s.start = integer(json["start"]);
  s.end = integer(json["end"]);
  s.fps = integer(json["fps"]);
  if (obj.count("exportFrames"))
    s.exportFrames = integer(json["exportFrames"]);
  s.speed = number(json["speed"]);
  s.loop = boolean(json["loop"]);
  s.turntable = boolean(json["turntable"]);
  if (obj.count("fitSequenceOnBake"))
    s.fitSequenceOnBake = boolean(json["fitSequenceOnBake"]);
  s.position = decodeKeys(json["position"]);
  s.rotation = decodeKeys(json["rotation"]);
  s.scale = decodeKeys(json["scale"]);
  return s;
}

std::string encodeSettings(const Settings& s)
{
  auto lights = encodeLights(s.lights);
  const auto value = Json(Json::object{
                            { "version",              kVersion                          },
                            { "orientation",          vec(s.orientation)                },
                            { "heading",              s.heading                         },
                            { "modelScale",           s.modelScale                      },
                            { "basePivot",            s.basePivot                       },
                            { "yaw",                  s.yaw                             },
                            { "pitch",                s.pitch                           },
                            { "target",               vec(s.target)                     },
                            { "framing",              s.framing                         },
                            { "width",                s.width                           },
                            { "height",               s.height                          },
                            { "ambient",              s.ambient                         },
                            { "shadowStrength",       s.shadowStrength                  },
                            { "groundShadow",         s.groundShadow                    },
                            { "shadesEnabled",        s.shadesEnabled                   },
                            { "shades",               s.shades                          },
                            { "outlineEnabled",       s.outlineEnabled                  },
                            { "outlineWidth",         s.outlineWidth                    },
                            { "outlineColor",         encodeColor(s.outlineColor)       },
                            { "outlineSeparateLayer", s.outlineSeparateLayer            },
                            { "lights",               std::move(lights)                 },
                            { "clip",                 s.clip                            },
                            { "start",                s.start                           },
                            { "end",                  s.end                             },
                            { "fps",                  s.fps                             },
                            { "exportFrames",         s.exportFrames                    },
                            { "speed",                s.speed                           },
                            { "loop",                 s.loop                            },
                            { "turntable",            s.turntable                       },
                            { "fitSequenceOnBake",    s.fitSequenceOnBake               },
                            { "position",             encodeKeys(s.position)            },
                            { "rotation",             encodeKeys(s.rotation)            },
                            { "scale",                encodeKeys(s.scale)               }
  })
                       .dump();
  if (value.size() > kSettingsLimit)
    throw ModelIssue("easymetric_model.issue_the_easymetric_setup_exceeds_its_project_stora");
  return value;
}

std::string encodeConfig(const Settings& s)
{
  const Json doc = Json::object{
    { "version", kVersion },
    { "model",
      Json::object{ { "orientation", vec(s.orientation) },
                    { "heading", s.heading },
                    { "modelScale", s.modelScale },
                    { "basePivot", s.basePivot } } },
    { "camera",
      Json::object{ { "yaw", s.yaw },
                    { "pitch", s.pitch },
                    { "target", vec(s.target) },
                    { "framing", s.framing } } },
    { "lighting",
      Json::object{ { "ambient", s.ambient },
                    { "shadowStrength", s.shadowStrength },
                    { "groundShadow", s.groundShadow },
                    { "shadesEnabled", s.shadesEnabled },
                    { "shades", s.shades },
                    { "lights", encodeLights(s.lights) } } },
    { "output",
      Json::object{ { "width", s.width },
                    { "height", s.height },
                    { "fps", s.fps },
                    { "exportFrames", s.exportFrames },
                    { "fitSequenceOnBake", s.fitSequenceOnBake },
                    { "outlineEnabled", s.outlineEnabled },
                    { "outlineWidth", s.outlineWidth },
                    { "outlineColor", encodeColor(s.outlineColor) },
                    { "outlineSeparateLayer", s.outlineSeparateLayer } } }
  };
  std::string out;
  writePrettyJson(out, doc, 0);
  out += '\n';
  if (out.size() > kSettingsLimit)
    throw ModelIssue("easymetric_model.issue_the_easymetric_setup_exceeds_its_project_stora");
  return out;
}

Settings applyConfig(const Settings& current, const std::string& source)
{
  if (source.size() > kSettingsLimit)
    invalid();
  std::string error;
  const auto json = Json::parse(source, error);
  if (!error.empty() || !json.is_object())
    invalid();
  const auto& obj = json.object_items();
  if (obj.count("version") && integer(obj.at("version")) != kVersion)
    throw ModelIssue("easymetric_model.issue_the_saved_easymetric_setup_uses_an_unsupported");

  Settings next = current;
  bool appliedAny = false;
  if (obj.count("model")) {
    if (!obj.at("model").is_object())
      invalid();
    applyModelFields(next, obj.at("model").object_items());
    appliedAny = true;
  }
  if (obj.count("camera")) {
    if (!obj.at("camera").is_object())
      invalid();
    applyCameraFields(next, obj.at("camera").object_items());
    appliedAny = true;
  }
  if (obj.count("lighting")) {
    if (!obj.at("lighting").is_object())
      invalid();
    applyLightingFields(next, obj.at("lighting").object_items());
    appliedAny = true;
  }
  if (obj.count("output")) {
    if (!obj.at("output").is_object())
      invalid();
    applyOutputFields(next, obj.at("output").object_items());
    appliedAny = true;
  }
  if (!appliedAny) {
    // Allow flat scene/preset JSON without section wrappers.
    for (const char* k : { "orientation",
                           "heading",
                           "modelScale",
                           "basePivot",
                           "yaw",
                           "pitch",
                           "target",
                           "framing",
                           "ambient",
                           "shadowStrength",
                           "groundShadow",
                           "shadesEnabled",
                           "shades",
                           "lights",
                           "width",
                           "height",
                           "fps",
                           "exportFrames",
                           "fitSequenceOnBake",
                           "outlineEnabled",
                           "outlineWidth",
                           "outlineColor",
                           "outlineSeparateLayer" }) {
      if (obj.count(k)) {
        appliedAny = true;
        break;
      }
    }
    if (!appliedAny)
      invalid();
    applyModelFields(next, obj);
    applyCameraFields(next, obj);
    applyLightingFields(next, obj);
    applyOutputFields(next, obj);
  }
  return next;
}

void setProjectAsset(Tx& tx, doc::Sprite& sprite, Props asset)
{
  tx(
    new cmd::SetUserDataProperty(&sprite, kGroup, kAsset, doc::UserData::Variant(std::move(asset))));
}

void setProjectSettings(Tx& tx, doc::Sprite& sprite, std::string settings)
{
  tx(new cmd::SetUserDataProperty(&sprite,
                                  kGroup,
                                  kSettings,
                                  doc::UserData::Variant(std::move(settings))));
}

void copyProject(const doc::Sprite& source, doc::Sprite& output)
{
  if (const auto* props = properties(source))
    output.userData().properties(kGroup) = *props;
}

doc::ImageRef toDocumentImage(const Image& src)
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

void commitBakedOutput(Tx& tx,
                       Doc& document,
                       const std::vector<BakedFrame>& frames,
                       doc::frame_t startFrame,
                       bool sequence,
                       bool separateOutlineLayer,
                       const std::string& renderLayerName,
                       const std::string& outlineLayerName,
                       const std::string& tagName)
{
  if (frames.empty())
    return;
  auto* destSprite = document.sprite();
  DocApi api = document.getApi(tx);
  const auto end = startFrame + doc::frame_t(frames.size()) - 1;
  if (end >= destSprite->totalFrames())
    api.setTotalFrames(destSprite, end + 1);
  auto* renderLayer = api.newLayer(destSprite->root(), renderLayerName);
  doc::LayerImage* outlineLayer =
    separateOutlineLayer ? api.newLayer(destSprite->root(), outlineLayerName) : nullptr;
  for (std::size_t i = 0; i < frames.size(); ++i) {
    const auto frame = startFrame + doc::frame_t(i);
    api.addCel(renderLayer, frame, toDocumentImage(frames[i].model));
    if (outlineLayer)
      api.addCel(outlineLayer, frame, toDocumentImage(frames[i].outline));
    if (sequence)
      api.setFrameDuration(destSprite, frame, frames[i].duration);
  }
  if (sequence) {
    auto* tag = new doc::Tag(startFrame, end);
    auto uniqueName = tagName;
    for (int i = 2; destSprite->tags().getByName(uniqueName); ++i)
      uniqueName = fmt::format("{} {}", tagName, i);
    tag->setName(uniqueName);
    tx(new cmd::AddTag(destSprite, tag));
  }
}

} // namespace app::easymetric
