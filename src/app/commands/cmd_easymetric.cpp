// Aseprite 2.0
// Copyright (C) 2026 Antigravity / Aseprite 2.0 contributors
//
// This program is distributed under the terms of
// the End-User License Agreement for Aseprite.

#ifdef HAVE_CONFIG_H
  #include "config.h"
#endif

#include "app/app.h"
#include "app/cmd/add_slice.h"
#include "app/cmd/add_tag.h"
#include "app/cmd/flip_image.h"
#include "app/cmd/remove_slice.h"
#include "app/cmd/set_layer_name.h"
#include "app/cmd/set_slice_key.h"
#include "app/cmd/set_user_data_property.h"
#include "app/commands/command.h"
#include "app/commands/commands.h"
#include "app/console.h"
#include "app/context.h"
#include "app/doc.h"
#include "app/doc_api.h"
#include "app/easymetric/model_project.h"
#include "app/easymetric/model_studio.h"
#include "app/i18n/strings.h"
#include "app/extra_cel.h"
#include "app/file/file.h"
#include "app/file_selector.h"
#include "app/color_utils.h"
#include "app/pref/preferences.h"
#include "app/site.h"
#include "app/doc_undo.h"
#include "app/doc_undo_observer.h"
#include "app/tools/active_tool.h"
#include "app/tools/ink.h"
#include "app/tools/tool.h"
#include "app/tools/tool_box.h"
#include "app/tx.h"
#include "app/ui/color_button.h"
#include "app/ui/doc_view.h"
#include "ui/int_entry.h"
#include "ui/window.h"
#include "app/ui/editor/editor.h"
#include "app/ui/editor/editor_observer.h"
#include "app/ui/editor/standby_state.h"
#include "app/ui/toolbar.h"
#include "doc/mask.h"
#include "app/ui/editor/editor_view.h"
#include "app/ui/skin/skin_theme.h"
#include "app/ui/status_bar.h"
#include "app/ui_context.h"
#include "app/util/conversion_to_surface.h"
#include "base/fs.h"
#include "base/time.h"
#include "doc/algorithm/flip_type.h"
#include "doc/brush_type.h"
#include "doc/cel.h"
#include "doc/cels_range.h"
#include "doc/color.h"
#include "doc/frames_sequence.h"
#include "doc/image.h"
#include "doc/layer.h"
#include "doc/layer_smart_object.h"
#include "doc/algorithm/flip_image.h"
#include "doc/primitives.h"
#include "doc/slice.h"
#include "doc/sprite.h"
#include "doc/tag.h"
#include "doc/tags.h"
#include "fmt/format.h"
#include "json11.hpp"
#include "os/sampling.h"
#include "os/surface.h"
#include "os/system.h"
#include "render/render.h"
#include "ui/alert.h"
#include "ui/box.h"
#include "ui/button.h"
#include "ui/combobox.h"
#include "ui/entry.h"
#include "ui/graphics.h"
#include "ui/label.h"
#include "ui/manager.h"
#include "ui/message.h"
#include "ui/paint_event.h"
#include "ui/resize_event.h"
#include "ui/scale.h"
#include "ui/separator.h"
#include "ui/size_hint_event.h"
#include "ui/slider.h"
#include "ui/system.h"
#include "ui/timer.h"
#include "ui/view.h"
#include "ui/widget.h"

#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <typeinfo>
#include <vector>

#if defined(_WIN32)
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #include <windows.h>
  #include <winhttp.h>
  #pragma comment(lib, "winhttp.lib")
#endif

namespace app {

namespace {

// ---------------------------------------------------------------------------
// Easymetric error / crash reporting to agent-hub.
//
// What this does, and what it deliberately does NOT do:
// - Every Easymetric error caught in this file is appended to a local
//   log file (see easymetricHubLogPath) AND, only if reporting is
//   explicitly enabled, posted to agent-hub as a message.
// - A native crash cannot post from inside the crash itself, so the
//   studio writes a session marker on entry and removes it on a
//   clean exit. If the marker is still there on the next entry,
//   the previous session ended unexpectedly, and a "crash" report
//   (no dump, no document content, just that fact) is sent then.
// - Reporting is OFF by default. It turns on only when all of these
//   environment variables are set on the machine running Aseprite:
//     AGENT_HUB_ENABLED=1
//     AGENT_HUB_TOKEN=<agent token issued by the hub admin>
//     AGENT_HUB_AGENT=<agent name that token was issued for>
//   Optional: AGENT_HUB_URL (default is the mesh hub URL below),
//   AGENT_HUB_PROJECT (default "agent-hub").
// - No token, URL, or password is hardcoded here and none is ever
//   written to the log file. The token is read from the environment
//   at send time only, and is sent only as a Bearer header over
//   HTTPS. If any variable is missing, reporting is local-log only.
// - Only the error kind, location, and exception message (truncated
//   to 500 characters) are sent. Never a sprite, image, file
//   contents, or document name. A hub send failure never throws
//   into the UI: it is swallowed, so reporting can never itself
//   crash the app, and it runs on a detached thread with a 5s
//   timeout so it can never block painting or editing.
// - Hub API (tools/agent-mesh/mesh_client.py, cloudflare-hub):
//   POST {hub_url}/send, Bearer token, JSON {project, from, to,
//   type, body}. "from" must equal the token's agent, and the
//   project must be allowed for that token, or the hub returns
//   403. On non-Windows builds this is local-log only: the
//   WinHTTP sender below is Windows-only.
// ---------------------------------------------------------------------------
inline std::string easymetricHubTempDir()
{
  const char* temp = std::getenv("TEMP");
  if (!temp || !*temp)
    temp = std::getenv("TMP");
  return temp && *temp ? std::string(temp) : std::string(".");
}

inline std::string easymetricHubLogPath()
{
  return easymetricHubTempDir() + "\\aseprite-easymetric-errors.log";
}

inline std::string easymetricHubMarkerPath()
{
  return easymetricHubTempDir() + "\\aseprite-easymetric-session.marker";
}

inline std::string easymetricHubTimestamp()
{
  std::time_t now = std::time(nullptr);
  std::tm tmv{};
#if defined(_WIN32)
  gmtime_s(&tmv, &now);
#else
  gmtime_r(&now, &tmv);
#endif
  char buf[32];
  std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tmv);
  return std::string(buf);
}

inline std::string easymetricJsonEscape(const std::string& in)
{
  std::string out;
  for (char ch : in) {
    switch (ch) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (static_cast<unsigned char>(ch) < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof(buf), "\\u%04x", ch);
          out += buf;
        }
        else
          out += ch;
    }
  }
  return out;
}

inline std::string easymetricUtf8(const std::wstring& in)
{
#if defined(_WIN32)
  if (in.empty())
    return std::string();
  const int n = WideCharToMultiByte(CP_UTF8, 0, in.c_str(), -1,
                                    nullptr, 0, nullptr, nullptr);
  if (n <= 0)
    return std::string();
  // Buffer sized n (including the terminator), then trimmed, so
  // the conversion never writes past the string's storage.
  std::string out(static_cast<size_t>(n), '\0');
  WideCharToMultiByte(CP_UTF8, 0, in.c_str(), -1, out.data(), n,
                      nullptr, nullptr);
  if (!out.empty() && out.back() == '\0')
    out.pop_back();
  return out;
#else
  (void)in;
  return std::string();
#endif
}

inline std::wstring easymetricWide(const std::string& in)
{
#if defined(_WIN32)
  if (in.empty())
    return std::wstring();
  const int n = MultiByteToWideChar(CP_UTF8, 0, in.c_str(), -1,
                                    nullptr, 0);
  if (n <= 0)
    return std::wstring();
  std::wstring out(static_cast<size_t>(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, in.c_str(), -1, out.data(), n);
  if (!out.empty() && out.back() == L'\0')
    out.pop_back();
  return out;
#else
  (void)in;
  return std::wstring();
#endif
}

inline void easymetricSendToHub(const std::string& type,
                                const std::string& body)
{
  // Local log first, always, even when hub sending is disabled.
  try {
    std::ofstream log(easymetricHubLogPath(), std::ios::app);
    if (log)
      log << easymetricHubTimestamp() << " [" << type << "] " << body << "\n";
  }
  catch (...) {
  }

  const char* enabled = std::getenv("AGENT_HUB_ENABLED");
  const char* token = std::getenv("AGENT_HUB_TOKEN");
  const char* agent = std::getenv("AGENT_HUB_AGENT");
  if (!enabled || std::string(enabled) != "1" || !token || !*token ||
      !agent || !*agent)
    return;

  const char* urlEnv = std::getenv("AGENT_HUB_URL");
  const char* projectEnv = std::getenv("AGENT_HUB_PROJECT");
  const std::string hubUrl =
    urlEnv && *urlEnv ? urlEnv : "https://agent-mesh-hub.slack-clone-arin.workers.dev";
  const std::string project =
    projectEnv && *projectEnv ? projectEnv : "agent-hub";
  const std::string fullBody = "[easymetric] " + body;
  const std::string json =
    "{\"project\":\"" + easymetricJsonEscape(project) +
    "\",\"from\":\"" + easymetricJsonEscape(agent) +
    "\",\"to\":\"all\",\"type\":\"" + easymetricJsonEscape(type) +
    "\",\"body\":\"" + easymetricJsonEscape(fullBody) + "\"}";

#if defined(_WIN32)
  // Detached thread: a slow or dead hub must never block the UI.
  // Token and body are copied into the thread by value; nothing
  // here is written to a file, and the token is never logged.
  std::thread([hubUrl, token = std::string(token), json]() {
    try {
      const std::wstring host =
        L"agent-mesh-hub.slack-clone-arin.workers.dev";
      // Only the default hub host is used over WinHTTP here; a
      // custom AGENT_HUB_URL is honoured for its host only when
      // it is an https URL, otherwise the default host is used.
      std::wstring useHost = host;
      if (hubUrl.rfind("https://", 0) == 0) {
        const std::string rest = hubUrl.substr(8);
        const size_t slash = rest.find('/');
        const std::string customHost =
          slash == std::string::npos ? rest : rest.substr(0, slash);
        if (!customHost.empty())
          useHost = easymetricWide(customHost);
      }
      HINTERNET session = WinHttpOpen(L"AsepriteEasymetric/2.0",
                                      WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                      WINHTTP_NO_PROXY_NAME,
                                      WINHTTP_NO_PROXY_BYPASS, 0);
      if (!session)
        return;
      WinHttpSetTimeouts(session, 5000, 5000, 5000, 5000);
      HINTERNET connect = WinHttpConnect(session, useHost.c_str(),
                                         INTERNET_DEFAULT_HTTPS_PORT, 0);
      if (!connect) {
        WinHttpCloseHandle(session);
        return;
      }
      HINTERNET request = WinHttpOpenRequest(
        connect, L"POST", L"/send", nullptr, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
      if (!request) {
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return;
      }
      const std::wstring headers =
        L"Content-Type: application/json\r\nAuthorization: Bearer " +
        easymetricWide(token) + L"\r\n";
      BOOL sent = WinHttpSendRequest(request, headers.c_str(),
                                     static_cast<DWORD>(headers.size()),
                                     (LPVOID)json.data(),
                                     static_cast<DWORD>(json.size()),
                                     static_cast<DWORD>(json.size()), 0);
      if (sent)
        WinHttpReceiveResponse(request, nullptr);
      WinHttpCloseHandle(request);
      WinHttpCloseHandle(connect);
      WinHttpCloseHandle(session);
    }
    catch (...) {
    }
  }).detach();
#else
  (void)hubUrl;
  (void)json;
#endif
}

inline void easymetricReportError(const std::string& where,
                                  const std::string& what)
{
  std::string msg = what.substr(0, 500);
  easymetricSendToHub("error", where + ": " + msg);
}

inline void easymetricReportCrash(const std::string& what)
{
  easymetricSendToHub("crash", what.substr(0, 500));
}

enum FaceId : int {
  kFaceTop = 0,
  kFaceDown = 1,
  kFaceFront = 2,
  kFaceBack = 3,
  kFaceRight = 4,
  kFaceLeft = 5,
  kFaceCount = 6
};

enum RenderStyle : int {
  kRenderTexturedLit = 0,
  kRenderTexturedFlat = 1,
  kRenderColoredFaces = 2,
  kRenderNormalShaded = 3,
  kRenderDepthShaded = 4,
  kRenderWireframeTex = 5
};

struct FaceSpec {
  const char* sliceName;
  const char* displayName;
  int gridCol;
  int gridRow;
  uint8_t r, g, b;
  int defaultYaw;
  int defaultPitch;
};

// 4-Column 2-Row layout matching reference UI:
// Col 0: Top (row 0),    Bottom (row 1)
// Col 1: Front (row 0),  Back (row 1)
// Col 2: Right (row 0),  Left (row 1)
// Col 3: Preview (row 0), Fields (row 1)
constexpr std::array<FaceSpec, kFaceCount> kFaceSpecs = {{
  { "TOP",   "Top",    0, 0, 168,  85, 247,  45,  60 },
  { "DOWN",  "Bottom", 0, 1, 244,  63,  94,  45, -60 },
  { "FRONT", "Front",  1, 0,   0, 229, 255,   0,   0 },
  { "BACK",  "Back",   1, 1,  59, 130, 246, 180,   0 },
  { "RIGHT", "Right",  2, 0, 245, 158,  11, -90,   0 },
  { "LEFT",  "Left",   2, 1,  16, 185, 129,  90,   0 },
}};

inline uint8_t clampByte(float v)
{
  if (v <= 0.0f) return 0;
  if (v >= 255.0f) return 255;
  return static_cast<uint8_t>(v + 0.5f);
}

inline float pixelLuma(uint32_t c)
{
  if (doc::rgba_geta(c) <= 16)
    return 0.0f;
  return (0.299f * doc::rgba_getr(c) +
          0.587f * doc::rgba_getg(c) +
          0.114f * doc::rgba_getb(c)) * (1.0f / 255.0f);
}

uint32_t starterSurfacePixel(int face, int u, int v, int W, int H)
{
  const bool isBorder = (u == 0 || v == 0 || u == W - 1 || v == H - 1);
  const bool isInnerBorder = (u == 1 || v == 1 || u == W - 2 || v == H - 2);
  const bool isCorner = ((u <= 2 && v <= 2) || (u >= W - 3 && v <= 2) ||
                         (u <= 2 && v >= H - 3) || (u >= W - 3 && v >= H - 3));
  if (isCorner)
    return doc::rgba(251, 191, 36, 255);
  if (isBorder)
    return doc::rgba(30, 41, 59, 255);
  if (isInnerBorder) {
    const auto& spec = kFaceSpecs[face];
    return doc::rgba(spec.r / 2 + 30, spec.g / 2 + 30, spec.b / 2 + 30, 255);
  }

  const float cx = (W - 1) * 0.5f;
  const float cy = (H - 1) * 0.5f;
  const float dx = u - cx;
  const float dy = v - cy;
  const float dist = std::sqrt(dx * dx + dy * dy);
  const int minDim = std::min(W, H);

  switch (face) {
    case kFaceFront: {
      if (v == 3 && u >= 4 && u <= W - 5)
        return doc::rgba(0, 229, 255, 255);
      if (dist <= 2.3f)
        return doc::rgba(148, 163, 184, 255);
      if (dist <= minDim * 0.30f)
        return doc::rgba(15, 23, 42, 255);
      if (dist <= minDim * 0.36f)
        return doc::rgba(71, 85, 105, 255);
      return (((u + v) & 1) == 0) ? doc::rgba(30, 41, 59, 255) : doc::rgba(51, 65, 85, 255);
    }
    case kFaceBack: {
      if (v >= 4 && v <= 8 && (v & 1) == 0 && u >= 5 && u <= W - 6)
        return doc::rgba(15, 23, 42, 255);
      if (dist <= 2.5f)
        return doc::rgba(239, 68, 68, 255);
      return doc::rgba(45, 55, 72, 255);
    }
    case kFaceLeft:
    case kFaceRight: {
      if (v >= H / 2 - 2 && v <= H / 2 + 2 && u >= 5 && u <= W - 6)
        return doc::rgba(28, 25, 23, 255);
      return (((u + v) % 3) == 0) ? doc::rgba(93, 64, 55, 255) : doc::rgba(78, 52, 46, 255);
    }
    case kFaceTop: {
      if (v >= H / 2 - 1 && v <= H / 2 + 1 && u >= 4 && u <= W - 5)
        return doc::rgba(15, 23, 42, 255);
      if (v >= 3 && v <= 4 && u >= 4 && u <= W - 5 && (u % 3 == 0))
        return doc::rgba(253, 224, 71, 255);
      return doc::rgba(109, 76, 65, 255);
    }
    case kFaceDown:
    default: {
      if ((u >= 3 && u <= 5 && v >= 3 && v <= 5) ||
          (u >= W - 6 && u <= W - 4 && v >= 3 && v <= 5) ||
          (u >= 3 && u <= 5 && v >= H - 6 && v <= H - 4) ||
          (u >= W - 6 && u <= W - 4 && v >= H - 6 && v <= H - 4)) {
        return doc::rgba(15, 23, 42, 255);
      }
      return doc::rgba(51, 65, 85, 255);
    }
  }
}

struct SixSurfaceData {
  int S = 24;
  std::array<int, kFaceCount> w = { 24, 24, 24, 24, 24, 24 };
  std::array<int, kFaceCount> h = { 24, 24, 24, 24, 24, 24 };
  std::array<std::vector<uint32_t>, kFaceCount> pixels;
  std::array<std::vector<uint8_t>, kFaceCount> depthPixels;
  // Depth alpha: 255 = opaque depth value, <= 16 = transparent =
  // through-hole (William's spec: transparent cuts past the centre).
  // Kept separate from depthPixels because the gray of a transparent
  // pixel reads as 0 (black = deepest), which is a different thing.
  std::array<std::vector<uint8_t>, kFaceCount> depthAlpha;
  std::array<std::vector<uint8_t>, kFaceCount> baseAlpha;
  std::array<bool, kFaceCount> hasBaseLayer = { false, false, false, false, false, false };
  std::array<gfx::Rect, kFaceCount> bounds;
  std::array<bool, 3> colLinked = { false, false, false };
  bool hasSlices = false;
  bool hasCustomDepthLayer = false;

  void init(int faceSize)
  {
    S = std::max(8, faceSize);
    for (int f = 0; f < kFaceCount; ++f) {
      w[f] = S;
      h[f] = S;
      pixels[f].assign(S * S, 0);
      depthPixels[f].assign(S * S, 128);
      depthAlpha[f].assign(S * S, 255);
      baseAlpha[f].assign(S * S, 0);
    }
    hasBaseLayer.fill(false);
    colLinked.fill(false);
  }

  void setFaceSize(int face, int fw, int fh)
  {
    fw = std::clamp(fw, 1, 1024);
    fh = std::clamp(fh, 1, 1024);
    w[face] = fw;
    h[face] = fh;
    pixels[face].assign(fw * fh, 0);
    depthPixels[face].assign(fw * fh, 128);
    depthAlpha[face].assign(fw * fh, 255);
    baseAlpha[face].assign(fw * fh, 0);
    S = std::max(S, std::max(fw, fh));
  }

  void propagateDimensionLink(int srcFace, int newW, int newH)
  {
    newW = std::clamp(newW, 1, 256);
    newH = std::clamp(newH, 1, 256);

    int X = w[kFaceFront];
    int Y = h[kFaceFront];
    int Z = w[kFaceRight];

    switch (srcFace) {
      case kFaceTop:
      case kFaceDown:
        X = newW;
        Z = newH;
        break;
      case kFaceFront:
      case kFaceBack:
        X = newW;
        Y = newH;
        break;
      case kFaceRight:
      case kFaceLeft:
        Z = newW;
        Y = newH;
        break;
    }

    setFaceSize(kFaceTop,   X, Z);
    setFaceSize(kFaceDown,  X, Z);
    setFaceSize(kFaceFront, X, Y);
    setFaceSize(kFaceBack,  X, Y);
    setFaceSize(kFaceRight, Z, Y);
    setFaceSize(kFaceLeft,  Z, Y);
  }

  uint32_t sample(int face, int u, int v) const
  {
    const int fw = w[face];
    const int fh = h[face];
    u = std::clamp(u, 0, fw - 1);
    v = std::clamp(v, 0, fh - 1);
    return pixels[face][v * fw + u];
  }

  float sampleDepthNorm(int face, int u, int v) const
  {
    const int fw = w[face];
    const int fh = h[face];
    u = std::clamp(u, 0, fw - 1);
    v = std::clamp(v, 0, fh - 1);
    if (hasCustomDepthLayer) {
      return float(depthPixels[face][v * fw + u]) * (1.0f / 255.0f);
    }
    return pixelLuma(pixels[face][v * fw + u]);
  }

  // Depth gray at a face pixel. Returns false when the depth pixel is
  // transparent (alpha <= 16): a through-hole, not a gray value.
  // Used by the voxel sculptor; the normal/brightness path above
  // deliberately keeps reading transparent as black (deepest).
  bool depthGray(int face, int u, int v, uint8_t& gray) const
  {
    if (face < 0 || face >= kFaceCount) return false;
    const int fw = w[face];
    const int fh = h[face];
    if (fw <= 0 || fh <= 0 || depthAlpha[face].empty()) return false;
    u = std::clamp(u, 0, fw - 1);
    v = std::clamp(v, 0, fh - 1);
    if (depthAlpha[face][v * fw + u] <= 16) return false;
    gray = depthPixels[face][v * fw + u];
    return true;
  }

  bool hasPixels(int face) const
  {
    if (face < 0 || face >= kFaceCount) return false;
    const auto& px = pixels[face];
    for (uint32_t c : px) {
      if (doc::rgba_geta(c) > 16)
        return true;
    }
    return false;
  }

  int symmetricFace(int face) const
  {
    if (hasPixels(face)) return face;
    switch (face) {
      case kFaceTop:   if (hasPixels(kFaceDown)) return kFaceDown; break;
      case kFaceDown:  if (hasPixels(kFaceTop))  return kFaceTop; break;
      case kFaceFront: if (hasPixels(kFaceBack)) return kFaceBack; break;
      case kFaceBack:  if (hasPixels(kFaceFront)) return kFaceFront; break;
      case kFaceRight: if (hasPixels(kFaceLeft))  return kFaceLeft; break;
      case kFaceLeft:  if (hasPixels(kFaceRight)) return kFaceRight; break;
    }
    return face;
  }

  // An empty face shows its opposite face, MIRRORED the way the axis table
  // (ghostAxesOfFace / the renderer) relates them: Top/Down share X and flip
  // Z (v); Front/Back flip X (u); Right/Left flip Z (u). These are the same
  // flips the linked-column copies use. (Before 2026-11-16 this sampled the
  // opposite face at the same (u, v), i.e. unmirrored.) Sizes may differ, so
  // the position is carried over as a fraction of the face.
  uint32_t sampleSymmetric(int face, int u, int v) const
  {
    const int sf = symmetricFace(face);
    if (sf == face)
      return sample(face, u, v);
    const int fw = std::max(1, w[face]), fh = std::max(1, h[face]);
    double fu = (std::clamp(u, 0, fw - 1) + 0.5) / fw;
    double fv = (std::clamp(v, 0, fh - 1) + 0.5) / fh;
    if (face / 2 == 0)
      fv = 1.0 - fv; // Top <-> Down
    else
      fu = 1.0 - fu; // Front <-> Back, Right <-> Left
    return sample(sf, int(fu * w[sf]), int(fv * h[sf]));
  }

  bool isBaseSolid(int face, int u, int v) const
  {
    if (face < 0 || face >= kFaceCount) return false;
    const int fw = w[face];
    const int fh = h[face];
    if (fw <= 0 || fh <= 0 || baseAlpha[face].empty()) return false;
    u = std::clamp(u, 0, fw - 1);
    v = std::clamp(v, 0, fh - 1);
    return baseAlpha[face][v * fw + u] > 16;
  }

  bool isFaceSolid(int face, int u, int v) const
  {
    if (hasBaseLayer[face]) {
      return isBaseSolid(face, u, v);
    }
    return doc::rgba_geta(sampleSymmetric(face, u, v)) > 16;
  }
};

// Largest voxel grid per axis. The Base box is measured in voxels, so for
// faces bigger than this the box is capped here and every face lookup
// scales its (u, v) by the box size (never by the face size), which keeps
// shape and texture aligned for faces bigger than this. 128 keeps the
// user's 64x128 faces at one voxel per pixel (128^3 = 2M voxels, ~10 ms).
static constexpr int kMaxVoxelDim = 128;

struct SculptedVoxelVolume {
  int vx = 24;
  int vy = 24;
  int vz = 24;
  std::vector<uint8_t> solid;

  bool isSolid(int x, int y, int z) const
  {
    if (x < 0 || x >= vx || y < 0 || y >= vy || z < 0 || z >= vz)
      return false;
    return solid[(y * vz + z) * vx + x] != 0;
  }

  void setSolid(int x, int y, int z, bool val)
  {
    if (x >= 0 && x < vx && y >= 0 && y < vy && z >= 0 && z < vz)
      solid[(y * vz + z) * vx + x] = (val ? 1 : 0);
  }
};

// Depth interval along one axis from the two opposing faces' depth
// grays. gLo = depth on the lo-side face, gHi = depth on the hi-side
// face. Returns false for a through-hole (either depth pixel
// transparent). Gray 128 = surface stays on the box face; whiter
// extends outward, darker carves inward, black 0 = box centre.
// D = dim/2 per axis. Mirrors the sim1-validated model
// (simulations-2026-11-06/sim1-depth): c_round is std::lround.
static bool depthSurfaceInterval(const SixSurfaceData& data,
                                 int faceLo, int uLo, int vLo,
                                 int faceHi, int uHi, int vHi,
                                 int loBase, int hiBase, int D,
                                 int& lo, int& hi)
{
  uint8_t gLo = 128, gHi = 128;
  if (!data.depthGray(faceLo, uLo, vLo, gLo)) return false;
  if (!data.depthGray(faceHi, uHi, vHi, gHi)) return false;
  if (gLo > 128)      lo = loBase - int(std::lround((float(gLo) - 128.0f) / 128.0f * float(D)));
  else if (gLo < 128) lo = loBase + int(std::lround((128.0f - float(gLo)) / 128.0f * float(D)));
  else                lo = loBase;
  if (gHi > 128)      hi = hiBase + int(std::lround((float(gHi) - 128.0f) / 128.0f * float(D)));
  else if (gHi < 128) hi = hiBase - int(std::lround((128.0f - float(gHi)) / 128.0f * float(D)));
  else                hi = hiBase;
  return true;
}

static SculptedVoxelVolume buildSculptedVolume(const SixSurfaceData& data,
                                              int pivX, int pivY, int pivZ)
{
  SculptedVoxelVolume vol;
  const int dimX = std::min(kMaxVoxelDim, std::max({ data.w[kFaceFront], data.w[kFaceBack], data.w[kFaceTop], data.w[kFaceDown], 8 }));
  const int dimY = std::min(kMaxVoxelDim, std::max({ data.h[kFaceFront], data.h[kFaceBack], data.h[kFaceLeft], data.h[kFaceRight], 8 }));
  const int dimZ = std::min(kMaxVoxelDim, std::max({ data.w[kFaceLeft], data.w[kFaceRight], data.h[kFaceTop], data.h[kFaceDown], 8 }));

  // Depth sculpting (2026-11-07, William: the depth map must carve real
  // mesh, not just bend normals). When a Surface_DEPTH layer exists, the
  // depth gray moves the actual voxel surfaces: 128 = surface stays on
  // the box face, whiter (Plus) extends outward, darker (Minus) carves
  // inward, black 0 = box centre, transparent = through-hole. D = dim/2
  // per axis. Outward extension grows the grid (padded per axis, still
  // capped at 64). Flat 128 changes nothing. The Base silhouette logic
  // below is untouched — Base still always wins.
  const bool sculptDepth = data.hasCustomDepthLayer;
  int extX = 0, extY = 0, extZ = 0;
  if (sculptDepth) {
    auto maxExt = [&data](int face, int D) {
      int m = 0;
      for (int v = 0; v < data.h[face]; ++v) {
        for (int u = 0; u < data.w[face]; ++u) {
          uint8_t g = 0;
          if (data.depthGray(face, u, v, g) && g > 128)
            m = std::max(m, int(std::lround((float(g) - 128.0f) / 128.0f * float(D))));
        }
      }
      return m;
    };
    extX = std::max(maxExt(kFaceRight, dimX / 2), maxExt(kFaceLeft, dimX / 2));
    extY = std::max(maxExt(kFaceTop, dimY / 2), maxExt(kFaceDown, dimY / 2));
    extZ = std::max(maxExt(kFaceFront, dimZ / 2), maxExt(kFaceBack, dimZ / 2));
  }

  vol.vx = std::clamp(dimX + 2 * extX, 8, kMaxVoxelDim);
  vol.vy = std::clamp(dimY + 2 * extY, 8, kMaxVoxelDim);
  vol.vz = std::clamp(dimZ + 2 * extZ, 8, kMaxVoxelDim);
  const int padX = (vol.vx - dimX) / 2;
  const int padY = (vol.vy - dimY) / 2;
  const int padZ = (vol.vz - dimZ) / 2;
  vol.solid.assign(vol.vx * vol.vy * vol.vz, 0);

  const bool hasTop   = data.hasPixels(kFaceTop);
  const bool hasDown  = data.hasPixels(kFaceDown);
  const bool hasFront = data.hasPixels(kFaceFront);
  const bool hasBack  = data.hasPixels(kFaceBack);
  const bool hasRight = data.hasPixels(kFaceRight);
  const bool hasLeft  = data.hasPixels(kFaceLeft);

  const bool activeTopDown   = (hasTop || hasDown);
  const bool activeFrontBack = (hasFront || hasBack);
  const bool activeRightLeft = (hasRight || hasLeft);

  if (!activeTopDown && !activeFrontBack && !activeRightLeft) {
    vol.solid.assign(vol.vx * vol.vy * vol.vz, 1);
    return vol;
  }

  const int pivIndexX = padX + std::clamp(int(pivX * 0.01f * (dimX - 1)), 0, dimX - 1);
  const int pivIndexY = padY + std::clamp(int(pivY * 0.01f * (dimY - 1)), 0, dimY - 1);
  const int pivIndexZ = padZ + std::clamp(int(pivZ * 0.01f * (dimZ - 1)), 0, dimZ - 1);

  const bool onlyTopDown = activeTopDown && !activeFrontBack && !activeRightLeft;
  const bool onlyFrontBack = activeFrontBack && !activeTopDown && !activeRightLeft;
  const bool onlyRightLeft = activeRightLeft && !activeTopDown && !activeFrontBack;

  const int slabYMin = std::max(0, pivIndexY - 1);
  const int slabYMax = std::min(vol.vy - 1, pivIndexY + 1);

  const int slabZMin = std::max(0, pivIndexZ - 1);
  const int slabZMax = std::min(vol.vz - 1, pivIndexZ + 1);

  const int slabXMin = std::max(0, pivIndexX - 1);
  const int slabXMax = std::min(vol.vx - 1, pivIndexX + 1);

  for (int y = 0; y < vol.vy; ++y) {
    if (onlyTopDown && (y < slabYMin || y > slabYMax))
      continue;

    for (int z = 0; z < vol.vz; ++z) {
      if (onlyFrontBack && (z < slabZMin || z > slabZMax))
        continue;

      for (int x = 0; x < vol.vx; ++x) {
        if (onlyRightLeft && (x < slabXMin || x > slabXMax))
          continue;

        // Box coords: the Base box sits centred in the (possibly
        // depth-padded) volume. When nothing is sculpted the pads are
        // 0 and bx/by/bz are just x/y/z, so this changes nothing.
        const int bx = x - padX;
        const int by = y - padY;
        const int bz = z - padZ;

        bool solid = true;

        if (activeTopDown) {
          const int uTop = std::clamp(
            int((float(bx) + 0.5f) / float(dimX) * data.w[kFaceTop]), 0, data.w[kFaceTop] - 1);
          const int vTop = std::clamp(
            int((float(bz) + 0.5f) / float(dimZ) * data.h[kFaceTop]), 0, data.h[kFaceTop] - 1);
          bool topSolid = data.isFaceSolid(kFaceTop, uTop, vTop);

          // Down uv: needed for the Base OR below, and for the depth
          // interval when sculpting (even when Down has no pixels —
          // its depth then reads flat 128 = no displacement).
          const int uDown = std::clamp(
            int((float(bx) + 0.5f) / float(dimX) * data.w[kFaceDown]), 0, data.w[kFaceDown] - 1);
          const int vDown = std::clamp(
            int((float(dimZ - 1 - bz) + 0.5f) / float(dimZ) * data.h[kFaceDown]),
            0, data.h[kFaceDown] - 1);

          if (!data.colLinked[0] && hasDown) {
            const bool downSolid = data.isFaceSolid(kFaceDown, uDown, vDown);
            if (data.hasBaseLayer[kFaceTop] && hasTop) {
              // Base silhouette forms outer bounding hull: unlinked Down cannot expand voxels
            }
            else {
              topSolid = (hasTop ? (topSolid || downSolid) : downSolid);
            }
          }
          if (!topSolid) {
            solid = false;
          }
          else if (sculptDepth) {
            // Top is the lo (y=0) side, Down the hi side.
            int loY = 0, hiY = 0;
            if (!depthSurfaceInterval(data, kFaceTop, uTop, vTop,
                                      kFaceDown, uDown, vDown,
                                      padY, padY + dimY - 1, dimY / 2, loY, hiY) ||
                y < loY || y > hiY) {
              solid = false;
            }
          }
        }

        if (solid && activeFrontBack) {
          const int uFront = std::clamp(
            int((float(bx) + 0.5f) / float(dimX) * data.w[kFaceFront]), 0, data.w[kFaceFront] - 1);
          const int vFront = std::clamp(
            int((float(by) + 0.5f) / float(dimY) * data.h[kFaceFront]), 0, data.h[kFaceFront] - 1);
          bool frontSolid = data.isFaceSolid(kFaceFront, uFront, vFront);

          const int uBack = std::clamp(
            int((float(dimX - 1 - bx) + 0.5f) / float(dimX) * data.w[kFaceBack]),
            0, data.w[kFaceBack] - 1);
          const int vBack = std::clamp(
            int((float(by) + 0.5f) / float(dimY) * data.h[kFaceBack]), 0, data.h[kFaceBack] - 1);

          if (!data.colLinked[1] && hasBack) {
            const bool backSolid = data.isFaceSolid(kFaceBack, uBack, vBack);
            if (data.hasBaseLayer[kFaceFront] && hasFront) {
              // Base silhouette forms outer bounding hull: unlinked Back cannot expand voxels
            }
            else {
              frontSolid = (hasFront ? (frontSolid || backSolid) : backSolid);
            }
          }
          if (!frontSolid) {
            solid = false;
          }
          else if (sculptDepth) {
            // Back is the lo (z=0) side, Front the hi side.
            int loZ = 0, hiZ = 0;
            if (!depthSurfaceInterval(data, kFaceBack, uBack, vBack,
                                      kFaceFront, uFront, vFront,
                                      padZ, padZ + dimZ - 1, dimZ / 2, loZ, hiZ) ||
                z < loZ || z > hiZ) {
              solid = false;
            }
          }
        }

        if (solid && activeRightLeft) {
          const int uRight = std::clamp(
            int((float(dimZ - 1 - bz) + 0.5f) / float(dimZ) * data.w[kFaceRight]),
            0, data.w[kFaceRight] - 1);
          const int vRight = std::clamp(
            int((float(by) + 0.5f) / float(dimY) * data.h[kFaceRight]), 0, data.h[kFaceRight] - 1);
          bool sideSolid = data.isFaceSolid(kFaceRight, uRight, vRight);

          const int uLeft = std::clamp(
            int((float(bz) + 0.5f) / float(dimZ) * data.w[kFaceLeft]), 0, data.w[kFaceLeft] - 1);
          const int vLeft = std::clamp(
            int((float(by) + 0.5f) / float(dimY) * data.h[kFaceLeft]), 0, data.h[kFaceLeft] - 1);

          if (!data.colLinked[2] && hasLeft) {
            const bool leftSolid = data.isFaceSolid(kFaceLeft, uLeft, vLeft);
            if (data.hasBaseLayer[kFaceRight] && hasRight) {
              // Base silhouette forms outer bounding hull: unlinked Left cannot expand voxels
            }
            else {
              sideSolid = (hasRight ? (sideSolid || leftSolid) : leftSolid);
            }
          }
          if (!sideSolid) {
            solid = false;
          }
          else if (sculptDepth) {
            // Left is the lo (x=0) side, Right the hi side.
            int loX = 0, hiX = 0;
            if (!depthSurfaceInterval(data, kFaceLeft, uLeft, vLeft,
                                      kFaceRight, uRight, vRight,
                                      padX, padX + dimX - 1, dimX / 2, loX, hiX) ||
                x < loX || x > hiX) {
              solid = false;
            }
          }
        }

        if (solid) {
          vol.setSolid(x, y, z, true);
        }
      }
    }
  }

  return vol;
}

// Voxel volume cache (2026-11-18). Building a sculpted volume is most of a
// render (measured: 13 ms for a 64^3 object, ~20 ms with a depth sheet,
// against ~2 ms of ray marching), and it only depends on the silhouettes,
// the depth sheet and the pivot, never on the camera. Rotating the preview,
// or painting one object of several, now reuses every unchanged volume.
// Keyed by a hash of exactly the inputs buildSculptedVolume reads; a few
// entries (one per object plus spares), least recently used dropped first.
static uint64_t sculptedVolumeKey(const SixSurfaceData& d, int pivX, int pivY, int pivZ)
{
  uint64_t h = 1469598103934665603ull;
  auto mix = [&h](uint64_t v) {
    h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
  };
  auto mixBytes = [&h](const uint8_t* p, size_t n) {
    uint64_t acc = 0;
    size_t i = 0;
    for (; i + 8 <= n; i += 8) {
      uint64_t w;
      std::memcpy(&w, p + i, 8);
      acc = (acc ^ w) * 0x100000001b3ull + 0x9e3779b97f4a7c15ull;
    }
    for (; i < n; ++i)
      acc = (acc ^ p[i]) * 0x100000001b3ull;
    h ^= acc + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
  };
  mix(uint64_t(pivX));
  mix(uint64_t(pivY));
  mix(uint64_t(pivZ));
  mix(d.hasCustomDepthLayer ? 1 : 0);
  for (int c = 0; c < 3; ++c)
    mix(d.colLinked[c] ? 1 : 0);
  for (int f = 0; f < kFaceCount; ++f) {
    mix(uint64_t(d.w[f]));
    mix(uint64_t(d.h[f]));
    mix(d.hasBaseLayer[f] ? 1 : 0);
    // Solidity reads the alpha of pixels (or baseAlpha with a Base layer).
    mixBytes(reinterpret_cast<const uint8_t*>(d.pixels[f].data()), d.pixels[f].size() * 4);
    mixBytes(d.baseAlpha[f].data(), d.baseAlpha[f].size());
    if (d.hasCustomDepthLayer) {
      mixBytes(d.depthPixels[f].data(), d.depthPixels[f].size());
      mixBytes(d.depthAlpha[f].data(), d.depthAlpha[f].size());
    }
  }
  return h;
}

static std::shared_ptr<const SculptedVoxelVolume> cachedSculptedVolume(const SixSurfaceData& data,
                                                                       int pivX,
                                                                       int pivY,
                                                                       int pivZ)
{
  struct Entry {
    uint64_t key;
    std::shared_ptr<const SculptedVoxelVolume> vol;
  };
  static std::vector<Entry> cache; // most recently used last
  constexpr size_t kMaxEntries = 8;
  const uint64_t key = sculptedVolumeKey(data, pivX, pivY, pivZ);
  for (size_t i = 0; i < cache.size(); ++i) {
    if (cache[i].key == key) {
      Entry e = cache[i];
      cache.erase(cache.begin() + i);
      cache.push_back(e);
      return e.vol;
    }
  }
  auto vol = std::make_shared<const SculptedVoxelVolume>(buildSculptedVolume(data, pivX, pivY, pivZ));
  if (cache.size() >= kMaxEntries)
    cache.erase(cache.begin());
  cache.push_back({ key, vol });
  return vol;
}

static int matchFaceIndex(const std::string& name)
{
  if (name.empty()) return -1;
  std::string lower;
  lower.reserve(name.size());
  for (char c : name)
    lower.push_back(std::tolower(static_cast<unsigned char>(c)));

  if (lower.rfind("surface_", 0) == 0)
    lower = lower.substr(8);
  else if (lower.rfind("face_", 0) == 0)
    lower = lower.substr(5);

  if (lower == "top") return kFaceTop;
  if (lower == "down" || lower == "bottom") return kFaceDown;
  if (lower == "front") return kFaceFront;
  if (lower == "back") return kFaceBack;
  if (lower == "right") return kFaceRight;
  if (lower == "left") return kFaceLeft;
  return -1;
}

// ---------------------------------------------------------------------------
// Objects (2026-11-17, Arin: "separate objects like head and body, each in
// its own layer, Blender-like"). The six views of ONE object constrain each
// other (the model is the intersection of their silhouettes), so a head drawn
// in the same cells as the body is cut by the body's Top view. An object is a
// separate set of six views with its own size and its own place in the model.
//
//  - Object 1 ("root") is what every document already has: Surface_Base /
//    _Texture / _DEPTH at the top level and the slices TOP..LEFT. It is never
//    moved or renamed, so a single-object document behaves exactly as before.
//  - Every other object is a top-level layer group "Object: <name>" whose
//    direct children are its own Surface_Base / _Texture / _DEPTH sheets, and
//    whose user-data property "easymetric" holds its metadata (slice key,
//    model offset, rank, creation order). Its six cells are slices named
//    "<key>:TOP" .. "<key>:LEFT" in its own band of the canvas (bands are
//    stacked downwards under the root's cells). Every cel stays full-canvas at
//    (0, 0), like the root sheets.
//  - The face-size / face-rect / layer helpers consult the CURRENT OBJECT
//    (g_objectScope) instead of growing an object parameter: the studio sets
//    it to its focused object, headless actions set it from `object=`
//    (default root) with EasymetricObjectScope.
// ---------------------------------------------------------------------------
constexpr const char* kObjectGroupPrefix = "Object: ";
constexpr const char* kObjectProp = "easymetric";

struct ObjectMeta {
  std::string key;          // slice prefix; fixed at creation, unique
  int o[3] = { 0, 0, 0 };   // model offset of the box's min corner (X right, Y up, Z to front)
  int rank = -1;            // -1 = auto (smaller box in front)
  int order = 0;            // creation order (ties)
};

static const std::string* objectMetaText(const doc::Layer* lyr)
{
  if (!lyr || !lyr->isGroup())
    return nullptr;
  const auto& maps = lyr->userData().propertiesMaps();
  auto it = maps.find(std::string());
  if (it == maps.end())
    return nullptr;
  auto jt = it->second.find(kObjectProp);
  if (jt == it->second.end())
    return nullptr;
  return std::get_if<std::string>(static_cast<const doc::UserData::VariantBase*>(&jt->second));
}

static bool isObjectGroup(const doc::Layer* lyr)
{
  return objectMetaText(lyr) != nullptr;
}

static bool readObjectMeta(const doc::Layer* lyr, ObjectMeta& m)
{
  const std::string* s = objectMetaText(lyr);
  if (!s)
    return false;
  m = ObjectMeta();
  // "key=Head;o=0,16,0;rank=-1;order=1"
  size_t pos = 0;
  while (pos <= s->size()) {
    size_t end = s->find(';', pos);
    if (end == std::string::npos)
      end = s->size();
    const std::string item = s->substr(pos, end - pos);
    const size_t eq = item.find('=');
    if (eq != std::string::npos) {
      const std::string k = item.substr(0, eq), v = item.substr(eq + 1);
      if (k == "key")
        m.key = v;
      else if (k == "o")
        std::sscanf(v.c_str(), "%d,%d,%d", &m.o[0], &m.o[1], &m.o[2]);
      else if (k == "rank")
        m.rank = std::atoi(v.c_str());
      else if (k == "order")
        m.order = std::atoi(v.c_str());
    }
    pos = end + 1;
  }
  return !m.key.empty();
}

static std::string objectMetaString(const ObjectMeta& m)
{
  return fmt::format("key={};o={},{},{};rank={};order={}", m.key, m.o[0], m.o[1], m.o[2], m.rank,
                     m.order);
}

static bool insideObjectGroup(const doc::Layer* lyr)
{
  for (const doc::Layer* p = lyr ? lyr->parent() : nullptr; p; p = p->parent())
    if (isObjectGroup(p))
      return true;
  return false;
}

struct ObjectGroupRef {
  doc::LayerGroup* group = nullptr;
  ObjectMeta meta;
};

// Top-level object groups, in creation order.
static std::vector<ObjectGroupRef> objectGroups(doc::Sprite* spr)
{
  std::vector<ObjectGroupRef> out;
  if (!spr)
    return out;
  for (doc::Layer* lyr : spr->root()->layers()) {
    ObjectGroupRef r;
    if (readObjectMeta(lyr, r.meta)) {
      r.group = static_cast<doc::LayerGroup*>(lyr);
      out.push_back(r);
    }
  }
  std::stable_sort(out.begin(), out.end(), [](const ObjectGroupRef& a, const ObjectGroupRef& b) {
    return a.meta.order < b.meta.order;
  });
  return out;
}

static bool hasObjectGroups(doc::Sprite* spr)
{
  if (!spr)
    return false;
  for (doc::Layer* lyr : spr->root()->layers())
    if (isObjectGroup(lyr))
      return true;
  return false;
}

static doc::LayerGroup* findObjectGroup(doc::Sprite* spr, const std::string& key)
{
  if (!spr || key.empty())
    return nullptr;
  for (doc::Layer* lyr : spr->root()->layers()) {
    ObjectMeta m;
    if (readObjectMeta(lyr, m) && m.key == key)
      return static_cast<doc::LayerGroup*>(lyr);
  }
  return nullptr;
}

// The object the helpers below work on: {sprite, key}; key "" = root.
static doc::ObjectId g_objectScopeSprite = doc::NullId;
static std::string g_objectScopeKey;

class EasymetricObjectScope {
public:
  EasymetricObjectScope(doc::Sprite* spr, const std::string& key)
    : m_spr(g_objectScopeSprite)
    , m_key(g_objectScopeKey)
  {
    g_objectScopeSprite = spr ? spr->id() : doc::NullId;
    g_objectScopeKey = key;
  }
  ~EasymetricObjectScope()
  {
    g_objectScopeSprite = m_spr;
    g_objectScopeKey = m_key;
  }

private:
  doc::ObjectId m_spr;
  std::string m_key;
};

// Key of the current object for `spr` ("" = root). A key whose group no
// longer exists (undo, deleted) falls back to the root.
static std::string scopeKeyFor(doc::Sprite* spr)
{
  if (!spr || g_objectScopeKey.empty() || spr->id() != g_objectScopeSprite)
    return std::string();
  return findObjectGroup(spr, g_objectScopeKey) ? g_objectScopeKey : std::string();
}

static doc::LayerGroup* scopedObjectGroup(doc::Sprite* spr)
{
  if (!spr || g_objectScopeKey.empty() || spr->id() != g_objectScopeSprite)
    return nullptr;
  return findObjectGroup(spr, g_objectScopeKey);
}

static std::string objectSliceName(const std::string& key, int face)
{
  return key + ":" + kFaceSpecs[face].sliceName;
}

static doc::Slice* findSliceForFace(doc::Sprite* spr, int face)
{
  if (!spr || face < 0 || face >= kFaceCount)
    return nullptr;
  const std::string key = scopeKeyFor(spr);
  if (!key.empty()) {
    const std::string want = objectSliceName(key, face);
    for (auto* slice : spr->slices())
      if (slice && slice->name() == want)
        return slice;
    return nullptr;
  }
  // Root: keyed object slices ("Head:TOP") never match matchFaceIndex.
  for (auto* slice : spr->slices()) {
    if (slice && matchFaceIndex(slice->name()) == face) {
      return slice;
    }
  }
  return nullptr;
}

static doc::LayerImage* findLayerByName(doc::Sprite* spr, const std::string& name)
{
  if (!spr)
    return nullptr;
  if (doc::LayerGroup* grp = scopedObjectGroup(spr)) {
    for (doc::Layer* lyr : grp->layers())
      if (lyr->isImage() && lyr->name() == name)
        return static_cast<doc::LayerImage*>(lyr);
    // An object's sheets are only ever its own: never fall back to the root's.
    if (name.rfind("Surface_", 0) == 0 || name == "Base" || name == "base")
      return nullptr;
  }
  for (doc::Layer* lyr : spr->allLayers()) {
    if (lyr->isImage() && lyr->name() == name && !insideObjectGroup(lyr))
      return static_cast<doc::LayerImage*>(lyr);
  }
  return nullptr;
}

static doc::LayerImage* findOrCreateLayer(app::Doc* doc,
                                          app::DocApi& api,
                                          doc::Sprite* spr,
                                          const std::string& name)
{
  if (auto* existing = findLayerByName(spr, name))
    return existing;
  if (doc::LayerGroup* grp = scopedObjectGroup(spr))
    if (name.rfind("Surface_", 0) == 0)
      return api.newLayer(grp, name);
  return api.newLayer(spr->root(), name);
}

struct SurfaceLayerPair {
  doc::LayerImage* baseLayer = nullptr;
  doc::LayerImage* texLayer = nullptr;
};

static SurfaceLayerPair findSurfaceLayers(doc::Sprite* spr, int face)
{
  SurfaceLayerPair pair;
  if (!spr || face < 0 || face >= kFaceCount)
    return pair;

  const std::string faceName = kFaceSpecs[face].sliceName;
  const std::string baseName = "Surface_" + faceName + "_Base";
  const std::string texName = "Surface_" + faceName + "_Texture";
  const std::string defaultName = "Surface_" + faceName;
  const std::string groupName = "Surface_" + faceName + "_Group";

  // 1. Check if there's a group named Surface_<Face>, Surface_<Face>_Group, or Surface_<Face>_Texture (unpacked SO)
  for (doc::Layer* lyr : spr->allLayers()) {
    if (lyr->isGroup() && (lyr->name() == defaultName || lyr->name() == groupName || lyr->name() == texName ||
                           (face == kFaceDown && (lyr->name() == "Surface_BOTTOM" || lyr->name() == "Surface_Bottom" || lyr->name() == "Surface_BOTTOM_Group" || lyr->name() == "Surface_Bottom_Group")))) {
      doc::LayerGroup* grp = static_cast<doc::LayerGroup*>(lyr);
      for (doc::Layer* child : grp->layers()) {
        if (!child->isImage()) continue;
        doc::LayerImage* childImg = static_cast<doc::LayerImage*>(child);
        const std::string& cn = child->name();
        if (cn == "Texture" || cn == "texture" || cn == texName || child->isSmartObject()) {
          if (!pair.texLayer) pair.texLayer = childImg;
        }
        else if (cn == "Base" || cn == "base" || cn == baseName || !pair.baseLayer) {
          if (!pair.baseLayer) pair.baseLayer = childImg;
        }
      }
      if (pair.baseLayer && pair.texLayer)
        return pair;
    }
  }

  // 2. Check flat layers: Surface_<Face>_Base and Surface_<Face>_Texture
  if (!pair.baseLayer) {
    pair.baseLayer = findLayerByName(spr, baseName);
    if (!pair.baseLayer && face == kFaceDown) {
      pair.baseLayer = findLayerByName(spr, "Surface_BOTTOM_Base");
      if (!pair.baseLayer)
        pair.baseLayer = findLayerByName(spr, "Surface_Bottom_Base");
    }
  }
  if (!pair.texLayer) {
    pair.texLayer = findLayerByName(spr, texName);
    if (!pair.texLayer && face == kFaceDown) {
      pair.texLayer = findLayerByName(spr, "Surface_BOTTOM_Texture");
      if (!pair.texLayer)
        pair.texLayer = findLayerByName(spr, "Surface_Bottom_Texture");
    }
  }

  // 3. Fallback: if no base layer found, check Surface_<Face>
  if (!pair.baseLayer) {
    pair.baseLayer = findLayerByName(spr, defaultName);
    if (!pair.baseLayer && face == kFaceDown) {
      pair.baseLayer = findLayerByName(spr, "Surface_BOTTOM");
      if (!pair.baseLayer)
        pair.baseLayer = findLayerByName(spr, "Surface_Bottom");
    }
  }

  return pair;
}


// ---------------------------------------------------------------------------
// Shared UV sheets: one Base, one Texture and one Depth layer for all faces.
// ---------------------------------------------------------------------------
// Before this change every face had its own Base/Texture layers
// (Surface_Top_Base, Surface_Front_Texture, ...). Now Easymetric keeps
// exactly three image layers:
//
//   Surface_Base     - one sheet, every face paints into its own UV rect
//   Surface_Texture  - one sheet, same UV rects as Base
//   Surface_DEPTH    - one sheet, same UV rects as Base (already shared
//                      by name, but previously written at (0,0) for
//                      every face, so faces overwrote each other)
//
// A face's UV rect is its grid cell in the sheet: FaceSpec already
// carries gridCol/gridRow for the studio layout (Top/Bottom = col 0,
// Front/Back = col 1, Right/Left = col 2; top faces = row 0, bottom
// faces = row 1), so the UV layout is the same 3x2 grid the user sees.
// Each surface panel is then just a window onto its UV rect of the
// shared layers (Editor::setClipSpriteBounds), and the 3D preview
// samples each face from its own rect instead of from its own layer.
// Clicking a panel therefore no longer switches layers at all: it
// switches the UV rect being edited, while the active layer stays
// on the same shared sheet. This is the "single sheet of paper,
// UV-mapped" model: the sheet is not copied per surface, the model
// cuts out the rectangle it needs for each surface.
// ---------------------------------------------------------------------------
struct SharedSurfaceLayers {
  doc::LayerImage* baseLayer = nullptr;
  doc::LayerImage* textureLayer = nullptr;
  doc::LayerImage* depthLayer = nullptr;
};

static SharedSurfaceLayers findSharedLayers(doc::Sprite* spr)
{
  SharedSurfaceLayers shared;
  if (!spr) return shared;
  shared.baseLayer = findLayerByName(spr, "Surface_Base");
  shared.textureLayer = findLayerByName(spr, "Surface_Texture");
  shared.depthLayer = findLayerByName(spr, "Surface_DEPTH");

  if (!shared.baseLayer && !shared.textureLayer && !scopedObjectGroup(spr)) {
    shared.baseLayer = findLayerByName(spr, "Base");
    if (!shared.baseLayer)
      shared.baseLayer = findLayerByName(spr, "base");

    if (!shared.baseLayer) {
      doc::LayerImage* soleImg = nullptr;
      int imgCount = 0;
      for (doc::Layer* lyr : spr->allLayers()) {
        if (lyr->isImage() && !insideObjectGroup(lyr)) {
          soleImg = static_cast<doc::LayerImage*>(lyr);
          imgCount++;
        }
      }
      if (imgCount == 1 && soleImg) {
        shared.baseLayer = soleImg;
      }
    }
  }
  return shared;
}

// Exact UV layout: the canvas (sheet) size is derived from the
// UV mapping, not the other way round. A column is exactly as
// wide as its widest face, a row exactly as tall as its tallest
// face, and the sheet is the sum of the columns/rows. Faces of
// different sizes therefore do NOT force every cell to the
// largest face size (the old uniform 3 x cellW, 2 x cellH grid
// padded every smaller face and, worse, derived cell origins
// from the sprite size, so a manual canvas resize shifted every
// UV rect without changing any face size). The only unavoidable
// padding is where two faces in one column/row differ in size:
// the smaller face's rect is its own exact size, the rest of
// its column/row belongs to its partner face.
// Layout: Top/Front/Right = row 0, Bottom/Back/Left = row 1,
// columns Top/Bottom, Front/Back, Right/Left (kFaceSpecs).
static void sharedFaceSizes(doc::Sprite* spr, int w[kFaceCount], int h[kFaceCount])
{
  const int fallbackW = spr ? std::max(1, spr->width() / 3) : 24;
  const int fallbackH = spr ? std::max(1, spr->height() / 2) : 24;
  for (int f = 0; f < kFaceCount; ++f) {
    w[f] = fallbackW;
    h[f] = fallbackH;
    if (spr) {
      if (doc::Slice* sl = findSliceForFace(spr, f)) {
        if (const auto* key = sl->getByFrame(0)) {
          if (!key->bounds().isEmpty()) {
            w[f] = key->bounds().w;
            h[f] = key->bounds().h;
          }
        }
      }
    }
  }
}

static void sharedLayout(const int w[kFaceCount], const int h[kFaceCount],
                         int colW[3], int rowH[2])
{
  for (int c = 0; c < 3; ++c) colW[c] = 1;
  for (int r = 0; r < 2; ++r) rowH[r] = 1;
  for (int f = 0; f < kFaceCount; ++f) {
    colW[kFaceSpecs[f].gridCol] = std::max(colW[kFaceSpecs[f].gridCol], w[f]);
    rowH[kFaceSpecs[f].gridRow] = std::max(rowH[kFaceSpecs[f].gridRow], h[f]);
  }
}

// UV rect of one face inside the exact layout above.
static gfx::Rect sharedUVRectExact(int face, const int colW[3], const int rowH[2],
                                   int faceW, int faceH)
{
  if (face < 0 || face >= kFaceCount) return gfx::Rect(0, 0, faceW, faceH);
  int x = 0, y = 0;
  for (int c = 0; c < kFaceSpecs[face].gridCol; ++c) x += colW[c];
  for (int r = 0; r < kFaceSpecs[face].gridRow; ++r) y += rowH[r];
  return gfx::Rect(x, y, faceW, faceH);
}

// Top of the current object's band: the root's is 0; an object's is where
// its TOP cell sits (column 0, row 0 of its band).
static int objectBandY(doc::Sprite* spr)
{
  if (scopeKeyFor(spr).empty())
    return 0;
  if (doc::Slice* sl = findSliceForFace(spr, kFaceTop))
    if (const auto* key = sl->getByFrame(0))
      return key->bounds().y;
  return 0;
}

// UV rect for a face in the current document, from its slice size
// and the exact layout (not from the sprite size). This also
// repairs documents whose slices were left at (0, 0) by the old
// per-face layout: every face would otherwise sample the same
// top-left cell of the shared sheet.
static gfx::Rect sharedFaceRect(doc::Sprite* spr, int face)
{
  if (!spr || face < 0 || face >= kFaceCount)
    return gfx::Rect(0, 0, 0, 0);
  int w[kFaceCount], h[kFaceCount];
  sharedFaceSizes(spr, w, h);
  int colW[3], rowH[2];
  sharedLayout(w, h, colW, rowH);
  gfx::Rect rc = sharedUVRectExact(face, colW, rowH, w[face], h[face]);
  rc.y += objectBandY(spr);
  return rc;
}

// Exact sheet size for the current face sizes (0, 0 if no sprite).
// For an object this is its own band's size, not the canvas.
static gfx::Size sharedSheetSize(doc::Sprite* spr)
{
  if (!spr) return gfx::Size(0, 0);
  int w[kFaceCount], h[kFaceCount];
  sharedFaceSizes(spr, w, h);
  int colW[3], rowH[2];
  sharedLayout(w, h, colW, rowH);
  return gfx::Size(colW[0] + colW[1] + colW[2], rowH[0] + rowH[1]);
}

// The canvas size every object's band fits in exactly. Without object
// groups this is sharedSheetSize of the root (unchanged behaviour).
static gfx::Size atlasSize(doc::Sprite* spr)
{
  if (!spr) return gfx::Size(0, 0);
  gfx::Size s;
  {
    EasymetricObjectScope root(spr, std::string());
    s = sharedSheetSize(spr);
  }
  for (const ObjectGroupRef& g : objectGroups(spr)) {
    EasymetricObjectScope sc(spr, g.meta.key);
    const gfx::Size band = sharedSheetSize(spr);
    s.w = std::max(s.w, band.w);
    s.h = std::max(s.h, objectBandY(spr) + band.h);
  }
  return s;
}

// The root object's own settings live on the sprite (it has no group):
// "rank=N;hidden=0|1".
constexpr const char* kRootObjectProp = "easymetric_root";

static void readRootObjectProps(doc::Sprite* spr, int& rank, bool& hidden)
{
  rank = -1;
  hidden = false;
  if (!spr)
    return;
  const auto& maps = spr->userData().propertiesMaps();
  auto it = maps.find(std::string());
  if (it == maps.end())
    return;
  auto jt = it->second.find(kRootObjectProp);
  if (jt == it->second.end())
    return;
  if (const std::string* s =
        std::get_if<std::string>(static_cast<const doc::UserData::VariantBase*>(&jt->second))) {
    int r = -1, h = 0;
    if (std::sscanf(s->c_str(), "rank=%d;hidden=%d", &r, &h) >= 1) {
      rank = r;
      hidden = (h != 0);
    }
  }
}

struct ObjectInfo {
  std::string key;  // "" = the root object
  std::string name; // "Object 1" for the root
  doc::LayerGroup* group = nullptr;
  ObjectMeta meta;
  bool visible = true;
  int box[3] = { 0, 0, 0 }; // X width, Y height, Z depth in pixels (= the renderer's box)
  int priority = 0;         // higher wins where two objects' surfaces coincide
  int64_t size = 0;         // painted volume (frame 0) for the automatic priority
};

static bool paintedIndexBox(doc::Sprite* spr, doc::frame_t fr, int lo[3], int hi[3]);

// Box of the current object: the renderer's halfX/Y/Z * 2.
static void currentObjectBox(doc::Sprite* spr, int box[3])
{
  int w[kFaceCount], h[kFaceCount];
  sharedFaceSizes(spr, w, h);
  box[0] = std::max({ w[kFaceFront], w[kFaceBack], w[kFaceTop], w[kFaceDown] });
  box[1] = std::max({ h[kFaceFront], h[kFaceBack], h[kFaceLeft], h[kFaceRight] });
  box[2] = std::max({ w[kFaceLeft], w[kFaceRight], h[kFaceTop], h[kFaceDown] });
}

static std::string objectDisplayName(const doc::Layer* group)
{
  const std::string& n = group->name();
  const std::string prefix = kObjectGroupPrefix;
  return n.rfind(prefix, 0) == 0 ? n.substr(prefix.size()) : n;
}

// Every object, the root first. Priority (Arin, 2026-11-17: "priority,
// auto by default"): a manual rank (>= 0) beats every automatic one and
// higher ranks win; automatic objects: the smaller box is in front; equal
// boxes: the later created one is in front.
static std::vector<ObjectInfo> resolveObjects(doc::Sprite* spr)
{
  std::vector<ObjectInfo> out;
  if (!spr)
    return out;
  {
    EasymetricObjectScope sc(spr, std::string());
    ObjectInfo r;
    r.name = "Object 1";
    bool hidden = false;
    readRootObjectProps(spr, r.meta.rank, hidden);
    r.visible = !hidden;
    currentObjectBox(spr, r.box);
    out.push_back(r);
  }
  for (const ObjectGroupRef& g : objectGroups(spr)) {
    EasymetricObjectScope sc(spr, g.meta.key);
    ObjectInfo o;
    o.key = g.meta.key;
    o.name = objectDisplayName(g.group);
    o.group = g.group;
    o.meta = g.meta;
    o.visible = g.group->isVisible();
    currentObjectBox(spr, o.box);
    out.push_back(o);
  }
  // "Smaller in front" (Arin, 2026-11-17). Since every object has the model
  // size (2026-11-18), "smaller" is the box around what is painted.
  for (ObjectInfo& o : out) {
    EasymetricObjectScope sc(spr, o.key);
    int lo[3], hi[3];
    o.size = paintedIndexBox(spr, 0, lo, hi)
               ? int64_t(hi[0] - lo[0]) * (hi[1] - lo[1]) * (hi[2] - lo[2])
               : int64_t(o.box[0]) * o.box[1] * o.box[2];
  }
  std::vector<size_t> idx(out.size());
  for (size_t i = 0; i < idx.size(); ++i)
    idx[i] = i;
  auto volume = [&](size_t i) { return out[i].size; };
  std::stable_sort(idx.begin(), idx.end(), [&](size_t a, size_t b) {
    const ObjectInfo& A = out[a];
    const ObjectInfo& B = out[b];
    const bool ma = A.meta.rank >= 0, mb = B.meta.rank >= 0;
    if (ma != mb)
      return !ma; // automatic below manual
    if (ma && A.meta.rank != B.meta.rank)
      return A.meta.rank < B.meta.rank;
    if (volume(a) != volume(b))
      return volume(a) > volume(b); // bigger = lower priority
    return A.meta.order < B.meta.order;
  });
  for (size_t p = 0; p < idx.size(); ++p)
    out[idx[p]].priority = int(p);
  return out;
}

// World centre (renderer frame: X right, Y DOWN, Z to the front) of an
// object's box. The root's box is centred at the origin; an object's offset
// is its box's min corner relative to the root's (X right, Y UP, Z front).
static void objectWorldCentre(const ObjectInfo& root, const ObjectInfo& o, float c[3])
{
  c[0] = -root.box[0] * 0.5f + float(o.meta.o[0]) + o.box[0] * 0.5f;
  c[1] = root.box[1] * 0.5f - float(o.meta.o[1]) - o.box[1] * 0.5f;
  c[2] = -root.box[2] * 0.5f + float(o.meta.o[2]) + o.box[2] * 0.5f;
}

// "Head", "Object: Head", a key, or "Object 1" / "root" / "" -> key ("" =
// root). Returns false when no such object exists.
static bool resolveObjectKey(doc::Sprite* spr, const std::string& nameOrKey, std::string& key)
{
  key.clear();
  if (nameOrKey.empty() || nameOrKey == "Object 1" || nameOrKey == "root")
    return true;
  for (const ObjectGroupRef& g : objectGroups(spr)) {
    if (g.meta.key == nameOrKey || g.group->name() == nameOrKey ||
        objectDisplayName(g.group) == nameOrKey) {
      key = g.meta.key;
      return true;
    }
  }
  return false;
}

// ---------------------------------------------------------------------------
// Machine-readable state ("test without looking at pixels", 2026-11-09).
// EasymetricStudio{action="state", out=<file>} writes this JSON; the in-app
// UI test server (ui_test_server.cpp) returns the same text. Everything an
// agent needs to assert on without a screenshot is here: layers, per-face
// pixel statistics per sheet layer, and (when the studio is open) panel
// geometry, tool mode, symmetry, grid, flip/button state.
// ---------------------------------------------------------------------------
struct RectStats {
  int opaque = 0;
  int64_t sum = 0;       // sum of R+G+B over opaque pixels
  uint64_t hash = 1469598103934665603ull;
};

// Statistics of `rect` (SPRITE coordinates) of a cel, honouring the cel
// position, so it is correct even when the cel is not at (0, 0).
static RectStats rectStatsOfCel(const doc::Cel* cel, const gfx::Rect& rect)
{
  RectStats st;
  if (!cel || !cel->image())
    return st;
  const doc::Image* img = cel->image();
  const int ox = cel->x();
  const int oy = cel->y();
  for (int y = rect.y; y < rect.y + rect.h; ++y) {
    for (int x = rect.x; x < rect.x + rect.w; ++x) {
      uint32_t px = 0;
      const int ix = x - ox;
      const int iy = y - oy;
      if (ix >= 0 && iy >= 0 && ix < img->width() && iy < img->height())
        px = img->getPixel(ix, iy);
      // Image may be RGB or grayscale/indexed; only RGB sheets are used.
      const int a = doc::rgba_geta(px);
      if (a > 0) {
        ++st.opaque;
        st.sum += doc::rgba_getr(px) + doc::rgba_getg(px) + doc::rgba_getb(px);
      }
      st.hash ^= uint64_t(px);
      st.hash *= 1099511628211ull;
    }
  }
  return st;
}

static std::string hex64(uint64_t v)
{
  char buf[24];
  std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(v));
  return buf;
}

static json11::Json rectJson(const gfx::Rect& r)
{
  return json11::Json::array{ r.x, r.y, r.w, r.h };
}

static json11::Json rectStatsJson(const RectStats& st)
{
  return json11::Json::object{ { "opaque", st.opaque },
                               { "sum", double(st.sum) },
                               { "hash", hex64(st.hash) } };
}

// ---------------------------------------------------------------------------
// Ghost hints (2026-11-10, William): "when I draw a rectangle on one surface
// there should be a similar shape on the perpendicular surfaces; where it is
// missing show a faint marker, toggleable like onion skin".
//
// The six surfaces are the six orthographic projections of ONE object, so they
// constrain each other: a pixel of Front at (x, y) says the object occupies
// column x of Top/Down (some depth z) and row y of Right/Left (some z). If that
// column/row of the other surface has NO content at all, the object cannot be
// built there: that is a flaw in the 3D object, and this reports it.
// Opposite surfaces (Top/Down, Front/Back, Right/Left) are NOT compared: they
// may legitimately differ (concave shapes).
//
// World axes per surface (fractions of the object size, 0..1) — identical to the
// mirroring buildSculptedVolume uses:
//   Top:   X = u        Z = v          Down:  X = u        Z = 1 - v
//   Front: X = u        Y = v          Back:  X = 1 - u    Y = v
//   Right: Z = 1 - u    Y = v          Left:  Z = u        Y = v
// ---------------------------------------------------------------------------
struct GhostAxis {
  int axis;      // 0 = X, 1 = Y, 2 = Z
  int coord;     // 0 = along u, 1 = along v
  bool flip;
};

static int ghostAxesOfFace(int face, GhostAxis out[2])
{
  switch (face) {
    case kFaceTop:   out[0] = { 0, 0, false }; out[1] = { 2, 1, false }; return 2;
    case kFaceDown:  out[0] = { 0, 0, false }; out[1] = { 2, 1, true };  return 2;
    case kFaceFront: out[0] = { 0, 0, false }; out[1] = { 1, 1, false }; return 2;
    case kFaceBack:  out[0] = { 0, 0, true };  out[1] = { 1, 1, false }; return 2;
    case kFaceRight: out[0] = { 2, 0, true };  out[1] = { 1, 1, false }; return 2;
    case kFaceLeft:  out[0] = { 2, 0, false }; out[1] = { 1, 1, false }; return 2;
  }
  return 0;
}

// ---------------------------------------------------------------------------
// Resizing a face's pixels consistently in model space (2026-11-18).
// "Index space" of the model box: X grows to the right, Y grows DOWN (row 0 =
// the top), Z grows towards the front; a non-flipped face axis runs along it,
// a flipped one against it (ghostAxesOfFace). Arin's report: a resize that
// copied each view into the top-left of its bigger cell left half of the side
// views empty (a slab instead of a cube), and a top-left anchor is a
// different 3D corner in Back/Right than in Front/Left.
// ---------------------------------------------------------------------------
enum class ResizeMode { Keep, Stretch };

static int floorHalf(int v)
{
  return v >= 0 ? v / 2 : -((-v + 1) / 2);
}

// Where old face pixel (0, 0) lands in the new face rect, when the old box's
// index-space min corner sits at org[axis] inside the new box. The axis a
// face does not show is ignored. org == nullptr: centred on both axes.
static void faceShiftForOrigin(int face,
                               int oldW,
                               int oldH,
                               int newW,
                               int newH,
                               const int* org,
                               int& du,
                               int& dv)
{
  GhostAxis ax[2];
  ghostAxesOfFace(face, ax);
  du = dv = 0;
  for (const GhostAxis& a : ax) {
    const int oldLen = a.coord == 0 ? oldW : oldH;
    const int newLen = a.coord == 0 ? newW : newH;
    const int o = org ? org[a.axis] : floorHalf(newLen - oldLen);
    (a.coord == 0 ? du : dv) = a.flip ? newLen - o - oldLen : o;
  }
}

// Copies one face from srcRc (src image coordinates) into dstRc of dst.
// Keep: unscaled, placed by faceShiftForOrigin (centred when org is null;
// whatever falls outside dstRc is cropped). Stretch: nearest-neighbour,
// sampled at pixel centres in index space, so a face and its mirror face
// pick the same source pixels.
static void copyFacePixels(doc::Image* dst,
                           const gfx::Rect& dstRc,
                           const doc::Image* src,
                           const gfx::Rect& srcRc,
                           int face,
                           ResizeMode mode,
                           const int* org = nullptr)
{
  if (!dst || !src || dstRc.isEmpty() || srcRc.isEmpty())
    return;
  if (mode == ResizeMode::Stretch) {
    GhostAxis ax[2];
    ghostAxesOfFace(face, ax);
    bool flipU = false, flipV = false;
    for (const GhostAxis& a : ax)
      (a.coord == 0 ? flipU : flipV) = a.flip;
    auto mapIdx = [](int i, int n, int o, bool flip) {
      const int m = flip ? n - 1 - i : i;
      const int sm = int((int64_t(2 * m + 1) * o) / (int64_t(2) * n));
      return flip ? o - 1 - sm : sm;
    };
    for (int v = 0; v < dstRc.h; ++v) {
      const int sy = srcRc.y + mapIdx(v, dstRc.h, srcRc.h, flipV);
      if (sy < 0 || sy >= src->height())
        continue;
      for (int u = 0; u < dstRc.w; ++u) {
        const int sx = srcRc.x + mapIdx(u, dstRc.w, srcRc.w, flipU);
        if (sx < 0 || sx >= src->width())
          continue;
        doc::put_pixel(dst, dstRc.x + u, dstRc.y + v, doc::get_pixel(src, sx, sy));
      }
    }
    return;
  }
  int du = 0, dv = 0;
  faceShiftForOrigin(face, srcRc.w, srcRc.h, dstRc.w, dstRc.h, org, du, dv);
  for (int v = 0; v < srcRc.h; ++v) {
    const int ty = dv + v;
    const int sy = srcRc.y + v;
    if (ty < 0 || ty >= dstRc.h || sy < 0 || sy >= src->height())
      continue;
    for (int u = 0; u < srcRc.w; ++u) {
      const int tx = du + u;
      const int sx = srcRc.x + u;
      if (tx < 0 || tx >= dstRc.w || sx < 0 || sx >= src->width())
        continue;
      doc::put_pixel(dst, dstRc.x + tx, dstRc.y + ty, doc::get_pixel(src, sx, sy));
    }
  }
}

// Index-space bounds [lo, hi) of what is painted (Base alpha > 0) in the
// current object's cells on frame `fr`, merged over all six views. Returns
// false (lo/hi untouched) when nothing is painted.
static bool paintedIndexBox(doc::Sprite* spr, doc::frame_t fr, int lo[3], int hi[3])
{
  const SharedSurfaceLayers sh = findSharedLayers(spr);
  const doc::Cel* cel = sh.baseLayer ? sh.baseLayer->cel(fr) : nullptr;
  const doc::Image* img = cel ? cel->image() : nullptr;
  if (!img)
    return false;
  int l[3] = { INT_MAX, INT_MAX, INT_MAX }, h[3] = { INT_MIN, INT_MIN, INT_MIN };
  bool any = false;
  for (int f = 0; f < kFaceCount; ++f) {
    const gfx::Rect rc = sharedFaceRect(spr, f);
    int u0 = INT_MAX, u1 = INT_MIN, v0 = INT_MAX, v1 = INT_MIN;
    for (int v = 0; v < rc.h; ++v) {
      const int y = rc.y + v - cel->y();
      if (y < 0 || y >= img->height())
        continue;
      for (int u = 0; u < rc.w; ++u) {
        const int x = rc.x + u - cel->x();
        if (x < 0 || x >= img->width())
          continue;
        const doc::color_t c = doc::get_pixel(img, x, y);
        const bool solid = img->pixelFormat() == doc::IMAGE_RGB ? doc::rgba_geta(c) > 0
                           : img->pixelFormat() == doc::IMAGE_GRAYSCALE
                             ? doc::graya_geta(c) > 0
                             : c != img->maskColor();
        if (!solid)
          continue;
        u0 = std::min(u0, u);
        u1 = std::max(u1, u + 1);
        v0 = std::min(v0, v);
        v1 = std::max(v1, v + 1);
      }
    }
    if (u0 > u1)
      continue;
    any = true;
    GhostAxis ax[2];
    ghostAxesOfFace(f, ax);
    for (const GhostAxis& a : ax) {
      const int len = a.coord == 0 ? rc.w : rc.h;
      int a0 = a.coord == 0 ? u0 : v0, a1 = a.coord == 0 ? u1 : v1;
      if (a.flip) {
        const int t = len - a1;
        a1 = len - a0;
        a0 = t;
      }
      l[a.axis] = std::min(l[a.axis], a0);
      h[a.axis] = std::max(h[a.axis], a1);
    }
  }
  if (!any)
    return false;
  for (int a = 0; a < 3; ++a) {
    if (l[a] > h[a]) { // an axis no painted view shows: the whole box
      int w[kFaceCount], hh[kFaceCount];
      sharedFaceSizes(spr, w, hh);
      int box[3];
      box[0] = w[kFaceFront];
      box[1] = hh[kFaceFront];
      box[2] = w[kFaceRight];
      l[a] = 0;
      h[a] = box[a];
    }
    lo[a] = l[a];
    hi[a] = h[a];
  }
  return true;
}

static int ghostOppositeFace(int face)
{
  switch (face) {
    case kFaceTop:   return kFaceDown;
    case kFaceDown:  return kFaceTop;
    case kFaceFront: return kFaceBack;
    case kFaceBack:  return kFaceFront;
    case kFaceRight: return kFaceLeft;
    default:         return kFaceRight;
  }
}

struct GhostData {
  // [dst][src] -> 1 where dst's column / row is demanded by src but empty.
  std::vector<uint8_t> col[kFaceCount][kFaceCount];
  std::vector<uint8_t> row[kFaceCount][kFaceCount];
  int w[kFaceCount] = {};
  int h[kFaceCount] = {};
  bool any = false;

  bool colMissing(int dst, int c) const
  {
    for (int s = 0; s < kFaceCount; ++s)
      if (c >= 0 && c < int(col[dst][s].size()) && col[dst][s][size_t(c)])
        return true;
    return false;
  }
  bool rowMissing(int dst, int r) const
  {
    for (int s = 0; s < kFaceCount; ++s)
      if (r >= 0 && r < int(row[dst][s].size()) && row[dst][s][size_t(r)])
        return true;
    return false;
  }
};

// Content = alpha > 16 on the Base sheet (the silhouette the preview carves
// from), else the Texture sheet when there is no Base.
// `fr[f]` is the frame each face's panel shows (panels can have their own
// frame). `colLinked[c]`: the lower face of a linked column is only a copy of
// the upper one (the preview and the doc sync do not mirror it), so it is
// neither a source nor a destination of hints. `edgesU/V[f]` are cell
// boundaries (0 .. size) along that face's u / v axis when its panel grid is
// on; empty = every pixel is its own cell (pixel-exact hints). In cell mode a
// whole destination cell is flagged when it has no content at all.
// Which columns / rows of each face hold content (alpha > 16 on Base, else
// Texture), in face pixels. Shared by the ghost hints and auto-align so both
// see exactly the same occupancy. Returns false when there is no sheet.
struct FaceOccupancy {
  int w[kFaceCount] = {};
  int h[kFaceCount] = {};
  std::vector<uint8_t> colHas[kFaceCount];
  std::vector<uint8_t> rowHas[kFaceCount];
};

static bool computeFaceOccupancy(Doc* doc, const doc::frame_t fr[kFaceCount], FaceOccupancy& o)
{
  doc::Sprite* spr = doc ? doc->sprite() : nullptr;
  if (!spr)
    return false;
  SharedSurfaceLayers shared = findSharedLayers(spr);
  doc::LayerImage* lyr = shared.baseLayer ? shared.baseLayer : shared.textureLayer;
  if (!lyr)
    return false;

  // Opaque test that works in every colour mode (a plain rgba_geta read made
  // Indexed and Grayscale sprites look empty).
  auto isOpaque = [spr](const doc::Image* img, int x, int y) {
    const doc::color_t c = img->getPixel(x, y);
    switch (img->pixelFormat()) {
      case doc::IMAGE_RGB:       return doc::rgba_geta(c) > 16;
      case doc::IMAGE_GRAYSCALE: return doc::graya_geta(c) > 16;
      case doc::IMAGE_INDEXED:   return c != spr->transparentColor();
      default:                   return false;
    }
  };

  for (int f = 0; f < kFaceCount; ++f) {
    const gfx::Rect rc = sharedFaceRect(spr, f);
    o.w[f] = rc.w;
    o.h[f] = rc.h;
    o.colHas[f].assign(size_t(std::max(0, rc.w)), 0);
    o.rowHas[f].assign(size_t(std::max(0, rc.h)), 0);
    doc::Cel* cel = lyr->cel(fr[f]);
    if (!cel || !cel->image())
      continue;
    const doc::Image* img = cel->image();
    for (int v = 0; v < rc.h; ++v) {
      for (int u = 0; u < rc.w; ++u) {
        const int ix = rc.x + u - cel->x();
        const int iy = rc.y + v - cel->y();
        if (ix < 0 || iy < 0 || ix >= img->width() || iy >= img->height())
          continue;
        if (isOpaque(img, ix, iy)) {
          o.colHas[f][size_t(u)] = 1;
          o.rowHas[f][size_t(v)] = 1;
        }
      }
    }
  }
  return true;
}

static GhostData computeGhost(Doc* doc,
                              const doc::frame_t fr[kFaceCount],
                              const bool colLinked[3],
                              const std::vector<int> edgesU[kFaceCount],
                              const std::vector<int> edgesV[kFaceCount])
{
  GhostData g;
  FaceOccupancy occ;
  if (!computeFaceOccupancy(doc, fr, occ))
    return g;

  auto hidden = [&](int f) { return (f & 1) && colLinked[f >> 1]; };

  using Range = std::pair<int, int>;
  auto cellsOf = [](const std::vector<int>& edges, int size) {
    std::vector<Range> out;
    if (edges.size() < 3) {
      for (int i = 0; i < size; ++i)
        out.emplace_back(i, i + 1);
      return out;
    }
    for (size_t i = 0; i + 1 < edges.size(); ++i) {
      const int a = std::clamp(edges[i], 0, size);
      const int b = std::clamp(edges[i + 1], 0, size);
      if (b > a)
        out.emplace_back(a, b);
    }
    return out;
  };

  std::vector<Range> cellU[kFaceCount], cellV[kFaceCount];
  const std::vector<uint8_t>* colHas = occ.colHas;
  const std::vector<uint8_t>* rowHas = occ.rowHas;
  for (int f = 0; f < kFaceCount; ++f) {
    g.w[f] = occ.w[f];
    g.h[f] = occ.h[f];
    cellU[f] = cellsOf(edgesU[f], g.w[f]);
    cellV[f] = cellsOf(edgesV[f], g.h[f]);
  }

  for (int d = 0; d < kFaceCount; ++d)
    for (int s = 0; s < kFaceCount; ++s) {
      g.col[d][s].assign(size_t(std::max(0, g.w[d])), 0);
      g.row[d][s].assign(size_t(std::max(0, g.h[d])), 0);
    }

  // True when any pixel inside `r` of `has` is set.
  auto rangeHas = [](const std::vector<uint8_t>& has, const Range& r) {
    for (int i = r.first; i < r.second && i < int(has.size()); ++i)
      if (has[size_t(i)])
        return true;
    return false;
  };
  auto cellIndexAt = [](const std::vector<Range>& cells, int idx) {
    for (size_t i = 0; i < cells.size(); ++i)
      if (idx >= cells[i].first && idx < cells[i].second)
        return int(i);
    return -1;
  };

  for (int s = 0; s < kFaceCount; ++s) {
    if (hidden(s) || g.w[s] <= 0 || g.h[s] <= 0)
      continue;
    GhostAxis sAxes[2];
    ghostAxesOfFace(s, sAxes);
    for (int k = 0; k < 2; ++k) {
      const GhostAxis& sa = sAxes[k];
      const int sSize = (sa.coord == 0) ? g.w[s] : g.h[s];
      const std::vector<Range>& sCells = (sa.coord == 0) ? cellU[s] : cellV[s];
      const std::vector<uint8_t>& sHas = (sa.coord == 0) ? colHas[s] : rowHas[s];
      for (const Range& sc : sCells) {
        if (!rangeHas(sHas, sc))
          continue; // nothing drawn in this cell of the source: it demands nothing
        double a = (0.5 * (sc.first + sc.second)) / double(sSize);
        if (sa.flip)
          a = 1.0 - a;
        for (int d = 0; d < kFaceCount; ++d) {
          if (d == s || d == ghostOppositeFace(s) || hidden(d) || g.w[d] <= 0 || g.h[d] <= 0)
            continue;
          GhostAxis dAxes[2];
          ghostAxesOfFace(d, dAxes);
          for (int m = 0; m < 2; ++m) {
            const GhostAxis& da = dAxes[m];
            if (da.axis != sa.axis)
              continue;
            const double pos = da.flip ? 1.0 - a : a;
            const bool alongU = (da.coord == 0);
            const int dSize = alongU ? g.w[d] : g.h[d];
            const std::vector<Range>& dCells = alongU ? cellU[d] : cellV[d];
            const std::vector<uint8_t>& dHas = alongU ? colHas[d] : rowHas[d];
            const int idx = std::clamp(int(pos * dSize), 0, dSize - 1);
            const int ci = cellIndexAt(dCells, idx);
            if (ci < 0 || rangeHas(dHas, dCells[size_t(ci)]))
              continue;
            auto& mark = alongU ? g.col[d][s] : g.row[d][s];
            for (int i = dCells[size_t(ci)].first; i < dCells[size_t(ci)].second; ++i)
              mark[size_t(i)] = 1;
            g.any = true;
          }
        }
      }
    }
  }
  return g;
}

// ---------------------------------------------------------------------------
// Auto-align (2026-11-16, Arin: the editor owns alignment). After a stroke in
// a view, the view's occupied extent on each of its two model axes is
// compared with the reference extent: Front for X (width) and Y (height), Top
// for Z (depth). Occupancy is the ghost hints' (computeFaceOccupancy) and the
// view -> model axes are the ghost table (ghostAxesOfFace), which matches the
// renderer, so there is no second mapping here.
//   same length, different start  -> offset: shift the view (<= limit px)
//   different length              -> shape change: move nothing, readout
//   drawn view is the reference    -> move nothing, readout of the others
// The shift is applied by applyFaceShift as its own undoable transaction.
// ---------------------------------------------------------------------------
struct AlignPlan {
  int face = -1;
  int du = 0; // view-pixel shift along u (columns), + = right
  int dv = 0; // view-pixel shift along v (rows), + = down
  std::vector<std::string> shifted; // e.g. "+2 px depth"
  std::vector<std::string> notes;   // readouts, nothing moved
};

static const char* alignAxisName(int axis)
{
  return axis == 0 ? "width" : axis == 1 ? "height" : "depth";
}

static int alignReferenceFace(int axis)
{
  return axis == 2 ? int(kFaceTop) : int(kFaceFront);
}

// Occupied extent of face f along model axis `axis`, in model pixels (flip
// applied). False if the face has nothing there or does not show that axis.
static bool alignExtent(const FaceOccupancy& o, int f, int axis, int& first, int& len, int& size)
{
  GhostAxis ax[2];
  ghostAxesOfFace(f, ax);
  for (const GhostAxis& a : ax) {
    if (a.axis != axis)
      continue;
    const std::vector<uint8_t>& has = (a.coord == 0) ? o.colHas[f] : o.rowHas[f];
    size = int(has.size());
    int lo = -1, hi = -1;
    for (int i = 0; i < size; ++i)
      if (has[size_t(i)]) {
        if (lo < 0)
          lo = i;
        hi = i;
      }
    if (lo < 0)
      return false;
    first = a.flip ? size - 1 - hi : lo;
    len = hi - lo + 1;
    return true;
  }
  return false;
}

// Direction of model axis `axis` in face f's pixels: +1 when the model
// coordinate grows with u / v, -1 when the face shows it flipped.
static int alignViewSign(int f, int axis)
{
  GhostAxis ax[2];
  ghostAxesOfFace(f, ax);
  for (const GhostAxis& a : ax)
    if (a.axis == axis)
      return a.flip ? -1 : 1;
  return 1;
}

static AlignPlan planFaceAlign(Doc* doc,
                               const doc::frame_t fr[kFaceCount],
                               const bool colLinked[3],
                               int face,
                               int limit)
{
  AlignPlan plan;
  plan.face = face;
  FaceOccupancy o;
  if (face < 0 || face >= kFaceCount || !computeFaceOccupancy(doc, fr, o))
    return plan;
  auto hidden = [&](int f) { return (f & 1) && colLinked[f >> 1]; };
  if (hidden(face))
    return plan; // a linked lower face is only a mirror of its partner

  GhostAxis ax[2];
  ghostAxesOfFace(face, ax);
  for (const GhostAxis& a : ax) {
    const int ref = alignReferenceFace(a.axis);
    const char* axisName = alignAxisName(a.axis);
    int rFirst = 0, rLen = 0, rSize = 0;
    const bool refHas = alignExtent(o, ref, a.axis, rFirst, rLen, rSize);

    if (face == ref) {
      // Drawn in the reference: never move the others, report them.
      if (!refHas)
        continue;
      for (int g = 0; g < kFaceCount; ++g) {
        int gFirst = 0, gLen = 0, gSize = 0;
        if (g == face || hidden(g) || !alignExtent(o, g, a.axis, gFirst, gLen, gSize))
          continue;
        if (gLen != rLen)
          plan.notes.push_back(fmt::format("{}: {} {} px, {} says {} px",
                                           kFaceSpecs[g].displayName, axisName, gLen,
                                           kFaceSpecs[ref].displayName, rLen));
        else if (gSize == rSize && gFirst != rFirst)
          plan.notes.push_back(fmt::format("{}: {} offset {:+d} px from {}",
                                           kFaceSpecs[g].displayName, axisName,
                                           (gFirst - rFirst) * alignViewSign(g, a.axis),
                                           kFaceSpecs[ref].displayName));
      }
      continue;
    }

    int vFirst = 0, vLen = 0, vSize = 0;
    if (!refHas || !alignExtent(o, face, a.axis, vFirst, vLen, vSize))
      continue;
    if (vLen != rLen) {
      plan.notes.push_back(fmt::format("{}: {} {} px, {} says {} px",
                                       kFaceSpecs[face].displayName, axisName, vLen,
                                       kFaceSpecs[ref].displayName, rLen));
      continue;
    }
    if (vSize != rSize) {
      plan.notes.push_back(fmt::format("{}: {} not aligned, the face is {} px and {} is {} px",
                                       kFaceSpecs[face].displayName, axisName, vSize,
                                       kFaceSpecs[ref].displayName, rSize));
      continue;
    }
    const int delta = rFirst - vFirst; // in model pixels
    if (delta == 0)
      continue;
    // Shifts and offsets are reported in the view's own pixels (+ = right
    // or down in that panel), which is what the user sees move.
    const int px = a.flip ? -delta : delta;
    if (std::abs(delta) > limit) {
      plan.notes.push_back(fmt::format("{}: {} offset {:+d} px from {} (over the {} px "
                                       "auto-align limit, left as drawn)",
                                       kFaceSpecs[face].displayName, axisName, -px,
                                       kFaceSpecs[ref].displayName, limit));
      continue;
    }
    if (a.coord == 0)
      plan.du += px;
    else
      plan.dv += px;
    plan.shifted.push_back(fmt::format("{:+d} px {}", px, axisName));
  }
  return plan;
}

// Shifts face `face` by (du, dv) view pixels on every surface sheet (Base,
// Texture, Depth) of frame `fr`. When its column is linked, the partner face
// is rewritten as the mirrored copy (the flips syncOpposingCels and
// extractSixSurfaces use). One transaction = one undo step.
static bool applyFaceShift(Doc* doc,
                           doc::frame_t fr,
                           int face,
                           int du,
                           int dv,
                           const bool colLinked[3],
                           const std::string& label)
{
  doc::Sprite* spr = doc ? doc->sprite() : nullptr;
  if (!spr || (du == 0 && dv == 0) || face < 0 || face >= kFaceCount)
    return false;
  SharedSurfaceLayers shared = findSharedLayers(spr);
  if (!shared.baseLayer && !shared.textureLayer)
    return false;
  const gfx::Rect rc = sharedFaceRect(spr, face);
  const int col = face / 2;
  const bool mirror = !(face & 1) && colLinked[col];
  const gfx::Rect prc = mirror ? sharedFaceRect(spr, face + 1) : gfx::Rect();
  const bool flipV = (col == 0);

  Tx tx(doc, label, ModifyDocument);
  DocApi api = doc->getApi(tx);
  auto shiftLayer = [&](doc::LayerImage* lyr, bool replicateEdges) {
    if (!lyr)
      return;
    doc::Cel* cel = lyr->cel(fr);
    if (!cel || !cel->image())
      return;
    int ox = cel->x(), oy = cel->y();
    const gfx::Rect need = mirror ? rc.createUnion(prc) : rc;
    doc::ImageRef img;
    if (cel->bounds().contains(need)) {
      img.reset(doc::Image::createCopy(cel->image()));
    }
    else {
      img.reset(doc::Image::create(cel->image()->pixelFormat(), spr->width(), spr->height()));
      doc::clear_image(img.get(), img->maskColor());
      doc::copy_image(img.get(), cel->image(), ox, oy);
      ox = oy = 0;
    }
    const doc::color_t empty = img->maskColor();
    std::unique_ptr<doc::Image> src(doc::Image::createCopy(img.get()));
    auto at = [&](int u, int v) -> doc::color_t {
      if (replicateEdges) {
        u = std::clamp(u, 0, rc.w - 1);
        v = std::clamp(v, 0, rc.h - 1);
      }
      else if (u < 0 || v < 0 || u >= rc.w || v >= rc.h)
        return empty;
      const int x = rc.x + u - ox, y = rc.y + v - oy;
      if (x < 0 || y < 0 || x >= src->width() || y >= src->height())
        return empty;
      return doc::get_pixel(src.get(), x, y);
    };
    for (int v = 0; v < rc.h; ++v)
      for (int u = 0; u < rc.w; ++u)
        doc::put_pixel(img.get(), rc.x + u - ox, rc.y + v - oy, at(u - du, v - dv));
    if (mirror) {
      const int cw = std::min(rc.w, prc.w), ch = std::min(rc.h, prc.h);
      for (int v = 0; v < ch; ++v)
        for (int u = 0; u < cw; ++u) {
          const int su = flipV ? u : cw - 1 - u;
          const int sv = flipV ? ch - 1 - v : v;
          doc::put_pixel(img.get(), prc.x + u - ox, prc.y + v - oy,
                         doc::get_pixel(img.get(), rc.x + su - ox, rc.y + sv - oy));
        }
    }
    api.replaceImage(spr, cel->imageRef(), img);
    if (ox != cel->x() || oy != cel->y())
      api.setCelPosition(spr, cel, ox, oy);
  };
  shiftLayer(shared.baseLayer, false);
  shiftLayer(shared.textureLayer, false);
  shiftLayer(shared.depthLayer, true);
  tx.commit();
  return true;
}

static std::string alignPlanSummary(const AlignPlan& p)
{
  std::string msg;
  if (!p.shifted.empty()) {
    msg = fmt::format("{} view aligned", kFaceSpecs[p.face].displayName);
    for (size_t i = 0; i < p.shifted.size(); ++i)
      msg += (i == 0 ? " " : ", ") + p.shifted[i];
  }
  for (const std::string& n : p.notes)
    msg += (msg.empty() ? "" : " | ") + n;
  return msg;
}

static json11::Json alignPlanJson(const AlignPlan& p, bool applied)
{
  json11::Json::array shifted(p.shifted.begin(), p.shifted.end());
  json11::Json::array notes(p.notes.begin(), p.notes.end());
  return json11::Json::object{
    { "face", p.face >= 0 ? kFaceSpecs[p.face].sliceName : "" },
    { "du", p.du },
    { "dv", p.dv },
    { "applied", applied },
    { "shifted", shifted },
    { "notes", notes },
    { "message", alignPlanSummary(p) },
  };
}

// Document-level state: works headless (no UI needed).
static json11::Json::object docStateObject(Doc* doc, doc::Layer* activeLayer, doc::frame_t fr)
{
  json11::Json::object o;
  doc::Sprite* spr = doc ? doc->sprite() : nullptr;
  if (!spr) {
    o["sprite"] = nullptr;
    return o;
  }
  o["sprite"] = json11::Json::object{ { "width", spr->width() },
                                      { "height", spr->height() },
                                      { "frames", int(spr->totalFrames()) },
                                      { "frame", int(fr) },
                                      { "filename", doc->filename() } };
  json11::Json::array layers;
  for (doc::Layer* lyr : spr->allLayers()) {
    json11::Json::object lo;
    lo["name"] = lyr->name();
    lo["kind"] = lyr->isGroup() ? "group" : (lyr->isImage() ? "image" : "other");
    lo["visible"] = lyr->isVisible();
    lo["editable"] = lyr->isEditable();
    lo["active"] = (lyr == activeLayer);
    if (lyr->isImage()) {
      doc::Cel* cel = lyr->cel(fr);
      lo["hasCel"] = (cel != nullptr);
      if (cel)
        lo["cel"] = rectJson(gfx::Rect(cel->x(), cel->y(), cel->image()->width(),
                                       cel->image()->height()));
    }
    layers.push_back(lo);
  }
  o["layers"] = layers;
  o["activeLayer"] = activeLayer ? activeLayer->name() : std::string();

  SharedSurfaceLayers shared = findSharedLayers(spr);
  json11::Json::object faces;
  doc::frame_t ghostFrames[kFaceCount];
  for (int f = 0; f < kFaceCount; ++f)
    ghostFrames[f] = fr;
  const bool ghostNoLinks[3] = { false, false, false };
  const std::vector<int> ghostNoEdges[kFaceCount];
  const GhostData ghost = computeGhost(doc, ghostFrames, ghostNoLinks, ghostNoEdges, ghostNoEdges);
  const struct { const char* key; doc::LayerImage* lyr; } sheets[3] = {
    { "base", shared.baseLayer }, { "texture", shared.textureLayer }, { "depth", shared.depthLayer }
  };
  for (int f = 0; f < kFaceCount; ++f) {
    const gfx::Rect rc = sharedFaceRect(spr, f);
    json11::Json::object fo;
    fo["uv"] = rectJson(rc);
    for (const auto& sh : sheets) {
      if (sh.lyr && sh.lyr->cel(fr))
        fo[sh.key] = rectStatsJson(rectStatsOfCel(sh.lyr->cel(fr), rc));
    }
    {
      json11::Json::array gc, gr;
      for (int c = 0; c < ghost.w[f]; ++c)
        if (ghost.colMissing(f, c))
          gc.push_back(c);
      for (int r = 0; r < ghost.h[f]; ++r)
        if (ghost.rowMissing(f, r))
          gr.push_back(r);
      fo["ghost"] = json11::Json::object{ { "cols", gc }, { "rows", gr } };
    }
    faces[kFaceSpecs[f].sliceName] = fo;
  }
  o["faces"] = faces;
  o["hasSurfaces"] = (shared.baseLayer != nullptr);
  o["hasDepthLayer"] = (shared.depthLayer != nullptr);

  // Objects (2026-11-17): every object, the root first, with its cells and
  // per-face Base statistics (the "faces" above are the current object's).
  json11::Json::array objs;
  for (const ObjectInfo& info : resolveObjects(spr)) {
    EasymetricObjectScope sc(spr, info.key);
    SharedSurfaceLayers ol = findSharedLayers(spr);
    json11::Json::object jo;
    jo["name"] = info.name;
    jo["key"] = info.key;
    jo["group"] = info.group ? info.group->name() : std::string();
    jo["visible"] = info.visible;
    jo["offset"] = json11::Json::array{ info.meta.o[0], info.meta.o[1], info.meta.o[2] };
    jo["rank"] = info.meta.rank;
    jo["priority"] = info.priority;
    jo["box"] = json11::Json::array{ info.box[0], info.box[1], info.box[2] };
    jo["bandY"] = objectBandY(spr);
    jo["hasDepthLayer"] = (ol.depthLayer != nullptr);
    json11::Json::object of;
    for (int f = 0; f < kFaceCount; ++f) {
      const gfx::Rect rc = sharedFaceRect(spr, f);
      json11::Json::object fo{ { "uv", rectJson(rc) } };
      if (ol.baseLayer && ol.baseLayer->cel(fr))
        fo["base"] = rectStatsJson(rectStatsOfCel(ol.baseLayer->cel(fr), rc));
      if (ol.textureLayer && ol.textureLayer->cel(fr))
        fo["texture"] = rectStatsJson(rectStatsOfCel(ol.textureLayer->cel(fr), rc));
      if (ol.depthLayer && ol.depthLayer->cel(fr))
        fo["depth"] = rectStatsJson(rectStatsOfCel(ol.depthLayer->cel(fr), rc));
      of[kFaceSpecs[f].sliceName] = fo;
    }
    jo["faces"] = of;
    objs.push_back(jo);
  }
  o["objects"] = objs;
  {
    // The size every object shares (2026-11-18): Object 1's box.
    EasymetricObjectScope root(spr, std::string());
    int w[kFaceCount], h[kFaceCount];
    sharedFaceSizes(spr, w, h);
    o["modelSize"] = json11::Json::array{ w[kFaceFront], h[kFaceFront], w[kFaceRight] };
  }
  return o;
}

// Mirror one face's pixels within its UV rect on frame `fr`, on every shared
// sheet layer that has a cel on that frame (Base, Texture, Depth), in ONE
// transaction (= one undo step). Used by the panel FH/FV buttons and by the
// headless action flipFace (so it is testable without the UI).
//
// Coordinates: the face rect is in SPRITE coordinates; FlipImage works in
// IMAGE coordinates, so it is converted with the cel position and clipped to
// the cel image. A cel that does not fully contain the face is grown to the
// whole sprite first (transparent padding), otherwise the mirrored pixels
// could land outside the cel and be lost.
// Returns false (and changes nothing) if there is nothing to flip.
static bool flipFaceOnDoc(Doc* doc, int face, bool horizontal, doc::frame_t fr)
{
  if (!doc || !doc->sprite() || face < 0 || face >= kFaceCount)
    return false;
  doc::Sprite* spr = doc->sprite();
  const gfx::Rect faceRect = sharedFaceRect(spr, face);
  if (faceRect.isEmpty())
    return false;
  SharedSurfaceLayers shared = findSharedLayers(spr);
  doc::LayerImage* layers[3] = { shared.baseLayer, shared.textureLayer, shared.depthLayer };
  bool any = false;
  for (doc::LayerImage* lyr : layers) {
    if (lyr && lyr->cel(fr) && lyr->cel(fr)->image()) {
      any = true;
      break;
    }
  }
  if (!any)
    return false;
  Tx tx(doc,
        horizontal ? "Flip Easymetric Face Horizontally" : "Flip Easymetric Face Vertically",
        ModifyDocument);
  const doc::algorithm::FlipType type =
    horizontal ? doc::algorithm::FlipHorizontal : doc::algorithm::FlipVertical;
  for (doc::LayerImage* lyr : layers) {
    if (!lyr)
      continue;
    doc::Cel* cel = lyr->cel(fr);
    if (!cel || !cel->image())
      continue;
    gfx::Rect celRc(cel->x(), cel->y(), cel->image()->width(), cel->image()->height());
    if (!celRc.contains(faceRect)) {
      // Grow the cel to cover the whole sprite, keeping its pixels in place.
      doc::ImageRef grown(doc::Image::create(cel->image()->pixelFormat(), spr->width(),
                                             spr->height()));
      grown->setMaskColor(cel->image()->maskColor());
      doc::clear_image(grown.get(), cel->image()->maskColor());
      doc::copy_image(grown.get(), cel->image(), cel->x(), cel->y());
      DocApi api = doc->getApi(tx);
      api.replaceImage(spr, cel->imageRef(), grown);
      api.setCelPosition(spr, cel, 0, 0);
      celRc = gfx::Rect(0, 0, spr->width(), spr->height());
    }
    const gfx::Rect inImage(faceRect.x - cel->x(), faceRect.y - cel->y(), faceRect.w, faceRect.h);
    tx(new cmd::FlipImage(cel->image(), inImage, type));
  }
  tx.commit();
  return true;
}

// Rebuild the three shared sheets from their old UV rects
// (taken from the current slice bounds, which are the rects the
// pixels actually sit in today) into new UV rects of the given
// new face sizes, and set the sprite to the exact sheet size
// for those faces. Face pixels are copied, never scaled: a
// larger new rect gets transparent padding, a smaller one is
// cropped at its bottom-right, exactly like a canvas resize.
// Used both for a face resize and for reconciling a manual
// canvas resize (see pollAndRender in the studio view).
// With object groups present (Objects, 2026-11-17): re-lay out EVERY
// object's band. The object `targetKey` gets the new face sizes, the others
// keep theirs; bands are stacked downwards (root first, then objects in
// creation order), the canvas becomes exactly the atlas, every object's
// sheets are rebuilt for every frame (pixels copied from their old cells,
// never scaled) and every object's slices move to their new cells. One Tx,
// so it joins an outer transaction (New Object = one undo step).
// Shared size (Arin, 2026-11-18): every object has the SAME face sizes (the
// model size), so newW/newH apply to every object. `mode` says what happens
// to the drawn pixels (ResizeMode). `orgs` (unifyObjectsOnDoc) places each
// object's old box at an explicit index-space origin inside the new one.
using ObjectOrigins = std::map<std::string, std::array<int, 3>>;

static void rebuildAtlas(Doc* doc,
                         const int newW[kFaceCount],
                         const int newH[kFaceCount],
                         ResizeMode mode = ResizeMode::Keep,
                         const ObjectOrigins* orgs = nullptr,
                         const char* label = "Resize Easymetric Objects")
{
  if (!doc || !doc->sprite())
    return;
  doc::Sprite* spr = doc->sprite();

  struct Item {
    std::string key;
    SharedSurfaceLayers layers;
    gfx::Rect oldRect[kFaceCount];
    int w[kFaceCount], h[kFaceCount];
    int colW[3], rowH[2];
    int bandY = 0;
  };
  std::vector<Item> items;
  std::vector<std::string> keys = { std::string() };
  for (const ObjectGroupRef& g : objectGroups(spr))
    keys.push_back(g.meta.key);
  for (const std::string& key : keys) {
    EasymetricObjectScope sc(spr, key);
    Item it;
    it.key = key;
    it.layers = findSharedLayers(spr);
    sharedFaceSizes(spr, it.w, it.h);
    for (int f = 0; f < kFaceCount; ++f) {
      it.oldRect[f] = sharedFaceRect(spr, f);
      if (doc::Slice* sl = findSliceForFace(spr, f))
        if (const auto* k = sl->getByFrame(0))
          if (!k->bounds().isEmpty())
            it.oldRect[f] = k->bounds();
    }
    for (int f = 0; f < kFaceCount; ++f) {
      it.w[f] = std::clamp(newW[f], 1, 1024);
      it.h[f] = std::clamp(newH[f], 1, 1024);
    }
    sharedLayout(it.w, it.h, it.colW, it.rowH);
    items.push_back(it);
  }
  int atlasW = 1, atlasH = 0;
  for (Item& it : items) {
    it.bandY = atlasH;
    atlasW = std::max(atlasW, it.colW[0] + it.colW[1] + it.colW[2]);
    atlasH += it.rowH[0] + it.rowH[1];
  }
  atlasH = std::max(1, atlasH);

  try {
    Tx tx(doc, label, ModifyDocument);
    DocApi api = doc->getApi(tx);
    for (const Item& it : items) {
      gfx::Rect newRect[kFaceCount];
      for (int f = 0; f < kFaceCount; ++f) {
        newRect[f] = sharedUVRectExact(f, it.colW, it.rowH, it.w[f], it.h[f]);
        newRect[f].y += it.bandY;
      }
      const int* org = nullptr;
      if (orgs) {
        auto o = orgs->find(it.key);
        if (o != orgs->end())
          org = o->second.data();
      }
      for (doc::LayerImage* lyr :
           { it.layers.baseLayer, it.layers.textureLayer, it.layers.depthLayer }) {
        if (!lyr)
          continue;
        for (doc::frame_t fr = doc::frame_t(0); fr <= spr->lastFrame(); ++fr) {
          doc::Cel* cel = lyr->cel(fr);
          if (!cel || !cel->image())
            continue;
          const doc::Image* src = cel->image();
          doc::ImageRef dst(doc::Image::create(src->pixelFormat(), atlasW, atlasH));
          dst->clear(0);
          for (int f = 0; f < kFaceCount; ++f) {
            gfx::Rect srcRc = it.oldRect[f];
            srcRc.offset(-cel->x(), -cel->y());
            copyFacePixels(dst.get(), newRect[f], src, srcRc, f, mode, org);
          }
          api.replaceImage(spr, cel->imageRef(), dst);
          api.setCelPosition(spr, cel, 0, 0);
        }
      }
    }
    api.setSpriteSize(spr, atlasW, atlasH);
    for (const Item& it : items) {
      EasymetricObjectScope sc(spr, it.key);
      for (int f = 0; f < kFaceCount; ++f) {
        gfx::Rect uv = sharedUVRectExact(f, it.colW, it.rowH, it.w[f], it.h[f]);
        uv.y += it.bandY;
        doc::Slice* sl = findSliceForFace(spr, f);
        if (!sl) {
          sl = new doc::Slice;
          sl->setName(it.key.empty() ? std::string(kFaceSpecs[f].sliceName)
                                     : objectSliceName(it.key, f));
          sl->userData().setColor(
            doc::rgba(kFaceSpecs[f].r, kFaceSpecs[f].g, kFaceSpecs[f].b, 255));
          sl->insert(0, doc::SliceKey(uv));
          tx(new cmd::AddSlice(spr, sl));
        }
        else {
          tx(new cmd::SetSliceKey(sl, 0, doc::SliceKey(uv)));
        }
      }
    }
    tx.commit();
    doc->notifyGeneralUpdate();
  }
  catch (const std::exception& e) {
    easymetricReportError("rebuildAtlas", e.what());
  }
}

// True when some object still has its own size or an offset (files made
// before the shared model size, 2026-11-17).
static bool needsUnify(doc::Sprite* spr)
{
  if (!spr || !hasObjectGroups(spr))
    return false;
  int rw[kFaceCount], rh[kFaceCount];
  {
    EasymetricObjectScope root(spr, std::string());
    sharedFaceSizes(spr, rw, rh);
  }
  for (const ObjectGroupRef& g : objectGroups(spr)) {
    if (g.meta.o[0] || g.meta.o[1] || g.meta.o[2])
      return true;
    EasymetricObjectScope sc(spr, g.meta.key);
    int w[kFaceCount], h[kFaceCount];
    sharedFaceSizes(spr, w, h);
    for (int f = 0; f < kFaceCount; ++f)
      if (w[f] != rw[f] || h[f] != rh[f])
        return true;
  }
  return false;
}

// Migrates such a file to one shared model size: the space becomes the
// union of every object's box (at its offset), each object's six views are
// copied unscaled to their old place in it, and every offset becomes 0. The
// 3D model is unchanged. One undo step. Returns the new box (X, Y, Z), or
// all zeros when nothing had to change.
static std::array<int, 3> unifyObjectsOnDoc(Doc* doc)
{
  std::array<int, 3> out = { 0, 0, 0 };
  doc::Sprite* spr = doc ? doc->sprite() : nullptr;
  if (!needsUnify(spr))
    return out;
  const std::vector<ObjectInfo> infos = resolveObjects(spr);
  int lo[3] = { 0, 0, 0 }, hi[3] = { infos[0].box[0], infos[0].box[1], infos[0].box[2] };
  for (const ObjectInfo& i : infos) {
    if (i.key.empty())
      continue;
    for (int a = 0; a < 3; ++a) {
      lo[a] = std::min(lo[a], i.meta.o[a]);
      hi[a] = std::max(hi[a], i.meta.o[a] + i.box[a]);
    }
  }
  int S[3];
  for (int a = 0; a < 3; ++a)
    S[a] = std::clamp(hi[a] - lo[a], 1, 256);
  ObjectOrigins orgs;
  for (const ObjectInfo& i : infos) {
    const int* o = i.meta.o; // the root's is 0, 0, 0
    // Offsets are Y UP; index space is Y DOWN (row 0 = the top).
    orgs[i.key] = { o[0] - lo[0], hi[1] - (o[1] + i.box[1]), o[2] - lo[2] };
  }
  int w[kFaceCount], h[kFaceCount];
  for (int f = 0; f < kFaceCount; ++f) { // = boxFaceSizes(S), defined further down
    GhostAxis ax[2];
    ghostAxesOfFace(f, ax);
    w[f] = S[ax[0].axis];
    h[f] = S[ax[1].axis];
  }
  try {
    Tx tx(doc, "Unify Easymetric Object Sizes", ModifyDocument);
    rebuildAtlas(doc, w, h, ResizeMode::Keep, &orgs, "Unify Easymetric Object Sizes");
    for (const ObjectGroupRef& g : objectGroups(spr)) {
      ObjectMeta meta = g.meta;
      meta.o[0] = meta.o[1] = meta.o[2] = 0;
      tx(new cmd::SetUserDataProperty(g.group, std::string(), kObjectProp,
                                      doc::UserData::Variant(objectMetaString(meta))));
    }
    tx.commit();
    doc->notifyGeneralUpdate();
  }
  catch (const std::exception& e) {
    easymetricReportError("unifyObjects", e.what());
    return out;
  }
  out = { S[0], S[1], S[2] };
  return out;
}

static void rebuildSharedSheets(Doc* doc,
                                const int newW[kFaceCount],
                                const int newH[kFaceCount],
                                ResizeMode mode = ResizeMode::Keep)
{
  if (!doc || !doc->sprite()) return;
  doc::Sprite* spr = doc->sprite();
  if (hasObjectGroups(spr)) {
    if (needsUnify(spr)) {
      // Sizes asked for before the migration still apply afterwards.
      unifyObjectsOnDoc(doc);
    }
    rebuildAtlas(doc, newW, newH, mode);
    return;
  }
  SharedSurfaceLayers shared = findSharedLayers(spr);
  if (!shared.baseLayer && !shared.textureLayer) return;

  int oldW[kFaceCount], oldH[kFaceCount];
  sharedFaceSizes(spr, oldW, oldH);
  gfx::Rect oldRect[kFaceCount];
  for (int f = 0; f < kFaceCount; ++f) {
    oldRect[f] = gfx::Rect(0, 0, oldW[f], oldH[f]);
    if (doc::Slice* sl = findSliceForFace(spr, f)) {
      if (const auto* key = sl->getByFrame(0)) {
        if (!key->bounds().isEmpty())
          oldRect[f] = key->bounds();
      }
    }
  }
  int newColW[3], newRowH[2];
  sharedLayout(newW, newH, newColW, newRowH);
  const int newSheetW = newColW[0] + newColW[1] + newColW[2];
  const int newSheetH = newRowH[0] + newRowH[1];

  try {
    Tx tx(doc, "Resize Easymetric UV Sheet", ModifyDocument);
    DocApi api = doc->getApi(tx);
    auto rebuildSheet = [&](doc::LayerImage* lyr) {
      if (!lyr) return;
      for (doc::frame_t fr = doc::frame_t(0); fr <= spr->lastFrame(); ++fr) {
        doc::Cel* cel = lyr->cel(fr);
        if (!cel || !cel->image()) continue;
        doc::ImageRef resized(doc::Image::create(cel->image()->pixelFormat(),
                                                 newSheetW, newSheetH));
        resized->clear(0);
        for (int f = 0; f < kFaceCount; ++f) {
          const gfx::Rect newUv = sharedUVRectExact(f, newColW, newRowH,
                                                    newW[f], newH[f]);
          // Centred (Keep) or scaled (Stretch) in model space, not anchored
          // at the cell's top-left (2026-11-18). Same cel-offset handling
          // as before: the old code read oldRect in image coordinates.
          copyFacePixels(resized.get(), newUv, cel->image(), oldRect[f], f, mode);
        }
        api.replaceImage(spr, cel->imageRef(), resized);
        api.setCelPosition(spr, cel, 0, 0);
      }
    };
    rebuildSheet(shared.baseLayer);
    rebuildSheet(shared.textureLayer);
    rebuildSheet(shared.depthLayer);
    api.setSpriteSize(spr, newSheetW, newSheetH);
    for (int f = 0; f < kFaceCount; ++f) {
      const gfx::Rect uv = sharedUVRectExact(f, newColW, newRowH, newW[f], newH[f]);
      doc::Slice* sl = findSliceForFace(spr, f);
      if (!sl) {
        // A face with no slice yet (e.g. only "surface_front"
        // existed when linkDims ran) must get one: the studio
        // and sharedFaceSizes need all six face slices to exist.
        // Previously this loop only updated slices that already
        // existed, so a linkDims on such a document left the
        // other five faces sliceless (four-requirements TC 4.6).
        sl = new doc::Slice;
        sl->setName(kFaceSpecs[f].sliceName);
        sl->userData().setColor(
          doc::rgba(kFaceSpecs[f].r, kFaceSpecs[f].g, kFaceSpecs[f].b, 255));
        sl->insert(0, doc::SliceKey(uv));
        tx(new cmd::AddSlice(spr, sl));
      }
      else {
        tx(new cmd::SetSliceKey(sl, 0, doc::SliceKey(uv)));
      }
    }
    tx.commit();
    doc->notifyGeneralUpdate();
  }
  catch (const std::exception& e) {
    easymetricReportError("rebuildSharedSheets", e.what());
    // Ignore: entering the studio will retry.
  }
}

// Migrate a document from per-face layers to the three shared sheets.
// Old per-face Base/Texture images are copied into their UV cells,
// the old per-face layers/groups are removed, slices are moved to
// their UV rects, and the sprite is resized to the sheet size.
// A document that already has the shared sheets is left alone, and
// a document with no Easymetric layers at all gets starter sheets.
// The old depth sheet cannot be migrated per face (it was written
// at (0, 0) for every face), so it is recreated blank; use
// "Render Editable Face Depths" to regenerate it from the UV cells.
static void setupSharedSurfaceLayers(Doc* doc, bool seedStarterIfBlank,
                                     bool seedSinglePixel = false)
{
  if (!doc || !doc->sprite()) return;
  doc::Sprite* spr = doc->sprite();

  // Already shared: nothing to migrate, just make sure the slices
  // point at the UV rects. If the canvas was resized by hand
  // since the sheets were built, the sprite size no longer
  // matches the exact sheet size for the face sizes, so rebuild
  // the sheets to that exact size first (face pixels preserved,
  // see rebuildSharedSheets) instead of leaving padding/gaps
  // between the canvas and the UV mapping.
  SharedSurfaceLayers existing = findSharedLayers(spr);
  if (existing.baseLayer || existing.textureLayer) {
    if (existing.baseLayer && existing.baseLayer->name() != "Surface_Base" &&
        !existing.textureLayer) {
      existing.baseLayer->setName("Surface_Base");
    }
    const gfx::Size exact = atlasSize(spr);
    if (exact.w > 0 && (spr->width() != exact.w || spr->height() != exact.h)) {
      int w[kFaceCount], h[kFaceCount];
      sharedFaceSizes(spr, w, h);
      rebuildSharedSheets(doc, w, h);
    }
    try {
      Tx tx(doc, "Setup Shared Easymetric Layers", ModifyDocument);
      for (int f = 0; f < kFaceCount; ++f) {
        const gfx::Rect uv = sharedFaceRect(spr, f);
        doc::Slice* sl = findSliceForFace(spr, f);
        if (!sl) {
          sl = new doc::Slice;
          sl->setName(kFaceSpecs[f].sliceName);
          sl->userData().setColor(
            doc::rgba(kFaceSpecs[f].r, kFaceSpecs[f].g, kFaceSpecs[f].b, 255));
          sl->insert(0, doc::SliceKey(uv));
          tx(new cmd::AddSlice(spr, sl));
        }
        else {
          tx(new cmd::SetSliceKey(sl, 0, doc::SliceKey(uv)));
        }
      }
      tx.commit();
      doc->notifyGeneralUpdate();
    }
    catch (const std::exception& e) {
    easymetricReportError("setupSharedSurfaceLayers", e.what());
      // Ignore: the shared layers themselves are already usable.
    }
    return;
  }

  // Capture the old per-face images before any layer is removed.
  struct FaceImage {
    doc::ImageRef baseImg;
    doc::ImageRef texImg;
    int w = 0;
    int h = 0;
  };
  std::array<FaceImage, kFaceCount> faces;
  // Do not start from the sprite size: after a partial migration the
  // sprite may already be sheet-sized, while the surviving per-face
  // images are still face-sized. Face sizes win; the sprite size is
  // only the fallback for a brand-new, empty document.
  int cellW = 0;
  int cellH = 0;
  for (int f = 0; f < kFaceCount; ++f) {
    SurfaceLayerPair sp = findSurfaceLayers(spr, f);
    auto capture = [](doc::LayerImage* lyr) -> doc::ImageRef {
      if (!lyr) return doc::ImageRef();
      if (doc::Cel* cel = lyr->cel(0)) {
        if (cel->image()) return doc::ImageRef(doc::Image::createCopy(cel->image()));
      }
      return doc::ImageRef();
    };
    faces[f].baseImg = capture(sp.baseLayer);
    faces[f].texImg = capture(sp.texLayer);
    const doc::Image* anyImg = faces[f].baseImg ? faces[f].baseImg.get() :
                               (faces[f].texImg ? faces[f].texImg.get() : nullptr);
    if (anyImg) {
      faces[f].w = anyImg->width();
      faces[f].h = anyImg->height();
    }
    else if (doc::Slice* sl = findSliceForFace(spr, f)) {
      if (const auto* key = sl->getByFrame(0)) {
        if (!key->bounds().isEmpty()) {
          faces[f].w = key->bounds().w;
          faces[f].h = key->bounds().h;
        }
      }
    }
    if (faces[f].w <= 0) faces[f].w = spr->width();
    if (faces[f].h <= 0) faces[f].h = spr->height();
    cellW = std::max(cellW, faces[f].w);
    cellH = std::max(cellH, faces[f].h);
  }
  if (cellW <= 0) cellW = spr->width();
  if (cellH <= 0) cellH = spr->height();
  // Exact layout, not a uniform grid: each column/row is sized
  // by its own faces, so the sheet is exactly the UV mapping.
  int faceW[kFaceCount], faceH[kFaceCount];
  for (int f = 0; f < kFaceCount; ++f) {
    faceW[f] = faces[f].w;
    faceH[f] = faces[f].h;
  }
  int colW[3], rowH[2];
  sharedLayout(faceW, faceH, colW, rowH);
  const int sheetW = colW[0] + colW[1] + colW[2];
  const int sheetH = rowH[0] + rowH[1];

  // Layers/groups to remove after their images have been copied.
  // The shared depth layer itself is kept: it is recreated below.
  std::vector<doc::Layer*> layersToRemove;
  for (int f = 0; f < kFaceCount; ++f) {
    const std::string faceName = kFaceSpecs[f].sliceName;
    SurfaceLayerPair sp = findSurfaceLayers(spr, f);
    if (sp.baseLayer) layersToRemove.push_back(sp.baseLayer);
    if (sp.texLayer) layersToRemove.push_back(sp.texLayer);
    for (doc::Layer* lyr : spr->allLayers()) {
      if (lyr->name() == ("Surface_" + faceName) ||
          lyr->name() == ("Surface_" + faceName + "_Group")) {
        layersToRemove.push_back(lyr);
      }
    }
  }

  try {
    Tx tx(doc, "Setup Shared Easymetric Layers (UV Sheets)", ModifyDocument);
    DocApi api = doc->getApi(tx);

    api.setSpriteSize(spr, sheetW, sheetH);

    auto buildSheet = [&](const char* kind,
                          doc::ImageRef FaceImage::*img) {
      doc::ImageRef sheet(doc::Image::create(spr->pixelFormat(), sheetW, sheetH));
      sheet->clear(0);
      bool hasPixels = false;
      for (int ff = 0; ff < kFaceCount; ++ff) {
        const doc::ImageRef& src = faces[ff].*img;
        if (!src) continue;
        const gfx::Rect uv = sharedUVRectExact(ff, colW, rowH, faces[ff].w, faces[ff].h);
        doc::copy_image(sheet.get(), src.get(), uv.x, uv.y);
        hasPixels = true;
      }
      if (!hasPixels && seedStarterIfBlank && std::string(kind) == "base") {
        if (seedSinglePixel) {
          // Easy start, NOT the default: ONE central Base pixel
          // per face (2 or 3 pixels across when the face is
          // large enough that a single pixel cannot be
          // seen/clicked reliably), grown outward from those
          // seeds. The default full starter pattern is kept
          // for setupSurfaces, because the four-requirements
          // suite (TC 1.2) and existing preview work rely on
          // it generating solid geometry; replacing it by
          // default broke TC 1.2 (22/23). Easy start is an
          // explicit action instead, see setupSurfacesEasy.
          for (int ff = 0; ff < kFaceCount; ++ff) {
            const gfx::Rect uv = sharedUVRectExact(ff, colW, rowH, faces[ff].w, faces[ff].h);
            const int fw = faces[ff].w;
            const int fh = faces[ff].h;
            const int minDim = std::min(fw, fh);
            const int seed = (minDim >= 32) ? 3 : (minDim >= 16) ? 2 : 1;
            const int sx = (fw - seed) / 2;
            const int sy = (fh - seed) / 2;
            const auto& spec = kFaceSpecs[ff];
            const uint32_t col = doc::rgba(spec.r, spec.g, spec.b, 255);
            for (int v = 0; v < seed; ++v)
              for (int u = 0; u < seed; ++u)
                doc::put_pixel(sheet.get(), uv.x + sx + u, uv.y + sy + v, col);
          }
        }
        else {
          for (int ff = 0; ff < kFaceCount; ++ff) {
            const gfx::Rect uv = sharedUVRectExact(ff, colW, rowH, faces[ff].w, faces[ff].h);
            for (int v = 0; v < faces[ff].h; ++v)
              for (int u = 0; u < faces[ff].w; ++u)
                doc::put_pixel(sheet.get(), uv.x + u, uv.y + v,
                               starterSurfacePixel(ff, u, v, faces[ff].w, faces[ff].h));
          }
        }
      }
      return sheet;
    };

    struct SheetLayer {
      const char* name;
      doc::ImageRef img;
    };
    std::vector<SheetLayer> sheets;
    sheets.push_back({ "Surface_Base", buildSheet("base", &FaceImage::baseImg) });
    sheets.push_back({ "Surface_Texture", buildSheet("texture", &FaceImage::texImg) });
    // Depth is recreated blank (see comment above); keep the layer
    // so "Render Editable Face Depths" has somewhere to write to.
    {
      doc::ImageRef depthSheet(doc::Image::create(spr->pixelFormat(), sheetW, sheetH));
      depthSheet->clear(0);
      sheets.push_back({ "Surface_DEPTH", depthSheet });
    }

    for (const auto& sheetLayer : sheets) {
      doc::LayerImage* lyr = findOrCreateLayer(doc, api, spr, sheetLayer.name);
      if (doc::Cel* cel = lyr->cel(0)) {
        api.replaceImage(spr, cel->imageRef(), sheetLayer.img);
        api.setCelPosition(spr, cel, 0, 0);
      }
      else {
        api.addCel(lyr, doc::frame_t(0), sheetLayer.img);
      }
    }

    for (int f = 0; f < kFaceCount; ++f) {
      const gfx::Rect uv = sharedUVRectExact(f, colW, rowH, faces[f].w, faces[f].h);
      doc::Slice* sl = findSliceForFace(spr, f);
      if (!sl) {
        sl = new doc::Slice;
        sl->setName(kFaceSpecs[f].sliceName);
        sl->userData().setColor(
          doc::rgba(kFaceSpecs[f].r, kFaceSpecs[f].g, kFaceSpecs[f].b, 255));
        sl->insert(0, doc::SliceKey(uv));
        tx(new cmd::AddSlice(spr, sl));
      }
      else {
        tx(new cmd::SetSliceKey(sl, 0, doc::SliceKey(uv)));
      }
    }

    // Remove the old per-face layers/groups last, so a failure
    // above does not leave a document with neither old nor new.
    // Image layers first (groups become empty), then groups.
    std::sort(layersToRemove.begin(), layersToRemove.end(),
              [](doc::Layer* a, doc::Layer* b) {
                return a->isImage() && !b->isImage();
              });
    for (doc::Layer* lyr : layersToRemove) {
      // Skip anything that is already gone, or a shared layer.
      bool stillThere = false;
      for (doc::Layer* l : spr->allLayers())
        if (l == lyr) { stillThere = true; break; }
      if (!stillThere) continue;
      if (lyr->name() == "Surface_Base" || lyr->name() == "Surface_Texture" ||
          lyr->name() == "Surface_DEPTH")
        continue;
      api.removeLayer(lyr);
    }

    tx.commit();
    doc->notifyGeneralUpdate();
  }
  catch (const std::exception& e) {
    easymetricReportError("setupSharedSurfaceLayers", e.what());
    // Ignore: entering the studio will retry the migration.
  }
}

static void propagateLinkedDimensions(Doc* doc, SixSurfaceData& data, int srcFace, int newW, int newH)
{
  if (!doc || !doc->sprite()) return;
  doc::Sprite* spr = doc->sprite();

  // Shared sheets, exact layout: resizing one face changes
  // only that face's size. Its column/row may grow or shrink,
  // so the other faces in that column/row move to their new
  // UV origins, but their sizes and pixels do not change.
  // That is the independence the old uniform grid broke: it
  // forced every face to the resized face's size. The sheet
  // (canvas) becomes exactly the sum of the new column/row
  // sizes, via rebuildSharedSheets.
  {
    SharedSurfaceLayers shared = findSharedLayers(spr);
    if (shared.baseLayer || shared.textureLayer) {
      int targetW[kFaceCount], targetH[kFaceCount];
      for (int f = 0; f < kFaceCount; ++f) {
        targetW[f] = data.w[f];
        targetH[f] = data.h[f];
      }
      if (srcFace >= 0 && srcFace < kFaceCount) {
        targetW[srcFace] = std::clamp(newW, 1, 256);
        targetH[srcFace] = std::clamp(newH, 1, 256);
      }
      int oldW[kFaceCount], oldH[kFaceCount];
      sharedFaceSizes(spr, oldW, oldH);
      const gfx::Size exact = atlasSize(spr);
      bool changed = false;
      for (int f = 0; f < kFaceCount; ++f)
        if (targetW[f] != oldW[f] || targetH[f] != oldH[f]) changed = true;
      if (spr->width() != exact.w || spr->height() != exact.h) changed = true;
      if (changed) {
        rebuildSharedSheets(doc, targetW, targetH);
        for (int f = 0; f < kFaceCount; ++f) {
          data.w[f] = targetW[f];
          data.h[f] = targetH[f];
        }
      }
      return;
    }
  }

  data.propagateDimensionLink(srcFace, newW, newH);

  try {
    Tx tx(doc, "Resize Easymetric Surface Dimensions", ModifyDocument);
    DocApi api = doc->getApi(tx);

    int maxW = spr->width();
    int maxH = spr->height();
    for (int f = 0; f < kFaceCount; ++f) {
      if (doc::Slice* sl = findSliceForFace(spr, f)) {
        for (auto it = sl->begin(); it != sl->end(); ++it) {
          maxW = std::max(maxW, it->value()->bounds().x + data.w[f]);
          maxH = std::max(maxH, it->value()->bounds().y + data.h[f]);
        }
      }
      maxW = std::max(maxW, data.w[f]);
      maxH = std::max(maxH, data.h[f]);
    }
    if (maxW > spr->width() || maxH > spr->height()) {
      api.setSpriteSize(spr, maxW, maxH);
    }

    std::set<doc::ObjectId> resizedImages;

    for (int f = 0; f < kFaceCount; ++f) {
      const int targetW = data.w[f];
      const int targetH = data.h[f];

      // Update slice bounds for all keys in slice
      doc::Slice* sl = findSliceForFace(spr, f);
      if (!sl) {
        sl = new doc::Slice;
        sl->setName(kFaceSpecs[f].sliceName);
        sl->userData().setColor(
          doc::rgba(kFaceSpecs[f].r, kFaceSpecs[f].g, kFaceSpecs[f].b, 255));
        sl->insert(0, doc::SliceKey(gfx::Rect(0, 0, targetW, targetH)));
        tx(new cmd::AddSlice(spr, sl));
      }
      else if (sl->empty()) {
        tx(new cmd::SetSliceKey(sl, 0, doc::SliceKey(gfx::Rect(0, 0, targetW, targetH))));
      }
      else {
        for (auto it = sl->begin(); it != sl->end(); ++it) {
          gfx::Rect curBounds = it->value()->bounds();
          curBounds.w = targetW;
          curBounds.h = targetH;
          tx(new cmd::SetSliceKey(sl, it->frame(),
            doc::SliceKey(curBounds, it->value()->center(), it->value()->pivot())));
        }
      }

      // Update layer cels
      SurfaceLayerPair sp = findSurfaceLayers(spr, f);
      auto resizeLayerCel = [&](doc::LayerImage* lyr) {
        if (!lyr) return;
        for (doc::frame_t fr = doc::frame_t(0); fr <= spr->lastFrame(); ++fr) {
          doc::Cel* cel = lyr->cel(fr);
          if (!cel || !cel->image()) continue;
          if (resizedImages.count(cel->image()->id())) continue;

          if (cel->image()->width() != targetW || cel->image()->height() != targetH) {
            doc::ImageRef resized(doc::Image::create(cel->image()->pixelFormat(), targetW, targetH));
            resized->clear(0);
            doc::copy_image(resized.get(), cel->image(), 0, 0);
            resizedImages.insert(resized->id());
            api.replaceImage(spr, cel->imageRef(), resized);
            api.setCelPosition(spr, cel, cel->x(), cel->y());
          }
        }
      };
      resizeLayerCel(sp.baseLayer);
      resizeLayerCel(sp.texLayer);
    }

    // Also update Surface_DEPTH if present
    if (doc::LayerImage* depthLyr = findLayerByName(spr, "Surface_DEPTH")) {
      const int maxFaceW = std::max({ data.w[kFaceTop], data.w[kFaceFront], data.w[kFaceRight] });
      const int maxFaceH = std::max({ data.h[kFaceTop], data.h[kFaceFront], data.h[kFaceRight] });
      for (doc::frame_t fr = doc::frame_t(0); fr <= spr->lastFrame(); ++fr) {
        doc::Cel* cel = depthLyr->cel(fr);
        if (!cel || !cel->image()) continue;
        if (resizedImages.count(cel->image()->id())) continue;
        if (cel->image()->width() != maxFaceW || cel->image()->height() != maxFaceH) {
          doc::ImageRef resized(doc::Image::create(cel->image()->pixelFormat(), maxFaceW, maxFaceH));
          resized->clear(0);
          doc::copy_image(resized.get(), cel->image(), 0, 0);
          resizedImages.insert(resized->id());
          api.replaceImage(spr, cel->imageRef(), resized);
          api.setCelPosition(spr, cel, cel->x(), cel->y());
        }
      }
    }

    tx.commit();
    doc->notifyGeneralUpdate();
  }
  catch (const std::exception& e) {
    easymetricReportError("propagateLinkedDimensions", e.what());
    // Ignore
  }
}

static void propagateLinkedDimensions(Doc* doc, int srcFace, int newW, int newH)
{
  if (!doc || !doc->sprite()) return;
  doc::Sprite* spr = doc->sprite();
  SixSurfaceData data;
  data.init(std::max(spr->width(), spr->height()));
  for (int f = 0; f < kFaceCount; ++f) {
    if (doc::Slice* sl = findSliceForFace(spr, f)) {
      if (const auto* key = sl->getByFrame(0)) {
        data.w[f] = key->bounds().w;
        data.h[f] = key->bounds().h;
        continue;
      }
    }
    SurfaceLayerPair sp = findSurfaceLayers(spr, f);
    doc::LayerImage* lyr = sp.baseLayer ? sp.baseLayer : sp.texLayer;
    if (lyr && lyr->cel(0) && lyr->cel(0)->image()) {
      data.w[f] = lyr->cel(0)->image()->width();
      data.h[f] = lyr->cel(0)->image()->height();
    }
    else {
      data.w[f] = spr->width();
      data.h[f] = spr->height();
    }
  }
  propagateLinkedDimensions(doc, data, srcFace, newW, newH);
}

static void setupTwoLayerStructure(Doc* doc, bool asSmartObjects)
{
  if (!doc || !doc->sprite()) return;
  doc::Sprite* spr = doc->sprite();

  // Shared-sheet model: there is no per-face 2-layer structure
  // to build any more. "Setup 2-layer" now means "make sure the
  // shared Base/Texture sheets exist", and "smart objects" means
  // converting the ONE shared Texture sheet, not six copies of it.
  // (Converting the shared sheet makes the whole UV sheet one
  // smart object; per-face smart-object links no longer exist.)
  setupSharedSurfaceLayers(doc, true);
  if (asSmartObjects) {
    SharedSurfaceLayers shared = findSharedLayers(spr);
    Context* ctx = UIContext::instance() ? static_cast<Context*>(UIContext::instance()) :
                                           App::instance()->context();
    if (ctx && shared.textureLayer && !shared.textureLayer->isSmartObject()) {
      ctx->setActiveLayer(shared.textureLayer);
      Command* cmd = Commands::instance()->byId(CommandId::ConvertLayer());
      if (cmd) {
        Params params;
        params.set("to", "smartobject");
        params.set("ui", "false");
        ctx->executeCommand(cmd, params);
      }
    }
  }
  doc->notifyGeneralUpdate();
}

static void convertSmartObjectsToGroups(Doc* doc)
{
  if (!doc || !doc->sprite()) return;
  doc::Sprite* spr = doc->sprite();

  Context* ctx = UIContext::instance() ? static_cast<Context*>(UIContext::instance()) :
                                         App::instance()->context();
  if (!ctx) return;

  bool convertedAny = false;
  while (true) {
    doc::Layer* target = nullptr;
    for (doc::Layer* lyr : spr->allLayers()) {
      if (lyr->isSmartObject()) {
        target = lyr;
        break;
      }
    }
    if (!target) break;

    ctx->setActiveLayer(target);
    Command* cmd = Commands::instance()->byId(CommandId::ConvertLayer());
    if (cmd) {
      Params params;
      params.set("to", "group");
      ctx->executeCommand(cmd, params);
      convertedAny = true;
    }
    else {
      break;
    }
  }

  if (convertedAny)
    doc->notifyGeneralUpdate();
}

class LinkToggleButton : public ui::Button {
public:
  LinkToggleButton()
    : ui::Button("")
    , m_linked(false)
  {
    setFocusStop(false);
  }

  bool isLinked() const { return m_linked; }

  void setLinked(bool linked)
  {
    if (m_linked != linked) {
      m_linked = linked;
      setSelected(linked);
      invalidate();
    }
  }

protected:
  void onSizeHint(ui::SizeHintEvent& ev) override
  {
    const int s = ui::guiscale();
    ev.setSizeHint(gfx::Size(20 * s, 14 * s));
  }

  void onPaint(ui::PaintEvent& ev) override
  {
    ui::Graphics* g = ev.graphics();
    const gfx::Rect rc = clientBounds();
    const int s = ui::guiscale();
    auto* theme = skin::SkinTheme::instance();

    gfx::Color bg, borderCol, iconCol;
    if (m_linked) {
      bg = hasMouse() ? gfx::rgba(14, 165, 233) : gfx::rgba(2, 132, 199);
      borderCol = gfx::rgba(56, 189, 248);
      iconCol = gfx::rgba(255, 255, 255);
    }
    else {
      bg = hasMouse() ? gfx::rgba(100, 116, 139) : gfx::rgba(64, 78, 94);
      borderCol = hasMouse() ? gfx::rgba(148, 163, 184) : gfx::rgba(45, 55, 66);
      iconCol = hasMouse() ? gfx::rgba(241, 245, 249) : gfx::rgba(180, 195, 210);
    }

    g->fillRect(bg, rc);
    g->drawRect(borderCol, rc);

    // Draw link icon from theme part
    os::Surface* icon = nullptr;
    if (theme) {
      const auto& part = theme->parts.iconAspectRatio();
      if (part)
        icon = part->bitmap(0);
    }

    if (icon) {
      const int ix = rc.x + (rc.w - icon->width()) / 2;
      const int iy = rc.y + (rc.h - icon->height()) / 2;
      g->drawColoredRgbaSurface(icon, iconCol, ix, iy);
    }
    else {
      const int cx = rc.x + rc.w / 2;
      const int cy = rc.y + rc.h / 2;
      g->drawRect(iconCol, gfx::Rect(cx - 5 * s, cy - 2 * s, 5 * s, 4 * s));
      g->drawRect(iconCol, gfx::Rect(cx - 1 * s, cy - 2 * s, 5 * s, 4 * s));
      g->drawHLine(iconCol, cx - 2 * s, cy, 3 * s);
    }
  }

private:
  bool m_linked;
};

// Small text action button for a panel header (Link / Sym X / Sym Y /
// Flip H / Flip V). Same visual family as LinkToggleButton: blue when
// ON (toggle), slate when off. Action buttons (Flip) are momentary —
// they have no persistent state, they just run on Click.
class PaneActionButton : public ui::Button {
public:
  PaneActionButton(const std::string& label, bool isToggle)
    : ui::Button(label)
    , m_isToggle(isToggle)
    , m_on(false)
  {
    setFocusStop(false);
  }

  bool isOn() const { return m_on; }

  void setOn(bool on)
  {
    if (!m_isToggle)
      return;
    if (m_on != on) {
      m_on = on;
      setSelected(on);
      invalidate();
    }
  }

  // Icon buttons (2026-11-09, William: "icons instead of just the text").
  // The icon is the theme part `partId` (drawn from the theme sheet, so the
  // artist's pixel art lands without a rebuild) when the theme has it, else
  // a built-in pixel placeholder of the same meaning (see drawFallbackIcon).
  // The label text is kept for the accessible name / UI tests, and the
  // tooltip is shown in the status bar on hover.
  enum class Icon { None, FlipH, FlipV, Grid, Link, SymX, SymY, Ghost };

  void setIcon(Icon icon, const std::string& partId)
  {
    m_icon = icon;
    m_iconPart = partId;
    invalidate();
  }

  void setTooltipText(const std::string& tip) { m_tip = tip; }
  const std::string& tooltipText() const { return m_tip; }

protected:
  void onSizeHint(ui::SizeHintEvent& ev) override
  {
    // Base Button measures the label text; keep its width, but force
    // the compact LinkToggleButton height so a row of them fits.
    ui::Button::onSizeHint(ev);
    gfx::Size sz = ev.sizeHint();
    const int s = ui::guiscale();
    sz.h = 14 * s;
    if (m_icon != Icon::None)
      sz.w = 16 * s;
    ev.setSizeHint(sz);
  }

  bool onProcessMessage(ui::Message* msg) override
  {
    if (msg->type() == ui::kMouseEnterMessage && !m_tip.empty()) {
      if (auto* sb = StatusBar::instance())
        sb->showTip(2000, m_tip);
    }
    return ui::Button::onProcessMessage(msg);
  }

  // Pixel-art placeholders drawn in code (used only while the theme sheet has
  // no part for the button). '#' = lit pixel. Each is scaled by guiscale.
  static const std::vector<std::string>& fallbackPixmap(Icon icon)
  {
    static const std::vector<std::string> flipH = {
      "#....#....#", "##...#...##", "###..#..###", "####.#.####", "#####.#####",
      "####.#.####", "###..#..###", "##...#...##", "#....#....#" };
    static const std::vector<std::string> flipV = {
      "#########", "#########"  /* replaced below by a transpose of flipH */ };
    static const std::vector<std::string> grid = {
      "#########", "#...#...#", "#...#...#", "#...#...#", "#########",
      "#...#...#", "#...#...#", "#...#...#", "#########" };
    static const std::vector<std::string> link = {
      ".####..####.", "#....##....#", "#....##....#", ".####..####." };
    static const std::vector<std::string> symX = {
      ".....#.....", ".#...#...#.", "##.......##", "#####.#####",
      "##.......##", ".#...#...#.", ".....#....." };
    static const std::vector<std::string> ghost = {
      "#.#.#.#.#", "#.......#", "#.......#", "#...#...#", "#..###..#",
      "#...#...#", "#.......#", "#.......#", "#.#.#.#.#" };
    (void)flipV;
    switch (icon) {
      case Icon::Ghost: return ghost;
      case Icon::FlipH: return flipH;
      case Icon::Grid: return grid;
      case Icon::Link: return link;
      case Icon::SymX: return symX;
      default: break;
    }
    static const std::vector<std::string> none;
    return none;
  }

  static std::vector<std::string> transposed(const std::vector<std::string>& src)
  {
    std::vector<std::string> out;
    if (src.empty())
      return out;
    for (size_t x = 0; x < src[0].size(); ++x) {
      std::string row;
      for (size_t y = 0; y < src.size(); ++y)
        row += src[y][x];
      out.push_back(row);
    }
    return out;
  }

  void drawFallbackIcon(ui::Graphics* g, const gfx::Rect& rc, gfx::Color col)
  {
    const int s = ui::guiscale();
    std::vector<std::string> map;
    switch (m_icon) {
      case Icon::FlipV: map = transposed(fallbackPixmap(Icon::FlipH)); break;
      case Icon::SymY: map = transposed(fallbackPixmap(Icon::SymX)); break;
      default: map = fallbackPixmap(m_icon); break;
    }
    if (map.empty())
      return;
    const int W = int(map[0].size()), H = int(map.size());
    const int ox = rc.x + (rc.w - W * s) / 2;
    const int oy = rc.y + (rc.h - H * s) / 2;
    for (int y = 0; y < H; ++y)
      for (int x = 0; x < W; ++x)
        if (map[y][x] == '#')
          g->fillRect(col, gfx::Rect(ox + x * s, oy + y * s, s, s));
  }

  void onPaint(ui::PaintEvent& ev) override
  {
    ui::Graphics* g = ev.graphics();
    const gfx::Rect rc = clientBounds();

    gfx::Color bg, borderCol, textCol;
    if (m_isToggle && m_on) {
      bg = hasMouse() ? gfx::rgba(14, 165, 233) : gfx::rgba(2, 132, 199);
      borderCol = gfx::rgba(56, 189, 248);
      textCol = gfx::rgba(255, 255, 255);
    }
    else {
      bg = hasMouse() ? gfx::rgba(100, 116, 139) : gfx::rgba(64, 78, 94);
      borderCol = hasMouse() ? gfx::rgba(148, 163, 184) : gfx::rgba(45, 55, 66);
      textCol = hasMouse() ? gfx::rgba(241, 245, 249) : gfx::rgba(180, 195, 210);
    }

    g->fillRect(bg, rc);
    g->drawRect(borderCol, rc);
    if (m_icon != Icon::None) {
      os::Surface* surf = nullptr;
      if (auto* theme = skin::SkinTheme::get(this)) {
        if (auto part = theme->getPartById(m_iconPart))
          surf = part->bitmap(0);
      }
      if (surf) {
        g->drawRgbaSurface(surf,
                           rc.x + (rc.w - surf->width()) / 2,
                           rc.y + (rc.h - surf->height()) / 2);
      }
      else {
        drawFallbackIcon(g, rc, textCol);
      }
      return;
    }
    const gfx::Size textSz = g->measureText(text());
    g->drawText(text(),
                textCol,
                gfx::ColorNone,
                gfx::Point(rc.x + (rc.w - textSz.w) / 2,
                           rc.y + (rc.h - textSz.h) / 2));
  }

private:
  bool m_isToggle;
  bool m_on;
  Icon m_icon = Icon::None;
  std::string m_iconPart;
  std::string m_tip;
};

// Folder-tab ear header bar matching reference UI
class PaneHeader : public ui::Widget {
public:
  PaneHeader(const std::string& title, gfx::Color accentCol = gfx::ColorNone)
    : ui::Widget(ui::kGenericWidget)
    , m_title(title)
    , m_accentCol(accentCol)
    , m_active(false)
    , m_linkBtn(nullptr)
  {
    setFocusStop(false);
  }

  void setTitle(const std::string& title)
  {
    m_title = title;
    invalidate();
  }

  const std::string& title() const { return m_title; }

  void setActive(bool active)
  {
    if (m_active != active) {
      m_active = active;
      invalidate();
    }
  }

  void setLinkButton(LinkToggleButton* btn)
  {
    m_linkBtn = btn;
    if (btn)
      addChild(btn);
  }

  // Panel action buttons (Link / Sym X / Sym Y / Flip H / Flip V).
  // Added in priority order: index 0 is laid out rightmost (after the
  // dimension-link button, if any) and survives longest when the
  // header is too narrow; later ones hide first.
  void addActionButton(PaneActionButton* btn)
  {
    if (!btn)
      return;
    m_actionButtons.push_back(btn);
    addChild(btn);
  }

protected:
  void onSizeHint(ui::SizeHintEvent& ev) override
  {
    ev.setSizeHint(gfx::Size(64 * ui::guiscale(), 16 * ui::guiscale()));
  }

  void onResize(ui::ResizeEvent& ev) override
  {
    setBoundsQuietly(ev.bounds());
    const gfx::Rect rc = childrenBounds();
    const int s = ui::guiscale();
    const int btnH = 14 * s;
    const int gap = 2 * s;
    // Right edge for the next button, moving left as buttons are placed.
    int right = rc.x + rc.w - 3 * s;
    if (m_linkBtn) {
      const int btnW = 20 * s;
      m_linkBtn->setBounds(gfx::Rect(right - btnW,
                                     rc.y + (rc.h - btnH) / 2,
                                     btnW,
                                     btnH));
      right -= btnW + gap;
    }
    // Action buttons, right-to-left in priority order. A button is
    // hidden (and every lower-priority one after it) when its left
    // edge would cross into the title tab plus a small margin.
    // The tab is at least 48*guiscale wide (see onPaint), which is
    // within a few px of its real width for the short face names.
    const int minLeft = rc.x + 48 * s + 4 * s;
    for (PaneActionButton* btn : m_actionButtons) {
      if (!btn)
        continue;
      const int btnW = btn->sizeHint().w;
      const int left = right - btnW;
      if (left < minLeft) {
        btn->setVisible(false);
        continue;
      }
      btn->setVisible(true);
      btn->setBounds(gfx::Rect(left,
                                rc.y + (rc.h - btnH) / 2,
                                btnW,
                                btnH));
      right = left - gap;
    }
  }

  void onPaint(ui::PaintEvent& ev) override
  {
    ui::Graphics* g = ev.graphics();
    const gfx::Rect rc = clientBounds();
    const int s = ui::guiscale();

    const gfx::Size textSz = g->measureText(m_title);
    const int tabW = std::min(rc.w, std::max(textSz.w + 14 * s, 48 * s));
    const gfx::Rect tabRc(rc.x, rc.y, tabW, rc.h);

    // Slate blue header bar background
    g->fillRect(gfx::rgba(92, 108, 122), rc);
    // Raised folder tab ear
    g->fillRect(m_active ? gfx::rgba(125, 142, 160) : gfx::rgba(105, 122, 138), tabRc);
    g->drawRect(gfx::rgba(28, 35, 43), rc);

    if (m_accentCol != gfx::ColorNone) {
      g->fillRect(m_accentCol, gfx::Rect(tabRc.x + 1, tabRc.y + 1, 3 * s, tabRc.h - 2));
    }

    // No grip dots: the header's dead space is now panel action
    // buttons (Link / Sym X / Sym Y / Flip H / Flip V), laid out
    // as children in onResize.

    g->drawText(m_title,
                gfx::rgba(255, 255, 255),
                gfx::ColorNone,
                gfx::Point(tabRc.x + (m_accentCol != gfx::ColorNone ? 6 * s : 4 * s),
                           tabRc.y + (rc.h - textSz.h) / 2));
  }

private:
  std::string m_title;
  gfx::Color m_accentCol;
  bool m_active;
  LinkToggleButton* m_linkBtn;
  std::vector<PaneActionButton*> m_actionButtons;
};

class FacePaneBox : public ui::VBox {
public:
  FacePaneBox(int faceIndex, DocView* docView, Editor* editor)
    : m_faceIndex(faceIndex)
    , m_docView(docView)
    , m_editor(editor)
  {
  }

protected:
  bool onProcessMessage(ui::Message* msg) override
  {
    if (msg->type() == ui::kMouseDownMessage) {
      if (m_docView && m_docView->document() && m_docView->document()->sprite()) {
        doc::Sprite* spr = m_docView->document()->sprite();
        // Shared sheets: every panel edits the same Texture (or
        // Base) layer; only the editor's clip rect (its UV rect,
        // set in initLayersAndFraming) differs per panel.
        SharedSurfaceLayers shared = findSharedLayers(spr);
        doc::LayerImage* lyr = nullptr;
        if (shared.textureLayer && !shared.textureLayer->isSmartObject())
          lyr = shared.textureLayer;
        else if (shared.baseLayer)
          lyr = shared.baseLayer;
        else if (shared.textureLayer)
          lyr = shared.textureLayer;
        if (!lyr) {
          SurfaceLayerPair sp = findSurfaceLayers(spr, m_faceIndex);
          if (sp.texLayer && !sp.texLayer->isSmartObject())
            lyr = sp.texLayer;
          else if (sp.baseLayer)
            lyr = sp.baseLayer;
          else if (sp.texLayer)
            lyr = sp.texLayer;
          if (!lyr) {
            const std::string lyrName = std::string("Surface_") + kFaceSpecs[m_faceIndex].sliceName;
            lyr = findLayerByName(spr, lyrName);
          }
        }
        if (lyr) {
          m_editor->setLayer(lyr);
          if (m_docView->editor() != m_editor) {
            m_docView->setActivePaneEditor(m_editor);
          }
          if (UIContext* ctx = UIContext::instance())
            ctx->setActiveView(m_docView);
        }
      }
    }
    return ui::VBox::onProcessMessage(msg);
  }

private:
  int m_faceIndex;
  DocView* m_docView;
  Editor* m_editor;
};

// Checkerboard helper
static void drawCheckerboardPattern(ui::Graphics* g, const gfx::Rect& rc)
{
  const int tile = 12 * ui::guiscale();
  const gfx::Color c1 = gfx::rgba(128, 128, 128);
  const gfx::Color c2 = gfx::rgba(192, 192, 192);
  g->fillRect(c1, rc);
  for (int y = rc.y; y < rc.y2(); y += tile) {
    const int row = (y - rc.y) / tile;
    const int h = std::min(tile, rc.y2() - y);
    for (int x = rc.x; x < rc.x2(); x += tile) {
      const int col = (x - rc.x) / tile;
      if (((row + col) & 1) == 1) {
        const int w = std::min(tile, rc.x2() - x);
        g->fillRect(c2, gfx::Rect(x, y, w, h));
      }
    }
  }
}

// 3D Preview Widget (Top of Column 4)
class EasymetricPreviewWidget : public ui::Widget {
public:
  EasymetricPreviewWidget()
    : ui::Widget(ui::kGenericWidget)
  {
    setExpansive(true);
  }

  obs::signal<void(int, int)> OrbitDrag;
  obs::signal<void(double)> ZoomChanged;

  // Zoom of the rendered model, relative to the fit-to-pane size.
  // 1.0 = fit, 0.25 = quarter size, 8.0 = 8x. Wheel, +/- buttons and
  // double-click (reset) all go through setZoom() so the value is
  // always clamped and observers always see the same value.
  double zoom() const { return m_zoom; }

  void setZoom(double zoom)
  {
    const double clamped = std::clamp(zoom, 0.25, 8.0);
    if (std::abs(clamped - m_zoom) < 1e-9)
      return;
    m_zoom = clamped;
    ZoomChanged(m_zoom);
    invalidate();
  }

  void zoomIn() { setZoom(m_zoom * 1.25); }
  void zoomOut() { setZoom(m_zoom / 1.25); }

  // Preview background, set from the Fields "Preview BG" row.
  // Default alpha 0 = fully transparent, so the checkerboard
  // shows exactly as before. Alpha 255 = solid colour,
  // in-between = colour tinted over the checkerboard.
  void setBackgroundColor(int r, int g, int b, int a)
  {
    r = std::clamp(r, 0, 255); g = std::clamp(g, 0, 255);
    b = std::clamp(b, 0, 255); a = std::clamp(a, 0, 255);
    if (r == m_bgR && g == m_bgG && b == m_bgB && a == m_bgA) return;
    m_bgR = r; m_bgG = g; m_bgB = b; m_bgA = a;
    invalidate();
  }

  void resetZoom()
  {
    m_panX = 0;
    m_panY = 0;
    setZoom(1.0);
    // setZoom() skips invalidate() when already at 1.0, but the pan
    // reset above still needs a repaint in that case.
    invalidate();
  }

  void setSurfaces(const os::SurfaceRef& diffSurf,
                   const os::SurfaceRef& normSurf,
                   const os::SurfaceRef& deptSurf,
                   int yawDeg,
                   int pitchDeg,
                   int pivScreenX,
                   int pivScreenY,
                   bool showDiff,
                   bool showNorm,
                   bool showDept)
  {
    m_diffSurf = diffSurf;
    m_normSurf = normSurf;
    m_deptSurf = deptSurf;
    m_yawDeg = yawDeg;
    m_pitchDeg = pitchDeg;
    m_pivScreenX = pivScreenX;
    m_pivScreenY = pivScreenY;
    m_showDiff = showDiff;
    m_showNorm = showNorm;
    m_showDept = showDept;
    invalidate();
  }

protected:
  bool onProcessMessage(ui::Message* msg) override
  {
    switch (msg->type()) {
      case ui::kMouseDownMessage: {
        auto* mouseMsg = static_cast<ui::MouseMessage*>(msg);
        if (mouseMsg->left()) {
          m_dragging = true;
          m_dragStart = mouseMsg->position();
          m_startYaw = m_yawDeg;
          m_startPitch = m_pitchDeg;
          captureMouse();
          return true;
        }
        if (mouseMsg->right() || mouseMsg->middle()) {
          // Pan the zoomed rendering; left-drag stays reserved for orbit.
          m_panning = true;
          m_panStart = mouseMsg->position();
          m_startPanX = m_panX;
          m_startPanY = m_panY;
          captureMouse();
          return true;
        }
        break;
      }
      case ui::kMouseMoveMessage: {
        if (m_panning) {
          auto* mouseMsg = static_cast<ui::MouseMessage*>(msg);
          const gfx::Point pos = mouseMsg->position();
          m_panX = m_startPanX + (pos.x - m_panStart.x);
          m_panY = m_startPanY + (pos.y - m_panStart.y);
          invalidate();
          return true;
        }
        if (m_dragging) {
          auto* mouseMsg = static_cast<ui::MouseMessage*>(msg);
          const gfx::Point pos = mouseMsg->position();
          const int s = std::max(1, ui::guiscale());
          const int dYaw = (pos.x - m_dragStart.x) / s;
          const int dPitch = (pos.y - m_dragStart.y) / s;
          int newYaw = m_startYaw + dYaw;
          while (newYaw > 180) newYaw -= 360;
          while (newYaw < -180) newYaw += 360;
          int newPitch = std::clamp(m_startPitch + dPitch, -75, 75);
          OrbitDrag(newYaw, newPitch);
          return true;
        }
        break;
      }
      case ui::kMouseUpMessage: {
        if (m_dragging || m_panning) {
          m_dragging = false;
          m_panning = false;
          releaseMouse();
          return true;
        }
        break;
      }
      case ui::kMouseWheelMessage: {
        auto* mouseMsg = static_cast<ui::MouseMessage*>(msg);
        const gfx::Point delta = mouseMsg->wheelDelta();
        // Shift+wheel scrolls (pans) instead of zooming, so the
        // preview can be scrolled as well as zoomed.
        if (mouseMsg->shiftPressed()) {
          m_panX += delta.x * 8;
          m_panY -= delta.y * 8;
          invalidate();
          return true;
        }
        // Same sign convention as palette_view: delta.x - delta.y > 0
        // means wheel-up, which zooms in.
        const int steps = delta.x - delta.y;
        if (steps != 0) {
          setZoom(m_zoom * std::pow(1.25, steps));
          return true;
        }
        break;
      }
      case ui::kDoubleClickMessage: {
        resetZoom();
        return true;
      }
    }
    return ui::Widget::onProcessMessage(msg);
  }

  void onPaint(ui::PaintEvent& ev) override
  {
    ui::Graphics* g = ev.graphics();
    const gfx::Rect rc = clientBounds();
    const int s = ui::guiscale();

    drawCheckerboardPattern(g, rc);
    if (m_bgA > 0)
      g->fillRect(gfx::rgba(m_bgR, m_bgG, m_bgB, m_bgA), rc);
    g->drawRect(gfx::rgba(28, 35, 43), rc);

    std::vector<os::SurfaceRef> activeSurfs;
    std::vector<const char*> activeLabels;
    if (m_showDiff && m_diffSurf) {
      activeSurfs.push_back(m_diffSurf);
      activeLabels.push_back("Diffuse");
    }
    if (m_showNorm && m_normSurf) {
      activeSurfs.push_back(m_normSurf);
      activeLabels.push_back("Normal");
    }
    if (m_showDept && m_deptSurf) {
      activeSurfs.push_back(m_deptSurf);
      activeLabels.push_back("Depth");
    }
    if (activeSurfs.empty() && m_diffSurf) {
      activeSurfs.push_back(m_diffSurf);
      activeLabels.push_back("Diffuse");
    }

    if (!activeSurfs.empty()) {
      const int count = int(activeSurfs.size());
      const int cellW = rc.w / count;

      for (int i = 0; i < count; ++i) {
        const gfx::Rect cellRc(rc.x + i * cellW, rc.y, cellW, rc.h);
        if (i > 0)
          g->drawVLine(gfx::rgba(28, 35, 43, 180), cellRc.x, cellRc.y, cellRc.h);

        const int sw = activeSurfs[i]->width();
        const int sh = activeSurfs[i]->height();
        if (sw > 0 && sh > 0) {
          const double scaleX = double(cellRc.w - 8 * s) / double(sw);
          const double scaleY = double(cellRc.h - 8 * s) / double(sh);
          const double scale = std::max(0.5, std::min(scaleX, scaleY)) * m_zoom;
          const int dw = std::max(1, int(sw * scale));
          const int dh = std::max(1, int(sh * scale));
          const int dx = cellRc.x + (cellRc.w - dw) / 2 + m_panX;
          const int dy = cellRc.y + (cellRc.h - dh) / 2 + m_panY;

          // Keep a zoomed rendering inside its own cell instead of
          // painting over the neighbouring map cells.
          ui::IntersectClip clip(g, cellRc);
          if (clip) {
            g->drawSurface(activeSurfs[i].get(),
                           gfx::Rect(0, 0, sw, sh),
                           gfx::Rect(dx, dy, dw, dh),
                           os::Sampling(os::Sampling::Filter::Nearest),
                           nullptr);

            if (i == 0) {
              const int px = dx + int(m_pivScreenX * scale);
              const int py = dy + int(m_pivScreenY * scale);
              if (cellRc.contains(gfx::Point(px, py))) {
                const gfx::Color pivCol = gfx::rgba(250, 204, 21, 230);
                g->drawHLine(pivCol, px - 4 * s, py, 9 * s);
                g->drawVLine(pivCol, px, py - 4 * s, 9 * s);
              }
            }
          }
        }

        const gfx::Size lblSz = g->measureText(activeLabels[i]);
        const gfx::Rect badgeRc(cellRc.x + 3 * s, cellRc.y + 3 * s, lblSz.w + 6 * s, lblSz.h + 2 * s);
        g->fillRect(gfx::rgba(28, 35, 43, 195), badgeRc);
        g->drawText(activeLabels[i],
                    gfx::rgba(255, 255, 255),
                    gfx::ColorNone,
                    gfx::Point(badgeRc.x + 3 * s, badgeRc.y + 1 * s));
      }
    }

    if (std::abs(m_zoom - 1.0) > 1e-9) {
      const std::string zoomText = fmt::format("{}%", int(std::round(m_zoom * 100.0)));
      const gfx::Size zoomSz = g->measureText(zoomText);
      const gfx::Rect zoomRc(rc.x + rc.w - zoomSz.w - 9 * s, rc.y + 3 * s,
                             zoomSz.w + 6 * s, zoomSz.h + 2 * s);
      g->fillRect(gfx::rgba(28, 35, 43, 195), zoomRc);
      g->drawText(zoomText,
                  gfx::rgba(255, 255, 255),
                  gfx::ColorNone,
                  gfx::Point(zoomRc.x + 3 * s, zoomRc.y + 1 * s));
    }
  }

private:
  os::SurfaceRef m_diffSurf;
  os::SurfaceRef m_normSurf;
  os::SurfaceRef m_deptSurf;
  int m_yawDeg = 45;
  int m_pitchDeg = 30;
  int m_pivScreenX = 40;
  int m_pivScreenY = 40;
  bool m_showDiff = true;
  bool m_showNorm = false;
  bool m_showDept = false;
  bool m_dragging = false;
  gfx::Point m_dragStart;
  int m_startYaw = 45;
  int m_startPitch = 30;
  double m_zoom = 1.0;
  int m_panX = 0;
  int m_panY = 0;
  int m_bgR = 0;
  int m_bgG = 0;
  int m_bgB = 0;
  int m_bgA = 0;
  bool m_panning = false;
  gfx::Point m_panStart;
  int m_startPanX = 0;
  int m_startPanY = 0;
};

// Sleek dark-slate container matching reference UI controls
class FieldsPanelBox : public ui::VBox {
public:
  FieldsPanelBox()
  {
    setExpansive(true);
  }
protected:
  void onPaint(ui::PaintEvent& ev) override
  {
    ui::Graphics* g = ev.graphics();
    const gfx::Rect rc = clientBounds();
    g->fillRect(gfx::rgba(45, 55, 66), rc);
    g->drawRect(gfx::rgba(28, 35, 43), rc);
  }
};

static void setupSixSurfacesOnDoc(Doc* doc, bool seedStarterIfBlank,
                                  bool seedSinglePixel = false)
{
  // Entering the studio now sets up the shared UV sheets, not
  // per-face layers. setupSharedSurfaceLayers migrates any existing
  // per-face Base/Texture images into their UV cells and removes
  // the old layers, so opening an old document upgrades it in
  // place instead of leaving 12+ layers behind.
  setupSharedSurfaceLayers(doc, seedStarterIfBlank, seedSinglePixel);
}

static bool extractSixSurfaces(
  Doc* doc,
  doc::frame_t frame,
  SixSurfaceData& out,
  bool useFaceDepth = true,
  DocView* docView = nullptr,
  const std::array<bool, 3>* colLinked = nullptr)
{
  if (!doc || !doc->sprite()) return false;
  doc::Sprite* spr = doc->sprite();
  const int sw = spr->width();
  const int sh = spr->height();
  // Every face is re-sized below; a canvas-sized init only wasted memory
  // (S x S per face), which matters once objects stack bands downwards.
  out.init(8);

  doc::ImageRef depthSrcImg;
  if (useFaceDepth) {
    if (doc::LayerImage* depthLyr = findLayerByName(spr, "Surface_DEPTH")) {
      if (const doc::Cel* dCel = depthLyr->cel(frame)) {
        if (dCel->image() && dCel->image()->pixelFormat() == doc::IMAGE_RGB) {
          depthSrcImg.reset(doc::Image::create(doc::IMAGE_RGB, sw, sh));
          depthSrcImg->clear(0);
          doc::copy_image(depthSrcImg.get(), dCel->image(), dCel->x(), dCel->y());
          out.hasCustomDepthLayer = true;
        }
      }
    }
  }

  ExtraCelRef extra = doc->extraCel();

  // Shared sheets, if present, decide both the layers and the rect:
  // each face samples its own UV rect of the same Base/Texture
  // sheets, instead of its own per-face layers.
  const SharedSurfaceLayers shared = findSharedLayers(spr);
  const bool useShared = (shared.baseLayer || shared.textureLayer);

  for (int f = 0; f < kFaceCount; ++f) {
    doc::Slice* slice = findSliceForFace(spr, f);
    gfx::Rect faceRect(0, 0, sw, sh);
    if (slice) {
      const auto* key = slice->getByFrame(frame);
      if (key && !key->bounds().isEmpty()) {
        faceRect = key->bounds();
      }
    }
    if (useShared) {
      // Recompute from the grid so a slice left at (0, 0) by the
      // old layout does not make every face sample the same cell.
      // sharedFaceRect keeps the slice's face size, only the
      // origin moves to the face's UV cell.
      faceRect = sharedFaceRect(spr, f);
    }
    out.bounds[f] = faceRect;
    out.setFaceSize(f, faceRect.w, faceRect.h);
    // The buffers are sized by the (clamped) face size: index with it.
    const int fw = out.w[f];
    const int fh = out.h[f];
    out.hasSlices = true;

    doc::LayerImage* baseLyr = shared.baseLayer;
    doc::LayerImage* texLyr = shared.textureLayer;
    if (!useShared) {
      SurfaceLayerPair sp = findSurfaceLayers(spr, f);
      baseLyr = sp.baseLayer;
      texLyr = sp.texLayer;
    }

    // If no dedicated surface layer found, check shared layer
    if (!baseLyr && !texLyr) {
      for (doc::Layer* l : spr->allLayers()) {
        if (l->isImage() && l->isVisible()) {
          baseLyr = static_cast<doc::LayerImage*>(l);
          break;
        }
      }
    }

    const doc::Cel* baseCel = baseLyr ? baseLyr->cel(frame) : nullptr;
    const doc::Cel* texCel = texLyr ? texLyr->cel(frame) : nullptr;
    const doc::Image* baseImg = baseCel ? baseCel->image() : nullptr;
    const doc::Image* texImg = texCel ? texCel->image() : nullptr;

    const int baseOx = baseCel ? baseCel->x() : 0;
    const int baseOy = baseCel ? baseCel->y() : 0;
    const int texOx = texCel ? texCel->x() : 0;
    const int texOy = texCel ? texCel->y() : 0;

    auto getRgbaPixel = [&](const doc::Image* img, int x, int y) -> uint32_t {
      if (!img || x < 0 || x >= img->width() || y < 0 || y >= img->height())
        return 0;
      switch (img->pixelFormat()) {
        case doc::IMAGE_RGB:
          return doc::get_pixel_fast<doc::RgbTraits>(img, x, y);
        case doc::IMAGE_GRAYSCALE: {
          const doc::color_t c = doc::get_pixel_fast<doc::GrayscaleTraits>(img, x, y);
          const int val = doc::graya_getv(c);
          return doc::rgba(val, val, val, doc::graya_geta(c));
        }
        case doc::IMAGE_INDEXED: {
          const doc::color_t idx = doc::get_pixel_fast<doc::IndexedTraits>(img, x, y);
          if (idx == spr->transparentColor())
            return 0;
          if (const doc::Palette* pal = spr->palette(frame))
            return pal->getEntry(idx);
          return 0;
        }
        default:
          return 0;
      }
    };

    const bool faceHasBase = (baseLyr != nullptr);
    out.hasBaseLayer[f] = faceHasBase;

    bool hasPixels = false;

    for (int v = 0; v < fh; ++v) {
      const int canvasY = faceRect.y + v;
      for (int u = 0; u < fw; ++u) {
        const int canvasX = faceRect.x + u;

        const uint32_t baseCol = getRgbaPixel(baseImg, canvasX - baseOx, canvasY - baseOy);
        const uint32_t texCol = getRgbaPixel(texImg, canvasX - texOx, canvasY - texOy);
        const uint8_t bA = doc::rgba_geta(baseCol);
        out.baseAlpha[f][v * fw + u] = bA;

        uint32_t finalCol = 0;
        if (faceHasBase) {
          // Base sheet strictly defines the silhouette boundary:
          // If base pixel is transparent (bA <= 16), content is clamped to 0.
          if (bA > 16) {
            if (texImg) {
              const int aT = doc::rgba_geta(texCol);
              if (aT == 255) {
                finalCol = texCol;
              }
              else if (aT > 0) {
                const int invA = 255 - aT;
                finalCol = doc::rgba(
                  (doc::rgba_getr(texCol) * aT + doc::rgba_getr(baseCol) * invA) / 255,
                  (doc::rgba_getg(texCol) * aT + doc::rgba_getg(baseCol) * invA) / 255,
                  (doc::rgba_getb(texCol) * aT + doc::rgba_getb(baseCol) * invA) / 255,
                  bA);
              }
              else {
                finalCol = baseCol;
              }
            }
            else {
              finalCol = baseCol;
            }
          }
        }
        else if (texImg) {
          finalCol = texCol;
        }

        if (doc::rgba_geta(finalCol) > 0) {
          out.pixels[f][v * fw + u] = finalCol;
          hasPixels = true;
        }
      }
    }

    // Check if extra cel applies to this face editor
    if (extra && extra->type() != render::ExtraType::NONE &&
        extra->purpose() != ExtraCel::Purpose::BrushPreview &&
        extra->image() && extra->cel() &&
        docView && docView->editor() &&
        (docView->editor()->layer() == baseLyr || docView->editor()->layer() == texLyr)) {
      const doc::Image* eImg = extra->image();
      const int ox = extra->cel()->x();
      const int oy = extra->cel()->y();
      const bool editingBase = (docView->editor()->layer() == baseLyr);
      const bool editingTex = (docView->editor()->layer() == texLyr);

      for (int v = 0; v < fh; ++v) {
        const int canvasY = faceRect.y + v;
        const int sy = canvasY - oy;
        if (sy < 0 || sy >= eImg->height()) continue;
        for (int u = 0; u < fw; ++u) {
          const int canvasX = faceRect.x + u;
          const int sx = canvasX - ox;
          if (sx < 0 || sx >= eImg->width()) continue;
          const uint32_t ec = getRgbaPixel(eImg, sx, sy);
          if (doc::rgba_geta(ec) > 0) {
            if (editingBase) {
              out.pixels[f][v * fw + u] = ec;
              out.baseAlpha[f][v * fw + u] = doc::rgba_geta(ec);
              hasPixels = true;
            }
            else if (editingTex) {
              if (faceHasBase) {
                const int bx = canvasX - baseOx;
                const int by = canvasY - baseOy;
                const uint32_t bc = getRgbaPixel(baseImg, bx, by);
                if (doc::rgba_geta(bc) <= 16) {
                  continue; // Clamped by Base silhouette
                }
                const int aT = doc::rgba_geta(ec);
                if (aT == 255) {
                  out.pixels[f][v * fw + u] = ec;
                }
                else if (aT > 0) {
                  const int invA = 255 - aT;
                  out.pixels[f][v * fw + u] = doc::rgba(
                    (doc::rgba_getr(ec) * aT + doc::rgba_getr(bc) * invA) / 255,
                    (doc::rgba_getg(ec) * aT + doc::rgba_getg(bc) * invA) / 255,
                    (doc::rgba_getb(ec) * aT + doc::rgba_getb(bc) * invA) / 255,
                    doc::rgba_geta(bc));
                }
                else {
                  out.pixels[f][v * fw + u] = bc;
                }
                hasPixels = true;
              }
              else {
                out.pixels[f][v * fw + u] = ec;
                hasPixels = true;
              }
            }
          }
        }
      }
    }

    // If custom depth layer is present, extract depth for this face
    if (depthSrcImg) {
      for (int v = 0; v < fh; ++v) {
        const int canvasY = faceRect.y + v;
        if (canvasY < 0 || canvasY >= depthSrcImg->height()) continue;
        for (int u = 0; u < fw; ++u) {
          const int canvasX = faceRect.x + u;
          if (canvasX < 0 || canvasX >= depthSrcImg->width()) continue;
          const uint32_t dc = doc::get_pixel_fast<doc::RgbTraits>(depthSrcImg.get(), canvasX, canvasY);
          out.depthPixels[f][v * fw + u] = clampByte(
            doc::rgba_getr(dc) * 0.299f + doc::rgba_getg(dc) * 0.587f + doc::rgba_getb(dc) * 0.114f);
          out.depthAlpha[f][v * fw + u] = doc::rgba_geta(dc);
        }
      }
      // A face whose depth area has nothing painted at all is FLAT, not a
      // through-hole (2026-11-17): an empty Surface_DEPTH sheet used to make
      // the whole model vanish, because transparent depth = hole. Holes are
      // still made by erasing depth where other depth pixels exist.
      bool anyDepth = false;
      for (uint8_t a : out.depthAlpha[f])
        if (a > 16) {
          anyDepth = true;
          break;
        }
      if (!anyDepth) {
        std::fill(out.depthPixels[f].begin(), out.depthPixels[f].end(), uint8_t(128));
        std::fill(out.depthAlpha[f].begin(), out.depthAlpha[f].end(), uint8_t(255));
      }
    }

    // Only populate starter pixels if the sprite has NO established shared layers
    // and NO base/texture layers (pure virgin document initialization).
    if (!hasPixels && !useShared && !baseLyr && !texLyr) {
      for (int v = 0; v < fh; ++v) {
        for (int u = 0; u < fw; ++u) {
          out.pixels[f][v * fw + u] = starterSurfacePixel(f, u, v, fw, fh);
        }
      }
    }
  }

  // Unify linked opposing face pairs (Top/Down, Front/Back, Right/Left).
  // The copy is MIRRORED (2026-11-16): the renderer and the ghost hints
  // sample Down with Z = 1 - v, Back with X = 1 - u and Left with Z = u
  // (Right: 1 - u), so a linked face must be the flipped source or the two
  // silhouettes disagree by twice the shape's off-centre offset, which
  // showed up as slits / holes in the preview with Cutouts on.
  if (colLinked) {
    for (int c = 0; c < 3; ++c) {
      out.colLinked[c] = (*colLinked)[c];
      if ((*colLinked)[c]) {
        const int topFace = c * 2;
        const int btmFace = c * 2 + 1;
        const bool flipV = (c == 0);
        const int fw = out.w[topFace], fh = out.h[topFace];
        auto mirrored = [&](const auto& src) {
          auto dst = src;
          if (int(src.size()) == fw * fh)
            for (int v = 0; v < fh; ++v)
              for (int u = 0; u < fw; ++u)
                dst[size_t(v) * fw + u] =
                  src[size_t(flipV ? fh - 1 - v : v) * fw + (flipV ? u : fw - 1 - u)];
          return dst;
        };
        out.pixels[btmFace] = mirrored(out.pixels[topFace]);
        out.depthPixels[btmFace] = mirrored(out.depthPixels[topFace]);
        out.depthAlpha[btmFace] = mirrored(out.depthAlpha[topFace]);
        out.baseAlpha[btmFace] = mirrored(out.baseAlpha[topFace]);
        out.hasBaseLayer[btmFace] = out.hasBaseLayer[topFace];
        out.w[btmFace] = out.w[topFace];
        out.h[btmFace] = out.h[topFace];
        out.bounds[btmFace] = out.bounds[topFace];
      }
    }
  }

  return true;
}

// Per-panel grid (G2, 2026-11-07, William): a grid overlay per
// face, stored per panel, never document pixels. Both axes are
// stored (H = horizontal lines, spaced down the face height;
// V = vertical lines, spaced across the face width); the pop-up's
// H/V toggle picks which axis
// the entries edit. Cell = (faceSize - (n-1)*s) / n; lines sit at
// cell boundaries; if the cell comes out <= 0 the numbers do not
// fit that face and that axis draws no lines. The grid is an
// overlay only: it never writes a cel image and never appears in
// undo history. In-memory only, like the G1 panel splits —
// closing the studio forgets the grids.
struct PanelGridSettings {
  bool on = false;
  int countH = 4;
  int spacingH = 2;
  int countV = 4;
  int spacingV = 2;
};

// Grid line positions (float face-pixel coords) for one axis, or
// false when the numbers do not fit the face (cell <= 0).
// Mirrors panel_goals_sim.py grid_lines().
static bool gridLinePositions(int faceSize, int count, int spacing,
                              std::vector<float>& outLines)
{
  outLines.clear();
  if (count < 1 || spacing < 0)
    return false;
  const float cell = (float(faceSize) - float(count - 1) * float(spacing)) / float(count);
  if (cell <= 0.0f)
    return false;
  for (int i = 0; i < count - 1; ++i)
    outLines.push_back(float(i + 1) * cell + float(i) * float(spacing));
  return true;
}

// Is rendered face pixel u on grid line `line`? A pixel is on
// the line when its centre is within half a pixel of it, so a
// half-integer line lights exactly one pixel and an integer line
// straddles two. Mirrors panel_goals_sim.py on_line().
static bool gridPixelOnLine(int u, float line)
{
  return std::fabs((float(u) + 0.5f) - line) <= 0.5f;
}

struct Render3DResult {
  doc::ImageRef diffImg;
  doc::ImageRef normImg;
  doc::ImageRef deptImg;
  int outW = 0;
  int outH = 0;
  float cxScreen = 0.0f;
  float cyScreen = 0.0f;
  // Screen pixels where a ray hit a voxel whose surface texel is
  // transparent, i.e. what Cutouts carves away (counted even when the
  // pixel is dropped). 0 = no slits in the surface.
  int cutoutPx = 0;
  // Composite previews: screen pixels won by each object (input order).
  std::vector<int> objHitPx;
  // Time spent building voxel volumes (the rest of a render is ray marching).
  double volumeMs = 0.0;
};

// One object of a composite preview (Objects, 2026-11-17). Each object is
// rendered with its own voxel volume, placed at its world centre; all share
// one camera. Where two objects' surfaces coincide (within half a pixel) the
// higher priority wins; elsewhere the nearer surface wins, as in any union.
struct PreviewObject {
  const SixSurfaceData* data = nullptr;
  float cx = 0.0f, cy = 0.0f, cz = 0.0f; // world centre of its Base box (Y down)
  int priority = 0;
  bool grids = true; // draw the per-panel grids on this object
};

static Render3DResult compute3DPreviewMulti(
  const std::vector<PreviewObject>& objects,
  int yawDeg,
  int pitchDeg,
  int pivX,
  int pivY,
  int pivZ,
  int renderStyle,
  bool addOutline,
  bool allowCutouts,
  const std::array<PanelGridSettings, kFaceCount>* grids,
  bool tintCutouts)
{
  Render3DResult res;

  struct Obj {
    const SixSurfaceData* data = nullptr;
    float cx = 0.0f, cy = 0.0f, cz = 0.0f;
    int priority = 0;
    bool grids = false;
    float halfX = 0.0f, halfY = 0.0f, halfZ = 0.0f;
    float halfXr = 0.0f, halfYr = 0.0f, halfZr = 0.0f;
    float stepSize = 1.0f;
    std::shared_ptr<const SculptedVoxelVolume> vol;
  };
  std::vector<Obj> objs;
  objs.reserve(objects.size());
  for (const PreviewObject& p : objects) {
    if (!p.data)
      continue;
    const SixSurfaceData& data = *p.data;
    Obj o;
    o.data = p.data;
    o.cx = p.cx;
    o.cy = p.cy;
    o.cz = p.cz;
    o.priority = p.priority;
    o.grids = p.grids;
    o.halfX = std::max({ data.w[kFaceFront], data.w[kFaceBack], data.w[kFaceTop], data.w[kFaceDown] }) * 0.5f;
    o.halfY = std::max({ data.h[kFaceFront], data.h[kFaceBack], data.h[kFaceLeft], data.h[kFaceRight] }) * 0.5f;
    o.halfZ = std::max({ data.w[kFaceLeft], data.w[kFaceRight], data.h[kFaceTop], data.h[kFaceDown] }) * 0.5f;

    {
      const auto tv = std::chrono::steady_clock::now();
      o.vol = cachedSculptedVolume(data, pivX, pivY, pivZ);
      res.volumeMs += std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - tv)
                        .count();
    }

    // Render half-extents (2026-11-07, depth sculpting): when depth
    // sculpting grew the volume, the ray clip box grows with it so the
    // Base box keeps its world size and Plus bumps stick out beyond it
    // instead of squeezing the whole model. The voxel mapping below
    // uses these; the face-UV mapping keeps halfX/halfY/halfZ because
    // a face's lateral extent never changes. When nothing is sculpted
    // vol dims equal the base dims and these are exactly halfX/Y/Z.
    const int dimX = std::min(kMaxVoxelDim, std::max({ data.w[kFaceFront], data.w[kFaceBack], data.w[kFaceTop], data.w[kFaceDown], 8 }));
    const int dimY = std::min(kMaxVoxelDim, std::max({ data.h[kFaceFront], data.h[kFaceBack], data.h[kFaceLeft], data.h[kFaceRight], 8 }));
    const int dimZ = std::min(kMaxVoxelDim, std::max({ data.w[kFaceLeft], data.w[kFaceRight], data.h[kFaceTop], data.h[kFaceDown], 8 }));
    const SculptedVoxelVolume& ov = *o.vol;
    o.halfXr = (ov.vx > dimX) ? o.halfX * float(ov.vx) / float(dimX) : o.halfX;
    o.halfYr = (ov.vy > dimY) ? o.halfY * float(ov.vy) / float(dimY) : o.halfY;
    o.halfZr = (ov.vz > dimZ) ? o.halfZ * float(ov.vz) / float(dimZ) : o.halfZ;
    o.stepSize = std::min({ (2.0f * o.halfXr) / float(ov.vx),
                            (2.0f * o.halfYr) / float(ov.vy),
                            (2.0f * o.halfZr) / float(ov.vz) }) * 0.45f;
    objs.push_back(std::move(o));
  }
  res.objHitPx.assign(objs.size(), 0);

  // Camera framing over the union of all objects. For one object at the
  // origin these are exactly its own half-extents and a zero centre, so the
  // single-object render is unchanged bit for bit.
  float bMin[3] = { 0, 0, 0 }, bMax[3] = { 0, 0, 0 }; // Base boxes
  float rMin[3] = { 0, 0, 0 }, rMax[3] = { 0, 0, 0 }; // render (sculpt-grown) boxes
  for (size_t i = 0; i < objs.size(); ++i) {
    const Obj& o = objs[i];
    const float c[3] = { o.cx, o.cy, o.cz };
    const float hb[3] = { o.halfX, o.halfY, o.halfZ };
    const float hr[3] = { o.halfXr, o.halfYr, o.halfZr };
    for (int a = 0; a < 3; ++a) {
      const float b0 = c[a] - hb[a], b1 = c[a] + hb[a];
      const float r0 = c[a] - hr[a], r1 = c[a] + hr[a];
      bMin[a] = (i == 0) ? b0 : std::min(bMin[a], b0);
      bMax[a] = (i == 0) ? b1 : std::max(bMax[a], b1);
      rMin[a] = (i == 0) ? r0 : std::min(rMin[a], r0);
      rMax[a] = (i == 0) ? r1 : std::max(rMax[a], r1);
    }
  }
  if (objs.empty()) {
    for (int a = 0; a < 3; ++a) {
      bMin[a] = rMin[a] = -12.0f;
      bMax[a] = rMax[a] = 12.0f;
    }
  }
  const float halfX = (bMax[0] - bMin[0]) * 0.5f;
  const float halfY = (bMax[1] - bMin[1]) * 0.5f;
  const float halfZ = (bMax[2] - bMin[2]) * 0.5f;
  const float halfXr = (rMax[0] - rMin[0]) * 0.5f;
  const float halfYr = (rMax[1] - rMin[1]) * 0.5f;
  const float halfZr = (rMax[2] - rMin[2]) * 0.5f;
  const float camCX = (rMax[0] + rMin[0]) * 0.5f;
  const float camCY = (rMax[1] + rMin[1]) * 0.5f;
  const float camCZ = (rMax[2] + rMin[2]) * 0.5f;
  const int maxDim = std::max({ int(halfXr * 2.0f), int(halfYr * 2.0f), int(halfZr * 2.0f), 24 });
  const int outW = maxDim * 2 + 16;
  const int outH = maxDim * 2 + 16;

  res.outW = outW;
  res.outH = outH;

  constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;
  const float yaw = yawDeg * kDegToRad;
  const float pitch = pitchDeg * kDegToRad;
  const float cy = std::cos(yaw), sy = std::sin(yaw);
  const float cp = std::cos(pitch), sp = std::sin(pitch);

  // Screen-right axis, with its sign corrected: the old axis
  // (cy, 0, -sy) made screen-right x screen-down = view direction,
  // which is a left-handed (mirrored) camera in this Y-down world,
  // where screen-right x screen-up must equal the view direction.
  // With the old sign, a straight-on Front view (yaw 180) put
  // world +X on screen left, so every face texture in Preview
  // (and in bake3DToDoc, which shares this function) appeared
  // mirrored left-to-right, and Front/Right swapped screen sides.
  // Negating only this axis keeps screen-down and the view
  // direction unchanged, so vertical orientation, depth, and
  // which faces are visible are unchanged; only the mirror goes.
  const float rxX = -cy,     rxY = 0.0f, rzX = sy;
  const float ryX = sy * sp, ryY = cp,   ryZ = cy * sp;
  const float vdX = sy * cp, vdY = -sp,  vdZ = cy * cp;

  const float pivOffX = ((pivX - 50) * 0.01f) * (halfX * 2.0f);
  const float pivOffY = ((pivY - 50) * 0.01f) * (halfY * 2.0f);
  const float pivOffZ = ((pivZ - 50) * 0.01f) * (halfZ * 2.0f);
  const float camOffX = pivOffX + camCX;
  const float camOffY = pivOffY + camCY;
  const float camOffZ = pivOffZ + camCZ;

  const float cxScreen = outW * 0.5f;
  const float cyScreen = outH * 0.5f;
  res.cxScreen = cxScreen;
  res.cyScreen = cyScreen;

  res.diffImg.reset(doc::Image::create(doc::IMAGE_RGB, outW, outH));
  res.normImg.reset(doc::Image::create(doc::IMAGE_RGB, outW, outH));
  res.deptImg.reset(doc::Image::create(doc::IMAGE_RGB, outW, outH));
  res.diffImg->clear(0);
  res.normImg->clear(0);
  res.deptImg->clear(0);

  std::vector<int> faceMap(outW * outH, -1);
  std::vector<int> objMap(outW * outH, -1);
  std::vector<float> zBuf(objs.size() > 1 ? size_t(outW) * outH : 0, 0.0f);
  std::vector<int> zPrio(zBuf.size(), 0);
  const float maxDepthRange = float(maxDim) * 1.732f;
  const float Lx = -0.45f, Ly = -0.60f, Lz = 0.66f;

  // Per-panel grid line positions (G2), computed once per render
  // — never per pixel. A face whose grid is off, or whose numbers
  // do not fit it, keeps empty line lists and draws no lines.
  // Only the object marked `grids` (the focused one) gets them.
  std::vector<float> gridXLines[kFaceCount];
  std::vector<float> gridYLines[kFaceCount];
  bool gridOnFace[kFaceCount] = {};
  const SixSurfaceData* gridData = nullptr;
  for (const Obj& o : objs)
    if (o.grids && !gridData)
      gridData = o.data;
  if (grids && gridData) {
    for (int f = 0; f < kFaceCount; ++f) {
      gridOnFace[f] = (*grids)[f].on;
      if (!gridOnFace[f])
        continue;
      // X lines are vertical (Vertical settings); Y lines are
      // horizontal (Horizontal settings). William, 2026-11-07:
      // Horizontal gives horizontal lines.
      gridLinePositions(gridData->w[f], (*grids)[f].countV, (*grids)[f].spacingV, gridXLines[f]);
      gridLinePositions(gridData->h[f], (*grids)[f].countH, (*grids)[f].spacingH, gridYLines[f]);
    }
  }

  const float camDist = maxDepthRange * 2.5f;
  const float nearPlane = 1.0f;
  const float farPlane = camDist * 2.0f + maxDepthRange;

  for (int py = 0; py < outH; ++py) {
    const float camY = (py - cyScreen);
    uint32_t* __restrict diffRow =
      reinterpret_cast<uint32_t*>(res.diffImg->getPixelAddress(0, py));
    uint32_t* __restrict normRow =
      reinterpret_cast<uint32_t*>(res.normImg->getPixelAddress(0, py));
    uint32_t* __restrict deptRow =
      reinterpret_cast<uint32_t*>(res.deptImg->getPixelAddress(0, py));

    for (int px = 0; px < outW; ++px) {
      const float camX = (px - cxScreen);
      const float wox = (camX * rxX + camY * ryX + camOffX) - camDist * vdX;
      const float woy = (camX * rxY + camY * ryY + camOffY) - camDist * vdY;
      const float woz = (camX * rzX + camY * ryZ + camOffZ) - camDist * vdZ;
      const size_t pidx = size_t(py) * outW + px;

      for (size_t oi = 0; oi < objs.size(); ++oi) {
      const Obj& O = objs[oi];
      const SixSurfaceData& data = *O.data;
      const SculptedVoxelVolume& vol = *O.vol;
      const float oHalfX = O.halfX, oHalfY = O.halfY, oHalfZ = O.halfZ;
      const float oHalfXr = O.halfXr, oHalfYr = O.halfYr, oHalfZr = O.halfZr;
      // Ray origin in the object's own frame (its Base box centred at 0).
      const float ox = wox - O.cx;
      const float oy = woy - O.cy;
      const float oz = woz - O.cz;

      float t0 = -1e9f, t1 = 1e9f;
      auto clipAxis = [&](float orig, float dir, float bMin, float bMax) -> bool {
        if (std::fabs(dir) > 1e-5f) {
          float invD = 1.0f / dir;
          float ta = (bMin - orig) * invD;
          float tb = (bMax - orig) * invD;
          if (ta > tb) std::swap(ta, tb);
          if (ta > t0) t0 = ta;
          if (tb < t1) t1 = tb;
          return t0 <= t1;
        }
        return (orig >= bMin && orig <= bMax);
      };

      if (!clipAxis(ox, vdX, -oHalfXr, oHalfXr) ||
          !clipAxis(oy, vdY, -oHalfYr, oHalfYr) ||
          !clipAxis(oz, vdZ, -oHalfZr, oHalfZr) ||
          t1 < nearPlane || t0 > farPlane || t0 > t1) {
        continue;
      }

      const float stepSize = O.stepSize;
      float tStart = std::max(nearPlane, t0);
      float tEnd = std::min(farPlane, t1);
      float tHit = -1.0f;
      int hitVx = -1, hitVy = -1, hitVz = -1;

      for (float t = tStart; t <= tEnd; t += stepSize) {
        const float pxW = ox + t * vdX;
        const float pyW = oy + t * vdY;
        const float pzW = oz + t * vdZ;

        int vx = int((pxW + oHalfXr) / (2.0f * oHalfXr) * float(vol.vx));
        int vy = int((pyW + oHalfYr) / (2.0f * oHalfYr) * float(vol.vy));
        int vz = int((pzW + oHalfZr) / (2.0f * oHalfZr) * float(vol.vz));

        if (vol.isSolid(vx, vy, vz)) {
          float ta = t - stepSize;
          float tb = t;
          for (int b = 0; b < 3; ++b) {
            float tm = (ta + tb) * 0.5f;
            int mx = int((ox + tm * vdX + oHalfXr) / (2.0f * oHalfXr) * float(vol.vx));
            int my = int((oy + tm * vdY + oHalfYr) / (2.0f * oHalfYr) * float(vol.vy));
            int mz = int((oz + tm * vdZ + oHalfZr) / (2.0f * oHalfZr) * float(vol.vz));
            if (vol.isSolid(mx, my, mz)) {
              tb = tm;
              vx = mx; vy = my; vz = mz;
            }
            else {
              ta = tm;
            }
          }
          tHit = tb;
          hitVx = vx; hitVy = vy; hitVz = vz;
          break;
        }
      }

      if (tHit < 0.0f) continue;

      // Composite depth test (several objects only): the nearer surface
      // wins; within half a pixel the higher priority wins.
      if (!zBuf.empty() && objMap[pidx] >= 0) {
        constexpr float kTie = 0.5f;
        if (tHit > zBuf[pidx] + kTie)
          continue;
        if (tHit >= zBuf[pidx] - kTie && O.priority <= zPrio[pidx])
          continue;
      }

      float gX = float(vol.isSolid(hitVx - 1, hitVy, hitVz)) - float(vol.isSolid(hitVx + 1, hitVy, hitVz));
      float gY = float(vol.isSolid(hitVx, hitVy - 1, hitVz)) - float(vol.isSolid(hitVx, hitVy + 1, hitVz));
      float gZ = float(vol.isSolid(hitVx, hitVy, hitVz - 1)) - float(vol.isSolid(hitVx, hitVy, hitVz + 1));
      float len = std::sqrt(gX * gX + gY * gY + gZ * gZ);
      if (len > 1e-4f) {
        gX /= len; gY /= len; gZ /= len;
      }
      else {
        gX = (ox + tHit * vdX) / oHalfX;
        gY = (oy + tHit * vdY) / oHalfY;
        gZ = (oz + tHit * vdZ) / oHalfZ;
        len = std::sqrt(gX * gX + gY * gY + gZ * gZ);
        if (len > 1e-4f) { gX /= len; gY /= len; gZ /= len; }
        else { gX = 0.0f; gY = 0.0f; gZ = 1.0f; }
      }

      int face = kFaceFront;
      if (std::fabs(gZ) >= std::fabs(gX) && std::fabs(gZ) >= std::fabs(gY)) {
        face = (gZ > 0.0f ? kFaceFront : kFaceBack);
      }
      else if (std::fabs(gY) >= std::fabs(gX) && std::fabs(gY) >= std::fabs(gZ)) {
        face = (gY < 0.0f ? kFaceTop : kFaceDown);
      }
      else {
        face = (gX > 0.0f ? kFaceRight : kFaceLeft);
      }

      const float hx = ox + tHit * vdX;
      const float hy = oy + tHit * vdY;
      const float hz = oz + tHit * vdZ;

      const int fw = data.w[face];
      const int fh = data.h[face];
      int u = 0, v = 0;
      switch (face) {
        case kFaceFront:
          u = std::clamp(int((hx + oHalfX) / (2.0f * oHalfX) * fw), 0, fw - 1);
          v = std::clamp(int((hy + oHalfY) / (2.0f * oHalfY) * fh), 0, fh - 1);
          break;
        case kFaceBack:
          u = std::clamp(int((oHalfX - hx) / (2.0f * oHalfX) * fw), 0, fw - 1);
          v = std::clamp(int((hy + oHalfY) / (2.0f * oHalfY) * fh), 0, fh - 1);
          break;
        case kFaceLeft:
          u = std::clamp(int((hz + oHalfZ) / (2.0f * oHalfZ) * fw), 0, fw - 1);
          v = std::clamp(int((hy + oHalfY) / (2.0f * oHalfY) * fh), 0, fh - 1);
          break;
        case kFaceRight:
          u = std::clamp(int((oHalfZ - hz) / (2.0f * oHalfZ) * fw), 0, fw - 1);
          v = std::clamp(int((hy + oHalfY) / (2.0f * oHalfY) * fh), 0, fh - 1);
          break;
        case kFaceTop:
          u = std::clamp(int((hx + oHalfX) / (2.0f * oHalfX) * fw), 0, fw - 1);
          v = std::clamp(int((hz + oHalfZ) / (2.0f * oHalfZ) * fh), 0, fh - 1);
          break;
        case kFaceDown:
          u = std::clamp(int((hx + oHalfX) / (2.0f * oHalfX) * fw), 0, fw - 1);
          v = std::clamp(int((oHalfZ - hz) / (2.0f * oHalfZ) * fh), 0, fh - 1);
          break;
      }

      const uint32_t texCol = data.sampleSymmetric(face, u, v);
      const uint8_t a = doc::rgba_geta(texCol);
      if (a <= 16 && renderStyle != kRenderColoredFaces) {
        if (allowCutouts) {
          ++res.cutoutPx;
          if (!tintCutouts)
            continue;
          diffRow[px] = doc::rgba(255, 0, 255, 255);
          faceMap[pidx] = face;
          objMap[pidx] = int(oi);
          if (!zBuf.empty()) {
            zBuf[pidx] = tHit;
            zPrio[pidx] = O.priority;
          }
          continue;
        }
      }

      const float dC = data.sampleDepthNorm(face, u, v);
      const float dL = data.sampleDepthNorm(face, u - 1, v);
      const float dR = data.sampleDepthNorm(face, u + 1, v);
      const float dU = data.sampleDepthNorm(face, u, v - 1);
      const float dD = data.sampleDepthNorm(face, u, v + 1);
      const float gradScale = data.hasCustomDepthLayer ? 1.45f : 0.65f;
      const float du = (dL - dR) * gradScale;
      const float dv = (dD - dU) * gradScale;

      float cnx = gX * rxX + gY * ryX + gZ * rzX + du;
      float cny = -(gX * ryX + gY * ryY + gZ * ryZ) + dv;
      float cnz = std::fabs(gX * vdX + gY * vdY + gZ * vdZ) + 0.15f;
      const float nLen = std::sqrt(cnx * cnx + cny * cny + cnz * cnz);
      if (nLen > 1e-4f) {
        cnx /= nLen; cny /= nLen; cnz /= nLen;
      }

      const uint8_t nr = clampByte((cnx * 0.5f + 0.5f) * 255.0f);
      const uint8_t ng = clampByte((cny * 0.5f + 0.5f) * 255.0f);
      const uint8_t nb = clampByte((cnz * 0.5f + 0.5f) * 255.0f);
      const uint32_t normPixel = doc::rgba(nr, ng, nb, 255);
      normRow[px] = normPixel;

      const float tRel = tHit - camDist;
      const float normZ = std::clamp((tRel + maxDepthRange * 0.5f) / maxDepthRange, 0.0f, 1.0f);
      const float faceRelief = (dC - 0.5f) * (data.hasCustomDepthLayer ? 0.35f : 0.14f);
      const uint8_t dByte = clampByte(35.0f + std::clamp(normZ + faceRelief, 0.0f, 1.0f) * 215.0f);
      const uint32_t deptPixel = doc::rgba(dByte, dByte, dByte, 255);
      deptRow[px] = deptPixel;

      const float ndotl = std::max(0.0f, cnx * Lx + (-cny) * Ly + cnz * Lz);
      const float shade = std::clamp(0.84f + ndotl * 0.16f, 0.84f, 1.0f);

      switch (renderStyle) {
        case kRenderTexturedFlat:
          diffRow[px] = doc::rgba(doc::rgba_getr(texCol),
                                  doc::rgba_getg(texCol),
                                  doc::rgba_getb(texCol), 255);
          break;
        case kRenderColoredFaces: {
          const auto& spec = kFaceSpecs[face];
          const float fShade = std::clamp(0.75f + ndotl * 0.25f, 0.75f, 1.0f);
          diffRow[px] = doc::rgba(clampByte(spec.r * fShade),
                                  clampByte(spec.g * fShade),
                                  clampByte(spec.b * fShade), 255);
          break;
        }
        case kRenderNormalShaded:
          diffRow[px] = normPixel;
          break;
        case kRenderDepthShaded:
          diffRow[px] = deptPixel;
          break;
        case kRenderWireframeTex: {
          bool isVoxelEdge = (hitVx == 0 || hitVy == 0 || hitVz == 0 ||
                              hitVx == vol.vx - 1 || hitVy == vol.vy - 1 || hitVz == vol.vz - 1 ||
                              !vol.isSolid(hitVx - 1, hitVy, hitVz) || !vol.isSolid(hitVx + 1, hitVy, hitVz) ||
                              !vol.isSolid(hitVx, hitVy - 1, hitVz) || !vol.isSolid(hitVx, hitVy + 1, hitVz) ||
                              !vol.isSolid(hitVx, hitVy, hitVz - 1) || !vol.isSolid(hitVx, hitVy, hitVz + 1));
          if (isVoxelEdge) {
            diffRow[px] = doc::rgba(14, 165, 233, 255);
          }
          else {
            diffRow[px] = doc::rgba(clampByte(doc::rgba_getr(texCol) * shade * 0.75f),
                                    clampByte(doc::rgba_getg(texCol) * shade * 0.75f),
                                    clampByte(doc::rgba_getb(texCol) * shade * 0.75f), 255);
          }
          break;
        }
        case kRenderTexturedLit:
        default:
          diffRow[px] = doc::rgba(clampByte(doc::rgba_getr(texCol) * shade),
                                  clampByte(doc::rgba_getg(texCol) * shade),
                                  clampByte(doc::rgba_getb(texCol) * shade), 255);
          break;
      }
      // Per-panel grid (G2): overlay on the Diffuse map only.
      // The ray already computed the hit face and its (u, v), so
      // a grid line is a face-pixel test, not a screen-space
      // projection — a face that is not visible from this angle
      // simply has no hit pixels and shows no grid. Normal and
      // Depth maps are ungridded data views. The flat light-grey
      // override is not shaded, so the line stays visible on any
      // face colour. The later addOutline pass may tint a grid
      // pixel where two faces meet on screen; rare, harmless.
      if (O.grids && gridOnFace[face]) {
        bool onGrid = false;
        for (float line : gridXLines[face]) {
          if (gridPixelOnLine(u, line)) {
            onGrid = true;
            break;
          }
        }
        if (!onGrid) {
          for (float line : gridYLines[face]) {
            if (gridPixelOnLine(v, line)) {
              onGrid = true;
              break;
            }
          }
        }
        if (onGrid)
          diffRow[px] = doc::rgba(240, 240, 240, 255);
      }
      faceMap[pidx] = face;
      objMap[pidx] = int(oi);
      if (!zBuf.empty()) {
        zBuf[pidx] = tHit;
        zPrio[pidx] = O.priority;
      }
      } // objects
    }
  }
  for (int o : objMap)
    if (o >= 0)
      ++res.objHitPx[size_t(o)];

  if (addOutline) {
    const uint32_t outlineCol = doc::rgba(15, 23, 42, 255);
    const uint32_t outNormCol = doc::rgba(128, 128, 255, 255);
    const uint32_t outDeptCol = doc::rgba(28, 28, 28, 255);

    for (int y = 1; y < outH - 1; ++y) {
      uint32_t* __restrict diffRow =
        reinterpret_cast<uint32_t*>(res.diffImg->getPixelAddress(0, y));
      uint32_t* __restrict normRow =
        reinterpret_cast<uint32_t*>(res.normImg->getPixelAddress(0, y));
      uint32_t* __restrict deptRow =
        reinterpret_cast<uint32_t*>(res.deptImg->getPixelAddress(0, y));

      for (int x = 1; x < outW - 1; ++x) {
        const int idx = y * outW + x;
        const int f = faceMap[idx];
        if (f < 0) {
          if (faceMap[idx - 1] >= 0 || faceMap[idx + 1] >= 0 ||
              faceMap[idx - outW] >= 0 || faceMap[idx + outW] >= 0) {
            diffRow[x] = outlineCol;
            normRow[x] = outNormCol;
            deptRow[x] = outDeptCol;
          }
        }
        else if (f == kFaceTop &&
                 (faceMap[idx + outW] >= 0 && faceMap[idx + outW] != kFaceTop)) {
          const uint32_t cur = diffRow[x];
          diffRow[x] = doc::rgba(
            clampByte(doc::rgba_getr(cur) * 0.68f + 253.0f * 0.32f),
            clampByte(doc::rgba_getg(cur) * 0.68f + 230.0f * 0.32f),
            clampByte(doc::rgba_getb(cur) * 0.68f + 138.0f * 0.32f),
            255);
        }
      }
    }
  }

  return res;
}

// One object at the origin: the classic single-object preview.
static Render3DResult compute3DPreviewImages(
  const SixSurfaceData& data,
  int yawDeg,
  int pitchDeg,
  int pivX,
  int pivY,
  int pivZ,
  int renderStyle = 0,
  bool addOutline = true,
  bool allowCutouts = true,
  // Per-panel grids (G2): null means no grids (baked Iso layers
  // and every pre-G2 caller). Non-null overlays the grid of the
  // hit face on the Diffuse map only.
  const std::array<PanelGridSettings, kFaceCount>* grids = nullptr,
  // Show the voxels Cutouts removes in magenta instead of dropping them.
  bool tintCutouts = false)
{
  PreviewObject one;
  one.data = &data;
  return compute3DPreviewMulti({ one }, yawDeg, pitchDeg, pivX, pivY, pivZ, renderStyle,
                               addOutline, allowCutouts, grids, tintCutouts);
}

// Six-surface data of every visible object with something painted, placed
// for a composite preview. `focusKey` gets the per-panel grids; `linked`
// returns the linked-column flags per object key.
struct CompositeInput {
  std::vector<std::unique_ptr<SixSurfaceData>> data;
  std::vector<PreviewObject> objects;
  std::vector<std::string> keys; // per entry of `objects`
};

static void buildCompositeInput(
  Doc* doc,
  doc::frame_t frame,
  bool useDepth,
  DocView* docView,
  const std::string& focusKey,
  const std::function<const std::array<bool, 3>*(const std::string&)>& linkedFor,
  CompositeInput& in)
{
  doc::Sprite* spr = doc ? doc->sprite() : nullptr;
  if (!spr)
    return;
  const std::vector<ObjectInfo> infos = resolveObjects(spr);
  for (const ObjectInfo& info : infos) {
    if (!info.visible)
      continue;
    EasymetricObjectScope sc(spr, info.key);
    auto d = std::make_unique<SixSurfaceData>();
    extractSixSurfaces(doc, frame, *d, useDepth, docView, linkedFor ? linkedFor(info.key) : nullptr);
    bool any = false;
    for (int f = 0; f < kFaceCount && !any; ++f)
      any = d->hasPixels(f);
    if (!any)
      continue; // an empty object would render as a solid box
    PreviewObject p;
    p.data = d.get();
    float c[3];
    objectWorldCentre(infos[0], info, c);
    p.cx = c[0];
    p.cy = c[1];
    p.cz = c[2];
    p.priority = info.priority;
    p.grids = (info.key == focusKey);
    in.objects.push_back(p);
    in.keys.push_back(info.key);
    in.data.push_back(std::move(d));
  }
}

static void bake3DToDoc(Doc* doc, int yawDeg = 45, int pitchDeg = 30)
{
  if (!doc || !doc->sprite()) return;
  Render3DResult res;
  if (hasObjectGroups(doc->sprite())) {
    // Objects: every visible object in one render.
    CompositeInput in;
    buildCompositeInput(doc, doc::frame_t(0), true, nullptr, std::string(), nullptr, in);
    res = compute3DPreviewMulti(in.objects, yawDeg, pitchDeg, 50, 50, 50, 0, true, true, nullptr,
                                false);
  }
  else {
    SixSurfaceData data;
    extractSixSurfaces(doc, doc::frame_t(0), data, true);
    res = compute3DPreviewImages(data, yawDeg, pitchDeg, 50, 50, 50, 0, true, true);
  }
  if (!res.diffImg || !res.normImg || !res.deptImg)
    return;

  doc::Sprite* spr = doc->sprite();
  const doc::frame_t fr = doc::frame_t(0);
  try {
    Tx tx(doc, "Render Easymetric 3D + Normal + Depth Layers", ModifyDocument);
    DocApi api = doc->getApi(tx);

    auto writeLayerImg = [&](const char* layerName, const doc::Image* srcImg) {
      doc::LayerImage* lyr = findOrCreateLayer(doc, api, spr, layerName);
      doc::ImageRef copyImg(doc::Image::createCopy(srcImg));
      if (doc::Cel* cel = lyr->cel(fr))
        api.replaceImage(spr, cel->imageRef(), copyImg);
      else
        api.addCel(lyr, fr, copyImg);
    };

    writeLayerImg("Iso_Depth", res.deptImg.get());
    writeLayerImg("Iso_Normal", res.normImg.get());
    writeLayerImg("Iso_Diffuse", res.diffImg.get());

    tx.commit();
    doc->notifyGeneralUpdate();
  }
  catch (const std::exception& e) {
    easymetricReportError("bake3DToDoc", e.what());
    // Ignore
  }
}

// Integrated 4-Column 2-Row Workspace Multi-Pane Widget
// Panel editor state: wheel = zoom, never View scroll, regardless
// of the global "zoom with wheel" preference or a customised
// mouse-wheel shortcut table (both of which made the old
// preference-forcing fix unreliable in practice: wheel over a
// surface panel still scrolled the panel and did not zoom).
// StandbyState is kept as the base, so drawing, tools and the
// temporary moving/drawing states all work as before; only
// onMouseWheel is overridden. Zoom is anchored at the mouse
// (not at the centre), so the View scroll only follows the
// zoom anchor instead of jumping, and Shift+wheel (or a
// horizontal wheel) is still horizontal scroll, so scrolling
// is still possible on purpose. Alt+wheel keeps the base
// colour-change behaviour.
// Forward declaration: the grid decorator (nested in
// EasymetricWheelZoomState below) holds a pointer to the view.
// ---------------------------------------------------------------------------
// Start from a block (2026-11-16, Arin, Phase 2): enter the block's width,
// height and depth once; the editor sizes the six cells and fills each view
// with a correctly sized, correctly placed block, so mismatched margins like
// 20 px / 22 px cannot exist in a generated layout.
//
// The block is placed ONCE in model space: its origin on each axis is
// (cell - block) / 2, rounded down. Every view is then derived from that
// through ghostAxesOfFace (the same table the ghost hints and auto-align use,
// which mirrors the renderer), so an odd leftover pixel goes to the same side
// of the model in every view (a flipped view shows it on its other side).
// ---------------------------------------------------------------------------
struct BlockLayout {
  int block[3] = { 0, 0, 0 }; // block size per model axis (X width, Y height, Z depth)
  int cell[3] = { 0, 0, 0 };  // cell size per model axis
  int origin[3] = { 0, 0, 0 }; // block origin per model axis, in the cell
};

static BlockLayout planBlockLayout(int bx, int by, int bz, int cx, int cy, int cz)
{
  BlockLayout b;
  const int B[3] = { std::clamp(bx, 1, 256), std::clamp(by, 1, 256), std::clamp(bz, 1, 256) };
  const int C[3] = { cx, cy, cz };
  for (int a = 0; a < 3; ++a) {
    b.block[a] = B[a];
    b.cell[a] = std::clamp(std::max(C[a], B[a]), std::max(1, B[a]), 256);
    b.block[a] = std::min(b.block[a], b.cell[a]);
    b.origin[a] = (b.cell[a] - b.block[a]) / 2; // the single rounding
  }
  return b;
}

// The block's pixel range inside face f, along the face's u (coord 0) or v
// (coord 1): [first, first + len).
static void blockRangeInFace(const BlockLayout& b, int face, int coord, int& first, int& len)
{
  GhostAxis ax[2];
  ghostAxesOfFace(face, ax);
  for (const GhostAxis& a : ax) {
    if (a.coord != coord)
      continue;
    len = b.block[a.axis];
    first = a.flip ? b.cell[a.axis] - b.origin[a.axis] - len : b.origin[a.axis];
    return;
  }
  first = 0;
  len = 0;
}

// Resizes the six cells to the layout and replaces every view's content with
// the block (Base and Texture sheets of frame `fr`, filled on Base with
// `color`; Depth reset to flat grey). Two undo steps: the sheet resize, then
// the fill. Returns false (and sets `err`) when there is no sheet to work on.
static bool applyBlockLayout(Doc* doc,
                             doc::frame_t fr,
                             const BlockLayout& b,
                             doc::color_t color,
                             std::string& err)
{
  doc::Sprite* spr = doc ? doc->sprite() : nullptr;
  if (!spr) {
    err = "no sprite";
    return false;
  }
  if (!findSharedLayers(spr).baseLayer && !findSharedLayers(spr).textureLayer)
    setupSixSurfacesOnDoc(doc, false);
  SharedSurfaceLayers shared = findSharedLayers(spr);
  if (!shared.baseLayer) {
    err = "no Surface_Base layer";
    return false;
  }

  // Cells: Top/Down X x Z, Front/Back X x Y, Right/Left Z x Y.
  int tw[kFaceCount], th[kFaceCount];
  for (int f = 0; f < kFaceCount; ++f) {
    GhostAxis ax[2];
    ghostAxesOfFace(f, ax);
    tw[f] = b.cell[ax[0].axis]; // ax[0] is always the u axis
    th[f] = b.cell[ax[1].axis];
  }
  int ow[kFaceCount], oh[kFaceCount];
  sharedFaceSizes(spr, ow, oh);
  bool resize = false;
  for (int f = 0; f < kFaceCount; ++f)
    if (ow[f] != tw[f] || oh[f] != th[f])
      resize = true;
  const gfx::Size exact = atlasSize(spr);
  if (spr->width() != exact.w || spr->height() != exact.h)
    resize = true;
  if (resize)
    rebuildSharedSheets(doc, tw, th);

  // Fill. Every cel is grown to the whole sprite so each face rect is covered.
  try {
    Tx tx(doc, "Start From Block", ModifyDocument);
    DocApi api = doc->getApi(tx);
    // depthFlat: the face rects become flat grey 128 (no displacement), so an
    // old depth map cannot carve or extrude the new block (2026-11-17).
    auto paint = [&](doc::LayerImage* lyr, bool withBlock, bool depthFlat = false) {
      if (!lyr)
        return;
      doc::Cel* cel = lyr->cel(fr);
      doc::ImageRef img(doc::Image::create(doc::IMAGE_RGB, spr->width(), spr->height()));
      doc::clear_image(img.get(), img->maskColor());
      if (cel && cel->image() && cel->image()->pixelFormat() == doc::IMAGE_RGB)
        doc::copy_image(img.get(), cel->image(), cel->x(), cel->y());
      for (int f = 0; f < kFaceCount; ++f) {
        const gfx::Rect rc = sharedFaceRect(spr, f);
        const doc::color_t clearTo = depthFlat ? doc::rgba(128, 128, 128, 255) : img->maskColor();
        for (int v = 0; v < rc.h; ++v)
          for (int u = 0; u < rc.w; ++u)
            doc::put_pixel(img.get(), rc.x + u, rc.y + v, clearTo);
        if (!withBlock)
          continue;
        int u0 = 0, ul = 0, v0 = 0, vl = 0;
        blockRangeInFace(b, f, 0, u0, ul);
        blockRangeInFace(b, f, 1, v0, vl);
        for (int v = v0; v < v0 + vl; ++v)
          for (int u = u0; u < u0 + ul; ++u)
            doc::put_pixel(img.get(), rc.x + u, rc.y + v, color);
      }
      if (cel) {
        api.replaceImage(spr, cel->imageRef(), img);
        if (cel->x() != 0 || cel->y() != 0)
          api.setCelPosition(spr, cel, 0, 0);
      }
      else {
        api.addCel(lyr, fr, img);
      }
    };
    paint(shared.baseLayer, true);
    paint(shared.textureLayer, false);
    paint(shared.depthLayer, false, true);
    tx.commit();
    doc->notifyGeneralUpdate();
  }
  catch (const std::exception& e) {
    err = e.what();
    return false;
  }
  return true;
}

// ---------------------------------------------------------------------------
// Object operations (2026-11-17). All undoable, each ONE undo step.
// ---------------------------------------------------------------------------

// Face sizes of a W x H x D box: Top/Down X x Z, Front/Back X x Y,
// Right/Left Z x Y (ghostAxesOfFace: ax[0] is always the u axis).
static void boxFaceSizes(const int box[3], int w[kFaceCount], int h[kFaceCount])
{
  for (int f = 0; f < kFaceCount; ++f) {
    GhostAxis ax[2];
    ghostAxesOfFace(f, ax);
    w[f] = std::max(1, box[ax[0].axis]);
    h[f] = std::max(1, box[ax[1].axis]);
  }
}

// Resizes the model, i.e. every object's six cells, to box (X, Y, Z).
// Keep: drawings unscaled, centred; Stretch: scaled to the new size. One
// undo step; nothing happens when the size does not change.
static void modelBox(doc::Sprite* spr, int box[3]);
static bool resizeModelOnDoc(Doc* doc, const int box[3], ResizeMode mode)
{
  doc::Sprite* spr = doc ? doc->sprite() : nullptr;
  if (!spr)
    return false;
  int cur[3];
  modelBox(spr, cur);
  if (cur[0] == box[0] && cur[1] == box[1] && cur[2] == box[2] && !needsUnify(spr) &&
      atlasSize(spr) == gfx::Size(spr->width(), spr->height()))
    return false;
  int w[kFaceCount], h[kFaceCount];
  boxFaceSizes(box, w, h);
  EasymetricObjectScope root(spr, std::string());
  try {
    Tx tx(doc, "Resize Easymetric Model", ModifyDocument); // migration + resize = one step
    rebuildSharedSheets(doc, w, h, mode);
    tx.commit();
  }
  catch (const std::exception& e) {
    easymetricReportError("modelSize", e.what());
    return false;
  }
  return true;
}

// The model size (X, Y, Z) every object shares: Object 1's box.
static void modelBox(doc::Sprite* spr, int box[3])
{
  EasymetricObjectScope root(spr, std::string());
  int w[kFaceCount], h[kFaceCount];
  sharedFaceSizes(spr, w, h);
  box[0] = w[kFaceFront];
  box[1] = h[kFaceFront];
  box[2] = w[kFaceRight];
}

// Index-space origin (X right, Y DOWN, Z front) of a block of size `block`
// put next to what object `refKey` has painted on frame `fr` ("above",
// "below", "right", "left", "front", "back", "center"), centred on the other
// axes, then clamped into the model box. Nothing painted: the whole box is
// the reference. Returns false when the block had to be pushed or cut to fit.
static bool placeBlock(doc::Sprite* spr,
                       const std::string& refKey,
                       doc::frame_t fr,
                       int block[3],
                       const std::string& where,
                       int org[3])
{
  int S[3];
  modelBox(spr, S);
  int lo[3] = { 0, 0, 0 }, hi[3] = { S[0], S[1], S[2] };
  {
    EasymetricObjectScope sc(spr, refKey);
    paintedIndexBox(spr, fr, lo, hi);
  }
  for (int a = 0; a < 3; ++a)
    org[a] = lo[a] + floorHalf(hi[a] - lo[a] - block[a]);
  if (where == "above" || where == "top")
    org[1] = lo[1] - block[1];
  else if (where == "below" || where == "bottom")
    org[1] = hi[1];
  else if (where == "right")
    org[0] = hi[0];
  else if (where == "left")
    org[0] = lo[0] - block[0];
  else if (where == "front")
    org[2] = hi[2];
  else if (where == "back")
    org[2] = lo[2] - block[2];
  bool fits = true;
  for (int a = 0; a < 3; ++a) {
    if (block[a] > S[a]) {
      block[a] = S[a];
      fits = false;
    }
    const int c = std::clamp(org[a], 0, S[a] - block[a]);
    if (c != org[a])
      fits = false;
    org[a] = c;
  }
  return fits;
}

static std::string uniqueObjectKey(doc::Sprite* spr, const std::string& name)
{
  std::string base;
  for (char c : name)
    if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-')
      base.push_back(c);
  if (base.empty())
    base = "Object";
  std::string key = base;
  for (int n = 2; findObjectGroup(spr, key) || key == "root"; ++n)
    key = base + std::to_string(n);
  return key;
}

// Every object has the model size (2026-11-18); `block` is an optional
// starting block INSIDE that space, at index-space origin `blockOrg` (see
// placeBlock), painted on frame `frame` only.
struct NewObjectSpec {
  std::string name = "Object";
  bool fillBlock = false;
  int block[3] = { 8, 8, 8 };
  int blockOrg[3] = { 0, 0, 0 };
  doc::frame_t frame = 0;
  doc::color_t color = doc::rgba(34, 32, 52, 255);
};

// Adds an object group with its own Base/Texture/Depth sheets and six
// cells (the model size) in a new band under the existing ones; optionally
// paints a starting block (flat depth). Returns the new key ("" on failure).
static std::string newObjectOnDoc(Doc* doc, const NewObjectSpec& spec, std::string& err)
{
  doc::Sprite* spr = doc ? doc->sprite() : nullptr;
  if (!spr) {
    err = "no sprite";
    return std::string();
  }
  {
    EasymetricObjectScope root(spr, std::string());
    if (!findSharedLayers(spr).baseLayer)
      setupSixSurfacesOnDoc(doc, false);
    if (!findSharedLayers(spr).baseLayer) {
      err = "the document has no Surface_Base (Object 1) to add objects to";
      return std::string();
    }
  }
  const std::string key = uniqueObjectKey(spr, spec.name);
  ObjectMeta meta;
  meta.key = key;
  meta.order = 1;
  for (const ObjectGroupRef& g : objectGroups(spr))
    meta.order = std::max(meta.order, g.meta.order + 1);

  try {
    Tx tx(doc, "New Easymetric Object", ModifyDocument);
    DocApi api = doc->getApi(tx);
    unifyObjectsOnDoc(doc); // a pre-2026-11-18 file: one size first
    int S[3];
    modelBox(spr, S);
    int fw[kFaceCount], fh[kFaceCount];
    boxFaceSizes(S, fw, fh);
    doc::LayerGroup* grp = api.newGroup(spr->root(), kObjectGroupPrefix + spec.name);
    tx(new cmd::SetUserDataProperty(grp, std::string(), kObjectProp,
                                    doc::UserData::Variant(objectMetaString(meta))));
    // Same stack as the root: Depth under Base, Texture on top.
    for (const char* name : { "Surface_DEPTH", "Surface_Base", "Surface_Texture" }) {
      doc::LayerImage* lyr = api.newLayer(grp, name);
      for (doc::frame_t fr = 0; fr <= spr->lastFrame(); ++fr) {
        doc::ImageRef img(doc::Image::create(doc::IMAGE_RGB, spr->width(), spr->height()));
        img->clear(0);
        api.addCel(lyr, fr, img);
      }
    }
    // Placeholder cells under the current canvas; rebuildAtlas then lays
    // out every band and sizes the canvas.
    for (int f = 0; f < kFaceCount; ++f) {
      auto* sl = new doc::Slice;
      sl->setName(objectSliceName(key, f));
      sl->userData().setColor(doc::rgba(kFaceSpecs[f].r, kFaceSpecs[f].g, kFaceSpecs[f].b, 255));
      sl->insert(0, doc::SliceKey(gfx::Rect(0, spr->height(), fw[f], fh[f])));
      tx(new cmd::AddSlice(spr, sl));
    }
    rebuildAtlas(doc, fw, fh);
    if (spec.fillBlock) {
      EasymetricObjectScope sc(spr, key);
      BlockLayout b;
      for (int a = 0; a < 3; ++a) {
        b.cell[a] = S[a];
        b.block[a] = std::clamp(spec.block[a], 1, S[a]);
        b.origin[a] = std::clamp(spec.blockOrg[a], 0, S[a] - b.block[a]);
      }
      const doc::frame_t fr = std::clamp(spec.frame, doc::frame_t(0), spr->lastFrame());
      if (!applyBlockLayout(doc, fr, b, spec.color, err))
        throw std::runtime_error(err);
    }
    tx.commit();
    doc->notifyGeneralUpdate();
  }
  catch (const std::exception& e) {
    err = e.what();
    easymetricReportError("newObject", err);
    return std::string();
  }
  return key;
}

// Offset / rank / visibility / name of an object. For the root only rank
// and visibility exist (stored on the sprite). One undo step.
static bool setObjectOnDoc(Doc* doc,
                           const std::string& key,
                           const int* offset,
                           const int* rank,
                           const bool* visible,
                           const std::string* name)
{
  doc::Sprite* spr = doc ? doc->sprite() : nullptr;
  if (!spr)
    return false;
  try {
    Tx tx(doc, "Edit Easymetric Object", ModifyDocument);
    if (key.empty()) {
      int r = -1;
      bool hidden = false;
      readRootObjectProps(spr, r, hidden);
      if (rank)
        r = *rank;
      if (visible)
        hidden = !*visible;
      tx(new cmd::SetUserDataProperty(spr, std::string(), kRootObjectProp,
                                      doc::UserData::Variant(
                                        fmt::format("rank={};hidden={}", r, hidden ? 1 : 0))));
    }
    else {
      doc::LayerGroup* grp = findObjectGroup(spr, key);
      ObjectMeta meta;
      if (!grp || !readObjectMeta(grp, meta))
        return false;
      if (offset)
        for (int a = 0; a < 3; ++a)
          meta.o[a] = offset[a];
      if (rank)
        meta.rank = *rank;
      tx(new cmd::SetUserDataProperty(grp, std::string(), kObjectProp,
                                      doc::UserData::Variant(objectMetaString(meta))));
      if (name && !name->empty())
        tx(new cmd::SetLayerName(grp, kObjectGroupPrefix + *name));
      if (visible)
        grp->setVisible(*visible); // like the timeline eye: not an undo step
    }
    tx.commit();
    doc->notifyGeneralUpdate();
  }
  catch (const std::exception& e) {
    easymetricReportError("setObject", e.what());
    return false;
  }
  return true;
}

// Removes an object (group, sheets and cells) and closes the gap in the
// atlas. The root cannot be deleted. One undo step.
static bool deleteObjectOnDoc(Doc* doc, const std::string& key)
{
  doc::Sprite* spr = doc ? doc->sprite() : nullptr;
  doc::LayerGroup* grp = spr ? findObjectGroup(spr, key) : nullptr;
  if (!grp)
    return false;
  try {
    Tx tx(doc, "Delete Easymetric Object", ModifyDocument);
    DocApi api = doc->getApi(tx);
    unifyObjectsOnDoc(doc); // a pre-2026-11-18 file: one size first
    std::vector<doc::Slice*> slices;
    for (int f = 0; f < kFaceCount; ++f)
      for (auto* sl : spr->slices())
        if (sl && sl->name() == objectSliceName(key, f))
          slices.push_back(sl);
    for (doc::Slice* sl : slices)
      tx(new cmd::RemoveSlice(spr, sl));
    api.removeLayer(grp);
    int w[kFaceCount], h[kFaceCount];
    {
      EasymetricObjectScope root(spr, std::string());
      sharedFaceSizes(spr, w, h);
    }
    if (hasObjectGroups(spr))
      rebuildAtlas(doc, w, h);
    else {
      EasymetricObjectScope root(spr, std::string());
      rebuildSharedSheets(doc, w, h);
    }
    tx.commit();
    doc->notifyGeneralUpdate();
  }
  catch (const std::exception& e) {
    easymetricReportError("deleteObject", e.what());
    return false;
  }
  return true;
}


// One object's sculpted volume as OBJ quads (one per exposed voxel face),
// moved by (ox, oy, oz) in OBJ space (Y up). Texture coordinates point into
// the object's own cells of the flattened sprite (the exported atlas).
static void writeObjMesh(std::ostream& obj,
                         doc::Sprite* spr,
                         const SixSurfaceData& data,
                         const SculptedVoxelVolume& vol,
                         float ox,
                         float oy,
                         float oz,
                         int& vIdx,
                         int& vtIdx,
                         int& vnIdx)
{
  const float sprW = std::max(1.0f, float(spr->width()));
  const float sprH = std::max(1.0f, float(spr->height()));
  // Per-face rect in the exported texture atlas (the flattened
  // sprite). Previously every face was given UVs over the whole
  // 0..1 atlas, so each face showed the entire atlas at once and
  // transparent regions of other faces read as holes. Each face
  // must instead sample only its own rect.
  gfx::Rect faceRect[kFaceCount];
  for (int f = 0; f < kFaceCount; ++f)
    faceRect[f] = sharedFaceRect(spr, f);
  const float halfX = std::max({ data.w[kFaceFront], data.w[kFaceBack], data.w[kFaceTop], data.w[kFaceDown] }) * 0.5f;
  const float halfY = std::max({ data.h[kFaceFront], data.h[kFaceBack], data.h[kFaceLeft], data.h[kFaceRight] }) * 0.5f;
  const float halfZ = std::max({ data.w[kFaceLeft], data.w[kFaceRight], data.h[kFaceTop], data.h[kFaceDown] }) * 0.5f;

  // Depth sculpting (2026-11-07): when the volume grew past the Base
  // box, the Base box sits centred in it (pad voxels per side).
  // World and UV coords are measured from the Base box so it keeps
  // its size and Plus bumps stick out beyond it in the exported
  // mesh. Otherwise (pad 0) these are exactly the old formulas.
  const int dimX = std::min(kMaxVoxelDim, std::max({ data.w[kFaceFront], data.w[kFaceBack], data.w[kFaceTop], data.w[kFaceDown], 8 }));
  const int dimY = std::min(kMaxVoxelDim, std::max({ data.h[kFaceFront], data.h[kFaceBack], data.h[kFaceLeft], data.h[kFaceRight], 8 }));
  const int dimZ = std::min(kMaxVoxelDim, std::max({ data.w[kFaceLeft], data.w[kFaceRight], data.h[kFaceTop], data.h[kFaceDown], 8 }));
  const int padX = std::max(0, (vol.vx - dimX) / 2);
  const int padY = std::max(0, (vol.vy - dimY) / 2);
  const int padZ = std::max(0, (vol.vz - dimZ) / 2);
  const int spanX = (padX > 0 ? dimX : vol.vx);
  const int spanY = (padY > 0 ? dimY : vol.vy);
  const int spanZ = (padZ > 0 ? dimZ : vol.vz);
  const float cellSizeX = (2.0f * halfX) / float(spanX);
  const float cellSizeY = (2.0f * halfY) / float(spanY);
  const float cellSizeZ = (2.0f * halfZ) / float(spanZ);


  // uOld/vOld are the old 0..1 coordinates across the voxel
  // volume (orientation preserved exactly); they are remapped
  // into this face's rect in the atlas. vOld is bottom-up, so
  // (1 - vOld) is the distance down from the top of the face.
  auto emitQuad = [&](int face,
                      float x0, float y0, float z0,
                      float x1, float y1, float z1,
                      float x2, float y2, float z2,
                      float x3, float y3, float z3,
                      float nx, float ny, float nz,
                      float uOld0, float vOld0, float uOld1, float vOld1) {
    const gfx::Rect& fr = faceRect[face];
    const float u0 = (fr.x + uOld0 * float(data.w[face])) / sprW;
    const float u1 = (fr.x + uOld1 * float(data.w[face])) / sprW;
    const float v0 = 1.0f - (fr.y + (1.0f - vOld0) * float(data.h[face])) / sprH;
    const float v1 = 1.0f - (fr.y + (1.0f - vOld1) * float(data.h[face])) / sprH;
    obj << "v " << (x0 + ox) << " " << (y0 + oy) << " " << (z0 + oz) << "\n"
        << "v " << (x1 + ox) << " " << (y1 + oy) << " " << (z1 + oz) << "\n"
        << "v " << (x2 + ox) << " " << (y2 + oy) << " " << (z2 + oz) << "\n"
        << "v " << (x3 + ox) << " " << (y3 + oy) << " " << (z3 + oz) << "\n";
    obj << "vt " << u0 << " " << v0 << "\n"
        << "vt " << u1 << " " << v0 << "\n"
        << "vt " << u1 << " " << v1 << "\n"
        << "vt " << u0 << " " << v1 << "\n";
    obj << "vn " << nx << " " << ny << " " << nz << "\n";
    obj << "f "
        << (vIdx + 0) << "/" << (vtIdx + 0) << "/" << vnIdx << " "
        << (vIdx + 1) << "/" << (vtIdx + 1) << "/" << vnIdx << " "
        << (vIdx + 2) << "/" << (vtIdx + 2) << "/" << vnIdx << " "
        << (vIdx + 3) << "/" << (vtIdx + 3) << "/" << vnIdx << "\n";
    vIdx += 4;
    vtIdx += 4;
    vnIdx += 1;
  };

  for (int vy = 0; vy < vol.vy; ++vy) {
    const float yMax = halfY - float(vy - padY) * cellSizeY;
    const float yMin = halfY - float(vy + 1 - padY) * cellSizeY;

    for (int vz = 0; vz < vol.vz; ++vz) {
      const float zMin = -halfZ + float(vz - padZ) * cellSizeZ;
      const float zMax = -halfZ + float(vz + 1 - padZ) * cellSizeZ;

      for (int vx = 0; vx < vol.vx; ++vx) {
        if (!vol.isSolid(vx, vy, vz)) continue;

        const float xMin = -halfX + float(vx - padX) * cellSizeX;
        const float xMax = -halfX + float(vx + 1 - padX) * cellSizeX;

        // 1. Front face (+Z)
        if (vz + 1 >= vol.vz || !vol.isSolid(vx, vy, vz + 1)) {
          // UVs are clamped to [0,1]: a Plus bump sticks out past
          // the Base box, and its side faces would otherwise get
          // out-of-range UVs (texture repeat instead of the face
          // edge). Un-sculpted values are already in range, so
          // this is a no-op for them.
          const float u0 = std::clamp(float(vx - padX) / float(spanX), 0.0f, 1.0f);
          const float u1 = std::clamp(float(vx + 1 - padX) / float(spanX), 0.0f, 1.0f);
          const float v0 = std::clamp(1.0f - float(vy + 1 - padY) / float(spanY), 0.0f, 1.0f);
          const float v1 = std::clamp(1.0f - float(vy - padY) / float(spanY), 0.0f, 1.0f);
          emitQuad(kFaceFront, xMin, yMin, zMax,
                   xMax, yMin, zMax,
                   xMax, yMax, zMax,
                   xMin, yMax, zMax,
                   0.0f, 0.0f, 1.0f,
                   u0, v0, u1, v1);
        }

        // 2. Back face (-Z)
        if (vz - 1 < 0 || !vol.isSolid(vx, vy, vz - 1)) {
          const float u0 = std::clamp(float(spanX - 1 - (vx - padX)) / float(spanX), 0.0f, 1.0f);
          const float u1 = std::clamp(float(spanX - (vx - padX)) / float(spanX), 0.0f, 1.0f);
          const float v0 = std::clamp(1.0f - float(vy + 1 - padY) / float(spanY), 0.0f, 1.0f);
          const float v1 = std::clamp(1.0f - float(vy - padY) / float(spanY), 0.0f, 1.0f);
          emitQuad(kFaceBack, xMax, yMin, zMin,
                   xMin, yMin, zMin,
                   xMin, yMax, zMin,
                   xMax, yMax, zMin,
                   0.0f, 0.0f, -1.0f,
                   u0, v0, u1, v1);
        }

        // 3. Top face (+Y)
        if (vy - 1 < 0 || !vol.isSolid(vx, vy - 1, vz)) {
          const float u0 = std::clamp(float(vx - padX) / float(spanX), 0.0f, 1.0f);
          const float u1 = std::clamp(float(vx + 1 - padX) / float(spanX), 0.0f, 1.0f);
          const float v0 = std::clamp(1.0f - float(vz + 1 - padZ) / float(spanZ), 0.0f, 1.0f);
          const float v1 = std::clamp(1.0f - float(vz - padZ) / float(spanZ), 0.0f, 1.0f);
          emitQuad(kFaceTop, xMin, yMax, zMax,
                   xMax, yMax, zMax,
                   xMax, yMax, zMin,
                   xMin, yMax, zMin,
                   0.0f, 1.0f, 0.0f,
                   u0, v0, u1, v1);
        }

        // 4. Bottom face (-Y)
        if (vy + 1 >= vol.vy || !vol.isSolid(vx, vy + 1, vz)) {
          const float u0 = std::clamp(float(vx - padX) / float(spanX), 0.0f, 1.0f);
          const float u1 = std::clamp(float(vx + 1 - padX) / float(spanX), 0.0f, 1.0f);
          const float v0 = std::clamp(1.0f - float(spanZ - (vz - padZ)) / float(spanZ), 0.0f, 1.0f);
          const float v1 = std::clamp(1.0f - float(spanZ - 1 - (vz - padZ)) / float(spanZ), 0.0f, 1.0f);
          emitQuad(kFaceDown, xMin, yMin, zMin,
                   xMax, yMin, zMin,
                   xMax, yMin, zMax,
                   xMin, yMin, zMax,
                   0.0f, -1.0f, 0.0f,
                   u0, v0, u1, v1);
        }

        // 5. Right face (+X)
        if (vx + 1 >= vol.vx || !vol.isSolid(vx + 1, vy, vz)) {
          const float u0 = std::clamp(float(spanZ - 1 - (vz - padZ)) / float(spanZ), 0.0f, 1.0f);
          const float u1 = std::clamp(float(spanZ - (vz - padZ)) / float(spanZ), 0.0f, 1.0f);
          const float v0 = std::clamp(1.0f - float(vy + 1 - padY) / float(spanY), 0.0f, 1.0f);
          const float v1 = std::clamp(1.0f - float(vy - padY) / float(spanY), 0.0f, 1.0f);
          emitQuad(kFaceRight, xMax, yMin, zMax,
                   xMax, yMin, zMin,
                   xMax, yMax, zMin,
                   xMax, yMax, zMax,
                   1.0f, 0.0f, 0.0f,
                   u0, v0, u1, v1);
        }

        // 6. Left face (-X)
        if (vx - 1 < 0 || !vol.isSolid(vx - 1, vy, vz)) {
          const float u0 = std::clamp(float(vz - padZ) / float(spanZ), 0.0f, 1.0f);
          const float u1 = std::clamp(float(vz + 1 - padZ) / float(spanZ), 0.0f, 1.0f);
          const float v0 = std::clamp(1.0f - float(vy + 1 - padY) / float(spanY), 0.0f, 1.0f);
          const float v1 = std::clamp(1.0f - float(vy - padY) / float(spanY), 0.0f, 1.0f);
          emitQuad(kFaceLeft, xMin, yMin, zMin,
                   xMin, yMin, zMax,
                   xMin, yMax, zMax,
                   xMin, yMax, zMin,
                   -1.0f, 0.0f, 0.0f,
                   u0, v0, u1, v1);
        }
      }
    }
  }
}

// Wavefront OBJ + MTL (+ the flattened sprite as the texture when `ctx` can
// save files). With object groups every visible object with pixels is its
// own "o <name>", at its place in the model (Objects, 2026-11-18); a single
// object is written exactly as before ("o EasymetricSculptedModel").
static bool exportEasymetricObj(
  Context* ctx,
  Doc* doc,
  doc::frame_t fr,
  const std::string& objPathIn,
  int pivX,
  int pivY,
  int pivZ,
  bool useDepth,
  const std::function<const std::array<bool, 3>*(const std::string&)>& linkedFor)
{
  doc::Sprite* spr = doc ? doc->sprite() : nullptr;
  if (!spr)
    return false;
  const std::string objPath = base::replace_extension(objPathIn, "obj");
  const std::string mtlPath = base::replace_extension(objPath, "mtl");
  const std::string texPath = base::replace_extension(objPath, "png");
  const std::string mtlFileName = base::get_file_name(mtlPath);
  const std::string texFileName = base::get_file_name(texPath);

  if (ctx) {
    doc::FramesSequence seq;
    seq.insert(fr);
    std::unique_ptr<FileOp> fop(FileOp::createSaveDocumentOperation(
      ctx, FileOpROI(doc, spr->bounds(), "", "", seq, false), texPath, "", false));
    if (fop && !fop->hasError()) {
      fop->operate();
      fop->done();
    }
  }
  {
    std::ofstream mtl(mtlPath);
    if (mtl.is_open()) {
      mtl << "# Aseprite 2.0 Easymetric Material\n"
          << "newmtl EasymetricMat\n"
          << "Ka 1.000 1.000 1.000\n"
          << "Kd 1.000 1.000 1.000\n"
          << "Ks 0.000 0.000 0.000\n"
          << "d 1.0\n"
          << "illum 1\n"
          << "map_Kd " << texFileName << "\n";
    }
  }
  std::ofstream obj(objPath);
  if (!obj.is_open())
    return false;
  obj << "# Aseprite 2.0 Easymetric Sculpted 3D Model\n"
      << "mtllib " << mtlFileName << "\n"
      << "usemtl EasymetricMat\n";
  int vIdx = 1, vtIdx = 1, vnIdx = 1;
  if (!hasObjectGroups(spr)) {
    SixSurfaceData data;
    extractSixSurfaces(doc, fr, data, useDepth, nullptr,
                       linkedFor ? linkedFor(scopeKeyFor(spr)) : nullptr);
    const SculptedVoxelVolume vol = buildSculptedVolume(data, pivX, pivY, pivZ);
    obj << "o EasymetricSculptedModel\n";
    writeObjMesh(obj, spr, data, vol, 0.0f, 0.0f, 0.0f, vIdx, vtIdx, vnIdx);
    return true;
  }
  CompositeInput in;
  buildCompositeInput(doc, fr, useDepth, nullptr, std::string(), linkedFor, in);
  const std::vector<ObjectInfo> infos = resolveObjects(spr);
  for (size_t i = 0; i < in.objects.size(); ++i) {
    EasymetricObjectScope sc(spr, in.keys[i]);
    std::string name = "Object 1";
    for (const ObjectInfo& info : infos)
      if (info.key == in.keys[i])
        name = info.name;
    std::replace(name.begin(), name.end(), ' ', '_');
    const SixSurfaceData& data = *in.objects[i].data;
    const SculptedVoxelVolume vol = buildSculptedVolume(data, pivX, pivY, pivZ);
    obj << "o " << name << "\n";
    // The preview's world is Y down; OBJ is Y up.
    writeObjMesh(obj, spr, data, vol, in.objects[i].cx, -in.objects[i].cy, in.objects[i].cz, vIdx,
                 vtIdx, vnIdx);
  }
  return true;
}

// A whole-number field for sizes (2026-11-17). ui::Slider clamps on every
// keystroke (typing "3" in a 4..256 slider becomes 4 and the next digit is
// appended to it), so sizes are typed into an IntEntry instead, which only
// clamps when it loses focus. A plain click (no drag) right after the field
// gets focus selects the whole value, so typing replaces it instead of being
// appended to it (the "64" + "24" = "6424" seen in the recordings).
class NumberField : public ui::IntEntry {
public:
  NumberField(int min, int max, int value) : ui::IntEntry(min, max)
  {
    useSlider(false);
    setValue(value);
  }

  // Enter / numpad Enter while typing: the value is committed (clamped) and
  // this fires. The key is consumed, so it never reaches the Enter = Play
  // Animation shortcut (Arin pressed Enter to confirm a size, 2026-11-18,
  // and the animation started instead).
  obs::signal<void()> Enter;

protected:
  bool onProcessMessage(ui::Message* msg) override
  {
    switch (msg->type()) {
      case ui::kKeyDownMessage:
        if (hasFocus()) {
          const ui::KeyScancode sc = static_cast<ui::KeyMessage*>(msg)->scancode();
          if (sc == ui::kKeyEnter || sc == ui::kKeyEnterPad) {
            setValue(std::clamp(getValue(), m_min, m_max));
            selectAllText();
            Enter();
            return true;
          }
        }
        break;
      case ui::kFocusEnterMessage: m_selectOnRelease = true; break;
      case ui::kMouseDownMessage:
        m_downPos = static_cast<ui::MouseMessage*>(msg)->position();
        break;
      case ui::kMouseUpMessage: {
        const bool result = ui::IntEntry::onProcessMessage(msg);
        const gfx::Point d = static_cast<ui::MouseMessage*>(msg)->position() - m_downPos;
        const int slop = 6 * ui::guiscale();
        if (m_selectOnRelease && std::abs(d.x) <= slop && std::abs(d.y) <= slop)
          selectAllText();
        m_selectOnRelease = false;
        return result;
      }
      default: break;
    }
    return ui::IntEntry::onProcessMessage(msg);
  }

private:
  bool m_selectOnRelease = false;
  gfx::Point m_downPos;
};

class EasymetricMultiPaneView;

class EasymetricWheelZoomState : public StandbyState {
public:
  // Called when a button goes down in this panel, BEFORE the stroke starts:
  // the studio uses it to make this panel the active one (active editor, face
  // mask, per-panel symmetry) right now instead of on the next 33 ms poll,
  // which is why the first click in a not-yet-active panel used to draw
  // nothing (its mask still pointed at the previous face).
  using BeforePressFn = std::function<void(Editor*)>;
  void setBeforePress(BeforePressFn fn) { m_beforePress = std::move(fn); }

  bool onMouseDown(Editor* editor, ui::MouseMessage* msg) override
  {
    if (m_beforePress)
      m_beforePress(editor);
    return StandbyState::onMouseDown(editor, msg);
  }

  bool onMouseWheel(Editor* editor, ui::MouseMessage* msg) override
  {
    if (msg->altPressed())
      return StandbyState::onMouseWheel(editor, msg);
    gfx::Point delta = msg->wheelDelta();
    if (msg->shiftPressed() || delta.x != 0) {
      processWheelAction(editor, WheelAction::HScroll, msg->position(),
                         gfx::Point(delta.x, 0), double(delta.x),
                         ScrollBigSteps::Off,
                         (msg->preciseWheel() ? PreciseWheel::On : PreciseWheel::Off),
                         FromMouseWheel::On);
      return true;
    }
    // Vertical wheel is ALWAYS zoom in a surface panel, never
    // View scroll, regardless of the zoomWithWheel preference or
    // whether the wheel is precise/high-resolution.
    // Old precise handling divided dz by 1.5, clamped to [-1, 1]
    // and then truncated with int(dz): a small precise delta
    // (e.g. 0.66 after division) truncated to 0, so the wheel
    // did nothing, and a large one jumped several zoom steps.
    // Accumulate precise deltas instead, and use exactly one
    // step per event for a normal (non-precise) mouse notch,
    // whatever magnitude the OS reports for it.
    int step = 0;
    if (msg->preciseWheel()) {
      m_wheelAccum += double(delta.y) / 4.0;
      if (m_wheelAccum >= 1.0 || m_wheelAccum <= -1.0) {
        step = int(m_wheelAccum);
        m_wheelAccum -= double(step);
      }
      else {
        // Swallow the event: a partial precise step must not
        // fall through to View scroll, or the canvas displaces
        // instead of zooming once the accumulation completes.
        return true;
      }
    }
    else {
      step = (delta.y > 0) ? 1 : -1;
    }
    if (step == 0)
      return true;
    render::Zoom zoom = render::Zoom::fromLinearScale(
      editor->zoom().linearScale() - step);
    // Wheel-zoom revert guard (proven by Linux wheel harness,
    // 2026-11-04): setZoomAndCenterInMouse calls updateEditor,
    // which resizes the viewport, which calls Editor::onResize,
    // which calls fitClipSpriteBounds on a panel editor that
    // has a clip rect — and that fit floors the zoom back to
    // 1x and overwrites the scroll, so the panel displaced
    // instead of zooming. Clearing the clip for the duration
    // of this one call makes fitClip return early (empty
    // clip), so the zoom and the mouse-centred scroll survive;
    // the clip is restored immediately afterwards, in the
    // same call, so panel editing confinement is unchanged.
    // This touches only Easymetric panel editors, not the
    // upstream Editor::onResize/fitClip behaviour.
    // 2026-11-16: the clip-clearing workaround that used to be here is
    // gone. Editor::onResize no longer re-fits on resize events whose
    // viewport size is unchanged, and a manual zoom disables auto-fit.
    editor->setZoomAndCenterInMouse(zoom, msg->position(),
                                    Editor::ZoomBehavior::MOUSE);
    return true;
  }

  // Grid overlay decorator (G2): draws a face's grid lines over
  // its panel editor. It subclasses StandbyState::Decorator —
  // NOT a bare EditorDecorator — because MovingPixelsState
  // static_casts editor->decorator() to StandbyState::Decorator,
  // so any other base would be an invalid cast the first time a
  // Move runs in a panel. It is nested in this StandbyState
  // subclass because StandbyState::Decorator is protected: only
  // StandbyState and its subclasses may name it. (A free class
  // cannot derive from it — the phase-1 draft tried that and
  // did not compile.) The base Decorator draws nothing in
  // panels (PR #29: it early-returns for Easymetric panel
  // editors), so replacing it loses no handles; the grid is the
  // only thing this decorator draws. The editor's state
  // re-installs its own decorator on every state entry, so
  // pollAndRender re-installs this one whenever it is missing
  // (only while the editor is idle — never under a selection-box
  // or other temporary state).
  class EasymetricGridDecorator : public StandbyState::Decorator {
  public:
    EasymetricGridDecorator(StandbyState* state, EasymetricMultiPaneView* view, int face)
      : StandbyState::Decorator(state)
      , m_view(view)
      , m_face(face)
    {
    }

    void postRenderDecorator(EditorPostRender* render) override;
    void getInvalidDecoratoredRegion(Editor* editor, gfx::Region& region) override;

  private:
    EasymetricMultiPaneView* m_view;
    int m_face;
  };

protected:
  BeforePressFn m_beforePress;

private:
  double m_wheelAccum = 0.0;
};

// The three studio depth tools (toolbar group "easymetric_depth_tools").
enum class DepthTool { None, Brush, Pencil, Fill };

static DepthTool depthToolFromId(const std::string& id)
{
  if (id == "depth_brush") return DepthTool::Brush;
  if (id == "depth_pencil") return DepthTool::Pencil;
  if (id == "depth_fill") return DepthTool::Fill;
  return DepthTool::None;
}

// Depth-tool state for surface panels. Inherits the wheel-zoom
// behaviour, and intercepts mouse strokes so they edit the
// Surface_DEPTH layer instead of painting with the active tool.
// Left button = FG / raise, right button = BG / lower, exactly like
// the normal tools. The actual editing lives in EasymetricMultiPaneView
// (paintDepthDab / endDepthStroke, via the callbacks below), because
// only the view knows which panel editor is which face.
class EasymetricDepthState : public EasymetricWheelZoomState {
public:
  using PaintFn = std::function<void(Editor*, const gfx::Point&, bool down, bool right)>;
  using EndFn = std::function<void()>;
  EasymetricDepthState(PaintFn paint, EndFn end)
    : m_paint(std::move(paint))
    , m_end(std::move(end))
  {
  }
  bool onMouseDown(Editor* editor, ui::MouseMessage* msg) override
  {
    // Alt = eyedropper / customisation, Space = scroll, middle = scroll:
    // leave all of those to the normal editor behaviour.
    if (msg->altPressed() || (!msg->left() && !msg->right()))
      return false;
    if (m_beforePress)
      m_beforePress(editor);
    m_paint(editor, msg->position(), true, msg->right());
    // A stroke owns the mouse until the button is released (the normal
    // DrawingState does the same), so the drag keeps arriving even when it
    // leaves the panel.
    editor->captureMouse();
    return true;
  }
  bool onMouseMove(Editor* editor, ui::MouseMessage* msg) override
  {
    if (editor->hasCapture()) {
      m_paint(editor, msg->position(), false, msg->right() || msg->button() == ui::kButtonRight);
      return true;
    }
    return false;
  }
  bool onMouseUp(Editor* editor, ui::MouseMessage* msg) override
  {
    (void)msg;
    if (editor->hasCapture())
      editor->releaseMouse();
    m_end();
    return true;
  }

private:
  PaintFn m_paint;
  EndFn m_end;
};

// Standalone depth-map generation (William's spec, 2026-11-04).
// Used by the studio button AND by the headless script action
// "generateDepthFromBase", so a Lua batch test can verify the
// exact gray values without opening the GUI. See the method
// generateEditableFaceDepthLayer() for the spec comment.
static bool generateDepthFromBaseOnDoc(app::Doc* doc, doc::frame_t fr)
{
  if (!doc || !doc->sprite()) return false;
  doc::Sprite* spr = doc->sprite();
  SharedSurfaceLayers shared = findSharedLayers(spr);
  if (!shared.baseLayer || !shared.baseLayer->cel(fr)) return false;
  doc::ImageRef baseImg = shared.baseLayer->cel(fr)->imageRef();

  // Creating the depth layer makes it the active layer
  // (api.newLayer selects it), and the panel editors follow the
  // active sheet — so after the first depth dab every panel showed
  // the flat-grey depth map instead of the Base colours (GUI
  // harness run 3, 2026-11-07). Save the user's active layer and
  // restore it below: generating a depth map must not hijack the
  // sheet the panels show.
  Context* ctx = UIContext::instance() ? static_cast<Context*>(UIContext::instance()) :
                                         App::instance()->context();
  doc::Layer* prevActive = ctx ? ctx->activeSite().layer() : nullptr;

  try {
    Tx tx(doc, "Generate Depth from Base", ModifyDocument);
    DocApi api = doc->getApi(tx);

    doc::LayerImage* depthLayer = findOrCreateLayer(doc, api, spr, "Surface_DEPTH");
    depthLayer->setVisible(true);
    depthLayer->userData().setColor(doc::rgba(148, 163, 184, 255));
    // The depth sheet is DATA, not paint: it must sit BELOW the
    // Base sheet in the stack. A depth layer on top paints its flat
    // 128 grey over the Base colours in every panel editor (the
    // editors show the document composite, not just their own layer),
    // so after the first depth dab all six faces went flat grey (GUI
    // harness runs 3-4, 2026-11-07). Below Base it shows only where
    // Base is transparent — and the depth map is clipped by Base, so
    // it never covers a face colour. (Hiding the layer instead is
    // NOT an option: a hidden layer's pixels cannot be edited at
    // all — Lua clear() fails with the misleading "cannot modify
    // pixels on a smart object layer" — proven by probe 9, and it
    // broke sculpt test DS1 on build5. The layer stays visible and
    // editable; only its stack position changes.)
    if (shared.baseLayer)
      api.restackLayerBefore(depthLayer, shared.baseLayer->parent(), shared.baseLayer);

    doc::ImageRef depthImg(doc::Image::create(doc::IMAGE_RGB, spr->width(), spr->height()));
    depthImg->clear(0);

    gfx::Rect rects[kFaceCount];
    for (int f = 0; f < kFaceCount; ++f)
      rects[f] = sharedFaceRect(spr, f);

    auto lineCount = [&](int view, bool column, int index) {
      const gfx::Rect& r = rects[view];
      int count = 0;
      if (column) {
        if (index < 0 || index >= r.w) return 0;
        for (int v = 0; v < r.h; ++v)
          if (doc::rgba_geta(doc::get_pixel(baseImg.get(), r.x + index, r.y + v)) > 16)
            ++count;
      }
      else {
        if (index < 0 || index >= r.h) return 0;
        for (int u = 0; u < r.w; ++u)
          if (doc::rgba_geta(doc::get_pixel(baseImg.get(), r.x + u, r.y + index)) > 16)
            ++count;
      }
      return count;
    };

    for (int f = 0; f < kFaceCount; ++f) {
      const gfx::Rect& r = rects[f];
      if (r.w <= 0 || r.h <= 0) continue;
      std::vector<float> thick(size_t(r.w) * size_t(r.h), -1.0f);
      float sum = 0.0f;
      int n = 0;
      for (int v = 0; v < r.h; ++v) {
        for (int u = 0; u < r.w; ++u) {
          if (doc::rgba_geta(doc::get_pixel(baseImg.get(), r.x + u, r.y + v)) <= 16)
            continue;
          float t = 0.0f;
          switch (f) {
            case kFaceTop:
            case kFaceDown:
              t = 0.5f * (float(lineCount(kFaceFront, true, u)) / std::max(1, rects[kFaceFront].h) +
                          float(lineCount(kFaceRight, true, v)) / std::max(1, rects[kFaceRight].h));
              break;
            case kFaceFront:
              t = 0.5f * (float(lineCount(kFaceTop, true, u)) / std::max(1, rects[kFaceTop].h) +
                          float(lineCount(kFaceRight, false, v)) / std::max(1, rects[kFaceRight].w));
              break;
            case kFaceBack:
              t = 0.5f * (float(lineCount(kFaceTop, true, u)) / std::max(1, rects[kFaceTop].h) +
                          float(lineCount(kFaceLeft, false, v)) / std::max(1, rects[kFaceLeft].w));
              break;
            case kFaceRight:
              t = 0.5f * (float(lineCount(kFaceFront, false, v)) / std::max(1, rects[kFaceFront].w) +
                          float(lineCount(kFaceTop, true, u)) / std::max(1, rects[kFaceTop].w));
              break;
            case kFaceLeft:
              t = 0.5f * (float(lineCount(kFaceFront, false, v)) / std::max(1, rects[kFaceFront].w) +
                          float(lineCount(kFaceTop, true, u)) / std::max(1, rects[kFaceTop].w));
              break;
          }
          thick[size_t(v) * size_t(r.w) + size_t(u)] = t;
          sum += t;
          ++n;
        }
      }
      if (n == 0) continue;
      const float avg = sum / float(n);
      for (int v = 0; v < r.h; ++v) {
        for (int u = 0; u < r.w; ++u) {
          const float t = thick[size_t(v) * size_t(r.w) + size_t(u)];
          if (t < 0.0f) continue;
          const int gray = std::clamp(int(std::round(128.0f + (t - avg) * 128.0f)), 0, 255);
          doc::put_pixel(depthImg.get(), r.x + u, r.y + v, doc::rgba(gray, gray, gray, 255));
        }
      }
    }

    if (doc::Cel* cel = depthLayer->cel(fr))
      api.replaceImage(spr, cel->imageRef(), depthImg);
    else
      api.addCel(depthLayer, fr, depthImg);

    tx.commit();
    doc->notifyGeneralUpdate();
    // Restore the layer the user had active (see above): the new
    // depth layer must not stay selected.
    if (ctx && prevActive && prevActive != depthLayer)
      ctx->setActiveLayer(prevActive);
    return true;
  }
  catch (const std::exception& e) {
    easymetricReportError("generateDepthFromBaseOnDoc", e.what());
    return false;
  }
}

// Pure depth-sculpt dab on two working images. Shared by the
// GUI brush (which holds one Tx for a whole drag) and by the
// headless script action "depthDab" (which opens its own Tx),
// so both paths apply exactly the same step math.
// Returns {depthChanged, baseChanged}. See paintDepthDab for
// the spec (128 surface, step 128/D, black then through-hole).
// `touched` (optional, face-sized): when given, a pixel is changed at most
// once per stroke, so one stroke = one clean step (like painting with an
// Aseprite brush); the GUI stroke engine passes it, the headless depthDab
// action does not. `square` makes the dab a square instead of a disc.
static std::pair<bool, bool> applyDepthDabToImages(doc::Image* depthWork,
                                                   doc::Image* baseWork,
                                                   const gfx::Rect& faceRect,
                                                   int u0, int v0,
                                                   bool minus, float step, int radius,
                                                   std::vector<uint8_t>* touched = nullptr,
                                                   bool square = false)
{
  bool depthChanged = false, baseChanged = false;
  for (int dy = -radius; dy <= radius; ++dy) {
    for (int dx = -radius; dx <= radius; ++dx) {
      if (!square && dx * dx + dy * dy > radius * radius) continue;
      const int u = u0 + dx, v = v0 + dy;
      if (u < 0 || v < 0 || u >= faceRect.w || v >= faceRect.h) continue;
      if (touched) {
        uint8_t& tc = (*touched)[size_t(v) * size_t(faceRect.w) + size_t(u)];
        if (tc) continue;
        tc = 1;
      }
      const int cx = faceRect.x + u, cy = faceRect.y + v;
      if (doc::rgba_geta(doc::get_pixel(baseWork, cx, cy)) <= 16) continue;
      const uint32_t dCol = doc::get_pixel(depthWork, cx, cy);
      const float oldGray = (doc::rgba_geta(dCol) <= 16) ? 128.0f : float(doc::rgba_getr(dCol));
      if (!minus) {
        const int ng = std::min(255, int(std::round(oldGray + step)));
        doc::put_pixel(depthWork, cx, cy, doc::rgba(ng, ng, ng, 255));
        depthChanged = true;
      }
      else {
        if (oldGray <= 0.0f) {
          doc::put_pixel(depthWork, cx, cy, 0);
          doc::put_pixel(baseWork, cx, cy, 0);
          depthChanged = true;
          baseChanged = true;
        }
        else {
          const float nv = oldGray - step;
          const int ng = (nv <= 0.0f) ? 0 : int(std::round(nv));
          doc::put_pixel(depthWork, cx, cy, doc::rgba(ng, ng, ng, 255));
          depthChanged = true;
        }
      }
    }
  }
  return {depthChanged, baseChanged};
}

// Depth Pencil / Depth Fill target: an absolute depth value. `hole` = a
// through-hole (depth transparent AND the Base pixel cleared, the same
// semantics as Minus past black).
struct DepthValue {
  int gray = 128;
  bool hole = false;
};

// Foreground/background color -> depth value: brightness is the depth
// (white = out, 128 = flat, black = in); a (nearly) transparent color cuts
// a hole.
static DepthValue depthValueFromColor(const app::Color& c)
{
  DepthValue v;
  const doc::color_t px = color_utils::color_for_image(c, doc::IMAGE_RGB);
  v.hole = (doc::rgba_geta(px) <= 16);
  v.gray = std::clamp(int(std::lround(0.299 * doc::rgba_getr(px) + 0.587 * doc::rgba_getg(px) +
                                      0.114 * doc::rgba_getb(px))),
                      0, 255);
  return v;
}

// One pencil dab: sets every pixel of the brush (inside the face and inside
// the Base silhouette, like every depth edit) to `val`. Returns
// {depthChanged, baseChanged}.
static std::pair<bool, bool> applyDepthPaintToImages(doc::Image* depthWork,
                                                     doc::Image* baseWork,
                                                     const gfx::Rect& faceRect,
                                                     int u0, int v0,
                                                     const DepthValue& val, int radius,
                                                     bool square)
{
  bool depthChanged = false, baseChanged = false;
  for (int dy = -radius; dy <= radius; ++dy) {
    for (int dx = -radius; dx <= radius; ++dx) {
      if (!square && dx * dx + dy * dy > radius * radius) continue;
      const int u = u0 + dx, v = v0 + dy;
      if (u < 0 || v < 0 || u >= faceRect.w || v >= faceRect.h) continue;
      const int cx = faceRect.x + u, cy = faceRect.y + v;
      if (doc::rgba_geta(doc::get_pixel(baseWork, cx, cy)) <= 16) continue;
      const uint32_t old = doc::get_pixel(depthWork, cx, cy);
      if (val.hole) {
        if (old != 0) {
          doc::put_pixel(depthWork, cx, cy, 0);
          depthChanged = true;
        }
        doc::put_pixel(baseWork, cx, cy, 0);
        baseChanged = true;
      }
      else {
        const uint32_t nv = doc::rgba(val.gray, val.gray, val.gray, 255);
        if (old != nv) {
          doc::put_pixel(depthWork, cx, cy, nv);
          depthChanged = true;
        }
      }
    }
  }
  return {depthChanged, baseChanged};
}

// Depth Fill: 4-connected flood fill, inside the face and the Base
// silhouette, of the pixels whose depth equals the clicked pixel's depth
// (a transparent depth pixel counts as 128, like the sculpt). Returns
// {depthChanged, baseChanged}.
static std::pair<bool, bool> floodFillDepth(doc::Image* depthWork,
                                            doc::Image* baseWork,
                                            const gfx::Rect& faceRect,
                                            int u0, int v0,
                                            const DepthValue& val)
{
  bool depthChanged = false, baseChanged = false;
  if (u0 < 0 || v0 < 0 || u0 >= faceRect.w || v0 >= faceRect.h)
    return {false, false};
  auto inside = [&](int u, int v) {
    return u >= 0 && v >= 0 && u < faceRect.w && v < faceRect.h &&
           doc::rgba_geta(doc::get_pixel(baseWork, faceRect.x + u, faceRect.y + v)) > 16;
  };
  auto depthKey = [&](int u, int v) {
    const uint32_t d = doc::get_pixel(depthWork, faceRect.x + u, faceRect.y + v);
    return (doc::rgba_geta(d) <= 16) ? 128 : int(doc::rgba_getr(d));
  };
  if (!inside(u0, v0))
    return {false, false};
  const int target = depthKey(u0, v0);
  std::vector<uint8_t> seen(size_t(faceRect.w) * size_t(faceRect.h), 0);
  std::vector<gfx::Point> stack;
  stack.push_back(gfx::Point(u0, v0));
  seen[size_t(v0) * size_t(faceRect.w) + size_t(u0)] = 1;
  while (!stack.empty()) {
    const gfx::Point p = stack.back();
    stack.pop_back();
    const int cx = faceRect.x + p.x, cy = faceRect.y + p.y;
    if (val.hole) {
      doc::put_pixel(depthWork, cx, cy, 0);
      doc::put_pixel(baseWork, cx, cy, 0);
      depthChanged = baseChanged = true;
    }
    else {
      const uint32_t nv = doc::rgba(val.gray, val.gray, val.gray, 255);
      if (doc::get_pixel(depthWork, cx, cy) != nv) {
        doc::put_pixel(depthWork, cx, cy, nv);
        depthChanged = true;
      }
    }
    static const int dxs[4] = { 1, -1, 0, 0 };
    static const int dys[4] = { 0, 0, 1, -1 };
    for (int k = 0; k < 4; ++k) {
      const int nu = p.x + dxs[k], nv2 = p.y + dys[k];
      if (nu < 0 || nv2 < 0 || nu >= faceRect.w || nv2 >= faceRect.h)
        continue;
      uint8_t& s = seen[size_t(nv2) * size_t(faceRect.w) + size_t(nu)];
      if (s)
        continue;
      s = 1;
      // Each pixel is looked at once, before it is painted, so depthKey
      // always reads the ORIGINAL value.
      if (inside(nu, nv2) && depthKey(nu, nv2) == target)
        stack.push_back(gfx::Point(nu, nv2));
    }
  }
  return {depthChanged, baseChanged};
}

// Headless single dab, used by the script action "depthDab"
// and by Lua tests. Step is computed from the face normal size
// exactly as the GUI brush does.
static bool sculptDepthDabOnDoc(app::Doc* doc, doc::frame_t fr, int face,
                                int u, int v, bool minus, int radius)
{
  if (!doc || !doc->sprite() || face < 0 || face >= kFaceCount) return false;
  doc::Sprite* spr = doc->sprite();
  SharedSurfaceLayers shared = findSharedLayers(spr);
  if (!shared.baseLayer || !shared.baseLayer->cel(fr)) return false;
  if (!shared.depthLayer || !shared.depthLayer->cel(fr)) {
    if (!generateDepthFromBaseOnDoc(doc, fr)) return false;
    shared = findSharedLayers(spr);
    if (!shared.depthLayer || !shared.depthLayer->cel(fr)) return false;
  }
  int fw[kFaceCount], fh[kFaceCount];
  sharedFaceSizes(spr, fw, fh);
  int normalSize = 0;
  if (face == kFaceTop || face == kFaceDown)
    normalSize = fh[kFaceFront];
  else if (face == kFaceFront || face == kFaceBack)
    normalSize = fw[kFaceRight];
  else
    normalSize = fw[kFaceFront];
  const float step = 128.0f / float(std::max(1, normalSize / 2));
  const gfx::Rect faceRect = sharedFaceRect(spr, face);

  try {
    Tx tx(doc, "Depth Sculpt", ModifyDocument);
    DocApi api = doc->getApi(tx);
    doc::ImageRef depthWork(doc::Image::createCopy(shared.depthLayer->cel(fr)->imageRef().get()));
    doc::ImageRef baseWork(doc::Image::createCopy(shared.baseLayer->cel(fr)->imageRef().get()));
    const auto changed = applyDepthDabToImages(depthWork.get(), baseWork.get(),
                                               faceRect, u, v, minus, step, radius);
    if (changed.first)
      api.replaceImage(spr, shared.depthLayer->cel(fr)->imageRef(), depthWork);
    if (changed.second)
      api.replaceImage(spr, shared.baseLayer->cel(fr)->imageRef(), baseWork);
    tx.commit();
    doc->notifyGeneralUpdate();
    return changed.first || changed.second;
  }
  catch (const std::exception& e) {
    easymetricReportError("sculptDepthDabOnDoc", e.what());
    return false;
  }
}

void switchStudioSource(DocView* view, bool model);
class EasymetricMultiPaneView : public ui::Widget,
                                public EditorObserver,
                                public DocUndoObserver {
public:
  // Face index shown by this panel editor, or -1 (used by the session recorder).
  int faceOfEditor(const Editor* ed) const
  {
    for (int f = 0; f < kFaceCount; ++f)
      if (ed && m_editors[f] == ed)
        return f;
    return -1;
  }

  EasymetricMultiPaneView(DocView* docView)
    : ui::Widget(ui::kGenericWidget)
    , m_docView(docView)
    , m_timer(33, this)
  {
    // Wheel = zoom, not scroll, while the studio is open.
    // The preference-forcing below is kept as a fallback for
    // any editor that is not a surface panel, but the surface
    // panels themselves now use EasymetricWheelZoomState (set
    // below), which forces wheel = zoom regardless of the
    // preference or a customised shortcut table. Save the old
    // value and restore it when the studio closes (destructor
    // and the Exit button both restore, idempotently), so normal
    // editing outside Easymetric is untouched. The Preview
    // widget already zooms on wheel and returns the message as
    // handled, so it never scrolls the UI either; Shift+wheel
    // still pans it.
    m_savedZoomWithWheel = Preferences::instance().editor.zoomWithWheel();
    // Auto-align: watch committed transactions to catch the end of a stroke.
    if (Doc* undoDoc = docView ? docView->document() : nullptr) {
      m_undoObserved = undoDoc->undoHistory();
      m_undoObserved->add_observer(this);
    }
    if (!m_savedZoomWithWheel) {
      Preferences::instance().editor.zoomWithWheel(true);
      m_restoreZoomWithWheel = true;
    }

    setExpansive(true);

    // Remember the document selection so the per-face mask
    // confinement in pollAndRender can be undone on exit.
    if (Doc* doc0 = m_docView->document()) {
      if (doc0->mask() && !doc0->mask()->isEmpty())
        m_savedMask.reset(new doc::Mask(*doc0->mask()));
    }

    Doc* doc = m_docView->document();
    doc::Sprite* spr = doc ? doc->sprite() : nullptr;

    // 1. Create the 6 Surface Editors (Top, Bottom, Front, Back, Right, Left)
    for (int f = 0; f < kFaceCount; ++f) {
      m_headers[f] = new PaneHeader(kFaceSpecs[f].displayName,
                                    gfx::rgba(kFaceSpecs[f].r, kFaceSpecs[f].g, kFaceSpecs[f].b));

      // Panel action buttons, replacing the dead six-dot gripper.
      // Added in priority order (rightmost first): Grid, Link,
      // Sym X, Sym Y, Flip H, Flip V. The header hides
      // lower-priority ones when it is too narrow. Labels are
      // abbreviated (SX = Symmetry X, SY = Symmetry Y, FH = Flip
      // Horizontal, FV = Flip Vertical) so all five fit in a face
      // header alongside the title tab and, on top faces, the
      // dimension-link button; the Fields panel still shows the
      // full "Sym X"/"Sym Y" names. "Grid" is not abbreviated:
      // it is the panel's own feature button (per-panel grid, G2)
      // and it sits rightmost so it survives narrowing longest.
      // It is a toggle-style button — blue while this panel has
      // a grid — whose click opens the grid settings pop-up for
      // this panel only. It does not toggle the grid itself; the
      // pop-up's on/off checkbox does.
      m_gridButtons[f] = new PaneActionButton("Grid", true);
      m_gridButtons[f]->setIcon(PaneActionButton::Icon::Grid, "easymetric_grid");
      m_gridButtons[f]->setTooltipText("Grid: overlay a grid on this face (opens settings)");
      m_gridButtons[f]->Click.connect([this, f] { openGridPopup(f); });
      m_headers[f]->addActionButton(m_gridButtons[f]);

      m_animLinkButtons[f] = new PaneActionButton("Link", true);
      m_animLinkButtons[f]->setIcon(PaneActionButton::Icon::Link, "easymetric_link");
      m_animLinkButtons[f]->setTooltipText("Link: this panel follows the animation frame (off = stays on its frame)");
      m_animLinkButtons[f]->setOn(true);
      m_animLinkButtons[f]->Click.connect([this, f] {
        setAnimLinked(f, !m_animLinked[f]);
      });
      m_headers[f]->addActionButton(m_animLinkButtons[f]);

      m_symXButtons[f] = new PaneActionButton("SX", true);
      m_symXButtons[f]->setIcon(PaneActionButton::Icon::SymX, "easymetric_sym_x");
      m_symXButtons[f]->setTooltipText("Symmetry X: mirror strokes left/right around this face's centre");
      m_symXButtons[f]->Click.connect([this, f] { toggleSymmetryAxis(f, true); });
      m_headers[f]->addActionButton(m_symXButtons[f]);

      m_symYButtons[f] = new PaneActionButton("SY", true);
      m_symYButtons[f]->setIcon(PaneActionButton::Icon::SymY, "easymetric_sym_y");
      m_symYButtons[f]->setTooltipText("Symmetry Y: mirror strokes top/bottom around this face's centre");
      m_symYButtons[f]->Click.connect([this, f] { toggleSymmetryAxis(f, false); });
      m_headers[f]->addActionButton(m_symYButtons[f]);

      m_flipHButtons[f] = new PaneActionButton("FH", false);
      m_flipHButtons[f]->setIcon(PaneActionButton::Icon::FlipH, "easymetric_flip_h");
      m_flipHButtons[f]->setTooltipText("Flip this face horizontally (mirror left/right)");
      m_flipHButtons[f]->Click.connect([this, f] { flipFace(f, true); });
      m_headers[f]->addActionButton(m_flipHButtons[f]);

      m_flipVButtons[f] = new PaneActionButton("FV", false);
      m_flipVButtons[f]->setIcon(PaneActionButton::Icon::FlipV, "easymetric_flip_v");
      m_flipVButtons[f]->setTooltipText("Flip this face vertically (mirror top/bottom)");
      m_flipVButtons[f]->Click.connect([this, f] { flipFace(f, false); });
      m_headers[f]->addActionButton(m_flipVButtons[f]);

      Editor* ed = nullptr;
      ui::View* view = m_docView->createPaneEditorView(&ed);
      // Scrollbars on: each surface panel must be scrollable
      // horizontally and vertically on its own, instead of only
      // fitting the whole sprite into the pane. Hiding the bars
      // hid that affordance without disabling it, so show them.
      // The wheel is reserved for zooming (see above), so
      // scrolling needs the bars and drag, not the wheel.
      // fitting the whole sprite into the pane. The editor itself
      // already zooms/scrolls on wheel input; hiding the bars hid
      // that affordance without disabling it, so show them.
      view->showScrollBars();
      view->setExpansive(true);

      m_editors[f] = ed;
      m_views[f] = view;
      // Wheel = zoom in this panel, never View scroll (see
      // EasymetricWheelZoomState). Set after createPaneEditorView
      // so the custom state is the base state the temporary
      // drawing/moving states return to. The state is also kept
      // alive here as the StandbyState* the grid decorator's
      // base class needs (G2).
      {
        auto* base = new EasymetricWheelZoomState;
        base->setBeforePress([this](Editor* e) { activatePanelForPress(e); });
        m_panelBaseStates[f] = EditorStatePtr(base);
      }
      ed->setState(m_panelBaseStates[f]);
      // Grid overlay decorator (G2): installed on the panel
      // editor; pollAndRender re-installs it whenever the
      // editor's state re-installs its own decorator.
      m_gridDecorators[f] = new EasymetricWheelZoomState::EasymetricGridDecorator(
        static_cast<StandbyState*>(m_panelBaseStates[f].get()), this, f);

      if (spr) {
        SharedSurfaceLayers shared = findSharedLayers(spr);
        if (shared.baseLayer || shared.textureLayer) {
          // One sheet, one UV window per panel.
          ed->setClipSpriteBounds(sharedFaceRect(spr, f));
        }
        else if (doc::Slice* sl = findSliceForFace(spr, f)) {
          if (sl->getByFrame(0) && !sl->getByFrame(0)->bounds().isEmpty()) {
            ed->setClipSpriteBounds(sl->getByFrame(0)->bounds());
          }
          else {
            ed->setClipSpriteBounds(gfx::Rect(0, 0, spr->width(), spr->height()));
          }
        }
        else {
          ed->setClipSpriteBounds(gfx::Rect(0, 0, spr->width(), spr->height()));
        }
        // Bug 3 fix: a panel must NOT force the texture layer.
        // Before this, entering the studio always switched the
        // active layer to Surface_Texture, even if the user had
        // Surface_Base (or Surface_DEPTH) selected in the Layers
        // panel, and every later initLayersAndFraming() did it
        // again. Now the panel follows the layer the user had
        // selected in the main editor before entering the
        // studio, if it is one of the three sheets, and only
        // falls back to texture (then base) otherwise.
        doc::Layer* preStudioLayer = m_docView->editor() ? m_docView->editor()->layer() : nullptr;
        doc::LayerImage* lyr = nullptr;
        if (preStudioLayer == shared.baseLayer || preStudioLayer == shared.textureLayer ||
            preStudioLayer == shared.depthLayer)
          lyr = static_cast<doc::LayerImage*>(preStudioLayer);
        if (!lyr)
          lyr = shared.textureLayer ? shared.textureLayer : shared.baseLayer;
        if (!lyr) {
          SurfaceLayerPair sp = findSurfaceLayers(spr, f);
          lyr = sp.texLayer ? sp.texLayer : sp.baseLayer;
          if (!lyr) {
            const std::string lyrName = std::string("Surface_") + kFaceSpecs[f].sliceName;
            lyr = findLayerByName(spr, lyrName);
          }
        }
        if (lyr)
          ed->setLayer(lyr);
      }
      ed->setEditorFlags(Editor::EditorFlags((ed->editorFlags() | Editor::kShowOutside) & ~Editor::kShowSlices));

      m_panes[f] = new FacePaneBox(f, m_docView, ed);
      m_panes[f]->addChild(m_headers[f]);
      m_panes[f]->addChild(m_views[f]);
      addChild(m_panes[f]);
    }

    // Attach link toggle buttons to the top face header of each column (Col 0: Top, Col 1: Front, Col 2: Right)
    for (int c = 0; c < 3; ++c) {
      m_linkButtons[c] = new LinkToggleButton();
      m_linkButtons[c]->Click.connect([this, c] { toggleColumnLink(c); });
    }
    m_headers[0]->setLinkButton(m_linkButtons[0]);
    m_headers[2]->setLinkButton(m_linkButtons[1]);
    m_headers[4]->setLinkButton(m_linkButtons[2]);

    // Animation link: follow frame changes on every panel editor so
    // linked panels stay on the active editor's frame and unlinked
    // ones stay pinned. Unregistered in the destructor.
    for (int f = 0; f < kFaceCount; ++f) {
      if (m_editors[f]) {
        m_animPinnedFrame[f] = m_editors[f]->frame();
        m_editors[f]->add_observer(this);
      }
    }

    // 2. Column 4 Row 0: Preview Pane
    m_headers[kFaceCount] = new PaneHeader("Preview");
    m_previewWidget = new EasymetricPreviewWidget;
    m_previewWidget->setExpansive(true);

    // Pop-out button on the Preview header, opposite the label
    // (2026-11-07, William): pops out the WHOLE studio column —
    // Preview and Fields together — into a floating window, and
    // docks it back. The Fields "Pop out Column" button below
    // does the same thing.
    m_previewPopoutHeaderBtn = new PaneActionButton("Pop out", false);
    m_previewPopoutHeaderBtn->Click.connect([this] {
      if (m_previewFloating) dockPreview();
      else popOutPreview();
    });
    m_headers[kFaceCount]->addActionButton(m_previewPopoutHeaderBtn);
    m_ghostHeaderBtn = new PaneActionButton("Ghost", true);
    m_ghostHeaderBtn->setIcon(PaneActionButton::Icon::Ghost, "easymetric_ghost");
    m_ghostHeaderBtn->setTooltipText("Ghost hints: faint bands where another surface is missing "
                                     "content for a shape you drew (like onion skin)");
    m_ghostHeaderBtn->setOn(m_ghostOn);
    m_ghostHeaderBtn->Click.connect([this] { setGhostOn(!m_ghostOn); });
    m_headers[kFaceCount]->addActionButton(m_ghostHeaderBtn);

    m_panes[kFaceCount] = new ui::VBox;
    m_panes[kFaceCount]->addChild(m_headers[kFaceCount]);
    m_panes[kFaceCount]->addChild(m_previewWidget);
    m_previewPlaceholder = new ui::Label("Preview is in the floating window. Click Pop out in the Preview header to bring it back, or close the floating window.");
    m_previewPlaceholder->setVisible(false);
    m_panes[kFaceCount]->addChild(m_previewPlaceholder);
    addChild(m_panes[kFaceCount]);

    // 3. Column 4 Row 1: Fields Pane, scrollable. Previously the
    // Fields box was added directly to the pane, so when its
    // controls grew taller than the pane the bottom ones were
    // simply hidden with no way to reach them. Wrapping it in a
    // View with scrollbars makes every field reachable; the
    // mouse wheel scrolls it via the kMouseWheelMessage branch
    // in onProcessMessage (ui::View does not scroll on wheel
    // by itself — it needs an explicit scrollByMessage call).
    m_headers[kFaceCount + 1] = new PaneHeader("Fields");
    m_fieldsBox = new FieldsPanelBox;
    m_fieldsView = new ui::View;
    m_fieldsView->showScrollBars();
    m_fieldsView->setExpansive(true);
    m_fieldsView->attachToView(m_fieldsBox);

    buildFieldsControls();

    m_panes[kFaceCount + 1] = new ui::VBox;
    m_panes[kFaceCount + 1]->addChild(m_headers[kFaceCount + 1]);
    m_panes[kFaceCount + 1]->addChild(m_fieldsView);
    m_fieldsPlaceholder = new ui::Label("Fields are in the floating window. Click Pop out in the Preview header to bring them back, or close the floating window.");
    m_fieldsPlaceholder->setVisible(false);
    m_panes[kFaceCount + 1]->addChild(m_fieldsPlaceholder);
    addChild(m_panes[kFaceCount + 1]);

    // Studio tool strip across the top (Draw / Depth Sculpt).
    // Built after the panes so it can be laid out above them
    // in onResize; the pane layout lives below it.
    // The depth tools are in the left toolbar now: show their group.
    if (auto* tb = ToolBar::instance())
      tb->setStudioToolsVisible(true);
    hideDebugOverlays();

    // Timer for real-time 3D model update
    m_timer.Tick.connect([this] { pollAndRender(false); });
    m_timer.start();

    // Grid buttons start off (no grid is on at studio open);
    // syncGridButtonStates keeps the invariant in one place.
    syncGridButtonStates();

    m_lastHash = 0;
    // Session marker for crash reporting: if a marker from a
    // previous studio session is still present, that session
    // ended without a clean exit, so report it now (the crash
    // itself could not post). Then start this session's marker.
    {
      std::ifstream oldMarker(easymetricHubMarkerPath());
      if (oldMarker)
        easymetricReportCrash("previous Easymetric session ended unexpectedly");
      std::ofstream marker(easymetricHubMarkerPath());
      if (marker)
        marker << easymetricHubTimestamp() << "\n";
    }
    pollAndRender(true);
  }

  ~EasymetricMultiPaneView() override
  {
    m_timer.stop();
    // Animation link: unregister first, so a later frame change on a
    // surviving panel editor can never call back into this dead view.
    for (int f = 0; f < kFaceCount; ++f) {
      if (m_editors[f])
        m_editors[f]->remove_observer(this);
    }
    // A popped-out preview is a separate top-level window that
    // owns no studio members, but it does hold the preview
    // widget: dock it back first, then delete the window,
    // so closing the studio never leaves an orphan preview
    // window (or a widget owned by a dead window) behind.
    if (m_previewFloating && m_previewWindow)
      m_previewWindow->closeWindow(nullptr);
    if (m_previewWindow) {
      delete m_previewWindow;
      m_previewWindow = nullptr;
    }
    // Grid settings pop-up (G2): close it first — its Close
    // handler reverts an unconfirmed draft, suppressed here by
    // clearing the face first because the studio is going
    // away — then delete the window.
    m_gridPopupFace = -1;
    if (m_gridWindow) {
      m_gridWindow->closeWindow(nullptr);
      delete m_gridWindow;
      m_gridWindow = nullptr;
    }
    // Grid decorators (G2): detach them from the panel editors
    // before deleting them, so a surviving editor never paints
    // through a dead decorator. The editors themselves are
    // children of the view's panes and die with the view.
    for (int f = 0; f < kFaceCount; ++f) {
      if (m_editors[f] && m_gridDecorators[f] && m_editors[f]->decorator() == m_gridDecorators[f])
        m_editors[f]->setDecorator(nullptr);
      delete m_gridDecorators[f];
      m_gridDecorators[f] = nullptr;
    }
    restoreStateOnExit();
  }

  // Studio exit (Feature 4.3 follow-up, 2026-11-09). The studio
  // view is removed with DocView::exitMultiPaneMode(), which only
  // *defers* its deletion, and ui::Manager collects deferred
  // deletions only when no timer is running. This view's own
  // polling timer keeps running, so the destructor never ran: a
  // floating Preview & Fields window stayed on screen (and a
  // second one appeared on the next entry), the face-centre
  // symmetry axis leaked into the main editor, and the per-face
  // mask was never restored. The Studio command calls this
  // right before exitMultiPaneMode() so all of that happens at
  // exit time; the destructor then skips the restores.
  void prepareForExit()
  {
    if (m_exited)
      return;
    m_timer.stop();
    // Closing the floating window runs its Close handler, which
    // docks the column back (dockPreview), the same path as the
    // window's own X button.
    if (m_previewFloating && m_previewWindow)
      m_previewWindow->closeWindow(nullptr);
    restoreStateOnExit();
  }

  // Idempotent: restores what the studio changed outside its own
  // widgets (hub marker, wheel-zoom pref, document symmetry,
  // document mask). Runs once, from prepareForExit() or, when
  // the view dies another way (document closed), from the
  // destructor.
  void restoreStateOnExit()
  {
    if (m_exited)
      return;
    m_exited = true;
    // Objects: helpers outside the studio work on the root object again.
    g_objectScopeKey.clear();
    if (m_undoObserved) {
      m_undoObserved->remove_observer(this);
      m_undoObserved = nullptr;
    }
    restoreDebugOverlays();
    leaveDepthTools();
    std::remove(easymetricHubMarkerPath().c_str());
    restoreWheelZoomPref();
    if (Doc* doc = m_docView->document()) {
      // G3: give the document back its entry symmetry (the
      // per-panel face-centre axis must not leak into the main
      // editor). Runs before the mask restore; both are undone
      // on exit the same way they were saved on entry.
      restoreDocSymmetry();
      if (m_savedMask) {
        doc->setMask(m_savedMask.get());
        doc->setMaskVisible(true);
      }
      else {
        doc::Mask empty;
        doc->setMask(&empty);
        doc->setMaskVisible(false);
      }
      doc->notifySelectionChanged();
    }
  }

  void restoreWheelZoomPref()
  {
    if (m_restoreZoomWithWheel) {
      Preferences::instance().editor.zoomWithWheel(m_savedZoomWithWheel);
      m_restoreZoomWithWheel = false;
    }
  }

  // Studio part of the machine-readable state (see docStateObject).
  // Screen rects are in UI (logical) coordinates, the same space the
  // UI test server uses for injected mouse events.
  json11::Json::object studioStateObject() const
  {
    json11::Json::object s;
    s["open"] = true;
    // The studio reports its own focused object, whatever scope the
    // caller (the headless "state" action) set.
    doc::Sprite* scopeSpr =
      (m_docView && m_docView->document()) ? m_docView->document()->sprite() : nullptr;
    EasymetricObjectScope focusScope(scopeSpr, m_focusKey);
    s["focusedObject"] = m_focusKey;
    {
      json11::Json::array hits;
      for (size_t i = 0; i < m_lastObjHitKeys.size() && i < m_lastObjHitPx.size(); ++i)
        hits.push_back(json11::Json::object{ { "key", m_lastObjHitKeys[i] },
                                             { "px", m_lastObjHitPx[i] } });
      s["previewObjects"] = hits;
    }
    s["showOtherObjects"] = m_showOthers;
    s["colLinked"] = json11::Json::array{ m_colLinked[0], m_colLinked[1], m_colLinked[2] };
    s["otherObjectsPx"] = m_lastOverlayPx;
    s["toolMode"] = m_depthTool == DepthTool::None ? "draw" : "depth";
    s["depthTool"] = m_depthTool == DepthTool::Brush ? "brush"
                     : m_depthTool == DepthTool::Pencil ? "pencil"
                     : m_depthTool == DepthTool::Fill ? "fill" : "none";
    s["depthStrokeActive"] = m_depthStrokeActive;
    s["previewFloating"] = m_previewFloating;
    s["ghostOn"] = m_ghostOn;
    s["autoAlign"] = Preferences::instance().easymetric.autoAlign();
    s["autoAlignLimit"] = Preferences::instance().easymetric.autoAlignLimit();
    s["showCutouts"] = Preferences::instance().easymetric.showCutouts();
    s["lastAlign"] = m_lastAlign;
    s["previewCutoutPx"] = m_lastCutoutPx;
    if (m_previewWidget)
      s["previewRect"] = rectJson(m_previewWidget->bounds());
    if (m_fieldsView)
      s["fieldsViewport"] = rectJson(m_fieldsView->viewportBounds());
    s["activeFace"] = m_lastActiveFace;
    s["columnsPx"] = json11::Json::array{ m_colWpx[0], m_colWpx[1], m_colWpx[2] };
    s["rowsPx"] = json11::Json::array{ m_rowHpx[0], m_rowHpx[1], m_rowHpx[2], m_rowHpx[3] };
    s["bounds"] = rectJson(bounds());
    doc::Sprite* spr = (m_docView && m_docView->document()) ? m_docView->document()->sprite()
                                                              : nullptr;
    json11::Json::array faces;
    for (int f = 0; f < kFaceCount; ++f) {
      json11::Json::object fo;
      fo["name"] = kFaceSpecs[f].sliceName;
      fo["title"] = kFaceSpecs[f].displayName;
      if (spr)
        fo["uv"] = rectJson(sharedFaceRect(spr, f));
      if (m_editors[f]) {
        // "viewport" = the visible panel area; "faceScreen" = where this
        // face's pixels currently are, in UI coordinates (click inside it
        // to paint on the face). "editor" is the scrolled canvas widget.
        fo["editor"] = rectJson(m_editors[f]->bounds());
        if (m_views[f])
          fo["viewport"] = rectJson(m_views[f]->viewportBounds());
        if (spr)
          fo["faceScreen"] = rectJson(m_editors[f]->editorToScreen(sharedFaceRect(spr, f)));
        fo["zoom"] = m_editors[f]->zoom().scale();
        fo["layer"] = m_editors[f]->layer() ? m_editors[f]->layer()->name() : std::string();
        fo["idle"] = isPanelEditorIdle(m_editors[f]);
        fo["frame"] = int(m_editors[f]->frame());
        fo["playing"] = m_editors[f]->isPlaying();
        fo["active"] = (m_docView && m_docView->editor() == m_editors[f]);
      }
      if (m_headers[f])
        fo["header"] = rectJson(m_headers[f]->bounds());
      fo["symX"] = m_symXFace[f];
      fo["symY"] = m_symYFace[f];
      fo["animLinked"] = m_animLinked[f];
      const PanelGridSettings& g = m_gridDraftValid[f] ? m_gridDraft[f] : m_grid[f];
      fo["grid"] = json11::Json::object{ { "on", g.on },
                                         { "countH", g.countH },
                                         { "spacingH", g.spacingH },
                                         { "countV", g.countV },
                                         { "spacingV", g.spacingV } };
      // Header buttons: label + toggle state + bounds, so a test can
      // find and press them by name.
      json11::Json::array btns;
      auto addBtn = [&btns](const char* name, PaneActionButton* b) {
        if (!b)
          return;
        btns.push_back(json11::Json::object{ { "name", name },
                                             { "text", b->text() },
                                             { "on", b->isOn() },
                                             { "visible", b->isVisible() },
                                             { "bounds", rectJson(b->bounds()) } });
      };
      addBtn("grid", m_gridButtons[f]);
      addBtn("animLink", m_animLinkButtons[f]);
      addBtn("symX", m_symXButtons[f]);
      addBtn("symY", m_symYButtons[f]);
      addBtn("flipH", m_flipHButtons[f]);
      addBtn("flipV", m_flipVButtons[f]);
      fo["buttons"] = btns;
      faces.push_back(fo);
    }
    s["faces"] = faces;
    return s;
  }

  std::vector<Editor*> surfaceEditors() const
  {
    return std::vector<Editor*>(m_editors.begin(), m_editors.end());
  }

  void syncOpposingCels(int srcFace, int dstFace)
  {
    Doc* doc = m_docView->document();
    if (!doc || !doc->sprite()) return;
    doc::Sprite* spr = doc->sprite();

    SharedSurfaceLayers shared = findSharedLayers(spr);
    if (shared.baseLayer || shared.textureLayer) {
      // Shared sheets: opposing faces are different UV rects of
      // the SAME layer, so syncing means copying the source rect
      // into the destination rect, not replacing the whole sheet
      // (which would also overwrite the source itself).
      try {
        Tx tx(doc, "Sync Opposing Faces", ModifyDocument);
        DocApi api = doc->getApi(tx);
        const gfx::Rect srcRect = sharedFaceRect(spr, srcFace);
        const gfx::Rect dstRect = sharedFaceRect(spr, dstFace);
        // Mirrored copy (see extractSixSurfaces): Down = Top flipped
        // vertically, Back / Left = Front / Right flipped horizontally.
        const bool flipV = (srcFace / 2 == 0);
        auto syncRect = [&](doc::LayerImage* lyr) {
          if (!lyr) return;
          for (doc::frame_t fr = doc::frame_t(0); fr <= spr->lastFrame(); ++fr) {
            doc::Cel* cel = lyr->cel(fr);
            if (!cel || !cel->image()) continue;
            // A trimmed cel may not cover the destination face: grow the
            // image to the whole sprite so every mirrored pixel can land.
            int ox = cel->x(), oy = cel->y();
            doc::ImageRef copyImg;
            if (cel->bounds().contains(dstRect)) {
              copyImg.reset(doc::Image::createCopy(cel->image()));
            }
            else {
              copyImg.reset(doc::Image::create(cel->image()->pixelFormat(), spr->width(),
                                               spr->height()));
              doc::clear_image(copyImg.get(), copyImg->maskColor());
              doc::copy_image(copyImg.get(), cel->image(), ox, oy);
              ox = oy = 0;
            }
            const int cw = std::min(srcRect.w, dstRect.w);
            const int ch = std::min(srcRect.h, dstRect.h);
            for (int v = 0; v < ch; ++v)
              for (int u = 0; u < cw; ++u) {
                const int su = flipV ? u : cw - 1 - u;
                const int sv = flipV ? ch - 1 - v : v;
                const int sx = srcRect.x + su - ox;
                const int sy = srcRect.y + sv - oy;
                // Outside the (possibly trimmed) cel image = transparent.
                const doc::color_t c = (sx >= 0 && sy >= 0 && sx < copyImg->width() &&
                                        sy < copyImg->height()) ?
                                         doc::get_pixel(copyImg.get(), sx, sy) :
                                         copyImg->maskColor();
                doc::put_pixel(copyImg.get(), dstRect.x + u - ox, dstRect.y + v - oy, c);
              }
            api.replaceImage(spr, cel->imageRef(), copyImg);
            if (ox != cel->x() || oy != cel->y())
              api.setCelPosition(spr, cel, ox, oy);
          }
        };
        syncRect(shared.baseLayer);
        syncRect(shared.textureLayer);
        syncRect(shared.depthLayer);
        tx.commit();
        doc->notifyGeneralUpdate();
      }
      catch (const std::exception& e) {
        Console::showException(e);
        easymetricReportError("syncOpposingCels", e.what());
      }
      return;
    }

    SurfaceLayerPair srcPair = findSurfaceLayers(spr, srcFace);
    SurfaceLayerPair dstPair = findSurfaceLayers(spr, dstFace);

    const std::string srcName = std::string("Surface_") + kFaceSpecs[srcFace].sliceName;
    const std::string dstName = std::string("Surface_") + kFaceSpecs[dstFace].sliceName;
    doc::LayerImage* srcLyr = srcPair.baseLayer ? srcPair.baseLayer : findLayerByName(spr, srcName);
    doc::LayerImage* dstLyr = dstPair.baseLayer ? dstPair.baseLayer : findLayerByName(spr, dstName);
    if (!srcLyr || !dstLyr) return;

    try {
      Tx tx(doc, "Sync Opposing Faces", ModifyDocument);
      DocApi api = doc->getApi(tx);

      auto syncLayers = [&](doc::LayerImage* sL, doc::LayerImage* dL) {
        if (!sL || !dL) return;
        for (doc::frame_t fr = doc::frame_t(0); fr <= spr->lastFrame(); ++fr) {
          doc::Cel* srcCel = sL->cel(fr);
          doc::Cel* dstCel = dL->cel(fr);
          if (srcCel && srcCel->image()) {
            doc::ImageRef copyImg(doc::Image::createCopy(srcCel->image()));
            doc::algorithm::flip_image(copyImg.get(), copyImg->bounds(),
                                       (srcFace / 2 == 0) ? doc::algorithm::FlipVertical
                                                          : doc::algorithm::FlipHorizontal);
            if (dstCel) {
              api.replaceImage(spr, dstCel->imageRef(), copyImg);
              api.setCelPosition(spr, dstCel, srcCel->x(), srcCel->y());
            }
            else {
              api.addCel(dL, fr, copyImg);
            }
          }
        }
      };

      syncLayers(srcLyr, dstLyr);
      if (srcPair.texLayer && dstPair.texLayer)
        syncLayers(srcPair.texLayer, dstPair.texLayer);

      tx.commit();
      doc->notifyGeneralUpdate();
    }
    catch (const std::exception& e) {
      Console::showException(e);
      easymetricReportError("syncOpposingCels", e.what());
    }
  }

  void syncAllLinkedCels()
  {
    for (int c = 0; c < 3; ++c) {
      if (m_colLinked[c]) {
        syncOpposingCels(c * 2, c * 2 + 1);
      }
    }
  }

  void toggleColumnLink(int c)
  {
    if (c < 0 || c >= 3) return;
    const int topFace = c * 2;
    const int btmFace = c * 2 + 1;

    m_colLinked[c] = !m_colLinked[c];
    const bool linked = m_colLinked[c];

    if (m_linkButtons[c])
      m_linkButtons[c]->setLinked(linked);

    Doc* doc = m_docView->document();
    doc::Sprite* spr = doc ? doc->sprite() : nullptr;

    if (linked) {
      if (Editor::activeEditor() == m_editors[btmFace]) {
        Editor::_setActiveEditor(m_editors[topFace]);
      }
      if (m_docView->editor() == m_editors[btmFace]) {
        m_docView->setActivePaneEditor(m_editors[topFace]);
      }
      if (spr) {
        // Bug 3: linking a column must not flip the active
        // layer to texture. Keep the user's chosen sheet if
        // there is one, otherwise keep the panel's own layer.
        if (doc::LayerImage* chosen = activeSheetLayer(spr))
          m_editors[topFace]->setLayer(chosen);
        else if (!m_editors[topFace]->layer()) {
          SharedSurfaceLayers shared = findSharedLayers(spr);
          if (shared.textureLayer)
            m_editors[topFace]->setLayer(shared.textureLayer);
          else if (shared.baseLayer)
            m_editors[topFace]->setLayer(shared.baseLayer);
          else {
            const std::string topLyrName = std::string("Surface_") + kFaceSpecs[topFace].sliceName;
            if (doc::LayerImage* lyr = findLayerByName(spr, topLyrName))
              m_editors[topFace]->setLayer(lyr);
          }
        }
      }

      syncOpposingCels(topFace, btmFace);

      std::string unifiedTitle;
      switch (c) {
        case 0: unifiedTitle = "Top / Bottom"; break;
        case 1: unifiedTitle = "Front / Back"; break;
        case 2: unifiedTitle = "Right / Left"; break;
      }
      m_headers[topFace]->setTitle(unifiedTitle);
    }
    else {
      m_headers[topFace]->setTitle(kFaceSpecs[topFace].displayName);
      m_headers[btmFace]->setTitle(kFaceSpecs[btmFace].displayName);
      syncOpposingCels(topFace, btmFace);
    }

    layout();
    for (int f = 0; f < kFaceCount; ++f) {
      if (m_editors[f] && m_panes[f]->isVisible())
        m_editors[f]->fitClipSpriteBounds();
    }
    invalidate();
    m_lastHash = 0;
    pollAndRender(true);
  }

  // --- Panel action buttons ---

  // EditorObserver: keep linked panels on the active editor's frame
  // and unlinked ones pinned. setFrame is a no-op when the frame is
  // already equal, so the sync below cannot loop.
  void onAfterFrameChanged(Editor* editor) override
  {
    if (!editor || !m_docView)
      return;
    int face = -1;
    for (int f = 0; f < kFaceCount; ++f) {
      if (m_editors[f] == editor) { face = f; break; }
    }
    if (face < 0)
      return;
    const doc::frame_t newFrame = editor->frame();
    Editor* activeEd = m_docView->editor();
    // A panel that is PLAYING is also a reference (Arin, 2026-11-18: only
    // the Top panel animated). Playback keeps driving the panel it started
    // in even when another panel became the active one, and the linked
    // panels used to follow the active panel only.
    if (editor == activeEd || editor->isPlaying()) {
      // User intent (or playback on the active editor): this frame
      // is the new reference. An unlinked active panel adopts it as
      // its new pinned frame; every LINKED panel follows it.
      if (!m_animLinked[face])
        m_animPinnedFrame[face] = newFrame;
      for (int f = 0; f < kFaceCount; ++f) {
        if (f == face || !m_animLinked[f] || !m_editors[f])
          continue;
        if (m_editors[f]->frame() != newFrame)
          m_editors[f]->setFrame(newFrame);
      }
    }
    else {
      // Not the active editor. An unlinked panel that is not itself
      // the playback editor stays pinned: restore it. This also
      // catches DocView::setActivePaneEditor, which forces the
      // clicked panel to the old active frame BEFORE making it
      // active — restoring inside this handler is flicker-free.
      // A linked panel needs nothing (the active editor already
      // synced it). An unlinked panel that IS the playback editor
      // still advances during playback: PlayState drives the editor
      // captured at Play start, and pinning it would need an
      // upstream timeline/playback change (out of scope).
      if (!m_animLinked[face] && !editor->isPlaying() &&
          editor->frame() != m_animPinnedFrame[face]) {
        editor->setFrame(m_animPinnedFrame[face]);
      }
    }
  }

  void setAnimLinked(int face, bool linked)
  {
    if (face < 0 || face >= kFaceCount)
      return;
    m_animLinked[face] = linked;
    if (m_animLinkButtons[face])
      m_animLinkButtons[face]->setOn(linked);
    if (linked) {
      // Re-link: sync immediately to the active editor's frame.
      Editor* activeEd = m_docView ? m_docView->editor() : nullptr;
      if (activeEd && m_editors[face] &&
          m_editors[face]->frame() != activeEd->frame()) {
        m_editors[face]->setFrame(activeEd->frame());
      }
    }
    else {
      // Unlink: pin the panel at its current frame.
      if (m_editors[face])
        m_animPinnedFrame[face] = m_editors[face]->frame();
    }
  }

  // G3 per-panel symmetry (2026-11-07, William): each face keeps
  // its own SX/SY, and the studio follows the active panel. A
  // header button toggles ONLY its own panel's stored state (its
  // button shows the new state); the document only changes when
  // that panel is the active one — the active panel is what the
  // document follows. A toggle on a non-active panel is stored
  // and applied when that panel activates (at most one poll later
  // if the click lands before the poll notices the switch).
  void toggleSymmetryAxis(int face, bool isX)
  {
    if (face < 0 || face >= kFaceCount)
      return;
    initPanelSymmetry();
    if (isX)
      m_symXFace[face] = !m_symXFace[face];
    else
      m_symYFace[face] = !m_symYFace[face];
    if (m_symXButtons[face])
      m_symXButtons[face]->setOn(m_symXFace[face]);
    if (m_symYButtons[face])
      m_symYButtons[face]->setOn(m_symYFace[face]);
    if (face == m_lastActiveFace)
      applyPanelSymmetry(face);
  }

  // G3: first use per studio session — remember the document
  // symmetry to restore on exit, and seed every panel's stored
  // state from it, so the studio opens consistent with the
  // document instead of always showing symmetry off.
  // Debug overlays (2026-11-09, William: "I don't want the bounding boxes of
  // the surfaces / layers in geometric mode"): Aseprite draws every slice as
  // a labelled rectangle (the six TOP/DOWN/FRONT/... face slices that define
  // the UV rects) and, if enabled, the active layer's edges, in every panel
  // editor. They are internal data, not something to look at, so hide them
  // while the studio is open and give the document its own settings back on
  // exit (restoreStateOnExit).
  void hideDebugOverlays()
  {
    if (m_overlaysSaved)
      return;
    Doc* doc = m_docView ? m_docView->document() : nullptr;
    if (!doc)
      return;
    auto& docPref = Preferences::instance().document(doc);
    m_savedShowSlices = docPref.show.slices();
    m_savedShowLayerEdges = docPref.show.layerEdges();
    m_overlaysSaved = true;
    docPref.show.slices(false);
    docPref.show.layerEdges(false);
  }

  void restoreDebugOverlays()
  {
    if (!m_overlaysSaved)
      return;
    m_overlaysSaved = false;
    Doc* doc = m_docView ? m_docView->document() : nullptr;
    if (!doc)
      return;
    auto& docPref = Preferences::instance().document(doc);
    docPref.show.slices(m_savedShowSlices);
    docPref.show.layerEdges(m_savedShowLayerEdges);
  }

  void initPanelSymmetry()
  {
    if (m_symFacesInit)
      return;
    m_symFacesInit = true;
    Doc* doc = m_docView ? m_docView->document() : nullptr;
    if (!doc || !doc->sprite())
      return;
    auto& docPref = Preferences::instance().document(doc);
    const int mode = int(docPref.symmetry.mode());
    const bool symX = (mode & int(app::gen::SymmetryMode::HORIZONTAL)) != 0;
    const bool symY = (mode & int(app::gen::SymmetryMode::VERTICAL)) != 0;
    for (int f = 0; f < kFaceCount; ++f) {
      m_symXFace[f] = symX;
      m_symYFace[f] = symY;
    }
    m_savedSymMode = mode;
    m_savedSymEnabled = Preferences::instance().symmetryMode.enabled();
    m_savedSymXAxis = docPref.symmetry.xAxis();
    m_savedSymYAxis = docPref.symmetry.yAxis();
    m_symDocSaved = true;
  }

  // G3: apply one panel's stored SX/SY + its face-centre axis to
  // the document symmetry. The tool loop snapshots doc prefs at
  // stroke start (tool_loop_impl.cpp), so this is all a stroke
  // needs to mirror around this face's centre. The axis is
  // always set (even with mode NONE) so it is correct the moment
  // an axis is turned on. Fields checkboxes show this panel's
  // state; header buttons already show their own face's state.
  // Also records the face rect, so pollAndRender can re-apply
  // when a Fields edit moves the face centre.
  void applyPanelSymmetry(int face)
  {
    Doc* doc = m_docView ? m_docView->document() : nullptr;
    if (!doc || !doc->sprite() || face < 0 || face >= kFaceCount)
      return;
    initPanelSymmetry();
    auto& pref = Preferences::instance();
    auto& docPref = pref.document(doc);
    const bool symX = m_symXFace[face];
    const bool symY = m_symYFace[face];
    int mode = int(app::gen::SymmetryMode::NONE);
    if (symX)
      mode |= int(app::gen::SymmetryMode::HORIZONTAL);
    if (symY)
      mode |= int(app::gen::SymmetryMode::VERTICAL);
    if (mode != int(app::gen::SymmetryMode::NONE))
      pref.symmetryMode.enabled(true);
    docPref.symmetry.mode(static_cast<app::gen::SymmetryMode>(mode));
    const gfx::Rect faceRect = sharedFaceRect(doc->sprite(), face);
    docPref.symmetry.xAxis(faceRect.x + faceRect.w * 0.5);
    docPref.symmetry.yAxis(faceRect.y + faceRect.h * 0.5);
    m_lastSymFaceRect = faceRect;
    if (m_symXCheck)
      m_symXCheck->setSelected(symX);
    if (m_symYCheck)
      m_symYCheck->setSelected(symY);
    for (Editor* ed : m_editors) {
      if (ed)
        ed->invalidate();
    }
  }

  // G3: restore the document symmetry saved on entry (mode +
  // axes). Called when focus leaves all panels and in the
  // destructor, so the studio never leaks a face-centre axis into
  // the main editor. It does NOT invalidate the panel editors:
  // no panel is active when this runs, and the main editor
  // repaints on its own — which also keeps the destructor path
  // free of editor calls. Fields checkboxes show the restored
  // document state.
  void restoreDocSymmetry()
  {
    if (!m_symDocSaved)
      return;
    Doc* doc = m_docView ? m_docView->document() : nullptr;
    if (!doc || !doc->sprite())
      return;
    auto& docPref = Preferences::instance().document(doc);
    docPref.symmetry.mode(static_cast<app::gen::SymmetryMode>(m_savedSymMode));
    docPref.symmetry.xAxis(m_savedSymXAxis);
    docPref.symmetry.yAxis(m_savedSymYAxis);
    // applyPanelSymmetry() switches the global "symmetry options"
    // preference on; give it back too, or the main editor keeps
    // the symmetry buttons in its context bar after the studio.
    Preferences::instance().symmetryMode.enabled(m_savedSymEnabled);
    if (m_symXCheck)
      m_symXCheck->setSelected(
        (m_savedSymMode & int(app::gen::SymmetryMode::HORIZONTAL)) != 0);
    if (m_symYCheck)
      m_symYCheck->setSelected(
        (m_savedSymMode & int(app::gen::SymmetryMode::VERTICAL)) != 0);
  }

  // G3: seed every panel's stored state from the document, show
  // each header button its own panel's state, and show the Fields
  // checkboxes the active panel's state. Called once after the
  // Fields controls are built, so the studio opens consistent
  // with the document instead of always showing symmetry off.
  void syncSymmetryButtons()
  {
    initPanelSymmetry();
    const int face = (m_lastActiveFace >= 0) ? m_lastActiveFace : 0;
    if (m_symXCheck)
      m_symXCheck->setSelected(m_symXFace[face]);
    if (m_symYCheck)
      m_symYCheck->setSelected(m_symYFace[face]);
    for (int f = 0; f < kFaceCount; ++f) {
      if (m_symXButtons[f])
        m_symXButtons[f]->setOn(m_symXFace[f]);
      if (m_symYButtons[f])
        m_symYButtons[f]->setOn(m_symYFace[f]);
    }
  }

  // Mirror one face's pixels within its UV rect, on the CURRENT
  // frame, on every shared layer that has a cel on that frame
  // (Base, Texture, Depth), in one transaction = one undo. Slice
  // bounds are unchanged. The preview refreshes via the poll.
  void flipFace(int face, bool horizontal)
  {
    if (face < 0 || face >= kFaceCount)
      return;
    Doc* doc = m_docView ? m_docView->document() : nullptr;
    if (!doc || !doc->sprite())
      return;
    // The frame the PANEL shows (an unlinked panel can be pinned to a
    // different frame than the main editor), not the main editor's.
    const doc::frame_t fr = m_editors[face] ? m_editors[face]->frame()
                                            : (m_docView->editor() ? m_docView->editor()->frame()
                                                                   : doc::frame_t(0));
    // A stroke or Move in progress owns the document; never flip under it.
    if (anyPanelEditorBusy())
      return;
    if (!flipFaceOnDoc(doc, face, horizontal, fr))
      return;
    m_lastHash = 0;
    pollAndRender(true);
    if (auto* sb = StatusBar::instance())
      sb->showTip(1500,
                  fmt::format("Flipped {} {}", kFaceSpecs[face].displayName,
                              horizontal ? "horizontally" : "vertically"));
  }

  // Bug 1/2 helper: an editor is idle only when it is in its
  // standby state, not mid-stroke. The old code used
  // dynamic_cast<StandbyState*>, which is true for EVERY editor
  // state, because DrawingState, MovingPixelsState,
  // MovingCelState and SelectBoxState all inherit StandbyState,
  // so the old "only in standby" guard never guarded anything.
  // Idle here means exactly StandbyState, or the Easymetric
  // wheel-zoom idle state the panels are created with.
  bool isPanelEditorIdle(Editor* ed) const
  {
    if (!ed || !ed->getState()) return false;
    const std::type_info& t = typeid(*ed->getState());
    return t == typeid(StandbyState) || t == typeid(EasymetricWheelZoomState) ||
           t == typeid(EasymetricDepthState);
  }

  bool anyPanelEditorBusy() const
  {
    for (int f = 0; f < kFaceCount; ++f) {
      if (m_editors[f] && (m_editors[f]->hasCapture() || !isPanelEditorIdle(m_editors[f])))
        return true;
    }
    return false;
  }

  // Bug 3 helper: the sheet layer the user actually chose, or
  // nullptr if the active panel editor is not on a sheet. The
  // active panel editor is the one the Layers panel last set
  // (Timeline::setLayer sets that editor's layer), so it is the
  // user's choice, not a studio default.
  doc::LayerImage* activeSheetLayer(doc::Sprite* spr) const
  {
    if (!spr) return nullptr;
    SharedSurfaceLayers shared = findSharedLayers(spr);
    doc::Layer* active = m_docView->editor() ? m_docView->editor()->layer() : nullptr;
    if (active == shared.baseLayer || active == shared.textureLayer ||
        active == shared.depthLayer)
      return static_cast<doc::LayerImage*>(active);
    return nullptr;
  }

  EditorStatePtr newPanelState()
  {
    if (depthBrushOn()) {
      auto* st = new EasymetricDepthState(
        [this](Editor* ed, const gfx::Point& pt, bool down, bool right) {
          paintDepthDab(ed, pt, down, right);
        },
        [this] { endDepthStroke(); });
      st->setBeforePress([this](Editor* e) { activatePanelForPress(e); });
      return EditorStatePtr(st);
    }
    auto* st = new EasymetricWheelZoomState;
    st->setBeforePress([this](Editor* e) { activatePanelForPress(e); });
    return EditorStatePtr(st);
  }

  // Make the panel under a button press the active one NOW (see
  // EasymetricWheelZoomState::setBeforePress).
  void activatePanelForPress(Editor* ed)
  {
    if (!ed || m_docView->editor() == ed || m_depthStrokeActive)
      return;
    int face = -1;
    for (int f = 0; f < kFaceCount; ++f)
      if (m_editors[f] == ed)
        face = f;
    if (face < 0)
      return;
    m_docView->setActivePaneEditor(ed);
    if (UIContext* ctx = UIContext::instance())
      ctx->setActiveView(m_docView);
    // Applies this face's mask and symmetry (the same code the poll runs).
    pollAndRender(false);
  }

  bool depthBrushOn() const { return m_depthTool != DepthTool::None; }

  // The studio depth tools are real tools in the left toolbar (group
  // "easymetric_depth_tools", visible only while the studio is open).
  // The panels follow the ACTIVE TOOL: when it is one of the depth tools the
  // panel editors get the depth state, otherwise the normal draw state. Called
  // from every poll, so picking a tool in the toolbar, by shortcut, or from a
  // script all work the same way.
  void syncDepthToolFromApp()
  {
    tools::Tool* tool = App::instance() ? App::instance()->activeTool() : nullptr;
    setDepthTool(tool ? depthToolFromId(tool->getId()) : DepthTool::None);
  }

  void setDepthTool(DepthTool tool)
  {
    if (m_depthTool == tool)
      return;
    // Never switch under a stroke; the next poll retries.
    for (int f = 0; f < kFaceCount; ++f)
      if (m_editors[f] && m_editors[f]->hasCapture())
        return;
    endDepthStroke();
    m_depthTool = tool;
    for (int f = 0; f < kFaceCount; ++f) {
      Editor* ed = m_editors[f];
      if (ed && !ed->hasCapture())
        ed->setState(newPanelState());
    }
    if (auto* sb = StatusBar::instance()) {
      switch (tool) {
        case DepthTool::Brush:
          sb->showTip(3000, "Depth Brush: left click raises, right click lowers the surface. "
                            "Brush size = the normal brush size.");
          break;
        case DepthTool::Pencil:
          sb->showTip(3000, "Depth Pencil: paints the foreground (left) / background (right) "
                            "color's brightness as depth. Transparent = hole.");
          break;
        case DepthTool::Fill:
          sb->showTip(3000, "Depth Fill: fills the connected area of equal depth with the "
                            "foreground (left) / background (right) color's brightness.");
          break;
        case DepthTool::None: break;
      }
    }
  }

  // Normal tool restored (and the toolbar group hidden) when the studio closes.
  void leaveDepthTools()
  {
    endDepthStroke();
    m_depthTool = DepthTool::None;
    if (auto* tb = ToolBar::instance()) {
      tools::Tool* active = App::instance() ? App::instance()->activeTool() : nullptr;
      if (active && ToolBar::isStudioTool(active)) {
        if (tools::Tool* pencil = App::instance()->toolBox()->getToolById("pencil"))
          tb->selectTool(pencil);
      }
      tb->setStudioToolsVisible(false);
    }
  }

  // Current brush of the active depth tool: the NORMAL Aseprite brush size
  // and shape (context bar), so the depth brush feels like any brush.
  struct DepthBrushSpec {
    int radius = 1;
    bool square = false;
  };

  DepthBrushSpec currentDepthBrush() const
  {
    DepthBrushSpec s;
    if (tools::Tool* tool = App::instance() ? App::instance()->activeTool() : nullptr) {
      auto& tp = Preferences::instance().tool(tool);
      const int size = std::clamp(int(tp.brush.size()), 1, 64);
      s.radius = std::max(0, (size - 1) / 2);
      s.square = (tp.brush.type() == app::gen::BrushType::SQUARE);
    }
    return s;
  }

  // Depth tools (William's spec, 2026-11-04; toolbar tools 2026-11-09).
  //  * Depth Brush: left = PLUS (bump, whiter towards the user), right = MINUS
  //    (carve, darker into the plane). 128 = the main surface. 1 step = 1
  //    voxel of depth, i.e. gray step 128 / D, D = pixels from surface to box
  //    centre along the face normal (Front/Back use depth/2, Top/Bottom
  //    height/2, Right/Left width/2). One stroke changes each pixel by at
  //    most one step. Black 0 = deepest; one more MINUS step = through-hole
  //    (depth transparent AND the Base pixel cleared, so the hole is real).
  //  * Depth Pencil: sets the depth to the FG (left) / BG (right) color's
  //    brightness. Depth Fill: flood fills equal-depth areas the same way.
  // All edits stay inside the face and inside the Base silhouette.
  //
  // Performance: ONE transaction per stroke; the sheet images are copied
  // once when the first pixel changes and then edited IN PLACE (undo restores
  // the originals), and only the touched rectangle is invalidated. The old
  // code copied the whole sheet and pushed an undo command for every mouse
  // event, and repainted everything.
  void paintDepthDab(Editor* ed, const gfx::Point& screenPt, bool isDown, bool right)
  {
    if (!depthBrushOn() || !ed) return;
    int face = -1;
    for (int f = 0; f < kFaceCount; ++f)
      if (m_editors[f] == ed) face = f;
    if (face < 0) return;
    Doc* doc = m_docView->document();
    doc::Sprite* spr = doc ? doc->sprite() : nullptr;
    if (!spr) return;
    SharedSurfaceLayers shared = findSharedLayers(spr);
    if (!shared.baseLayer) return;

    // The frame THIS panel shows (an unlinked panel may be pinned to another
    // frame than the main editor).
    const doc::frame_t fr = ed->frame();
    const gfx::Point sp = ed->screenToEditor(screenPt);
    const gfx::Rect faceRect = sharedFaceRect(spr, face);
    const int u0 = sp.x - faceRect.x;
    const int v0 = sp.y - faceRect.y;
    const bool inFace = (u0 >= 0 && v0 >= 0 && u0 < faceRect.w && v0 < faceRect.h);

    try {
      if (isDown || !m_depthStrokeActive) {
        endDepthStroke();
        if (!inFace) return;            // a stroke starts inside the face
        // The depth map must exist BEFORE the stroke Tx opens:
        // generateDepthFromBaseOnDoc runs its own Tx, so it cannot be called
        // once the stroke Tx is open. (A blank transparent depth image is
        // NOT a substitute: transparent depth reads as a through-hole.)
        if (!shared.depthLayer || !shared.depthLayer->cel(fr)) {
          if (!generateDepthFromBaseOnDoc(doc, fr)) return;
          shared = findSharedLayers(spr);
          if (!shared.depthLayer || !shared.depthLayer->cel(fr)) return;
        }
        m_depthTx.reset(new Tx(doc, m_depthTool == DepthTool::Fill ? "Depth Fill"
                                   : m_depthTool == DepthTool::Pencil ? "Depth Pencil"
                                                                      : "Depth Brush",
                               ModifyDocument));
        // A depth layer created by an older build sits ON TOP of Base and
        // paints its flat grey over the panels: restack it below Base. Do NOT
        // hide it instead (a hidden layer's pixels cannot be edited).
        if (shared.depthLayer && shared.baseLayer) {
          DocApi api = doc->getApi(*m_depthTx);
          api.restackLayerBefore(shared.depthLayer, shared.baseLayer->parent(), shared.baseLayer);
        }
        doc::Cel* depthCel = shared.depthLayer->cel(fr);
        doc::Cel* baseCel = shared.baseLayer->cel(fr);
        if (!depthCel || !baseCel) {
          endDepthStroke();
          return;
        }
        m_depthLive = depthCel->imageRef();
        m_depthWork.reset(doc::Image::createCopy(m_depthLive.get()));
        m_baseLive = baseCel->imageRef();
        m_baseWork.reset(doc::Image::createCopy(m_baseLive.get()));
        m_depthInstalled = m_baseInstalled = false;
        m_strokeFace = face;
        m_strokeFrame = fr;
        m_strokeRight = right;
        m_strokeHasLast = false;
        m_strokeTouched.assign(size_t(faceRect.w) * size_t(faceRect.h), 0);
        m_depthStrokeActive = true;
      }
      if (!m_depthTx || !m_depthWork || !m_baseWork || face != m_strokeFace)
        return;
      if (!inFace) {
        m_strokeHasLast = false;        // do not draw a long line on re-entry
        return;
      }
      if (m_strokeHasLast && m_strokeLastU == u0 && m_strokeLastV == v0)
        return;                         // same face pixel as the last event

      int fw[kFaceCount], fh[kFaceCount];
      sharedFaceSizes(spr, fw, fh);
      int normalSize = 0;
      if (face == kFaceTop || face == kFaceDown)
        normalSize = fh[kFaceFront];
      else if (face == kFaceFront || face == kFaceBack)
        normalSize = fw[kFaceRight];
      else
        normalSize = fw[kFaceFront];
      const float step = 128.0f / float(std::max(1, normalSize / 2));
      const DepthBrushSpec brush = currentDepthBrush();
      auto& prefs = Preferences::instance();
      const DepthValue value = depthValueFromColor(m_strokeRight ? prefs.colorBar.bgColor()
                                                                  : prefs.colorBar.fgColor());

      bool depthChanged = false, baseChanged = false;
      gfx::Rect dirty;
      auto applyAt = [&](int u, int v) {
        std::pair<bool, bool> c;
        switch (m_depthTool) {
          case DepthTool::Brush:
            c = applyDepthDabToImages(m_depthWork.get(), m_baseWork.get(), faceRect, u, v,
                                      m_strokeRight, step, brush.radius, &m_strokeTouched,
                                      brush.square);
            break;
          case DepthTool::Pencil:
            c = applyDepthPaintToImages(m_depthWork.get(), m_baseWork.get(), faceRect, u, v,
                                        value, brush.radius, brush.square);
            break;
          case DepthTool::Fill:
            c = floodFillDepth(m_depthWork.get(), m_baseWork.get(), faceRect, u, v, value);
            break;
          case DepthTool::None: return;
        }
        depthChanged |= c.first;
        baseChanged |= c.second;
        if (c.first || c.second) {
          const int r = (m_depthTool == DepthTool::Fill) ? 0 : brush.radius;
          gfx::Rect rc(faceRect.x + u - r, faceRect.y + v - r, 2 * r + 1, 2 * r + 1);
          if (m_depthTool == DepthTool::Fill)
            rc = faceRect;
          dirty = dirty.isEmpty() ? rc : dirty.createUnion(rc);
        }
      };

      if (m_depthTool == DepthTool::Fill) {
        if (isDown)
          applyAt(u0, v0);
      }
      else if (m_strokeHasLast) {
        // Walk the line from the last face pixel to this one, so a fast
        // mouse movement leaves no gaps.
        const int du = u0 - m_strokeLastU, dv = v0 - m_strokeLastV;
        const int n = std::max(std::abs(du), std::abs(dv));
        for (int i = 1; i <= n; ++i)
          applyAt(m_strokeLastU + int(std::lround(double(du) * i / n)),
                  m_strokeLastV + int(std::lround(double(dv) * i / n)));
      }
      else {
        applyAt(u0, v0);
      }
      m_strokeHasLast = true;
      m_strokeLastU = u0;
      m_strokeLastV = v0;

      if (depthChanged || baseChanged) {
        DocApi api = doc->getApi(*m_depthTx);
        // Install the work copies in the cels the first time they change
        // (the Tx records the ORIGINAL images for undo); after that they are
        // edited in place.
        if (depthChanged && !m_depthInstalled) {
          api.replaceImage(spr, m_depthLive, m_depthWork);
          m_depthLive = m_depthWork;
          m_depthInstalled = true;
        }
        if (baseChanged && !m_baseInstalled) {
          api.replaceImage(spr, m_baseLive, m_baseWork);
          m_baseLive = m_baseWork;
          m_baseInstalled = true;
        }
        doc->notifySpritePixelsModified(spr, gfx::Region(dirty), fr);
      }
    }
    catch (const std::exception& e) {
      endDepthStroke();
      easymetricReportError("paintDepthDab", e.what());
    }
  }

  void endDepthStroke()
  {
    if (m_depthTx) {
      try {
        m_depthTx->commit();
      }
      catch (...) {
      }
      m_depthTx.reset();
    }
    m_depthWork.reset();
    m_depthLive.reset();
    m_baseWork.reset();
    m_baseLive.reset();
    m_strokeTouched.clear();
    m_depthStrokeActive = false;
    m_strokeHasLast = false;
    m_lastHash = 0;
  }

  void initLayersAndFraming()
  {
    Doc* doc = m_docView->document();
    doc::Sprite* spr = doc ? doc->sprite() : nullptr;
    if (!spr) return;

    // Bug 3: sync all panels to the user's chosen sheet layer
    // when there is one. When the active editor is on a
    // non-sheet layer, leave every panel's layer untouched:
    // overwriting it with texture here is exactly what flipped
    // the active layer back to texture after the user had
    // chosen something else. Panels with no layer yet (studio
    // entry) still get the texture/base fallback below.
    doc::LayerImage* chosenSheet = activeSheetLayer(spr);
    const bool busy = anyPanelEditorBusy();

    for (int f = 0; f < kFaceCount; ++f) {
      if (m_editors[f]) {
        SharedSurfaceLayers shared = findSharedLayers(spr);
        doc::LayerImage* lyr = chosenSheet;
        if (!lyr && !m_editors[f]->layer())
          lyr = shared.textureLayer ? shared.textureLayer : shared.baseLayer;
        if (!lyr && !m_editors[f]->layer()) {
          SurfaceLayerPair sp = findSurfaceLayers(spr, f);
          lyr = sp.texLayer ? sp.texLayer : sp.baseLayer;
          if (!lyr) {
            const std::string lyrName = std::string("Surface_") + kFaceSpecs[f].sliceName;
            lyr = findLayerByName(spr, lyrName);
          }
        }
        if (lyr)
          m_editors[f]->setLayer(lyr);

        if (shared.baseLayer || shared.textureLayer) {
          m_editors[f]->setClipSpriteBounds(sharedFaceRect(spr, f));
        }
        else if (doc::Slice* sl = findSliceForFace(spr, f)) {
          if (sl->getByFrame(0) && !sl->getByFrame(0)->bounds().isEmpty()) {
            m_editors[f]->setClipSpriteBounds(sl->getByFrame(0)->bounds());
          }
          else {
            m_editors[f]->setClipSpriteBounds(gfx::Rect(0, 0, spr->width(), spr->height()));
          }
        }
        else {
          m_editors[f]->setClipSpriteBounds(gfx::Rect(0, 0, spr->width(), spr->height()));
        }
        // Bug 2: fitting resets an editor's zoom/scroll. Doing
        // it while that editor (or any panel) is mid-stroke
        // makes the panel jump/resize under the user's pen, so
        // skip the fit when busy; the next idle poll fits it.
        if (!busy)
          m_editors[f]->fitClipSpriteBounds();
      }
    }
  }

protected:
  // One layout source for onResize, onPaint and splitter hit-testing,
  // so the painted handles always line up with the dragged ones.
  // colW[3] is the remainder, so the four columns always fill rc.
  // Splitter gaps are 6*guiscale wide (was 4*): the gap itself
  // is the only area that actually reaches this widget (the panes
  // cover everything else), so a 4*guiscale gap was easy to miss
  // and its cursor indication flickered at the edges. 6*guiscale
  // is the grab area, not just the painted bar.
  // Columns are stored in PIXELS, not fractions of totalW.
  // That is what makes them independent: with fractions, making
  // the window wider (or dragging the Preview boundary, whose
  // width was the remainder of a fraction sum) scaled every
  // column at once, so stretching Preview shrank/grew all
  // three surface columns. With pixel widths, the three
  // surface columns keep exactly their stored widths when
  // the window is resized, and only the Preview column
  // (col 3, the remainder) absorbs the difference. Dragging
  // a boundary changes only the two columns either side of
  // it (see onProcessMessage). Row heights are now per-column
  // too (rowHpx[c] is column c's top-row height, the bottom
  // row absorbs the rest in that column only), so dragging
  // one column's horizontal splitter never moves another
  // column's row split. First layout: quarter / half, as before.
  // Output: colRowTopH[c] = top-row height in column c,
  // colRowBotY[c] = bottom-row top edge in column c,
  // colRowBotH[c] = bottom-row height in column c.
  void computePaneLayout(const gfx::Rect& rc, int colX[4], int colW[4],
                         int colRowTopH[4], int colRowBotY[4], int colRowBotH[4]) const
  {
    const int s = std::max(1, ui::guiscale());
    const int bw = 6 * s;
    const int bh = 6 * s;
    const int totalH = std::max(16, rc.h - bh);
    const int minRow = std::min(80 * s, totalH / 2);

    if (m_previewFloating) {
      // 3-column layout when preview column is floated:
      // columns 0..2 expand to fill the entire freed width.
      const int totalW = std::max(16, rc.w - 2 * bw);
      const int minCol = std::min(80 * s, totalW / 3);

      if (!m_colInit) {
        m_colWpx[0] = totalW / 3;
        m_colWpx[1] = totalW / 3;
        m_colWpx[2] = totalW / 3;
        for (int c = 0; c < 4; ++c)
          m_rowHpx[c] = totalH / 2;
        m_colInit = true;
      }

      if (!m_floatColInit) {
        const int sumW = m_colWpx[0] + m_colWpx[1] + m_colWpx[2];
        if (sumW > 0) {
          m_floatColWpx[0] = (int)((int64_t)m_colWpx[0] * totalW / sumW);
          m_floatColWpx[1] = (int)((int64_t)m_colWpx[1] * totalW / sumW);
          m_floatColWpx[2] = totalW - m_floatColWpx[0] - m_floatColWpx[1];
        }
        else {
          m_floatColWpx[0] = totalW / 3;
          m_floatColWpx[1] = totalW / 3;
          m_floatColWpx[2] = totalW - 2 * (totalW / 3);
        }
        m_floatColInit = true;
      }

      const int avail = totalW - minCol;
      colW[0] = std::clamp(m_floatColWpx[0], minCol, avail - 2 * minCol);
      colW[1] = std::clamp(m_floatColWpx[1], minCol, avail - colW[0] - minCol);
      colW[2] = std::max(minCol, totalW - colW[0] - colW[1]);
      colW[3] = 0;

      colX[0] = rc.x;
      colX[1] = rc.x + colW[0] + bw;
      colX[2] = rc.x + colW[0] + bw + colW[1] + bw;
      colX[3] = colX[2] + colW[2];

      for (int c = 0; c < 3; ++c) {
        colRowTopH[c] = std::clamp(m_rowHpx[c], minRow, std::max(minRow, totalH - minRow));
        colRowBotH[c] = std::max(minRow, totalH - colRowTopH[c]);
        colRowBotY[c] = rc.y + colRowTopH[c] + bh;
      }
      colRowTopH[3] = 0;
      colRowBotH[3] = 0;
      colRowBotY[3] = rc.y;
    }
    else {
      // 4-column docked layout: surface columns 0..2 keep stored widths,
      // column 3 (preview/fields) absorbs the remainder.
      const int totalW = std::max(16, rc.w - 3 * bw);
      const int minCol = std::min(80 * s, totalW / 4);

      if (!m_colInit) {
        m_colWpx[0] = totalW / 4;
        m_colWpx[1] = totalW / 4;
        m_colWpx[2] = totalW / 4;
        for (int c = 0; c < 4; ++c)
          m_rowHpx[c] = totalH / 2;
        m_colInit = true;
      }

      const int avail = totalW - minCol;
      colW[0] = std::clamp(m_colWpx[0], minCol, avail - 2 * minCol);
      colW[1] = std::clamp(m_colWpx[1], minCol, avail - colW[0] - minCol);
      colW[2] = std::clamp(m_colWpx[2], minCol, avail - colW[0] - colW[1]);
      colW[3] = std::max(minCol, totalW - colW[0] - colW[1] - colW[2]);

      colX[0] = rc.x;
      colX[1] = rc.x + colW[0] + bw;
      colX[2] = rc.x + colW[0] + bw + colW[1] + bw;
      colX[3] = rc.x + colW[0] + bw + colW[1] + bw + colW[2] + bw;

      for (int c = 0; c < 4; ++c) {
        colRowTopH[c] = std::clamp(m_rowHpx[c], minRow, std::max(minRow, totalH - minRow));
        colRowBotH[c] = std::max(minRow, totalH - colRowTopH[c]);
        colRowBotY[c] = rc.y + colRowTopH[c] + bh;
      }
    }
  }

  // Bug 2: splitter under the cursor, or 0: 1..3 = vertical
  // boundary before column c, 10+c = horizontal boundary
  // between the two rows in column c only — but ONLY if the
  // cursor is actually in a splitter gap strip, not merely
  // near one. The old hit test extended bw/2+s beyond the gap
  // into both neighbouring panes, so a drawing stroke starting
  // a few pixels inside a pane, or a click on a pane header
  // that propagates up to this widget, was treated as a
  // splitter drag and resized a panel while drawing. A drag
  // may only start in [colX[c]-bw, colX[c]) vertically, or
  // [botY[c]-bh, botY[c]) horizontally within that column.
  // G1 (2026-11-07): every panel border is a splitter again —
  // vertical 1..3 between the columns (full content height)
  // and horizontal 10..13 between the two rows of each
  // column (13 = Preview/Fields, as before). A linked column
  // has no horizontal gap: its top pane spans the full
  // height, so there is no bottom pane to split. The hit area
  // is exactly the painted bar, no extension into either
  // neighbouring pane.
  // pos is in DISPLAY coordinates (MouseMessage::position() is
  // the top-level window's space, not this widget's). Convert to
  // local coordinates first: contentBounds(), colX/colW and the
  // painted bars are all local. Without the conversion the hit
  // area is shifted by the widget's origin in the window
  // (bounds().origin(), here (80,46)), so a press on the visible
  // bar misses the gap entirely — which is why no splitter drag
  // (gap 13 included) ever started. Proven 2026-11-07: a press
  // on the visible Preview/Fields bar arrived as (768,262) with
  // hit 0; toClient gives (688,216), inside the bar [216,222).
  int splitterGapAt(const gfx::Point& displayPos) const
  {
    const gfx::Point pos = toClient(displayPos);
    const gfx::Rect rc = contentBounds();
    const int s = std::max(1, ui::guiscale());
    const int bw = 6 * s;
    const int bh = 6 * s;
    int colX[4], colW[4], colTopH[4], colBotY[4], colBotH[4];
    computePaneLayout(rc, colX, colW, colTopH, colBotY, colBotH);
    if (!rc.contains(pos))
      return 0;
    for (int c = 1; c < (m_previewFloating ? 3 : 4); ++c) {
      if (pos.x >= colX[c] - bw && pos.x < colX[c])
        return c;
    }
    for (int c = 0; c < (m_previewFloating ? 3 : 4); ++c) {
      if (c < 3 && m_colLinked[c])
        continue;
      if (pos.x >= colX[c] && pos.x < colX[c] + colW[c] && pos.y >= colBotY[c] - bh &&
          pos.y < colBotY[c])
        return 10 + c;
    }
    return 0;
  }

  // Re-centre every panel's canvas in its pane without changing
  // its zoom. This is what makes a resized pane move its
  // contents instead of leaving them clipped at the old edge:
  // onResize deliberately does NOT fit (fitting would reset
  // zoom), but centring preserves zoom and keeps the face
  // visible in the new pane size. Skipped while any panel is
  // busy, so a resize never jumps under a stroke.
  void recenterPanelCanvases()
  {
    if (anyPanelEditorBusy())
      return;
    for (int f = 0; f < kFaceCount; ++f) {
      Editor* ed = m_editors[f];
      if (!ed || !m_panes[f]->isVisible())
        continue;
      const gfx::Rect clip = ed->clipSpriteBounds();
      if (clip.isEmpty())
        continue;
      ed->centerInSpritePoint(gfx::PointF(clip.x + clip.w * 0.5, clip.y + clip.h * 0.5));
    }
  }

  // View-level wheel catch: if a wheel message reaches this
  // widget, the panel editor under the mouse did NOT handle
  // it (e.g. its HAS_MOUSE flag was clear, or its state had
  // fallen back to plain Standby). Without this, that wheel
  // falls through to View scroll and displaces the canvas —
  // the reported bug, on a build that already contained the
  // state-level wheel-zoom fix. Forcing the wheel-zoom state
  // here and calling it directly, bypassing the editor's
  // hasMouse() gate, makes wheel = zoom in a surface panel
  // regardless of editor state. Fields/Preview wheels are
  // left alone: they already scroll/zoom correctly.
  bool handlePanelWheel(ui::MouseMessage* msg)
  {
    const gfx::Point pos = msg->position();
    for (int f = 0; f < kFaceCount; ++f) {
      ui::View* view = m_views[f];
      Editor* ed = m_editors[f];
      if (!view || !ed || !m_panes[f]->isVisible())
        continue;
      if (!view->viewportBounds().contains(pos))
        continue;
      if (!isPanelEditorIdle(ed) && typeid(*ed->getState()) != typeid(EasymetricWheelZoomState) &&
          typeid(*ed->getState()) != typeid(EasymetricDepthState))
        return false;
      if (typeid(*ed->getState()) != typeid(EasymetricWheelZoomState) &&
          typeid(*ed->getState()) != typeid(EasymetricDepthState))
        ed->setState(newPanelState());
      return ed->getState()->onMouseWheel(ed, msg);
    }
    return false;
  }

  bool onProcessMessage(ui::Message* msg) override
  {
    switch (msg->type()) {
      case ui::kMouseWheelMessage: {
        auto* mouseMsg = static_cast<ui::MouseMessage*>(msg);
        if (handlePanelWheel(mouseMsg))
          return true;
        // Fields wheel (2026-11-07, William: the wheel did nothing
        // in Fields and he had to drag the scrollbar): nothing in
        // the chain ever scrolled the Fields View. ui::View does
        // not handle kMouseWheelMessage itself; widgets that want
        // wheel scrolling must call View::scrollByMessage
        // explicitly (ui::ListBox, menus, etc. do). Surface-panel
        // wheels are already handled above; Preview has its own
        // wheel zoom. A wheel over the Fields pane scrolls it.
        if (m_fieldsView && m_fieldsBox && m_fieldsView->viewportBounds().contains(mouseMsg->position())) {
          ui::View::scrollByMessage(m_fieldsBox, mouseMsg);
          return true;
        }
        break;
      }
      case ui::kSetCursorMessage: {
        if (m_dragSplitter != 0) {
          ui::set_mouse_cursor(m_dragSplitter >= 10 ? ui::kSizeNSCursor : ui::kSizeWECursor);
          return true;
        }
        auto* mouseMsg = static_cast<ui::MouseMessage*>(msg);
        const int hit = splitterGapAt(mouseMsg->position());
        if (hit != 0) {
          ui::set_mouse_cursor(hit >= 10 ? ui::kSizeNSCursor : ui::kSizeWECursor);
          return true;
        }
        break;
      }
      case ui::kMouseDownMessage: {
        auto* mouseMsg = static_cast<ui::MouseMessage*>(msg);
        const int hit = splitterGapAt(mouseMsg->position());
        // Bug 2: a splitter drag may only start from a real
        // gap click, never while a panel editor is drawing,
        // moving, or selecting (mouse captured or in a
        // non-idle state). Without this, a stroke that began
        // in a panel and propagated here could start resizing.
        if (hit != 0 && mouseMsg->left() && !anyPanelEditorBusy()) {
          m_dragSplitter = hit;
          m_dragStart = toClient(mouseMsg->position());
          {
            int cx[4], cw[4], topH[4], botY[4], botH[4];
            computePaneLayout(contentBounds(), cx, cw, topH, botY, botH);
            for (int i = 0; i < 3; ++i)
              m_dragColWpx[i] = cw[i];
            m_dragPreviewW = cw[3];
            m_dragRowCol = (hit >= 10) ? (hit - 10) : 0;
            m_dragRowHpx = (hit >= 10) ? topH[hit - 10] : topH[0];
          }
          captureMouse();
          invalidate();
          return true;
        }
        break;
      }
      case ui::kMouseMoveMessage: {
        auto* mouseMsg = static_cast<ui::MouseMessage*>(msg);
        if (m_dragSplitter != 0) {
          // If mouse capture was lost mid-drag (window deactivated,
          // drag interrupted, mouse released outside the widget),
          // a later move must NOT keep resizing panes, and the
          // resize cursor must not stay stuck. Cancel the drag
          // instead of applying a stale dragStart to a new position.
          if (!hasCapture() || anyPanelEditorBusy()) {
            m_dragSplitter = 0;
            m_hoverSplitter = 0;
            invalidate();
            return true;
          }
          const gfx::Rect rc = contentBounds();
          const int s = std::max(1, ui::guiscale());
          const int bw = 6 * s;
          const int bh = 6 * s;
          const int numCols = m_previewFloating ? 3 : 4;
          const int totalW = std::max(16, rc.w - (numCols - 1) * bw);
          const int totalH = std::max(16, rc.h - bh);
          const int minColPx = std::min(80 * s, totalW / numCols);
          const int minRowPx = std::min(80 * s, totalH / 2);
          if (m_dragSplitter < 10 && m_dragSplitter < numCols) {
            const gfx::Point dragPos = toClient(mouseMsg->position());
            const int deltaPx = dragPos.x - m_dragStart.x;
            const int left = m_dragSplitter - 1;
            int pair = 0;
            if (m_previewFloating) {
              pair = m_dragColWpx[left] + m_dragColWpx[m_dragSplitter];
              const int hiCol = std::max(minColPx, pair - minColPx);
              const int newLeft = std::clamp(m_dragColWpx[left] + deltaPx,
                                             minColPx, hiCol);
              m_floatColWpx[left] = newLeft;
              m_floatColWpx[m_dragSplitter] = pair - newLeft;
            }
            else {
              if (m_dragSplitter < 3)
                pair = m_dragColWpx[left] + m_dragColWpx[m_dragSplitter];
              else
                pair = m_dragColWpx[left] + m_dragPreviewW;
              const int hiCol = std::max(minColPx, pair - minColPx);
              const int newLeft = std::clamp(m_dragColWpx[left] + deltaPx,
                                             minColPx, hiCol);
              m_colWpx[left] = newLeft;
              if (m_dragSplitter < 3)
                m_colWpx[m_dragSplitter] = pair - newLeft;
            }
          }
          else {
            const gfx::Point dragPos = toClient(mouseMsg->position());
            const int deltaPx = dragPos.y - m_dragStart.y;
            const int hiRow = std::max(minRowPx, totalH - minRowPx);
            m_rowHpx[m_dragRowCol] = std::clamp(m_dragRowHpx + deltaPx,
                                                minRowPx, hiRow);
          }
          layout();
          invalidate();
          return true;
        }
        const int hit = splitterGapAt(mouseMsg->position());
        if (hit != m_hoverSplitter) {
          m_hoverSplitter = hit;
          invalidate();
        }
        break;
      }
      case ui::kMouseUpMessage: {
        if (m_dragSplitter != 0) {
          m_dragSplitter = 0;
          releaseMouse();
          recenterPanelCanvases();
          invalidate();
          return true;
        }
        break;
      }
      case ui::kMouseLeaveMessage: {
        if (m_dragSplitter != 0) {
          m_dragSplitter = 0;
          if (hasCapture())
            releaseMouse();
        }
        if (m_hoverSplitter != 0) {
          m_hoverSplitter = 0;
          invalidate();
        }
        break;
      }
    }
    return ui::Widget::onProcessMessage(msg);
  }

  // Studio tool strip height (0 until the strip is built). The strip
  // sits across the top of the studio; the four pane columns live
  // in the rect below it.
  int toolStripHeight() const
  {
    return m_toolStrip ? 24 * ui::guiscale() : 0;
  }

  gfx::Rect contentBounds() const
  {
    gfx::Rect rc = clientBounds();
    const int sh = toolStripHeight();
    rc.y += sh;
    rc.h -= sh;
    return rc;
  }

  void onResize(ui::ResizeEvent& ev) override
  {
    setBoundsQuietly(ev.bounds());
    const gfx::Rect rc = childrenBounds();
    const int s = ui::guiscale();
    const int bh = 6 * s;
    const int stripH = toolStripHeight();
    if (m_toolStrip)
      m_toolStrip->setBounds(gfx::Rect(rc.x, rc.y, rc.w, stripH));
    gfx::Rect contentRc = rc;
    contentRc.y += stripH;
    contentRc.h -= stripH;
    int colX[4], colW[4], colTopH[4], colBotY[4], colBotH[4];
    computePaneLayout(contentRc, colX, colW, colTopH, colBotY, colBotH);
    const int totalH = std::max(16, contentRc.h - bh);

    // Cols 0..2: Opposing face pairs
    for (int c = 0; c < 3; ++c) {
      const int topFace = c * 2;
      const int btmFace = c * 2 + 1;
      if (m_colLinked[c]) {
        m_panes[btmFace]->setVisible(false);
        m_panes[topFace]->setVisible(true);
        m_panes[topFace]->setBounds(gfx::Rect(colX[c], contentRc.y, colW[c], totalH + bh));
      }
      else {
        m_panes[topFace]->setVisible(true);
        m_panes[btmFace]->setVisible(true);
        m_panes[topFace]->setBounds(gfx::Rect(colX[c], contentRc.y, colW[c], colTopH[c]));
        m_panes[btmFace]->setBounds(gfx::Rect(colX[c], colBotY[c], colW[c], colBotH[c]));
      }
    }

    // Col 3: Preview (6), Fields (7)
    if (m_previewFloating) {
      m_panes[6]->setVisible(false);
      m_panes[7]->setVisible(false);
    }
    else {
      m_panes[6]->setVisible(true);
      m_panes[7]->setVisible(true);
      m_panes[6]->setBounds(gfx::Rect(colX[3], contentRc.y, colW[3], colTopH[3]));
      m_panes[7]->setBounds(gfx::Rect(colX[3], colBotY[3], colW[3], colBotH[3]));
    }

    // NOTE: deliberately no fitClipSpriteBounds() here. Fitting on
    // every resize reset each surface editor's zoom/scroll whenever
    // a splitter was dragged or the window was resized, which is
    // what made the panels feel unscrollable/unzoomable. Initial
    // fitting still happens in initLayersAndFraming(). Centring
    // (not fitting) is safe here: it preserves zoom and only
    // moves the canvas so it stays visible in its new pane.
    // It is skipped while a panel is busy, and during a
    // splitter drag the drag-end recenter does the same job.
    if (m_dragSplitter == 0)
      recenterPanelCanvases();
  }

  void onPaint(ui::PaintEvent& ev) override
  {
    ui::Graphics* g = ev.graphics();
    const gfx::Rect rc = clientBounds();
    const int s = ui::guiscale();
    const int bw = 6 * s;
    const int bh = 6 * s;
    int colX[4], colW[4], colTopH[4], colBotY[4], colBotH[4];
    computePaneLayout(contentBounds(), colX, colW, colTopH, colBotY, colBotH);

    // Dark charcoal surround & separator lines
    g->fillRect(gfx::rgba(28, 35, 43), rc);

    const gfx::Color sepCol = gfx::rgba(45, 55, 66);
    const gfx::Color gripCol = gfx::rgba(80, 95, 110);
    const gfx::Color activeCol = gfx::rgba(120, 170, 220);

    // G1: separator bars on every draggable gap, so the user
    // can see where to grab. Vertical bars between the columns
    // (full content height) and horizontal bars between the
    // two rows of each unlinked column (plus Preview/Fields).
    // Each bar sits exactly in its gap, which is also what
    // splitterGapAt() hit-tests, so the resize cursor appears
    // where the bar is drawn. A linked column has no
    // horizontal bar: its top pane spans the full height.
    const gfx::Rect contentRc = contentBounds();
    for (int c = 1; c < (m_previewFloating ? 3 : 4); ++c) {
      const bool active = (m_hoverSplitter == c || m_dragSplitter == c);
      g->fillRect(active ? activeCol : sepCol,
                  gfx::Rect(colX[c] - bw, contentRc.y, bw, contentRc.h));
      const int midX = colX[c] - bw / 2;
      const int midY = contentRc.y + contentRc.h / 2;
      g->fillRect(active ? activeCol : gripCol,
                  gfx::Rect(midX - 1 * s, midY - 12 * s, 2 * s, 24 * s));
    }
    for (int c = 0; c < (m_previewFloating ? 3 : 4); ++c) {
      if (c < 3 && m_colLinked[c])
        continue;
      const int gap = 10 + c;
      const bool active = (m_hoverSplitter == gap || m_dragSplitter == gap);
      g->fillRect(active ? activeCol : sepCol,
                  gfx::Rect(colX[c], colBotY[c] - bh, colW[c], bh));
      const int midX = colX[c] + colW[c] / 2;
      const int midY = colBotY[c] - bh / 2;
      g->fillRect(active ? activeCol : gripCol,
                  gfx::Rect(midX - 12 * s, midY - 1 * s, 24 * s, 2 * s));
    }
  }

private:
  void buildFieldsControls()
  {
    const int s = ui::guiscale();
    m_fieldsBox->setBorder(gfx::Border(4 * s));
    m_fieldsBox->setChildSpacing(3 * s);

    // Row 0: Objects (2026-11-17). Which object the panels edit, a guided
    // New object dialog, the focused object's place (X right, Y up, Z to
    // the front, from Object 1's corner) and its front rank, and whether
    // the other objects are shown faded in the panels.
    {
      auto* row = new ui::HBox;
      row->setChildSpacing(4 * s);
      m_objectCombo = new ui::ComboBox;
      m_objectCombo->setExpansive(true);
      auto* newBtn = new ui::Button("New object...");
      row->addChild(new ui::Label("Object:"));
      row->addChild(m_objectCombo);
      m_fieldsBox->addChild(row);
      // Short rows: the Fields pane can be under 200 px wide.
      auto* newRow = new ui::HBox;
      newRow->setChildSpacing(4 * s);
      m_showOthersCheck = new ui::CheckBox("Show others");
      m_showOthersCheck->setSelected(m_showOthers);
      // Delete object (Arin, 2026-11-18): removes the focused object (not
      // Object 1) after a confirmation; focus falls back to the object
      // before it in the list. Undo brings it back.
      m_deleteObjBtn = new ui::Button("Delete object");
      m_deleteObjBtn->setEnabled(false);
      newRow->addChild(newBtn);
      newRow->addChild(m_deleteObjBtn);
      m_fieldsBox->addChild(newRow);
      m_deleteObjBtn->Click.connect([this] { deleteFocusedObject(); });
      m_objectCombo->Change.connect([this] {
        if (m_updatingObjectUi)
          return;
        const int i = m_objectCombo->getSelectedItemIndex();
        if (i >= 0 && i < int(m_objectComboKeys.size()))
          focusObject(m_objectComboKeys[size_t(i)]);
      });
      newBtn->Click.connect([this] { openNewObjectDialog(); });

      // (The "At X / Y / Z + Move" row was removed 2026-11-18: every object
      // shares the model size and space, so an object's place is simply
      // where it is drawn.)

      auto* rankRow = new ui::HBox;
      rankRow->setChildSpacing(4 * s);
      m_objRankCombo = new ui::ComboBox;
      m_objRankCombo->addItem("Auto");
      for (int r = 0; r <= 9; ++r)
        m_objRankCombo->addItem(fmt::format("Rank {}", r));
      m_objRankCombo->setSelectedItemIndex(0);
      m_objRankCombo->setExpansive(true);
      rankRow->addChild(new ui::Label("In front:"));
      rankRow->addChild(m_objRankCombo);
      m_fieldsBox->addChild(rankRow);
      // Own row: next to Delete object or the rank combo it was cut off in a
      // narrow Fields pane (2026-11-18).
      m_fieldsBox->addChild(m_showOthersCheck);
      m_objRankCombo->Change.connect([this] {
        if (m_updatingObjectUi)
          return;
        Doc* doc = m_docView->document();
        if (!doc)
          return;
        const int rank = m_objRankCombo->getSelectedItemIndex() - 1;
        setObjectOnDoc(doc, m_focusKey, nullptr, &rank, nullptr, nullptr);
        m_lastHash = 0;
        pollAndRender(true);
      });
      m_showOthersCheck->Click.connect([this] {
        m_showOthers = m_showOthersCheck->isSelected();
        m_lastHash = 0;
        pollAndRender(true);
        invalidateAllPanels();
      });
    }

    // Row 1: Rotation on Y-axis + Cam Angle
    auto* rotRow = new ui::HBox;
    rotRow->setChildSpacing(4 * s);
    m_yawSlider = new ui::Slider(-180, 180, 45);
    m_pitchSlider = new ui::Slider(-75, 75, 30);
    m_yawSlider->setExpansive(true);
    m_pitchSlider->setExpansive(true);
    rotRow->addChild(new ui::Label("Y-Axis Rot:"));
    rotRow->addChild(m_yawSlider);
    rotRow->addChild(new ui::Label("Cam Angle:"));
    rotRow->addChild(m_pitchSlider);
    m_fieldsBox->addChild(rotRow);

    // Row 2: Pivot X/Y/Z + Center + Base
    auto* pivRow = new ui::HBox;
    pivRow->setChildSpacing(4 * s);
    m_pivXSlider = new ui::Slider(0, 100, 50);
    m_pivYSlider = new ui::Slider(0, 100, 50);
    m_pivZSlider = new ui::Slider(0, 100, 50);
    m_pivXSlider->setExpansive(true);
    m_pivYSlider->setExpansive(true);
    m_pivZSlider->setExpansive(true);
    auto* pivCenterBtn = new ui::Button("Center");
    auto* pivBaseBtn = new ui::Button("Base");
    pivRow->addChild(new ui::Label("Pivot X/Y/Z:"));
    pivRow->addChild(m_pivXSlider);
    pivRow->addChild(m_pivYSlider);
    pivRow->addChild(m_pivZSlider);
    pivRow->addChild(pivCenterBtn);
    pivRow->addChild(pivBaseBtn);
    m_fieldsBox->addChild(pivRow);

    // Row 3: Tag / Angle Preset Dropdown + Save Tag
    auto* tagRow = new ui::HBox;
    tagRow->setChildSpacing(4 * s);
    m_tagCombo = new ui::ComboBox;
    m_tagCombo->setExpansive(true);
    populateTagCombo();
    auto* saveTagBtn = new ui::Button("Save Tag");
    tagRow->addChild(new ui::Label("Tag / Angle:"));
    tagRow->addChild(m_tagCombo);
    tagRow->addChild(saveTagBtn);
    m_fieldsBox->addChild(tagRow);

    // Row 4: Maps Toggles + Render Type Dropdown
    auto* mapRow = new ui::HBox;
    mapRow->setChildSpacing(4 * s);
    m_mapDiffuseCheck = new ui::CheckBox("Diffuse");
    m_mapNormalCheck = new ui::CheckBox("Normal");
    m_mapDepthCheck = new ui::CheckBox("Depth");
    m_mapDiffuseCheck->setSelected(true);

    m_renderTypeCombo = new ui::ComboBox;
    m_renderTypeCombo->addItem("Render: Textured (Lit)");
    m_renderTypeCombo->addItem("Render: Textured (Unlit)");
    m_renderTypeCombo->addItem("Render: Colored (Faces)");
    m_renderTypeCombo->addItem("Render: Normal Shaded");
    m_renderTypeCombo->addItem("Render: Depth Shaded");
    m_renderTypeCombo->addItem("Render: Wireframe + Tex");
    m_renderTypeCombo->setSelectedItemIndex(0);
    m_renderTypeCombo->setExpansive(true);

    mapRow->addChild(new ui::Label("Maps:"));
    mapRow->addChild(m_mapDiffuseCheck);
    mapRow->addChild(m_mapNormalCheck);
    mapRow->addChild(m_mapDepthCheck);
    mapRow->addChild(m_renderTypeCombo);
    m_fieldsBox->addChild(mapRow);

    // Row 5: Stroke Outline + Cutouts.
    // (2026-11-17, Arin: removed from Fields because they duplicate other
    // controls or have no effect in the studio: Sym X / Sym Y / Center Sym
    // (= the SX / SY panel header buttons; centring is automatic), Lock
    // Borders (slices are not drawn in panels), Link Dims (only reacted to
    // slice edits made outside the studio), Ghost hints (= the Preview
    // header Ghost button) and Pop out Column (= the Preview header button).
    // Their member pointers stay null; every user checks for null.)
    auto* symRow = new ui::HBox;
    symRow->setChildSpacing(4 * s);
    m_strokeCheck = new ui::CheckBox("Stroke Outline");
    m_cutoutCheck = new ui::CheckBox("Cutouts");
    m_strokeCheck->setSelected(true);
    m_cutoutCheck->setSelected(true);
    symRow->addChild(m_strokeCheck);
    symRow->addChild(m_cutoutCheck);
    m_fieldsBox->addChild(symRow);

    // Auto-align (persisted in the [easymetric] preferences section).
    auto* alignRow = new ui::HBox;
    alignRow->setChildSpacing(4 * s);
    auto& epref = Preferences::instance().easymetric;
    m_autoAlignCheck = new ui::CheckBox("Auto-align views");
    m_autoAlignCheck->setSelected(epref.autoAlign());
    m_autoAlignCheck->Click.connect(
      [this] { Preferences::instance().easymetric.autoAlign(m_autoAlignCheck->isSelected()); });
    m_alignLimitSlider = new ui::Slider(0, 16, std::clamp(epref.autoAlignLimit(), 0, 16));
    m_alignLimitSlider->setExpansive(true);
    m_alignLimitSlider->Change.connect([this] {
      Preferences::instance().easymetric.autoAlignLimit(m_alignLimitSlider->getValue());
    });
    m_showCutoutsCheck = new ui::CheckBox("Show cut voxels (magenta)");
    m_showCutoutsCheck->setSelected(epref.showCutouts());
    m_showCutoutsCheck->Click.connect([this] {
      Preferences::instance().easymetric.showCutouts(m_showCutoutsCheck->isSelected());
      pollAndRender(true);
    });
    alignRow->addChild(m_autoAlignCheck);
    alignRow->addChild(new ui::Label("max px:"));
    alignRow->addChild(m_alignLimitSlider);
    alignRow->addChild(m_showCutoutsCheck);
    m_fieldsBox->addChild(alignRow);

    // (Row 5b, the per-face "Size:" combo with Top/Front/Right/... XY/YZ resizing,
    // was removed 2026-11-10 at William's request: the object is resized only
    // through the three-dimension Object W/H/D controls below.)

    // Row 5c: Object Size (Width/Height/Depth) — one set of
    // three dimensions for the whole object, applied to ALL
    // face bound boxes concurrently. Mapping: Width = X
    // (Top/Bottom/Front/Back width), Height = Y
    // (Front/Back/Right/Left height), Depth = Z
    // (Top/Bottom/Right/Left width). This replaces the
    // per-face-only workflow for sizing the object: changing
    // Width here does not leave Front at the old width while
    // Top moves, because every face using that dimension is
    // set to the same value in one rebuild.
    auto* objRow = new ui::HBox;
    objRow->setChildSpacing(4 * s);
    int curX = 24, curY = 24, curZ = 24;
    if (Doc* doc0 = m_docView->document()) {
      // NOTE: do NOT call extractSixSurfaces() here. That member
      // reads m_useFaceDepthCheck, which is only created later
      // in this function; calling it during construction read
      // an uninitialised pointer and segfaulted studio open
      // intermittently (Widget::isSelected on 0x101). Face
      // sizes alone are enough for the initial slider values.
      int fw[kFaceCount], fh[kFaceCount];
      sharedFaceSizes(doc0->sprite(), fw, fh);
      curX = fw[kFaceFront];
      curY = fh[kFaceFront];
      curZ = fw[kFaceRight];
    }
    // Typed whole numbers (1..256), labelled; refreshed by refreshObjectSizeFields().
    auto* objXSlider = new NumberField(1, 256, curX);
    auto* objYSlider = new NumberField(1, 256, curY);
    auto* objZSlider = new NumberField(1, 256, curZ);
    m_objSizeFields = { objXSlider, objYSlider, objZSlider };
    auto* applyObjBtn = new ui::Button("Apply Model Size");
    // Three short rows instead of one very wide one: the buttons used to sit
    // past the right edge of the Fields pane (2026-11-17). Since 2026-11-18
    // this is the MODEL size, shared by every object.
    objRow->addChild(new ui::Label("Model size  W"));
    objRow->addChild(objXSlider);
    objRow->addChild(new ui::Label("H"));
    objRow->addChild(objYSlider);
    objRow->addChild(new ui::Label("D"));
    objRow->addChild(objZSlider);
    m_fieldsBox->addChild(objRow);
    auto* objRow2 = new ui::HBox;
    objRow2->setChildSpacing(4 * s);
    objRow2->addChild(applyObjBtn);
    m_fieldsBox->addChild(objRow2);

    auto* objRow3 = new ui::HBox;
    objRow3->setChildSpacing(4 * s);
    auto* easyStartBtn = new ui::Button("Easy Start (1 pixel)");
    objRow3->addChild(easyStartBtn);
    auto* blockBtn = new ui::Button("Start from block...");
    objRow3->addChild(blockBtn);
    m_fieldsBox->addChild(objRow3);
    blockBtn->Click.connect([this] { openBlockDialog(); });
    easyStartBtn->Click.connect([this] {
      Doc* doc = m_docView->document();
      if (!doc || !doc->sprite()) return;
      // Inside the studio the shared sheets already exist, so the old
      // setupSixSurfacesOnDoc(seed) path did nothing (2026-11-17). Easy start
      // = a 1x1x1 block centred in the current cells, through the same path
      // as Start from block (replaces the views; undoable).
      int fw[kFaceCount], fh[kFaceCount];
      sharedFaceSizes(doc->sprite(), fw, fh);
      const BlockLayout b =
        planBlockLayout(1, 1, 1, fw[kFaceFront], fh[kFaceFront], fw[kFaceRight]);
      const doc::color_t col =
        color_utils::color_for_image(Preferences::instance().colorBar.fgColor(), doc::IMAGE_RGB);
      std::string err;
      const doc::frame_t fr = m_editors[0] ? m_editors[0]->frame() : doc::frame_t(0);
      if (!applyBlockLayout(doc, fr, b, doc::rgba(doc::rgba_getr(col), doc::rgba_getg(col),
                                                  doc::rgba_getb(col), 255), err))
        easymetricReportError("easyStart", err);
      initLayersAndFraming();
      m_lastHash = 0;
      pollAndRender(true);
      if (auto* sb = StatusBar::instance())
        sb->showTip(1200, "Easy start: 1 central pixel per face");
    });

    // Apply = resize the MODEL (every object). Arin, 2026-11-18: a bigger
    // size used to leave the old drawing in the top-left of each view (a
    // slab instead of a cube), so it now asks each time: stretch the
    // drawings to the new size, or keep them unscaled and centred.
    auto applyModelSize = [this, objXSlider, objYSlider, objZSlider] {
      Doc* doc = m_docView->document();
      if (!doc || !doc->sprite()) return;
      const int box[3] = { std::clamp(objXSlider->getValue(), 1, 256),
                           std::clamp(objYSlider->getValue(), 1, 256),
                           std::clamp(objZSlider->getValue(), 1, 256) };
      int cur[3];
      modelBox(doc->sprite(), cur);
      if (cur[0] == box[0] && cur[1] == box[1] && cur[2] == box[2])
        return;
      stopAllPlayback();
      const int nObj = int(resolveObjects(doc->sprite()).size());
      const int choice = ui::Alert::show(fmt::format(
        "Model size<<Resize {} to {} x {} x {} (was {} x {} x {})?"
        "<<Stretch: every view's drawing is scaled to the new size."
        "<<Keep pixels: drawings stay their size, centred; the rest is empty."
        "||&Stretch drawings||&Keep pixels||&Cancel",
        nObj > 1 ? fmt::format("all {} objects", nObj) : std::string("the model"), box[0],
        box[1], box[2], cur[0], cur[1], cur[2]));
      if (choice != 1 && choice != 2) {
        refreshObjectSizeFields(true);
        return;
      }
      resizeModelOnDoc(doc, box, choice == 1 ? ResizeMode::Stretch : ResizeMode::Keep);
      initLayersAndFraming();
      m_refitPanels = 2;
      m_lastHash = 0;
      pollAndRender(true);
      if (auto* sb = StatusBar::instance())
        sb->showTip(1500, fmt::format("Model size {} x {} x {} ({})", box[0], box[1], box[2],
                                      choice == 1 ? "stretched" : "pixels kept"));
    };
    applyObjBtn->Click.connect(applyModelSize);
    for (NumberField* nf : { objXSlider, objYSlider, objZSlider })
      nf->Enter.connect(applyModelSize);

    // Row 6: Per-Face Editable Depth Maps + Live Sync
    auto* depthRow = new ui::HBox;
    depthRow->setChildSpacing(4 * s);
    auto* genFaceDepthBtn = new ui::Button("Generate Depth from Base");
    m_useFaceDepthCheck = new ui::CheckBox("Use Face Depth Layer");
    m_liveBakeCheck = new ui::CheckBox("Live Sync");
    m_useFaceDepthCheck->setSelected(true);

    depthRow->addChild(genFaceDepthBtn);
    depthRow->addChild(m_useFaceDepthCheck);
    depthRow->addChild(m_liveBakeCheck);
    m_fieldsBox->addChild(depthRow);

    // NOTE (2026-11-07): the Depth Sculpt tool moved to the studio
    // tool strip (buildToolStrip / setToolMode); there is no Fields
    // Depth Brush row any more. The depth *map* controls (Generate
    // Depth from Base, Use Face Depth Layer, Live Sync) stay here.

    // Row 6b: 2-Layer Smart Object & Clipping Structure
    auto* smartRow = new ui::HBox;
    smartRow->setChildSpacing(4 * s);
    auto* setupSmartObjectsBtn = new ui::Button("Setup 2-Layer Smart Objects");
    auto* convertToGroupBtn = new ui::Button("Convert to Groups");
    setupSmartObjectsBtn->setExpansive(true);
    convertToGroupBtn->setExpansive(true);
    smartRow->addChild(setupSmartObjectsBtn);
    smartRow->addChild(convertToGroupBtn);
    m_fieldsBox->addChild(smartRow);

    // Zoom row: buttons + label mirror the preview widget's zoom, so
    // the rendering can also be zoomed without the mouse wheel.
    auto* zoomRow = new ui::HBox;
    zoomRow->setChildSpacing(4 * s);
    auto* zoomOutBtn = new ui::Button("-");
    auto* zoomResetBtn = new ui::Button("100%");
    auto* zoomInBtn = new ui::Button("+");
    m_zoomLabel = new ui::Label("100%");
    zoomRow->addChild(new ui::Label("Preview Zoom:"));
    zoomRow->addChild(zoomOutBtn);
    zoomRow->addChild(zoomInBtn);
    zoomRow->addChild(zoomResetBtn);
    zoomRow->addChild(m_zoomLabel);
    m_fieldsBox->addChild(zoomRow);

    // Preview Background row: colour picker + alpha.
    // Preview-only: it changes what the Preview pane shows
    // behind the model, not the baked Iso layers or the
    // exported .OBJ, whose background stays transparent.
    // Alpha 0 (default) = checkerboard as before; 255 = solid.
    auto* bgRow = new ui::HBox;
    bgRow->setChildSpacing(4 * s);
    m_bgColorButton = new app::ColorButton(app::Color::fromRgb(0, 0, 0),
                                           doc::IMAGE_RGB,
                                           app::ColorButtonOptions());
    m_bgAlphaSlider = new ui::Slider(0, 255, 0);
    m_bgAlphaSlider->setExpansive(true);
    bgRow->addChild(new ui::Label("Preview BG:"));
    bgRow->addChild(m_bgColorButton);
    bgRow->addChild(new ui::Label("Alpha:"));
    bgRow->addChild(m_bgAlphaSlider);
    m_fieldsBox->addChild(bgRow);

    // Floating column row: the Preview+Fields column in the
    // studio is small, so the whole column can be popped out
    // into a moveable, sizeable floating window instead, and
    // docked back at any time. Closing the floating window
    // docks it back automatically. The same pop-out is also on
    // the Preview header (opposite the label).
    // (The Fields "Pop out Column" button was removed 2026-11-17: the
    // Preview header has the same Pop out / Dock button.)

    auto* source = new ui::ComboBox;
    source->addItem(Strings::easymetric_model_six_surfaces());
    source->addItem(Strings::easymetric_model_imported_model());
    source->setSelectedItemIndex(0);
    source->Change.connect([this, source] {
      if (source->getSelectedItemIndex() == 1)
        switchStudioSource(m_docView, true);
    });
    m_fieldsBox->addChild(source);

    // Row 7: Action Buttons (Render, Export .OBJ, Exit)
    auto* actRow = new ui::HBox;
    actRow->setChildSpacing(4 * s);
    auto* renderBtn = new ui::Button("&Render");
    auto* exportObjBtn = new ui::Button("Export .&OBJ...");
    auto* exitBtn = new ui::Button("E&xit Studio");
    renderBtn->setExpansive(true);
    exportObjBtn->setExpansive(true);

    actRow->addChild(renderBtn);
    actRow->addChild(exportObjBtn);
    actRow->addChild(exitBtn);
    m_fieldsBox->addChild(actRow);

    // Event connections
    auto onParam = [this] {
      m_lastHash = 0;
      pollAndRender(true);
    };

    m_yawSlider->Change.connect(onParam);
    m_pitchSlider->Change.connect(onParam);
    m_pivXSlider->Change.connect(onParam);
    m_pivYSlider->Change.connect(onParam);
    m_pivZSlider->Change.connect(onParam);
    m_mapDiffuseCheck->Click.connect(onParam);
    m_mapNormalCheck->Click.connect(onParam);
    m_mapDepthCheck->Click.connect(onParam);
    m_renderTypeCombo->Change.connect(onParam);
    m_strokeCheck->Click.connect(onParam);
    m_cutoutCheck->Click.connect(onParam);
    m_useFaceDepthCheck->Click.connect(onParam);
    m_liveBakeCheck->Click.connect(onParam);
    // BG is composited into the preview image itself (see
    // render3DModel), so changing it must re-render, not just
    // update the widget fill that the opaque surface covers.
    auto applyBg = [this] {
      if (m_previewWidget && m_bgColorButton && m_bgAlphaSlider) {
        const app::Color c = m_bgColorButton->getColor();
        m_previewWidget->setBackgroundColor(c.getRed(), c.getGreen(), c.getBlue(),
                                            m_bgAlphaSlider->getValue());
        m_lastHash = 0;
        pollAndRender(true);
      }
    };
    m_bgColorButton->Change.connect([applyBg](const app::Color&) { applyBg(); });
    m_bgAlphaSlider->Change.connect(applyBg);
    // NOTE (2026-11-07): the Depth Sculpt tool moved to the studio
    // tool strip (buildToolStrip / setToolMode); there is no Fields
    // Depth Brush checkbox any more.

    m_previewWidget->OrbitDrag.connect([this](int newYaw, int newPitch) {
      m_yawSlider->setValue(newYaw);
      m_pitchSlider->setValue(newPitch);
      m_lastHash = 0;
      pollAndRender(true);
    });

    zoomOutBtn->Click.connect([this] { m_previewWidget->zoomOut(); });
    zoomInBtn->Click.connect([this] { m_previewWidget->zoomIn(); });
    zoomResetBtn->Click.connect([this] { m_previewWidget->resetZoom(); });
    m_previewWidget->ZoomChanged.connect([this](double zoom) {
      if (m_zoomLabel)
        m_zoomLabel->setText(fmt::format("{}%", int(std::round(zoom * 100.0))));
    });

    pivCenterBtn->Click.connect([this] {
      m_pivXSlider->setValue(50);
      m_pivYSlider->setValue(50);
      m_pivZSlider->setValue(50);
      m_lastHash = 0;
      pollAndRender(true);
    });

    pivBaseBtn->Click.connect([this] {
      m_pivXSlider->setValue(50);
      m_pivYSlider->setValue(100);
      m_pivZSlider->setValue(50);
      m_lastHash = 0;
      pollAndRender(true);
    });

    m_tagCombo->Change.connect([this] { onTagSelected(); });
    saveTagBtn->Click.connect([this] { saveCurrentAngleAsTag(); });

    setupSmartObjectsBtn->Click.connect([this] {
      Doc* doc = m_docView->document();
      if (doc) {
        setupTwoLayerStructure(doc, true);
        initLayersAndFraming();
        m_lastHash = 0;
        pollAndRender(true);
        if (auto* sb = StatusBar::instance())
          sb->showTip(1500, "Initialized 2-layer smart object surface hierarchy");
      }
    });

    convertToGroupBtn->Click.connect([this] {
      Doc* doc = m_docView->document();
      if (doc) {
        convertSmartObjectsToGroups(doc);
        initLayersAndFraming();
        m_lastHash = 0;
        pollAndRender(true);
        if (auto* sb = StatusBar::instance())
          sb->showTip(1500, "Converted smart objects to layer groups");
      }
    });

    // Show the document's actual symmetry mode in the Fields
    // checkboxes and the header Sym buttons on open.
    syncSymmetryButtons();

    genFaceDepthBtn->Click.connect([this] { generateEditableFaceDepthLayer(); });
    renderBtn->Click.connect([this] { bakeToActiveDocLayers(); });
    exportObjBtn->Click.connect([this] { exportToWavefrontObj(); });
    exitBtn->Click.connect([this] {
      syncAllLinkedCels();
      // Clean studio exit: prepareForExit() clears the session
      // marker (so a normal Exit can never be reported as a crash
      // on the next entry), restores the wheel-zoom pref,
      // symmetry and mask, stops the polling timer and closes a
      // floating Preview & Fields window.
      prepareForExit();
      m_docView->exitMultiPaneMode();
    });
  }

  struct PresetItem {
    std::string label;
    int yaw;
    int pitch;
    int frame;
  };
  std::vector<PresetItem> m_presets;
  bool m_updatingCombo = false;

  void populateTagCombo()
  {
    m_updatingCombo = true;
    m_tagCombo->deleteAllItems();
    m_presets.clear();

    auto add = [&](const std::string& lbl, int yaw, int pitch, int fr = -1) {
      m_presets.push_back({ lbl, yaw, pitch, fr });
      m_tagCombo->addItem(lbl);
    };

    add("Fast Rotation", 45, 30);
    add("Iso 2:1 (Yaw 45°, Cam 30°)", 45, 30);
    add("Front View (Yaw 0°, Cam 0°)", 0, 0);
    add("Back View (Yaw 180°, Cam 0°)", 180, 0);
    add("Right View (Yaw -90°, Cam 0°)", -90, 0);
    add("Left View (Yaw 90°, Cam 0°)", 90, 0);
    add("Top View (Yaw 45°, Cam 60°)", 45, 60);
    add("Bottom View (Yaw 45°, Cam -60°)", 45, -60);
    add("Dimetric (Yaw 30°, Cam 26°)", 30, 26);
    add("Trimetric (Yaw 60°, Cam 35°)", 60, 35);

    Doc* doc = m_docView->document();
    if (doc && doc->sprite()) {
      for (const doc::Tag* tag : doc->sprite()->tags()) {
        int ty = 45, tp = 30;
        const std::string& udata = tag->userData().text();
        if (std::sscanf(udata.c_str(), "easymetric:yaw=%d,pitch=%d", &ty, &tp) != 2) {
          std::sscanf(tag->name().c_str(), "Yaw%d_Cam%d", &ty, &tp);
        }
        add(fmt::format("Tag: {} [f{}..{}] ({}°, {}°)",
                        tag->name(), tag->fromFrame() + 1, tag->toFrame() + 1, ty, tp),
            ty, tp, int(tag->fromFrame()));
      }
    }

    m_tagCombo->setSelectedItemIndex(0);
    m_updatingCombo = false;
  }

  void onTagSelected()
  {
    if (m_updatingCombo) return;
    const int idx = m_tagCombo->getSelectedItemIndex();
    if (idx < 0 || idx >= int(m_presets.size())) return;
    const auto& p = m_presets[idx];
    m_yawSlider->setValue(p.yaw);
    m_pitchSlider->setValue(p.pitch);
    if (p.frame >= 0 && m_docView->editor())
      m_docView->editor()->setFrame(doc::frame_t(p.frame));
    m_lastHash = 0;
    pollAndRender(true);
  }

  void saveCurrentAngleAsTag()
  {
    Doc* doc = m_docView->document();
    if (!doc || !doc->sprite()) return;
    const doc::frame_t fr = (m_docView->editor() ? m_docView->editor()->frame() : doc::frame_t(0));
    const int yaw = m_yawSlider->getValue();
    const int pitch = m_pitchSlider->getValue();
    const std::string tagName = fmt::format("Yaw{}_Cam{}", yaw, pitch);
    const std::string meta = fmt::format("easymetric:yaw={},pitch={}", yaw, pitch);

    try {
      Tx tx(doc, "Save Camera Tag", ModifyDocument);
      doc::Tag* tag = new doc::Tag(fr, fr);
      tag->setName(tagName);
      tag->userData().setText(meta);
      tx(new cmd::AddTag(doc->sprite(), tag));
      tx.commit();
      doc->notifyGeneralUpdate();
      populateTagCombo();
      m_updatingCombo = true;
      m_tagCombo->setSelectedItemIndex(int(m_presets.size()) - 1);
      m_updatingCombo = false;
      if (auto* sb = StatusBar::instance())
        sb->showTip(1500, fmt::format("Saved tag '{}'", tagName));
    }
    catch (const std::exception& e) {
      Console::showException(e);
      easymetricReportError("saveCurrentAngleAsTag", e.what());
    }
  }

  // Initial depth map, William's spec (2026-11-04):
  // - Starts at mid-gray 128 = the main surface, opaque.
  // - Clipped by Base: depth only where Base is opaque;
  //   outside the base silhouette, depth stays transparent.
  // - Gradient derived from the OTHER views' depth, not a
  //   dome or the Base colour: for each face pixel, thickness
  //   along the face normal is read from the two orthogonal
  //   views (e.g. a Front pixel's Z thickness comes from the
  //   Top view's column and the Right view's row), averaged,
  //   then centred on the face's own mean thickness, so a flat
  //   face comes out flat 128 and a curved shape (a cylinder
  //   seen from the side) comes out as a curved gradient
  //   around 128, centre whiter, edges darker. The centring
  //   choice is Muse's: William specified "starts at 128"
  //   and "gradient from other views", not the exact formula.
  void generateEditableFaceDepthLayer()
  {
    Doc* doc = m_docView->document();
    if (!doc || !doc->sprite()) return;
    const doc::frame_t fr = (m_docView->editor() ? m_docView->editor()->frame() : doc::frame_t(0));
    if (generateDepthFromBaseOnDoc(doc, fr)) {
      if (auto* sb = StatusBar::instance())
        sb->showTip(2000, "Generated depth map from Base (128 surface, clipped by Base)");
    }
    m_lastHash = 0;
    pollAndRender(true);
  }

  bool extractSixSurfaces(Doc* doc, doc::frame_t frame, SixSurfaceData& out)
  {
    const bool useDepth = (m_useFaceDepthCheck && m_useFaceDepthCheck->isSelected());
    return app::extractSixSurfaces(doc, frame, out, useDepth, m_docView, &m_colLinked);
  }

  // DocUndoObserver: a transaction was committed. If it came from a paint
  // stroke in a panel (that panel is still mid-stroke when its tool loop
  // commits), remember the panel; the poll aligns it once the stroke is over,
  // outside the stroke's transaction, so the nudge is its own undo step.
  void onAddUndoState(DocUndo*) override
  {
    if (m_aligning || m_exited)
      return;
    Editor* ed = Editor::activeEditor();
    int face = -1;
    for (int f = 0; f < kFaceCount; ++f)
      if (m_editors[f] && m_editors[f] == ed)
        face = f;
    if (face < 0 || isPanelEditorIdle(ed))
      return;
    tools::Ink* ink = App::instance()->activeToolManager()->activeInk();
    if (!ink || !(ink->isPaint() || ink->isEraser()))
      return;
    m_pendingAlignFace = face;
  }

  void runPendingAlign()
  {
    const int face = m_pendingAlignFace;
    if (face < 0 || !m_editors[face] || anyPanelEditorBusy())
      return; // wait until the stroke is fully over
    m_pendingAlignFace = -1;
    auto& epref = Preferences::instance().easymetric;
    if (!epref.autoAlign())
      return;
    Doc* doc = m_docView ? m_docView->document() : nullptr;
    if (!doc || !doc->sprite())
      return;
    doc::frame_t frames[kFaceCount];
    for (int f = 0; f < kFaceCount; ++f)
      frames[f] = m_editors[f] ? m_editors[f]->frame() : doc::frame_t(0);
    const bool linked[3] = { m_colLinked[0], m_colLinked[1], m_colLinked[2] };
    const AlignPlan plan =
      planFaceAlign(doc, frames, linked, face, std::max(0, epref.autoAlignLimit()));
    bool applied = false;
    if (plan.du != 0 || plan.dv != 0) {
      m_aligning = true;
      try {
        applied = applyFaceShift(doc, frames[face], face, plan.du, plan.dv, linked,
                                 fmt::format("Align {} View", kFaceSpecs[face].displayName));
      }
      catch (const std::exception& e) {
        easymetricReportError("autoAlign", e.what());
      }
      m_aligning = false;
      if (applied)
        doc->notifyGeneralUpdate();
    }
    m_lastAlign = alignPlanJson(plan, applied);
    const std::string msg = alignPlanSummary(plan);
    if (!msg.empty())
      if (auto* sb = StatusBar::instance())
        sb->showTip(plan.notes.empty() ? 2500 : 5000, msg);
  }

  // Keeps the Fields W/H/D numbers equal to the actual object size (Front
  // width / Front height / Right width) after any resize, undo or reconcile.
  // A field the user is typing into is left alone.
  // Model size since 2026-11-18 (Object 1's box = every object's).
  // `force`: also when unchanged (Cancel in the Apply question).
  void refreshObjectSizeFields(bool force = false)
  {
    Doc* doc = m_docView ? m_docView->document() : nullptr;
    if (!doc || !doc->sprite() || !m_objSizeFields[0])
      return;
    int v[3];
    modelBox(doc->sprite(), v);
    // Only when the real size CHANGED: otherwise a value typed but not yet
    // applied would be reset as soon as the user moves to the next field.
    if (!force && v[0] == m_lastObjSize[0] && v[1] == m_lastObjSize[1] &&
        v[2] == m_lastObjSize[2])
      return;
    for (int a = 0; a < 3; ++a) {
      m_lastObjSize[a] = v[a];
      if (NumberField* e = m_objSizeFields[a])
        e->setValue(v[a]);
    }
  }

  void pollAndRender(bool force)
  {
    if (!isVisible()) return;
    // Objects: the helpers work on the focused object; a sheet picked in
    // the timeline focuses its object; the Fields object row follows.
    stopOrphanPlayback();
    applyFocusScope();
    followTimelineObject();
    refreshObjectUi();
    if (m_refitPanels > 0 && !anyPanelEditorBusy()) {
      --m_refitPanels;
      for (int f = 0; f < kFaceCount; ++f)
        if (m_editors[f] && m_panes[f]->isVisible()) {
          // After New object the canvas grew; the view's scrollable area is
          // stale until its next layout and would clamp the fit's scroll.
          // Only here (a focus switch), never on every fit: that disturbed
          // the panels during studio start-up and strokes.
          if (ui::View* v = ui::View::getView(m_editors[f]))
            v->updateView();
          m_editors[f]->fitClipSpriteBounds();
        }
    }
    syncDepthToolFromApp();
    runPendingAlign();
    refreshObjectSizeFields();
    // Wheel-zoom guarantee: a panel editor whose idle state has
    // fallen back to plain StandbyState (for any reason — a state
    // pop, a tool change, an editor recreated by a caller) would
    // scroll the View on wheel instead of zooming, which is the
    // reported "scroll displaces the canvas" bug. Re-apply the
    // wheel-zoom state while the editor is idle and uncaptured;
    // this is a no-op once the state is already the wheel-zoom
    // state, and it never runs mid-stroke.
    for (int f = 0; f < kFaceCount; ++f) {
      Editor* ed = m_editors[f];
      if (ed && !ed->hasCapture() && ed->getState() &&
          typeid(*ed->getState()) == typeid(StandbyState)) {
        ed->setState(newPanelState());
      }
    }
    // Grid overlay decorators (G2): the editor's state
    // re-installs its own decorator on every state entry, so
    // re-install the grid decorator whenever it is missing —
    // but only while the editor is idle, never under a
    // selection-box or other temporary state (those install
    // their own decorators).
    for (int f = 0; f < kFaceCount; ++f) {
      Editor* ed = m_editors[f];
      if (ed && m_gridDecorators[f] && isPanelEditorIdle(ed) &&
          ed->decorator() != m_gridDecorators[f]) {
        ed->setDecorator(m_gridDecorators[f]);
        ed->invalidate();
      }
    }
    Doc* doc = m_docView->document();
    const doc::frame_t frame = (m_docView->editor() ? m_docView->editor()->frame() : doc::frame_t(0));

    // Canvas <-> UV reconciliation, and per-face editing
    // confinement (see below). Both run before extraction so
    // the Preview samples the reconciled UV layout.
    if (doc && doc->sprite()) {
      doc::Sprite* spr = doc->sprite();
      SharedSurfaceLayers shared = findSharedLayers(spr);
      // Hidden-Base tip (2026-11-07, William's grey-panels report):
      // when Surface_Base is hidden the panels show the document
      // composite, i.e. the depth map only (flat grey 128 with the
      // depth dabs), while the Preview stays coloured because it
      // samples the Base/Depth images directly. That looks like a
      // rendering bug, so say so once per hide in the status bar.
      // Edge-triggered: re-arms when Base becomes visible again.
      // Never auto-shows Base: viewing the depth map alone is a
      // legitimate workflow.
      if (shared.baseLayer && !shared.baseLayer->isVisible()) {
        if (!m_baseHiddenTipShown) {
          if (auto* sb = StatusBar::instance())
            sb->showTip(4000, "Surface_Base is hidden - panels show the depth map only. "
                               "Click its eye in the timeline to show it again.");
          m_baseHiddenTipShown = true;
        }
      }
      else {
        m_baseHiddenTipShown = false;
      }
      if (shared.baseLayer || shared.textureLayer) {
        // Canvas resize -> UV mapping: if the canvas was
        // resized by hand (sprite size no longer equals the
        // exact sheet size for the current face sizes), scale
        // each face size by the same ratio and rebuild the
        // sheets, so the canvas ends up exactly the sum of
        // the new UV rects (rounding may move it a few pixels
        // from the hand-resized size). If the canvas already
        // matches the exact sheet size, nothing happens here,
        // so a face resize via the dimension controls is not
        // double-applied.
        const gfx::Size exact = atlasSize(spr);
        // Never rebuild the sheets (and so never shrink the
        // canvas) while a panel splitter is being dragged:
        // a UI layout drag must not mutate the document.
        // Before this guard, a timer poll firing mid-drag
        // could rebuild the UV sheets from a transient
        // sprite size, so resizing a panel made the canvas
        // itself smaller, and repeated polls kept shrinking
        // it until the studio was left in a broken state.
        // Bug 2: also never rebuild while any panel editor
        // is busy (drawing/moving/selecting). A document
        // change mid-stroke must not trigger a sheet rebuild
        // or a panel fit, which is what made panels jump
        // while drawing.
        if (m_dragSplitter == 0 && !anyPanelEditorBusy() && exact.w > 0 &&
            (spr->width() != exact.w || spr->height() != exact.h)) {
          int oldW[kFaceCount], oldH[kFaceCount];
          sharedFaceSizes(spr, oldW, oldH);
          // With several objects a hand canvas resize has no single
          // meaning (whose cells would it scale?): the atlas is restored.
          const bool restore = hasObjectGroups(spr);
          const double sx = (!restore && exact.w > 0 && spr->width() != exact.w)
                              ? double(spr->width()) / double(exact.w) : 1.0;
          const double sy = (!restore && exact.h > 0 && spr->height() != exact.h)
                              ? double(spr->height()) / double(exact.h) : 1.0;
          int newW[kFaceCount], newH[kFaceCount];
          for (int f = 0; f < kFaceCount; ++f) {
            newW[f] = std::clamp(int(std::round(oldW[f] * sx)), 1, 256);
            newH[f] = std::clamp(int(std::round(oldH[f] * sy)), 1, 256);
          }
          rebuildSharedSheets(doc, newW, newH);
          initLayersAndFraming();
          force = true;
        }

        // Per-face independence: when the active panel changes
        // to a different surface (and that panel is in its
        // standby state, not mid-stroke/mid-move), set the
        // document selection to that face's UV rect, without
        // adding an undo step. Brush painting and a Move are
        // then confined to that face's pixels: moving Top's
        // content does not move Front's, Back's, etc., because
        // the other faces' pixels are outside the selection.
        // It is set only on a face change, so a user selection
        // inside a face (e.g. a sub-region to move) is not
        // overwritten every poll. Limitation, stated honestly:
        // a sub-selection dragged far enough to cross into a
        // neighbouring UV rect will still land in that rect's
        // sampled area, because the sheet is one image; what
        // is guaranteed is that the other faces' pixels do
        // not move with it. On studio exit the original
        // selection is restored (destructor).
        Editor* curEd = m_docView->editor();
        int activeFace = -1;
        for (int f = 0; f < kFaceCount; ++f)
          if (curEd == m_editors[f]) activeFace = f;
        if (activeFace != m_lastActiveFace) {
          // Bug 1: the old guard here was
          // dynamic_cast<StandbyState*>, which is true for
          // every editor state (Drawing, MovingPixels,
          // MovingCel and SelectBox all inherit StandbyState),
          // so the mask was overwritten even mid-stroke and
          // mid-transform, losing the user's sub-selection.
          // isPanelEditorIdle() only matches the true idle
          // states, and a non-empty mask already inside the
          // new face's rect (a sub-selection the user made,
          // then clicked away and back) is preserved, not
          // replaced with the whole face rect.
          if (activeFace >= 0 && curEd && isPanelEditorIdle(curEd)) {
            const gfx::Rect faceRect = sharedFaceRect(spr, activeFace);
            bool keepExisting = false;
            if (doc->mask() && !doc->mask()->isEmpty()) {
              const gfx::Rect m = doc->mask()->bounds();
              keepExisting = (m.x >= faceRect.x && m.y >= faceRect.y &&
                              m.x + m.w <= faceRect.x + faceRect.w &&
                              m.y + m.h <= faceRect.y + faceRect.h);
            }
            if (!keepExisting) {
              doc::Mask mask;
              mask.replace(faceRect);
              doc->setMask(&mask);
              doc->setMaskVisible(true);
              doc->notifySelectionChanged();
            }
            m_lastActiveFace = activeFace;
            // G3: follow the active panel — its stored SX/SY and
            // its face-centre axis become the document symmetry,
            // so the next stroke in this panel mirrors around
            // this face's centre.
            applyPanelSymmetry(activeFace);
          }
          else if (activeFace < 0) {
            m_lastActiveFace = -1;
            // G3: no panel is active — give the document back its
            // entry symmetry until a panel is active again.
            restoreDocSymmetry();
          }
        }
        // G3: if the active face's rect changed (a Fields W/H/D
        // edit, a sprite resize), re-apply its symmetry so the
        // axis tracks the face centre. The cache is updated by
        // applyPanelSymmetry; only the active face is checked,
        // and only while its editor is idle.
        if (m_lastActiveFace >= 0 && curEd && isPanelEditorIdle(curEd)) {
          const gfx::Rect symRect = sharedFaceRect(spr, m_lastActiveFace);
          if (symRect != m_lastSymFaceRect)
            applyPanelSymmetry(m_lastActiveFace);
        }
      }
    }

    SixSurfaceData data;
    if (!extractSixSurfaces(doc, frame, data)) {
      data.init(24);
      for (int f = 0; f < kFaceCount; ++f)
        for (int v = 0; v < 24; ++v)
          for (int u = 0; u < 24; ++u)
            data.pixels[f][v * 24 + u] = starterSurfacePixel(f, u, v, 24, 24);
    }

    uint64_t baseHash = 14695981039346656037ULL;
    bool anyBase = false;
    for (int f = 0; f < kFaceCount; ++f) {
      if (data.hasBaseLayer[f]) {
        anyBase = true;
        for (uint8_t a : data.baseAlpha[f]) {
          baseHash ^= uint64_t(a > 16 ? 1 : 0);
          baseHash *= 1099511628211ULL;
        }
      }
    }
    if (anyBase) {
      if (m_lastBaseSilhouetteHash != 0 && baseHash != m_lastBaseSilhouetteHash) {
        if (auto* sb = StatusBar::instance())
          sb->showTip(1000, "Views clamped to Base sheet silhouette");
      }
      m_lastBaseSilhouetteHash = baseHash;
    }

    // Check if any face slice dimensions changed and Link Dims is active.
    // Bug 2: never propagate/fit while a panel editor is busy;
    // a dimension change detected mid-stroke must wait for the
    // next idle poll, or the panel fit jumps under the pen.
    if (m_linkDimensionsCheck && m_linkDimensionsCheck->isSelected() && !anyPanelEditorBusy()) {
      int changedFace = -1;
      for (int f = 0; f < kFaceCount; ++f) {
        if (m_lastFaceW[f] > 0 && (data.w[f] != m_lastFaceW[f] || data.h[f] != m_lastFaceH[f])) {
          changedFace = f;
          break;
        }
      }
      if (changedFace >= 0) {
        propagateLinkedDimensions(doc, data, changedFace, data.w[changedFace], data.h[changedFace]);
        initLayersAndFraming();
        for (int f = 0; f < kFaceCount; ++f) {
          m_lastFaceW[f] = data.w[f];
          m_lastFaceH[f] = data.h[f];
        }
        force = true;
      }
      else {
        for (int f = 0; f < kFaceCount; ++f) {
          m_lastFaceW[f] = data.w[f];
          m_lastFaceH[f] = data.h[f];
        }
      }
    }
    else {
      for (int f = 0; f < kFaceCount; ++f) {
        m_lastFaceW[f] = data.w[f];
        m_lastFaceH[f] = data.h[f];
      }
    }

    const int yawDeg = m_yawSlider->getValue();
    const int pitchDeg = m_pitchSlider->getValue();
    const int pivX = m_pivXSlider->getValue();
    const int pivY = m_pivYSlider->getValue();
    const int pivZ = m_pivZSlider->getValue();
    const int renderStyle = std::max(0, m_renderTypeCombo->getSelectedItemIndex());
    const bool addOutline = m_strokeCheck->isSelected();
    const bool allowCutouts = m_cutoutCheck->isSelected();
    const bool tintCutouts = Preferences::instance().easymetric.showCutouts();
    const bool showDiff = m_mapDiffuseCheck->isSelected();
    const bool showNorm = m_mapNormalCheck->isSelected();
    const bool showDept = m_mapDepthCheck->isSelected();

    uint64_t h = 14695981039346656037ULL;
    auto mix = [&h](uint64_t v) {
      h ^= v;
      h *= 1099511628211ULL;
    };
    mix(uint64_t(yawDeg + 360));
    mix(uint64_t(pitchDeg + 360));
    mix(uint64_t(pivX));
    mix(uint64_t(pivY));
    mix(uint64_t(pivZ));
    mix(uint64_t(renderStyle));
    mix(uint64_t(addOutline ? 1 : 0));
    mix(uint64_t(allowCutouts ? 1 : 0));
    mix(uint64_t(tintCutouts ? 1 : 0));
    mix(uint64_t(showDiff ? 1 : 0));
    mix(uint64_t(showNorm ? 1 : 0));
    mix(uint64_t(showDept ? 1 : 0));

    auto mixData = [&mix](const SixSurfaceData& d) {
      for (int f = 0; f < kFaceCount; ++f) {
        mix(uint64_t(d.w[f]));
        mix(uint64_t(d.h[f]));
        const uint32_t* p = d.pixels[f].data();
        const size_t n = d.pixels[f].size();
        for (size_t i = 0; i < n; ++i)
          mix(p[i]);
        if (d.hasCustomDepthLayer) {
          const uint8_t* dp = d.depthPixels[f].data();
          for (size_t i = 0; i < n; ++i)
            mix(dp[i]);
        }
        if (d.hasBaseLayer[f]) {
          const uint8_t* ba = d.baseAlpha[f].data();
          for (size_t i = 0; i < n; ++i)
            mix(uint64_t(ba[i]));
        }
      }
    };
    mixData(data);

    // Objects (Stage 3): with object groups the preview shows every
    // visible object, each placed at its offset; any object's pixels,
    // place, rank or visibility re-render it.
    const bool composite = doc && doc->sprite() && hasObjectGroups(doc->sprite());
    CompositeInput comp;
    if (composite) {
      const bool useDepth = (m_useFaceDepthCheck && m_useFaceDepthCheck->isSelected());
      buildCompositeInput(
        doc, frame, useDepth, m_docView, m_focusKey,
        [this](const std::string& k) -> const std::array<bool, 3>* {
          if (k == m_focusKey)
            return &m_colLinked;
          auto it = m_linkedByObject.find(k);
          return it != m_linkedByObject.end() ? &it->second : nullptr;
        },
        comp);
      mix(uint64_t(comp.objects.size()));
      for (size_t i = 0; i < comp.objects.size(); ++i) {
        const PreviewObject& p = comp.objects[i];
        mix(uint64_t(std::hash<std::string>()(comp.keys[i])));
        for (float c : { p.cx, p.cy, p.cz }) {
          uint32_t bits = 0;
          std::memcpy(&bits, &c, sizeof(bits));
          mix(bits);
        }
        mix(uint64_t(p.priority));
        if (comp.keys[i] != m_focusKey)
          mixData(*p.data);
      }
      mix(uint64_t(m_showOthers ? 1 : 0));
    }

    // Effective grids (committed, or the live draft while a
    // pop-up is open for that face) — mixed into the render
    // hash so a grid change re-renders, and passed to
    // render3DModel below.
    std::array<PanelGridSettings, kFaceCount> effGrids;
    for (int f = 0; f < kFaceCount; ++f) {
      effGrids[f] = gridForFace(f);
      mix(uint64_t(effGrids[f].on ? 1 : 0));
      mix(uint64_t(effGrids[f].countH));
      mix(uint64_t(effGrids[f].spacingH));
      mix(uint64_t(effGrids[f].countV));
      mix(uint64_t(effGrids[f].spacingV));
    }

    if (!force && h == m_lastHash)
      return;
    if (m_ghostOn) {
      const GhostData before = m_ghost;
      recomputeGhost();
      bool changed = (before.any != m_ghost.any);
      for (int d = 0; d < kFaceCount && !changed; ++d)
        for (int s = 0; s < kFaceCount && !changed; ++s)
          changed = (before.col[d][s] != m_ghost.col[d][s] || before.row[d][s] != m_ghost.row[d][s]);
      if (changed)
        invalidateAllPanels();
    }
    // Stroke throttle (2026-11-09, "lag while brushing"): while a stroke is in
    // progress the 3D preview is re-rendered at most every
    // max(120 ms, 3 x the last render's duration). The depth maps rebuild the
    // whole sculpted volume (cost grows with the cube of the face size), and
    // doing that on every 33 ms tick starved the mouse events (measured: a 256px
    // depth stroke took 4x longer than the same stroke with a normal brush). The
    // hash stays unchanged here, so the preview catches up the moment the
    // throttle opens or the stroke ends (endDepthStroke / mouse up).
    const double nowMs = double(base::current_tick());
    if (!force && (m_depthStrokeActive || anyPanelEditorBusy()) &&
        nowMs - m_lastRenderMs < std::max(120.0, 3.0 * m_lastRenderDurMs))
      return;
    m_lastHash = h;

    render3DModel(data, yawDeg, pitchDeg, pivX, pivY, pivZ,
                  renderStyle, addOutline, allowCutouts,
                  showDiff, showNorm, showDept, &effGrids, tintCutouts,
                  composite ? &comp.objects : nullptr);
    if (composite) {
      m_lastObjHitKeys = comp.keys;
      rebuildOverlay(data, comp);
      invalidateAllPanels();
    }
    else {
      m_lastObjHitKeys.clear();
      m_lastObjHitPx.clear();
      m_lastOverlayPx = 0;
      for (int f = 0; f < kFaceCount; ++f)
        m_overlay[f].clear();
    }
    m_lastRenderMs = double(base::current_tick());
    m_lastRenderDurMs = m_lastRenderMs - nowMs;

    if (m_liveBakeCheck->isSelected() && doc && doc->sprite())
      bakeToActiveDocLayers();
  }

public:
  bool isPreviewFloating() const { return m_previewFloating; }

  // ---- Ghost hints (see computeGhost) ----
  bool ghostOn() const { return m_ghostOn; }

  void setGhostOn(bool on)
  {
    m_ghostOn = on;
    if (m_ghostHeaderBtn)
      m_ghostHeaderBtn->setOn(on);
    if (m_ghostCheck)
      m_ghostCheck->setSelected(on);
    if (on)
      recomputeGhost();
    invalidateAllPanels();
    if (auto* sb = StatusBar::instance())
      sb->showTip(2500, on ? "Ghost hints ON: faint bands mark where another surface still "
                             "has no content for a shape you drew."
                           : "Ghost hints OFF");
  }

  void recomputeGhost()
  {
    Doc* doc = m_docView ? m_docView->document() : nullptr;
    doc::frame_t frames[kFaceCount];
    std::vector<int> edgesU[kFaceCount], edgesV[kFaceCount];
    for (int f = 0; f < kFaceCount; ++f) {
      frames[f] = m_editors[f] ? m_editors[f]->frame() : doc::frame_t(0);
      // Cell mode: when this face's panel grid is on, hints work per grid cell.
      const PanelGridSettings gs = gridForFace(f);
      const gfx::Rect rc = gridFaceRect(f);
      std::vector<float> lines;
      if (gs.on && !rc.isEmpty()) {
        if (gridLinePositions(rc.w, gs.countV, gs.spacingV, lines) && gs.countV > 1) {
          edgesU[f].push_back(0);
          for (float l : lines)
            edgesU[f].push_back(int(std::lround(l)));
          edgesU[f].push_back(rc.w);
        }
        if (gridLinePositions(rc.h, gs.countH, gs.spacingH, lines) && gs.countH > 1) {
          edgesV[f].push_back(0);
          for (float l : lines)
            edgesV[f].push_back(int(std::lround(l)));
          edgesV[f].push_back(rc.h);
        }
      }
    }
    const bool linked[3] = { m_colLinked[0], m_colLinked[1], m_colLinked[2] };
    m_ghost = computeGhost(doc, frames, linked, edgesU, edgesV);
  }

  void invalidateAllPanels()
  {
    for (int f = 0; f < kFaceCount; ++f)
      if (m_editors[f])
        m_editors[f]->invalidate();
  }

  // Draws this panel's missing-content bands (called from the grid decorator's
  // post-render). One low-opacity band per demanding surface, in that
  // surface's colour, so it is clear WHICH surface asks for the content.
  // ---- Objects (2026-11-17) ----

  const std::string& focusedObjectKey() const { return m_focusKey; }

  // Re-asserts the studio's focused object as the helpers' scope (a headless
  // command may have changed it). A focused object that no longer exists
  // (undo of New Object, deleted from the studio, the timeline or a script)
  // falls back to the object before it in the list (Arin, 2026-11-18), and
  // to Object 1 when it was the first.
  void applyFocusScope()
  {
    Doc* doc = m_docView ? m_docView->document() : nullptr;
    doc::Sprite* spr = doc ? doc->sprite() : nullptr;
    if (!spr)
      return;
    if (!m_focusKey.empty() && !findObjectGroup(spr, m_focusKey))
      switchFocus(previousObjectKey(spr, m_focusKey));
    g_objectScopeSprite = spr->id();
    g_objectScopeKey = m_focusKey;
  }

  // Stops playback in every panel (Arin, 2026-11-18: "the animation isn't
  // stopping"). Called before the studio rearranges its panels (object
  // switch, new/delete object, model resize). A playback left running in
  // a panel that is no longer the active one cannot be stopped with Enter:
  // Play/Stop acts on the active panel and would start a second playback.
  void stopAllPlayback()
  {
    for (Editor* ed : m_editors)
      if (ed && ed->isPlaying())
        ed->stop();
    if (Editor* ed = m_docView ? m_docView->editor() : nullptr)
      if (ed->isPlaying())
        ed->stop();
  }

  // Poll: a panel still playing while ANOTHER panel is the active one is
  // an orphan playback (see stopAllPlayback); stop it so Enter on the
  // active panel is a plain Play/Stop again.
  void stopOrphanPlayback()
  {
    Editor* active = m_docView ? m_docView->editor() : nullptr;
    for (Editor* ed : m_editors)
      if (ed && ed != active && ed->isPlaying())
        ed->stop();
  }

  // The nearest object before `key` in the last object list the studio
  // showed (= creation order) that still exists; "" = Object 1.
  std::string previousObjectKey(doc::Sprite* spr, const std::string& key) const
  {
    auto it = std::find(m_objectComboKeys.begin(), m_objectComboKeys.end(), key);
    while (it != m_objectComboKeys.begin()) {
      --it;
      if (it->empty() || findObjectGroup(spr, *it))
        return *it;
    }
    return std::string();
  }

  void deleteFocusedObject()
  {
    Doc* doc = m_docView ? m_docView->document() : nullptr;
    doc::Sprite* spr = doc ? doc->sprite() : nullptr;
    if (!spr || m_focusKey.empty() || !findObjectGroup(spr, m_focusKey))
      return;
    if (anyPanelEditorBusy())
      return;
    stopAllPlayback();
    const std::string key = m_focusKey;
    std::string name = key;
    for (const ObjectInfo& i : resolveObjects(spr))
      if (i.key == key)
        name = i.name;
    const std::string prev = previousObjectKey(spr, key);
    std::string prevName = "Object 1";
    for (const ObjectInfo& i : resolveObjects(spr))
      if (i.key == prev)
        prevName = i.name;
    if (ui::Alert::show(fmt::format(
          "Delete object<<Delete \"{}\" and everything drawn in it (all frames)?"
          "<<You will be on \"{}\" afterwards. Undo (Ctrl+Z) brings it back."
          "||&Delete||&Cancel",
          name, prevName)) != 1)
      return;
    // Leave the object first, so no panel still points at its sheets.
    switchFocus(prev);
    if (!deleteObjectOnDoc(doc, key)) {
      easymetricReportError("deleteObject", "could not delete " + name);
      return;
    }
    switchFocus(prev);
    initLayersAndFraming();
    m_refitPanels = 2;
    m_lastHash = 0;
    pollAndRender(true);
    if (auto* sb = StatusBar::instance())
      sb->showTip(2500, fmt::format("Deleted \"{}\"; now on \"{}\" (undo to restore)", name,
                                    prevName));
  }

  void focusObject(const std::string& key)
  {
    switchFocus(key);
    m_lastHash = 0;
    pollAndRender(true);
  }

  // Picking another object's sheet in the timeline focuses that object.
  void followTimelineObject()
  {
    Editor* ed = m_docView ? m_docView->editor() : nullptr;
    doc::Layer* al = ed ? ed->layer() : nullptr;
    if (!al || al->name().rfind("Surface_", 0) != 0 || anyPanelEditorBusy())
      return;
    std::string key;
    for (doc::Layer* p = al->parent(); p; p = p->parent()) {
      ObjectMeta m;
      if (readObjectMeta(p, m)) {
        key = m.key;
        break;
      }
    }
    if (key != m_focusKey)
      switchFocus(key);
  }

  void switchFocus(const std::string& key)
  {
    Doc* doc = m_docView ? m_docView->document() : nullptr;
    doc::Sprite* spr = doc ? doc->sprite() : nullptr;
    if (!spr)
      return;
    stopAllPlayback();
    // The sheet the user works on (Base / Texture / Depth) carries over.
    int kind = 0;
    {
      EasymetricObjectScope old(spr, m_focusKey);
      const SharedSurfaceLayers cur = findSharedLayers(spr);
      doc::Layer* a = m_docView->editor() ? m_docView->editor()->layer() : nullptr;
      if (a && a == cur.textureLayer)
        kind = 1;
      else if (a && a == cur.depthLayer)
        kind = 2;
      else if (a && a->name() == "Surface_Texture")
        kind = 1; // picked in the timeline: same kind in the new object
      else if (a && a->name() == "Surface_DEPTH")
        kind = 2;
    }
    m_linkedByObject[m_focusKey] = m_colLinked;
    m_focusKey = (key.empty() || findObjectGroup(spr, key)) ? key : std::string();
    g_objectScopeSprite = spr->id();
    g_objectScopeKey = m_focusKey;
    auto it = m_linkedByObject.find(m_focusKey);
    const std::array<bool, 3> linked =
      it != m_linkedByObject.end() ? it->second : std::array<bool, 3>{ false, false, false };

    const SharedSurfaceLayers nw = findSharedLayers(spr);
    doc::LayerImage* lyr = kind == 1 ? nw.textureLayer : kind == 2 ? nw.depthLayer : nw.baseLayer;
    if (!lyr)
      lyr = nw.baseLayer ? nw.baseLayer : nw.textureLayer;
    for (int f = 0; f < kFaceCount; ++f)
      if (m_editors[f] && lyr)
        m_editors[f]->setLayer(lyr);
    // Fresh per-object bookkeeping: the new object's sizes are not a
    // "change" of the old one's (that would resize the new object).
    m_lastActiveFace = -1;
    for (int f = 0; f < kFaceCount; ++f) {
      m_lastFaceW[f] = 0;
      m_lastFaceH[f] = 0;
    }
    for (int a = 0; a < 3; ++a)
      m_lastObjSize[a] = -1;
    m_pendingAlignFace = -1;
    applyColumnLinks(linked);
    initLayersAndFraming();
    // The canvas may have just grown (New object): the editors learn the
    // new sprite size later, so fit the panels again on the next idle poll.
    m_refitPanels = 2;
    m_objectListSig.clear(); // refresh the Fields object row
    for (int f = 0; f < kFaceCount; ++f)
      m_overlay[f].clear();
    std::string name = "Object 1";
    if (doc::LayerGroup* g = findObjectGroup(spr, m_focusKey))
      name = objectDisplayName(g);
    if (auto* sb = StatusBar::instance())
      sb->showTip(1500, "Editing " + name);
  }

  // Linked columns without touching the document (unlike toggleColumnLink,
  // which copies the top face into the bottom one when linking).
  void applyColumnLinks(const std::array<bool, 3>& linked)
  {
    for (int c = 0; c < 3; ++c) {
      m_colLinked[c] = linked[c];
      if (m_linkButtons[c])
        m_linkButtons[c]->setLinked(linked[c]);
      const int top = c * 2, btm = c * 2 + 1;
      if (!m_headers[top] || !m_headers[btm])
        continue;
      if (linked[c]) {
        static const char* titles[3] = { "Top / Bottom", "Front / Back", "Right / Left" };
        m_headers[top]->setTitle(titles[c]);
      }
      else {
        m_headers[top]->setTitle(kFaceSpecs[top].displayName);
        m_headers[btm]->setTitle(kFaceSpecs[btm].displayName);
      }
    }
    layout();
    for (int f = 0; f < kFaceCount; ++f)
      if (m_editors[f] && m_panes[f]->isVisible())
        m_editors[f]->fitClipSpriteBounds();
    invalidate();
  }

  // Fields object row: the list, the focused object's place and rank.
  void refreshObjectUi()
  {
    Doc* doc = m_docView ? m_docView->document() : nullptr;
    doc::Sprite* spr = doc ? doc->sprite() : nullptr;
    if (!spr || !m_objectCombo)
      return;
    const std::vector<ObjectInfo> infos = resolveObjects(spr);
    std::string sig = m_focusKey + "|";
    for (const ObjectInfo& i : infos)
      sig += fmt::format("{}:{}:{},{},{}:{}:{};", i.key, i.name, i.meta.o[0], i.meta.o[1],
                         i.meta.o[2], i.meta.rank, i.visible ? 1 : 0);
    if (sig == m_objectListSig)
      return;
    m_objectListSig = sig;
    m_updatingObjectUi = true;
    m_objectCombo->deleteAllItems();
    m_objectComboKeys.clear();
    for (const ObjectInfo& i : infos) {
      m_objectCombo->addItem(i.visible ? i.name : i.name + " (hidden)");
      m_objectComboKeys.push_back(i.key);
      if (i.key == m_focusKey) {
        m_objectCombo->setSelectedItemIndex(int(m_objectComboKeys.size()) - 1);
        if (m_objRankCombo)
          m_objRankCombo->setSelectedItemIndex(std::clamp(i.meta.rank + 1, 0, 10));
      }
    }
    m_updatingObjectUi = false;
    if (m_deleteObjBtn) // Object 1 cannot be deleted
      m_deleteObjBtn->setEnabled(!m_focusKey.empty());
  }

  // Faded other objects (Arin: "faded in panels, normal in preview"). For
  // each pixel of a focused face that is empty, the same view of every other
  // visible object is sampled at the same MODEL position (offsets + the
  // ghostAxesOfFace table), frontmost priority first. Display only.
  void rebuildOverlay(const SixSurfaceData& focused, const CompositeInput& comp)
  {
    m_lastOverlayPx = 0;
    for (int f = 0; f < kFaceCount; ++f) {
      m_overlay[f].clear();
      m_overlaySize[f] = gfx::Size(0, 0);
    }
    Doc* doc = m_docView ? m_docView->document() : nullptr;
    doc::Sprite* spr = doc ? doc->sprite() : nullptr;
    if (!spr || !m_showOthers || comp.objects.size() == 0)
      return;
    const std::vector<ObjectInfo> infos = resolveObjects(spr);
    const ObjectInfo* fInfo = nullptr;
    for (const ObjectInfo& i : infos)
      if (i.key == m_focusKey)
        fInfo = &i;
    if (!fInfo || infos.empty())
      return;
    const int rootH = infos[0].box[1];
    // Model frame: X from the root's left, Y DOWN from the root's top, Z
    // from the root's back (the ghost-axes fractions are Y-down too).
    auto origin = [rootH](const ObjectInfo& o, float org[3]) {
      org[0] = float(o.meta.o[0]);
      org[1] = float(rootH - o.meta.o[1] - o.box[1]);
      org[2] = float(o.meta.o[2]);
    };
    struct Other {
      const SixSurfaceData* data;
      float org[3];
      int box[3];
      int priority;
    };
    std::vector<Other> others;
    for (size_t k = 0; k < comp.keys.size(); ++k) {
      if (comp.keys[k] == m_focusKey)
        continue;
      for (const ObjectInfo& i : infos)
        if (i.key == comp.keys[k]) {
          Other o;
          o.data = comp.objects[k].data;
          origin(i, o.org);
          for (int a = 0; a < 3; ++a)
            o.box[a] = std::max(1, i.box[a]);
          o.priority = i.priority;
          others.push_back(o);
        }
    }
    if (others.empty())
      return;
    std::sort(others.begin(), others.end(),
              [](const Other& a, const Other& b) { return a.priority > b.priority; });
    float forg[3];
    origin(*fInfo, forg);
    for (int f = 0; f < kFaceCount; ++f) {
      const int fw = focused.w[f], fh = focused.h[f];
      if (fw <= 0 || fh <= 0 || int(focused.pixels[f].size()) < fw * fh)
        continue;
      GhostAxis ax[2];
      ghostAxesOfFace(f, ax);
      std::vector<uint32_t> out(size_t(fw) * fh, 0);
      bool any = false;
      for (int v = 0; v < fh; ++v) {
        for (int u = 0; u < fw; ++u) {
          if (doc::rgba_geta(focused.pixels[f][size_t(v) * fw + u]) > 16)
            continue;
          float m[3] = { 0, 0, 0 };
          for (const GhostAxis& a : ax) {
            float c = a.coord == 0 ? (u + 0.5f) / fw : (v + 0.5f) / fh;
            if (a.flip)
              c = 1.0f - c;
            m[a.axis] = forg[a.axis] + c * std::max(1, fInfo->box[a.axis]);
          }
          for (const Other& o : others) {
            const int ow = o.data->w[f], oh = o.data->h[f];
            int pu = -1, pv = -1;
            bool inside = true;
            for (const GhostAxis& a : ax) {
              float c = (m[a.axis] - o.org[a.axis]) / float(o.box[a.axis]);
              if (c < 0.0f || c >= 1.0f) {
                inside = false;
                break;
              }
              if (a.flip)
                c = 1.0f - c;
              if (a.coord == 0)
                pu = std::clamp(int(c * ow), 0, ow - 1);
              else
                pv = std::clamp(int(c * oh), 0, oh - 1);
            }
            if (!inside || pu < 0 || pv < 0)
              continue;
            const uint32_t col = o.data->sampleSymmetric(f, pu, pv);
            if (doc::rgba_geta(col) > 16) {
              out[size_t(v) * fw + u] = col;
              any = true;
              ++m_lastOverlayPx;
              break;
            }
          }
        }
      }
      if (any) {
        m_overlay[f] = std::move(out);
        m_overlaySize[f] = gfx::Size(fw, fh);
      }
    }
  }

  void drawOtherObjects(EditorPostRender* render, int face) const
  {
    if (!m_showOthers || face < 0 || face >= kFaceCount || m_overlay[face].empty())
      return;
    const gfx::Rect rc = gridFaceRect(face);
    const int fw = m_overlaySize[face].w, fh = m_overlaySize[face].h;
    if (rc.isEmpty() || rc.w != fw || rc.h != fh)
      return; // stale (the face was resized); rebuilt with the next preview
    const auto& px = m_overlay[face];
    for (int v = 0; v < fh; ++v) {
      for (int u = 0; u < fw;) {
        const uint32_t c = px[size_t(v) * fw + u];
        if (!c) {
          ++u;
          continue;
        }
        int e = u + 1;
        while (e < fw && px[size_t(v) * fw + e] == c)
          ++e;
        render->fillRect(gfx::rgba(doc::rgba_getr(c), doc::rgba_getg(c), doc::rgba_getb(c), 90),
                         gfx::Rect(rc.x + u, rc.y + v, e - u, 1));
        u = e;
      }
    }
  }

  // Guided "New object" (Arin, 2026-11-17: block creation was not guided
  // enough). Name, size as seen from the Front, where it goes relative to
  // the focused object, and a live description of what will be made.
  // New object (shared model size, 2026-11-18): the object gets the model
  // size, like every object; the dialog only asks for a name and an
  // optional starting block placed next to what the focused object has
  // painted. The block goes on the current frame only (Arin).
  void openNewObjectDialog()
  {
    Doc* doc = m_docView ? m_docView->document() : nullptr;
    doc::Sprite* spr = doc ? doc->sprite() : nullptr;
    if (!spr)
      return;
    stopAllPlayback();
    const std::vector<ObjectInfo> infos = resolveObjects(spr);
    if (infos.empty())
      return;
    std::string refName = infos[0].name;
    for (const ObjectInfo& i : infos)
      if (i.key == m_focusKey)
        refName = i.name;
    const std::string refKey = m_focusKey;
    const doc::frame_t frame = m_editors[0] ? m_editors[0]->frame() : doc::frame_t(0);
    int S[3];
    modelBox(spr, S);

    const int s = ui::guiscale();
    ui::Window win(ui::Window::WithTitleBar, "New object");
    auto* box = new ui::VBox;
    box->setBorder(gfx::Border(4 * s));
    box->setChildSpacing(3 * s);
    win.addChild(box);

    auto* nameRow = new ui::HBox;
    nameRow->setChildSpacing(4 * s);
    auto* nameE = new ui::Entry(32, "");
    nameE->setText(fmt::format("Object {}", infos.size() + 1));
    nameE->setExpansive(true);
    nameRow->addChild(new ui::Label("Name"));
    nameRow->addChild(nameE);
    box->addChild(nameRow);

    box->addChild(new ui::Label(fmt::format(
      "Uses the model size {} x {} x {}, like every object.", S[0], S[1], S[2])));
    box->addChild(new ui::Label("Draw it where it belongs; the other objects show faded."));

    auto* fillCheck = new ui::CheckBox("Start with a block (flat depth)");
    fillCheck->setSelected(false);
    box->addChild(fillCheck);

    auto* blockBox = new ui::VBox;
    blockBox->setChildSpacing(3 * s);
    auto* sizeRow = new ui::HBox;
    sizeRow->setChildSpacing(4 * s);
    NumberField* sizeF[3];
    const char* sizeNames[3] = { "Block W", "H", "D" };
    for (int a = 0; a < 3; ++a) {
      sizeF[a] = new NumberField(1, 256, std::max(1, S[a] / 4));
      sizeRow->addChild(new ui::Label(sizeNames[a]));
      sizeRow->addChild(sizeF[a]);
    }
    blockBox->addChild(sizeRow);
    blockBox->addChild(new ui::Label("W = left-right, H = up-down, D = front-back."));

    auto* placeRow = new ui::HBox;
    placeRow->setChildSpacing(4 * s);
    auto* placeCombo = new ui::ComboBox;
    static const char* kPlaceKeys[] = { "above", "below", "right", "left", "front", "back", "center" };
    static const char* kPlaceText[] = { "On top of", "Below",   "Right of", "Left of",
                                        "In front of", "Behind", "Inside (centred in)" };
    for (const char* t : kPlaceText)
      placeCombo->addItem(fmt::format("{} {}", t, refName));
    placeCombo->setSelectedItemIndex(int(std::size(kPlaceKeys)) - 1);
    placeCombo->setExpansive(true);
    placeRow->addChild(new ui::Label("Place"));
    placeRow->addChild(placeCombo);
    blockBox->addChild(placeRow);
    auto* sketch = new ui::Label("");
    blockBox->addChild(sketch);
    auto* sketch2 = new ui::Label("");
    blockBox->addChild(sketch2);
    box->addChild(blockBox);
    blockBox->setVisible(false);

    auto* btnRow = new ui::HBox;
    btnRow->setChildSpacing(4 * s);
    auto* okBtn = new ui::Button("Create");
    auto* cancelBtn = new ui::Button("Cancel");
    btnRow->addChild(okBtn);
    btnRow->addChild(cancelBtn);
    box->addChild(btnRow);

    int blk[3] = { 1, 1, 1 }, org[3] = { 0, 0, 0 };
    bool fits = true;
    auto compute = [&] {
      for (int a = 0; a < 3; ++a)
        blk[a] = std::clamp(sizeF[a]->getValue(), 1, 256);
      const int idx = std::clamp(placeCombo->getSelectedItemIndex(), 0,
                                 int(std::size(kPlaceKeys)) - 1);
      fits = placeBlock(spr, refKey, frame, blk, kPlaceKeys[idx], org);
    };
    auto updateSketch = [&] {
      compute();
      sketch->setText(fmt::format("Block {} x {} x {} at X {}..{}, Y {}..{} (from the top), "
                                  "Z {}..{} (from the back), frame {}",
                                  blk[0], blk[1], blk[2], org[0], org[0] + blk[0], org[1],
                                  org[1] + blk[1], org[2], org[2] + blk[2], frame + 1));
      sketch2->setText(fits ? std::string()
                            : std::string("No room there: moved/cut to fit. Make the model "
                                          "bigger (Fields > Model size) for more room."));
      win.layout();
    };
    for (int a = 0; a < 3; ++a)
      sizeF[a]->Change.connect([&] { updateSketch(); });
    placeCombo->Change.connect([&] { updateSketch(); });
    fillCheck->Click.connect([&] {
      blockBox->setVisible(fillCheck->isSelected());
      updateSketch();
      win.remapWindow();
      win.centerWindow();
      win.invalidate();
    });
    updateSketch();

    bool ok = false;
    auto accept = [&] {
      ok = true;
      win.closeWindow(okBtn);
    };
    okBtn->Click.connect(accept);
    for (NumberField* nf : sizeF)
      nf->Enter.connect(accept);
    cancelBtn->Click.connect([&] { win.closeWindow(cancelBtn); });
    win.remapWindow();
    win.centerWindow();
    win.openWindowInForeground();
    if (!ok)
      return;

    compute();
    NewObjectSpec spec;
    spec.name = nameE->text().empty() ? std::string("Object") : nameE->text();
    spec.fillBlock = fillCheck->isSelected();
    for (int a = 0; a < 3; ++a) {
      spec.block[a] = blk[a];
      spec.blockOrg[a] = org[a];
    }
    spec.frame = frame;
    const doc::color_t col =
      color_utils::color_for_image(Preferences::instance().colorBar.fgColor(), doc::IMAGE_RGB);
    spec.color = doc::rgba(doc::rgba_getr(col), doc::rgba_getg(col), doc::rgba_getb(col), 255);
    std::string err;
    const std::string key = newObjectOnDoc(doc, spec, err);
    if (key.empty()) {
      easymetricReportError("newObject", err);
      return;
    }
    focusObject(key);
    // The block goes on the current frame only (Arin's choice): say so in a
    // multi-frame file, where the object is empty (and so blinks during
    // playback) on the other frames.
    if (spec.fillBlock && spr->totalFrames() > 1)
      if (auto* sb = StatusBar::instance())
        sb->showTip(5000, fmt::format("\"{}\" has its block on frame {} only; its other {} "
                                      "frames are empty until you draw on them",
                                      spec.name, frame + 1, spr->totalFrames() - 1));
  }

  void drawGhostBands(EditorPostRender* render, int dst) const
  {
    if (!m_ghostOn || !m_ghost.any || dst < 0 || dst >= kFaceCount)
      return;
    const gfx::Rect rc = gridFaceRect(dst);
    if (rc.isEmpty())
      return;
    for (int s = 0; s < kFaceCount; ++s) {
      const gfx::Color col = gfx::rgba(kFaceSpecs[s].r, kFaceSpecs[s].g, kFaceSpecs[s].b, 56);
      const auto& cols = m_ghost.col[dst][s];
      for (int c = 0; c < int(cols.size());) {
        if (!cols[size_t(c)]) { ++c; continue; }
        int e = c;
        while (e < int(cols.size()) && cols[size_t(e)]) ++e;
        render->fillRect(col, gfx::Rect(rc.x + c, rc.y, e - c, rc.h));
        c = e;
      }
      const auto& rows = m_ghost.row[dst][s];
      for (int r = 0; r < int(rows.size());) {
        if (!rows[size_t(r)]) { ++r; continue; }
        int e = r;
        while (e < int(rows.size()) && rows[size_t(e)]) ++e;
        render->fillRect(col, gfx::Rect(rc.x, rc.y + r, rc.w, e - r));
        r = e;
      }
    }
  }

  // Per-panel grid (G2): the effective settings for a face —
  // the live draft while its pop-up is open, else the committed
  // values — and the face's UV rect in the sheet (sprite coords,
  // which are also the panel editor's canvas coords).
  PanelGridSettings gridForFace(int f) const
  {
    if (f < 0 || f >= kFaceCount)
      return PanelGridSettings();
    if (m_gridDraftValid[f])
      return m_gridDraft[f];
    return m_grid[f];
  }

  gfx::Rect gridFaceRect(int f) const
  {
    Doc* doc = m_docView ? m_docView->document() : nullptr;
    if (!doc || !doc->sprite() || f < 0 || f >= kFaceCount)
      return gfx::Rect(0, 0, 0, 0);
    return sharedFaceRect(doc->sprite(), f);
  }

  void popOutPreview()
  {
    // Whole-column pop-out (2026-11-07, William): Preview AND
    // Fields move into the floating window together, so the
    // column can be read and used at full size. The old
    // preview-only pop-out is superseded; the script action
    // previewFloat and the Fields button call this same function.
    if (m_previewFloating || !m_previewWidget || !m_fieldsView) return;
    if (!m_previewWindow) {
      m_previewWindow = new ui::Window(ui::Window::WithTitleBar, "Easymetric Preview & Fields");
      m_previewWindow->setMoveable(true);
      m_previewWindow->setSizeable(true);
      m_previewWindow->Close.connect([this](ui::CloseEvent&) { dockPreview(); });
      m_columnBox = new ui::VBox;
      m_columnBox->setChildSpacing(4 * ui::guiscale());
      m_previewWindow->addChild(m_columnBox);
      // NOTE: ui::Window has no setSize(); remapWindow() below sizes the
      // window to fit the column box, same as the old preview-only pop-out.
    }
    // Neither child reports a useful preferred size (the preview is a
    // custom widget with no onSizeHint, the fields View reports only
    // its viewport), so without explicit hints remapWindow() shrinks
    // the window to just its title bar — the first GUI run opened a
    // 260x70 strip with the column squeezed inside it. Hint both
    // children from their docked sizes; dockPreview() resets them so
    // the docked layout keeps its dynamic sizing.
    const gfx::Rect prevBounds = m_previewWidget->bounds();
    const gfx::Rect fieldsBounds = m_fieldsView->bounds();
    m_previewWidget->setSizeHint(
        gfx::Size(std::max(200, prevBounds.w), std::max(160, prevBounds.h)));
    m_fieldsView->setSizeHint(
        gfx::Size(std::max(200, fieldsBounds.w), std::max(160, fieldsBounds.h)));
    m_panes[kFaceCount]->removeChild(m_previewWidget);
    m_panes[kFaceCount + 1]->removeChild(m_fieldsView);
    m_columnBox->addChild(m_previewWidget);
    m_columnBox->addChild(m_fieldsView);
    m_previewWidget->setExpansive(true);
    m_fieldsView->setExpansive(true);
    for (int i = 0; i < 3; ++i)
      m_dockedColWpx[i] = m_colWpx[i];
    m_previewFloating = true;
    m_floatColInit = false;
    m_hoverSplitter = 0;
    m_dragSplitter = 0;
    m_panes[kFaceCount]->setVisible(false);
    m_panes[kFaceCount + 1]->setVisible(false);
    if (m_previewPlaceholder) m_previewPlaceholder->setVisible(false);
    if (m_fieldsPlaceholder) m_fieldsPlaceholder->setVisible(false);
    if (m_previewPopoutBtn) m_previewPopoutBtn->setText("Dock Column");
    if (m_previewPopoutHeaderBtn) m_previewPopoutHeaderBtn->setText("Dock");
    // Reflow the studio into a 3-column layout where the 3 face
    // columns expand to fill the entire freed width.
    layout();
    invalidate();
    m_previewWindow->remapWindow();
    m_previewWindow->centerWindow();
    m_previewWindow->openWindow();
    m_previewWidget->invalidate();
  }

  void dockPreview()
  {
    if (!m_previewFloating || !m_previewWidget || !m_fieldsView) return;
    // Called from the floating window's Close signal too: by
    // then the window is already closed by the manager, so
    // moving the widgets back here is safe, and the window
    // object itself is kept (closed) for the next pop-out.
    if (m_columnBox) {
      m_columnBox->removeChild(m_previewWidget);
      m_columnBox->removeChild(m_fieldsView);
    }
    m_panes[kFaceCount]->addChild(m_previewWidget);
    m_panes[kFaceCount + 1]->addChild(m_fieldsView);
    m_previewWidget->setExpansive(true);
    m_fieldsView->setExpansive(true);
    m_previewFloating = false;
    for (int i = 0; i < 3; ++i)
      m_colWpx[i] = m_dockedColWpx[i];
    m_floatColInit = false;
    m_hoverSplitter = 0;
    m_dragSplitter = 0;
    m_panes[kFaceCount]->setVisible(true);
    m_panes[kFaceCount + 1]->setVisible(true);
    if (m_previewPlaceholder) m_previewPlaceholder->setVisible(false);
    if (m_fieldsPlaceholder) m_fieldsPlaceholder->setVisible(false);
    if (m_previewPopoutBtn) m_previewPopoutBtn->setText("Pop out Column");
    if (m_previewPopoutHeaderBtn) m_previewPopoutHeaderBtn->setText("Pop out");
    // Undo the floating size hints (see popOutPreview) and re-layout
    // the panes back into the original 4-column layout.
    m_previewWidget->resetSizeHint();
    m_fieldsView->resetSizeHint();
    m_panes[kFaceCount]->layout();
    m_panes[kFaceCount + 1]->layout();
    layout();
    invalidate();
    m_previewWidget->invalidate();
  }

  // Grid settings pop-up (G2): one modal window, reused per
  // face. Edits write into the face's live draft, so the editor
  // decorator and the Preview show the new grid immediately;
  // Confirm commits the draft, closing any other way discards
  // it. The window is modal (openWindowInForeground, the same
  // pattern as Aseprite's own Grid Settings): Esc / the X
  // button / Cancel all revert.
  void openGridPopup(int face)
  {
    if (face < 0 || face >= kFaceCount)
      return;
    if (!m_gridWindow) {
      buildGridPopup();
      if (!m_gridWindow)
        return;
    }
    m_gridPopupFace = face;
    m_gridConfirmed = false;
    m_gridDraft[face] = m_grid[face];
    m_gridDraftValid[face] = true;
    refillGridPopup();
    m_gridWindow->setText(fmt::format("Grid - {}", kFaceSpecs[face].displayName));
    m_gridWindow->remapWindow();
    m_gridWindow->centerWindow();
    m_gridWindow->openWindowInForeground();
    // Modal loop done: if Confirm did not commit, revert the
    // draft (the Close handler already did this for the X /
    // Cancel paths; revertGridDraft is idempotent, so calling
    // it again here is a no-op in that case).
    if (!m_gridConfirmed)
      revertGridDraft();
    m_gridPopupFace = -1;
  }

  // "Start from a block": width / height / depth of the block, and the size
  // of the cells it sits in (default: the current object size, grown if the
  // block does not fit). Replaces the content of all six views.
  void openBlockDialog()
  {
    Doc* doc = m_docView ? m_docView->document() : nullptr;
    if (!doc || !doc->sprite())
      return;
    const int s = ui::guiscale();
    int fw[kFaceCount], fh[kFaceCount];
    sharedFaceSizes(doc->sprite(), fw, fh);
    const int curCell[3] = { fw[kFaceFront], fh[kFaceFront], fw[kFaceRight] };
    ui::Window win(ui::Window::WithTitleBar, "Start from a block");
    auto* box = new ui::VBox;
    box->setBorder(gfx::Border(4 * s));
    box->setChildSpacing(3 * s);
    win.addChild(box);
    auto* note = new ui::Label("Replaces everything on all six views of this object with one block.");
    box->addChild(note);
    NumberField* blockE[3];
    NumberField* cellE[3];
    const char* names[3] = { "Width (X)", "Height (Y)", "Depth (Z)" };
    for (int a = 0; a < 3; ++a) {
      auto* row = new ui::HBox;
      row->setChildSpacing(4 * s);
      blockE[a] = new NumberField(1, 256, std::max(1, curCell[a] / 3));
      cellE[a] = new NumberField(1, 256, curCell[a]);
      row->addChild(new ui::Label(names[a]));
      row->addChild(new ui::Label("block"));
      row->addChild(blockE[a]);
      row->addChild(new ui::Label("cell"));
      row->addChild(cellE[a]);
      box->addChild(row);
    }
    box->addChild(new ui::Label("The block is centred in each cell, rounded once in model space."));
    auto* btnRow = new ui::HBox;
    btnRow->setChildSpacing(4 * s);
    auto* okBtn = new ui::Button("Create");
    auto* cancelBtn = new ui::Button("Cancel");
    btnRow->addChild(okBtn);
    btnRow->addChild(cancelBtn);
    box->addChild(btnRow);
    bool ok = false;
    okBtn->Click.connect([&] {
      ok = true;
      win.closeWindow(okBtn);
    });
    cancelBtn->Click.connect([&] { win.closeWindow(cancelBtn); });
    win.remapWindow();
    win.centerWindow();
    win.openWindowInForeground();
    if (!ok)
      return;
    auto val = [](NumberField* e, int d) {
      const std::string t = e->text();
      return t.empty() ? d : std::clamp(std::atoi(t.c_str()), 1, 256);
    };
    const BlockLayout b = planBlockLayout(val(blockE[0], 8), val(blockE[1], 8), val(blockE[2], 8),
                                          val(cellE[0], curCell[0]), val(cellE[1], curCell[1]),
                                          val(cellE[2], curCell[2]));
    const doc::color_t col =
      color_utils::color_for_image(Preferences::instance().colorBar.fgColor(), doc::IMAGE_RGB);
    std::string err;
    const doc::frame_t fr = m_editors[0] ? m_editors[0]->frame() : doc::frame_t(0);
    if (!applyBlockLayout(doc, fr, b, doc::rgba(doc::rgba_getr(col), doc::rgba_getg(col),
                                                doc::rgba_getb(col), 255), err)) {
      easymetricReportError("startFromBlock", err);
      return;
    }
    initLayersAndFraming();
    m_lastHash = 0;
    pollAndRender(true);
    if (auto* sb = StatusBar::instance())
      sb->showTip(3000, fmt::format("Block {}x{}x{} in cells {}x{}x{}", b.block[0], b.block[1],
                                    b.block[2], b.cell[0], b.cell[1], b.cell[2]));
  }

  void buildGridPopup()
  {
    const int s = ui::guiscale();
    m_gridWindow = new ui::Window(ui::Window::WithTitleBar, "Grid");
    m_gridWindow->Close.connect([this](ui::CloseEvent&) { revertGridDraft(); });
    ui::VBox* box = new ui::VBox;
    box->setBorder(gfx::Border(4 * s));
    box->setChildSpacing(3 * s);
    m_gridWindow->addChild(box);

    // Row 1: on/off + axis toggle.
    ui::HBox* topRow = new ui::HBox;
    topRow->setChildSpacing(4 * s);
    m_gridOnCheck = new ui::CheckBox("Grid on");
    m_gridOnCheck->Click.connect([this] { updateGridDraftFromPopup(); });
    m_gridAxisCombo = new ui::ComboBox;
    m_gridAxisCombo->addItem("Horizontal");
    m_gridAxisCombo->addItem("Vertical");
    m_gridAxisCombo->setSelectedItemIndex(0);
    m_gridAxisCombo->Change.connect([this] { onGridAxisChanged(); });
    topRow->addChild(m_gridOnCheck);
    topRow->addChild(new ui::Label("Axis:"));
    topRow->addChild(m_gridAxisCombo);
    box->addChild(topRow);

    // Row 2: number of grids + gap, for the chosen axis.
    // Plain ui::Entry (not ExprEntry): the numbers are plain
    // counts/gaps, and the draft write clamps them anyway.
    ui::HBox* numRow = new ui::HBox;
    numRow->setChildSpacing(4 * s);
    m_gridCountEntry = new ui::Entry(4, "");
    m_gridSpacingEntry = new ui::Entry(4, "");
    m_gridCountEntry->setExpansive(true);
    m_gridSpacingEntry->setExpansive(true);
    m_gridCountEntry->Change.connect([this] { updateGridDraftFromPopup(); });
    m_gridSpacingEntry->Change.connect([this] { updateGridDraftFromPopup(); });
    numRow->addChild(new ui::Label("Grids:"));
    numRow->addChild(m_gridCountEntry);
    numRow->addChild(new ui::Label("Gap (px):"));
    numRow->addChild(m_gridSpacingEntry);
    box->addChild(numRow);

    // Live status: the cell size for the edited axis, or why the
    // numbers do not fit the face. Confirm reads it too.
    m_gridStatusLabel = new ui::Label("");
    box->addChild(m_gridStatusLabel);

    // Row 3: Confirm / Cancel.
    ui::HBox* btnRow = new ui::HBox;
    btnRow->setChildSpacing(4 * s);
    m_gridConfirmBtn = new ui::Button("Confirm");
    m_gridCancelBtn = new ui::Button("Cancel");
    m_gridConfirmBtn->Click.connect([this] { confirmGridPopup(); });
    m_gridCancelBtn->Click.connect([this] {
      if (m_gridWindow)
        m_gridWindow->closeWindow(m_gridCancelBtn);
    });
    btnRow->addChild(m_gridConfirmBtn);
    btnRow->addChild(m_gridCancelBtn);
    box->addChild(btnRow);
  }

  void refillGridPopup()
  {
    const int face = m_gridPopupFace;
    if (face < 0 || face >= kFaceCount || !m_gridWindow)
      return;
    const PanelGridSettings& draft = m_gridDraft[face];
    m_gridRefillingPopup = true;
    m_gridPopupAxis = 0;
    m_gridOnCheck->setSelected(draft.on);
    m_gridAxisCombo->setSelectedItemIndex(0);
    m_gridCountEntry->setTextf("%d", draft.countH);
    m_gridSpacingEntry->setTextf("%d", draft.spacingH);
    m_gridRefillingPopup = false;
    updateGridStatusLabel();
  }

  // The H/V toggle switches which axis the entries edit: write
  // the entries back into the draft for the axis being left,
  // then load the other axis's values.
  void onGridAxisChanged()
  {
    if (m_gridRefillingPopup)
      return;
    const int face = m_gridPopupFace;
    if (face < 0 || face >= kFaceCount || !m_gridAxisCombo)
      return;
    writeGridPopupEntriesToDraft();
    m_gridPopupAxis = m_gridAxisCombo->getSelectedItemIndex();
    const PanelGridSettings& draft = m_gridDraft[face];
    if (m_gridPopupAxis == 0) {
      m_gridCountEntry->setTextf("%d", draft.countH);
      m_gridSpacingEntry->setTextf("%d", draft.spacingH);
    }
    else {
      m_gridCountEntry->setTextf("%d", draft.countV);
      m_gridSpacingEntry->setTextf("%d", draft.spacingV);
    }
    updateGridStatusLabel();
  }

  // Writes the entries into the draft for the axis currently
  // being edited (m_gridPopupAxis), clamped to count [1, 64]
  // and spacing [0, 256]. A half-typed entry reads as 0, which
  // clamps to 1 / 0 — the value is corrected on the next edit,
  // and Confirm re-reads the entries before validating.
  void writeGridPopupEntriesToDraft()
  {
    const int face = m_gridPopupFace;
    if (face < 0 || face >= kFaceCount || !m_gridCountEntry || !m_gridSpacingEntry)
      return;
    PanelGridSettings& draft = m_gridDraft[face];
    const int count = std::clamp(m_gridCountEntry->textInt(), 1, 64);
    const int spacing = std::clamp(m_gridSpacingEntry->textInt(), 0, 256);
    if (m_gridPopupAxis == 0) {
      draft.countH = count;
      draft.spacingH = spacing;
    }
    else {
      draft.countV = count;
      draft.spacingV = spacing;
    }
  }

  // Live preview: every edit in the pop-up writes the draft,
  // refreshes the status line, and repaints the editor +
  // Preview from the draft, so the user sees the grid change
  // as they type. Same pattern as the Fields handlers, which
  // also call pollAndRender(true) on every change.
  void updateGridDraftFromPopup()
  {
    if (m_gridRefillingPopup)
      return;
    const int face = m_gridPopupFace;
    if (face < 0 || face >= kFaceCount || !m_gridOnCheck)
      return;
    m_gridDraft[face].on = m_gridOnCheck->isSelected();
    writeGridPopupEntriesToDraft();
    updateGridStatusLabel();
    if (m_editors[face])
      m_editors[face]->invalidate();
    pollAndRender(true);
  }

  void updateGridStatusLabel()
  {
    const int face = m_gridPopupFace;
    if (face < 0 || face >= kFaceCount || !m_gridStatusLabel)
      return;
    const gfx::Rect faceRect = gridFaceRect(face);
    const int axis = m_gridAxisCombo ? m_gridAxisCombo->getSelectedItemIndex() : 0;
    // Axis 0 is Horizontal: its lines are spaced down the face
    // height; axis 1 (Vertical) across the face width.
    const int faceSize = (axis == 0) ? faceRect.h : faceRect.w;
    const int count = m_gridCountEntry ? std::clamp(m_gridCountEntry->textInt(), 1, 64) : 1;
    const int spacing = m_gridSpacingEntry ? std::clamp(m_gridSpacingEntry->textInt(), 0, 256) : 0;
    std::vector<float> lines;
    if (gridLinePositions(faceSize, count, spacing, lines)) {
      const float cell = (float(faceSize) - float(count - 1) * float(spacing)) / float(count);
      m_gridStatusLabel->setText(fmt::format("Cell: {:.1f}px, {} line{} on this axis",
                                             cell, lines.size(),
                                             lines.size() == 1 ? "" : "s"));
    }
    else {
      const int need = (count - 1) * spacing + count;
      m_gridStatusLabel->setText(fmt::format(
        "Does not fit: {} grids with {}px gaps need at least {}px on a {}px face",
        count, spacing, need, faceSize));
    }
  }

  void confirmGridPopup()
  {
    const int face = m_gridPopupFace;
    if (face < 0 || face >= kFaceCount || !m_gridWindow)
      return;
    // Write the entries back first (the user may Confirm without
    // leaving the entry), then refuse with a reason when either
    // axis does not fit the CURRENT face size (the face may have
    // been resized via Fields while the pop-up was open). The
    // fit check only applies while the grid is on: with it off
    // the numbers are inert and draw nothing.
    if (m_gridOnCheck)
      m_gridDraft[face].on = m_gridOnCheck->isSelected();
    writeGridPopupEntriesToDraft();
    const gfx::Rect faceRect = gridFaceRect(face);
    const PanelGridSettings& draft = m_gridDraft[face];
    std::vector<float> lines;
    std::string why;
    if (draft.on) {
      if (!gridLinePositions(faceRect.h, draft.countH, draft.spacingH, lines))
        why = fmt::format("Horizontal: {} grids with {}px gaps do not fit a {}px face",
                          draft.countH, draft.spacingH, faceRect.h);
      else if (!gridLinePositions(faceRect.w, draft.countV, draft.spacingV, lines))
        why = fmt::format("Vertical: {} grids with {}px gaps do not fit a {}px face",
                          draft.countV, draft.spacingV, faceRect.w);
    }
    if (!why.empty()) {
      if (m_gridStatusLabel)
        m_gridStatusLabel->setText(why + " — adjust the numbers or turn the grid off.");
      return;
    }
    m_grid[face] = draft;
    m_gridDraftValid[face] = false;
    m_gridConfirmed = true;
    syncGridButtonStates();
    if (m_editors[face])
      m_editors[face]->invalidate();
    pollAndRender(true);
    m_gridWindow->closeWindow(m_gridConfirmBtn);
  }

  void revertGridDraft()
  {
    const int face = m_gridPopupFace;
    if (face < 0 || face >= kFaceCount)
      return;
    if (!m_gridDraftValid[face])
      return;
    m_gridDraftValid[face] = false;
    syncGridButtonStates();
    if (m_editors[face])
      m_editors[face]->invalidate();
    pollAndRender(true);
  }

  void syncGridButtonStates()
  {
    for (int f = 0; f < kFaceCount; ++f) {
      if (m_gridButtons[f])
        m_gridButtons[f]->setOn(m_grid[f].on);
    }
  }

private:
  void render3DModel(const SixSurfaceData& data,
                     int yawDeg,
                     int pitchDeg,
                     int pivX,
                     int pivY,
                     int pivZ,
                     int renderStyle,
                     bool addOutline,
                     bool allowCutouts,
                     bool showDiff,
                     bool showNorm,
                     bool showDept,
                     const std::array<PanelGridSettings, kFaceCount>* grids = nullptr,
                     bool tintCutouts = false,
                     const std::vector<PreviewObject>* composite = nullptr)
  {
    Render3DResult res =
      composite ? compute3DPreviewMulti(*composite, yawDeg, pitchDeg, pivX, pivY, pivZ,
                                        renderStyle, addOutline, allowCutouts, grids, tintCutouts)
                : compute3DPreviewImages(data, yawDeg, pitchDeg, pivX, pivY, pivZ, renderStyle,
                                         addOutline, allowCutouts, grids, tintCutouts);
    m_lastCutoutPx = res.cutoutPx;
    m_lastObjHitPx = res.objHitPx;
    m_lastDiffImg = res.diffImg;
    m_lastNormImg = res.normImg;
    m_lastDeptImg = res.deptImg;

    if (!m_previewWidget)
      return;

    // Preview images are RGB, so their background is opaque
    // black (0,0,0) — and that black image completely covered
    // the Preview BG colour/alpha fill drawn underneath it.
    // That is why setting BG to #000000 / alpha 0 still showed
    // a black background behind the model (2026-11-04).
    // For the preview only (baked Iso layers keep the RGB
    // images in m_last*Img unchanged), build RGBA copies
    // where the background is transparent. Background is
    // exactly the pixels no ray hit: diff, norm AND dept are
    // all 0 there, while every hit pixel writes a non-zero
    // dept (>=35) and a non-zero norm, so a genuinely black
    // model pixel is never mistaken for background.
    // Background is composited INTO the preview image, not
    // relied on from surface alpha: in practice the scaled
    // drawSurface path draws the image opaque (Linux harness
    // screenshot, 2026-11-04: black rectangle, checkerboard
    // only outside the image bounds, even though the image
    // background pixels are alpha 0). So bg pixels here get
    // the chosen BG colour (blended over a checkerboard when
    // alpha is in-between), and alpha 0 gets the checkerboard
    // itself — never black. Baked Iso layers are untouched.
    int bgR = 0, bgG = 0, bgB = 0, bgA = 0;
    if (m_bgColorButton && m_bgAlphaSlider) {
      const app::Color c = m_bgColorButton->getColor();
      bgR = c.getRed(); bgG = c.getGreen(); bgB = c.getBlue();
      bgA = m_bgAlphaSlider->getValue();
    }
    auto makePreviewSurface = [](const doc::Image* img,
                                 const doc::Image* diff,
                                 const doc::Image* norm,
                                 const doc::Image* dept,
                                 int bgR, int bgG, int bgB, int bgA) -> os::SurfaceRef {
      if (!img) return nullptr;
      const int w = img->width();
      const int h = img->height();
      doc::ImageRef rgba(doc::Image::create(doc::IMAGE_RGB, w, h));
      for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
          const uint32_t dc = diff ? doc::get_pixel_fast<doc::RgbTraits>(diff, x, y) : 0;
          const uint32_t nc = norm ? doc::get_pixel_fast<doc::RgbTraits>(norm, x, y) : 0;
          const uint32_t tc = dept ? doc::get_pixel_fast<doc::RgbTraits>(dept, x, y) : 0;
          const bool isBg = (dc == 0 && nc == 0 && tc == 0);
          const uint32_t c = doc::get_pixel_fast<doc::RgbTraits>(img, x, y);
          if (isBg) {
            const int checker = (((x / 8) + (y / 8)) & 1) ? 192 : 128;
            const int r = (bgR * bgA + checker * (255 - bgA)) / 255;
            const int g = (bgG * bgA + checker * (255 - bgA)) / 255;
            const int b = (bgB * bgA + checker * (255 - bgA)) / 255;
            doc::put_pixel(rgba.get(), x, y, doc::rgba(r, g, b, 255));
          }
          else {
            doc::put_pixel(rgba.get(), x, y,
                           doc::rgba(doc::rgba_getr(c), doc::rgba_getg(c), doc::rgba_getb(c), 255));
          }
        }
      }
      os::SurfaceRef surf = os::System::instance()->makeRgbaSurface(w, h);
      convert_image_to_surface(rgba.get(), nullptr, surf.get(), 0, 0, 0, 0, w, h);
      return surf;
    };

    m_previewWidget->setSurfaces(
      makePreviewSurface(m_lastDiffImg.get(), m_lastDiffImg.get(), m_lastNormImg.get(), m_lastDeptImg.get(), bgR, bgG, bgB, bgA),
      makePreviewSurface(m_lastNormImg.get(), m_lastDiffImg.get(), m_lastNormImg.get(), m_lastDeptImg.get(), bgR, bgG, bgB, bgA),
      makePreviewSurface(m_lastDeptImg.get(), m_lastDiffImg.get(), m_lastNormImg.get(), m_lastDeptImg.get(), bgR, bgG, bgB, bgA),
      yawDeg,
      pitchDeg,
      int(res.cxScreen),
      int(res.cyScreen),
      showDiff,
      showNorm,
      showDept);
  }

  void bakeToActiveDocLayers()
  {
    Doc* doc = m_docView->document();
    if (!doc || !doc->sprite() || !m_lastDiffImg || !m_lastNormImg || !m_lastDeptImg)
      return;

    doc::Sprite* spr = doc->sprite();
    const doc::frame_t fr = (m_docView->editor() ? m_docView->editor()->frame() : doc::frame_t(0));

    try {
      Tx tx(doc, "Render Easymetric 3D + Normal + Depth Layers", ModifyDocument);
      DocApi api = doc->getApi(tx);

      auto writeLayerImg = [&](const char* layerName, const doc::Image* srcImg) {
        doc::LayerImage* lyr = findOrCreateLayer(doc, api, spr, layerName);
        doc::ImageRef copyImg(doc::Image::createCopy(srcImg));
        if (doc::Cel* cel = lyr->cel(fr))
          api.replaceImage(spr, cel->imageRef(), copyImg);
        else
          api.addCel(lyr, fr, copyImg);
      };

      const bool anyChecked = m_mapDiffuseCheck->isSelected() ||
                              m_mapNormalCheck->isSelected() ||
                              m_mapDepthCheck->isSelected();
      if (!anyChecked || m_mapDepthCheck->isSelected())
        writeLayerImg("Iso_Depth", m_lastDeptImg.get());
      if (!anyChecked || m_mapNormalCheck->isSelected())
        writeLayerImg("Iso_Normal", m_lastNormImg.get());
      if (!anyChecked || m_mapDiffuseCheck->isSelected())
        writeLayerImg("Iso_Diffuse", m_lastDiffImg.get());

      for (int c = 0; c < 3; ++c) {
        if (m_colLinked[c]) {
          const int srcFace = c * 2;
          const int dstFace = c * 2 + 1;
          const std::string srcName = std::string("Surface_") + kFaceSpecs[srcFace].sliceName;
          const std::string dstName = std::string("Surface_") + kFaceSpecs[dstFace].sliceName;
          doc::LayerImage* srcLyr = findLayerByName(spr, srcName);
          doc::LayerImage* dstLyr = findLayerByName(spr, dstName);
          if (srcLyr && dstLyr) {
            doc::Cel* srcCel = srcLyr->cel(fr);
            doc::Cel* dstCel = dstLyr->cel(fr);
            if (srcCel && srcCel->image()) {
              doc::ImageRef copyImg(doc::Image::createCopy(srcCel->image()));
              if (dstCel) {
                api.replaceImage(spr, dstCel->imageRef(), copyImg);
                api.setCelPosition(spr, dstCel, srcCel->x(), srcCel->y());
              }
              else {
                api.addCel(dstLyr, fr, copyImg);
              }
            }
          }
        }
      }

      tx.commit();
      doc->notifyGeneralUpdate();
      if (auto* sb = StatusBar::instance())
        sb->showTip(1500, "Rendered 3D model to Iso_Diffuse / Iso_Normal / Iso_Depth layers");
    }
    catch (const std::exception& e) {
      Console::showException(e);
      easymetricReportError("bakeToActiveDocLayers", e.what());
    }
  }

  void exportToWavefrontObj()
  {
    Doc* doc = m_docView->document();
    if (!doc || !doc->sprite()) return;

    std::string defaultPath = doc->filename();
    if (defaultPath.empty())
      defaultPath = "easymetric_model.obj";
    else
      defaultPath = base::replace_extension(defaultPath, "obj");

    base::paths exts = { "obj" };
    base::paths selected;
    if (!show_file_selector("Export Easymetric 3D Model as Wavefront .OBJ",
                            defaultPath,
                            exts,
                            FileSelectorType::Save,
                            selected) ||
        selected.empty()) {
      return;
    }

    const std::string objPath = base::replace_extension(selected.front(), "obj");
    const doc::frame_t fr = (m_docView->editor() ? m_docView->editor()->frame() : doc::frame_t(0));
    const bool useDepth = (m_useFaceDepthCheck && m_useFaceDepthCheck->isSelected());
    exportEasymetricObj(UIContext::instance(), doc, fr, objPath,
                        m_pivXSlider ? m_pivXSlider->getValue() : 50,
                        m_pivYSlider ? m_pivYSlider->getValue() : 50,
                        m_pivZSlider ? m_pivZSlider->getValue() : 50, useDepth,
                        [this](const std::string& k) -> const std::array<bool, 3>* {
                          if (k == m_focusKey)
                            return &m_colLinked;
                          auto it = m_linkedByObject.find(k);
                          return it != m_linkedByObject.end() ? &it->second : nullptr;
                        });

    if (auto* sb = StatusBar::instance())
      sb->showTip(2500, fmt::format("Exported 3D Wavefront OBJ to {}", objPath));
  }

  DocView* m_docView;
  std::array<PaneHeader*, 8> m_headers;
  std::array<ui::VBox*, 8> m_panes;

  // 6 Surface Editors
  std::array<Editor*, kFaceCount> m_editors;
  std::array<ui::View*, kFaceCount> m_views;
  std::array<LinkToggleButton*, 3> m_linkButtons;
  std::array<bool, 3> m_colLinked = { false, false, false };

  // Panel action buttons, one set per face header (priority order in
  // each header: Grid, Link, Sym X, Sym Y, Flip H, Flip V).
  std::array<PaneActionButton*, kFaceCount> m_gridButtons = {};
  std::array<PaneActionButton*, kFaceCount> m_animLinkButtons = {};
  std::array<PaneActionButton*, kFaceCount> m_symXButtons = {};
  std::array<PaneActionButton*, kFaceCount> m_symYButtons = {};
  std::array<PaneActionButton*, kFaceCount> m_flipHButtons = {};
  std::array<PaneActionButton*, kFaceCount> m_flipVButtons = {};

  // Animation link: per-panel follow state (default linked) and the
  // frame an unlinked panel is pinned to. See onAfterFrameChanged.
  std::array<bool, kFaceCount> m_animLinked = { true, true, true, true, true, true };
  std::array<doc::frame_t, kFaceCount> m_animPinnedFrame = {
    doc::frame_t(0), doc::frame_t(0), doc::frame_t(0),
    doc::frame_t(0), doc::frame_t(0), doc::frame_t(0)
  };

  // Per-panel grid (G2, 2026-11-07): committed settings per face
  // plus a live draft while a grid pop-up is open for that face.
  // The draft is what the editor decorator and the Preview render
  // while the pop-up is open; Confirm copies it into m_grid,
  // closing without Confirm discards it. In-memory only —
  // closing the studio forgets the grids. Never document pixels,
  // never in undo history: the grid is drawn as an overlay
  // (decorator) and baked into the Preview's Diffuse image at
  // render time, never into a cel.
  std::array<PanelGridSettings, kFaceCount> m_grid;
  std::array<PanelGridSettings, kFaceCount> m_gridDraft;
  std::array<bool, kFaceCount> m_gridDraftValid = {};
  // Base idle states for the grid decorators (the StandbyState*
  // the Decorator base class needs; kept alive here because the
  // editor's own state object is replaced on tool switches).
  std::array<EditorStatePtr, kFaceCount> m_panelBaseStates;
  std::array<EasymetricWheelZoomState::EasymetricGridDecorator*, kFaceCount> m_gridDecorators = {};
  // Grid settings pop-up (one window, reused per face) and its
  // widgets, refilled on every open.
  ui::Window* m_gridWindow = nullptr;
  ui::CheckBox* m_gridOnCheck = nullptr;
  ui::ComboBox* m_gridAxisCombo = nullptr;
  ui::Entry* m_gridCountEntry = nullptr;
  ui::Entry* m_gridSpacingEntry = nullptr;
  ui::Label* m_gridStatusLabel = nullptr;
  ui::Button* m_gridConfirmBtn = nullptr;
  ui::Button* m_gridCancelBtn = nullptr;
  int m_gridPopupFace = -1;
  // Which axis the pop-up's entries are editing right now (0 =
  // Horizontal, 1 = Vertical); the axis switch writes the entries
  // back into the draft for the axis being left first.
  int m_gridPopupAxis = 0;
  // True while refillGridPopup is setting the widgets: the
  // ComboBox fires Change when its index actually changes, so
  // without this the refill would run onGridAxisChanged with
  // the previous pop-up's entry text still in the boxes and
  // write it into the new face's draft.
  bool m_gridRefillingPopup = false;
  bool m_gridConfirmed = false;

  // Preview & Fields
  EasymetricPreviewWidget* m_previewWidget = nullptr;
  FieldsPanelBox* m_fieldsBox = nullptr;
  ui::View* m_fieldsView = nullptr;
  bool m_savedZoomWithWheel = true;
  bool m_restoreZoomWithWheel = false;

  // Fields controls
  ui::Slider* m_yawSlider = nullptr;
  ui::Slider* m_pitchSlider = nullptr;
  ui::Slider* m_pivXSlider = nullptr;
  ui::Slider* m_pivYSlider = nullptr;
  ui::Slider* m_pivZSlider = nullptr;
  ui::ComboBox* m_tagCombo = nullptr;
  ui::CheckBox* m_mapDiffuseCheck = nullptr;
  ui::CheckBox* m_mapNormalCheck = nullptr;
  ui::CheckBox* m_mapDepthCheck = nullptr;
  ui::ComboBox* m_renderTypeCombo = nullptr;
  ui::Label* m_zoomLabel = nullptr;
  ui::CheckBox* m_strokeCheck = nullptr;
  ui::CheckBox* m_cutoutCheck = nullptr;
  ui::CheckBox* m_lockBordersCheck = nullptr;
  ui::CheckBox* m_symXCheck = nullptr;
  ui::CheckBox* m_symYCheck = nullptr;
  ui::CheckBox* m_useFaceDepthCheck = nullptr;
  ui::Window* m_previewWindow = nullptr;
  ui::VBox* m_columnBox = nullptr;
  ui::Label* m_previewPlaceholder = nullptr;
  ui::Label* m_fieldsPlaceholder = nullptr;
  ui::Button* m_previewPopoutBtn = nullptr;
  PaneActionButton* m_previewPopoutHeaderBtn = nullptr;
  bool m_previewFloating = false;
  app::ColorButton* m_bgColorButton = nullptr;
  ui::Slider* m_bgAlphaSlider = nullptr;
  // Studio tool strip (2026-11-07, William: the Depth tool must live
  // in a toolbar, not in Fields): Draw / Depth Sculpt toggle buttons
  // plus the Plus/Minus mode and brush size, in a strip across the
  // top of the studio. m_depthToolOn is the tool mode; the Fields
  // Depth Brush checkbox is gone.
  ui::HBox* m_toolStrip = nullptr;     // unused since 2026-11-09 (tools live in the toolbar)
  DepthTool m_depthTool = DepthTool::None;
  // Hidden-Base status tip (2026-11-07): set while Surface_Base is
  // hidden and the tip has already been shown once for that hide,
  // so pollAndRender warns once per hide instead of every 33 ms.
  bool m_baseHiddenTipShown = false;

  // Depth-sculpt stroke state: one Tx for the whole drag, so a
  // drag is a single undo step. Each dab installs a fresh copy
  // of the working image via replaceImage (the Tx groups them).
  std::unique_ptr<Tx> m_depthTx;
  doc::ImageRef m_depthWork;
  doc::ImageRef m_depthLive;
  doc::ImageRef m_baseWork;
  doc::ImageRef m_baseLive;
  bool m_depthStrokeActive = false;
  bool m_depthInstalled = false;
  bool m_baseInstalled = false;
  int m_strokeFace = -1;
  doc::frame_t m_strokeFrame = 0;
  bool m_strokeRight = false;
  bool m_strokeHasLast = false;
  int m_strokeLastU = 0;
  int m_strokeLastV = 0;
  std::vector<uint8_t> m_strokeTouched;
  ui::CheckBox* m_liveBakeCheck = nullptr;
  ui::CheckBox* m_linkDimensionsCheck = nullptr;

  int m_lastFaceW[kFaceCount] = { 0, 0, 0, 0, 0, 0 };
  int m_lastFaceH[kFaceCount] = { 0, 0, 0, 0, 0, 0 };

  // Splitter state: pixel widths for cols 0..2 (col 3 is the
  // remainder), top-row pixel height per column, and
  // hover/drag tracking. 0 in m_hoverSplitter/m_dragSplitter
  // means no splitter; 1..3 = vertical boundary, 10+c =
  // horizontal boundary in column c.
  // m_colWpx/m_rowHpx are mutable because computePaneLayout
  // (const, called from paint/hit-testing) lazily initialises
  // them on first layout; after that only a drag changes them.
  mutable int m_colWpx[3] = { 0, 0, 0 };
  mutable int m_floatColWpx[3] = { 0, 0, 0 };
  mutable int m_rowHpx[4] = { 0, 0, 0, 0 };
  mutable bool m_colInit = false;
  mutable bool m_floatColInit = false;
  // Set once the studio's exit restores have run (prepareForExit /
  // restoreStateOnExit), so the destructor does not repeat them.
  bool m_exited = false;
  bool m_ghostOn = true;
  GhostData m_ghost;
  PaneActionButton* m_ghostHeaderBtn = nullptr;
  ui::CheckBox* m_ghostCheck = nullptr;
  // Object W/H/D fields (Fields > Object row); kept in sync with the face sizes.
  std::array<NumberField*, 3> m_objSizeFields = { nullptr, nullptr, nullptr };
  int m_lastObjSize[3] = { -1, -1, -1 };
  // Objects (2026-11-17): the focused object ("" = Object 1), linked
  // columns per object, the Fields object row, the last composite preview.
  std::string m_focusKey;
  std::map<std::string, std::array<bool, 3>> m_linkedByObject;
  ui::ComboBox* m_objectCombo = nullptr;
  std::vector<std::string> m_objectComboKeys;
  std::string m_objectListSig;
  ui::ComboBox* m_objRankCombo = nullptr;
  ui::Button* m_deleteObjBtn = nullptr;
  ui::CheckBox* m_showOthersCheck = nullptr;
  bool m_showOthers = true;
  bool m_updatingObjectUi = false;
  int m_refitPanels = 0;
  std::vector<int> m_lastObjHitPx;
  std::vector<std::string> m_lastObjHitKeys;
  // Faded other objects in the panels: per face, colour per face pixel
  // (0 = nothing), rebuilt with the preview.
  std::array<std::vector<uint32_t>, kFaceCount> m_overlay;
  std::array<gfx::Size, kFaceCount> m_overlaySize;
  int m_lastOverlayPx = 0;
  // Auto-align (see planFaceAlign).
  ui::CheckBox* m_autoAlignCheck = nullptr;
  ui::Slider* m_alignLimitSlider = nullptr;
  ui::CheckBox* m_showCutoutsCheck = nullptr;
  DocUndo* m_undoObserved = nullptr;
  int m_pendingAlignFace = -1;
  bool m_aligning = false;
  json11::Json m_lastAlign;
  int m_lastCutoutPx = 0;
  bool m_overlaysSaved = false;
  bool m_savedShowSlices = true;
  bool m_savedShowLayerEdges = false;
  int m_dockedColWpx[3] = { 0, 0, 0 };
  int m_hoverSplitter = 0;
  int m_dragSplitter = 0;
  gfx::Point m_dragStart;
  int m_dragColWpx[3] = { 0, 0, 0 };
  int m_dragPreviewW = 0;
  int m_dragRowHpx = 0;
  int m_dragRowCol = 0;
  int m_lastActiveFace = -1;
  doc::MaskRef m_savedMask;
  // G3 per-panel symmetry (2026-11-07, William): each face keeps
  // its own SX/SY. The studio follows the active panel: when a
  // panel activates, its stored state + its face-centre axis are
  // applied to the document symmetry (the tool loop reads doc
  // prefs, so strokes mirror around that axis). Leaving all
  // panels, or closing the studio, restores the document symmetry
  // saved on entry, so the studio never leaks a face-centre axis
  // into the main editor.
  bool m_symXFace[kFaceCount] = { false, false, false, false, false, false };
  bool m_symYFace[kFaceCount] = { false, false, false, false, false, false };
  bool m_symFacesInit = false;
  int m_savedSymMode = int(app::gen::SymmetryMode::NONE);
  bool m_savedSymEnabled = false;
  double m_savedSymXAxis = 0.0;
  double m_savedSymYAxis = 0.0;
  bool m_symDocSaved = false;
  gfx::Rect m_lastSymFaceRect;

  ui::Timer m_timer;
  uint64_t m_lastHash = 0;
  double m_lastRenderMs = 0;
  double m_lastRenderDurMs = 0;
  uint64_t m_lastBaseSilhouetteHash = 0;
  doc::ImageRef m_lastDiffImg;
  doc::ImageRef m_lastNormImg;
  doc::ImageRef m_lastDeptImg;
};

void EasymetricWheelZoomState::EasymetricGridDecorator::postRenderDecorator(
  EditorPostRender* render)
{
  // Base draws nothing in panels (early return); call it anyway
  // so any future non-panel behaviour of the base is preserved.
  StandbyState::Decorator::postRenderDecorator(render);
  Editor* editor = render->getEditor();
  if (!editor || !m_view || m_face < 0 || m_face >= kFaceCount)
    return;
  m_view->drawOtherObjects(render, m_face);
  m_view->drawGhostBands(render, m_face);
  const PanelGridSettings grid = m_view->gridForFace(m_face);
  if (!grid.on)
    return;
  const gfx::Rect faceRect = m_view->gridFaceRect(m_face);
  if (faceRect.isEmpty())
    return;
  // drawLine takes editor (canvas) coords: the face rect is the
  // UV rect in the sheet, so a vertical line at face-x `line` is
  // canvas x = faceRect.x + line. The editor line is 1px at
  // int(line); that pixel is always one of the Preview's
  // straddled pixels (see the sim's editor_pixel check).
  std::vector<float> lines;
  // Vertical settings draw the vertical lines (face-x),
  // Horizontal settings the horizontal lines (face-y).
  if (gridLinePositions(faceRect.w, grid.countV, grid.spacingV, lines)) {
    for (float line : lines) {
      const int x = faceRect.x + int(line);
      render->drawLine(gfx::rgba(255, 255, 255, 180),
                       x, faceRect.y, x, faceRect.y + faceRect.h - 1);
    }
  }
  if (gridLinePositions(faceRect.h, grid.countH, grid.spacingH, lines)) {
    for (float line : lines) {
      const int y = faceRect.y + int(line);
      render->drawLine(gfx::rgba(255, 255, 255, 180),
                       faceRect.x, y, faceRect.x + faceRect.w - 1, y);
    }
  }
}

void EasymetricWheelZoomState::EasymetricGridDecorator::getInvalidDecoratoredRegion(
  Editor* editor, gfx::Region& region)
{
  StandbyState::Decorator::getInvalidDecoratoredRegion(editor, region);
  // No extra region: the grid is locked to the canvas, so it
  // scrolls and zooms with the face pixels and is redrawn by
  // postRenderDecorator on every editor paint. Grid setting
  // changes invalidate the editor explicitly (see
  // updateGridDraftFromPopup / confirmGridPopup).
}

// Builds the machine-readable state text. Works headless (document part only)
// and with the UI (adds studio panel geometry, tool mode, header buttons).
static std::string easymetricStateJsonText(Context* context)
{
  json11::Json::object root;
  Doc* doc = context ? static_cast<Doc*>(context->activeDocument()) : nullptr;
  doc::Layer* activeLayer = nullptr;
  doc::frame_t fr = 0;
  if (context) {
    Site site = context->activeSite();
    activeLayer = site.layer();
    fr = site.frame();
  }
  root["version"] = 1;
  root["uiAvailable"] = (context && context->isUIAvailable());
  if (doc) {
    json11::Json::object d = docStateObject(doc, activeLayer, fr);
    for (auto& kv : d)
      root[kv.first] = kv.second;
  }
  else {
    root["sprite"] = nullptr;
  }
  json11::Json::object studio{ { "open", false } };
  if (context && context->isUIAvailable() && UIContext::instance()) {
    DocView* view = UIContext::instance()->activeView();
    if (view && view->isMultiPane()) {
      if (auto* mp = dynamic_cast<EasymetricMultiPaneView*>(view->multiPaneLayout()))
        studio = mp->studioStateObject();
    }
  }
  root["studio"] = studio;
  if (tools::Tool* tool = App::instance() ? App::instance()->activeTool() : nullptr)
    root["tool"] = tool->getId();
  if (context && context->isUIAvailable() && ToolBar::instance()) {
    json11::Json::array tb;
    for (auto& tr : ToolBar::instance()->visibleToolRects())
      tb.push_back(json11::Json::object{ { "id", tr.first->getId() },
                                         { "rect", rectJson(tr.second) } });
    root["toolbar"] = json11::Json::object{ { "tools", tb },
                                            { "studioGroupVisible",
                                              ToolBar::studioToolsVisible() } };
  }
  return json11::Json(root).dump();
}

void closeStudio(DocView* view)
{
  if (auto* surfaces = dynamic_cast<EasymetricMultiPaneView*>(view->multiPaneLayout())) {
    surfaces->syncAllLinkedCels();
    surfaces->prepareForExit();
  }
  else
    easymetric::prepareModelStudioForExit(view->multiPaneLayout());
  view->exitMultiPaneMode();
}

void switchStudioSource(DocView* view, bool model)
{
  closeStudio(view);
  if (model) {
    auto* studio = easymetric::createModelStudio(
      *view, [view] { switchStudioSource(view, false); }, [view] { closeStudio(view); });
    view->enterMultiPaneMode(studio, {});
  }
  else {
    setupSixSurfacesOnDoc(view->document(), true);
    // Migrate legacy per-object sizes and offsets to the shared model space.
    const auto unified = unifyObjectsOnDoc(view->document());
    auto* studio = new EasymetricMultiPaneView(view);
    view->enterMultiPaneMode(studio, studio->surfaceEditors());
    studio->initLayersAndFraming();
    if (auto* sb = StatusBar::instance()) {
      if (unified[0] > 0)
        sb->showTip(4000,
                    fmt::format("Objects now share one {} x {} x {} model space (undo to revert)",
                                unified[0], unified[1], unified[2]));
      else
        sb->showTip(1500, "Easymetric 6-Surface Studio: 4-Column Workspace Grid Active");
    }
  }
}

class EasymetricModelStudioCommand : public Command {
public:
  EasymetricModelStudioCommand() : Command(CommandId::EasymetricModelStudio()) {}

protected:
  bool onEnabled(Context* context) override
  {
    return context && context->isUIAvailable() && context->activeDocument();
  }

  void onExecute(Context*) override
  {
    auto* context = UIContext::instance();
    auto* view = context ? context->activeView() : nullptr;
    if (view && view->document())
      switchStudioSource(view, true);
  }
};

class EasymetricStudioCommand : public Command {
public:
  EasymetricStudioCommand() : Command(CommandId::EasymetricStudio()) {}

protected:
  void onLoadParams(const Params& params) override
  {
    m_params = params;
  }

  bool onEnabled(Context* context) override
  {
    return context && context->activeDocument();
  }

  bool onChecked(Context* context) override
  {
    if (!UIContext::instance())
      return false;
    DocView* view = UIContext::instance()->activeView();
    return view && view->isMultiPane();
  }

  void onExecute(Context* context) override
  {
    Doc* doc = context ? static_cast<Doc*>(context->activeDocument()) : nullptr;
    if (!doc || !doc->sprite())
      return;

    // Headless / Parameterized Execution
    if (m_params.has_param("action")) {
      const std::string action = m_params.get("action");
      // Objects: every action works on `object` (name or key); without it,
      // on the root object ("Object 1"), whatever the studio has focused.
      std::string objKey;
      if (m_params.has_param("object") &&
          !resolveObjectKey(doc->sprite(), m_params.get("object"), objKey)) {
        easymetricReportError(action, "no object named " + m_params.get("object"));
        return;
      }
      std::optional<EasymetricObjectScope> objScope;
      objScope.emplace(doc->sprite(), objKey);
      auto geti = [this](const char* k, int d) {
        return m_params.has_param(k) ? std::stoi(m_params.get(k)) : d;
      };
      // Size-changing actions first migrate a pre-2026-11-18 file (objects
      // with their own sizes / offsets) to one shared model size. Read-only
      // actions (state, previewStats, exportObj) see the file as it is.
      if (action == "newObject" || action == "modelSize" || action == "unifyObjects" ||
          action == "linkDims" || action == "blockLayout" || action == "deleteObject") {
        objScope.reset();
        const std::array<int, 3> u = unifyObjectsOnDoc(doc);
        objScope.emplace(doc->sprite(), objKey);
        if (action == "unifyObjects") {
          if (m_params.has_param("out")) {
            std::ofstream f(m_params.get("out"));
            f << json11::Json(json11::Json::object{
                                { "changed", u[0] > 0 },
                                { "modelSize", json11::Json::array{ u[0], u[1], u[2] } } })
                   .dump();
          }
          return;
        }
      }
      if (action == "modelSize") {
        // Params: width, height, depth, mode (stretch|keep, default keep).
        // Every object gets the new size (one undo step).
        int S[3];
        modelBox(doc->sprite(), S);
        int box[3] = { std::clamp(geti("width", S[0]), 1, 256),
                       std::clamp(geti("height", S[1]), 1, 256),
                       std::clamp(geti("depth", S[2]), 1, 256) };
        const ResizeMode mode = (m_params.has_param("mode") && m_params.get("mode") == "stretch")
                                  ? ResizeMode::Stretch
                                  : ResizeMode::Keep;
        objScope.reset();
        resizeModelOnDoc(doc, box, mode);
        return;
      }
      if (action == "newObject") {
        // Params: name, block (true|false, default false), bw, bh, bd (block
        // size; width/height/depth are aliases), place (above|below|right|
        // left|front|back|center, next to what `object` - or Object 1 - has
        // painted), r, g, b, frame. The object itself has the model size.
        NewObjectSpec spec;
        if (m_params.has_param("name"))
          spec.name = m_params.get("name");
        spec.fillBlock = m_params.has_param("block") && m_params.get("block") == "true";
        spec.block[0] = std::clamp(geti("bw", geti("width", 8)), 1, 256);
        spec.block[1] = std::clamp(geti("bh", geti("height", 8)), 1, 256);
        spec.block[2] = std::clamp(geti("bd", geti("depth", 8)), 1, 256);
        spec.frame = doc::frame_t(geti("frame", 0));
        spec.color = doc::rgba(geti("r", 34), geti("g", 32), geti("b", 52), 255);
        placeBlock(doc->sprite(), objKey, spec.frame, spec.block,
                   m_params.has_param("place") ? m_params.get("place") : "center", spec.blockOrg);
        std::string err;
        const std::string key = newObjectOnDoc(doc, spec, err);
        objScope.reset(); // the studio's own focus scope takes over
        if (key.empty())
          easymetricReportError("newObject", err);
        else if (context->isUIAvailable())
          focusStudioObject(key);
        return;
      }
      if (action == "setObject") {
        // Params: rank (-1 = auto), visible, name. Objects have no offset
        // any more (shared model size, 2026-11-18): ox/oy/oz are ignored.
        const int rank = geti("rank", -2);
        const bool vis = !(m_params.has_param("visible") && m_params.get("visible") == "false");
        const std::string name = m_params.has_param("name") ? m_params.get("name") : std::string();
        setObjectOnDoc(doc, objKey, nullptr, rank >= -1 ? &rank : nullptr,
                       m_params.has_param("visible") ? &vis : nullptr,
                       name.empty() ? nullptr : &name);
        return;
      }
      if (action == "previewStats") {
        // Params: yaw, pitch, out. Renders the preview headless (every
        // visible object when there are object groups) and reports the
        // screen pixels each object won: {"objects":[{"key","px"}],...}.
        const int yaw = geti("yaw", 45), pitch = geti("pitch", 30);
        Render3DResult res;
        std::vector<std::string> keys;
        using clk = std::chrono::steady_clock;
        const auto t0 = clk::now();
        auto t1 = t0;
        if (hasObjectGroups(doc->sprite())) {
          CompositeInput in;
          buildCompositeInput(doc, doc::frame_t(0), true, nullptr, std::string(), nullptr, in);
          t1 = clk::now();
          res = compute3DPreviewMulti(in.objects, yaw, pitch, 50, 50, 50, 0, true, true, nullptr,
                                      false);
          keys = in.keys;
        }
        else {
          SixSurfaceData data;
          extractSixSurfaces(doc, doc::frame_t(0), data, true);
          t1 = clk::now();
          res = compute3DPreviewImages(data, yaw, pitch, 50, 50, 50, 0, true, true);
          keys = { std::string() };
        }
        const auto t2 = clk::now();
        auto ms = [](clk::duration d) {
          return std::chrono::duration<double, std::milli>(d).count();
        };
        json11::Json::array objs;
        for (size_t i = 0; i < keys.size() && i < res.objHitPx.size(); ++i)
          objs.push_back(json11::Json::object{ { "key", keys[i] }, { "px", res.objHitPx[i] } });
        // FNV-1a of the diffuse image (hex): equal images, equal hashes. The
        // shared-size migration test compares it before/after (2026-11-18).
        uint64_t hash = 1469598103934665603ull;
        if (const doc::Image* im = res.diffImg.get())
          for (int y = 0; y < im->height(); ++y)
            for (int x = 0; x < im->width(); ++x) {
              hash ^= uint64_t(doc::get_pixel(im, x, y));
              hash *= 1099511628211ull;
            }
        // `raw`: also write the diffuse image as raw RGBA bytes (outW x outH),
        // for Lua: Image(outW, outH).bytes = <file>, then :saveAs(png).
        if (m_params.has_param("raw") && res.diffImg &&
            res.diffImg->pixelFormat() == doc::IMAGE_RGB) {
          std::ofstream rf(m_params.get("raw"), std::ios::binary | std::ios::trunc);
          const doc::Image* im = res.diffImg.get();
          for (int y = 0; y < im->height(); ++y)
            rf.write(reinterpret_cast<const char*>(im->getPixelAddress(0, y)),
                     std::streamsize(im->widthBytes()));
        }
        const std::string text = json11::Json(json11::Json::object{
                                                { "hash", fmt::format("{:016x}", hash) },
                                                { "outW", res.outW },
                                                { "outH", res.outH },
                                                { "cutoutPx", res.cutoutPx },
                                                { "extractMs", ms(t1 - t0) },
                                                { "renderMs", ms(t2 - t1) },
                                                { "volumeMs", res.volumeMs },
                                                { "objects", objs } })
                                   .dump();
        if (m_params.has_param("out") && !m_params.get("out").empty()) {
          std::ofstream f(m_params.get("out"), std::ios::binary | std::ios::trunc);
          f << text;
        }
        else {
          std::printf("%s\n", text.c_str());
          std::fflush(stdout);
        }
        return;
      }
      if (action == "exportObj") {
        // Params: out (path; .obj, with .mtl and .png next to it), frame (1-based).
        if (m_params.has_param("out"))
          exportEasymetricObj(context, doc, doc::frame_t(geti("frame", 1) - 1), m_params.get("out"),
                              50, 50, 50, true, nullptr);
        return;
      }
      if (action == "deleteObject") {
        if (objKey.empty())
          easymetricReportError("deleteObject", "Object 1 cannot be deleted");
        else
          deleteObjectOnDoc(doc, objKey);
        return;
      }
      if (action == "focusObject") {
        objScope.reset();
        if (context->isUIAvailable())
          focusStudioObject(objKey);
        return;
      }
      if (action == "linkDims") {
        std::string faceStr = m_params.has_param("face") ? m_params.get("face") : "top";
        int w = m_params.has_param("width") ? std::stoi(m_params.get("width")) : 32;
        int h = m_params.has_param("height") ? std::stoi(m_params.get("height")) : 32;
        int faceIdx = kFaceTop;
        if (faceStr == "top" || faceStr == "TOP") faceIdx = kFaceTop;
        else if (faceStr == "down" || faceStr == "bottom" || faceStr == "DOWN") faceIdx = kFaceDown;
        else if (faceStr == "front" || faceStr == "FRONT") faceIdx = kFaceFront;
        else if (faceStr == "back" || faceStr == "BACK") faceIdx = kFaceBack;
        else if (faceStr == "right" || faceStr == "RIGHT") faceIdx = kFaceRight;
        else if (faceStr == "left" || faceStr == "LEFT") faceIdx = kFaceLeft;

        propagateLinkedDimensions(doc, faceIdx, w, h);
      }
      else if (action == "blockLayout") {
        auto geti = [this](const char* k, int d) {
          return m_params.has_param(k) ? std::stoi(m_params.get(k)) : d;
        };
        const int bx = geti("width", 22), by = geti("height", 21), bz = geti("depth", 22);
        const BlockLayout b = planBlockLayout(bx, by, bz, geti("cellW", bx), geti("cellH", by),
                                              geti("cellD", bz));
        const doc::color_t col = doc::rgba(geti("r", 34), geti("g", 32), geti("b", 52), geti("a", 255));
        std::string err;
        if (!applyBlockLayout(doc, doc::frame_t(geti("frame", 0)), b, col, err))
          easymetricReportError("blockLayout", err);
      }
      else if (action == "setupSmartObjects") {
        setupTwoLayerStructure(doc, true);
      }
      else if (action == "convertToGroups") {
        convertSmartObjectsToGroups(doc);
      }
      else if (action == "setupSurfaces") {
        setupSixSurfacesOnDoc(doc, true);
      }
      else if (action == "setupSurfacesEasy") {
        setupSixSurfacesOnDoc(doc, true, true);
      }
      else if (action == "generateDepthFromBase") {
        generateDepthFromBaseOnDoc(doc, doc::frame_t(0));
      }
      else if (action == "depthDab") {
        // Params: face (top/down/front/back/right/left), u, v,
        // mode (plus/minus), radius (default 0 = single pixel).
        std::string faceStr = m_params.has_param("face") ? m_params.get("face") : "front";
        int faceIdx = kFaceFront;
        if (faceStr == "top" || faceStr == "TOP") faceIdx = kFaceTop;
        else if (faceStr == "down" || faceStr == "bottom" || faceStr == "DOWN") faceIdx = kFaceDown;
        else if (faceStr == "front" || faceStr == "FRONT") faceIdx = kFaceFront;
        else if (faceStr == "back" || faceStr == "BACK") faceIdx = kFaceBack;
        else if (faceStr == "right" || faceStr == "RIGHT") faceIdx = kFaceRight;
        else if (faceStr == "left" || faceStr == "LEFT") faceIdx = kFaceLeft;
        const int u = m_params.has_param("u") ? std::stoi(m_params.get("u")) : 0;
        const int v = m_params.has_param("v") ? std::stoi(m_params.get("v")) : 0;
        const bool minus = m_params.has_param("mode") && m_params.get("mode") == "minus";
        const int radius = m_params.has_param("radius") ? std::stoi(m_params.get("radius")) : 0;
        sculptDepthDabOnDoc(doc, doc::frame_t(0), faceIdx, u, v, minus, radius);
      }
      else if (action == "flipFace") {
        // Params: face (top/down/front/back/right/left), axis (h|v, default h),
        // frame (1-based like Lua, default = active frame).
        const std::string faceStr = m_params.has_param("face") ? m_params.get("face") : "top";
        int faceIdx = kFaceTop;
        for (int f = 0; f < kFaceCount; ++f) {
          std::string a = kFaceSpecs[f].sliceName, d = kFaceSpecs[f].displayName;
          std::string q = faceStr;
          for (auto* s : { &a, &d, &q })
            std::transform(s->begin(), s->end(), s->begin(), [](unsigned char c) {
              return char(std::tolower(c));
            });
          if (q == a || q == d || (q == "bottom" && f == kFaceDown))
            faceIdx = f;
        }
        const bool horizontal = !(m_params.has_param("axis") && m_params.get("axis") == "v");
        doc::frame_t fr = context->activeSite().frame();
        if (m_params.has_param("frame"))
          fr = doc::frame_t(std::max(1, std::stoi(m_params.get("frame"))) - 1);
        flipFaceOnDoc(doc, faceIdx, horizontal, fr);
      }
      else if (action == "state") {
        // Params: out (file path, optional). Writes the machine-readable
        // state JSON (docs/aseprite2/UITEST.md) to `out`; with no `out`
        // it is printed to stdout, so a headless run can be asserted on
        // without a screenshot or a file.
        const std::string text = easymetricStateJsonText(context);
        if (m_params.has_param("out") && !m_params.get("out").empty()) {
          std::ofstream f(m_params.get("out"), std::ios::binary | std::ios::trunc);
          f << text;
        }
        else {
          std::printf("%s\n", text.c_str());
          std::fflush(stdout);
        }
      }
      else if (action == "previewFloat") {
        // Params: mode (pop/dock/toggle, default toggle).
        // UI-only: needs the studio open, so it does nothing
        // headless. Added so the floating preview can be
        // driven (and GUI-tested) without hunting for the
        // Fields button under a floating panel.
        if (context->isUIAvailable()) {
          UIContext* uiCtx = UIContext::instance();
          DocView* docView = uiCtx ? uiCtx->activeView() : nullptr;
          if (docView && docView->isMultiPane()) {
            if (auto* multiPane = dynamic_cast<EasymetricMultiPaneView*>(docView->multiPaneLayout())) {
              const std::string mode = m_params.has_param("mode") ? m_params.get("mode") : "toggle";
              if (mode == "pop") multiPane->popOutPreview();
              else if (mode == "dock") multiPane->dockPreview();
              else if (multiPane->isPreviewFloating()) multiPane->dockPreview();
              else multiPane->popOutPreview();
            }
          }
        }
      }
      else if (action == "setupGroups") {
        setupTwoLayerStructure(doc, false);
      }
      else if (action == "bake3D" || action == "render3D") {
        int yaw = m_params.has_param("yaw") ? std::stoi(m_params.get("yaw")) : 45;
        int pitch = m_params.has_param("pitch") ? std::stoi(m_params.get("pitch")) : 30;
        bake3DToDoc(doc, yaw, pitch);
      }
      return;
    }

    if (!context->isUIAvailable())
      return;

    UIContext* uiCtx = UIContext::instance();
    DocView* docView = uiCtx ? uiCtx->activeView() : nullptr;
    if (!docView || !docView->document())
      return;

    if (docView->isMultiPane()) {
      closeStudio(docView);
      if (auto* sb = StatusBar::instance())
        sb->showTip(1200, "Easymetric Studio: Switched to standard view");
    }
    else
      switchStudioSource(docView, easymetric::hasEmbeddedModel(*doc->sprite()));
  }

private:
  // Objects: focus an object in the open studio (no-op without one).
  static void focusStudioObject(const std::string& key)
  {
    UIContext* uiCtx = UIContext::instance();
    DocView* view = uiCtx ? uiCtx->activeView() : nullptr;
    if (view && view->isMultiPane())
      if (auto* mp = dynamic_cast<EasymetricMultiPaneView*>(view->multiPaneLayout()))
        mp->focusObject(key);
  }

  Params m_params;
};

} // anonymous namespace

std::string easymetric_state_json(Context* context)
{
  return easymetricStateJsonText(context);
}

std::string easymetric_face_of_editor(Editor* editor)
{
  if (!editor || !UIContext::instance())
    return std::string();
  DocView* view = UIContext::instance()->activeView();
  if (!view || !view->isMultiPane())
    return std::string();
  auto* mp = dynamic_cast<EasymetricMultiPaneView*>(view->multiPaneLayout());
  const int f = mp ? mp->faceOfEditor(editor) : -1;
  return f >= 0 ? std::string(kFaceSpecs[f].sliceName) : std::string();
}

Command* CommandFactory::createEasymetricModelStudioCommand()
{
  return new EasymetricModelStudioCommand;
}
Command* CommandFactory::createEasymetricStudioCommand()
{
  return new EasymetricStudioCommand;
}

} // namespace app
