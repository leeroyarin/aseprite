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
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <png.h>
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

uint8_t pixelRed(const Image& image, int x, int y)
{
  return image.rgba[(std::size_t(y) * image.width + x) * 4];
}

uint8_t pixelAlpha(const Image& image, int x, int y)
{
  return image.rgba[(std::size_t(y) * image.width + x) * 4 + 3];
}

std::vector<uint8_t> shadowTestScene()
{
  // Two quads facing +Z:
  // 0..3: Receiver quad at z = 0, x in [-1, 1], y in [-1, 1]
  // 4..7: Occluder quad at z = 0.5, x in [0.1, 0.5], y in [-0.4, 0.4]
  std::vector<uint8_t> data;
  floats(data,
         { -1.0f, -1.0f, 0.0f, 1.0f, -1.0f, 0.0f, 1.0f, 1.0f, 0.0f, -1.0f, 1.0f, 0.0f,
           0.1f,  -0.4f, 0.5f, 0.5f, -0.4f, 0.5f, 0.5f, 0.4f, 0.5f, 0.1f,  0.4f, 0.5f });
  floats(data,
         { 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1,
           0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1 });
  const std::vector<uint32_t> indices = { 0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7 };
  for (uint32_t idx : indices)
    word(data, idx);
  return glb(
    R"({"asset":{"version":"2.0"},"scenes":[{"nodes":[0]}],"nodes":[{"mesh":0}],
        "meshes":[{"primitives":[{"attributes":{"POSITION":0,"NORMAL":1},"indices":2}]}],
        "buffers":[{"byteLength":240}],
        "bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":96},
                       {"buffer":0,"byteOffset":96,"byteLength":96},
                       {"buffer":0,"byteOffset":192,"byteLength":48}],
        "accessors":[{"bufferView":0,"componentType":5126,"count":8,"type":"VEC3",
                      "min":[-1,-1,0],"max":[1,1,0.5]},
                     {"bufferView":1,"componentType":5126,"count":8,"type":"VEC3"},
                     {"bufferView":2,"componentType":5125,"count":12,"type":"SCALAR"}]})",
    std::move(data));
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

TEST(EasymetricModel, SelfShadowingRespondsToLightingAndShadowStrength)
{
  const Model model(shadowTestScene());
  Settings settings;
  settings.yaw = 0;
  settings.pitch = 0;
  settings.ambient = 0.1f;
  settings.groundShadow = false;
  settings.lights[0].point = false;
  settings.lights[0].azimuth = 45;
  settings.lights[0].elevation = 0;
  settings.lights[0].intensity = 0.8f;
  model.fit(settings, false);

  // In posed space: origin=(0,0,0.25), extent=sqrt(8.25)≈2.8723.
  // Receiver z=-0.0870, x in [-0.348, 0.348]; occluder z=+0.0870, x in [0.0348, 0.1741].
  // With azimuth 45, shadow falls at x in [-0.1393, 0.0]; with azimuth -45, at [0.2089, 0.348].
  const auto shadowPoint = project(settings, { -0.07f, 0.0f, -0.087f });
  const auto litPoint = project(settings, { 0.26f, 0.0f, -0.087f });
  const int sx = int(shadowPoint.x), sy = int(shadowPoint.y);
  const int lx = int(litPoint.x), ly = int(litPoint.y);

  settings.shadowStrength = 1.0f;
  const auto fullShadow = model.render(settings, 0);
  EXPECT_LT(pixelRed(fullShadow, sx, sy), pixelRed(fullShadow, lx, ly));

  settings.shadowStrength = 0.5f;
  const auto halfShadow = model.render(settings, 0);
  EXPECT_GT(pixelRed(halfShadow, sx, sy), pixelRed(fullShadow, sx, sy));

  settings.shadowStrength = 0.0f;
  const auto noShadow = model.render(settings, 0);
  EXPECT_GT(pixelRed(noShadow, sx, sy), pixelRed(halfShadow, sx, sy));
  EXPECT_EQ(pixelRed(noShadow, sx, sy), pixelRed(noShadow, lx, ly));

  // Flip directional light to -X,+Z (azimuth -45): shadow moves to the right side (+X).
  settings.shadowStrength = 1.0f;
  settings.lights[0].azimuth = -45;
  const auto flipped = model.render(settings, 0);
  EXPECT_GT(pixelRed(flipped, sx, sy), pixelRed(flipped, lx, ly));

  // Point light from +X,+Z also casts a directional shadow onto the left side.
  settings.lights[0].point = true;
  settings.lights[0].position = { 0.3f, 0.0f, 0.25f };
  settings.lights[0].range = 5.0f;
  const auto pointShadow = model.render(settings, 0);
  EXPECT_LT(pixelRed(pointShadow, sx, sy), pixelRed(pointShadow, lx, ly));
}

