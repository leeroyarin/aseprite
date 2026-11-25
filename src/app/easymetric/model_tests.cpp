// Aseprite 2.0
// Copyright (C) 2026 Aseprite 2.0 contributors
//
// This program is distributed under the terms of
// the End-User License Agreement for Aseprite.

#define TEST_APP
#include "tests/app_test.h"

#include "app/context.h"
#include "app/doc.h"
#include "app/doc_undo.h"
#include "app/easymetric/model.h"
#include "app/easymetric/model_project.h"
#include "app/file/file.h"
#include "app/tx.h"
#include "base/convert_to.h"
#include "base/fs.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace app::easymetric;

void word(std::vector<uint8_t>& bytes, uint32_t value)
{
  for (int i = 0; i < 4; ++i)
    bytes.push_back(uint8_t(value >> (i * 8)));
}

void floats(std::vector<uint8_t>& data, std::initializer_list<float> values)
{
  for (float value : values) {
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    word(data, bits);
  }
}

std::vector<uint8_t> glb(std::string json, std::vector<uint8_t> binary)
{
  while (json.size() % 4)
    json += ' ';
  while (binary.size() % 4)
    binary.push_back(0);
  std::vector<uint8_t> result;
  word(result, 0x46546c67);
  word(result, 2);
  word(result, uint32_t(28 + json.size() + binary.size()));
  word(result, uint32_t(json.size()));
  word(result, 0x4e4f534a);
  result.insert(result.end(), json.begin(), json.end());
  word(result, uint32_t(binary.size()));
  word(result, 0x004e4942);
  result.insert(result.end(), binary.begin(), binary.end());
  return result;
}

std::vector<uint8_t> riggedTriangle(bool external = false, std::size_t size = 40960)
{
  std::vector<uint8_t> data;
  floats(data, { -1, -1, 0, 1, -1, 0, -0.6f, 1, 0 });
  data.resize(48);
  floats(data, { 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0 });
  floats(data, { 0, 1 });
  floats(data, { 0, 0, 0, 0.6f, 0, 0 });
  data.resize(size);
  const std::string uri = external ? R"("uri":"missing.bin",)" : "";
  return glb(
    R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0,1]}],
        "nodes":[{"mesh":0,"skin":0},{}],"skins":[{"joints":[1]}],
        "meshes":[{"primitives":[{"attributes":{"POSITION":0,"JOINTS_0":1,"WEIGHTS_0":2}}]}],
        "animations":[{"name":"Slide","samplers":[{"input":3,"output":4}],
                        "channels":[{"sampler":0,"target":{"node":1,"path":"translation"}}]}],
        "buffers":[{)" +
      uri + R"("byteLength":)" + std::to_string(size) + R"(}],
        "bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},
                       {"buffer":0,"byteOffset":36,"byteLength":12},
                       {"buffer":0,"byteOffset":48,"byteLength":48},
                       {"buffer":0,"byteOffset":96,"byteLength":8},
                       {"buffer":0,"byteOffset":104,"byteLength":24}],
        "accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3",
                      "min":[-1,-1,0],"max":[1,1,0]},
                     {"bufferView":1,"componentType":5121,"count":3,"type":"VEC4"},
                     {"bufferView":2,"componentType":5126,"count":3,"type":"VEC4"},
                     {"bufferView":3,"componentType":5126,"count":2,"type":"SCALAR",
                      "min":[0],"max":[1]},
                     {"bufferView":4,"componentType":5126,"count":2,"type":"VEC3"}]})",
    std::move(data));
}

int firstOpaqueX(const Image& image)
{
  int result = image.width;
  for (int y = 0; y < image.height; ++y)
    for (int x = 0; x < image.width; ++x)
      if (image.rgba[(y * image.width + x) * 4 + 3])
        result = std::min(result, x);
  return result;
}

} // namespace

TEST(EasymetricModel, PixelsRespondToDensityLightingAndSkinning)
{
  const Model model(riggedTriangle());
  ASSERT_EQ(model.clips().size(), 1u);
  EXPECT_EQ(model.clips()[0].duration, 1000);
  Settings settings;
  settings.yaw = settings.pitch = 0;
  model.fit(settings, false);
  const auto rest = model.render(settings, 0);
  ASSERT_LT(firstOpaqueX(rest), rest.width);
  settings.width = settings.height = 32;
  EXPECT_EQ(model.render(settings, 0).rgba.size(), 32u * 32u * 4u);
  settings.width = settings.height = 64;

  settings.ambient = 0;
  settings.lights[0].point = true;
  settings.lights[0].position = { 0, 0, 0.5f };
  const auto near = model.render(settings, 0);
  settings.lights[0].position.z = -0.5f;
  const auto behind = model.render(settings, 0);
  EXPECT_EQ(firstOpaqueX(near), firstOpaqueX(behind));
  EXPECT_NE(near.rgba, behind.rgba);

  settings.clip = 0;
  const auto pose = model.render(settings, 500);
  EXPECT_GT(firstOpaqueX(pose), firstOpaqueX(rest));
  settings.position = {
    { 0,    {}             },
    { 1000, { 0.4f, 0, 0 } }
  };
  EXPECT_GT(firstOpaqueX(model.render(settings, 500)), firstOpaqueX(pose));
}

