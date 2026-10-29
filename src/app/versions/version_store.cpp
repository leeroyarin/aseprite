// Aseprite 2.0
// Copyright (C) 2026 Antigravity / Aseprite 2.0 contributors
//
// This program is distributed under the terms of
// the End-User License Agreement for Aseprite.

#ifdef HAVE_CONFIG_H
  #include "config.h"
#endif

#include "app/versions/version_store.h"

#include "app/app.h"
#include "app/context.h"
#include "app/doc.h"
#include "app/doc_access.h"
#include "app/file/file.h"
#include "app/util/conversion_to_surface.h"
#include "base/fs.h"
#include "base/time.h"
#include "doc/image.h"
#include "doc/layer.h"
#include "doc/primitives.h"
#include "doc/sprite.h"
#include "fmt/format.h"
#include "json11.hpp"
#include "os/system.h"
#include "render/render.h"

#include <algorithm>
#include <cstring>
#include <fstream>

namespace app {

namespace {

// In-memory manifest cache to eliminate redundant disk/Google Drive I/O
struct ManifestCache {
  std::string path;
  size_t fileSize = 0;
  base::Time mtime;
  VersionManifest manifest;
};

ManifestCache g_manifestCache;

} // anonymous namespace

std::string VersionStore::getVersionsDir(const std::string& docFilename)
{
  return base::normalize_path(docFilename) + ".versions";
}

std::string VersionStore::getManifestPath(const std::string& versionsDir)
{
  return base::join_path(versionsDir, "manifest.json");
}

bool VersionStore::loadManifest(const std::string& versionsDir, VersionManifest& manifest)
{
  manifest.versions.clear();
  manifest.nextId = 1;
  manifest.format = 1;

  std::string path = getManifestPath(versionsDir);
  if (!base::is_file(path))
    return false;

  const size_t currentSize = base::file_size(path);
  const base::Time currentMtime = base::get_modification_time(path);

  if (g_manifestCache.path == path &&
      g_manifestCache.fileSize == currentSize &&
      g_manifestCache.mtime == currentMtime &&
      !g_manifestCache.manifest.versions.empty()) {
    manifest = g_manifestCache.manifest;
    return true;
  }

  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file.is_open())
    return false;

  const std::streamsize size = file.tellg();
  if (size <= 0)
    return false;

  file.seekg(0, std::ios::beg);
  std::string raw(static_cast<size_t>(size), '\0');
  if (!file.read(&raw[0], size))
    return false;

  std::string err;
  json11::Json json = json11::Json::parse(raw, err);
  if (!err.empty() || !json.is_object())
    return false;

  manifest.format = json["format"].int_value();
  manifest.document = json["document"].string_value();
  manifest.nextId = json["nextId"].int_value();
  if (manifest.nextId <= 0)
    manifest.nextId = 1;

  const auto& items = json["versions"].array_items();
  manifest.versions.reserve(items.size());

  for (const auto& item : items) {
    VersionEntry entry;
    entry.id = item["id"].int_value();
    entry.file = item["file"].string_value();
    entry.thumbnail = item["thumbnail"].string_value();
    entry.created = item["created"].string_value();
    entry.note = item["note"].string_value();
    entry.author = item["author"].string_value();
    entry.width = item["width"].int_value();
    entry.height = item["height"].int_value();
    entry.frames = item["frames"].int_value();
    entry.layers = item["layers"].int_value();
    entry.bytes = static_cast<size_t>(item["bytes"].number_value());

    if (entry.id >= manifest.nextId)
      manifest.nextId = entry.id + 1;

    manifest.versions.push_back(std::move(entry));
  }

  g_manifestCache.path = path;
  g_manifestCache.fileSize = currentSize;
  g_manifestCache.mtime = currentMtime;
  g_manifestCache.manifest = manifest;

  return true;
}