TEST(EasymetricModel, GroundShadowCastsBelowModelAndRespondsToStrengthAndLight)
{
  const Model model(shadowTestScene());
  Settings settings;
  settings.yaw = 0;
  settings.pitch = 30;
  settings.ambient = 0.2f;
  settings.groundShadow = true;
  settings.shadowStrength = 1.0f;
  settings.lights[0].point = false;
  settings.lights[0].azimuth = 30;
  settings.lights[0].elevation = 55;
  model.fit(settings, false);
  settings.framing *= 1.5f;

  // Count semi-transparent ground shadow pixels (0 < alpha < 255).
  const auto shadowStats = [](const Image& img) {
    int count = 0;
    int maxAlpha = 0;
    for (int y = 0; y < img.height; ++y) {
      for (int x = 0; x < img.width; ++x) {
        const uint8_t a = pixelAlpha(img, x, y);
        if (a > 0 && a < 255) {
          ++count;
          maxAlpha = std::max(maxAlpha, int(a));
        }
      }
    }
    return std::pair<int, int>{ count, maxAlpha };
  };

  const auto fullGround = model.render(settings, 0);
  const auto [fullCount, fullMaxAlpha] = shadowStats(fullGround);
  EXPECT_GT(fullCount, 0);
  EXPECT_GT(fullMaxAlpha, 0);

  settings.shadowStrength = 0.4f;
  const auto softGround = model.render(settings, 0);
  const auto [softCount, softMaxAlpha] = shadowStats(softGround);
  EXPECT_GT(softCount, 0);
  EXPECT_LT(softMaxAlpha, fullMaxAlpha);

  settings.shadowStrength = 0.0f;
  EXPECT_EQ(shadowStats(model.render(settings, 0)).first, 0);

  settings.shadowStrength = 1.0f;
  settings.groundShadow = false;
  EXPECT_EQ(shadowStats(model.render(settings, 0)).first, 0);

  // Light below the ground plane must not darken the floor.
  settings.groundShadow = true;
  settings.lights[0].elevation = -25;
  EXPECT_EQ(shadowStats(model.render(settings, 0)).first, 0);
}

TEST(EasymetricModel, ShadowSettingsValidateAndDecodeLegacyProjects)
{
  const Model model(riggedTriangle());
  Settings settings;
  settings.shadowStrength = -0.1f;
  EXPECT_THROW(model.validate(settings), ModelIssue);
  settings.shadowStrength = 1.1f;
  EXPECT_THROW(model.validate(settings), ModelIssue);
  settings.shadowStrength = std::numeric_limits<float>::quiet_NaN();
  EXPECT_THROW(model.validate(settings), ModelIssue);

  settings.shadowStrength = 0.42f;
  settings.groundShadow = false;
  const auto encoded = encodeSettings(settings);
  const auto decoded = decodeSettings(encoded);
  EXPECT_NEAR(decoded.shadowStrength, 0.42f, 1e-5f);
  EXPECT_FALSE(decoded.groundShadow);

  // Older saved projects omit shadowStrength and groundShadow.
  const std::string legacyJson =
    R"({"version":1,"orientation":[0,0,0],"heading":0,"modelScale":1,"basePivot":false,
        "yaw":45,"pitch":30,"target":[0,0,0],"framing":1,"width":64,"height":64,
        "ambient":0.35,"lights":[],"clip":-1,"start":0,"end":1000,"fps":12,
        "speed":1,"loop":true,"turntable":false,"position":[],"rotation":[],"scale":[]})";
  const auto legacyDecoded = decodeSettings(legacyJson);
  EXPECT_FLOAT_EQ(legacyDecoded.shadowStrength, 1.0f);
  EXPECT_TRUE(legacyDecoded.groundShadow);
}

