// Aseprite 2.0
// Copyright (C) 2026 Aseprite 2.0 contributors
//
// This program is distributed under the terms of
// the End-User License Agreement for Aseprite.

#include "app/easymetric/model_project.h"

#include "app/cmd/set_user_data_property.h"
#include "app/tx.h"
#include "base/base64.h"
#include "doc/sprite.h"
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
  const auto& lights = json["lights"];
  if (!lights.is_array() || lights.array_items().size() > 4)
    invalid();
  s.lights.clear();
  for (const auto& item : lights.array_items()) {
    Light light;
    light.enabled = boolean(item["enabled"]);
    light.point = boolean(item["point"]);
    light.color = vec(item["color"]);
    light.intensity = number(item["intensity"]);
    light.position = vec(item["position"]);
    light.range = number(item["range"]);
    light.azimuth = number(item["azimuth"]);
    light.elevation = number(item["elevation"]);
    s.lights.push_back(light);
  }
  s.clip = integer(json["clip"]);
  s.start = integer(json["start"]);
  s.end = integer(json["end"]);
  s.fps = integer(json["fps"]);
  s.speed = number(json["speed"]);
  s.loop = boolean(json["loop"]);
  s.turntable = boolean(json["turntable"]);
  s.position = decodeKeys(json["position"]);
  s.rotation = decodeKeys(json["rotation"]);
  s.scale = decodeKeys(json["scale"]);
  return s;
}

std::string encodeSettings(const Settings& s)
{
  Json::array lights;
  for (const auto& light : s.lights) {
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
  const auto value = Json(Json::object{
                            { "version",     kVersion               },
                            { "orientation", vec(s.orientation)     },
                            { "heading",     s.heading              },
                            { "modelScale",  s.modelScale           },
                            { "basePivot",   s.basePivot            },
                            { "yaw",         s.yaw                  },
                            { "pitch",       s.pitch                },
                            { "target",      vec(s.target)          },
                            { "framing",     s.framing              },
                            { "width",       s.width                },
                            { "height",      s.height               },
                            { "ambient",     s.ambient              },
                            { "lights",      std::move(lights)      },
                            { "clip",        s.clip                 },
                            { "start",       s.start                },
                            { "end",         s.end                  },
                            { "fps",         s.fps                  },
                            { "speed",       s.speed                },
                            { "loop",        s.loop                 },
                            { "turntable",   s.turntable            },
                            { "position",    encodeKeys(s.position) },
                            { "rotation",    encodeKeys(s.rotation) },
                            { "scale",       encodeKeys(s.scale)    }
  })
                       .dump();
  if (value.size() > kSettingsLimit)
    throw ModelIssue("easymetric_model.issue_the_easymetric_setup_exceeds_its_project_stora");
  return value;
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

} // namespace app::easymetric