bool VersionStore::saveManifest(const std::string& versionsDir, const VersionManifest& manifest)
{
  json11::Json::object root;
  root["format"] = manifest.format;
  root["document"] = manifest.document;
  root["nextId"] = manifest.nextId;

  json11::Json::array arr;
  arr.reserve(manifest.versions.size());
  for (const auto& v : manifest.versions) {
    json11::Json::object item;
    item["id"] = v.id;
    item["file"] = v.file;
    item["thumbnail"] = v.thumbnail;
    item["created"] = v.created;
    item["note"] = v.note;
    item["author"] = v.author;
    item["width"] = v.width;
    item["height"] = v.height;
    item["frames"] = v.frames;
    item["layers"] = v.layers;
    item["bytes"] = static_cast<double>(v.bytes);
    arr.push_back(std::move(item));
  }
  root["versions"] = std::move(arr);

  std::string content = json11::Json(root).dump();
  std::string targetPath = getManifestPath(versionsDir);
  std::string tmpPath = targetPath + ".tmp";

  {
    std::ofstream out(tmpPath, std::ios::binary | std::ios::trunc);
    if (!out.is_open())
      return false;
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
  }

  base::move_file(tmpPath, targetPath, true);

  g_manifestCache.path = targetPath;
  g_manifestCache.fileSize = content.size();
  g_manifestCache.mtime = base::get_modification_time(targetPath);
  g_manifestCache.manifest = manifest;
  return true;
}

bool VersionStore::saveVersion(Context* ctx,
                               Doc* doc,
                               const std::string& note,
                               const std::string& author,
                               VersionEntry& outEntry)
{
  if (!doc || !doc->isAssociatedToFile() || doc->filename().empty())
    return false;

  std::string versionsDir = getVersionsDir(doc->filename());
  base::make_all_directories(versionsDir);

  VersionManifest manifest;
  if (!loadManifest(versionsDir, manifest)) {
    manifest.format = 1;
    manifest.document = base::get_file_name(doc->filename());
    manifest.nextId = 1;
  }

  int id = manifest.nextId++;
  base::Time t = base::current_time();
  std::string ts = fmt::format("{:04d}{:02d}{:02d}-{:02d}{:02d}{:02d}",
                               t.year, t.month, t.day, t.hour, t.minute, t.second);
  std::string createdIso = fmt::format("{:04d}-{:02d}-{:02d}T{:02d}:{:02d}:{:02d}",
                                       t.year, t.month, t.day, t.hour, t.minute, t.second);

  std::string baseName = fmt::format("v{:04d}_{}", id, ts);
  std::string aseFile = baseName + ".aseprite";
  std::string pngFile = baseName + ".png";
  std::string asePath = base::join_path(versionsDir, aseFile);
  std::string pngPath = base::join_path(versionsDir, pngFile);

  // 1. Save full .aseprite snapshot of current doc
  std::unique_ptr<FileOp> fop(FileOp::createSaveDocumentOperation(
    ctx,
    FileOpROI(doc, doc->sprite()->bounds(), "", "", doc::FramesSequence(), false),
    asePath,
    "",
    false));
  if (!fop || fop->hasError())
    return false;

  fop->operate();
  fop->done();
  if (fop->hasError())
    return false;

  // 2. Save thumbnail PNG (frame 0, max 128x128)
  doc::FramesSequence seq;
  seq.insert(doc::frame_t(0));
  const int w = doc->sprite()->width();
  const int h = doc->sprite()->height();
  const int maxDim = std::max(w, h);
  double scale = (maxDim > 128) ? (128.0 / double(maxDim)) : 1.0;

  std::unique_ptr<FileOp> thumbFop(FileOp::createSaveDocumentOperation(
    ctx,
    FileOpROI(doc, doc->sprite()->bounds(), "", "", seq, false),
    pngPath,
    "",
    false));
  if (thumbFop && !thumbFop->hasError()) {
    thumbFop->setOnTheFlyScale(gfx::PointF(scale, scale));
    thumbFop->operate();
    thumbFop->done();
  }

  // 3. Populate VersionEntry
  outEntry.id = id;
  outEntry.file = aseFile;
  outEntry.thumbnail = pngFile;
  outEntry.created = createdIso;
  outEntry.note = note;
  outEntry.author = author.empty() ? "user" : author;
  outEntry.width = doc->sprite()->width();
  outEntry.height = doc->sprite()->height();
  outEntry.frames = doc->sprite()->totalFrames();
  outEntry.layers = doc->sprite()->root()->layersCount();
  outEntry.bytes = base::file_size(asePath);

  manifest.versions.push_back(outEntry);
  return saveManifest(versionsDir, manifest);
}