TEST(EasymetricModel, SamplesExcludeTheEndAndRetainDuration)
{
  Settings settings;
  settings.start = 100;
  settings.end = 1050;
  settings.speed = 1.25f;
  const auto sequence = samples(settings);
  ASSERT_EQ(sequence.size(), 10u);
  EXPECT_EQ(sequence.front().time, 0);
  EXPECT_LT(sequence.back().time, duration(settings));
  int total = 0;
  for (const auto& frame : sequence) {
    EXPECT_GT(frame.duration, 0);
    total += frame.duration;
  }
  EXPECT_EQ(total, 760);
  settings.end = 600000;
  EXPECT_THROW(samples(settings), std::runtime_error);
}

TEST(EasymetricModel, SparseOverridesUseTheirOwnStride)
{
  std::vector<uint8_t> bytes;
  floats(bytes, { -1, -1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 });
  bytes.insert(bytes.end(), { 1, 2, 0, 0 });
  floats(bytes, { 1, -1, 0, -0.6f, 1, 0 });
  const Model sparse(glb(
    R"({"asset":{"version":"2.0"},"scenes":[{"nodes":[0]}],"nodes":[{"mesh":0}],
        "meshes":[{"primitives":[{"attributes":{"POSITION":0}}]}],
        "buffers":[{"byteLength":76}],
        "bufferViews":[{"buffer":0,"byteLength":48,"byteStride":16},
                       {"buffer":0,"byteOffset":48,"byteLength":2},
                       {"buffer":0,"byteOffset":52,"byteLength":24}],
        "accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3",
                      "min":[-1,-1,0],"max":[1,1,0],
                      "sparse":{"count":2,"indices":{"bufferView":1,"componentType":5121},
                                "values":{"bufferView":2}}}]})",
    std::move(bytes)));
  Settings settings;
  settings.yaw = settings.pitch = 0;
  sparse.fit(settings, false);
  const Model reference(riggedTriangle());
  EXPECT_EQ(sparse.render(settings, 0).rgba, reference.render(settings, 0).rgba);
}

TEST(EasymetricModel, RejectsExternalAndTruncatedAssets)
{
  EXPECT_THROW(Model{ riggedTriangle(true) }, std::runtime_error);
  auto truncated = riggedTriangle();
  truncated.pop_back();
  EXPECT_THROW(Model{ std::move(truncated) }, std::runtime_error);
}

TEST(EasymetricModel, EmbeddedModelSurvivesAsepriteSaveAndUndo)
{
  const Model model(riggedTriangle());
  Settings settings;
  settings.lights.push_back(Light{});
  settings.lights.back().point = true;
  settings.rotation = {
    { 0,   {}           },
    { 700, { 0, 30, 0 } }
  };
  settings.clip = 0;
  model.fit(settings, false);

  app::Context context;
  struct File {
    std::string path = base::join_path(
      base::get_temp_path(),
      "easymetric-model-" + base::convert_to<std::string>(base::Uuid::Generate()) + ".aseprite");
    ~File() { std::remove(path.c_str()); }
  } file;
  {
    std::unique_ptr<app::Doc> doc(context.documents().add(64, 64, doc::ColorMode::RGB, 256));
    doc->setFilename(file.path);
    {
      app::Tx tx(doc.get(), "Import GLB");
      setProjectAsset(tx, *doc->sprite(), encodeAsset("triangle.glb", model));
      setProjectSettings(tx, *doc->sprite(), encodeSettings(settings));
      tx.commit();
    }
    doc->undoHistory()->undo();
    EXPECT_FALSE(hasEmbeddedModel(*doc->sprite()));
    doc->undoHistory()->redo();
    EXPECT_TRUE(hasEmbeddedModel(*doc->sprite()));
    app::save_document(&context, doc.get());
    doc->close();
  }

  std::unique_ptr<app::Doc> reopened(app::load_document(&context, file.path));
  ASSERT_TRUE(reopened);
  const auto* asset = embeddedModel(*reopened->sprite());
  ASSERT_TRUE(asset);
  EXPECT_EQ(assetName(*asset), "triangle.glb");
  EXPECT_EQ(decodeAsset(*asset), model.bytes());
  const Model restored(decodeAsset(*asset));
  const auto restoredSettings = decodeSettings(savedSettings(*reopened->sprite()));
  EXPECT_EQ(restored.render(restoredSettings, 500).rgba, model.render(settings, 500).rgba);
  reopened->close();
}
