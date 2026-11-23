# Easymetric objects

Added 2026-11-17, updated 2026-11-18 for the **shared model size** architecture.

## Why

The six views of one object constrain each other: the 3D model is the intersection of their silhouettes. When multiple parts (head, torso, limbs) are drawn in a single set of sheets, one part's Top silhouette can cut away another part. With objects, each part is its own layer group containing its own six views (`Surface_Base`, `Surface_Texture`, `Surface_DEPTH`).

### Shared Model Size Architecture (2026-11-18)
All objects share **one model size** ($W \times H \times D$). Every object is drawn in the same six cells and the same model space, like Blender objects sharing a single 3D world coordinate system.
- An object's location is simply where it is drawn inside the shared six cells.
- Offsets ($o$) are always $[0, 0, 0]$.
- The faded overlay lines up 1:1 with all other objects without coordinate translation.
- Resizing the model updates every object's bands together, eliminating misalignment and slab artifacts.

## Data Model

| Component | Object 1 (Root) | Additional Objects |
|---|---|---|
| **Layers** | `Surface_Base`, `Surface_Texture`, `Surface_DEPTH` at top level | Top-level group `Object: <name>` with its own three child sheets |
| **Slices** | Slices `TOP` .. `LEFT` | Slices `<key>:TOP` .. `<key>:LEFT` |
| **Metadata** | Sprite user-data property `easymetric_root` (`rank=N;hidden=0/1`) | Group user-data property `easymetric` (`key=..;o=0,0,0;rank=N;order=N`) |
| **Canvas Band** | Rows `0 .. bandHeight-1` | Own horizontal band stacked below earlier bands |
| **Cell Dimensions**| $W \times H \times D$ (shared model size) | $W \times H \times D$ (identical across all bands) |

- **Key**: Unique identifier assigned at object creation. Renaming the group only changes display name.
- **Offset `o`**: Always `0,0,0`. Position is determined directly by painted pixels within the shared model space.
- **Rank**: `-1` = automatic priority. Manual rank `0..9` overrides automatic priority (higher wins). Automatic objects: the one with the smaller **painted** volume (the box around what is drawn on frame 0) is in front. Every object's box is the model size, so the box itself can't decide. Ties go to creation order (the later one wins). Priority only matters where two objects' surfaces coincide.
- **Single-Object Invariant**: Documents without object groups behave identically to legacy single-object files. Render goldens (`tests/check_golden.py`) enforce this.

## Model Resizing: Stretch vs Keep

Resizing the model changes the cell dimensions of all objects simultaneously via one undoable transaction (`Tx`). When resizing through the UI or `modelSize` action, two modes are available:

```
Original (16x16x16 Block in 16x16x16 Model):
+----------------+
|################|
|################|
|################|
|################|
+----------------+

Resize to 32x32x32:

Mode: Stretch (Scale nearest-neighbour)
+--------------------------------+
|################################|
|################################|
|################################|
|################################|
+--------------------------------+
All views scale proportionally across all frames.

Mode: Keep (Centred in model space)
+--------------------------------+
|        ........                |
|        .######.                |
|        .######.                |
|        ........                |
+--------------------------------+
Pixels stay unscaled and centred with symmetric margins.
Opposite views remain mirror-consistent.
```

- **`stretch`**: Nearest-neighbour scaling of Base, Texture, and Depth across all frames. Retains full coverage of drawings in expanded volumes.
- **`keep`**: Preserves original drawing pixels unscaled, centring them in the new dimensions. If shrinking, crops symmetrically around the center.

## Legacy Migration (`unifyObjects`)

Files created under the experimental per-object size model are migrated to the shared model size:
1. **Union Box**: Computes the bounding box enclosing all objects based on their legacy sizes and offsets.
2. **Re-anchoring**: Each object's pixels are transferred to their respective positions inside the new unified space.
3. **Reset Offsets**: Group metadata is updated with $o = [0, 0, 0]$ and identical slice bounds.
4. **Automatic, no prompt**: opening such a file in the studio migrates it straight away and shows the status tip "Objects now share one W x H x D model space (undo to revert)". **One undo reverts it.** Headless scripts call `action="unifyObjects"`. Size-changing actions (`newObject`, `modelSize`, `linkDims`, `blockLayout`, `deleteObject`) migrate first. `state`, `previewStats` and `exportObj` don't.
5. **What stays the same**: each object's shape and place. Each object's volume now spans the whole model space, so thin outline and edge-shading lines at volume boundaries can differ by a few pixels in the preview: about 70-120 px of 144x144 on Arin's test file.

## How the Code Scopes Objects