bool VersionStore::listVersions(const std::string& docFilename,
                                std::vector<VersionEntry>& outVersions)
{
  std::string versionsDir = getVersionsDir(docFilename);
  VersionManifest manifest;
  if (!loadManifest(versionsDir, manifest))
    return false;

  outVersions = manifest.versions;
  return true;
}

bool VersionStore::restoreVersion(Context* ctx,
                                  Doc* doc,
                                  int versionId,
                                  int& outBackupId)
{
  outBackupId = 0;
  if (!doc || !doc->isAssociatedToFile() || doc->filename().empty())
    return false;

  std::string versionsDir = getVersionsDir(doc->filename());
  VersionManifest manifest;
  if (!loadManifest(versionsDir, manifest))
    return false;

  auto it = std::find_if(manifest.versions.begin(), manifest.versions.end(),
                         [versionId](const VersionEntry& v) { return v.id == versionId; });
  if (it == manifest.versions.end())
    return false;

  std::string snapshotPath = base::join_path(versionsDir, it->file);
  if (!base::is_file(snapshotPath))
    return false;

  // Auto-backup current doc first
  VersionEntry backup;
  if (saveVersion(ctx, doc, fmt::format("Auto-backup before restoring v{:04d}", versionId), "auto", backup)) {
    outBackupId = backup.id;
  }

  std::string originalFilename = doc->filename();

  // Load snapshot document detached first so we don't trigger duplicate UIContext attachments
  Doc* restoredDoc = load_document(nullptr, snapshotPath);
  if (!restoredDoc)
    return false;

  // Close active document
  try {
    DocDestroyer destroyer(ctx, doc, 500);
    destroyer.destroyDocument();
  }
  catch (...) {
    // If closing failed, delete the newly loaded doc and abort
    delete restoredDoc;
    return false;
  }

  // Restore original filename and attach to context once
  restoredDoc->setFilename(originalFilename);
  restoredDoc->setContext(ctx);
  restoredDoc->impossibleToBackToSavedState();
  return true;
}

Doc* VersionStore::openVersion(Context* ctx,
                               const std::string& docFilename,
                               int versionId)
{
  std::string versionsDir = getVersionsDir(docFilename);
  VersionManifest manifest;
  if (!loadManifest(versionsDir, manifest))
    return nullptr;

  auto it = std::find_if(manifest.versions.begin(), manifest.versions.end(),
                         [versionId](const VersionEntry& v) { return v.id == versionId; });
  if (it == manifest.versions.end())
    return nullptr;

  std::string snapshotPath = base::join_path(versionsDir, it->file);
  if (!base::is_file(snapshotPath))
    return nullptr;

  Doc* copyDoc = load_document(nullptr, snapshotPath);
  if (!copyDoc)
    return nullptr;

  std::string dir = base::get_file_path(docFilename);
  std::string title = base::get_file_title(docFilename);
  std::string ext = base::get_file_extension(docFilename);

  copyDoc->setFilename(base::join_path(dir, fmt::format("{} (v{:04d}).{}", title, versionId, ext)));
  copyDoc->setContext(ctx);
  return copyDoc;
}