TEST(EasymetricModel, MaskedTexturesCastShadowsOnlyThroughOpaqueTexels)
{
  // Build a 2x1 RGBA PNG: left texel transparent (0), right texel opaque (255).
  uint8_t pixels[8] = { 255, 255, 255, 0, 255, 255, 255, 255 };
  std::vector<uint8_t> pngBytes;
  png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
  ASSERT_TRUE(png != nullptr);
  png_infop info = png_create_info_struct(png);
  ASSERT_TRUE(info != nullptr);
  png_set_write_fn(
    png,
    &pngBytes,
    [](png_structp p, png_bytep data, png_size_t len) {
      auto* out = static_cast<std::vector<uint8_t>*>(png_get_io_ptr(p));
      out->insert(out->end(), data, data + len);
    },
    nullptr);
  png_set_IHDR(png, info, 2, 1, 8, PNG_COLOR_TYPE_RGBA, PNG_INTERLACE_NONE,
               PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
  png_write_info(png, info);
  png_bytep rows[1] = { pixels };
  png_write_image(png, rows);
  png_write_end(png, nullptr);
  png_destroy_write_struct(&png, &info);

  // Two primitives:
  // 0: Opaque receiver quad at z=0, x in [-1, 1], y in [-1, 1]
  // 1: Masked occluder quad at z=0.5, x in [-0.2, 0.6], y in [-0.4, 0.4]
  //    Left half (u in [0, 0.5)) is transparent; right half (u in [0.5, 1]) is opaque.
  std::vector<uint8_t> data;
  floats(data,
         { -1.0f, -1.0f, 0.0f, 1.0f, -1.0f, 0.0f, 1.0f, 1.0f, 0.0f, -1.0f, 1.0f, 0.0f,
           -0.2f, -0.4f, 0.5f, 0.6f, -0.4f, 0.5f, 0.6f, 0.4f, 0.5f, -0.2f, 0.4f, 0.5f }); // 96 bytes
  floats(data,
         { 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1,
           0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1 }); // 96 bytes (offset 96)
  floats(data,
         { 0, 0, 1, 0, 1, 1, 0, 1,
           0, 0, 1, 0, 1, 1, 0, 1 }); // 64 bytes (offset 192)
  for (uint32_t idx : { 0u, 1u, 2u, 0u, 2u, 3u, 4u, 5u, 6u, 4u, 6u, 7u })
    word(data, idx); // 48 bytes (offset 256)
  const std::size_t pngOffset = data.size(); // 304
  data.insert(data.end(), pngBytes.begin(), pngBytes.end());
  const std::size_t bufferBytes = data.size();

  const Model model(glb(
    R"({"asset":{"version":"2.0"},"scenes":[{"nodes":[0]}],"nodes":[{"mesh":0}],
        "images":[{"bufferView":4,"mimeType":"image/png"}],
        "textures":[{"source":0,"sampler":0}],
        "samplers":[{"magFilter":9728,"minFilter":9728,"wrapS":33071,"wrapT":33071}],
        "materials":[
          {"pbrMetallicRoughness":{"baseColorFactor":[1,1,1,1]}},
          {"alphaMode":"MASK","alphaCutoff":0.5,"pbrMetallicRoughness":{"baseColorTexture":{"index":0}}}
        ],
        "meshes":[{"primitives":[
          {"attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2},"indices":3,"material":0},
          {"attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2},"indices":4,"material":1}
        ]}],
        "buffers":[{"byteLength":)" + std::to_string(bufferBytes) + R"(}],
        "bufferViews":[
          {"buffer":0,"byteOffset":0,"byteLength":96},
          {"buffer":0,"byteOffset":96,"byteLength":96},
          {"buffer":0,"byteOffset":192,"byteLength":64},
          {"buffer":0,"byteOffset":256,"byteLength":48},
          {"buffer":0,"byteOffset":)" + std::to_string(pngOffset) + R"(,"byteLength":)" + std::to_string(pngBytes.size()) + R"(}
        ],
        "accessors":[
          {"bufferView":0,"componentType":5126,"count":8,"type":"VEC3","min":[-1,-1,0],"max":[1,1,0.5]},
          {"bufferView":1,"componentType":5126,"count":8,"type":"VEC3"},
          {"bufferView":2,"componentType":5126,"count":8,"type":"VEC2"},
          {"bufferView":3,"componentType":5125,"count":6,"type":"SCALAR"},
          {"bufferView":3,"byteOffset":24,"componentType":5125,"count":6,"type":"SCALAR"}
        ]})",
    std::move(data)));

  // In posed space: origin=(0,0,0.25), extent=sqrt(8.25)≈2.8723.
  // Occluder x in [-0.0696, 0.2089], z=+0.0870:
  //   transparent half x in [-0.0696, 0.0696), opaque half x in [0.0696, 0.2089].
  // With azimuth 45 (dx = dz = 0.1741), shadow shifts -0.1741 in X:
  //   transparent region hits receiver x in [-0.2437, -0.1045) -> NO shadow!
  //   opaque region hits receiver x in [-0.1045, 0.0348] -> SHADOW!
  Settings settings;
  settings.yaw = 0;
  settings.pitch = 0;
  settings.ambient = 0.1f;
  settings.groundShadow = false;
  settings.shadowStrength = 1.0f;
  settings.lights[0].point = false;
  settings.lights[0].azimuth = 45;
  settings.lights[0].elevation = 0;
  model.fit(settings, false);

  const auto underTransparent = project(settings, { -0.17f, 0.0f, -0.087f });
  const auto underOpaque = project(settings, { -0.03f, 0.0f, -0.087f });
  const auto img = model.render(settings, 0);
  EXPECT_GT(pixelRed(img, int(underTransparent.x), int(underTransparent.y)),
            pixelRed(img, int(underOpaque.x), int(underOpaque.y)));
}

TEST(EasymetricModel, SmoothConvexMeshHasNoShadowTerminatorAcne)
{
  const auto makeSphere = [](bool doubleSided) {
    std::vector<float> pos, norm;
    std::vector<uint32_t> idx;
    const int stacks = 8, slices = 12;
    const float pi = 3.14159265358979323846f;
    for (int i = 0; i <= stacks; ++i) {
      const float phi = pi * i / stacks;
      for (int j = 0; j <= slices; ++j) {
        const float theta = 2.0f * pi * j / slices;
        const float x = std::sin(phi) * std::cos(theta);
        const float y = std::cos(phi);
        const float z = std::sin(phi) * std::sin(theta);
        pos.insert(pos.end(), { x, y, z });
        norm.insert(norm.end(), { x, y, z });
      }
    }
    for (int i = 0; i < stacks; ++i) {
      for (int j = 0; j < slices; ++j) {
        const uint32_t a = i * (slices + 1) + j;
        const uint32_t b = (i + 1) * (slices + 1) + j;
        const uint32_t c = b + 1;
        const uint32_t d = a + 1;
        if (i != stacks - 1)
          idx.insert(idx.end(), { a, c, b });
        if (i != 0)
          idx.insert(idx.end(), { a, d, c });
      }
    }
    std::vector<uint8_t> bin;
    for (float f : pos)
      floats(bin, { f });
    const std::size_t normOff = bin.size();
    for (float f : norm)
      floats(bin, { f });
    const std::size_t idxOff = bin.size();
    for (uint32_t id : idx)
      word(bin, id);
    const std::size_t vCount = pos.size() / 3;
    const std::string mat = doubleSided ? R"(,"materials":[{"doubleSided":true}])" : "";
    const std::string matRef = doubleSided ? R"(,"material":0)" : "";
    std::string json =
      R"({"asset":{"version":"2.0"},"scenes":[{"nodes":[0]}],"nodes":[{"mesh":0}])" + mat +
      R"(,"meshes":[{"primitives":[{"attributes":{"POSITION":0,"NORMAL":1},"indices":2)" +
      matRef + R"(}]}],
        "buffers":[{"byteLength":)" +
      std::to_string(bin.size()) + R"(}],
        "bufferViews":[
          {"buffer":0,"byteOffset":0,"byteLength":)" +
      std::to_string(normOff) + R"(},
          {"buffer":0,"byteOffset":)" +
      std::to_string(normOff) + R"(,"byteLength":)" + std::to_string(idxOff - normOff) + R"(},
          {"buffer":0,"byteOffset":)" +
      std::to_string(idxOff) + R"(,"byteLength":)" + std::to_string(bin.size() - idxOff) + R"(}
        ],
        "accessors":[
          {"bufferView":0,"componentType":5126,"count":)" +
      std::to_string(vCount) + R"(,"type":"VEC3","min":[-1,-1,-1],"max":[1,1,1]},
          {"bufferView":1,"componentType":5126,"count":)" +
      std::to_string(vCount) + R"(,"type":"VEC3"},
          {"bufferView":2,"componentType":5125,"count":)" +
      std::to_string(idx.size()) + R"(,"type":"SCALAR"}
        ]})";
    return glb(std::move(json), std::move(bin));
  };

  for (bool doubleSided : { false, true }) {
    const Model model(makeSphere(doubleSided));
    Settings settings;
    settings.width = settings.height = 96;
    settings.yaw = 0;
    settings.pitch = 20;
    settings.ambient = 0.0f;
    settings.groundShadow = true;
    settings.lights[0].azimuth = 65;
    settings.lights[0].elevation = 25;
    settings.lights[0].intensity = 1.0f;
    model.fit(settings, false);
    settings.framing *= 1.5f;

    settings.shadowStrength = 0.0f;
    const auto noShadow = model.render(settings, 0);
    settings.shadowStrength = 1.0f;
    const auto withShadow = model.render(settings, 0);

    int groundPx = 0;
    for (int i = 0; i < settings.width * settings.height; ++i) {
      if (noShadow.rgba[i * 4 + 3] == 255) {
        EXPECT_EQ(withShadow.rgba[i * 4], noShadow.rgba[i * 4]);
      }
      else if (withShadow.rgba[i * 4 + 3] > 0) {
        ++groundPx;
      }
    }
    EXPECT_GT(groundPx, 0);
  }
}