`g_objectScopeSprite` and `g_objectScopeKey` store the active object ("" = root). Helper methods automatically resolve scoped layers and slices:
- `findSliceForFace`: Retrieves `<key>:<FACE>` for active object, or unkeyed slice for root.
- `findLayerByName` / `findOrCreateLayer`: Resolves or creates `Surface_*` sheets inside the object group.
- `findSharedLayers`, `sharedFaceSizes`, `sharedFaceRect`, `sharedSheetSize`: Compute bounds corresponding to the scoped object's canvas band.
- `atlasSize`: Canvas dimensions required to accommodate all stacked bands.
- `rebuildAtlas`: Unified layout rebuild executed inside a single `Tx` transaction.

## 3D Preview & Composite Rendering

`compute3DPreviewMulti` renders all visible objects into a composite 3D viewport:
- **Volume Cache**: `cachedSculptedVolume` caches up to 8 sculpted volumes keyed by content hash (face sizes, pixels, depth, links, pivot). Camera rotation and single-object edits render in ~7ms instead of ~75ms.
- **Depth & Priority**: Per-pixel depth buffer tracks the nearest surface hit. Within 0.5px depth ties, priority/rank determines the visible pixel.
- **OBJ Export**: Headless `exportObj` writes one `o <name>` mesh per visible object with world transforms aligned to preview coordinates.

## Studio Interface

- **Model Size**: `Model size  W: [  ] H: [  ] D: [  ]` + **Apply Model Size** button. Prompt options: *Stretch*, *Keep*, *Cancel*.
- **Direct Entry**: Typing a number in `NumberField` and pressing Enter applies the size and does not trigger animation playback.
- **Object Selector**: Combo box to switch active object + **New object...** and **Delete object** buttons; **Show others** sits on its own row.
- **Delete object** (2026-11-18):
  - It asks first, naming the object you will land on.
  - It removes the focused object (all frames) and closes its band. Object 1 can't be deleted, so the button is disabled there.
  - Focus **falls back to the object before it** in the list, or to Object 1 when it was the first.
  - The same fallback applies when the focused object disappears another way: the `deleteObject` script action, deleting its group in the timeline, or undoing its creation.
  - One undo brings it back.
- **New Object Dialog**:
  - Name input.
  - Optional starting block checkbox (`block=true`).
  - Block size ($bw, bh, bd$) and placement preset (`above`, `below`, `left`, `right`, `front`, `back`, `center`) relative to existing objects.
  - Starting block is placed on the **current active frame** (not frame 0).
- **Faded Overlay**: Non-focused visible objects render at ~35% alpha in empty areas of the focused view to aid spatial alignment.

## Headless API Contract

Invoked via `app.command.EasymetricStudio{ action = ..., ... }`:

| Action | Parameters | Description |
|---|---|---|
| `modelSize` | `width`, `height`, `depth`, `mode` (`"stretch"` \| `"keep"`, default `"keep"`) | Resizes all objects to $W \times H \times D$ in one undo step. |
| `newObject` | `name`, `block` (bool, default false), `bw`, `bh`, `bd` (default 8; `width`/`height`/`depth` are aliases), `place` (default `center`; relative to what `object`, or Object 1, has painted), `r`, `g`, `b`, `frame` (**0-based**, default 0) | Creates `Object: <name>` at model size. The block lands on `frame` only, clamped into the model. `ox oy oz` are ignored. |
| `unifyObjects`| *(none)* | Migrates legacy multi-sized object document to shared union size. |
| `setObject` | `object`, `rank` (-1..9), `visible` (bool), `name` | Updates object metadata or visibility. |
| `deleteObject`| `object` | Deletes object group and associated band slices (Object 1 cannot be deleted). |
| `focusObject` | `object` | Changes studio focus to specified object. |
| `previewStats`| `yaw`, `pitch`, `out`, `raw` (optional path) | Render statistics and per-object visible pixel counts, plus `hash` (FNV-1a hex of the diffuse image). `raw` also writes the diffuse as raw RGBA bytes (`outW` x `outH`): in Lua, `Image(outW, outH).bytes = <file>`, then `:saveAs(png)`. |
| `exportObj` | `out` | Exports multi-object wavefront OBJ file. |

### State Inspection (`action="state"`)
- `modelSize`: `[W, H, D]` array representing the global model dimensions.
- `objects[]`: Array of object metadata. Every entry has `offset = [0, 0, 0]` and `box = [W, H, D]`.

## Tests

- `tests/easymetric_objects_test.lua` (headless, 16 cases, in `run_all.py`). It covers:
  - Stretch turns Arin's slab into a cube;
  - Keep centres the drawing;
  - the block lands on its zero-based frame;
  - migration preserves each object's footprint within 5%.
- `tests/ui/objects_test.py` (studio, 21 checks) and `tests/ui/fields_test.py` (Model size row,
  Stretch/Keep/Cancel question, Enter applies without changing the frame).
- `tests/ui/delete_object_test.py` (8 checks: confirm/cancel, fallback to previous, fallback to Object 1, undo, script delete).
- `tools/ui-test/realfile_shared_size.py`: real-file check on a temp copy of
  `Easymetric-Shubha-Model-Test.aseprite`. It is not in `run_all.py` because it needs that file.
