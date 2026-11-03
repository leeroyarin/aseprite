// Aseprite 2.0
// Copyright (C) 2026 Antigravity / Aseprite 2.0 contributors
//
// This program is distributed under the terms of
// the End-User License Agreement for Aseprite.

#ifndef APP_COMMANDS_EASYMETRIC_SHEETS_H_INCLUDED
#define APP_COMMANDS_EASYMETRIC_SHEETS_H_INCLUDED

#include "app/commands/easymetric_model.h"
#include "gfx/size.h"
#include <string>

namespace doc {
class Sprite;
class Slice;
class LayerImage;
} // namespace doc
namespace app {
class Doc;
class DocApi;
namespace easymetric {

struct SurfaceLayerPair {
  doc::LayerImage* baseLayer = nullptr;
  doc::LayerImage* texLayer = nullptr;
};

struct SharedSurfaceLayers {
  doc::LayerImage* baseLayer = nullptr;
  doc::LayerImage* textureLayer = nullptr;
  doc::LayerImage* depthLayer = nullptr;
};

int matchFaceIndex(const std::string& name);
doc::Slice* findSliceForFace(doc::Sprite* spr, int face);
doc::LayerImage* findLayerByName(doc::Sprite* spr, const std::string& name);
doc::LayerImage* findOrCreateLayer(Doc* doc, DocApi& api, doc::Sprite* spr, const std::string& name);
SurfaceLayerPair findSurfaceLayers(doc::Sprite* spr, int face);
SharedSurfaceLayers findSharedLayers(doc::Sprite* spr);
void sharedFaceSizes(doc::Sprite* spr, int w[kFaceCount], int h[kFaceCount]);
void sharedLayout(const int w[kFaceCount], const int h[kFaceCount], int colW[3], int rowH[2]);
gfx::Rect sharedUVRectExact(int face, const int colW[3], const int rowH[2], int faceW, int faceH);
gfx::Rect sharedFaceRect(doc::Sprite* spr, int face);
gfx::Size sharedSheetSize(doc::Sprite* spr);
void rebuildSharedSheets(Doc* doc, const int newW[kFaceCount], const int newH[kFaceCount]);
void setupSharedSurfaceLayers(Doc* doc, bool seedStarterIfBlank);
void propagateLinkedDimensions(Doc* doc, SixSurfaceData& data, int srcFace, int newW, int newH);
void propagateLinkedDimensions(Doc* doc, int srcFace, int newW, int newH);
void setupTwoLayerStructure(Doc* doc, bool asSmartObjects);
void convertSmartObjectsToGroups(Doc* doc);

} // namespace easymetric
} // namespace app

#endif

