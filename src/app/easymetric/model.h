// Aseprite 2.0
// Copyright (C) 2026 Aseprite 2.0 contributors
//
// This program is distributed under the terms of
// the End-User License Agreement for Aseprite.

#ifndef APP_EASYMETRIC_MODEL_H_INCLUDED
#define APP_EASYMETRIC_MODEL_H_INCLUDED
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace app::easymetric {

constexpr std::size_t kMaxGlbBytes = 32 * 1024 * 1024;
constexpr int kMinSize = 8;
constexpr int kMaxSize = 512;

struct Vec3 {
  float x = 0, y = 0, z = 0;
};

struct Light {
  bool enabled = true;
  bool point = false;
  Vec3 color{ 1, 1, 1 };
  float intensity = 0.8f;
  Vec3 position{ 1, 1, 1 };
  float range = 3;
  float azimuth = 35;
  float elevation = 50;
};

struct MotionKey {
  int time = 0; // Milliseconds on the output timeline.
  Vec3 value;
};

struct Settings {
  Vec3 orientation;
  float heading = 0;
  float modelScale = 1;
  bool basePivot = false;
  float yaw = 45, pitch = 30;
  Vec3 target;
  float framing = 1;
  int width = 64, height = 64;
  float ambient = 0.35f;
  float shadowStrength = 1.0f;
  bool groundShadow = true;
  std::vector<Light> lights{ Light{} };
  int clip = -1;
  int start = 0, end = 1000; // Milliseconds in the source clip.
  float speed = 1;
  int fps = 12;
  int exportFrames = 0; // 0 = auto from fps and duration; 1..256 = explicit sequence frame count.
  bool loop = true;
  bool turntable = false;
  bool fitSequenceOnBake = true;
  std::vector<MotionKey> position, rotation, scale;
};

struct Clip {
  std::string name;
  int duration = 0; // Milliseconds.
};

struct Sample {
  double time = 0; // Milliseconds on the output timeline.
  int duration = 0;
};

struct Image {
  int width = 0, height = 0;
  std::vector<uint8_t> rgba;
};

using Canceled = std::function<bool()>;

class ModelIssue : public std::runtime_error {
public:
  explicit ModelIssue(const char* messageId, std::string detail = {})
    : std::runtime_error(messageId)
    , detail(std::move(detail))
  {
  }

  std::string detail;
};

class Model {
public:
  struct Data;
  explicit Model(std::vector<uint8_t> bytes, const Canceled& canceled = {});
  ~Model();

  const std::vector<uint8_t>& bytes() const;
  const std::vector<Clip>& clips() const;
  const std::vector<std::string>& warnings() const;
  void validate(const Settings& settings) const;
  void fit(Settings& settings, bool sequence, const Canceled& canceled = {}) const;
  bool fitIfClipped(Settings& settings, const Canceled& canceled = {}) const;
  Image render(const Settings& settings, double time, const Canceled& canceled = {}) const;

private:
  std::unique_ptr<Data> m_data;
};

int duration(const Settings& settings);
std::vector<Sample> samples(const Settings& settings);
Vec3 project(const Settings& settings, Vec3 point);
Vec3 moveInView(const Settings& settings, Vec3 point, float dx, float dy);
Vec3 lightDirection(const Light& light);

} // namespace app::easymetric

#endif