TEST(EasymetricModel, SmallScaleAndFlippedMeshesCastShadows)
{
  // Small subdivided occluder at tiny combined scale (0.01 * 0.01 = 1e-4).
  std::vector<float> pos, norm;
  std::vector<uint32_t> idx;
  for (float p : { -1.f, -1.f, 0.f, 1.f, -1.f, 0.f, 1.f, 1.f, 0.f, -1.f, 1.f, 0.f })
    pos.push_back(p);
  for (int i = 0; i < 4; ++i)
    norm.insert(norm.end(), { 0.f, 0.f, 1.f });
  idx.insert(idx.end(), { 0u, 1u, 2u, 0u, 2u, 3u });

  const int n = 80;
  const uint32_t base = 4;
  for (int iy = 0; iy <= n; ++iy) {
    const float y = -0.4f + 0.8f * iy / n;
    for (int ix = 0; ix <= n; ++ix) {
      const float x = 0.1f + 0.4f * ix / n;
      pos.insert(pos.end(), { x, y, 0.5f });
      norm.insert(norm.end(), { 0.f, 0.f, 1.f });
    }
  }
  for (int iy = 0; iy < n; ++iy) {
    for (int ix = 0; ix < n; ++ix) {
      const uint32_t a = base + iy * (n + 1) + ix;
      const uint32_t b = a + 1;
      const uint32_t c = a + (n + 1) + 1;
      const uint32_t d = a + (n + 1);
      idx.insert(idx.end(), { a, b, c, a, c, d });
    }
  }
  std::vector<uint8_t> bin;
  for (float f : pos)
    floats(bin, { f });
  const std::size_t normOff = bin.size();
  for (float f : norm)
    floats(bin, { f });
  const std::size_t idxOff = bin.size();
  for (uint32_t id : idx)
    word(bin, id);
  const std::size_t vCount = pos.size() / 3;
  std::string json =
    R"({"asset":{"version":"2.0"},"scenes":[{"nodes":[0]}],"nodes":[{"mesh":0}],
        "meshes":[{"primitives":[{"attributes":{"POSITION":0,"NORMAL":1},"indices":2}]}],
        "buffers":[{"byteLength":)" +
    std::to_string(bin.size()) + R"(}],
        "bufferViews":[
          {"buffer":0,"byteOffset":0,"byteLength":)" +
    std::to_string(normOff) + R"(},
          {"buffer":0,"byteOffset":)" +
    std::to_string(normOff) + R"(,"byteLength":)" + std::to_string(idxOff - normOff) + R"(},
          {"buffer":0,"byteOffset":)" +
    std::to_string(idxOff) + R"(,"byteLength":)" + std::to_string(bin.size() - idxOff) + R"(}
        ],
        "accessors":[
          {"bufferView":0,"componentType":5126,"count":)" +
    std::to_string(vCount) + R"(,"type":"VEC3","min":[-1,-1,0],"max":[1,1,0.5]},
          {"bufferView":1,"componentType":5126,"count":)" +
    std::to_string(vCount) + R"(,"type":"VEC3"},
          {"bufferView":2,"componentType":5125,"count":)" +
    std::to_string(idx.size()) + R"(,"type":"SCALAR"}
        ]})";
  const Model tiny(glb(std::move(json), std::move(bin)));

  Settings settings;
  settings.yaw = 0;
  settings.pitch = 0;
  settings.ambient = 0.1f;
  settings.groundShadow = false;
  settings.modelScale = 0.01f;
  settings.lights[0].azimuth = 45;
  settings.lights[0].elevation = 0;
  tiny.fit(settings, false);
  settings.scale = {
    { 0, { 0.01f, 0, 0 } }
  };
  settings.framing *= 0.01f;

  const auto sh = project(settings, { -0.000007f, 0.0f, -0.0000087f });
  const auto lit = project(settings, { 0.000026f, 0.0f, -0.0000087f });
  const auto img = tiny.render(settings, 0);
  EXPECT_LT(pixelRed(img, int(sh.x), int(sh.y)), pixelRed(img, int(lit.x), int(lit.y)));

  // Negative-scale node (flipped = true) still self-shadows correctly.
  std::vector<uint8_t> fdata;
  floats(fdata,
         { -1.0f, -1.0f, 0.0f, 1.0f, -1.0f, 0.0f, 1.0f, 1.0f, 0.0f, -1.0f, 1.0f, 0.0f,
           0.1f,  -0.4f, 0.5f, 0.5f, -0.4f, 0.5f, 0.5f, 0.4f, 0.5f, 0.1f,  0.4f, 0.5f });
  floats(fdata,
         { 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1,
           0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1 });
  for (uint32_t id : { 0u, 1u, 2u, 0u, 2u, 3u, 4u, 5u, 6u, 4u, 6u, 7u })
    word(fdata, id);
  const Model flipped(glb(
    R"({"asset":{"version":"2.0"},"scenes":[{"nodes":[0]}],"nodes":[{"mesh":0,"scale":[-1,1,1]}],
        "meshes":[{"primitives":[{"attributes":{"POSITION":0,"NORMAL":1},"indices":2}]}],
        "buffers":[{"byteLength":240}],
        "bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":96},
                       {"buffer":0,"byteOffset":96,"byteLength":96},
                       {"buffer":0,"byteOffset":192,"byteLength":48}],
        "accessors":[{"bufferView":0,"componentType":5126,"count":8,"type":"VEC3",
                      "min":[-1,-1,0],"max":[1,1,0.5]},
                     {"bufferView":1,"componentType":5126,"count":8,"type":"VEC3"},
                     {"bufferView":2,"componentType":5125,"count":12,"type":"SCALAR"}]})",
    std::move(fdata)));

  Settings fset;
  fset.yaw = 0;
  fset.pitch = 0;
  fset.ambient = 0.1f;
  fset.groundShadow = false;
  fset.lights[0].azimuth = -45;
  fset.lights[0].elevation = 0;
  flipped.fit(fset, false);
  const auto fsh = project(fset, { 0.07f, 0.0f, -0.087f });
  const auto flit = project(fset, { -0.26f, 0.0f, -0.087f });
  const auto fimg = flipped.render(fset, 0);
  EXPECT_LT(pixelRed(fimg, int(fsh.x), int(fsh.y)), pixelRed(fimg, int(flit.x), int(flit.y)));
}

