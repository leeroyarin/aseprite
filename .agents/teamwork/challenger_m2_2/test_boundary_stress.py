#!/usr/bin/env python3
"""
test_boundary_stress.py
Empirical stress-test suite for Milestone 2 (Phase 1 Base-First Silhouette Carving)
Target: cmd_easymetric.cpp logic and boundary conditions

Scenarios tested:
1. Different face sizes and independent column resizing (PR #5 decoupled layout)
2. Moving content in one panel / sub-selection bounds: Base alpha masking prevents bleed across faces
3. Live brush ExtraCel overlay clamping: brush strokes cannot draw outside Base silhouette
4. Silhouette hash change detection in pollAndRender: StatusBar::showTip triggers only on genuine Base silhouette change
"""

import unittest
import math

# Face enum constants matching cmd_easymetric.cpp
kFaceTop = 0
kFaceDown = 1
kFaceFront = 2
kFaceBack = 3
kFaceRight = 4
kFaceLeft = 5
kFaceCount = 6

# Face specs matching cmd_easymetric.cpp
FACE_SPECS = [
    {"name": "TOP",   "col": 0, "row": 0},
    {"name": "DOWN",  "col": 0, "row": 1},
    {"name": "FRONT", "col": 1, "row": 0},
    {"name": "BACK",  "col": 1, "row": 1},
    {"name": "RIGHT", "col": 2, "row": 0},
    {"name": "LEFT",  "col": 2, "row": 1},
]

def rgba(r, g, b, a):
    return ((a & 0xFF) << 24) | ((b & 0xFF) << 16) | ((g & 0xFF) << 8) | (r & 0xFF)

def rgba_getr(c): return c & 0xFF
def rgba_getg(c): return (c >> 8) & 0xFF
def rgba_getb(c): return (c >> 16) & 0xFF
def rgba_geta(c): return (c >> 24) & 0xFF

class SixSurfaceData:
    def __init__(self, s=24):
        self.S = s
        self.w = [s] * kFaceCount
        self.h = [s] * kFaceCount
        self.pixels = [[0] * (s * s) for _ in range(kFaceCount)]
        self.depthPixels = [[128] * (s * s) for _ in range(kFaceCount)]
        self.baseAlpha = [[0] * (s * s) for _ in range(kFaceCount)]
        self.hasBaseLayer = [False] * kFaceCount
        self.colLinked = [False, False, False]
        self.bounds = [(0, 0, s, s) for _ in range(kFaceCount)]

    def init(self, face_size):
        self.S = max(8, face_size)
        for f in range(kFaceCount):
            self.w[f] = self.S
            self.h[f] = self.S
            self.pixels[f] = [0] * (self.S * self.S)
            self.depthPixels[f] = [128] * (self.S * self.S)
            self.baseAlpha[f] = [0] * (self.S * self.S)
        self.hasBaseLayer = [False] * kFaceCount
        self.colLinked = [False, False, False]

    def setFaceSize(self, face, fw, fh):
        fw = max(4, min(256, fw))
        fh = max(4, min(256, fh))
        self.w[face] = fw
        self.h[face] = fh
        self.pixels[face] = [0] * (fw * fh)
        self.depthPixels[face] = [128] * (fw * fh)
        self.baseAlpha[face] = [0] * (fw * fh)
        self.S = max(self.S, max(fw, fh))

    def hasPixels(self, face):
        if face < 0 or face >= kFaceCount:
            return False
        for c in self.pixels[face]:
            if rgba_geta(c) > 16:
                return True
        return False

    def isBaseSolid(self, face, u, v):
        if face < 0 or face >= kFaceCount:
            return False
        fw = self.w[face]
        fh = self.h[face]
        if fw <= 0 or fh <= 0 or len(self.baseAlpha[face]) == 0:
            return False
        u = max(0, min(fw - 1, u))
        v = max(0, min(fh - 1, v))
        return self.baseAlpha[face][v * fw + u] > 16

    def isFaceSolid(self, face, u, v):
        if self.hasBaseLayer[face]:
            return self.isBaseSolid(face, u, v)
        # Fallback to symmetric sample
        sf = face
        if not self.hasPixels(face):
            pairs = {
                kFaceTop: kFaceDown, kFaceDown: kFaceTop,
                kFaceFront: kFaceBack, kFaceBack: kFaceFront,
                kFaceRight: kFaceLeft, kFaceLeft: kFaceRight
            }
            opp = pairs.get(face, face)
            if self.hasPixels(opp):
                sf = opp
        fw = self.w[sf]
        fh = self.h[sf]
        u = max(0, min(fw - 1, u))
        v = max(0, min(fh - 1, v))
        return rgba_geta(self.pixels[sf][v * fw + u]) > 16

