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
#include "app/cmd/set_slice_key.h"
#include "app/commands/command.h"
#include "app/commands/commands.h"
#include "app/console.h"
#include "app/context.h"
#include "app/doc.h"
#include "app/doc_api.h"
#include "app/extra_cel.h"
#include "app/file/file.h"
#include "app/file_selector.h"
#include "app/pref/preferences.h"
#include "app/site.h"
#include "app/tx.h"
#include "app/ui/color_button.h"
#include "app/ui/doc_view.h"
#include "ui/window.h"
#include "app/ui/editor/editor.h"
#include "app/ui/editor/editor_observer.h"
#include "app/ui/editor/standby_state.h"
#include "doc/mask.h"
#include "app/ui/editor/editor_view.h"
#include "app/ui/skin/skin_theme.h"
#include "app/ui/status_bar.h"
#include "app/ui_context.h"
#include "app/util/conversion_to_surface.h"
#include "base/fs.h"
#include "doc/algorithm/flip_type.h"
#include "doc/cel.h"
#include "doc/cels_range.h"
#include "doc/color.h"
#include "doc/frames_sequence.h"
#include "doc/image.h"
#include "doc/layer.h"
#include "doc/layer_smart_object.h"
#include "doc/primitives.h"
#include "doc/slice.h"
#include "doc/sprite.h"
#include "doc/tag.h"
#include "doc/tags.h"
#include "fmt/format.h"
#include "os/sampling.h"
#include "os/surface.h"
#include "os/system.h"
#include "render/render.h"
#include "ui/box.h"
#include "ui/button.h"
#include "ui/combobox.h"
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
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <memory>
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
    fw = std::clamp(fw, 4, 256);
    fh = std::clamp(fh, 4, 256);
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
    newW = std::clamp(newW, 4, 256);
    newH = std::clamp(newH, 4, 256);

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

  uint32_t sampleSymmetric(int face, int u, int v) const
  {
    const int sf = symmetricFace(face);
    return sample(sf, u, v);
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
  const int dimX = std::max({ data.w[kFaceFront], data.w[kFaceBack], data.w[kFaceTop], data.w[kFaceDown], 8 });
  const int dimY = std::max({ data.h[kFaceFront], data.h[kFaceBack], data.h[kFaceLeft], data.h[kFaceRight], 8 });
  const int dimZ = std::max({ data.w[kFaceLeft], data.w[kFaceRight], data.h[kFaceTop], data.h[kFaceDown], 8 });

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

  vol.vx = std::clamp(dimX + 2 * extX, 8, 64);
  vol.vy = std::clamp(dimY + 2 * extY, 8, 64);
  vol.vz = std::clamp(dimZ + 2 * extZ, 8, 64);
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

static doc::Slice* findSliceForFace(doc::Sprite* spr, int face)
{
  if (!spr || face < 0 || face >= kFaceCount)
    return nullptr;
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
  for (doc::Layer* lyr : spr->allLayers()) {
    if (lyr->isImage() && lyr->name() == name)
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

  if (!shared.baseLayer && !shared.textureLayer) {
    shared.baseLayer = findLayerByName(spr, "Base");
    if (!shared.baseLayer)
      shared.baseLayer = findLayerByName(spr, "base");

    if (!shared.baseLayer) {
      doc::LayerImage* soleImg = nullptr;
      int imgCount = 0;
      for (doc::Layer* lyr : spr->allLayers()) {
        if (lyr->isImage()) {
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
  return sharedUVRectExact(face, colW, rowH, w[face], h[face]);
}

// Exact sheet size for the current face sizes (0, 0 if no sprite).
static gfx::Size sharedSheetSize(doc::Sprite* spr)
{
  if (!spr) return gfx::Size(0, 0);
  int w[kFaceCount], h[kFaceCount];
  sharedFaceSizes(spr, w, h);
  int colW[3], rowH[2];
  sharedLayout(w, h, colW, rowH);
  return gfx::Size(colW[0] + colW[1] + colW[2], rowH[0] + rowH[1]);
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
static void rebuildSharedSheets(Doc* doc, const int newW[kFaceCount],
                                const int newH[kFaceCount])
{
  if (!doc || !doc->sprite()) return;
  doc::Sprite* spr = doc->sprite();
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
          const int copyW = std::min(oldRect[f].w, newUv.w);
          const int copyH = std::min(oldRect[f].h, newUv.h);
          for (int v = 0; v < copyH; ++v) {
            const int srcY = oldRect[f].y + v;
            if (srcY < 0 || srcY >= cel->image()->height()) continue;
            for (int u = 0; u < copyW; ++u) {
              const int srcX = oldRect[f].x + u;
              if (srcX < 0 || srcX >= cel->image()->width()) continue;
              doc::put_pixel(resized.get(), newUv.x + u, newUv.y + v,
                             doc::get_pixel(cel->image(), srcX, srcY));
            }
          }
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
    const gfx::Size exact = sharedSheetSize(spr);
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
        targetW[srcFace] = std::clamp(newW, 4, 256);
        targetH[srcFace] = std::clamp(newH, 4, 256);
      }
      int oldW[kFaceCount], oldH[kFaceCount];
      sharedFaceSizes(spr, oldW, oldH);
      const gfx::Size exact = sharedSheetSize(spr);
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

protected:
  void onSizeHint(ui::SizeHintEvent& ev) override
  {
    // Base Button measures the label text; keep its width, but force
    // the compact LinkToggleButton height so a row of them fits.
    ui::Button::onSizeHint(ev);
    gfx::Size sz = ev.sizeHint();
    const int s = ui::guiscale();
    sz.h = 14 * s;
    ev.setSizeHint(sz);
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
  out.init(std::max(sw, sh));

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
    const int fw = faceRect.w;
    const int fh = faceRect.h;
    out.bounds[f] = faceRect;
    out.setFaceSize(f, fw, fh);
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

  // Unify linked opposing face pairs (Top/Down, Front/Back, Right/Left)
  if (colLinked) {
    for (int c = 0; c < 3; ++c) {
      out.colLinked[c] = (*colLinked)[c];
      if ((*colLinked)[c]) {
        const int topFace = c * 2;
        const int btmFace = c * 2 + 1;
        out.pixels[btmFace] = out.pixels[topFace];
        out.depthPixels[btmFace] = out.depthPixels[topFace];
        out.depthAlpha[btmFace] = out.depthAlpha[topFace];
        out.baseAlpha[btmFace] = out.baseAlpha[topFace];
        out.hasBaseLayer[btmFace] = out.hasBaseLayer[topFace];
        out.w[btmFace] = out.w[topFace];
        out.h[btmFace] = out.h[topFace];
        out.bounds[btmFace] = out.bounds[topFace];
      }
    }
  }

  return true;
}

struct Render3DResult {
  doc::ImageRef diffImg;
  doc::ImageRef normImg;
  doc::ImageRef deptImg;
  int outW = 0;
  int outH = 0;
  float cxScreen = 0.0f;
  float cyScreen = 0.0f;
};

static Render3DResult compute3DPreviewImages(
  const SixSurfaceData& data,
  int yawDeg,
  int pitchDeg,
  int pivX,
  int pivY,
  int pivZ,
  int renderStyle = 0,
  bool addOutline = true,
  bool allowCutouts = true)
{
  Render3DResult res;
  const float halfX = std::max({ data.w[kFaceFront], data.w[kFaceBack], data.w[kFaceTop], data.w[kFaceDown] }) * 0.5f;
  const float halfY = std::max({ data.h[kFaceFront], data.h[kFaceBack], data.h[kFaceLeft], data.h[kFaceRight] }) * 0.5f;
  const float halfZ = std::max({ data.w[kFaceLeft], data.w[kFaceRight], data.h[kFaceTop], data.h[kFaceDown] }) * 0.5f;

  SculptedVoxelVolume vol = buildSculptedVolume(data, pivX, pivY, pivZ);

  // Render half-extents (2026-11-07, depth sculpting): when depth
  // sculpting grew the volume, the ray clip box grows with it so the
  // Base box keeps its world size and Plus bumps stick out beyond it
  // instead of squeezing the whole model. The voxel mapping below
  // uses these; the face-UV mapping keeps halfX/halfY/halfZ because
  // a face's lateral extent never changes. When nothing is sculpted
  // vol dims equal the base dims and these are exactly halfX/Y/Z.
  const int dimX = std::max({ data.w[kFaceFront], data.w[kFaceBack], data.w[kFaceTop], data.w[kFaceDown], 8 });
  const int dimY = std::max({ data.h[kFaceFront], data.h[kFaceBack], data.h[kFaceLeft], data.h[kFaceRight], 8 });
  const int dimZ = std::max({ data.w[kFaceLeft], data.w[kFaceRight], data.h[kFaceTop], data.h[kFaceDown], 8 });
  const float halfXr = (vol.vx > dimX) ? halfX * float(vol.vx) / float(dimX) : halfX;
  const float halfYr = (vol.vy > dimY) ? halfY * float(vol.vy) / float(dimY) : halfY;
  const float halfZr = (vol.vz > dimZ) ? halfZ * float(vol.vz) / float(dimZ) : halfZ;
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
  const float maxDepthRange = float(maxDim) * 1.732f;
  const float Lx = -0.45f, Ly = -0.60f, Lz = 0.66f;

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
      const float ox = (camX * rxX + camY * ryX + pivOffX) - camDist * vdX;
      const float oy = (camX * rxY + camY * ryY + pivOffY) - camDist * vdY;
      const float oz = (camX * rzX + camY * ryZ + pivOffZ) - camDist * vdZ;

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

      if (!clipAxis(ox, vdX, -halfXr, halfXr) ||
          !clipAxis(oy, vdY, -halfYr, halfYr) ||
          !clipAxis(oz, vdZ, -halfZr, halfZr) ||
          t1 < nearPlane || t0 > farPlane || t0 > t1) {
        continue;
      }

      const float stepSize = std::min({ (2.0f * halfXr) / float(vol.vx),
                                        (2.0f * halfYr) / float(vol.vy),
                                        (2.0f * halfZr) / float(vol.vz) }) * 0.45f;
      float tStart = std::max(nearPlane, t0);
      float tEnd = std::min(farPlane, t1);
      float tHit = -1.0f;
      int hitVx = -1, hitVy = -1, hitVz = -1;

      for (float t = tStart; t <= tEnd; t += stepSize) {
        const float pxW = ox + t * vdX;
        const float pyW = oy + t * vdY;
        const float pzW = oz + t * vdZ;

        int vx = int((pxW + halfXr) / (2.0f * halfXr) * float(vol.vx));
        int vy = int((pyW + halfYr) / (2.0f * halfYr) * float(vol.vy));
        int vz = int((pzW + halfZr) / (2.0f * halfZr) * float(vol.vz));

        if (vol.isSolid(vx, vy, vz)) {
          float ta = t - stepSize;
          float tb = t;
          for (int b = 0; b < 3; ++b) {
            float tm = (ta + tb) * 0.5f;
            int mx = int((ox + tm * vdX + halfXr) / (2.0f * halfXr) * float(vol.vx));
            int my = int((oy + tm * vdY + halfYr) / (2.0f * halfYr) * float(vol.vy));
            int mz = int((oz + tm * vdZ + halfZr) / (2.0f * halfZr) * float(vol.vz));
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

      float gX = float(vol.isSolid(hitVx - 1, hitVy, hitVz)) - float(vol.isSolid(hitVx + 1, hitVy, hitVz));
      float gY = float(vol.isSolid(hitVx, hitVy - 1, hitVz)) - float(vol.isSolid(hitVx, hitVy + 1, hitVz));
      float gZ = float(vol.isSolid(hitVx, hitVy, hitVz - 1)) - float(vol.isSolid(hitVx, hitVy, hitVz + 1));
      float len = std::sqrt(gX * gX + gY * gY + gZ * gZ);
      if (len > 1e-4f) {
        gX /= len; gY /= len; gZ /= len;
      }
      else {
        gX = (ox + tHit * vdX) / halfX;
        gY = (oy + tHit * vdY) / halfY;
        gZ = (oz + tHit * vdZ) / halfZ;
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
          u = std::clamp(int((hx + halfX) / (2.0f * halfX) * fw), 0, fw - 1);
          v = std::clamp(int((hy + halfY) / (2.0f * halfY) * fh), 0, fh - 1);
          break;
        case kFaceBack:
          u = std::clamp(int((halfX - hx) / (2.0f * halfX) * fw), 0, fw - 1);
          v = std::clamp(int((hy + halfY) / (2.0f * halfY) * fh), 0, fh - 1);
          break;
        case kFaceLeft:
          u = std::clamp(int((hz + halfZ) / (2.0f * halfZ) * fw), 0, fw - 1);
          v = std::clamp(int((hy + halfY) / (2.0f * halfY) * fh), 0, fh - 1);
          break;
        case kFaceRight:
          u = std::clamp(int((halfZ - hz) / (2.0f * halfZ) * fw), 0, fw - 1);
          v = std::clamp(int((hy + halfY) / (2.0f * halfY) * fh), 0, fh - 1);
          break;
        case kFaceTop:
          u = std::clamp(int((hx + halfX) / (2.0f * halfX) * fw), 0, fw - 1);
          v = std::clamp(int((hz + halfZ) / (2.0f * halfZ) * fh), 0, fh - 1);
          break;
        case kFaceDown:
          u = std::clamp(int((hx + halfX) / (2.0f * halfX) * fw), 0, fw - 1);
          v = std::clamp(int((halfZ - hz) / (2.0f * halfZ) * fh), 0, fh - 1);
          break;
      }

      const uint32_t texCol = data.sampleSymmetric(face, u, v);
      const uint8_t a = doc::rgba_geta(texCol);
      if (a <= 16 && renderStyle != kRenderColoredFaces) {
        if (allowCutouts) continue;
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
      faceMap[py * outW + px] = face;
    }
  }

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

static void bake3DToDoc(Doc* doc, int yawDeg = 45, int pitchDeg = 30)
{
  if (!doc || !doc->sprite()) return;
  SixSurfaceData data;
  extractSixSurfaces(doc, doc::frame_t(0), data, true);
  Render3DResult res = compute3DPreviewImages(data, yawDeg, pitchDeg, 50, 50, 50, 0, true, true);
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
class EasymetricWheelZoomState : public StandbyState {
public:
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
    const gfx::Rect savedClip = editor->clipSpriteBounds();
    editor->setClipSpriteBounds(gfx::Rect());
    editor->setZoomAndCenterInMouse(zoom, msg->position(),
                                    Editor::ZoomBehavior::MOUSE);
    editor->setClipSpriteBounds(savedClip);
    return true;
  }

private:
  double m_wheelAccum = 0.0;
};

// Depth-sculpt state for surface panels. Inherits the wheel-zoom
// behaviour, and intercepts left-button strokes so they sculpt
// the Surface_DEPTH layer instead of painting with the active
// tool. The actual sculpting lives in EasymetricMultiPaneView
// (paintDepthDab / endDepthStroke, via the callbacks below),
// because only the view knows which panel editor is which face
// and holds the tool-strip Depth Sculpt settings.
class EasymetricDepthState : public EasymetricWheelZoomState {
public:
  using PaintFn = std::function<void(Editor*, const gfx::Point&, bool)>;
  using EndFn = std::function<void()>;
  EasymetricDepthState(PaintFn paint, EndFn end)
    : m_paint(std::move(paint))
    , m_end(std::move(end))
  {
  }
  bool onMouseDown(Editor* editor, ui::MouseMessage* msg) override
  {
    if (msg->left()) {
      m_paint(editor, msg->position(), true);
      return true;
    }
    return false;
  }
  bool onMouseMove(Editor* editor, ui::MouseMessage* msg) override
  {
    if (editor->hasCapture()) {
      m_paint(editor, msg->position(), false);
      return true;
    }
    return false;
  }
  bool onMouseUp(Editor* editor, ui::MouseMessage* msg) override
  {
    (void)editor;
    (void)msg;
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
      api.restackLayerBefore(depthLayer, spr->root(), shared.baseLayer);

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
static std::pair<bool, bool> applyDepthDabToImages(doc::Image* depthWork,
                                                   doc::Image* baseWork,
                                                   const gfx::Rect& faceRect,
                                                   int u0, int v0,
                                                   bool minus, float step, int radius)
{
  bool depthChanged = false, baseChanged = false;
  for (int dy = -radius; dy <= radius; ++dy) {
    for (int dx = -radius; dx <= radius; ++dx) {
      if (dx * dx + dy * dy > radius * radius) continue;
      const int u = u0 + dx, v = v0 + dy;
      if (u < 0 || v < 0 || u >= faceRect.w || v >= faceRect.h) continue;
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

class EasymetricMultiPaneView : public ui::Widget, public EditorObserver {
public:
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
      // Added in priority order (rightmost first): Link, Sym X,
      // Sym Y, Flip H, Flip V. The header hides lower-priority
      // ones when it is too narrow. Labels are abbreviated (SX =
      // Symmetry X, SY = Symmetry Y, FH = Flip Horizontal, FV =
      // Flip Vertical) so all five fit in a face header alongside
      // the title tab and, on top faces, the dimension-link button;
      // the Fields panel still shows the full "Sym X"/"Sym Y" names.
      m_animLinkButtons[f] = new PaneActionButton("Link", true);
      m_animLinkButtons[f]->setOn(true);
      m_animLinkButtons[f]->Click.connect([this, f] {
        setAnimLinked(f, !m_animLinked[f]);
      });
      m_headers[f]->addActionButton(m_animLinkButtons[f]);

      m_symXButtons[f] = new PaneActionButton("SX", true);
      m_symXButtons[f]->Click.connect([this, f] { toggleSymmetryAxis(f, true); });
      m_headers[f]->addActionButton(m_symXButtons[f]);

      m_symYButtons[f] = new PaneActionButton("SY", true);
      m_symYButtons[f]->Click.connect([this, f] { toggleSymmetryAxis(f, false); });
      m_headers[f]->addActionButton(m_symYButtons[f]);

      m_flipHButtons[f] = new PaneActionButton("FH", false);
      m_flipHButtons[f]->Click.connect([this, f] { flipFace(f, true); });
      m_headers[f]->addActionButton(m_flipHButtons[f]);

      m_flipVButtons[f] = new PaneActionButton("FV", false);
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
      // drawing/moving states return to.
      ed->setState(EditorStatePtr(new EasymetricWheelZoomState));

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
    buildToolStrip();

    // Timer for real-time 3D model update
    m_timer.Tick.connect([this] { pollAndRender(false); });
    m_timer.start();

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
        auto syncRect = [&](doc::LayerImage* lyr) {
          if (!lyr) return;
          for (doc::frame_t fr = doc::frame_t(0); fr <= spr->lastFrame(); ++fr) {
            doc::Cel* cel = lyr->cel(fr);
            if (!cel || !cel->image()) continue;
            doc::ImageRef copyImg(doc::Image::createCopy(cel->image()));
            for (int v = 0; v < srcRect.h && v < dstRect.h; ++v)
              for (int u = 0; u < srcRect.w && u < dstRect.w; ++u)
                doc::put_pixel(copyImg.get(), dstRect.x + u, dstRect.y + v,
                               doc::get_pixel(copyImg.get(), srcRect.x + u, srcRect.y + v));
            api.replaceImage(spr, cel->imageRef(), copyImg);
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
    if (editor == activeEd) {
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
    doc::Sprite* spr = doc->sprite();
    const gfx::Rect faceRect = sharedFaceRect(spr, face);
    if (faceRect.isEmpty())
      return;
    const doc::frame_t fr =
      (m_docView->editor() ? m_docView->editor()->frame() : doc::frame_t(0));
    SharedSurfaceLayers shared = findSharedLayers(spr);
    doc::LayerImage* layers[3] = {
      shared.baseLayer, shared.textureLayer, shared.depthLayer
    };
    bool any = false;
    for (doc::LayerImage* lyr : layers) {
      if (lyr && lyr->cel(fr) && lyr->cel(fr)->image()) {
        any = true;
        break;
      }
    }
    if (!any)
      return;
    Tx tx(doc,
          horizontal ? "Flip Easymetric Face Horizontally"
                     : "Flip Easymetric Face Vertically",
          ModifyDocument);
    const doc::algorithm::FlipType type = horizontal
      ? doc::algorithm::FlipHorizontal
      : doc::algorithm::FlipVertical;
    for (doc::LayerImage* lyr : layers) {
      if (!lyr)
        continue;
      doc::Cel* cel = lyr->cel(fr);
      if (!cel || !cel->image())
        continue;
      tx(new cmd::FlipImage(cel->image(), faceRect, type));
    }
    tx.commit();
    m_lastHash = 0;
    pollAndRender(true);
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
      return EditorStatePtr(new EasymetricDepthState(
        [this](Editor* ed, const gfx::Point& pt, bool down) { paintDepthDab(ed, pt, down); },
        [this] { endDepthStroke(); }));
    }
    return EditorStatePtr(new EasymetricWheelZoomState);
  }

  bool depthBrushOn() const
  {
    return m_depthToolOn;
  }

  // Switch the studio tool between Draw and Depth Sculpt, from the
  // tool strip. Ends any depth stroke in progress and re-states
  // every surface panel editor, exactly as the old Fields Depth
  // Brush checkbox handler did.
  void setToolMode(bool depth)
  {
    if (m_depthToolOn == depth) {
      if (m_drawToolBtn) m_drawToolBtn->setOn(!depth);
      if (m_depthToolBtn) m_depthToolBtn->setOn(depth);
      return;
    }
    endDepthStroke();
    m_depthToolOn = depth;
    if (m_drawToolBtn) m_drawToolBtn->setOn(!depth);
    if (m_depthToolBtn) m_depthToolBtn->setOn(depth);
    if (m_depthModeCombo) m_depthModeCombo->setEnabled(depth);
    if (m_depthBrushSlider) m_depthBrushSlider->setEnabled(depth);
    for (int f = 0; f < kFaceCount; ++f) {
      Editor* ed = m_editors[f];
      if (ed && !ed->hasCapture())
        ed->setState(newPanelState());
    }
  }

  // Build the studio tool strip: Draw / Depth Sculpt tools plus the
  // depth Plus/Minus mode and brush size. The strip sits across the
  // top of the studio (see onResize); nothing depth-tool related
  // remains in Fields.
  void buildToolStrip()
  {
    const int s = ui::guiscale();
    m_toolStrip = new ui::HBox;
    m_toolStrip->setChildSpacing(4 * s);
    m_drawToolBtn = new PaneActionButton("Draw", true);
    m_drawToolBtn->setOn(true);
    m_drawToolBtn->Click.connect([this] { setToolMode(false); });
    m_depthToolBtn = new PaneActionButton("Depth Sculpt", true);
    m_depthToolBtn->setOn(false);
    m_depthToolBtn->Click.connect([this] { setToolMode(true); });
    m_depthModeCombo = new ui::ComboBox();
    m_depthModeCombo->addItem("Plus (bump)");
    m_depthModeCombo->addItem("Minus (carve)");
    m_depthModeCombo->setEnabled(false);
    m_depthBrushSlider = new ui::Slider(1, 16, 3);
    m_depthBrushSlider->setExpansive(true);
    m_depthBrushSlider->setEnabled(false);
    m_toolStrip->addChild(new ui::Label("Tool:"));
    m_toolStrip->addChild(m_drawToolBtn);
    m_toolStrip->addChild(m_depthToolBtn);
    m_toolStrip->addChild(m_depthModeCombo);
    m_toolStrip->addChild(new ui::Label("Brush:"));
    m_toolStrip->addChild(m_depthBrushSlider);
    addChild(m_toolStrip);
  }

  // Depth-sculpt tool (William's spec, 2026-11-04; tool-strip
  // placement 2026-11-07: the tool lives in the studio toolbar,
  // not in Fields).
  // PLUS = bump, whiter towards the user; MINUS = carve, darker
  // into the plane. 128 = the main surface. 1 step = 1 pixel of
  // depth, so the gray step is 128 / D, D = pixels from surface
  // to box centre along the face normal (per-axis: Front/Back
  // use depth/2, Top/Bottom use height/2, Right/Left use
  // width/2). Black 0 = deepest, single-pixel thinness at the
  // centre; one more MINUS step past black = through-hole:
  // depth becomes transparent AND the Base pixel is cleared,
  // so the hole is real, not just shading.
  void paintDepthDab(Editor* ed, const gfx::Point& screenPt, bool isDown)
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

    const doc::frame_t fr = (m_docView->editor() ? m_docView->editor()->frame() : doc::frame_t(0));
    const gfx::Point sp = ed->screenToEditor(screenPt);
    const gfx::Rect faceRect = sharedFaceRect(spr, face);
    const int u0 = sp.x - faceRect.x;
    const int v0 = sp.y - faceRect.y;
    if (u0 < 0 || v0 < 0 || u0 >= faceRect.w || v0 >= faceRect.h) return;

    int fw[kFaceCount], fh[kFaceCount];
    sharedFaceSizes(spr, fw, fh);
    int normalSize = 0;
    if (face == kFaceTop || face == kFaceDown)
      normalSize = fh[kFaceFront];
    else if (face == kFaceFront || face == kFaceBack)
      normalSize = fw[kFaceRight];
    else
      normalSize = fw[kFaceFront];
    const int D = std::max(1, normalSize / 2);
    const float step = 128.0f / float(D);
    const bool minus = (m_depthModeCombo && m_depthModeCombo->getSelectedItemIndex() == 1);
    const int brush = m_depthBrushSlider ? m_depthBrushSlider->getValue() : 3;
    const int radius = std::max(0, (brush - 1) / 2);

    try {
      if (isDown || !m_depthStrokeActive) {
        endDepthStroke();
        // The depth map must exist BEFORE the stroke Tx opens:
        // generateDepthFromBaseOnDoc runs its own Tx, so it cannot
        // be called once the stroke Tx is open. A blank transparent
        // depth image is NOT a substitute — transparent depth reads
        // as a through-hole everywhere, which empties the preview
        // volume the moment the first dab lands (the cube vanished
        // after the first GUI depth drag; the headless depthDab
        // never had this bug because it generates first).
        if (!shared.depthLayer || !shared.depthLayer->cel(fr)) {
          if (!generateDepthFromBaseOnDoc(doc, fr)) return;
          shared = findSharedLayers(spr);
          if (!shared.depthLayer || !shared.depthLayer->cel(fr)) return;
        }
        // A depth layer created by an older build sits ON TOP of
        // Base and paints its flat grey over the panels (see
        // generateDepthFromBaseOnDoc). Restack it below Base now,
        // inside the stroke Tx. Do NOT hide it instead: a hidden
        // layer's pixels cannot be edited (probe 9, build5).
        m_depthTx.reset(new Tx(doc, "Depth Sculpt", ModifyDocument));
        DocApi api = doc->getApi(*m_depthTx);
        if (shared.depthLayer && shared.baseLayer)
          api.restackLayerBefore(shared.depthLayer, spr->root(), shared.baseLayer);
        doc::LayerImage* depthLayer = shared.depthLayer;
        doc::Cel* depthCel = depthLayer ? depthLayer->cel(fr) : nullptr;
        doc::Cel* baseCel = shared.baseLayer->cel(fr);
        if (!depthCel || !baseCel) {
          endDepthStroke();
          return;
        }
        m_depthLive = depthCel->imageRef();
        m_depthWork.reset(doc::Image::createCopy(m_depthLive.get()));
        m_baseLive = baseCel->imageRef();
        m_baseWork.reset(doc::Image::createCopy(m_baseLive.get()));
        m_depthStrokeActive = true;
      }
      if (!m_depthTx || !m_depthWork || !m_baseWork) return;

      const auto changed = applyDepthDabToImages(m_depthWork.get(), m_baseWork.get(),
                                               faceRect, u0, v0, minus, step, radius);
      const bool depthChanged = changed.first, baseChanged = changed.second;
      if (!depthChanged && !baseChanged) return;
      DocApi api = doc->getApi(*m_depthTx);
      if (depthChanged) {
        doc::ImageRef next(doc::Image::createCopy(m_depthWork.get()));
        api.replaceImage(spr, m_depthLive, next);
        m_depthLive = next;
        m_depthWork = next;
      }
      if (baseChanged) {
        doc::ImageRef next(doc::Image::createCopy(m_baseWork.get()));
        api.replaceImage(spr, m_baseLive, next);
        m_baseLive = next;
        m_baseWork = next;
      }
      doc->notifyGeneralUpdate();
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
    m_depthStrokeActive = false;
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
    const int totalW = std::max(16, rc.w - 3 * bw);
    const int totalH = std::max(16, rc.h - bh);
    const int minCol = std::min(64 * s, totalW / 4);
    const int minRow = std::min(64 * s, totalH / 2);

    if (!m_colInit) {
      m_colWpx[0] = totalW / 4;
      m_colWpx[1] = totalW / 4;
      m_colWpx[2] = totalW / 4;
      for (int c = 0; c < 4; ++c)
        m_rowHpx[c] = totalH / 2;
      m_colInit = true;
    }

    // 2026-11-04, William: surface splitters are REMOVED.
    // Sizing now lives in Fields (W/H/D, Size sliders), so
    // dragging a pane boundary only entangled drawing: the
    // resize cursor appeared over surface panels, a drag
    // started from a gap could resize a panel mid-stroke,
    // and the hit area was never where the bar looked to be.
    // Surface columns are now fixed equal widths and surface
    // rows fixed equal heights. The ONLY remaining splitter
    // is Preview/Fields (column 3, horizontal), which is not
    // a surface panel, so it can never entangle a drawing
    // stroke. Its stored split is rowHpx[3] only.
    colW[0] = totalW / 4;
    colW[1] = totalW / 4;
    colW[2] = totalW / 4;
    colW[3] = std::max(minCol, totalW - colW[0] - colW[1] - colW[2]);

    colX[0] = rc.x;
    colX[1] = rc.x + colW[0] + bw;
    colX[2] = rc.x + colW[0] + bw + colW[1] + bw;
    colX[3] = rc.x + colW[0] + bw + colW[1] + bw + colW[2] + bw;

    for (int c = 0; c < 4; ++c) {
      if (c == 3) {
        colRowTopH[c] = std::clamp(m_rowHpx[c], minRow, std::max(minRow, totalH - minRow));
      }
      else {
        colRowTopH[c] = totalH / 2;
      }
      colRowBotH[c] = std::max(minRow, totalH - colRowTopH[c]);
      colRowBotY[c] = rc.y + colRowTopH[c] + bh;
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
  // Only remaining splitter: 13 = horizontal boundary
  // between Preview and Fields in column 3. Everything
  // else returns 0, so no resize cursor ever appears over
  // a surface panel, and no surface-panel drag can start.
  // The hit area is exactly the painted bar, no extension
  // into either neighbouring pane.
  int splitterGapAt(const gfx::Point& pos) const
  {
    const gfx::Rect rc = contentBounds();
    const int s = std::max(1, ui::guiscale());
    const int bh = 6 * s;
    int colX[4], colW[4], colTopH[4], colBotY[4], colBotH[4];
    computePaneLayout(rc, colX, colW, colTopH, colBotY, colBotH);
    if (!rc.contains(pos))
      return 0;
    if (pos.x >= colX[3] && pos.x < colX[3] + colW[3] && pos.y >= colBotY[3] - bh &&
        pos.y < colBotY[3])
      return 13;
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
          m_dragStart = mouseMsg->position();
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
          const int totalW = std::max(16, rc.w - 3 * bw);
          const int totalH = std::max(16, rc.h - bh);
          // Pixel drag, adjacent pair only: the other columns'
          // stored pixel widths are not touched, so stretching
          // Preview (boundary 3) changes only Right/Left + Preview,
          // never the other two surface columns, and a window
          // resize (which only changes totalW) changes only
          // Preview via computePaneLayout.
          const int minColPx = std::min(64 * s, totalW / 4);
          const int minRowPx = std::min(64 * s, totalH / 2);
          if (m_dragSplitter < 10 && m_dragSplitter < 4) {
            const int deltaPx = mouseMsg->position().x - m_dragStart.x;
            const int left = m_dragSplitter - 1;
            int pair = 0;
            if (m_dragSplitter < 3)
              pair = m_dragColWpx[left] + m_dragColWpx[m_dragSplitter];
            else
              pair = m_dragColWpx[left] + m_dragPreviewW;
            // std::clamp is undefined when its upper bound is
            // below its lower bound. That happens when a pair
            // of narrow columns is smaller than twice the
            // minimum column width (small window, or HiDPI
            // guiscale making 64*guiscale large): dragging
            // that boundary then fed a shrinking / negative
            // width into the layout, which is exactly the
            // "panel shrinks, then it crashes at the end"
            // pattern. Clamp the upper bound up to the lower
            // bound first, so a drag can never produce a
            // negative or below-minimum column width.
            const int hiCol = std::max(minColPx, pair - minColPx);
            const int newLeft = std::clamp(m_dragColWpx[left] + deltaPx,
                                           minColPx, hiCol);
            m_colWpx[left] = newLeft;
            if (m_dragSplitter < 3)
              m_colWpx[m_dragSplitter] = pair - newLeft;
            // Boundary 3: Preview is the remainder, so it follows
            // automatically from the new left width.
          }
          else {
            const int deltaPx = mouseMsg->position().y - m_dragStart.y;
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
    m_panes[6]->setVisible(true);
    m_panes[7]->setVisible(true);
    m_panes[6]->setBounds(gfx::Rect(colX[3], contentRc.y, colW[3], colTopH[3]));
    m_panes[7]->setBounds(gfx::Rect(colX[3], colBotY[3], colW[3], colBotH[3]));

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

    // Horizontal separators, one per column (its own row split),
    // only for columns that are NOT linked (and Col 3).
    // The bar must sit exactly in the gap between the panes:
    // [colBotY-bh, colBotY), which is also what splitterGapAt()
    // hit-tests. It used to be painted centred on colBotY,
    // [colBotY-bh/2, colBotY+bh/2), so the resize cursor
    // appeared half a gap above the bar you see, and the
    // lower half of the visible bar did not resize at all —
    // the "cursor changes at a different place" report
    // (2026-11-04, William's screenshots). Vertical bars
    // already matched their gap, so they are unchanged.
    // Only remaining splitter bar: Preview/Fields, column 3.
    // Surface columns have no bars and no hit area any more,
    // so their boundaries are just the dark surround.
    {
      const bool active = (m_hoverSplitter == 13 || m_dragSplitter == 13);
      g->fillRect(active ? activeCol : sepCol,
                  gfx::Rect(colX[3], colBotY[3] - bh, colW[3], bh));
      const int midX = colX[3] + colW[3] / 2;
      const int midY = colBotY[3] - bh / 2;
      g->fillRect(active ? activeCol : gripCol, gfx::Rect(midX - 12 * s, midY - 1 * s, 24 * s, 2 * s));
    }
  }

private:
  void buildFieldsControls()
  {
    const int s = ui::guiscale();
    m_fieldsBox->setBorder(gfx::Border(4 * s));
    m_fieldsBox->setChildSpacing(3 * s);

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

    // Row 5: Stroke Outline + Cutouts + Lock Borders + Symmetry X/Y
    auto* symRow = new ui::HBox;
    symRow->setChildSpacing(4 * s);
    m_strokeCheck = new ui::CheckBox("Stroke Outline");
    m_cutoutCheck = new ui::CheckBox("Cutouts");
    m_lockBordersCheck = new ui::CheckBox("Lock Borders");
    m_symXCheck = new ui::CheckBox("Sym X");
    m_symYCheck = new ui::CheckBox("Sym Y");
    auto* centerSymBtn = new ui::Button("Center Sym");
    m_strokeCheck->setSelected(true);
    m_cutoutCheck->setSelected(true);

    m_linkDimensionsCheck = new ui::CheckBox("Link Dims");
    m_linkDimensionsCheck->setSelected(true);

    symRow->addChild(m_strokeCheck);
    symRow->addChild(m_cutoutCheck);
    symRow->addChild(m_lockBordersCheck);
    symRow->addChild(m_linkDimensionsCheck);
    symRow->addChild(m_symXCheck);
    symRow->addChild(m_symYCheck);
    symRow->addChild(centerSymBtn);
    m_fieldsBox->addChild(symRow);

    // Row 5b: Linked Dimension Controls
    auto* dimRow = new ui::HBox;
    dimRow->setChildSpacing(4 * s);
    auto* faceDimCombo = new ui::ComboBox;
    faceDimCombo->addItem("Top (X/Z)");
    faceDimCombo->addItem("Bottom (X/Z)");
    faceDimCombo->addItem("Front (X/Y)");
    faceDimCombo->addItem("Back (X/Y)");
    faceDimCombo->addItem("Right (Z/Y)");
    faceDimCombo->addItem("Left (Z/Y)");
    faceDimCombo->setSelectedItemIndex(0);

    auto* dimWSlider = new ui::Slider(4, 128, 24);
    auto* dimHSlider = new ui::Slider(4, 128, 24);
    dimWSlider->setExpansive(true);
    dimHSlider->setExpansive(true);
    auto* applyDimBtn = new ui::Button("Apply Size");

    dimRow->addChild(new ui::Label("Size:"));
    dimRow->addChild(faceDimCombo);
    dimRow->addChild(dimWSlider);
    dimRow->addChild(dimHSlider);
    dimRow->addChild(applyDimBtn);
    m_fieldsBox->addChild(dimRow);

    applyDimBtn->Click.connect([this, faceDimCombo, dimWSlider, dimHSlider] {
      Doc* doc = m_docView->document();
      if (!doc || !doc->sprite()) return;
      int selFace = faceDimCombo->getSelectedItemIndex();
      int nw = dimWSlider->getValue();
      int nh = dimHSlider->getValue();
      SixSurfaceData data;
      extractSixSurfaces(doc, 0, data);
      propagateLinkedDimensions(doc, data, selFace, nw, nh);
      initLayersAndFraming();
      m_lastHash = 0;
      pollAndRender(true);
      if (auto* sb = StatusBar::instance())
        sb->showTip(1200, "Propagated linked dimensions across surfaces");
    });

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
    auto* objXSlider = new ui::Slider(4, 256, curX);
    auto* objYSlider = new ui::Slider(4, 256, curY);
    auto* objZSlider = new ui::Slider(4, 256, curZ);
    objXSlider->setExpansive(true);
    objYSlider->setExpansive(true);
    objZSlider->setExpansive(true);
    auto* applyObjBtn = new ui::Button("Apply Object Size");
    objRow->addChild(new ui::Label("Object W/H/D:"));
    objRow->addChild(objXSlider);
    objRow->addChild(objYSlider);
    objRow->addChild(objZSlider);
    objRow->addChild(applyObjBtn);
    m_fieldsBox->addChild(objRow);

    auto* easyStartBtn = new ui::Button("Easy Start (1 pixel)");
    objRow->addChild(easyStartBtn);
    easyStartBtn->Click.connect([this] {
      Doc* doc = m_docView->document();
      if (!doc || !doc->sprite()) return;
      setupSixSurfacesOnDoc(doc, true, true);
      initLayersAndFraming();
      m_lastHash = 0;
      pollAndRender(true);
      if (auto* sb = StatusBar::instance())
        sb->showTip(1200, "Easy start: 1 central pixel per face");
    });

    applyObjBtn->Click.connect([this, objXSlider, objYSlider, objZSlider] {
      Doc* doc = m_docView->document();
      if (!doc || !doc->sprite()) return;
      const int X = std::clamp(objXSlider->getValue(), 4, 256);
      const int Y = std::clamp(objYSlider->getValue(), 4, 256);
      const int Z = std::clamp(objZSlider->getValue(), 4, 256);
      SixSurfaceData data;
      extractSixSurfaces(doc, 0, data);
      data.w[kFaceTop] = X;
      data.h[kFaceTop] = Z;
      data.w[kFaceDown] = X;
      data.h[kFaceDown] = Z;
      data.w[kFaceFront] = X;
      data.h[kFaceFront] = Y;
      data.w[kFaceBack] = X;
      data.h[kFaceBack] = Y;
      data.w[kFaceRight] = Z;
      data.h[kFaceRight] = Y;
      data.w[kFaceLeft] = Z;
      data.h[kFaceLeft] = Y;
      // srcFace = -1: do not override one face, apply the
      // whole data set above to every face at once.
      propagateLinkedDimensions(doc, data, -1, 0, 0);
      initLayersAndFraming();
      m_lastHash = 0;
      pollAndRender(true);
      if (auto* sb = StatusBar::instance())
        sb->showTip(1200, "Applied object size to all faces");
    });

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
    auto* floatRow = new ui::HBox;
    floatRow->setChildSpacing(4 * s);
    m_previewPopoutBtn = new ui::Button("Pop out Column");
    m_previewPopoutBtn->Click.connect([this] {
      if (m_previewFloating) dockPreview();
      else popOutPreview();
    });
    floatRow->addChild(m_previewPopoutBtn);
    m_fieldsBox->addChild(floatRow);

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

    m_lockBordersCheck->Click.connect([this] {
      const bool locked = m_lockBordersCheck->isSelected();
      Doc* doc = m_docView->document();
      if (doc && doc->sprite()) {
        for (int f = 0; f < kFaceCount; ++f) {
          if (doc::Slice* sl = findSliceForFace(doc->sprite(), f))
            sl->userData().setText(locked ? "locked" : "");
        }
        for (Editor* ed : m_editors) {
          if (ed) ed->invalidate();
        }
      }
      if (auto* sb = StatusBar::instance())
        sb->showTip(1200, locked ? "Surface borders locked" : "Surface borders unlocked");
    });

    m_linkDimensionsCheck->Click.connect([this] {
      if (auto* sb = StatusBar::instance()) {
        sb->showTip(1200, m_linkDimensionsCheck->isSelected() ?
                    "Linked surface dimensions active" :
                    "Linked surface dimensions disabled");
      }
    });

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

    // Fields checkboxes act on the ACTIVE panel (see
    // applyPanelSymmetry): the checkbox Click reads its own new
    // state and stores it as that panel's state. With no panel
    // active yet, they act on Top (face 0). The checkboxes always
    // show the active panel's state.
    auto applySymmetry = [this] {
      if (!m_symXCheck || !m_symYCheck)
        return;
      initPanelSymmetry();
      const int face = (m_lastActiveFace >= 0) ? m_lastActiveFace : 0;
      m_symXFace[face] = m_symXCheck->isSelected();
      m_symYFace[face] = m_symYCheck->isSelected();
      if (m_symXButtons[face])
        m_symXButtons[face]->setOn(m_symXFace[face]);
      if (m_symYButtons[face])
        m_symYButtons[face]->setOn(m_symYFace[face]);
      applyPanelSymmetry(face);
    };

    m_symXCheck->Click.connect([applySymmetry] { applySymmetry(); });
    m_symYCheck->Click.connect([applySymmetry] { applySymmetry(); });
    // "Centre" re-applies the active panel's face-centre axis.
    centerSymBtn->Click.connect([this] {
      applyPanelSymmetry((m_lastActiveFace >= 0) ? m_lastActiveFace : 0);
    });

    // Show the document's actual symmetry mode in the Fields
    // checkboxes and the header Sym buttons on open.
    syncSymmetryButtons();

    genFaceDepthBtn->Click.connect([this] { generateEditableFaceDepthLayer(); });
    renderBtn->Click.connect([this] { bakeToActiveDocLayers(); });
    exportObjBtn->Click.connect([this] { exportToWavefrontObj(); });
    exitBtn->Click.connect([this] {
      syncAllLinkedCels();
      restoreWheelZoomPref();
      // Clean studio exit: clear the session marker here as well
      // as in the destructor, so a normal Exit can never be
      // reported as a crash on the next entry.
      std::remove(easymetricHubMarkerPath().c_str());
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

  void pollAndRender(bool force)
  {
    if (!isVisible()) return;
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
    Doc* doc = m_docView->document();
    const doc::frame_t frame = (m_docView->editor() ? m_docView->editor()->frame() : doc::frame_t(0));

    // Canvas <-> UV reconciliation, and per-face editing
    // confinement (see below). Both run before extraction so
    // the Preview samples the reconciled UV layout.
    if (doc && doc->sprite()) {
      doc::Sprite* spr = doc->sprite();
      SharedSurfaceLayers shared = findSharedLayers(spr);
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
        const gfx::Size exact = sharedSheetSize(spr);
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
          const double sx = (exact.w > 0 && spr->width() != exact.w)
                              ? double(spr->width()) / double(exact.w) : 1.0;
          const double sy = (exact.h > 0 && spr->height() != exact.h)
                              ? double(spr->height()) / double(exact.h) : 1.0;
          int newW[kFaceCount], newH[kFaceCount];
          for (int f = 0; f < kFaceCount; ++f) {
            newW[f] = std::clamp(int(std::round(oldW[f] * sx)), 4, 256);
            newH[f] = std::clamp(int(std::round(oldH[f] * sy)), 4, 256);
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
    mix(uint64_t(showDiff ? 1 : 0));
    mix(uint64_t(showNorm ? 1 : 0));
    mix(uint64_t(showDept ? 1 : 0));

    for (int f = 0; f < kFaceCount; ++f) {
      mix(uint64_t(data.w[f]));
      mix(uint64_t(data.h[f]));
      const uint32_t* p = data.pixels[f].data();
      const size_t n = data.pixels[f].size();
      for (size_t i = 0; i < n; ++i)
        mix(p[i]);
      if (data.hasCustomDepthLayer) {
        const uint8_t* dp = data.depthPixels[f].data();
        for (size_t i = 0; i < n; ++i)
          mix(dp[i]);
      }
      if (data.hasBaseLayer[f]) {
        const uint8_t* ba = data.baseAlpha[f].data();
        for (size_t i = 0; i < n; ++i)
          mix(uint64_t(ba[i]));
      }
    }

    if (!force && h == m_lastHash)
      return;
    m_lastHash = h;

    render3DModel(data, yawDeg, pitchDeg, pivX, pivY, pivZ,
                  renderStyle, addOutline, allowCutouts,
                  showDiff, showNorm, showDept);

    if (m_liveBakeCheck->isSelected() && doc && doc->sprite())
      bakeToActiveDocLayers();
  }

public:
  bool isPreviewFloating() const { return m_previewFloating; }

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
    m_previewFloating = true;
    if (m_previewPlaceholder) m_previewPlaceholder->setVisible(true);
    if (m_fieldsPlaceholder) m_fieldsPlaceholder->setVisible(true);
    if (m_previewPopoutBtn) m_previewPopoutBtn->setText("Dock Column");
    if (m_previewPopoutHeaderBtn) m_previewPopoutHeaderBtn->setText("Dock");
    // addChild/removeChild neither re-layout nor repaint the panes
    // (see ui::Widget), so without this the panes keep showing the
    // last painted frame — the cube and the fields — and the
    // placeholder text that explains how to dock never appears.
    m_panes[kFaceCount]->layout();
    m_panes[kFaceCount + 1]->layout();
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
    if (m_previewPlaceholder) m_previewPlaceholder->setVisible(false);
    if (m_fieldsPlaceholder) m_fieldsPlaceholder->setVisible(false);
    if (m_previewPopoutBtn) m_previewPopoutBtn->setText("Pop out Column");
    if (m_previewPopoutHeaderBtn) m_previewPopoutHeaderBtn->setText("Pop out");
    // Undo the floating size hints (see popOutPreview) and re-layout
    // the panes, or the docked column keeps the floating sizes and
    // the widgets paint in their old spots.
    m_previewWidget->resetSizeHint();
    m_fieldsView->resetSizeHint();
    m_panes[kFaceCount]->layout();
    m_panes[kFaceCount + 1]->layout();
    invalidate();
    m_previewWidget->invalidate();
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
                     bool showDept)
  {
    Render3DResult res = compute3DPreviewImages(data, yawDeg, pitchDeg, pivX, pivY, pivZ,
                                                renderStyle, addOutline, allowCutouts);
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
    const std::string mtlPath = base::replace_extension(objPath, "mtl");
    const std::string texPath = base::replace_extension(objPath, "png");
    const std::string mtlFileName = base::get_file_name(mtlPath);
    const std::string texFileName = base::get_file_name(texPath);

    const doc::frame_t fr = (m_docView->editor() ? m_docView->editor()->frame() : doc::frame_t(0));
    SixSurfaceData data;
    extractSixSurfaces(doc, fr, data);

    // Texture atlas
    {
      doc::FramesSequence seq;
      seq.insert(fr);
      std::unique_ptr<FileOp> fop(FileOp::createSaveDocumentOperation(
        UIContext::instance(),
        FileOpROI(doc, doc->sprite()->bounds(), "", "", seq, false),
        texPath,
        "",
        false));
      if (fop && !fop->hasError()) {
        fop->operate();
        fop->done();
      }
    }

    // MTL file
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

    // OBJ file
    {
      std::ofstream obj(objPath);
      if (!obj.is_open()) return;

      const int pivX = m_pivXSlider ? m_pivXSlider->getValue() : 50;
      const int pivY = m_pivYSlider ? m_pivYSlider->getValue() : 50;
      const int pivZ = m_pivZSlider ? m_pivZSlider->getValue() : 50;
      SculptedVoxelVolume vol = buildSculptedVolume(data, pivX, pivY, pivZ);

      const float sprW = std::max(1.0f, float(doc->sprite()->width()));
      const float sprH = std::max(1.0f, float(doc->sprite()->height()));
      // Per-face rect in the exported texture atlas (the flattened
      // sprite). Previously every face was given UVs over the whole
      // 0..1 atlas, so each face showed the entire atlas at once and
      // transparent regions of other faces read as holes. Each face
      // must instead sample only its own rect.
      gfx::Rect faceRect[kFaceCount];
      for (int f = 0; f < kFaceCount; ++f)
        faceRect[f] = sharedFaceRect(doc->sprite(), f);
      const float halfX = std::max({ data.w[kFaceFront], data.w[kFaceBack], data.w[kFaceTop], data.w[kFaceDown] }) * 0.5f;
      const float halfY = std::max({ data.h[kFaceFront], data.h[kFaceBack], data.h[kFaceLeft], data.h[kFaceRight] }) * 0.5f;
      const float halfZ = std::max({ data.w[kFaceLeft], data.w[kFaceRight], data.h[kFaceTop], data.h[kFaceDown] }) * 0.5f;

      // Depth sculpting (2026-11-07): when the volume grew past the Base
      // box, the Base box sits centred in it (pad voxels per side).
      // World and UV coords are measured from the Base box so it keeps
      // its size and Plus bumps stick out beyond it in the exported
      // mesh. Otherwise (pad 0) these are exactly the old formulas.
      const int dimX = std::max({ data.w[kFaceFront], data.w[kFaceBack], data.w[kFaceTop], data.w[kFaceDown], 8 });
      const int dimY = std::max({ data.h[kFaceFront], data.h[kFaceBack], data.h[kFaceLeft], data.h[kFaceRight], 8 });
      const int dimZ = std::max({ data.w[kFaceLeft], data.w[kFaceRight], data.h[kFaceTop], data.h[kFaceDown], 8 });
      const int padX = std::max(0, (vol.vx - dimX) / 2);
      const int padY = std::max(0, (vol.vy - dimY) / 2);
      const int padZ = std::max(0, (vol.vz - dimZ) / 2);
      const int spanX = (padX > 0 ? dimX : vol.vx);
      const int spanY = (padY > 0 ? dimY : vol.vy);
      const int spanZ = (padZ > 0 ? dimZ : vol.vz);
      const float cellSizeX = (2.0f * halfX) / float(spanX);
      const float cellSizeY = (2.0f * halfY) / float(spanY);
      const float cellSizeZ = (2.0f * halfZ) / float(spanZ);

      obj << "# Aseprite 2.0 Easymetric Sculpted 3D Model\n"
          << "mtllib " << mtlFileName << "\n"
          << "usemtl EasymetricMat\n"
          << "o EasymetricSculptedModel\n";

      int vIdx = 1, vtIdx = 1, vnIdx = 1;

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
        obj << "v " << x0 << " " << y0 << " " << z0 << "\n"
            << "v " << x1 << " " << y1 << " " << z1 << "\n"
            << "v " << x2 << " " << y2 << " " << z2 << "\n"
            << "v " << x3 << " " << y3 << " " << z3 << "\n";
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
  // each header: Link, Sym X, Sym Y, Flip H, Flip V).
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
  ui::HBox* m_toolStrip = nullptr;
  PaneActionButton* m_drawToolBtn = nullptr;
  PaneActionButton* m_depthToolBtn = nullptr;
  bool m_depthToolOn = false;
  ui::ComboBox* m_depthModeCombo = nullptr;
  ui::Slider* m_depthBrushSlider = nullptr;
  // Depth-sculpt stroke state: one Tx for the whole drag, so a
  // drag is a single undo step. Each dab installs a fresh copy
  // of the working image via replaceImage (the Tx groups them).
  std::unique_ptr<Tx> m_depthTx;
  doc::ImageRef m_depthWork;
  doc::ImageRef m_depthLive;
  doc::ImageRef m_baseWork;
  doc::ImageRef m_baseLive;
  bool m_depthStrokeActive = false;
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
  mutable int m_rowHpx[4] = { 0, 0, 0, 0 };
  mutable bool m_colInit = false;
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
  double m_savedSymXAxis = 0.0;
  double m_savedSymYAxis = 0.0;
  bool m_symDocSaved = false;
  gfx::Rect m_lastSymFaceRect;

  ui::Timer m_timer;
  uint64_t m_lastHash = 0;
  uint64_t m_lastBaseSilhouetteHash = 0;
  doc::ImageRef m_lastDiffImg;
  doc::ImageRef m_lastNormImg;
  doc::ImageRef m_lastDeptImg;
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
      if (auto* multiPane = dynamic_cast<EasymetricMultiPaneView*>(docView->multiPaneLayout())) {
        multiPane->syncAllLinkedCels();
      }
      docView->exitMultiPaneMode();
      if (auto* sb = StatusBar::instance())
        sb->showTip(1200, "Easymetric 6-Surface Studio: Switched to standard view");
    }
    else {
      setupSixSurfacesOnDoc(docView->document(), true);
      auto* multiPane = new EasymetricMultiPaneView(docView);
      std::vector<Editor*> editors = multiPane->surfaceEditors();
      docView->enterMultiPaneMode(multiPane, editors);
      multiPane->initLayersAndFraming();
      if (auto* sb = StatusBar::instance())
        sb->showTip(1500, "Easymetric 6-Surface Studio: 4-Column Workspace Grid Active");
    }
  }

private:
  Params m_params;
};

} // anonymous namespace

Command* CommandFactory::createEasymetricStudioCommand()
{
  return new EasymetricStudioCommand;
}

} // namespace app