bool VersionStore::deleteVersion(const std::string& docFilename,
                                 int versionId)
{
  std::string versionsDir = getVersionsDir(docFilename);
  VersionManifest manifest;
  if (!loadManifest(versionsDir, manifest))
    return false;

  auto it = std::find_if(manifest.versions.begin(), manifest.versions.end(),
                         [versionId](const VersionEntry& v) { return v.id == versionId; });
  if (it == manifest.versions.end())
    return false;

  std::string asePath = base::join_path(versionsDir, it->file);
  std::string pngPath = base::join_path(versionsDir, it->thumbnail);

  base::delete_file(asePath);
  base::delete_file(pngPath);

  manifest.versions.erase(it);
  return saveManifest(versionsDir, manifest);
}

Doc* VersionStore::loadVersionSnapshot(const std::string& docFilename,
                                       int versionId)
{
  if (docFilename.empty())
    return nullptr;

  std::string versionsDir = getVersionsDir(docFilename);
  VersionManifest manifest;
  if (!loadManifest(versionsDir, manifest))
    return nullptr;

  auto it = std::find_if(manifest.versions.begin(), manifest.versions.end(),
                         [versionId](const VersionEntry& v) { return v.id == versionId; });
  if (it == manifest.versions.end())
    return nullptr;

  std::string snapshotPath = base::join_path(versionsDir, it->file);
  if (!base::is_file(snapshotPath))
    return nullptr;

  // Pass nullptr as Context so load_document does NOT attach the snapshot to UIContext
  // or trigger UI workspace/tab notifications.
  return load_document(nullptr, snapshotPath);
}

