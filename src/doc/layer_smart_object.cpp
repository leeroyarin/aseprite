// Aseprite Document Library
// Copyright (c) 2026 Igara Studio S.A.
//
// This file is released under the terms of the MIT license.
// Read LICENSE.txt for more information.

#ifdef HAVE_CONFIG_H
  #include "config.h"
#endif

#include "doc/layer_smart_object.h"

#include "doc/algorithm/flip_image.h"
#include "doc/algorithm/resize_image.h"
#include "doc/algorithm/rotate.h"
#include "doc/algorithm/rotsprite.h"
#include "doc/blend_image.h"
#include "doc/cel.h"
#include "doc/cel_data.h"
#include "doc/document.h"
#include "doc/image.h"
#include "doc/primitives.h"
#include "doc/render_plan.h"
#include "doc/sprite.h"
#include "base/fs.h"
#include "base/time.h"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <map>
#include <memory>

namespace doc {

static LayerSmartObject::SourceDocLoader s_sourceDocLoader = nullptr;
static LayerSmartObject::CompositeRenderer s_compositeRenderer = nullptr;
struct DetachedDocEntry {
  std::shared_ptr<Document> doc;
  uint64_t lastModified = 0;
};
static std::map<std::string, DetachedDocEntry> s_detachedDocsCache;

void LayerSmartObject::setSourceDocLoader(SourceDocLoader loader)
{
  s_sourceDocLoader = std::move(loader);
}

void LayerSmartObject::setCompositeRenderer(CompositeRenderer renderer)
{
  s_compositeRenderer = std::move(renderer);
}

uint64_t LayerSmartObject::fileModificationEpoch(const std::string& path)
{
  if (path.empty() || !base::is_file(path))
    return 0;

  base::Time diskTime = base::get_modification_time(path);
  if (!diskTime.valid())
    return 0;

  std::tm tm{};
  tm.tm_year = diskTime.year - 1900;
  tm.tm_mon  = diskTime.month - 1;
  tm.tm_mday = diskTime.day;
  tm.tm_hour = diskTime.hour;
  tm.tm_min  = diskTime.minute;
  tm.tm_sec  = diskTime.second;
  tm.tm_isdst = -1;
  std::time_t tt = std::mktime(&tm);
  return (tt != static_cast<std::time_t>(-1)) ? static_cast<uint64_t>(tt) : 0;
}

Document* LayerSmartObject::getDetachedDoc(const std::string& filepath)
{
  if (filepath.empty())
    return nullptr;
  auto it = s_detachedDocsCache.find(filepath);
  if (it != s_detachedDocsCache.end() && it->second.doc) {
    uint64_t diskEpoch = fileModificationEpoch(filepath);
    if (diskEpoch > 0 && it->second.lastModified > 0 && diskEpoch > it->second.lastModified) {
      // Stale entry: disk file was modified after it was cached
      return nullptr;
    }
    return it->second.doc.get();
  }
  return nullptr;
}

uint64_t LayerSmartObject::getDetachedDocTimestamp(const std::string& filepath)
{
  if (filepath.empty())
    return 0;
  auto it = s_detachedDocsCache.find(filepath);
  if (it != s_detachedDocsCache.end()) {
    return it->second.lastModified;
  }
  return 0;
}

void LayerSmartObject::setDetachedDoc(const std::string& filepath, Document* doc, uint64_t timestamp)
{
  if (filepath.empty())
    return;
  if (!doc) {
    s_detachedDocsCache.erase(filepath);
    return;
  }
  if (timestamp == 0)
    timestamp = fileModificationEpoch(filepath);

  auto it = s_detachedDocsCache.find(filepath);
  if (it != s_detachedDocsCache.end() && it->second.doc && it->second.doc.get() == doc) {
    it->second.lastModified = timestamp;
  }
  else {
    s_detachedDocsCache[filepath] = { std::shared_ptr<Document>(doc), timestamp };
  }
}

void LayerSmartObject::clearDetachedDocsCache()
{
  s_detachedDocsCache.clear();
}

double LayerSmartObject::snap_affine(double v, double eps)
{
  if (std::abs(v) < eps)
    return 0.0;
  double rounded = std::round(v);
  if (std::abs(v - rounded) < eps)
    return rounded;
  return v;
}

gfx::PointF LayerSmartObject::snap_affine(const gfx::PointF& pt, double eps)
{
  return gfx::PointF(snap_affine(pt.x, eps), snap_affine(pt.y, eps));
}

LayerSmartObject::LayerSmartObject(Sprite* sprite, const std::string& filepath)
  : LayerImage(ObjectType::LayerSmartObject, sprite)
  , m_filepath(filepath)
  , m_position(0.0, 0.0)
  , m_scale(1.0, 1.0)
  , m_angle(0.0)
  , m_flipH(false)
  , m_flipV(false)
  , m_pivot(0.5, 0.5)
  , m_timelineMapping(TimelineMapping::Loop)
  , m_sourceSize(0, 0)
  , m_lastModifiedTime(0)
  , m_linkedDoc(nullptr)
{
  switchFlags(LayerFlags::SmartObject, true);
  if (!m_filepath.empty()) {
    syncPropertiesToUserData();
  }
}

LayerSmartObject::~LayerSmartObject()
{
}

LayerSmartObject* LayerSmartObject::clone(Sprite* sprite) const
{
  auto cloned = new LayerSmartObject(sprite, m_filepath);
  cloned->m_position = m_position;
  cloned->m_scale = m_scale;
  cloned->m_angle = m_angle;
  cloned->m_flipH = m_flipH;
  cloned->m_flipV = m_flipV;
  cloned->m_pivot = m_pivot;
  cloned->m_timelineMapping = m_timelineMapping;
  cloned->m_sourceSize = m_sourceSize;
  cloned->m_lastModifiedTime = m_lastModifiedTime;
  cloned->m_linkedDoc = m_linkedDoc;
  cloned->syncPropertiesToUserData();
  return cloned;
}

void LayerSmartObject::setFilepath(const std::string& filepath)
{
  if (m_filepath != filepath) {
    m_filepath = filepath;
    m_linkedDoc = nullptr;
    userData().properties("smart_object")["filepath"] = filepath;
    if (sprite() && sprite()->document() && !sprite()->document()->filename().empty()) {
      std::string hostDir = base::get_file_path(sprite()->document()->filename());
      std::string rel = base::get_relative_path(filepath, hostDir);
      userData().properties("smart_object")["relative_path"] = rel;
    }
  }
}

void LayerSmartObject::setPosition(const gfx::PointF& pos)
{
  m_position = snap_affine(pos);
  auto& props = userData().properties("smart_object");
  props["pos_x"] = m_position.x;
  props["pos_y"] = m_position.y;
}

void LayerSmartObject::setScale(const gfx::PointF& scale)
{
  m_scale = snap_affine(scale);
  auto& props = userData().properties("smart_object");
  props["scale_x"] = m_scale.x;
  props["scale_y"] = m_scale.y;
}

void LayerSmartObject::setAngle(double angle)
{
  m_angle = snap_affine(angle);
  userData().properties("smart_object")["angle"] = m_angle;
}

void LayerSmartObject::setFlipH(bool flipH)
{
  m_flipH = flipH;
  userData().properties("smart_object")["flip_h"] = flipH;
}

void LayerSmartObject::setFlipV(bool flipV)
{
  m_flipV = flipV;
  userData().properties("smart_object")["flip_v"] = flipV;
}

void LayerSmartObject::setPivot(const gfx::PointF& p)
{
  m_pivot = snap_affine(p);
  auto& props = userData().properties("smart_object");
  props["pivot_x"] = m_pivot.x;
  props["pivot_y"] = m_pivot.y;
}

void LayerSmartObject::setTransform(const gfx::PointF& pos,
                                    const gfx::PointF& scale,
                                    double angle,
                                    bool flipH,
                                    bool flipV,
                                    const gfx::PointF& pivot)
{
  m_position = snap_affine(pos);
  m_scale = snap_affine(scale);
  m_angle = snap_affine(angle);
  m_flipH = flipH;
  m_flipV = flipV;
  m_pivot = snap_affine(pivot);
  syncPropertiesToUserData();
  renderAllCels();
}

void LayerSmartObject::resetTransform()
{
  m_scale = gfx::PointF(1.0, 1.0);
  m_angle = 0.0;
  m_flipH = false;
  m_flipV = false;
  m_pivot = gfx::PointF(0.5, 0.5);
  syncPropertiesToUserData();
  renderAllCels();
}

void LayerSmartObject::setSourceSize(const gfx::Size& size)
{
  m_sourceSize = size;
  auto& props = userData().properties("smart_object");
  props["src_w"] = int32_t(size.w);
  props["src_h"] = int32_t(size.h);
}

void LayerSmartObject::setLastModifiedTime(uint64_t t)
{
  m_lastModifiedTime = t;
  userData().properties("smart_object")["last_modified"] = int64_t(t);
  if (!m_filepath.empty()) {
    auto it = s_detachedDocsCache.find(m_filepath);
    if (it != s_detachedDocsCache.end() && it->second.doc.get() == m_linkedDoc) {
      it->second.lastModified = t;
    }
  }
}

std::string LayerSmartObject::resolveFilepath() const
{
  if (m_filepath.empty())
    return std::string();

  if (base::is_file(m_filepath))
    return m_filepath;

  // Fallback relative lookup if host document path is known
  if (sprite() && sprite()->document() && !sprite()->document()->filename().empty()) {
    std::string hostDir = base::get_file_path(sprite()->document()->filename());

    // Check UserData relative_path
    const auto& maps = userData().propertiesMaps();
    auto it = maps.find("smart_object");
    if (it != maps.end()) {
      auto pRel = it->second.find("relative_path");
      if (pRel != it->second.end()) {
        if (auto rel = std::get_if<std::string>(&pRel->second)) {
          std::string candidate = base::join_path(hostDir, *rel);
          if (base::is_file(candidate))
            return candidate;
        }
      }
    }

    // Check sibling file in same directory
    std::string sibling = base::join_path(hostDir, base::get_file_name(m_filepath));
    if (base::is_file(sibling))
      return sibling;
  }

  return m_filepath;
}

SmartObjectStatus LayerSmartObject::linkStatus() const
{
  if (m_filepath.empty())
    return SmartObjectStatus::Broken;

  std::string resolvedPath = resolveFilepath();
  if (resolvedPath.empty() || !base::is_file(resolvedPath))
    return SmartObjectStatus::Missing;

  const size_t sz = base::file_size(resolvedPath);
  if (sz == 0)
    return SmartObjectStatus::Broken;

  const uint64_t diskEpoch = fileModificationEpoch(resolvedPath);
  if (diskEpoch == 0)
    return SmartObjectStatus::Missing;

  if (m_lastModifiedTime == 0 || diskEpoch > m_lastModifiedTime)
    return SmartObjectStatus::Outdated;

  return SmartObjectStatus::Linked;
}

bool LayerSmartObject::isDiskFileNewer() const
{
  return (linkStatus() == SmartObjectStatus::Outdated);
}

bool LayerSmartObject::reloadSource(bool force)
{
  std::string path = resolveFilepath();
  if (path.empty() || !base::is_file(path))
    return false;

  // Guard against mid-write or 0-byte locked files
  if (base::file_size(path) == 0)
    return false;

  const uint64_t diskEpoch = fileModificationEpoch(path);
  if (diskEpoch == 0)
    return false;

  if (!force && m_lastModifiedTime != 0 && diskEpoch <= m_lastModifiedTime)
    return false; // Already up to date

  if (!s_sourceDocLoader)
    return false;

  // 1. Offscreen load into temporary pointer (non-destructive)
  Document* newDoc = nullptr;
  try {
    newDoc = s_sourceDocLoader(path);
  }
  catch (...) {
    newDoc = nullptr;
  }

  if (!newDoc || !newDoc->sprite()) {
    if (newDoc)
      delete newDoc;
    return false; // Corrupt or failed load; preserve existing cels
  }

  // 2. Atomic swap in detached cache and layer reference
  s_detachedDocsCache[m_filepath] = { std::shared_ptr<Document>(newDoc), diskEpoch };
  if (path != m_filepath) {
    s_detachedDocsCache[path] = s_detachedDocsCache[m_filepath];
  }
  m_linkedDoc = newDoc;

  // 3. Update dimensions and timestamps
  Sprite* srcSpr = newDoc->sprite();
  m_sourceSize = srcSpr->size();
  m_lastModifiedTime = diskEpoch;
  syncPropertiesToUserData();

  // 4. Synchronize multi-frame cel topology (handles frame count expansion/shrinkage)
  if (sprite()) {
    syncFrames(sprite()->totalFrames());
  }

  // 5. Re-render all cels
  renderAllCels();
  return true;
}

bool LayerSmartObject::reloadSource()
{
  return reloadSource(false);
}

Document* LayerSmartObject::linkedDoc() const
{
  if (!m_filepath.empty()) {
    Document* cached = getDetachedDoc(m_filepath);
    if (cached) {
      m_linkedDoc = cached;
      return m_linkedDoc;
    }
  }

  if (!m_linkedDoc && !m_filepath.empty() && s_sourceDocLoader) {
    Document* loaded = s_sourceDocLoader(m_filepath);
    if (loaded) {
      uint64_t epoch = fileModificationEpoch(m_filepath);
      s_detachedDocsCache[m_filepath] = { std::shared_ptr<Document>(loaded), epoch };
      m_linkedDoc = loaded;
      return m_linkedDoc;
    }
  }

  return m_linkedDoc;
}

void LayerSmartObject::setLinkedDoc(Document* doc)
{
  m_linkedDoc = doc;
  if (doc && !m_filepath.empty()) {
    uint64_t epoch = (m_lastModifiedTime != 0) ? m_lastModifiedTime : fileModificationEpoch(m_filepath);
    auto it = s_detachedDocsCache.find(m_filepath);
    if (it == s_detachedDocsCache.end() || it->second.doc.get() != doc) {
      s_detachedDocsCache[m_filepath] = { std::shared_ptr<Document>(doc), epoch };
    }
  }
}

void LayerSmartObject::setTimelineMapping(TimelineMapping mode)
{
  if (m_timelineMapping != mode) {
    m_timelineMapping = mode;
    syncPropertiesToUserData();
    if (sprite()) {
      syncFrames();
      renderAllCels();
    }
  }
}

frame_t LayerSmartObject::linkedFrameCount() const
{
  Document* doc = linkedDoc();
  if (doc && doc->sprite()) {
    return std::max(frame_t(1), doc->sprite()->totalFrames());
  }
  return frame_t(1);
}

frame_t LayerSmartObject::mapTimelineFrame(frame_t hostFrame) const
{
  frame_t M = linkedFrameCount();
  if (M <= 1)
    return frame_t(0);

  switch (m_timelineMapping) {
    case TimelineMapping::Hold:
      return std::clamp(hostFrame, frame_t(0), frame_t(M - 1));
    case TimelineMapping::Fixed:
      return frame_t(0);
    case TimelineMapping::Loop:
    default: {
      frame_t lf = hostFrame % M;
      if (lf < 0)
        lf = (lf + M) % M;
      return lf;
    }
  }
}

void LayerSmartObject::syncFrames(frame_t totalFrames)
{
  if (totalFrames <= 0)
    return;

  const frame_t M = linkedFrameCount();
  const frame_t canonicalCount = (m_timelineMapping == TimelineMapping::Fixed ?
                                  frame_t(1) : std::min(M, totalFrames));

  // 1. Ensure canonical (unlinked) cels exist for linked frames 0 .. canonicalCount - 1
  std::vector<Cel*> canonicalCels(canonicalCount, nullptr);
  for (frame_t k = 0; k < canonicalCount; ++k) {
    Cel* c = cel(k);
    if (!c || c->link() != nullptr) {
      if (c) {
        removeCel(c);
        const Document* doc = (sprite() ? sprite()->document() : nullptr);
        if (!doc || !doc->isUndoing())
          delete c;
      }
      renderCel(k, k);
      c = cel(k);
    }
    canonicalCels[k] = c;
  }

  // 2. Populate / link remaining host frames f from 0 to totalFrames - 1
  for (frame_t f = 0; f < totalFrames; ++f) {
    frame_t k = mapTimelineFrame(f);
    if (k >= canonicalCount)
      k = canonicalCount > 0 ? (canonicalCount - 1) : 0;

    Cel* canonical = (k < canonicalCount) ? canonicalCels[k] : nullptr;
    if (!canonical)
      continue;
    if (f == canonical->frame())
      continue; // Canonical cel already in place

    Cel* existing = cel(f);
    if (existing) {
      if (existing->dataRef() != canonical->dataRef()) {
        existing->setDataRef(canonical->dataRef());
        existing->setPosition(canonical->position());
      }
    }
    else {
      Cel* linkedCel = Cel::MakeLink(f, canonical);
      addCel(linkedCel);
    }
  }

  // 3. Prune out-of-range cels and duplicate cels sharing the same frame index,
  // ensuring at most 1 cel per frame exists on LayerSmartObject.
  std::vector<bool> seen(totalFrames, false);
  std::vector<Cel*> stale;

  // Pre-mark canonical cels as seen so they are guaranteed retained
  for (frame_t k = 0; k < canonicalCount; ++k) {
    if (canonicalCels[k] && canonicalCels[k]->frame() >= 0 &&
        canonicalCels[k]->frame() < totalFrames) {
      seen[canonicalCels[k]->frame()] = true;
    }
  }

  for (Cel* c : cels()) {
    if (!c)
      continue;

    if (c->frame() < 0 || c->frame() >= totalFrames) {
      stale.push_back(c);
    }
    else if (c->frame() < canonicalCount && canonicalCels[c->frame()] == c) {
      // Retain the designated canonical cel for this frame
      continue;
    }
    else if (seen[c->frame()]) {
      // Duplicate cel on an already occupied frame index
      stale.push_back(c);
    }
    else {
      seen[c->frame()] = true;
    }
  }
  const Document* doc = (sprite() ? sprite()->document() : nullptr);
  const bool undoing = (doc && doc->isUndoing());
  for (Cel* c : stale) {
    removeCel(c);
    if (!undoing)
      delete c;
  }
}

void LayerSmartObject::syncFrames()
{
  if (sprite())
    syncFrames(sprite()->totalFrames());
}

void LayerSmartObject::displaceFrames(frame_t fromThis, frame_t delta)
{
  if (!sprite())
    return;

  const Document* doc = sprite()->document();
  const bool undoing = (doc && doc->isUndoing());

  if (delta < 0) {
    // 1. Detach cels in range [fromThis, fromThis - delta) being removed.
    // Never shift them left into [fromThis + delta].
    for (frame_t f = fromThis; f < fromThis - delta; ++f) {
      if (Cel* c = cel(f)) {
        removeCel(c);
        if (!undoing)
          delete c;
      }
    }

    // 2. Displace only frames strictly after the removed range.
    for (frame_t c = fromThis - delta; c <= sprite()->lastFrame(); ++c) {
      if (Cel* cel = this->cel(c))
        moveCel(cel, c + delta);
    }
  }
  else {
    LayerImage::displaceFrames(fromThis, delta);
  }

  // 3. Account for frame deletion asymmetry
  frame_t newTotal = sprite()->totalFrames() + (delta < 0 ? delta : 0);
  newTotal = std::max(frame_t(1), newTotal);

  // 4. Synchronize topology: link remaining frames and prune out-of-range cels
  syncFrames(newTotal);

  // 5. Re-render canonical cels in Loop/Hold mode to refresh pixels.
  if (m_timelineMapping != TimelineMapping::Fixed) {
    const frame_t M = linkedFrameCount();
    const frame_t canonicalCount = std::min(M, newTotal);
    for (frame_t k = 0; k < canonicalCount; ++k) {
      frame_t lf = mapTimelineFrame(k);
      renderCel(k, lf);
    }
  }
}

void LayerSmartObject::addCel(Cel* cel)
{
  if (cel) {
    if (Cel* existing = this->cel(cel->frame())) {
      if (existing == cel)
        return;

      removeCel(existing);

      const Document* doc = (sprite() ? sprite()->document() : nullptr);
      if (!doc || !doc->isUndoing()) {
        delete existing;
      }
    }
  }
  LayerImage::addCel(cel);
}

gfx::RectF LayerSmartObject::boundsF() const
{
  const Cel* c = cel(frame_t(0));
  if (c)
    return c->boundsF();

  double w = static_cast<double>(m_sourceSize.w) * std::abs(m_scale.x);
  double h = static_cast<double>(m_sourceSize.h) * std::abs(m_scale.y);
  return gfx::RectF(m_position.x, m_position.y, w, h);
}

gfx::Rect LayerSmartObject::bounds() const
{
  const Cel* c = cel(frame_t(0));
  if (c)
    return c->bounds();

  if (m_sourceSize.w > 0 && m_sourceSize.h > 0) {
    gfx::RectF r = boundsF();
    return gfx::Rect(
      static_cast<int>(std::floor(r.x)),
      static_cast<int>(std::floor(r.y)),
      static_cast<int>(std::ceil(r.w)),
      static_cast<int>(std::ceil(r.h)));
  }
  return gfx::Rect();
}

int LayerSmartObject::getMemSize() const
{
  int size = sizeof(LayerSmartObject);
  size += int(m_filepath.size());
  for (const Cel* c : cels()) {
    if (c->link())
      continue;
    size += c->getMemSize();
  }
  return size;
}

void LayerSmartObject::setUserData(const UserData& userData)
{
  Layer::setUserData(userData);
  readPropertiesFromUserData();
}

void LayerSmartObject::syncPropertiesToUserData()
{
  auto& props = userData().properties("smart_object");
  props["filepath"] = m_filepath;
  props["pos_x"] = m_position.x;
  props["pos_y"] = m_position.y;
  props["scale_x"] = m_scale.x;
  props["scale_y"] = m_scale.y;
  props["angle"] = m_angle;
  props["flip_h"] = m_flipH;
  props["flip_v"] = m_flipV;
  props["pivot_x"] = m_pivot.x;
  props["pivot_y"] = m_pivot.y;
  const char* modeStr = "Loop";
  if (m_timelineMapping == TimelineMapping::Hold)
    modeStr = "Hold";
  else if (m_timelineMapping == TimelineMapping::Fixed)
    modeStr = "Fixed";
  props["timeline_mapping"] = std::string(modeStr);
  props["src_w"] = int32_t(m_sourceSize.w);
  props["src_h"] = int32_t(m_sourceSize.h);
  props["last_modified"] = int64_t(m_lastModifiedTime);
}

void LayerSmartObject::readPropertiesFromUserData()
{
  const auto& maps = userData().propertiesMaps();
  auto it = maps.find("smart_object");
  if (it == maps.end())
    return;

  const auto& props = it->second;

  auto get_double = [&](const char* key, double def) -> double {
    auto p = props.find(key);
    if (p == props.end()) return def;
    if (auto v = std::get_if<double>(&p->second)) return *v;
    if (auto v = std::get_if<float>(&p->second)) return static_cast<double>(*v);
    if (auto v = std::get_if<int32_t>(&p->second)) return static_cast<double>(*v);
    if (auto v = std::get_if<int16_t>(&p->second)) return static_cast<double>(*v);
    if (auto v = std::get_if<int64_t>(&p->second)) return static_cast<double>(*v);
    return def;
  };

  auto get_bool = [&](const char* key, bool def) -> bool {
    auto p = props.find(key);
    if (p == props.end()) return def;
    if (auto v = std::get_if<bool>(&p->second)) return *v;
    if (auto v = std::get_if<int32_t>(&p->second)) return (*v != 0);
    return def;
  };

  auto get_int = [&](const char* key, int def) -> int {
    auto p = props.find(key);
    if (p == props.end()) return def;
    if (auto v = std::get_if<int32_t>(&p->second)) return *v;
    if (auto v = std::get_if<int16_t>(&p->second)) return *v;
    if (auto v = std::get_if<int64_t>(&p->second)) return static_cast<int>(*v);
    return def;
  };

  auto pFile = props.find("filepath");
  if (pFile != props.end()) {
    if (auto v = std::get_if<std::string>(&pFile->second))
      m_filepath = *v;
  }

  m_position.x = get_double("pos_x", m_position.x);
  m_position.y = get_double("pos_y", m_position.y);
  m_scale.x = get_double("scale_x", m_scale.x);
  m_scale.y = get_double("scale_y", m_scale.y);
  m_angle = get_double("angle", m_angle);
  m_flipH = get_bool("flip_h", m_flipH);
  m_flipV = get_bool("flip_v", m_flipV);
  m_pivot.x = get_double("pivot_x", m_pivot.x);
  m_pivot.y = get_double("pivot_y", m_pivot.y);
  m_sourceSize.w = get_int("src_w", m_sourceSize.w);
  m_sourceSize.h = get_int("src_h", m_sourceSize.h);

  auto pMap = props.find("timeline_mapping");
  if (pMap != props.end()) {
    if (auto s = std::get_if<std::string>(&pMap->second)) {
      if (*s == "Hold")
        m_timelineMapping = TimelineMapping::Hold;
      else if (*s == "Fixed")
        m_timelineMapping = TimelineMapping::Fixed;
      else
        m_timelineMapping = TimelineMapping::Loop;
    }
    else if (auto v = std::get_if<int32_t>(&pMap->second)) {
      m_timelineMapping = static_cast<TimelineMapping>(*v);
    }
  }

  auto pMod = props.find("last_modified");
  if (pMod != props.end()) {
    if (auto v = std::get_if<int64_t>(&pMod->second))
      m_lastModifiedTime = static_cast<uint64_t>(*v);
    else if (auto v = std::get_if<int32_t>(&pMod->second))
      m_lastModifiedTime = static_cast<uint64_t>(*v);
  }
}

ImageRef LayerSmartObject::compositeSourceFrame(frame_t linkedFrame) const
{
  Document* doc = linkedDoc();
  if (!doc || !doc->sprite())
    return nullptr;

  Sprite* spr = doc->sprite();
  if (spr->totalFrames() <= 0)
    return nullptr;

  frame_t f = linkedFrame;
  if (f < 0 || f >= spr->totalFrames()) {
    f = (spr->totalFrames() > 0 ? (f % spr->totalFrames()) : frame_t(0));
    if (f < 0)
      f = (f + spr->totalFrames()) % spr->totalFrames();
  }

  if (s_compositeRenderer) {
    return s_compositeRenderer(doc, f, sprite());
  }

  // Standalone doc-lib fallback compositor
  ImageRef comp(Image::create(spr->pixelFormat(), spr->width(), spr->height()));
  clear_image(comp.get(), spr->transparentColor());

  RenderPlan plan;
  for (const Layer* lay : spr->root()->layers()) {
    plan.addLayer(lay, f);
  }
  for (const auto& item : plan.items()) {
    if (item.cel && item.cel->image()) {
      blend_image(comp.get(),
                  item.cel->image(),
                  gfx::Clip(item.cel->x(), item.cel->y(), 0, 0,
                            item.cel->image()->width(), item.cel->image()->height()),
                  spr->palette(f),
                  item.cel->opacity(),
                  item.layer->blendMode());
    }
  }
  return comp;
}

struct CornerBounds {
  int localX;
  int localY;
  int dstW;
  int dstH;
  gfx::PointF transformed[4];
};

static CornerBounds calcCornerBounds(int W, int H, const gfx::PointF& pivotNorm,
                                     double scaleX, double scaleY, double angle)
{
  const gfx::PointF pivot(pivotNorm.x * W, pivotNorm.y * H);
  const double rad = angle * (3.14159265358979323846 / 180.0);
  const double cosA = LayerSmartObject::snap_affine(std::cos(rad));
  const double sinA = LayerSmartObject::snap_affine(std::sin(rad));

  const gfx::PointF corners[4] = {
    gfx::PointF(0.0, 0.0),
    gfx::PointF(static_cast<double>(W), 0.0),
    gfx::PointF(static_cast<double>(W), static_cast<double>(H)),
    gfx::PointF(0.0, static_cast<double>(H))
  };

  CornerBounds b;
  double minX = 1e18, maxX = -1e18;
  double minY = 1e18, maxY = -1e18;

  for (int i = 0; i < 4; ++i) {
    double dx = (corners[i].x - pivot.x) * scaleX;
    double dy = (corners[i].y - pivot.y) * scaleY;

    double rx = dx * cosA - dy * sinA;
    double ry = dx * sinA + dy * cosA;

    b.transformed[i].x = LayerSmartObject::snap_affine(pivot.x + rx);
    b.transformed[i].y = LayerSmartObject::snap_affine(pivot.y + ry);

    minX = std::min(minX, static_cast<double>(b.transformed[i].x));
    maxX = std::max(maxX, static_cast<double>(b.transformed[i].x));
    minY = std::min(minY, static_cast<double>(b.transformed[i].y));
    maxY = std::max(maxY, static_cast<double>(b.transformed[i].y));
  }

  minX = LayerSmartObject::snap_affine(minX);
  maxX = LayerSmartObject::snap_affine(maxX);
  minY = LayerSmartObject::snap_affine(minY);
  maxY = LayerSmartObject::snap_affine(maxY);

  const double normAngle = std::fmod(std::abs(angle), 360.0);
  const double rem90 = std::fmod(normAngle, 90.0);
  const bool isRightAngle = (rem90 < 0.001 || std::abs(rem90 - 90.0) < 0.001);

  if (isRightAngle) {
    int targetWs = std::max(1, static_cast<int>(std::round(W * std::abs(scaleX))));
    int targetHs = std::max(1, static_cast<int>(std::round(H * std::abs(scaleY))));
    int angleInt = static_cast<int>(std::round(normAngle)) % 360;
    if (angle < 0.0 && angleInt != 0)
      angleInt = 360 - angleInt;

    if (angleInt == 90 || angleInt == 270) {
      b.dstW = targetHs;
      b.dstH = targetWs;
    }
    else {
      b.dstW = targetWs;
      b.dstH = targetHs;
    }
    b.localX = static_cast<int>(std::floor(LayerSmartObject::snap_affine(minX)));
    b.localY = static_cast<int>(std::floor(LayerSmartObject::snap_affine(minY)));
  }
  else {
    b.localX = static_cast<int>(std::floor(minX));
    b.localY = static_cast<int>(std::floor(minY));
    b.dstW = std::max(1, static_cast<int>(std::ceil(maxX)) - b.localX);
    b.dstH = std::max(1, static_cast<int>(std::ceil(maxY)) - b.localY);
  }
  return b;
}

void LayerSmartObject::renderTransformedImage(const Image* srcImg,
                                              ImageRef& dstImg,
                                              gfx::Point& celPos) const
{
  if (!srcImg)
    return;

  const int W = srcImg->width();
  const int H = srcImg->height();
  if (W <= 0 || H <= 0)
    return;

  // 1. Identity transform: bit-identical copy
  const bool isIdentity = (std::abs(m_scale.x - 1.0) < 1e-9 &&
                           std::abs(m_scale.y - 1.0) < 1e-9 &&
                           std::fmod(std::abs(m_angle), 360.0) < 1e-9 &&
                           !m_flipH &&
                           !m_flipV);
  if (isIdentity) {
    dstImg.reset(Image::createCopy(srcImg));
    celPos = gfx::Point(static_cast<int>(std::floor(m_position.x)),
                        static_cast<int>(std::floor(m_position.y)));
    return;
  }

  // 2. Base image with flips applied (folding negative scale signs)
  const bool effFlipH = (m_flipH != (m_scale.x < 0.0));
  const bool effFlipV = (m_flipV != (m_scale.y < 0.0));
  const double absScaleX = std::abs(m_scale.x);
  const double absScaleY = std::abs(m_scale.y);

  ImageRef baseImg;
  if (effFlipH || effFlipV) {
    baseImg.reset(Image::createCopy(srcImg));
    if (effFlipH) {
      doc::algorithm::flip_image(baseImg.get(),
                                 baseImg->bounds(),
                                 doc::algorithm::FlipType::FlipHorizontal);
    }
    if (effFlipV) {
      doc::algorithm::flip_image(baseImg.get(),
                                 baseImg->bounds(),
                                 doc::algorithm::FlipType::FlipVertical);
    }
  }
  else {
    baseImg.reset(Image::createCopy(srcImg));
  }

  const double normAngle = std::fmod(std::abs(m_angle), 360.0);

  // 3. Flips only (no scale, no rotation)
  if (normAngle < 1e-9 &&
      std::abs(absScaleX - 1.0) < 1e-9 &&
      std::abs(absScaleY - 1.0) < 1e-9) {
    dstImg = baseImg;
    celPos = gfx::Point(static_cast<int>(std::floor(m_position.x)),
                        static_cast<int>(std::floor(m_position.y)));
    return;
  }

  // 4. Scaling only (no rotation)
  if (normAngle < 1e-9) {
    int targetW = std::max(1, static_cast<int>(std::round(W * absScaleX)));
    int targetH = std::max(1, static_cast<int>(std::round(H * absScaleY)));

    dstImg.reset(Image::create(baseImg->pixelFormat(), targetW, targetH));
    doc::algorithm::resize_image(baseImg.get(),
                                 dstImg.get(),
                                 doc::algorithm::RESIZE_METHOD_NEAREST_NEIGHBOR,
                                 nullptr,
                                 nullptr,
                                 baseImg->maskColor());

    celPos = gfx::Point(static_cast<int>(std::floor(m_position.x)),
                        static_cast<int>(std::floor(m_position.y)));
    return;
  }

  // 5. Orthogonal right-angle rotation with arbitrary scale
  const double rem90 = std::fmod(normAngle, 90.0);
  const bool isRightAngle = (rem90 < 0.001 || std::abs(rem90 - 90.0) < 0.001);

  if (isRightAngle) {
    int angleInt = static_cast<int>(std::round(normAngle)) % 360;
    if (m_angle < 0.0 && angleInt != 0) {
      angleInt = 360 - angleInt;
    }

    int targetW = std::max(1, static_cast<int>(std::round(W * absScaleX)));
    int targetH = std::max(1, static_cast<int>(std::round(H * absScaleY)));

    ImageRef scaledImg;
    const bool isUnitScale = (std::abs(absScaleX - 1.0) < 1e-9 &&
                              std::abs(absScaleY - 1.0) < 1e-9);
    if (isUnitScale) {
      scaledImg = baseImg;
    }
    else {
      scaledImg.reset(Image::create(baseImg->pixelFormat(), targetW, targetH));
      doc::algorithm::resize_image(baseImg.get(),
                                   scaledImg.get(),
                                   doc::algorithm::RESIZE_METHOD_NEAREST_NEIGHBOR,
                                   nullptr,
                                   nullptr,
                                   baseImg->maskColor());
    }

    CornerBounds b = calcCornerBounds(W, H, m_pivot, absScaleX, absScaleY, m_angle);
    celPos = gfx::Point(static_cast<int>(std::floor(m_position.x + b.localX)),
                        static_cast<int>(std::floor(m_position.y + b.localY)));

    if (angleInt == 0) {
      dstImg = scaledImg;
      return;
    }
    else if (angleInt == 90) {
      dstImg.reset(Image::create(baseImg->pixelFormat(), targetH, targetW));
      doc::rotate_image(scaledImg.get(), dstImg.get(), 90);
      return;
    }
    else if (angleInt == 180) {
      dstImg.reset(Image::create(baseImg->pixelFormat(), targetW, targetH));
      doc::rotate_image(scaledImg.get(), dstImg.get(), 180);
      return;
    }
    else if (angleInt == 270) {
      dstImg.reset(Image::create(baseImg->pixelFormat(), targetH, targetW));
      doc::rotate_image(scaledImg.get(), dstImg.get(), -90);
      return;
    }
  }

  // 6. Arbitrary angle
  CornerBounds b = calcCornerBounds(W, H, m_pivot, absScaleX, absScaleY, m_angle);

  celPos = gfx::Point(static_cast<int>(std::floor(m_position.x + b.localX)),
                      static_cast<int>(std::floor(m_position.y + b.localY)));

  dstImg.reset(Image::create(baseImg->pixelFormat(), b.dstW, b.dstH));
  doc::clear_image(dstImg.get(), baseImg->maskColor());

  int x1 = static_cast<int>(std::round(b.transformed[0].x - b.localX));
  int y1 = static_cast<int>(std::round(b.transformed[0].y - b.localY));
  int x2 = static_cast<int>(std::round(b.transformed[1].x - b.localX));
  int y2 = static_cast<int>(std::round(b.transformed[1].y - b.localY));
  int x4 = static_cast<int>(std::round(b.transformed[3].x - b.localX));
  int y4 = static_cast<int>(std::round(b.transformed[3].y - b.localY));
  int x3 = x2 + x4 - x1;
  int y3 = y2 + y4 - y1;

  if (isRightAngle) {
    doc::algorithm::parallelogram(dstImg.get(),
                                  baseImg.get(),
                                  nullptr,
                                  x1, y1, x2, y2, x3, y3, x4, y4);
  }
  else {
    try {
      doc::algorithm::rotsprite_image(dstImg.get(),
                                      baseImg.get(),
                                      nullptr,
                                      x1, y1, x2, y2, x3, y3, x4, y4);
    }
    catch (const std::bad_alloc&) {
      doc::algorithm::parallelogram(dstImg.get(),
                                    baseImg.get(),
                                    nullptr,
                                    x1, y1, x2, y2, x3, y3, x4, y4);
    }
  }
}

bool LayerSmartObject::renderCel(frame_t hostFrame)
{
  return renderCel(hostFrame, mapTimelineFrame(hostFrame));
}

bool LayerSmartObject::renderCel(frame_t hostFrame, frame_t linkedFrame)
{
  ImageRef srcImg = compositeSourceFrame(linkedFrame);
  if (!srcImg)
    return false;

  ImageRef dstImg;
  gfx::Point celPos;
  renderTransformedImage(srcImg.get(), dstImg, celPos);
  if (!dstImg)
    return false;

  Cel* c = cel(hostFrame);
  if (c) {
    if (c->link()) {
      removeCel(c);
      delete c;
      c = new Cel(hostFrame, dstImg);
      c->setPosition(celPos);
      addCel(c);
    }
    else {
      c->data()->setImage(dstImg, this);
      c->setPosition(celPos);
    }
  }
  else {
    c = new Cel(hostFrame, dstImg);
    c->setPosition(celPos);
    addCel(c);
  }
  return true;
}

void LayerSmartObject::renderAllCels()
{
  if (sprite()) {
    syncFrames(sprite()->totalFrames());
  }
  else if (cels().empty()) {
    renderCel(frame_t(0), frame_t(0));
    return;
  }

  for (Cel* c : cels()) {
    if (c && c->link() == nullptr) {
      frame_t lf = mapTimelineFrame(c->frame());
      renderCel(c->frame(), lf);
    }
  }
}

} // namespace doc
