// Aseprite Document Library
// Copyright (c) 2026 Igara Studio S.A.
//
// This file is released under the terms of the MIT license.
// Read LICENSE.txt for more information.

#ifndef DOC_LAYER_SMART_OBJECT_H_INCLUDED
#define DOC_LAYER_SMART_OBJECT_H_INCLUDED
#pragma once

#include "doc/frame.h"
#include "doc/image_ref.h"
#include "doc/layer.h"
#include "gfx/point.h"
#include "gfx/rect.h"
#include "gfx/size.h"

#include <cstdint>
#include <functional>
#include <string>

namespace doc {

class Document;

enum class TimelineMapping {
  Loop = 0,
  Hold = 1,
  Fixed = 2,
};

enum class SmartObjectStatus {
  Linked = 0,    // File present on disk and memory is up to date
  Outdated = 1,  // File present on disk has newer modification timestamp
  Missing = 2,   // File not found on disk at specified path
  Broken = 3,    // File found but cannot be parsed / corrupted / 0 bytes
};

class LayerSmartObject final : public LayerImage {
public:
  LayerSmartObject(Sprite* sprite, const std::string& filepath);
  ~LayerSmartObject() override;

  LayerSmartObject* clone(Sprite* sprite) const override;

  // External linked filepath
  const std::string& filepath() const { return m_filepath; }
  void setFilepath(const std::string& filepath);

  // 2D Placement and Transformation parameters
  const gfx::PointF& position() const { return m_position; }
  void setPosition(const gfx::PointF& pos);
  void setPosition(double x, double y) { setPosition(gfx::PointF(x, y)); }

  const gfx::PointF& scale() const { return m_scale; }
  void setScale(const gfx::PointF& scale);
  void setScale(double sx, double sy) { setScale(gfx::PointF(sx, sy)); }

  double angle() const { return m_angle; }
  void setAngle(double angle);

  bool flipH() const { return m_flipH; }
  void setFlipH(bool flipH);

  bool flipV() const { return m_flipV; }
  void setFlipV(bool flipV);

  // Pivot point normalized (0.0..1.0), default (0.5, 0.5)
  const gfx::PointF& pivot() const { return m_pivot; }
  void setPivot(const gfx::PointF& p);
  void setPivot(double px, double py) { setPivot(gfx::PointF(px, py)); }

  // Compound transformation methods
  void setTransform(const gfx::PointF& pos,
                    const gfx::PointF& scale,
                    double angle,
                    bool flipH,
                    bool flipV,
                    const gfx::PointF& pivot = gfx::PointF(0.5, 0.5));
  void resetTransform();

  // Source document canvas dimensions
  const gfx::Size& sourceSize() const { return m_sourceSize; }
  void setSourceSize(const gfx::Size& size);

  // Bounding rect on host canvas
  gfx::RectF boundsF() const;
  gfx::Rect bounds() const;

  // Modification timestamp of linked file on disk (unix epoch ms/sec)
  uint64_t lastModifiedTime() const { return m_lastModifiedTime; }
  void setLastModifiedTime(uint64_t t);

  // File change detection & reload API
  SmartObjectStatus linkStatus() const;
  bool isDiskFileNewer() const;
  bool reloadSource();
  bool reloadSource(bool force);
  std::string resolveFilepath() const;

  // Static timestamp utility
  static uint64_t fileModificationEpoch(const std::string& path);

  // Detached offscreen Document reference (non-owning cache pointer)
  Document* linkedDoc() const;
  void setLinkedDoc(Document* doc);
  Document* sourceDoc() const { return linkedDoc(); }
  void setSourceDoc(Document* doc) { setLinkedDoc(doc); }

  // Timeline synchronization & frame mapping
  TimelineMapping timelineMapping() const { return m_timelineMapping; }
  void setTimelineMapping(TimelineMapping mode);

  frame_t mapTimelineFrame(frame_t hostFrame) const;
  frame_t linkedFrameCount() const;

  void syncFrames(frame_t totalFrames);
  void syncFrames();

  void displaceFrames(frame_t fromThis, frame_t delta) override;
  void addCel(Cel* cel) override;

  // Rendering pipeline
  bool renderCel(frame_t hostFrame);
  bool renderCel(frame_t hostFrame, frame_t linkedFrame);
  void renderAllCels();

  // UserData property persistence
  void syncPropertiesToUserData();
  void readPropertiesFromUserData();
  void setUserData(const UserData& userData) override;

  int getMemSize() const override;

  // Float noise filtering (< 1e-9) adapted from Rust reference
  static double snap_affine(double v, double eps = 1e-9);
  static gfx::PointF snap_affine(const gfx::PointF& pt, double eps = 1e-9);

  // Detached doc loader and composite renderer hooks
  using SourceDocLoader = std::function<Document*(const std::string&)>;
  using CompositeRenderer = std::function<ImageRef(Document*, frame_t, const Sprite*)>;

  static void setSourceDocLoader(SourceDocLoader loader);
  static void setCompositeRenderer(CompositeRenderer renderer);
  static Document* getDetachedDoc(const std::string& filepath);
  static uint64_t getDetachedDocTimestamp(const std::string& filepath);
  static void setDetachedDoc(const std::string& filepath, Document* doc, uint64_t timestamp = 0);
  static void clearDetachedDocsCache();

private:
  void renderTransformedImage(const Image* srcImg, ImageRef& dstImg, gfx::Point& celPos) const;
  ImageRef compositeSourceFrame(frame_t linkedFrame) const;

  std::string m_filepath;
  gfx::PointF m_position;       // Canvas placement translation (X, Y)
  gfx::PointF m_scale;          // 2D scale factor (X, Y)
  double m_angle;               // Rotation angle in degrees
  bool m_flipH;                 // Horizontal flip
  bool m_flipV;                 // Vertical flip
  gfx::PointF m_pivot;          // Pivot point normalized (0.0..1.0), default (0.5, 0.5)
  TimelineMapping m_timelineMapping; // Animation timeline mapping mode (Loop, Hold, Fixed)
  gfx::Size m_sourceSize;       // Linked document canvas size (width, height)
  uint64_t m_lastModifiedTime;  // Timestamp on disk when last loaded/synchronized
  mutable Document* m_linkedDoc;// Detached document pointer (cached, non-owning)
};

} // namespace doc

#endif