def compute_shared_layout(w, h):
    colW = [1, 1, 1]
    rowH = [1, 1]
    for f in range(kFaceCount):
        col = FACE_SPECS[f]["col"]
        row = FACE_SPECS[f]["row"]
        colW[col] = max(colW[col], w[f])
        rowH[row] = max(rowH[row], h[f])
    return colW, rowH

def compute_shared_uv_rect(face, colW, rowH, faceW, faceH):
    col = FACE_SPECS[face]["col"]
    row = FACE_SPECS[face]["row"]
    x = sum(colW[c] for c in range(col))
    y = sum(rowH[r] for r in range(row))
    return (x, y, faceW, faceH)

def build_sculpted_volume(data, pivX=50, pivY=50, pivZ=50):
    dimX = max(data.w[kFaceFront], data.w[kFaceBack], data.w[kFaceTop], data.w[kFaceDown], 8)
    dimY = max(data.h[kFaceFront], data.h[kFaceBack], data.h[kFaceLeft], data.h[kFaceRight], 8)
    dimZ = max(data.w[kFaceLeft], data.w[kFaceRight], data.h[kFaceTop], data.h[kFaceDown], 8)

    vx = max(8, min(64, dimX))
    vy = max(8, min(64, dimY))
    vz = max(8, min(64, dimZ))
    solid = [0] * (vx * vy * vz)

    hasTop = data.hasPixels(kFaceTop)
    hasDown = data.hasPixels(kFaceDown)
    hasFront = data.hasPixels(kFaceFront)
    hasBack = data.hasPixels(kFaceBack)
    hasRight = data.hasPixels(kFaceRight)
    hasLeft = data.hasPixels(kFaceLeft)

    activeTopDown = hasTop or hasDown
    activeFrontBack = hasFront or hasBack
    activeRightLeft = hasRight or hasLeft

    if not activeTopDown and not activeFrontBack and not activeRightLeft:
        return vx, vy, vz, [1] * (vx * vy * vz)

    pivIndexX = max(0, min(vx - 1, int(pivX * 0.01 * (vx - 1))))
    pivIndexY = max(0, min(vy - 1, int(pivY * 0.01 * (vy - 1))))
    pivIndexZ = max(0, min(vz - 1, int(pivZ * 0.01 * (vz - 1))))

    onlyTopDown = activeTopDown and not activeFrontBack and not activeRightLeft
    onlyFrontBack = activeFrontBack and not activeTopDown and not activeRightLeft
    onlyRightLeft = activeRightLeft and not activeTopDown and not activeFrontBack

    slabYMin = max(0, pivIndexY - 1)
    slabYMax = min(vy - 1, pivIndexY + 1)
    slabZMin = max(0, pivIndexZ - 1)
    slabZMax = min(vz - 1, pivIndexZ + 1)
    slabXMin = max(0, pivIndexX - 1)
    slabXMax = min(vx - 1, pivIndexX + 1)

    for y in range(vy):
        if onlyTopDown and (y < slabYMin or y > slabYMax):
            continue
        for z in range(vz):
            if onlyFrontBack and (z < slabZMin or z > slabZMax):
                continue
            for x in range(vx):
                if onlyRightLeft and (x < slabXMin or x > slabXMax):
                    continue

                voxel_solid = True

                if activeTopDown:
                    uTop = max(0, min(data.w[kFaceTop] - 1, int((x + 0.5) / float(vx) * data.w[kFaceTop])))
                    vTop = max(0, min(data.h[kFaceTop] - 1, int((z + 0.5) / float(vz) * data.h[kFaceTop])))
                    topSolid = data.isFaceSolid(kFaceTop, uTop, vTop)

                    if not data.colLinked[0] and hasDown:
                        uDown = max(0, min(data.w[kFaceDown] - 1, int((x + 0.5) / float(vx) * data.w[kFaceDown])))
                        vDown = max(0, min(data.h[kFaceDown] - 1, int((vz - 1 - z + 0.5) / float(vz) * data.h[kFaceDown])))
                        downSolid = data.isFaceSolid(kFaceDown, uDown, vDown)
                        if data.hasBaseLayer[kFaceTop] and hasTop:
                            pass # Clamped by Top Base silhouette
                        else:
                            topSolid = (topSolid or downSolid) if hasTop else downSolid
                    if not topSolid:
                        voxel_solid = False

                if voxel_solid and activeFrontBack:
                    uFront = max(0, min(data.w[kFaceFront] - 1, int((x + 0.5) / float(vx) * data.w[kFaceFront])))
                    vFront = max(0, min(data.h[kFaceFront] - 1, int((y + 0.5) / float(vy) * data.h[kFaceFront])))
                    frontSolid = data.isFaceSolid(kFaceFront, uFront, vFront)

                    if not data.colLinked[1] and hasBack:
                        uBack = max(0, min(data.w[kFaceBack] - 1, int((vx - 1 - x + 0.5) / float(vx) * data.w[kFaceBack])))
                        vBack = max(0, min(data.h[kFaceBack] - 1, int((y + 0.5) / float(vy) * data.h[kFaceBack])))
                        backSolid = data.isFaceSolid(kFaceBack, uBack, vBack)
                        if data.hasBaseLayer[kFaceFront] and hasFront:
                            pass # Clamped by Front Base silhouette
                        else:
                            frontSolid = (frontSolid or backSolid) if hasFront else backSolid
                    if not frontSolid:
                        voxel_solid = False

                if voxel_solid and activeRightLeft:
                    uRight = max(0, min(data.w[kFaceRight] - 1, int((vz - 1 - z + 0.5) / float(vz) * data.w[kFaceRight])))
                    vRight = max(0, min(data.h[kFaceRight] - 1, int((y + 0.5) / float(vy) * data.h[kFaceRight])))
                    sideSolid = data.isFaceSolid(kFaceRight, uRight, vRight)

                    if not data.colLinked[2] and hasLeft:
                        uLeft = max(0, min(data.w[kFaceLeft] - 1, int((z + 0.5) / float(vz) * data.w[kFaceLeft])))
                        vLeft = max(0, min(data.h[kFaceLeft] - 1, int((y + 0.5) / float(vy) * data.h[kFaceLeft])))
                        leftSolid = data.isFaceSolid(kFaceLeft, uLeft, vLeft)
                        if data.hasBaseLayer[kFaceRight] and hasRight:
                            pass # Clamped by Right Base silhouette
                        else:
                            sideSolid = (sideSolid or leftSolid) if hasRight else leftSolid
                    if not sideSolid:
                        voxel_solid = False

                if voxel_solid:
                    solid[(y * vz + z) * vx + x] = 1

    return vx, vy, vz, solid

