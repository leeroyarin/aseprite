// Aseprite 2.0
// Copyright (C) 2026 Aseprite 2.0 contributors
//
// This program is distributed under the terms of
// the End-User License Agreement for Aseprite.

#ifndef APP_EASYMETRIC_MODEL_PROJECT_H_INCLUDED
#define APP_EASYMETRIC_MODEL_PROJECT_H_INCLUDED
#pragma once

#include "app/easymetric/model.h"
#include "doc/frame.h"
#include "doc/image_ref.h"
#include "doc/user_data.h"

namespace doc {
class Sprite;
}
namespace app {
class Doc;
class Tx;
} // namespace app

namespace app::easymetric {

struct BakedFrame {
  Image model;
  Image outline;
  int duration = 0;
};

bool hasEmbeddedModel(const doc::Sprite& sprite);
const doc::UserData::Properties* embeddedModel(const doc::Sprite& sprite);
base::Uuid assetId(const doc::UserData::Properties& asset);
std::string assetName(const doc::UserData::Properties& asset);
std::vector<uint8_t> decodeAsset(const doc::UserData::Properties& asset);
doc::UserData::Properties encodeAsset(const std::string& name, const Model& model);

std::string savedSettings(const doc::Sprite& sprite);
Settings decodeSettings(const std::string& source);
std::string encodeSettings(const Settings& settings);
std::string encodeConfig(const Settings& settings);
Settings applyConfig(const Settings& current, const std::string& source);
void setProjectAsset(Tx& tx, doc::Sprite& sprite, doc::UserData::Properties asset);
void setProjectSettings(Tx& tx, doc::Sprite& sprite, std::string settings);
void copyProject(const doc::Sprite& original, doc::Sprite& output);
doc::ImageRef toDocumentImage(const Image& src);
void commitBakedOutput(Tx& tx,
                       Doc& document,
                       const std::vector<BakedFrame>& frames,
                       doc::frame_t startFrame,
                       bool sequence,
                       bool separateOutlineLayer,
                       const std::string& renderLayerName,
                       const std::string& outlineLayerName,
                       const std::string& tagName);

} // namespace app::easymetric
#endif