TEST(EasymetricModel, ExportFramesResamplesFullAnimationAndPersistsSettings)
{
  Settings s;
  s.start = 0;
  s.end = 8000;
  s.fps = 12;
  s.exportFrames = 12;
  s.loop = true;
  const auto loopSeq = samples(s);
  ASSERT_EQ(loopSeq.size(), 12u);
  EXPECT_DOUBLE_EQ(loopSeq.front().time, 0.0);
  EXPECT_NEAR(loopSeq.back().time, 8000.0 * 11.0 / 12.0, 1e-6);
  int loopDuration = 0;
  for (const auto& f : loopSeq) {
    EXPECT_GT(f.duration, 0);
    loopDuration += f.duration;
  }
  EXPECT_EQ(loopDuration, 8000);

  s.end = 1000;
  s.exportFrames = 5;
  s.loop = false;
  const auto onceSeq = samples(s);
  ASSERT_EQ(onceSeq.size(), 5u);
  EXPECT_DOUBLE_EQ(onceSeq.front().time, 0.0);
  EXPECT_DOUBLE_EQ(onceSeq.back().time, 1000.0);

  const Model model(riggedTriangle());
  model.validate(s);
  s.fitSequenceOnBake = false;
  const auto decoded = decodeSettings(encodeSettings(s));
  EXPECT_EQ(decoded.exportFrames, 5);
  EXPECT_FALSE(decoded.fitSequenceOnBake);

  const auto legacy = decodeSettings(
    R"({"version":1,"orientation":[0,0,0],"heading":0,"modelScale":1,"basePivot":false,
        "yaw":45,"pitch":30,"target":[0,0,0],"framing":1,"width":64,"height":64,
        "ambient":0.35,"lights":[],"clip":-1,"start":0,"end":1000,"fps":12,
        "speed":1,"loop":true,"turntable":false,"position":[],"rotation":[],"scale":[]})");
  EXPECT_EQ(legacy.exportFrames, 0);
  EXPECT_TRUE(legacy.fitSequenceOnBake);

  s.exportFrames = -1;
  EXPECT_THROW(model.validate(s), std::runtime_error);
  s.exportFrames = 257;
  EXPECT_THROW(model.validate(s), std::runtime_error);
}

