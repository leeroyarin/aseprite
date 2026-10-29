// Aseprite 2.0
// Copyright (C) 2026 Antigravity / Aseprite 2.0 contributors
//
// This program is distributed under the terms of
// the End-User License Agreement for Aseprite.

#ifndef APP_VERSIONS_VERSION_STORE_H_INCLUDED
#define APP_VERSIONS_VERSION_STORE_H_INCLUDED
#pragma once

#include "doc/frame.h"
#include "os/surface.h"

#include <string>
#include <vector>

namespace app {

class Context;
class Doc;

struct VersionDiffResult {
  int changedPixels = 0;
  int addedPixels = 0;
  int removedPixels = 0;
  int modifiedPixels = 0;
  int totalPixels = 0;
  int width = 0;
  int height = 0;
  int currentFrames = 1;
  int versionFrames = 1;
  int comparedFrame = 0; // 0-based
};

struct VersionEntry {
  int id = 0;
  std::string file;       // relative to versions dir: v0001_YYYYMMDD-HHMMSS.aseprite
  std::string thumbnail;  // relative to versions dir: v0001_YYYYMMDD-HHMMSS.png
  std::string created;    // ISO timestamp YYYY-MM-DDTHH:MM:SS
  std::string note;
  std::string author;     // "user", "agent", "auto"
  int width = 0;
  int height = 0;
  int frames = 0;
  int layers = 0;
  size_t bytes = 0;
};

struct VersionManifest {
  int format = 1;
  std::string document;
  int nextId = 1;
  std::vector<VersionEntry> versions;
};

class VersionStore {
public:
  static std::string getVersionsDir(const std::string& docFilename);
  static std::string getManifestPath(const std::string& versionsDir);

  static bool loadManifest(const std::string& versionsDir, VersionManifest& manifest);
  static bool saveManifest(const std::string& versionsDir, const VersionManifest& manifest);

  static bool saveVersion(Context* ctx,
                          Doc* doc,
                          const std::string& note,
                          const std::string& author,
                          VersionEntry& outEntry);

  static bool listVersions(const std::string& docFilename,
                           std::vector<VersionEntry>& outVersions);

  static bool restoreVersion(Context* ctx,
                             Doc* doc,
                             int versionId,
                             int& outBackupId);

  static Doc* openVersion(Context* ctx,
                          const std::string& docFilename,
                          int versionId);

  static bool deleteVersion(const std::string& docFilename,
                            int versionId);

  // Loads a version snapshot offscreen (detached from UIContext) for fast inspection/diffing.
  // Caller owns the returned Doc*.
  static Doc* loadVersionSnapshot(const std::string& docFilename,
                                  int versionId);

  static bool computeVisualDiff(Doc* currentDoc,
                                const Doc* versionDoc,
                                doc::frame_t frame,
                                os::SurfaceRef& outVersionSurf,
                                os::SurfaceRef& outCurrentSurf,
                                os::SurfaceRef& outDiffSurf,
                                VersionDiffResult& outStats);
};

} // namespace app

#endif