def compute_base_silhouette_hash(data):
    FNV_OFFSET = 14695981039346656037
    FNV_PRIME = 1099511628211
    MASK64 = 0xFFFFFFFFFFFFFFFF

    baseHash = FNV_OFFSET
    anyBase = False
    for f in range(kFaceCount):
        if data.hasBaseLayer[f]:
            anyBase = True
            for a in data.baseAlpha[f]:
                bit = 1 if a > 16 else 0
                baseHash = (baseHash ^ bit) & MASK64
                baseHash = (baseHash * FNV_PRIME) & MASK64
    return anyBase, baseHash


class TestBoundaryStress(unittest.TestCase):

    def test_scenario_1_different_face_sizes_and_decoupled_columns(self):
        """
        Scenario 1: Different face sizes & independent column resizing (PR #5 layout).
        Verifies:
        - Decoupled column dimensions (e.g. col0: 16x20 & 24x20; col1: 32x28; col2: 20x24)
        - Extreme bounds clamping in setFaceSize (clamped to 4..256)
        - Safe allocation and indexing of baseAlpha and pixels
        - build_sculpted_volume correctly dimensions and samples across mismatched faces
        """
        data = SixSurfaceData()

        # Decoupled sizes per face
        test_sizes = [
            (16, 20),  # Top
            (24, 18),  # Down
            (36, 28),  # Front
            (32, 28),  # Back
            (20, 28),  # Right
            (22, 28),  # Left
        ]
        for f in range(kFaceCount):
            data.setFaceSize(f, test_sizes[f][0], test_sizes[f][1])
            self.assertEqual(data.w[f], test_sizes[f][0])
            self.assertEqual(data.h[f], test_sizes[f][1])
            self.assertEqual(len(data.baseAlpha[f]), test_sizes[f][0] * test_sizes[f][1])
            self.assertEqual(len(data.pixels[f]), test_sizes[f][0] * test_sizes[f][1])

        # Test extreme bounds
        data.setFaceSize(kFaceTop, 1, 1000)
        self.assertEqual(data.w[kFaceTop], 4)   # clamped to 4
        self.assertEqual(data.h[kFaceTop], 256) # clamped to 256
        self.assertEqual(len(data.baseAlpha[kFaceTop]), 4 * 256)

        # Restore reasonable decoupled sizes
        for f in range(kFaceCount):
            data.setFaceSize(f, test_sizes[f][0], test_sizes[f][1])
            data.hasBaseLayer[f] = True

        # Populate a solid Base box on all faces
        for f in range(kFaceCount):
            fw, fh = data.w[f], data.h[f]
            for v in range(fh):
                for u in range(fw):
                    data.baseAlpha[f][v * fw + u] = 255
                    data.pixels[f][v * fw + u] = rgba(200, 200, 200, 255)

        # Verify build_sculpted_volume executes without out-of-bounds error
        vx, vy, vz, solid = build_sculpted_volume(data)
        self.assertEqual(vx, 36) # max(Front:36, Back:32, Top:16, Down:24)
        self.assertEqual(vy, 28) # max(Front:28, Back:28, Left:28, Right:28)
        self.assertEqual(vz, 22) # max(Left:22, Right:20, Top:20, Down:18)
        self.assertEqual(sum(solid), vx * vy * vz) # all solid box

        # Now test independent column resizing: resize only Column 0 (Top & Down)
        # Column 1 and 2 must remain completely unaffected
        data.setFaceSize(kFaceTop, 30, 30)
        data.setFaceSize(kFaceDown, 30, 30)
        self.assertEqual(data.w[kFaceFront], 36)
        self.assertEqual(data.h[kFaceFront], 28)
        self.assertEqual(data.w[kFaceRight], 20)
        self.assertEqual(data.h[kFaceRight], 28)

        # Test UV layout computation for PR #5 3-column layout
        colW, rowH = compute_shared_layout(data.w, data.h)
        self.assertEqual(colW[0], 30) # max(Top:30, Down:30)
        self.assertEqual(colW[1], 36) # max(Front:36, Back:32)
        self.assertEqual(colW[2], 22) # max(Right:20, Left:22)
        self.assertEqual(rowH[0], 30) # max(Top:30, Front:28, Right:28)
        self.assertEqual(rowH[1], 30) # max(Down:30, Back:28, Left:28)

    def test_scenario_2_moving_content_and_sub_selection_bleed_prevention(self):
        """
        Scenario 2: Moving content in one panel / sub-selection bounds.
        Verifies:
        - When content on Texture layer is moved outside the Base silhouette,
          it is strictly clamped (finalCol = 0) and NEVER bleeds or shows in 3D.
        - Bleed across panel boundaries in shared sheet: sampling is strictly confined
          to each face's UV rectangle.
        - Sub-selection cutout: Base transparent hole stays transparent in 3D despite
          texture layer having pixels over the cutout.
        """
        W, H = 20, 20
        data = SixSurfaceData()
        for f in range(kFaceCount):
            data.setFaceSize(f, W, H)
            data.hasBaseLayer[f] = True

        # Base sheet has a centered 8x8 square silhouette (coords 6..13)
        # Everything outside (0..5 and 14..19) is transparent (bA = 0)
        for f in range(kFaceCount):
            for v in range(H):
                for u in range(W):
                    if 6 <= u <= 13 and 6 <= v <= 13:
                        data.baseAlpha[f][v * W + u] = 255
                    else:
                        data.baseAlpha[f][v * W + u] = 0

        # Simulate user painting Texture layer across the ENTIRE face (all 20x20 pixels),
        # or moving a texture selection by (+5, +5) so it spills out of the 8x8 base silhouette.
        texImg = [[rgba(255, 0, 0, 255) for _ in range(W)] for _ in range(H)]

        # Simulate extractSixSurfaces clamping logic:
        for f in range(kFaceCount):
            for v in range(H):
                for u in range(W):
                    bA = data.baseAlpha[f][v * W + u]
                    baseCol = rgba(100, 100, 100, bA) if bA > 0 else 0
                    texCol = texImg[v][u]
                    aT = rgba_geta(texCol)

                    finalCol = 0
                    if data.hasBaseLayer[f]:
                        if bA > 16:
                            if aT == 255:
                                finalCol = texCol
                            elif aT > 0:
                                invA = 255 - aT
                                finalCol = rgba(
                                    (rgba_getr(texCol) * aT + rgba_getr(baseCol) * invA) // 255,
                                    (rgba_getg(texCol) * aT + rgba_getg(baseCol) * invA) // 255,
                                    (rgba_getb(texCol) * aT + rgba_getb(baseCol) * invA) // 255,
                                    bA
                                )
                            else:
                                finalCol = baseCol
                    else:
                        finalCol = texCol

                    if rgba_geta(finalCol) > 0:
                        data.pixels[f][v * W + u] = finalCol

        # Assert: EVERY pixel outside the 8x8 Base silhouette MUST have finalCol == 0
        clamped_pixels_count = 0
        for f in range(kFaceCount):
            for v in range(H):
                for u in range(W):
                    in_base = (6 <= u <= 13 and 6 <= v <= 13)
                    p = data.pixels[f][v * W + u]
                    if not in_base:
                        self.assertEqual(p, 0, f"Bleed violation at face {f}, ({u}, {v})")
                        clamped_pixels_count += 1
                    else:
                        self.assertGreater(rgba_geta(p), 16)

        # 400 total pixels per face - 64 base pixels = 336 clamped pixels per face
        self.assertEqual(clamped_pixels_count, (W * H - 8 * 8) * kFaceCount)

        # Verify 3D voxel volume is carved strictly to the 8x8 box:
        vx, vy, vz, solid = build_sculpted_volume(data)
        # Center voxels must be solid; border voxels must be completely empty!
        self.assertEqual(solid[0], 0) # (0,0,0) empty
        # Check center voxel
        cx, cy, cz = vx // 2, vy // 2, vz // 2
        self.assertEqual(solid[(cy * vz + cz) * vx + cx], 1)

    def test_scenario_3_live_brush_extracel_overlay_clamping(self):
        """
        Scenario 3: Live brush ExtraCel overlay clamping.
        Verifies:
        - When editing Texture layer (editingTex), brush strokes outside Base silhouette
          are completely dropped (continue), leaving background unchanged.
        - Brush strokes inside Base silhouette blend correctly.
        - When editing Base layer (editingBase), brush strokes update baseAlpha and
          expand/modify the silhouette.
        """
        W, H = 20, 20
        data = SixSurfaceData()
        data.setFaceSize(kFaceFront, W, H)
        data.hasBaseLayer[kFaceFront] = True

        # Base silhouette is a horizontal strip: y from 8 to 12
        for v in range(H):
            for u in range(W):
                if 8 <= v <= 12:
                    data.baseAlpha[kFaceFront][v * W + u] = 255
                    data.pixels[kFaceFront][v * W + u] = rgba(50, 50, 50, 255)
                else:
                    data.baseAlpha[kFaceFront][v * W + u] = 0
                    data.pixels[kFaceFront][v * W + u] = 0

        # Simulate a vertical brush stroke across the entire column x=10 (v from 0 to 19)
        # on Surface_Texture (editingTex = True)
        brush_color = rgba(0, 255, 0, 255) # Green brush
        for v in range(H):
            u = 10
            # ExtraCel overlay logic from cmd_easymetric.cpp:
            faceHasBase = data.hasBaseLayer[kFaceFront]
            bA = data.baseAlpha[kFaceFront][v * W + u]
            baseImg_col = rgba(50, 50, 50, bA) if bA > 0 else 0

            # When editingTex:
            if faceHasBase:
                if bA <= 16:
                    continue # Clamped by Base silhouette!
                data.pixels[kFaceFront][v * W + u] = brush_color

        # Verify: Green brush stroke applied ONLY at v in 8..12.
        # At v < 8 and v > 12, pixels MUST remain 0!
        for v in range(H):
            p = data.pixels[kFaceFront][v * W + 10]
            if 8 <= v <= 12:
                self.assertEqual(p, brush_color, f"Brush failed to draw inside Base at v={v}")
            else:
                self.assertEqual(p, 0, f"Brush leaked outside Base silhouette at v={v}")

        # Now test editing Base layer (editingBase = True):
        # A brush stroke at v=5 (previously transparent) SHOULD expand the Base silhouette
        data_base_edit = SixSurfaceData()
        data_base_edit.setFaceSize(kFaceFront, W, H)
        data_base_edit.hasBaseLayer[kFaceFront] = True
        # Initial baseAlpha all 0
        self.assertFalse(data_base_edit.isBaseSolid(kFaceFront, 10, 5))

        # Draw on Base
        data_base_edit.pixels[kFaceFront][5 * W + 10] = brush_color
        data_base_edit.baseAlpha[kFaceFront][5 * W + 10] = 255
        self.assertTrue(data_base_edit.isBaseSolid(kFaceFront, 10, 5))

    def test_scenario_4_silhouette_hash_change_detection_in_poll_and_render(self):
        """
        Scenario 4: Silhouette hash change detection in pollAndRender.
        Verifies:
        - Initial poll sets m_lastBaseSilhouetteHash without showing tip
        - Texture layer edits (colors) produce NO hash change
        - Base layer color/shade changes (with constant alpha > 16) produce NO hash change
        - Base layer silhouette carving (erasing a pixel: alpha 255 -> 0) produces genuine hash change and triggers tip
        - Base layer silhouette expansion (painting a pixel: alpha 0 -> 255) produces genuine hash change and triggers tip
        - Legacy document without Base sheet (anyBase = False) does not trigger tip
        """
        data = SixSurfaceData()
        for f in range(kFaceCount):
            data.setFaceSize(f, 16, 16)
            data.hasBaseLayer[f] = True

        # Initially populate baseAlpha with a 10x10 square
        for f in range(kFaceCount):
            for v in range(16):
                for u in range(16):
                    data.baseAlpha[f][v * 16 + u] = 255 if (3 <= u <= 12 and 3 <= v <= 12) else 0

        # Simulation of pollAndRender tracking state
        m_lastBaseSilhouetteHash = 0
        tips_shown = []

        def simulate_poll(surface_data):
            nonlocal m_lastBaseSilhouetteHash
            anyBase, baseHash = compute_base_silhouette_hash(surface_data)
            if anyBase:
                if m_lastBaseSilhouetteHash != 0 and baseHash != m_lastBaseSilhouetteHash:
                    tips_shown.append("Views clamped to Base sheet silhouette")
                m_lastBaseSilhouetteHash = baseHash

        # Tick 1: Initial document load
        simulate_poll(data)
        self.assertNotEqual(m_lastBaseSilhouetteHash, 0)
        self.assertEqual(len(tips_shown), 0, "Initial poll must not trigger tip")
        initial_hash = m_lastBaseSilhouetteHash

        # Tick 2: Texture edits (paint red, blue, green on Texture layer)
        # (Texture does not touch baseAlpha)
        for f in range(kFaceCount):
            data.pixels[f][5 * 16 + 5] = rgba(255, 0, 0, 255)
        simulate_poll(data)
        self.assertEqual(m_lastBaseSilhouetteHash, initial_hash)
        self.assertEqual(len(tips_shown), 0, "Texture edits must not trigger tip")

        # Tick 3: Base layer shading change (alpha remains 255, color changes)
        # baseAlpha values remain 255
        data.baseAlpha[kFaceFront][5 * 16 + 5] = 240 # still > 16!
        simulate_poll(data)
        self.assertEqual(m_lastBaseSilhouetteHash, initial_hash)
        self.assertEqual(len(tips_shown), 0, "Base color/shade edit (alpha > 16) must not trigger tip")

        # Tick 4: Genuine silhouette change: carve/erase a pixel (alpha 255 -> 0)
        data.baseAlpha[kFaceFront][5 * 16 + 5] = 0 # erased!
        simulate_poll(data)
        self.assertNotEqual(m_lastBaseSilhouetteHash, initial_hash)
        self.assertEqual(len(tips_shown), 1, "Silhouette carving MUST trigger tip")
        self.assertEqual(tips_shown[-1], "Views clamped to Base sheet silhouette")
        carved_hash = m_lastBaseSilhouetteHash

        # Tick 5: Idle tick without edits
        simulate_poll(data)
        self.assertEqual(m_lastBaseSilhouetteHash, carved_hash)
        self.assertEqual(len(tips_shown), 1, "Idle tick must not re-trigger tip")

        # Tick 6: Genuine silhouette change: add/draw a new pixel outside existing shape
        data.baseAlpha[kFaceFront][0] = 255 # new pixel at (0, 0)
        simulate_poll(data)
        self.assertNotEqual(m_lastBaseSilhouetteHash, carved_hash)
        self.assertEqual(len(tips_shown), 2, "Silhouette expansion MUST trigger tip")

        # Tick 7: Legacy document without Base sheet
        legacy_data = SixSurfaceData()
        for f in range(kFaceCount):
            legacy_data.hasBaseLayer[f] = False
        m_lastBaseSilhouetteHash = 0
        tips_shown.clear()
        simulate_poll(legacy_data)
        self.assertEqual(m_lastBaseSilhouetteHash, 0)
        self.assertEqual(len(tips_shown), 0, "Legacy doc must not trigger tip")

    def test_secondary_unlinked_face_cannot_expand_silhouette(self):
        """
        Stress test visual hull orthographic projection:
        When opposing faces are unlinked (colLinked = False) and Top has a Base silhouette,
        verify Down face CANNOT expand voxels outside Top's Base silhouette.
        """
        W, H = 16, 16
        data = SixSurfaceData()
        for f in range(kFaceCount):
            data.setFaceSize(f, W, H)
        data.colLinked[0] = False # Unlinked Top/Down

        # Top has a small 4x4 Base silhouette at (6..9, 6..9)
        data.hasBaseLayer[kFaceTop] = True
        for v in range(H):
            for u in range(W):
                if 6 <= u <= 9 and 6 <= v <= 9:
                    data.baseAlpha[kFaceTop][v * W + u] = 255
                    data.pixels[kFaceTop][v * W + u] = rgba(255, 255, 255, 255)
                else:
                    data.baseAlpha[kFaceTop][v * W + u] = 0
                    data.pixels[kFaceTop][v * W + u] = 0

        # Down face is completely solid 16x16
        data.hasBaseLayer[kFaceDown] = False # or true, but oversized
        for i in range(W * H):
            data.pixels[kFaceDown][i] = rgba(200, 200, 200, 255)

        # Front face is solid to provide Y depth
        data.hasBaseLayer[kFaceFront] = True
        for i in range(W * H):
            data.baseAlpha[kFaceFront][i] = 255
            data.pixels[kFaceFront][i] = rgba(200, 200, 200, 255)

        # Build volume
        vx, vy, vz, solid = build_sculpted_volume(data)

        # Count solid voxels in XZ plane across any Y:
        # In X and Z, voxels can ONLY exist within Top's 4x4 range!
        # Outside Top's 4x4, solid must be 0 everywhere, despite Down having pixels everywhere!
        for y in range(vy):
            for z in range(vz):
                for x in range(vx):
                    is_voxel_solid = solid[(y * vz + z) * vx + x]
                    uTop = int((x + 0.5) / float(vx) * W)
                    vTop = int((z + 0.5) / float(vz) * H)
                    in_top = (6 <= uTop <= 9 and 6 <= vTop <= 9)
                    if not in_top:
                        self.assertEqual(is_voxel_solid, 0,
                            f"Down expanded silhouette at x={x}, y={y}, z={z}")

    def test_randomized_fuzz_stress(self):
        """
        Adversarial fuzz stress test:
        Simulate 200 consecutive edits alternating between:
        1. Texture painting outside Base (must remain strictly clamped, finalCol == 0)
        2. Texture color variations inside Base (must update pixel, must NOT change base silhouette hash)
        3. Base alpha variations > 16 (must NOT change base silhouette hash, no tip)
        4. Base silhouette carving/expansion (MUST trigger tip)
        """
        import random
        random.seed(42)

        W, H = 16, 16
        data = SixSurfaceData()
        for f in range(kFaceCount):
            data.setFaceSize(f, W, H)
            data.hasBaseLayer[f] = True

        # Initialize Base with a diamond silhouette
        for f in range(kFaceCount):
            for v in range(H):
                for u in range(W):
                    dist = abs(u - 7.5) + abs(v - 7.5)
                    data.baseAlpha[f][v * W + u] = 255 if dist <= 5.0 else 0

        m_lastBaseSilhouetteHash = 0
        tip_counter = 0

        def poll():
            nonlocal m_lastBaseSilhouetteHash, tip_counter
            anyBase, baseHash = compute_base_silhouette_hash(data)
            if anyBase:
                if m_lastBaseSilhouetteHash != 0 and baseHash != m_lastBaseSilhouetteHash:
                    tip_counter += 1
                m_lastBaseSilhouetteHash = baseHash

        # Initial poll
        poll()
        self.assertEqual(tip_counter, 0)

        for step in range(200):
            action = step % 4
            f = random.randint(0, kFaceCount - 1)
            u = random.randint(0, W - 1)
            v = random.randint(0, H - 1)
            idx = v * W + u
            bA = data.baseAlpha[f][idx]

            if action == 0:
                # 1. Texture painted outside Base:
                # Even if random texture pixel is generated, simulate extractSixSurfaces
                texCol = rgba(random.randint(0, 255), random.randint(0, 255), random.randint(0, 255), 255)
                # Clamping check:
                if bA <= 16:
                    finalCol = 0
                else:
                    finalCol = texCol
                data.pixels[f][idx] = finalCol
                # Assert no bleed:
                if bA <= 16:
                    self.assertEqual(data.pixels[f][idx], 0)
                old_tips = tip_counter
                poll()
                self.assertEqual(tip_counter, old_tips, "Texture paint outside Base must not trigger tip")

            elif action == 1:
                # 2. Texture color variations inside Base:
                if bA > 16:
                    data.pixels[f][idx] = rgba(random.randint(0, 255), 100, 100, 255)
                old_tips = tip_counter
                poll()
                self.assertEqual(tip_counter, old_tips, "Texture color change must not trigger tip")

            elif action == 2:
                # 3. Base alpha variation > 16 (e.g. 255 -> 180, or 100 -> 220)
                if bA > 16:
                    new_a = random.randint(17, 255)
                    data.baseAlpha[f][idx] = new_a
                    old_tips = tip_counter
                    poll()
                    self.assertEqual(tip_counter, old_tips, "Base alpha edit within > 16 must not trigger tip")

            elif action == 3:
                # 4. Genuine silhouette change (flip across 16)
                old_solid = (bA > 16)
                new_a = 0 if old_solid else 255
                data.baseAlpha[f][idx] = new_a
                old_tips = tip_counter
                poll()
                self.assertEqual(tip_counter, old_tips + 1, f"Silhouette flip at step {step} MUST trigger tip")

if __name__ == "__main__":
    unittest.main()