TEST(EasymetricModel, FitIfClippedPreventsSpatialAnimationClipping)
{
  const Model model(riggedTriangle());
  Settings s;
  s.yaw = s.pitch = 0;
  s.groundShadow = false;
  s.exportFrames = 4;
  s.loop = false;
  model.fit(s, false);

  // Move horizontally well outside the rest-pose camera frame during the animation.
  s.position = {
    { 0,    { 0, 0, 0 }    },
    { 1000, { 2.5f, 0, 0 } }
  };

  EXPECT_TRUE(model.fitIfClipped(s));
  const float fittedFraming = s.framing;
  // Already fits cleanly -> second call does not alter camera framing.
  EXPECT_FALSE(model.fitIfClipped(s));
  EXPECT_FLOAT_EQ(s.framing, fittedFraming);

  const auto lastOpaqueX = [](const Image& img) {
    int last = -1;
    for (int y = 0; y < img.height; ++y)
      for (int x = 0; x < img.width; ++x)
        if (img.rgba[(std::size_t(y) * img.width + x) * 4 + 3])
          last = std::max(last, x);
    return last;
  };
  for (const auto& f : samples(s)) {
    const auto img = model.render(s, f.time);
    EXPECT_GT(firstOpaqueX(img), 0);
    EXPECT_LT(lastOpaqueX(img), img.width - 1);
  }
}

