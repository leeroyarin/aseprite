## 2026-11-03T04:45:31Z
You are Worker M2 (Easymetric C++ Implementation Specialist).

MANDATORY INTEGRITY WARNING:
DO NOT CHEAT. All implementations must be genuine. DO NOT hardcode test results, create dummy/facade implementations, or circumvent the intended task. A teamwork_auditor will independently verify your work. Integrity violations WILL be detected and your work WILL be rejected.

You MUST read the Original Request file first:
`e:\Work\Development\aseprite\.agents\teamwork\ORIGINAL_REQUEST.md`

Your Working Directory: `e:\Work\Development\aseprite\.agents\teamwork\worker_m2`
Your Identity: teamwork_worker
Your Parent / Orchestrator Conversation ID: `f47682be-6cc6-4df9-bbe4-491c1979bf04`
Input Analysis: Read `e:\Work\Development\aseprite\.agents\teamwork\explorer_m2\report.md` and `e:\Work\Development\aseprite\.agents\teamwork\explorer_m2\handoff.md`.

File Ownership:
You EXCLUSIVELY own:
- `src/app/commands/cmd_easymetric.cpp`
- `docs/aseprite2/HANDOFF.md`
Do NOT modify any other files in `src/` or `third_party/`.

Implementation Requirements:
1. `src/app/commands/cmd_easymetric.cpp`:
   - Implement Base-first silhouette clamping per Explorer M2's detailed design:
     a) `SixSurfaceData`: Add `baseAlpha` channel (`std::vector<uint8_t> baseAlpha[6]`), `hasBaseLayer` boolean array (`bool hasBaseLayer[6] = {false}`), and helper method `bool isBaseSolid(int face, int u, int v) const`.
     b) `extractSixSurfaces`:
        - In the extraction loops, populate `out.hasBaseLayer[f]` and store `out.baseAlpha[f]` from `baseImg`.
        - Enforce strict Base alpha clipping on `texImg`: when `hasBaseLayer[f]` is true, ensure texture pixels are clipped to the Base alpha boundary (e.g. `doc::rgba_geta(baseCol) > 16`) so textures cannot bleed beyond the Base silhouette.
        - Prevent silent resurrection of `starterSurfacePixel` when a face is intentionally erased if shared layers (`sharedLayers.base != nullptr`) exist in the document.
        - Clamp live brush `ExtraCel` overlay to `baseImg` when `hasBaseLayer[f]` is true.
     c) `buildSculptedVolume`:
        - Constrain orthographic visual hull carving so that the Base sheet silhouette forms the hard outer bounding hull. Unlinked secondary views cannot expand voxels beyond the silhouette established by the Base sheet projections.
     d) `pollAndRender`:
        - Add status feedback notification (`StatusBar::instance()->showTip(1000, "Views clamped to Base sheet silhouette")`) when Base clamping is applied.
   - Code Style & Line Length Constraint:
     - 2-space indentation, Aseprite C++ formatting.
     - CRITICAL: Every modified/added line MUST be <= 100 characters wide. Test with `git diff -U0 HEAD -- "src/*"` to ensure 0 violations.
   - Preserved Constraints:
     - Do NOT restore paired Top-Bottom dimension locking in `propagateLinkedDimensions` (PR #5 decoupled sizing remains active).
2. `docs/aseprite2/HANDOFF.md`:
   - Prepend a clean log entry under `## Log (newest first)` documenting the Phase 1 Base-first silhouette carving implementation on `feature/easymetric-base-first-daedalus`.
3. Verification:
   - Run differential hygiene check: `git diff -U0 HEAD -- "src/*"` and confirm zero lines > 100 cols.
   - Run `python tools/agent-mesh/mesh_subagent.py audit` and confirm exit code 0.
4. Deliverable:
   Write your handoff report to `e:\Work\Development\aseprite\.agents\teamwork\worker_m2\handoff.md`.
   Notify orchestrator (`f47682be-6cc6-4df9-bbe4-491c1979bf04`) via `send_message`.
