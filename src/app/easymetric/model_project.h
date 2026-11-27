// Aseprite 2.0
// Copyright (C) 2026 Aseprite 2.0 contributors
//
// This program is distributed under the terms of
// the End-User License Agreement for Aseprite.

#ifndef APP_EASYMETRIC_MODEL_PROJECT_H_INCLUDED
#define APP_EASYMETRIC_MODEL_PROJECT_H_INCLUDED
#pragma once

#include "app/easymetric/model.h"
#include "doc/user_data.h"

namespace doc {
class Sprite;
}
namespace app {
class Tx;
}

namespace app::easymetric {

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

} // namespace app::easymetric
#endif
