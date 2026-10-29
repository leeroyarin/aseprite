# Aseprite 2.0 Versioning — on-disk format

Owner: `src/app/versions/version_store.{h,cpp}` (C++). MCP server may read (never write) these files.

## Layout
For document `C:/art/hero.aseprite`:
```
C:/art/hero.aseprite
C:/art/hero.aseprite.versions/
    manifest.json
    v0001_20261027-160512.aseprite
    v0001_20261027-160512.png        (thumbnail, max 128x128, first frame flattened)
    v0002_20261027-161030.aseprite
    v0002_20261027-161030.png
```
- Folder name = full doc filename + `.versions` (keeps different extensions of same title separate).
- Snapshot always saved as `.aseprite` (lossless, keeps layers/frames/tags) even if doc is `.png`/`.gif`.
- Ids are monotonically increasing integers, never reused (use `nextId` in manifest).

## manifest.json
```json
{
  "format": 1,
  "document": "hero.aseprite",
  "nextId": 3,
  "versions": [
    {
      "id": 1,
      "file": "v0001_20261027-160512.aseprite",
      "thumbnail": "v0001_20261027-160512.png",
      "created": "2026-10-27T16:05:12+05:45",
      "note": "Base pose",
      "author": "user",
      "width": 32, "height": 32,
      "frames": 4, "layers": 3,
      "bytes": 2841
    }
  ]
}
```
- `author`: `"user"` (UI) | `"agent"` (MCP) | `"auto"` (auto-backup before restore / auto-version).
- Writers: write `manifest.json.tmp` then rename (atomic-ish).
- Missing/corrupt manifest: rebuild by scanning `v*.aseprite` files (note = "", author = "unknown").

## Operations
| op | behavior |
|---|---|
| save | snapshot current doc state (incl. unsaved changes) → new id. Doc filename/dirty flag unchanged. |
| list | newest first in UI; manifest stores ascending. |
| restore(id) | save auto-backup version (author `auto`, note `Auto-backup before restoring v<id>`), then load snapshot and replace doc sprite content; doc marked modified. |
| open(id) | open snapshot file as new document tab (read-only intent; title `hero (v1)`). |
| delete(id) | remove files + manifest entry. |