TEST(EasymetricModel, ConfigFileEncodesAndAppliesScaleAndLightingAcrossModels)
{
  Settings source;
  source.modelScale = 0.65f;
  source.orientation = { 0, 90, 0 };
  source.heading = 45.0f;
  source.basePivot = true;
  source.yaw = 135.0f;
  source.pitch = 22.0f;
  source.framing = 1.75f;
  source.ambient = 0.55f;
  source.shadowStrength = 0.72f;
  source.groundShadow = false;
  source.lights.push_back(Light{});
  source.lights.back().point = true;
  source.lights.back().position = { 0.5f, 1.2f, -0.3f };
  source.lights.back().intensity = 1.4f;
  source.width = 48;
  source.height = 48;
  source.exportFrames = 16;
  source.fitSequenceOnBake = false;
  source.clip = 2;
  source.start = 250;
  source.end = 4500;

  const std::string cfg = encodeConfig(source);
  EXPECT_NE(cfg.find("\"modelScale\""), std::string::npos);
  EXPECT_NE(cfg.find("\"lighting\""), std::string::npos);
  EXPECT_EQ(cfg.find("\"clip\""), std::string::npos);

  Settings target;
  target.clip = 0;
  target.start = 0;
  target.end = 1000;
  target.rotation = { { 0, { 0, 0, 0 } }, { 500, { 0, 45, 0 } } };

  const Settings applied = applyConfig(target, cfg);
  EXPECT_FLOAT_EQ(applied.modelScale, 0.65f);
  EXPECT_FLOAT_EQ(applied.orientation.y, 90.0f);
  EXPECT_FLOAT_EQ(applied.heading, 45.0f);
  EXPECT_TRUE(applied.basePivot);
  EXPECT_FLOAT_EQ(applied.yaw, 135.0f);
  EXPECT_FLOAT_EQ(applied.framing, 1.75f);
  EXPECT_FLOAT_EQ(applied.ambient, 0.55f);
  EXPECT_FLOAT_EQ(applied.shadowStrength, 0.72f);
  EXPECT_FALSE(applied.groundShadow);
  ASSERT_EQ(applied.lights.size(), 2u);
  EXPECT_TRUE(applied.lights[1].point);
  EXPECT_FLOAT_EQ(applied.lights[1].intensity, 1.4f);
  EXPECT_EQ(applied.width, 48);
  EXPECT_EQ(applied.exportFrames, 16);
  EXPECT_FALSE(applied.fitSequenceOnBake);
  // Per-model clip/timeline state on target must stay intact.
  EXPECT_EQ(applied.clip, 0);
  EXPECT_EQ(applied.end, 1000);
  EXPECT_EQ(applied.rotation.size(), 2u);

  // Partial copied config containing only model scale + lighting preserves target camera/output.
  const std::string partialJson = R"({
    "version": 1,
    "model": { "modelScale": 1.25 },
    "lighting": { "ambient": 0.2, "shadowStrength": 0.9 }
  })";
  Settings partialTarget;
  partialTarget.yaw = 10.0f;
  partialTarget.width = 96;
  const Settings partialApplied = applyConfig(partialTarget, partialJson);
  EXPECT_FLOAT_EQ(partialApplied.modelScale, 1.25f);
  EXPECT_FLOAT_EQ(partialApplied.ambient, 0.2f);
  EXPECT_FLOAT_EQ(partialApplied.shadowStrength, 0.9f);
  EXPECT_FLOAT_EQ(partialApplied.yaw, 10.0f);
  EXPECT_EQ(partialApplied.width, 96);

  EXPECT_THROW(applyConfig(target, "{}"), std::runtime_error);
  EXPECT_THROW(applyConfig(target, R"({"version":99,"model":{"modelScale":1}})"),
               std::runtime_error);
}