bool VersionStore::computeVisualDiff(Doc* currentDoc,
                                     const Doc* versionDoc,
                                     doc::frame_t frame,
                                     os::SurfaceRef& outVersionSurf,
                                     os::SurfaceRef& outCurrentSurf,
                                     os::SurfaceRef& outDiffSurf,
                                     VersionDiffResult& outStats)
{
  outStats = VersionDiffResult();
  outVersionSurf.reset();
  outCurrentSurf.reset();
  outDiffSurf.reset();

  if (!currentDoc || !currentDoc->sprite() || !versionDoc || !versionDoc->sprite())
    return false;

  const doc::Sprite* curSpr = currentDoc->sprite();
  const doc::Sprite* verSpr = versionDoc->sprite();

  const int curTotalFrames = std::max(1, int(curSpr->totalFrames()));
  const int verTotalFrames = std::max(1, int(verSpr->totalFrames()));
  const doc::frame_t curFrame = std::clamp<doc::frame_t>(frame, 0, curTotalFrames - 1);
  const doc::frame_t verFrame = std::clamp<doc::frame_t>(frame, 0, verTotalFrames - 1);

  outStats.currentFrames = curTotalFrames;
  outStats.versionFrames = verTotalFrames;
  outStats.comparedFrame = std::max<int>(0, frame);

  const int curW = curSpr->width();
  const int curH = curSpr->height();
  const int verW = verSpr->width();
  const int verH = verSpr->height();

  doc::ImageRef curImg(doc::Image::create(doc::IMAGE_RGB, curW, curH));
  doc::ImageRef verImg(doc::Image::create(doc::IMAGE_RGB, verW, verH));
  doc::clear_image(curImg.get(), 0);
  doc::clear_image(verImg.get(), 0);

  render::Render renderer;
  renderer.renderSprite(curImg.get(), curSpr, curFrame);
  renderer.renderSprite(verImg.get(), verSpr, verFrame);

  const int maxW = std::max(curW, verW);
  const int maxH = std::max(curH, verH);
  doc::ImageRef diffImg(doc::Image::create(doc::IMAGE_RGB, maxW, maxH));
  doc::clear_image(diffImg.get(), 0);

  outStats.width = maxW;
  outStats.height = maxH;
  outStats.totalPixels = maxW * maxH;

  const doc::color_t kAddedCol = doc::rgba(16, 245, 150, 255);
  const doc::color_t kRemovedCol = doc::rgba(255, 60, 90, 255);
  const doc::color_t kModifiedCol = doc::rgba(255, 190, 20, 255);
  const int minW = std::min(curW, verW);

  for (int y = 0; y < maxH; ++y) {
    uint32_t* __restrict dstRow =
      reinterpret_cast<uint32_t*>(diffImg->getPixelAddress(0, y));
    const uint32_t* __restrict curRow = (y < curH)
      ? reinterpret_cast<const uint32_t*>(curImg->getPixelAddress(0, y))
      : nullptr;
    const uint32_t* __restrict verRow = (y < verH)
      ? reinterpret_cast<const uint32_t*>(verImg->getPixelAddress(0, y))
      : nullptr;

    // Fast path: both scanlines exist, have equal width, and are byte-for-byte identical
    if (curRow && verRow && curW == verW &&
        std::memcmp(curRow, verRow, static_cast<size_t>(curW) * sizeof(uint32_t)) == 0) {
      for (int x = 0; x < curW; ++x) {
        const uint32_t c = curRow[x];
        const uint32_t a = doc::rgba_geta(c);
        if (a == 0) {
          dstRow[x] = 0;
        }
        else {
          const uint32_t gray = (doc::rgba_getr(c) + (doc::rgba_getg(c) << 1) + doc::rgba_getb(c)) >> 2;
          dstRow[x] = doc::rgba(gray, gray, gray, (a * 90) >> 8);
        }
      }
      continue;
    }

    // Shared overlap region [0 .. minW)
    int x = 0;
    if (curRow && verRow) {
      for (; x < minW; ++x) {
        const uint32_t cCur = curRow[x];
        const uint32_t cVer = verRow[x];
        const uint32_t aCur = doc::rgba_geta(cCur);
        const uint32_t aVer = doc::rgba_geta(cVer);

        if ((aCur | aVer) == 0) {
          dstRow[x] = 0;
        }
        else if (cCur == cVer) {
          const uint32_t gray = (doc::rgba_getr(cCur) + (doc::rgba_getg(cCur) << 1) + doc::rgba_getb(cCur)) >> 2;
          dstRow[x] = doc::rgba(gray, gray, gray, (aCur * 90) >> 8);
        }
        else {
          outStats.changedPixels++;
          if (aVer == 0) {
            outStats.addedPixels++;
            dstRow[x] = kAddedCol;
          }
          else if (aCur == 0) {
            outStats.removedPixels++;
            dstRow[x] = kRemovedCol;
          }
          else {
            outStats.modifiedPixels++;
            dstRow[x] = kModifiedCol;
          }
        }
      }
    }

    // Remaining overhang pixels if canvas dimensions changed between versions
    for (; x < maxW; ++x) {
      const uint32_t cCur = (curRow && x < curW) ? curRow[x] : 0;
      const uint32_t cVer = (verRow && x < verW) ? verRow[x] : 0;
      const uint32_t aCur = doc::rgba_geta(cCur);
      const uint32_t aVer = doc::rgba_geta(cVer);

      if ((aCur | aVer) == 0) {
        dstRow[x] = 0;
      }
      else {
        outStats.changedPixels++;
        if (aVer == 0) {
          outStats.addedPixels++;
          dstRow[x] = kAddedCol;
        }
        else {
          outStats.removedPixels++;
          dstRow[x] = kRemovedCol;
        }
      }
    }
  }

  if (os::System::instance()) {
    outVersionSurf = os::System::instance()->makeRgbaSurface(verW, verH);
    convert_image_to_surface(verImg.get(), verSpr->palette(verFrame), outVersionSurf.get(), 0, 0, 0, 0, verW, verH);

    outCurrentSurf = os::System::instance()->makeRgbaSurface(curW, curH);
    convert_image_to_surface(curImg.get(), curSpr->palette(curFrame), outCurrentSurf.get(), 0, 0, 0, 0, curW, curH);

    outDiffSurf = os::System::instance()->makeRgbaSurface(maxW, maxH);
    convert_image_to_surface(diffImg.get(), curSpr->palette(curFrame), outDiffSurf.get(), 0, 0, 0, 0, maxW, maxH);
  }

  return true;
}

} // namespace app
