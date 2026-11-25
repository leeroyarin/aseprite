// Aseprite 2.0
// Copyright (C) 2026 Aseprite 2.0 contributors
//
// This program is distributed under the terms of
// the End-User License Agreement for Aseprite.

#include "app/easymetric/model.h"

// cgltf 1.15, vendored verbatim under its MIT license.
#define CGLTF_IMPLEMENTATION
#include "app/easymetric/vendor/cgltf.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <csetjmp>
#include <cstdio>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <utility>

#include <jpeglib.h>
#include <png.h>

namespace app::easymetric {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr std::size_t kMaxTextureBytes = 128 * 1024 * 1024;
constexpr std::size_t kMaxTriangles = 500000;
constexpr int kMaxDuration = 600000;
using Mat = std::array<float, 16>;
using Quat = std::array<float, 4>;

void require(bool ok, const char* message)
{
  if (!ok)
    throw ModelIssue(message);
}

void checkpoint(const Canceled& canceled)
{
  if (canceled && canceled())
    throw ModelIssue("easymetric_model.issue_canceled");
}

float radians(float degrees)
{
  return degrees * kPi / 180;
}
Vec3 operator+(Vec3 a, Vec3 b)
{
  return { a.x + b.x, a.y + b.y, a.z + b.z };
}
Vec3 operator-(Vec3 a, Vec3 b)
{
  return { a.x - b.x, a.y - b.y, a.z - b.z };
}
Vec3 operator*(Vec3 a, float s)
{
  return { a.x * s, a.y * s, a.z * s };
}
Vec3& operator+=(Vec3& a, Vec3 b)
{
  return a = a + b;
}
float dot(Vec3 a, Vec3 b)
{
  return a.x * b.x + a.y * b.y + a.z * b.z;
}
Vec3 cross(Vec3 a, Vec3 b)
{
  return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
}
float length(Vec3 v)
{
  return std::sqrt(dot(v, v));
}
Vec3 unit(Vec3 v)
{
  const float n = length(v);
  return n > 1e-10f ? v * (1 / n) : Vec3{};
}
bool finite(Vec3 v)
{
  return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
Vec3 vec(const float* v)
{
  return { v[0], v[1], v[2] };
}

Mat multiply(const Mat& a, const Mat& b)
{
  Mat result{};
  for (int c = 0; c < 4; ++c)
    for (int r = 0; r < 4; ++r)
      for (int k = 0; k < 4; ++k)
        result[4 * c + r] += a[4 * k + r] * b[4 * c + k];
  return result;
}
Vec3 transform(const Mat& m, Vec3 v)
{
  return vec(m.data()) * v.x + vec(m.data() + 4) * v.y + vec(m.data() + 8) * v.z +
         vec(m.data() + 12);
}
Mat normals(const Mat& m)
{
  const Vec3 a = vec(m.data()), b = vec(m.data() + 4), c = vec(m.data() + 8);
  const Vec3 x = cross(b, c), y = cross(c, a), z = cross(a, b);
  const float det = dot(a, x);
  const float s = std::abs(det) > 1e-20f ? 1 / det : 0;
  return { x.x * s, x.y * s, x.z * s, 0, y.x * s, y.y * s, y.z * s, 0,
           z.x * s, z.y * s, z.z * s, 0, 0,       0,       0,       1 };
}
Quat normalized(Quat q)
{
  float n = 0;
  for (float x : q)
    n += x * x;
  if (!(n > 1e-20f) || !std::isfinite(n))
    return { 0, 0, 0, 1 };
  const float inv = 1 / std::sqrt(n);
  for (float& x : q)
    x *= inv;
  return q;
}
Quat slerp(Quat a, Quat b, float t)
{
  a = normalized(a);
  b = normalized(b);
  float cosAngle = 0;
  for (int i = 0; i < 4; ++i)
    cosAngle += a[i] * b[i];
  if (cosAngle < 0) {
    cosAngle = -cosAngle;
    for (float& x : b)
      x = -x;
  }
  float x = 1 - t, y = t;
  if (cosAngle < 0.9995f) {
    const float angle = std::acos(std::clamp(cosAngle, 0.0f, 1.0f));
    x = std::sin(x * angle) / std::sin(angle);
    y = std::sin(y * angle) / std::sin(angle);
  }
  for (int i = 0; i < 4; ++i)
    a[i] = x * a[i] + y * b[i];
  return normalized(a);
}
Quat euler(Vec3 v)
{
  const float x = radians(v.x) / 2, y = radians(v.y) / 2, z = radians(v.z) / 2;
  const float sx = std::sin(x), sy = std::sin(y), sz = std::sin(z);
  const float cx = std::cos(x), cy = std::cos(y), cz = std::cos(z);
  return { sx * cy * cz - cx * sy * sz,
           cx * sy * cz + sx * cy * sz,
           cx * cy * sz - sx * sy * cz,
           cx * cy * cz + sx * sy * sz };
}
Quat multiply(Quat a, Quat b)
{
  const auto axis = vec(a.data()) * b[3] + vec(b.data()) * a[3] +
                    cross(vec(a.data()), vec(b.data()));
  return { axis.x, axis.y, axis.z, a[3] * b[3] - dot(vec(a.data()), vec(b.data())) };
}
Vec3 rotate(Quat q, Vec3 v)
{
  const Vec3 axis = vec(q.data());
  const Vec3 t = cross(axis, v) * 2;
  return v + t * q[3] + cross(axis, t);
}

struct Axes {
  Vec3 right, up, front;
};

Axes camera(const Settings& s)
{
  const float yaw = radians(s.yaw), pitch = radians(s.pitch);
  const Vec3 front{ std::sin(yaw) * std::cos(pitch),
                    std::sin(pitch),
                    std::cos(yaw) * std::cos(pitch) };
  const Vec3 right{ std::cos(yaw), 0, -std::sin(yaw) };
  return { right, cross(front, right), front };
}

struct Bounds {
  Vec3 min, max;
  bool empty = true;
  void add(Vec3 p)
  {
    require(finite(p) && std::max({ std::abs(p.x), std::abs(p.y), std::abs(p.z) }) < 1e15f,
            "easymetric_model.issue_model_coordinates_are_out_of_range");
    if (empty) {
      min = max = p;
      empty = false;
    }
    else {
      min = { std::min(min.x, p.x), std::min(min.y, p.y), std::min(min.z, p.z) };
      max = { std::max(max.x, p.x), std::max(max.y, p.y), std::max(max.z, p.z) };
    }
  }
  Vec3 center() const { return (min + max) * 0.5f; }
};

struct Texture {
  Image image;
  int wrapS = 10497, wrapT = 10497;
};

struct Material {
  int texture = -1;
  std::array<float, 4> base{ 1, 1, 1, 1 };
  float cutoff = 0.5f;
  bool masked = false, doubleSided = false, unlit = false;
  Vec3 offset, scale{ 1, 1, 1 };
  float rotation = 0;
};

struct Vertex {
  Vec3 p, n, uv;
  std::array<int, 4> joints{};
  std::array<float, 4> weights{};
};

struct Primitive {
  std::vector<Vertex> vertices;
  std::vector<uint32_t> indices;
  Material material;
  bool skinned = false;
};

struct Instance {
  int node = -1;
  std::size_t primitive = 0;
};

struct Channel {
  int node;
  cgltf_animation_path_type path;
  cgltf_interpolation_type interpolation;
  std::vector<float> times, values;
};

struct JpegError {
  jpeg_error_mgr manager;
  std::jmp_buf jump;
};

void jpegError(j_common_ptr jpeg)
{
  auto* error = reinterpret_cast<JpegError*>(jpeg->err);
  std::longjmp(error->jump, 1);
}

void validateTextureSize(unsigned w, unsigned h, std::size_t budget)
{
  require(w && h && w <= 4096 && h <= 4096,
          "easymetric_model.issue_an_embedded_texture_exceeds_4096_pixels_per_si");
  require(std::size_t(w) * h * 4 <= budget,
          "easymetric_model.issue_decoded_textures_exceed_128_mib");
}

Image decodeTexture(const uint8_t* bytes, std::size_t size, std::size_t budget)
{
  Image result;
  if (size >= 8 && png_sig_cmp(bytes, 0, 8) == 0) {
    png_image png{};
    png.version = PNG_IMAGE_VERSION;
    const auto cleanup = [&png](png_image*) { png_image_free(&png); };
    std::unique_ptr<png_image, decltype(cleanup)> guard(&png, cleanup);
    require(png_image_begin_read_from_memory(&png, bytes, size),
            "easymetric_model.issue_invalid_embedded_png");
    validateTextureSize(png.width, png.height, budget);
    png.format = PNG_FORMAT_RGBA;
    result.width = int(png.width);
    result.height = int(png.height);
    result.rgba.resize(PNG_IMAGE_SIZE(png));
    require(png_image_finish_read(&png, nullptr, result.rgba.data(), 0, nullptr),
            "easymetric_model.issue_invalid_embedded_png_pixels");
    return result;
  }

  require(size >= 3 && bytes[0] == 0xff && bytes[1] == 0xd8 && bytes[2] == 0xff,
          "easymetric_model.issue_embedded_textures_must_be_png_or_jpeg");
  // libjpeg reports failures with longjmp. Keep the decoder and buffers outside
  // the modified automatic variables of the setjmp scope.
  auto jpeg = std::make_unique<jpeg_decompress_struct>();
  auto error = std::make_unique<JpegError>();
  auto decoded = std::make_unique<Image>();
  auto row = std::make_unique<std::vector<uint8_t>>();
  jpeg->err = jpeg_std_error(&error->manager);
  error->manager.error_exit = jpegError;
  const auto cleanup = [](jpeg_decompress_struct* p) { jpeg_destroy_decompress(p); };
  std::unique_ptr<jpeg_decompress_struct, decltype(cleanup)> guard(jpeg.get(), cleanup);
  if (setjmp(error->jump))
    throw ModelIssue("easymetric_model.issue_invalid_embedded_jpeg");
  jpeg_create_decompress(jpeg.get());
  jpeg_mem_src(jpeg.get(), bytes, static_cast<unsigned long>(size));
  jpeg_read_header(jpeg.get(), TRUE);
  validateTextureSize(jpeg->image_width, jpeg->image_height, budget);
  jpeg->out_color_space = JCS_RGB;
  jpeg_start_decompress(jpeg.get());
  decoded->width = int(jpeg->output_width);
  decoded->height = int(jpeg->output_height);
  decoded->rgba.resize(std::size_t(decoded->width) * decoded->height * 4);
  row->resize(std::size_t(decoded->width) * 3);
  for (int y = 0; y < decoded->height; ++y) {
    JSAMPROW src = row->data();
    require(jpeg_read_scanlines(jpeg.get(), &src, 1) == 1,
            "easymetric_model.issue_invalid_embedded_jpeg_pixels");
    for (int x = 0; x < decoded->width; ++x) {
      auto* dest = decoded->rgba.data() + (std::size_t(y) * decoded->width + x) * 4;
      std::copy_n(row->data() + 3 * x, 3, dest);
      dest[3] = 255;
    }
  }
  jpeg_finish_decompress(jpeg.get());
  return std::move(*decoded);
}

uint32_t readU32(const uint8_t* p)
{
  return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

void checkSpan(const cgltf_buffer_view* view,
               std::size_t offset,
               std::size_t count,
               std::size_t stride,
               std::size_t size,
               std::size_t alignment)
{
  require(view && size && alignment && stride >= size && offset <= view->size &&
            (view->offset + offset) % alignment == 0 && stride % alignment == 0,
          "easymetric_model.issue_invalid_glb_accessor");
  const auto available = view->size - offset;
  require(!count || (size <= available && count - 1 <= (available - size) / stride),
          "easymetric_model.issue_glb_accessor_exceeds_its_buffer_view");
}

std::vector<float> unpack(const cgltf_accessor* a,
                          std::size_t components,
                          std::size_t limit = kMaxTriangles * 3)
{
  require(a && cgltf_num_components(a->type) == components && a->count <= limit,
          "easymetric_model.issue_invalid_or_oversized_glb_accessor");
  std::vector<float> values(a->count * components);
  cgltf_accessor base = *a;
  base.is_sparse = false;
  require(cgltf_accessor_unpack_floats(&base, values.data(), values.size()) == values.size(),
          "easymetric_model.issue_could_not_read_glb_accessor");
  if (a->is_sparse) {
    const auto& sparse = a->sparse;
    cgltf_accessor indices{};
    indices.type = cgltf_type_scalar;
    indices.component_type = sparse.indices_component_type;
    indices.offset = sparse.indices_byte_offset;
    indices.buffer_view = sparse.indices_buffer_view;
    indices.count = sparse.count;
    indices.stride = cgltf_component_size(indices.component_type);

    // Sparse values are packed even when the base accessor is interleaved.
    // cgltf 1.15 uses the base stride when unpacking both in one call.
    cgltf_accessor overrides = base;
    overrides.offset = sparse.values_byte_offset;
    overrides.buffer_view = sparse.values_buffer_view;
    overrides.count = sparse.count;
    overrides.stride = cgltf_calc_size(a->type, a->component_type);
    std::size_t previous = 0;
    for (std::size_t i = 0; i < sparse.count; ++i) {
      const auto index = cgltf_accessor_read_index(&indices, i);
      require(
        index < a->count && (i == 0 || index > previous) &&
          cgltf_accessor_read_float(&overrides, i, values.data() + index * components, components),
        "easymetric_model.issue_invalid_sparse_glb_indices");
      previous = index;
    }
  }
  for (float v : values)
    require(std::isfinite(v), "easymetric_model.issue_glb_accessor_contains_a_non_finite_value");
  return values;
}

const cgltf_accessor* attribute(const cgltf_primitive& p, cgltf_attribute_type type, int index = 0)
{
  for (std::size_t i = 0; i < p.attributes_count; ++i)
    if (p.attributes[i].type == type && p.attributes[i].index == index)
      return p.attributes[i].data;
  return nullptr;
}

template<class T>
int indexOf(const T* base, const T* item)
{
  return item ? int(item - base) : -1;
}

void addWarning(std::vector<std::string>& list, const char* message)
{
  if (std::find(list.begin(), list.end(), message) == list.end())
    list.emplace_back(message);
}

} // namespace

struct Model::Data {
  std::vector<uint8_t> bytes;
  std::unique_ptr<cgltf_data, decltype(&cgltf_free)> gltf{ nullptr, cgltf_free };
  std::vector<Clip> clips;
  std::vector<std::vector<Channel>> channels;
  std::vector<std::string> warnings;
  std::vector<Primitive> primitives;
  std::vector<Instance> instances;
  std::vector<Texture> textures;
  std::vector<int> nodeOrder;
  std::vector<std::vector<Mat>> bind;
  Bounds bounds;
};

namespace {

void validateBuffers(const cgltf_data& data)
{
  require(data.buffers_count <= 1,
          "easymetric_model.issue_glb_must_contain_one_self_contained_binary_buf");
  for (std::size_t i = 0; i < data.buffers_count; ++i)
    require(!data.buffers[i].uri && data.buffers[i].size <= data.bin_size &&
              data.bin_size - data.buffers[i].size <= 3,
            "easymetric_model.issue_glb_contains_an_external_or_truncated_buffer");
  for (std::size_t i = 0; i < data.images_count; ++i)
    require(!data.images[i].uri && data.images[i].buffer_view,
            "easymetric_model.issue_glb_contains_an_external_image");
  for (std::size_t i = 0; i < data.buffer_views_count; ++i) {
    const auto& view = data.buffer_views[i];
    require(view.buffer && view.offset <= view.buffer->size &&
              view.size <= view.buffer->size - view.offset && !view.has_meshopt_compression,
            "easymetric_model.issue_glb_contains_an_invalid_or_compressed_buffer_v");
  }
  for (std::size_t i = 0; i < data.accessors_count; ++i) {
    const auto& a = data.accessors[i];
    const auto elem = cgltf_calc_size(a.type, a.component_type);
    const auto componentSize = cgltf_component_size(a.component_type);
    require(
      a.count <= kMaxTriangles * 3 && a.count > 0 && componentSize && cgltf_num_components(a.type),
      "easymetric_model.issue_invalid_or_oversized_glb_accessor");
    if (a.buffer_view)
      checkSpan(a.buffer_view, a.offset, a.count, a.stride, elem, componentSize);
    if (a.is_sparse) {
      const auto& sparse = a.sparse;
      require(sparse.count <= a.count, "easymetric_model.issue_invalid_sparse_glb_accessor");
      const auto indexSize = cgltf_component_size(sparse.indices_component_type);
      require(sparse.indices_component_type == cgltf_component_type_r_8u ||
                sparse.indices_component_type == cgltf_component_type_r_16u ||
                sparse.indices_component_type == cgltf_component_type_r_32u,
              "easymetric_model.issue_invalid_sparse_glb_indices");
      checkSpan(sparse.indices_buffer_view,
                sparse.indices_byte_offset,
                sparse.count,
                indexSize,
                indexSize,
                indexSize);
      checkSpan(sparse.values_buffer_view,
                sparse.values_byte_offset,
                sparse.count,
                elem,
                elem,
                componentSize);
    }
  }
}

Material readMaterial(Model::Data& out,
                      const cgltf_material* material,
                      int& uvSet,
                      std::vector<int>& textureIds,
                      std::size_t& textureBudget)
{
  Material result;
  if (!material)
    return result;
  result.doubleSided = material->double_sided;
  result.unlit = material->unlit;
  result.masked = material->alpha_mode != cgltf_alpha_mode_opaque;
  result.cutoff = material->alpha_mode == cgltf_alpha_mode_mask ? material->alpha_cutoff : 0.5f;
  if (material->alpha_mode == cgltf_alpha_mode_blend)
    addWarning(out.warnings,
               "easymetric_model.issue_blended_materials_use_a_50_transparency_cutoff");
  if (!material->has_pbr_metallic_roughness)
    return result;
  const auto& pbr = material->pbr_metallic_roughness;
  for (int i = 0; i < 4; ++i) {
    require(std::isfinite(pbr.base_color_factor[i]),
            "easymetric_model.issue_invalid_glb_base_color");
    result.base[i] = std::clamp(pbr.base_color_factor[i], 0.0f, 1.0f);
  }
  const auto& view = pbr.base_color_texture;
  if (!view.texture)
    return result;
  uvSet = view.texcoord;
  if (view.has_transform) {
    const auto& t = view.transform;
    if (t.has_texcoord)
      uvSet = t.texcoord;
    result.offset = { t.offset[0], t.offset[1] };
    result.scale = { t.scale[0], t.scale[1], 1 };
    result.rotation = t.rotation;
  }
  require(finite(result.offset) && finite(result.scale) && std::isfinite(result.rotation) &&
            std::isfinite(result.cutoff),
          "easymetric_model.issue_invalid_glb_material");
  const auto& texture = *view.texture;
  require(texture.image && texture.image->buffer_view,
          "easymetric_model.issue_unsupported_embedded_glb_texture");
  int& id = textureIds[indexOf(out.gltf->textures, &texture)];
  if (id < 0) {
    const auto& buffer = *texture.image->buffer_view;
    Texture decoded;
    decoded.image = decodeTexture(cgltf_buffer_view_data(&buffer), buffer.size, textureBudget);
    if (texture.sampler) {
      decoded.wrapS = texture.sampler->wrap_s;
      decoded.wrapT = texture.sampler->wrap_t;
    }
    textureBudget -= decoded.image.rgba.size();
    id = int(out.textures.size());
    out.textures.push_back(std::move(decoded));
  }
  result.texture = id;
  return result;
}

Primitive readPrimitive(Model::Data& out,
                        const cgltf_primitive& source,
                        std::vector<int>& textureIds,
                        std::size_t& textureBudget)
{
  require(source.type == cgltf_primitive_type_triangles && !source.has_draco_mesh_compression,
          "easymetric_model.issue_glb_contains_unsupported_or_compressed_geometr");
  if (source.targets_count)
    addWarning(out.warnings, "easymetric_model.issue_morph_targets_are_ignored");
  const auto* pos = attribute(source, cgltf_attribute_type_position);
  const auto positions = unpack(pos, 3);
  const auto count = pos->count;
  Primitive result;
  int uvSet = 0;
  result.material = readMaterial(out, source.material, uvSet, textureIds, textureBudget);
  result.vertices.resize(count);
  const auto readAttribute = [&](cgltf_attribute_type type, std::size_t components, int index) {
    const auto* a = attribute(source, type, index);
    if (!a)
      return std::vector<float>{};
    require(a->count == count,
            "easymetric_model.issue_glb_vertex_attributes_have_different_lengths");
    return unpack(a, components);
  };
  const auto normal = readAttribute(cgltf_attribute_type_normal, 3, 0);
  const auto uv = readAttribute(cgltf_attribute_type_texcoord, 2, uvSet);
  const auto joints = readAttribute(cgltf_attribute_type_joints, 4, 0);
  const auto weights = readAttribute(cgltf_attribute_type_weights, 4, 0);
  if (attribute(source, cgltf_attribute_type_joints, 1))
    addWarning(out.warnings,
               "easymetric_model.issue_only_the_first_four_skin_weights_per_vertex_ar");
  require(result.material.texture < 0 || !uv.empty(),
          "easymetric_model.issue_textured_glb_is_missing_texture_coordinates");
  require(joints.empty() == weights.empty(),
          "easymetric_model.issue_glb_skin_is_missing_joint_indices_or_weights");
  result.skinned = !joints.empty();
  for (std::size_t i = 0; i < count; ++i) {
    auto& v = result.vertices[i];
    v.p = vec(positions.data() + 3 * i);
    if (!normal.empty())
      v.n = vec(normal.data() + 3 * i);
    if (!uv.empty())
      v.uv = { uv[2 * i], uv[2 * i + 1] };
    if (!joints.empty()) {
      float sum = 0;
      for (int j = 0; j < 4; ++j) {
        const float id = joints[4 * i + j], weight = weights[4 * i + j];
        require(id >= 0 && id < 65536 && std::floor(id) == id && weight >= 0 && weight <= 1,
                "easymetric_model.issue_invalid_glb_skin_weights");
        v.joints[j] = int(id);
        v.weights[j] = weight;
        sum += weight;
      }
      if (sum > 0)
        for (float& w : v.weights)
          w /= sum;
    }
  }
  const auto n = source.indices ? source.indices->count : count;
  require(n > 0 && n % 3 == 0 && n / 3 <= kMaxTriangles,
          "easymetric_model.issue_invalid_glb_triangle_list");
  result.indices.resize(n);
  if (source.indices) {
    const auto indices = unpack(source.indices, 1);
    for (std::size_t i = 0; i < n; ++i) {
      const float id = indices[i];
      require(id >= 0 && id < float(count) && std::floor(id) == id,
              "easymetric_model.issue_glb_index_is_out_of_range");
      result.indices[i] = uint32_t(id);
    }
  }
  else {
    for (std::size_t i = 0; i < n; ++i)
      result.indices[i] = uint32_t(i);
  }
  return result;
}

void readAnimation(Model::Data& out, const Canceled& canceled)
{
  const auto& gltf = *out.gltf;
  std::size_t scalars = 0;
  for (std::size_t i = 0; i < gltf.animations_count; ++i) {
    checkpoint(canceled);
    const auto& animation = gltf.animations[i];
    auto& channels = out.channels.emplace_back();
    Clip clip{ animation.name ? animation.name : "Clip " + std::to_string(i + 1), 0 };
    for (std::size_t j = 0; j < animation.channels_count; ++j) {
      const auto& channel = animation.channels[j];
      if (!channel.target_node || channel.target_path == cgltf_animation_path_type_weights ||
          channel.target_path == cgltf_animation_path_type_invalid) {
        addWarning(out.warnings,
                   "easymetric_model.issue_unsupported_animation_channels_are_ignored");
        continue;
      }
      require(channel.sampler && channel.sampler->input && channel.sampler->output &&
                !channel.target_node->has_matrix,
              "easymetric_model.issue_invalid_glb_animation_channel");
      const auto& sampler = *channel.sampler;
      const auto components = channel.target_path == cgltf_animation_path_type_rotation ? 4u : 3u;
      const auto stride = sampler.interpolation == cgltf_interpolation_type_cubic_spline ? 3u : 1u;
      require(sampler.input->count * stride == sampler.output->count,
              "easymetric_model.issue_glb_animation_key_counts_differ");
      scalars += sampler.input->count + sampler.output->count * components;
      require(scalars <= 8000000,
              "easymetric_model.issue_glb_animation_data_exceeds_the_supported_size");
      Channel result{ indexOf(gltf.nodes, channel.target_node),
                      channel.target_path,
                      sampler.interpolation,
                      unpack(sampler.input, 1),
                      unpack(sampler.output, components) };
      require(result.times.front() >= 0 && result.times.back() <= kMaxDuration / 1000.0f &&
                std::adjacent_find(result.times.begin(),
                                   result.times.end(),
                                   std::greater_equal<float>{}) == result.times.end(),
              "easymetric_model.issue_glb_timestamps_must_increase_and_be_within_ten");
      clip.duration = std::max(clip.duration, int(std::ceil(double(result.times.back()) * 1000)));
      channels.push_back(std::move(result));
    }
    if (!channels.empty() && clip.duration > 0)
      out.clips.push_back(std::move(clip));
    else
      out.channels.pop_back();
  }
}

void interpolate(const Channel& channel, float time, float* value, std::size_t n)
{
  const auto& times = channel.times;
  const auto& data = channel.values;
  const bool cubic = channel.interpolation == cgltf_interpolation_type_cubic_spline;
  const auto stride = n * (cubic ? 3 : 1);
  const auto at = [&](std::size_t key) { return data.data() + key * stride + (cubic ? n : 0); };
  const auto next = std::upper_bound(times.begin(), times.end(), time);
  const std::size_t b = std::size_t(next - times.begin());
  if (b == 0 || b == times.size() || channel.interpolation == cgltf_interpolation_type_step) {
    std::copy_n(at(b ? b - 1 : 0), n, value);
  }
  else {
    const float dt = times[b] - times[b - 1];
    const float t = (time - times[b - 1]) / dt;
    const auto* left = at(b - 1);
    const auto* right = at(b);
    if (cubic) {
      const float t2 = t * t, t3 = t2 * t;
      const auto* outTangent = left + n;
      const auto* inTangent = right - n;
      for (std::size_t i = 0; i < n; ++i)
        value[i] = (2 * t3 - 3 * t2 + 1) * left[i] + (t3 - 2 * t2 + t) * dt * outTangent[i] +
                   (-2 * t3 + 3 * t2) * right[i] + (t3 - t2) * dt * inTangent[i];
    }
    else if (n == 4) {
      Quat x, y;
      std::copy_n(left, 4, x.begin());
      std::copy_n(right, 4, y.begin());
      const auto q = slerp(x, y, t);
      std::copy(q.begin(), q.end(), value);
    }
    else {
      for (std::size_t i = 0; i < n; ++i)
        value[i] = left[i] * (1 - t) + right[i] * t;
    }
  }
  if (n == 4) {
    const auto q = normalized({ value[0], value[1], value[2], value[3] });
    std::copy(q.begin(), q.end(), value);
  }
}

std::vector<Mat> worldMatrices(const Model::Data& data, int clip, float time)
{
  const auto& gltf = *data.gltf;
  std::vector<cgltf_node> nodes(gltf.nodes, gltf.nodes + gltf.nodes_count);
  if (clip >= 0) {
    for (const auto& channel : data.channels[clip]) {
      auto& node = nodes[channel.node];
      switch (channel.path) {
        case cgltf_animation_path_type_translation:
          interpolate(channel, time, node.translation, 3);
          break;
        case cgltf_animation_path_type_rotation:
          interpolate(channel, time, node.rotation, 4);
          break;
        case cgltf_animation_path_type_scale: interpolate(channel, time, node.scale, 3); break;
        default:                              break;
      }
    }
  }
  std::vector<Mat> world(nodes.size());
  for (int id : data.nodeOrder) {
    cgltf_node_transform_local(&nodes[id], world[id].data());
    if (nodes[id].parent)
      world[id] = multiply(world[indexOf(gltf.nodes, nodes[id].parent)], world[id]);
  }
  return world;
}

template<class F>
void posedPrimitives(const Model::Data& data, int clip, float time, const Canceled& canceled, F f)
{
  const auto world = worldMatrices(data, clip, time);
  const auto& gltf = *data.gltf;
  for (const auto& instance : data.instances) {
    checkpoint(canceled);
    const auto& primitive = data.primitives[instance.primitive];
    const auto& node = gltf.nodes[instance.node];
    const auto& matrix = world[instance.node];
    const auto normal = normals(matrix);
    std::vector<Mat> joints, jointNormals;
    if (node.skin && primitive.skinned) {
      const auto& bind = data.bind[indexOf(gltf.skins, node.skin)];
      for (std::size_t i = 0; i < node.skin->joints_count; ++i) {
        joints.push_back(multiply(world[indexOf(gltf.nodes, node.skin->joints[i])], bind[i]));
        jointNormals.push_back(normals(joints.back()));
      }
    }
    const bool flipped = dot(vec(matrix.data()),
                             cross(vec(matrix.data() + 4), vec(matrix.data() + 8))) < 0;
    std::vector<Vertex> vertices;
    vertices.reserve(primitive.vertices.size());
    for (auto v : primitive.vertices) {
      if ((vertices.size() & 4095u) == 0)
        checkpoint(canceled);
      if (!joints.empty() && v.weights[0] + v.weights[1] + v.weights[2] + v.weights[3] > 0) {
        Vec3 p, n;
        for (int i = 0; i < 4; ++i) {
          if (v.weights[i] == 0)
            continue;
          const int joint = v.joints[i];
          require(std::size_t(joint) < joints.size(),
                  "easymetric_model.issue_glb_vertex_refers_to_a_missing_joint");
          p += transform(joints[joint], v.p) * v.weights[i];
          n += transform(jointNormals[joint], v.n) * v.weights[i];
        }
        v.p = p;
        v.n = unit(n);
      }
      else {
        v.p = transform(matrix, v.p);
        v.n = unit(transform(normal, v.n));
      }
      vertices.push_back(v);
    }
    f(primitive, vertices, flipped && joints.empty());
  }
}

} // namespace

Model::Model(std::vector<uint8_t> bytes, const Canceled& canceled)
  : m_data(std::make_unique<Data>())
{
  auto& out = *m_data;
  require(bytes.size() >= 20 && bytes.size() <= kMaxGlbBytes,
          "easymetric_model.issue_glb_must_be_a_valid_file_no_larger_than_32_mib");
  require(readU32(bytes.data()) == 0x46546c67 && readU32(bytes.data() + 4) == 2 &&
            readU32(bytes.data() + 8) == bytes.size() && readU32(bytes.data() + 12) % 4 == 0,
          "easymetric_model.issue_expected_a_complete_glb_2_0_file");
  out.bytes = std::move(bytes);
  cgltf_options options{};
  cgltf_data* parsed = nullptr;
  require(
    cgltf_parse(&options, out.bytes.data(), out.bytes.size(), &parsed) == cgltf_result_success,
    "easymetric_model.issue_could_not_parse_glb_2_0");
  out.gltf.reset(parsed);
  auto& gltf = *parsed;
  for (std::size_t i = 0; i < gltf.extensions_required_count; ++i) {
    const std::string_view extension(gltf.extensions_required[i]);
    if (extension != "KHR_texture_transform" && extension != "KHR_materials_unlit")
      throw ModelIssue("easymetric_model.issue_unsupported_required_glb_extension",
                       std::string(extension));
  }
  require(gltf.nodes_count > 0 && gltf.nodes_count <= 8192,
          "easymetric_model.issue_glb_must_have_1_to_8192_nodes");
  validateBuffers(gltf);
  require(cgltf_load_buffers(&options, parsed, nullptr) == cgltf_result_success &&
            cgltf_validate(parsed) == cgltf_result_success,
          "easymetric_model.issue_glb_data_failed_validation");

  // Sort parents before their children so pose evaluation never recurses.
  for (std::size_t i = 0; i < gltf.nodes_count; ++i)
    if (!gltf.nodes[i].parent)
      out.nodeOrder.push_back(int(i));
  for (std::size_t i = 0; i < out.nodeOrder.size(); ++i) {
    const auto& node = gltf.nodes[out.nodeOrder[i]];
    for (std::size_t j = 0; j < node.children_count; ++j) {
      require(out.nodeOrder.size() < gltf.nodes_count,
              "easymetric_model.issue_invalid_glb_node_hierarchy");
      out.nodeOrder.push_back(indexOf(gltf.nodes, node.children[j]));
    }
  }
  require(out.nodeOrder.size() == gltf.nodes_count,
          "easymetric_model.issue_invalid_glb_node_hierarchy");

  std::vector<int> sceneNodes;
  const auto* scene = gltf.scene ? gltf.scene : (gltf.scenes_count ? gltf.scenes : nullptr);
  if (scene) {
    for (std::size_t i = 0; i < scene->nodes_count; ++i)
      sceneNodes.push_back(indexOf(gltf.nodes, scene->nodes[i]));
  }
  else {
    for (int i : out.nodeOrder)
      if (!gltf.nodes[i].parent)
        sceneNodes.push_back(i);
  }
  std::vector<bool> seen(gltf.nodes_count);
  std::vector<std::vector<std::size_t>> meshPrimitives(gltf.meshes_count);
  std::vector<int> textureIds(gltf.textures_count, -1);
  std::size_t budget = kMaxTextureBytes, triangles = 0, vertices = 0;
  for (std::size_t i = 0; i < sceneNodes.size(); ++i) {
    checkpoint(canceled);
    const int id = sceneNodes[i];
    require(id >= 0 && std::size_t(id) < seen.size() && !seen[id],
            "easymetric_model.issue_invalid_glb_scene_hierarchy");
    seen[id] = true;
    const auto& node = gltf.nodes[id];
    for (std::size_t j = 0; j < node.children_count; ++j)
      sceneNodes.push_back(indexOf(gltf.nodes, node.children[j]));
    if (!node.mesh)
      continue;
    const auto& mesh = *node.mesh;
    auto& ids = meshPrimitives[indexOf(gltf.meshes, &mesh)];
    if (ids.empty()) {
      for (std::size_t j = 0; j < mesh.primitives_count; ++j) {
        const auto& primitive = mesh.primitives[j];
        const auto* positions = attribute(primitive, cgltf_attribute_type_position);
        require(positions && positions->count <= 1500000 - vertices,
                "easymetric_model.issue_glb_vertex_limit_exceeded");
        vertices += positions->count;
        ids.push_back(out.primitives.size());
        out.primitives.push_back(readPrimitive(out, primitive, textureIds, budget));
      }
    }
    for (auto primitive : ids) {
      triangles += out.primitives[primitive].indices.size() / 3;
      require(triangles <= kMaxTriangles,
              "easymetric_model.issue_glb_exceeds_the_500_000_triangle_limit");
      out.instances.push_back({ id, primitive });
    }
  }
  require(!out.instances.empty(),
          "easymetric_model.issue_the_glb_scene_contains_no_triangle_meshes");

  const Mat identity{ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
  std::size_t jointCount = 0;
  for (std::size_t i = 0; i < gltf.skins_count; ++i) {
    const auto& skin = gltf.skins[i];
    jointCount += skin.joints_count;
    require(jointCount <= 16384, "easymetric_model.issue_glb_exceeds_the_supported_joint_count");
    auto& bind = out.bind.emplace_back(skin.joints_count, identity);
    if (!skin.inverse_bind_matrices)
      continue;
    require(skin.inverse_bind_matrices->count == bind.size(),
            "easymetric_model.issue_invalid_glb_inverse_bind_matrices");
    const auto values = unpack(skin.inverse_bind_matrices, 16, 16384);
    for (std::size_t j = 0; j < bind.size(); ++j)
      std::copy_n(values.data() + j * 16, 16, bind[j].begin());
  }

  readAnimation(out, canceled);
  posedPrimitives(out, -1, 0, canceled, [&](const Primitive&, const auto& posed, bool) {
    for (const auto& v : posed)
      out.bounds.add(v.p);
  });
}

Model::~Model() = default;
const std::vector<uint8_t>& Model::bytes() const
{
  return m_data->bytes;
}
const std::vector<Clip>& Model::clips() const
{
  return m_data->clips;
}
const std::vector<std::string>& Model::warnings() const
{
  return m_data->warnings;
}

int duration(const Settings& settings)
{
  if (!(settings.speed > 0) || settings.end <= settings.start)
    return 0;
  return int(std::clamp(std::round((settings.end - double(settings.start)) / settings.speed),
                        1.0,
                        double(kMaxDuration * 4)));
}

std::vector<Sample> samples(const Settings& s)
{
  const int total = duration(s);
  require(total > 0 && s.fps >= 1 && s.fps <= 60,
          "easymetric_model.issue_set_a_valid_animation_range_and_frame_rate");
  const int n = int(std::max(1.0, std::ceil(total * double(s.fps) / 1000)));
  require(n <= 256 && s.width >= kMinSize && s.width <= kMaxSize && s.height >= kMinSize &&
            s.height <= kMaxSize && std::size_t(n) * s.width * s.height * 4 <= 128 * 1024 * 1024,
          "easymetric_model.issue_a_bake_may_contain_at_most_256_frames_and_128");
  std::vector<Sample> result;
  for (int i = 0; i < n; ++i) {
    const double t = double(i) * total / n;
    const double next = double(i + 1) * total / n;
    result.push_back({ t, int(std::round(next) - std::round(t)) });
  }
  return result;
}

void Model::validate(const Settings& s) const
{
  require(
    s.width >= kMinSize && s.width <= kMaxSize && s.height >= kMinSize && s.height <= kMaxSize,
    "easymetric_model.issue_output_size_must_be_8_to_512_pixels_per_side");
  require(s.clip >= -1 && s.clip < int(clips().size()) && s.start >= 0 && s.end > s.start &&
            s.end <= (s.clip < 0 ? kMaxDuration : clips()[s.clip].duration) && s.speed >= 0.25f &&
            s.speed <= 4 && s.fps >= 1 && s.fps <= 60,
          "easymetric_model.issue_invalid_animation_settings");
  require(finite(s.orientation) && finite(s.target) && std::isfinite(s.heading) &&
            std::isfinite(s.yaw) && std::isfinite(s.pitch) && std::abs(s.pitch) <= 75 &&
            std::isfinite(s.framing) && s.framing >= 1e-12f && s.framing <= 1e15f &&
            s.modelScale >= 0.01f && s.modelScale <= 100 && s.ambient >= 0 && s.ambient <= 2,
          "easymetric_model.issue_invalid_model_or_camera_settings");
  require(s.lights.size() <= 4, "easymetric_model.issue_at_most_four_lights_are_supported");
  for (const auto& light : s.lights)
    require(finite(light.position) && finite(light.color) && light.intensity >= 0 &&
              light.intensity <= 8 && light.range > 0 && light.range <= 1000 &&
              std::isfinite(light.azimuth) && std::isfinite(light.elevation),
            "easymetric_model.issue_invalid_lighting_settings");
  for (const auto* keys : { &s.position, &s.rotation, &s.scale }) {
    require(keys->size() <= 256, "easymetric_model.issue_at_most_256_keys_are_supported_per_track");
    int last = -1;
    for (const auto& key : *keys) {
      require(key.time > last && key.time <= kMaxDuration * 4 && finite(key.value),
              "easymetric_model.issue_invalid_motion_key");
      if (keys == &s.scale)
        require(key.value.x >= 0.01f && key.value.x <= 100,
                "easymetric_model.issue_invalid_scale_key");
      last = key.time;
    }
  }
}

namespace {

struct KeyInterval {
  Vec3 a, b;
  float t = 0;
};

KeyInterval interval(const std::vector<MotionKey>& keys, double time, Vec3 normal = {})
{
  if (keys.empty())
    return { normal, normal };
  const auto b = std::upper_bound(keys.begin(), keys.end(), time, [](double t, const MotionKey& k) {
    return t < k.time;
  });
  if (b == keys.begin())
    return { b->value, b->value };
  if (b == keys.end())
    return { keys.back().value, keys.back().value };
  const auto& a = *(b - 1);
  return { a.value, b->value, float((time - a.time) / (b->time - a.time)) };
}

Vec3 lerp(const KeyInterval& key)
{
  return key.a * (1 - key.t) + key.b * key.t;
}

template<class F>
void scenePose(const Model::Data& data,
               const Settings& s,
               double t,
               const Canceled& canceled,
               F f,
               bool rest = false)
{
  const float sourceTime = float(std::clamp(s.start + t * s.speed, double(s.start), double(s.end)) /
                                 1000);
  const Vec3 origin = data.bounds.center();
  const float extent = std::max(length(data.bounds.max - data.bounds.min), 1e-9f);
  const auto orientation = euler(s.orientation);
  const Vec3 pivot = s.basePivot ? Vec3{ 0, (data.bounds.min.y - origin.y) / extent, 0 } : Vec3{};
  const Vec3 translation = rest ? Vec3{} : lerp(interval(s.position, t));
  const auto angle = rest ? KeyInterval{} : interval(s.rotation, t);
  auto motion = slerp(euler(angle.a), euler(angle.b), angle.t);
  if (s.turntable && !rest)
    motion = multiply(euler({ 0, float(360 * t / std::max(1, duration(s))), 0 }), motion);
  const float scale = (rest ? 1 : lerp(interval(s.scale, t, { 1, 1, 1 })).x) * s.modelScale;
  posedPrimitives(
    data,
    rest ? -1 : s.clip,
    sourceTime,
    canceled,
    [&](const Primitive& primitive, auto& vertices, bool flipped) {
      for (auto& v : vertices) {
        v.p = rotate(motion, rotate(orientation, (v.p - origin) * (1 / extent) - pivot) * scale) +
              pivot + translation;
        v.n = rotate(motion, rotate(orientation, v.n));
      }
      f(primitive, vertices, flipped);
    });
}

int nearestCoord(float x, int size, int wrap)
{
  if (!std::isfinite(x))
    return 0;
  if (wrap == 10497)
    x -= std::floor(x);
  else if (wrap == 33648)
    x = 1 - std::abs(std::fmod(std::abs(x), 2.0f) - 1);
  return std::min(size - 1, int(std::clamp(x, 0.0f, 1.0f) * size));
}

float srgbToLinear(float x)
{
  return x <= 0.04045f ? x / 12.92f : std::pow((x + 0.055f) / 1.055f, 2.4f);
}

uint8_t linearToSrgb(float x)
{
  x = std::clamp(x, 0.0f, 1.0f);
  const float value = x <= 0.0031308f ? x * 12.92f : 1.055f * std::pow(x, 1 / 2.4f) - 0.055f;
  return uint8_t(std::round(value * 255));
}

std::array<float, 4> baseColor(const Model::Data& data, const Material& m, Vec3 uv)
{
  auto color = m.base;
  if (m.texture >= 0) {
    const auto& t = data.textures[m.texture];
    const float a = std::cos(m.rotation), b = std::sin(m.rotation);
    const float u = m.offset.x + a * uv.x * m.scale.x - b * uv.y * m.scale.y;
    const float v = m.offset.y + b * uv.x * m.scale.x + a * uv.y * m.scale.y;
    const int x = nearestCoord(u, t.image.width, t.wrapS);
    const int y = nearestCoord(v, t.image.height, t.wrapT);
    const auto* p = &t.image.rgba[(std::size_t(y) * t.image.width + x) * 4];
    for (int i = 0; i < 3; ++i)
      color[i] *= srgbToLinear(p[i] / 255.0f);
    color[3] *= p[3] / 255.0f;
  }
  return color;
}

Vec3 illuminate(const Settings& settings, Vec3 point, Vec3 normal)
{
  Vec3 result{ settings.ambient, settings.ambient, settings.ambient };
  for (const auto& light : settings.lights) {
    if (!light.enabled)
      continue;
    Vec3 direction = lightDirection(light);
    float attenuation = 1;
    if (light.point) {
      const auto delta = light.position - point;
      const float distance = length(delta);
      if (distance >= light.range || distance < 1e-9f)
        continue;
      direction = delta * (1 / distance);
      attenuation = 1 - distance / light.range;
      attenuation *= attenuation;
    }
    result += light.color *
              (light.intensity * attenuation * std::max(0.0f, dot(normal, direction)));
  }
  return result;
}

float edge(Vec3 a, Vec3 b, Vec3 p)
{
  return (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);
}

Vec3 projectWithAxes(const Settings& s, const Axes& axes, Vec3 point)
{
  const Vec3 v = point - s.target;
  const float scale = std::min(s.width, s.height) / s.framing;
  return { s.width * 0.5f + dot(v, axes.right) * scale,
           s.height * 0.5f - dot(v, axes.up) * scale,
           dot(v, axes.front) };
}

} // namespace

Vec3 project(const Settings& s, Vec3 point)
{
  return projectWithAxes(s, camera(s), point);
}

Vec3 moveInView(const Settings& s, Vec3 point, float dx, float dy)
{
  const auto axes = camera(s);
  return point + (axes.right * dx - axes.up * dy) * (s.framing / std::min(s.width, s.height));
}

Vec3 lightDirection(const Light& light)
{
  const float yaw = radians(light.azimuth), pitch = radians(light.elevation);
  return { std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch) };
}

void Model::fit(Settings& s, bool sequence, const Canceled& canceled) const
{
  validate(s);
  Bounds bounds;
  const auto include = [&](double time, bool rest) {
    scenePose(
      *m_data,
      s,
      time,
      canceled,
      [&](const Primitive&, const auto& vertices, bool) {
        for (const Vertex& v : vertices)
          bounds.add(v.p);
      },
      rest);
  };
  if (sequence) {
    for (const auto& sample : samples(s))
      include(sample.time, false);
  }
  else {
    include(0, true);
  }
  s.target = bounds.center();
  // Use one bounding sphere so orbiting and switching direction keeps a fixed scale.
  s.framing = std::max(0.001f, length(bounds.max - bounds.min) * 1.12f);
}

Image Model::render(const Settings& s, double time, const Canceled& canceled) const
{
  validate(s);
  Image result{ s.width, s.height, std::vector<uint8_t>(std::size_t(s.width) * s.height * 4) };
  std::vector<float> depth(std::size_t(s.width) * s.height,
                           -std::numeric_limits<float>::infinity());
  const auto axes = camera(s);
  scenePose(*m_data,
            s,
            time,
            canceled,
            [&](const Primitive& primitive, const auto& vertices, bool flipped) {
              std::vector<Vec3> screen;
              screen.reserve(vertices.size());
              for (const Vertex& v : vertices)
                screen.push_back(projectWithAxes(s, axes, v.p));
              for (std::size_t i = 0; i < primitive.indices.size(); i += 3) {
                if (((i / 3) & 255u) == 0)
                  checkpoint(canceled);
                const auto ia = primitive.indices[i], ib = primitive.indices[i + 1],
                           ic = primitive.indices[i + 2];
                const Vec3 a = screen[ia], b = screen[ib], c = screen[ic];
                if (!finite(a) || !finite(b) || !finite(c))
                  throw ModelIssue("easymetric_model.issue_the_sampled_glb_pose_is_out_of_range");
                const float area = edge(a, b, c);
                const bool back = (area > 0) != flipped;
                if (!std::isfinite(area) || std::abs(area) < 1e-8f ||
                    (back && !primitive.material.doubleSided))
                  continue;
                const auto lower = [](float a, float b, float c, int size) {
                  return int(std::clamp(std::floor(std::min({ a, b, c })), 0.0f, float(size)));
                };
                const auto upper = [](float a, float b, float c, int size) {
                  return int(std::clamp(std::ceil(std::max({ a, b, c })), 0.0f, float(size)));
                };
                const int x0 = lower(a.x, b.x, c.x, s.width), x1 = upper(a.x, b.x, c.x, s.width);
                const int y0 = lower(a.y, b.y, c.y, s.height), y1 = upper(a.y, b.y, c.y, s.height);
                const Vertex& va = vertices[ia];
                const Vertex& vb = vertices[ib];
                const Vertex& vc = vertices[ic];
                const Vec3 face = unit(cross(vb.p - va.p, vc.p - va.p)) * (flipped ? -1.0f : 1.0f);
                for (int y = y0; y < y1; ++y) {
                  if ((y & 31) == 0)
                    checkpoint(canceled);
                  for (int x = x0; x < x1; ++x) {
                    const Vec3 pixel{ x + 0.5f, y + 0.5f };
                    const float u = edge(b, c, pixel) / area;
                    const float v = edge(c, a, pixel) / area;
                    const float w = 1 - u - v;
                    if (std::min({ u, v, w }) < -1e-6f)
                      continue;
                    const float z = a.z * u + b.z * v + c.z * w;
                    const auto index = std::size_t(y) * s.width + x;
                    if (z <= depth[index])
                      continue;
                    const auto color =
                      baseColor(*m_data, primitive.material, va.uv * u + vb.uv * v + vc.uv * w);
                    if (primitive.material.masked && color[3] < primitive.material.cutoff)
                      continue;
                    Vec3 normal = unit(va.n * u + vb.n * v + vc.n * w);
                    if (dot(normal, normal) == 0)
                      normal = face;
                    if (back)
                      normal = normal * -1;
                    const Vec3 light = primitive.material.unlit ?
                                         Vec3{ 1, 1, 1 } :
                                         illuminate(s, va.p * u + vb.p * v + vc.p * w, normal);
                    const auto p = index * 4;
                    result.rgba[p] = linearToSrgb(color[0] * light.x);
                    result.rgba[p + 1] = linearToSrgb(color[1] * light.y);
                    result.rgba[p + 2] = linearToSrgb(color[2] * light.z);
                    result.rgba[p + 3] = 255;
                    depth[index] = z;
                  }
                }
              }
            });
  return result;
}

} // namespace app::easymetric
