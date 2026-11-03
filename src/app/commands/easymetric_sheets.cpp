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
#include "app/cmd/set_slice_key.h"
#include "app/commands/command.h"
#include "app/commands/commands.h"
#include "app/commands/easymetric_model.h"
#include "app/commands/easymetric_render.h"
#include "app/commands/easymetric_sheets.h"
#include "app/commands/easymetric_ui.h"
#include "app/commands/easymetric_volume.h"
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
#include "app/ui/doc_view.h"
#include "app/ui/editor/editor.h"
#include "app/ui/editor/editor_view.h"
#include "app/ui/editor/standby_state.h"
#include "app/ui/skin/skin_theme.h"
#include "app/ui/status_bar.h"
#include "app/ui_context.h"
#include "app/util/conversion_to_surface.h"
#include "base/fs.h"
#include "doc/cel.h"
#include "doc/cels_range.h"
#include "doc/color.h"
#include "doc/frames_sequence.h"
#include "doc/image.h"
#include "doc/layer.h"
#include "doc/layer_smart_object.h"
#include "doc/mask.h"
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
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace app { namespace easymetric {

int matchFaceIndex(const std::string& name)
{
  if (name.empty())
    return -1;
  std::string lower;
  lower.reserve(name.size());
  for (char c : name)
    lower.push_back(std::tolower(static_cast<unsigned char>(c)));

  if (lower.rfind("surface_", 0) == 0)
    lower = lower.substr(8);
  else if (lower.rfind("face_", 0) == 0)
    lower = lower.substr(5);

  if (lower == "top")
    return kFaceTop;
  if (lower == "down" || lower == "bottom")
    return kFaceDown;
  if (lower == "front")
    return kFaceFront;
  if (lower == "back")
    return kFaceBack;
  if (lower == "right")
    return kFaceRight;
  if (lower == "left")
    return kFaceLeft;
  return -1;
}

doc::Slice* findSliceForFace(doc::Sprite* spr, int face)
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

doc::LayerImage* findLayerByName(doc::Sprite* spr, const std::string& name)
{
  if (!spr)
    return nullptr;
  for (doc::Layer* lyr : spr->allLayers()) {
    if (lyr->isImage() && lyr->name() == name)
      return static_cast<doc::LayerImage*>(lyr);
  }
  return nullptr;
}

doc::LayerImage* findOrCreateLayer(app::Doc* doc,
                                   app::DocApi& api,
                                   doc::Sprite* spr,
                                   const std::string& name)
{
  if (auto* existing = findLayerByName(spr, name))
    return existing;
  return api.newLayer(spr->root(), name);
}

SurfaceLayerPair findSurfaceLayers(doc::Sprite* spr, int face)
{
  SurfaceLayerPair pair;
  if (!spr || face < 0 || face >= kFaceCount)
    return pair;

  const std::string faceName = kFaceSpecs[face].sliceName;
  const std::string baseName = "Surface_" + faceName + "_Base";
  const std::string texName = "Surface_" + faceName + "_Texture";
  const std::string defaultName = "Surface_" + faceName;
  const std::string groupName = "Surface_" + faceName + "_Group";

  // 1. Check if there's a group named Surface_<Face>, Surface_<Face>_Group, or
  // Surface_<Face>_Texture (unpacked SO)
  for (doc::Layer* lyr : spr->allLayers()) {
    if (lyr->isGroup() &&
        (lyr->name() == defaultName || lyr->name() == groupName || lyr->name() == texName ||
         (face == kFaceDown &&
          (lyr->name() == "Surface_BOTTOM" || lyr->name() == "Surface_Bottom" ||
           lyr->name() == "Surface_BOTTOM_Group" || lyr->name() == "Surface_Bottom_Group")))) {
      doc::LayerGroup* grp = static_cast<doc::LayerGroup*>(lyr);
      for (doc::Layer* child : grp->layers()) {
        if (!child->isImage())
          continue;
        doc::LayerImage* childImg = static_cast<doc::LayerImage*>(child);
        const std::string& cn = child->name();
        if (cn == "Texture" || cn == "texture" || cn == texName || child->isSmartObject()) {
          if (!pair.texLayer)
            pair.texLayer = childImg;
        }
        else if (cn == "Base" || cn == "base" || cn == baseName || !pair.baseLayer) {
          if (!pair.baseLayer)
            pair.baseLayer = childImg;
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
SharedSurfaceLayers findSharedLayers(doc::Sprite* spr)
{
  SharedSurfaceLayers shared;
  if (!spr)
    return shared;
  shared.baseLayer = findLayerByName(spr, "Surface_Base");
  shared.textureLayer = findLayerByName(spr, "Surface_Texture");
  shared.depthLayer = findLayerByName(spr, "Surface_DEPTH");
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
void sharedFaceSizes(doc::Sprite* spr, int w[kFaceCount], int h[kFaceCount])
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

void sharedLayout(const int w[kFaceCount], const int h[kFaceCount], int colW[3], int rowH[2])
{
  for (int c = 0; c < 3; ++c)
    colW[c] = 1;
  for (int r = 0; r < 2; ++r)
    rowH[r] = 1;
  for (int f = 0; f < kFaceCount; ++f) {
    colW[kFaceSpecs[f].gridCol] = std::max(colW[kFaceSpecs[f].gridCol], w[f]);
    rowH[kFaceSpecs[f].gridRow] = std::max(rowH[kFaceSpecs[f].gridRow], h[f]);
  }
}

// UV rect of one face inside the exact layout above.
gfx::Rect sharedUVRectExact(int face, const int colW[3], const int rowH[2], int faceW, int faceH)
{
  if (face < 0 || face >= kFaceCount)
    return gfx::Rect(0, 0, faceW, faceH);
  int x = 0, y = 0;
  for (int c = 0; c < kFaceSpecs[face].gridCol; ++c)
    x += colW[c];
  for (int r = 0; r < kFaceSpecs[face].gridRow; ++r)
    y += rowH[r];
  return gfx::Rect(x, y, faceW, faceH);
}

// UV rect for a face in the current document, from its slice size
// and the exact layout (not from the sprite size). This also
// repairs documents whose slices were left at (0, 0) by the old
// per-face layout: every face would otherwise sample the same
// top-left cell of the shared sheet.
gfx::Rect sharedFaceRect(doc::Sprite* spr, int face)
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
gfx::Size sharedSheetSize(doc::Sprite* spr)
{
  if (!spr)
    return gfx::Size(0, 0);
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
void rebuildSharedSheets(Doc* doc, const int newW[kFaceCount], const int newH[kFaceCount])
{
  if (!doc || !doc->sprite())
    return;
  doc::Sprite* spr = doc->sprite();
  SharedSurfaceLayers shared = findSharedLayers(spr);
  if (!shared.baseLayer && !shared.textureLayer)
    return;

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
      if (!lyr)
        return;
      for (doc::frame_t fr = doc::frame_t(0); fr <= spr->lastFrame(); ++fr) {
        doc::Cel* cel = lyr->cel(fr);
        if (!cel || !cel->image())
          continue;
        doc::ImageRef resized(
          doc::Image::create(cel->image()->pixelFormat(), newSheetW, newSheetH));
        resized->clear(0);
        for (int f = 0; f < kFaceCount; ++f) {
          const gfx::Rect newUv = sharedUVRectExact(f, newColW, newRowH, newW[f], newH[f]);
          const int copyW = std::min(oldRect[f].w, newUv.w);
          const int copyH = std::min(oldRect[f].h, newUv.h);
          for (int v = 0; v < copyH; ++v) {
            const int srcY = oldRect[f].y + v;
            if (srcY < 0 || srcY >= cel->image()->height())
              continue;
            for (int u = 0; u < copyW; ++u) {
              const int srcX = oldRect[f].x + u;
              if (srcX < 0 || srcX >= cel->image()->width())
                continue;
              doc::put_pixel(resized.get(),
                             newUv.x + u,
                             newUv.y + v,
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
      if (doc::Slice* sl = findSliceForFace(spr, f))
        tx(new cmd::SetSliceKey(sl, 0, doc::SliceKey(uv)));
    }
    tx.commit();
    doc->notifyGeneralUpdate();
  }
  catch (const std::exception& e) {
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
void setupSharedSurfaceLayers(Doc* doc, bool seedStarterIfBlank)
{
  if (!doc || !doc->sprite())
    return;
  doc::Sprite* spr = doc->sprite();

  // Already shared: nothing to migrate, just make sure the slices
  // point at the UV rects. If the canvas was resized by hand
  // since the sheets were built, the sprite size no longer
  // matches the exact sheet size for the face sizes, so rebuild
  // the sheets to that exact size first (face pixels preserved,
  // see rebuildSharedSheets) instead of leaving padding/gaps
  // between the canvas and the UV mapping.
  SharedSurfaceLayers existing = findSharedLayers(spr);
  if (existing.baseLayer && existing.textureLayer) {
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
      if (!lyr)
        return doc::ImageRef();
      if (doc::Cel* cel = lyr->cel(0)) {
        if (cel->image())
          return doc::ImageRef(doc::Image::createCopy(cel->image()));
      }
      return doc::ImageRef();
    };
    faces[f].baseImg = capture(sp.baseLayer);
    faces[f].texImg = capture(sp.texLayer);
    const doc::Image* anyImg = faces[f].baseImg ?
                                 faces[f].baseImg.get() :
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
    if (faces[f].w <= 0)
      faces[f].w = spr->width();
    if (faces[f].h <= 0)
      faces[f].h = spr->height();
    cellW = std::max(cellW, faces[f].w);
    cellH = std::max(cellH, faces[f].h);
  }
  if (cellW <= 0)
    cellW = spr->width();
  if (cellH <= 0)
    cellH = spr->height();
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
    if (sp.baseLayer)
      layersToRemove.push_back(sp.baseLayer);
    if (sp.texLayer)
      layersToRemove.push_back(sp.texLayer);
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

    auto buildSheet = [&](const char* kind, doc::ImageRef FaceImage::* img) {
      doc::ImageRef sheet(doc::Image::create(spr->pixelFormat(), sheetW, sheetH));
      sheet->clear(0);
      bool hasPixels = false;
      for (int ff = 0; ff < kFaceCount; ++ff) {
        const doc::ImageRef& src = faces[ff].*img;
        if (!src)
          continue;
        const gfx::Rect uv = sharedUVRectExact(ff, colW, rowH, faces[ff].w, faces[ff].h);
        doc::copy_image(sheet.get(), src.get(), uv.x, uv.y);
        hasPixels = true;
      }
      if (!hasPixels && seedStarterIfBlank && std::string(kind) == "base") {
        for (int ff = 0; ff < kFaceCount; ++ff) {
          const gfx::Rect uv = sharedUVRectExact(ff, colW, rowH, faces[ff].w, faces[ff].h);
          for (int v = 0; v < faces[ff].h; ++v)
            for (int u = 0; u < faces[ff].w; ++u)
              doc::put_pixel(sheet.get(),
                             uv.x + u,
                             uv.y + v,
                             starterSurfacePixel(ff, u, v, faces[ff].w, faces[ff].h));
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
        sl->userData().setColor(doc::rgba(kFaceSpecs[f].r, kFaceSpecs[f].g, kFaceSpecs[f].b, 255));
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
    std::sort(layersToRemove.begin(), layersToRemove.end(), [](doc::Layer* a, doc::Layer* b) {
      return a->isImage() && !b->isImage();
    });
    for (doc::Layer* lyr : layersToRemove) {
      // Skip anything that is already gone, or a shared layer.
      bool stillThere = false;
      for (doc::Layer* l : spr->allLayers())
        if (l == lyr) {
          stillThere = true;
          break;
        }
      if (!stillThere)
        continue;
      if (lyr->name() == "Surface_Base" || lyr->name() == "Surface_Texture" ||
          lyr->name() == "Surface_DEPTH")
        continue;
      api.removeLayer(lyr);
    }

    tx.commit();
    doc->notifyGeneralUpdate();
  }
  catch (const std::exception& e) {
    // Ignore: entering the studio will retry the migration.
  }
}

void propagateLinkedDimensions(Doc* doc, SixSurfaceData& data, int srcFace, int newW, int newH)
{
  if (!doc || !doc->sprite())
    return;
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
        if (targetW[f] != oldW[f] || targetH[f] != oldH[f])
          changed = true;
      if (spr->width() != exact.w || spr->height() != exact.h)
        changed = true;
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
        sl->userData().setColor(doc::rgba(kFaceSpecs[f].r, kFaceSpecs[f].g, kFaceSpecs[f].b, 255));
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
          tx(new cmd::SetSliceKey(
            sl,
            it->frame(),
            doc::SliceKey(curBounds, it->value()->center(), it->value()->pivot())));
        }
      }

      // Update layer cels
      SurfaceLayerPair sp = findSurfaceLayers(spr, f);
      auto resizeLayerCel = [&](doc::LayerImage* lyr) {
        if (!lyr)
          return;
        for (doc::frame_t fr = doc::frame_t(0); fr <= spr->lastFrame(); ++fr) {
          doc::Cel* cel = lyr->cel(fr);
          if (!cel || !cel->image())
            continue;
          if (resizedImages.count(cel->image()->id()))
            continue;

          if (cel->image()->width() != targetW || cel->image()->height() != targetH) {
            doc::ImageRef resized(
              doc::Image::create(cel->image()->pixelFormat(), targetW, targetH));
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
        if (!cel || !cel->image())
          continue;
        if (resizedImages.count(cel->image()->id()))
          continue;
        if (cel->image()->width() != maxFaceW || cel->image()->height() != maxFaceH) {
          doc::ImageRef resized(
            doc::Image::create(cel->image()->pixelFormat(), maxFaceW, maxFaceH));
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
    // Ignore
  }
}

void propagateLinkedDimensions(Doc* doc, int srcFace, int newW, int newH)
{
  if (!doc || !doc->sprite())
    return;
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

void setupTwoLayerStructure(Doc* doc, bool asSmartObjects)
{
  if (!doc || !doc->sprite())
    return;
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

void convertSmartObjectsToGroups(Doc* doc)
{
  if (!doc || !doc->sprite())
    return;
  doc::Sprite* spr = doc->sprite();

  Context* ctx = UIContext::instance() ? static_cast<Context*>(UIContext::instance()) :
                                         App::instance()->context();
  if (!ctx)
    return;

  bool convertedAny = false;
  while (true) {
    doc::Layer* target = nullptr;
    for (doc::Layer* lyr : spr->allLayers()) {
      if (lyr->isSmartObject()) {
        target = lyr;
        break;
      }
    }
    if (!target)
      break;

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

}} // namespace app::easymetric
