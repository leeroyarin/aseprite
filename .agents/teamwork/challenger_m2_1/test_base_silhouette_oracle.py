"""
Adversarial Simulation and Test Oracle for Easymetric Base-First Silhouette Carving
Mirrors SixSurfaceData, extractSixSurfaces, and buildSculptedVolume from src/app/commands/cmd_easymetric.cpp.
"""

import math
import sys
import unittest

kFaceTop = 0
kFaceDown = 1
kFaceFront = 2
kFaceBack = 3
kFaceRight = 4
kFaceLeft = 5
kFaceCount = 6


def rgba(r, g, b, a):
    return ((a & 0xFF) << 24) | ((b & 0xFF) << 16) | ((g & 0xFF) << 8) | (r & 0xFF)


def rgba_getr(c):
    return c & 0xFF


def rgba_getg(c):
    return (c >> 8) & 0xFF


def rgba_getb(c):
    return (c >> 16) & 0xFF


def rgba_geta(c):
    return (c >> 24) & 0xFF


def clamp(val, low, high):
    return max(low, min(high, val))


class SixSurfaceData:
    def __init__(self, face_size=24):
        self.S = max(8, face_size)
        self.w = [self.S] * kFaceCount
        self.h = [self.S] * kFaceCount
        self.pixels = [[0] * (self.S * self.S) for _ in range(kFaceCount)]
        self.depthPixels = [[128] * (self.S * self.S) for _ in range(kFaceCount)]
        self.baseAlpha = [[0] * (self.S * self.S) for _ in range(kFaceCount)]
        self.hasBaseLayer = [False] * kFaceCount
        self.colLinked = [False] * 3
        self.hasSlices = False
        self.hasCustomDepthLayer = False

    def init(self, face_size):
        self.S = max(8, face_size)
        for f in range(kFaceCount):
            self.w[f] = self.S
            self.h[f] = self.S
            self.pixels[f] = [0] * (self.S * self.S)
            self.depthPixels[f] = [128] * (self.S * self.S)
            self.baseAlpha[f] = [0] * (self.S * self.S)
        self.hasBaseLayer = [False] * kFaceCount
        self.colLinked = [False] * 3

    def set_face_size(self, face, fw, fh):
        fw = clamp(fw, 4, 256)
        fh = clamp(fh, 4, 256)
        self.w[face] = fw
        self.h[face] = fh
        self.pixels[face] = [0] * (fw * fh)
        self.depthPixels[face] = [128] * (fw * fh)
        self.baseAlpha[face] = [0] * (fw * fh)
        self.S = max(self.S, fw, fh)

    def sample(self, face, u, v):
        fw = self.w[face]
        fh = self.h[face]
        u = clamp(u, 0, fw - 1)
        v = clamp(v, 0, fh - 1)
        return self.pixels[face][v * fw + u]

    def has_pixels(self, face):
        if face < 0 or face >= kFaceCount:
            return False
        for c in self.pixels[face]:
            if rgba_geta(c) > 16:
                return True
        return False

    def symmetric_face(self, face):
        if self.has_pixels(face):
            return face
        sym_map = {
            kFaceTop: kFaceDown,
            kFaceDown: kFaceTop,
            kFaceFront: kFaceBack,
            kFaceBack: kFaceFront,
            kFaceRight: kFaceLeft,
            kFaceLeft: kFaceRight,
        }
        partner = sym_map.get(face, face)
        if self.has_pixels(partner):
            return partner
        return face

    def sample_symmetric(self, face, u, v):
        sf = self.symmetric_face(face)
        return self.sample(sf, u, v)

    def is_base_solid(self, face, u, v):
        if face < 0 or face >= kFaceCount:
            return False
        fw = self.w[face]
        fh = self.h[face]
        if fw <= 0 or fh <= 0 or len(self.baseAlpha[face]) == 0:
            return False
        u = clamp(u, 0, fw - 1)
        v = clamp(v, 0, fh - 1)
        return self.baseAlpha[face][v * fw + u] > 16

    def is_face_solid(self, face, u, v):
        if self.hasBaseLayer[face]:
            return self.is_base_solid(face, u, v)
        return rgba_geta(self.sample_symmetric(face, u, v)) > 16


class SculptedVoxelVolume:
    def __init__(self, vx=24, vy=24, vz=24):
        self.vx = vx
        self.vy = vy
        self.vz = vz
        self.solid = [0] * (vx * vy * vz)

    def is_solid(self, x, y, z):
        if x < 0 or x >= self.vx or y < 0 or y >= self.vy or z < 0 or z >= self.vz:
            return False
        return self.solid[(y * self.vz + z) * self.vx + x] != 0

    def set_solid(self, x, y, z, val):
        if 0 <= x < self.vx and 0 <= y < self.vy and 0 <= z < self.vz:
            self.solid[(y * self.vz + z) * self.vx + x] = 1 if val else 0

    def total_solid(self):
        return sum(self.solid)


def starter_surface_pixel(face, u, v, fw, fh):
    # Dummy starter color (non-zero alpha)
    return rgba(100 + face * 20, 100, 100, 255)


def simulate_extract_six_surfaces(
    doc_info,
    out: SixSurfaceData,
    use_shared=True,
    extra_cel=None,
    col_linked=None,
):
    """
    Simulates extractSixSurfaces from cmd_easymetric.cpp.
    doc_info: dict containing:
      'base_layers': dict face -> 2D array of RGBA uint32 (or single sheet)
      'tex_layers': dict face -> 2D array of RGBA uint32
      'has_base_layer': bool or dict face -> bool
      'has_tex_layer': bool or dict face -> bool
      'face_sizes': list of (w, h)
      'virgin_doc': bool
    """
    sw = doc_info.get('sprite_w', 24)
    sh = doc_info.get('sprite_h', 24)
    out.init(max(sw, sh))

    for f in range(kFaceCount):
        fw, fh = doc_info.get('face_sizes', [(24, 24)] * 6)[f]
        out.set_face_size(f, fw, fh)
        out.hasSlices = True

        face_has_base = False
        if isinstance(doc_info.get('has_base_layer'), dict):
            face_has_base = doc_info['has_base_layer'].get(f, False)
        else:
            face_has_base = bool(doc_info.get('has_base_layer', False))

        face_has_tex = False
        if isinstance(doc_info.get('has_tex_layer'), dict):
            face_has_tex = doc_info['has_tex_layer'].get(f, False)
        else:
            face_has_tex = bool(doc_info.get('has_tex_layer', False))

        out.hasBaseLayer[f] = face_has_base
        has_pixels = False

        base_img = doc_info.get('base_pixels', {}).get(f, None)
        tex_img = doc_info.get('tex_pixels', {}).get(f, None)

        for v in range(fh):
            for u in range(fw):
                base_col = base_img[v * fw + u] if (base_img and v * fw + u < len(base_img)) else 0
                tex_col = tex_img[v * fw + u] if (tex_img and v * fw + u < len(tex_img)) else 0

                bA = rgba_geta(base_col)
                out.baseAlpha[f][v * fw + u] = bA

                final_col = 0
                if face_has_base:
                    # Strict Base silhouette clamping
                    if bA > 16:
                        if face_has_tex and tex_img:
                            aT = rgba_geta(tex_col)
                            if aT == 255:
                                final_col = tex_col
                            elif aT > 0:
                                invA = 255 - aT
                                final_col = rgba(
                                    (rgba_getr(tex_col) * aT + rgba_getr(base_col) * invA) // 255,
                                    (rgba_getg(tex_col) * aT + rgba_getg(base_col) * invA) // 255,
                                    (rgba_getb(tex_col) * aT + rgba_getb(base_col) * invA) // 255,
                                    bA,
                                )
                            else:
                                final_col = base_col
                        else:
                            final_col = base_col
                    # bA <= 16: final_col stays 0!
                elif face_has_tex and tex_img:
                    final_col = tex_col

                if rgba_geta(final_col) > 0:
                    out.pixels[f][v * fw + u] = final_col
                    has_pixels = True

        # ExtraCel simulation
        if extra_cel and extra_cel.get('active', False):
            editing_base = extra_cel.get('editing_base', False)
            editing_tex = extra_cel.get('editing_tex', False)
            ec_pixels = extra_cel.get('pixels', {}).get(f, None)
            if ec_pixels:
                for v in range(fh):
                    for u in range(fw):
                        ec = ec_pixels[v * fw + u]
                        if rgba_geta(ec) > 0:
                            if editing_base:
                                out.pixels[f][v * fw + u] = ec
                                out.baseAlpha[f][v * fw + u] = rgba_geta(ec)
                                has_pixels = True
                            elif editing_tex:
                                if face_has_base:
                                    bc = base_img[v * fw + u] if base_img else 0
                                    if rgba_geta(bc) <= 16:
                                        continue  # Clamped by Base silhouette!
                                    aT = rgba_geta(ec)
                                    if aT == 255:
                                        out.pixels[f][v * fw + u] = ec
                                    elif aT > 0:
                                        invA = 255 - aT
                                        out.pixels[f][v * fw + u] = rgba(
                                            (rgba_getr(ec) * aT + rgba_getr(bc) * invA) // 255,
                                            (rgba_getg(ec) * aT + rgba_getg(bc) * invA) // 255,
                                            (rgba_getb(ec) * aT + rgba_getb(bc) * invA) // 255,
                                            rgba_geta(bc),
                                        )
                                    else:
                                        out.pixels[f][v * fw + u] = bc
                                    has_pixels = True
                                else:
                                    out.pixels[f][v * fw + u] = ec
                                    has_pixels = True

        # Virgin document starter pixel rule
        base_lyr_present = face_has_base
        tex_lyr_present = face_has_tex
        if not has_pixels and not use_shared and not base_lyr_present and not tex_lyr_present:
            for v in range(fh):
                for u in range(fw):
                    out.pixels[f][v * fw + u] = starter_surface_pixel(f, u, v, fw, fh)

    if col_linked:
        for c in range(3):
            out.colLinked[c] = col_linked[c]
            if col_linked[c]:
                top_face = c * 2
                btm_face = c * 2 + 1
                out.pixels[btm_face] = list(out.pixels[top_face])
                out.depthPixels[btm_face] = list(out.depthPixels[top_face])
                out.baseAlpha[btm_face] = list(out.baseAlpha[top_face])
                out.hasBaseLayer[btm_face] = out.hasBaseLayer[top_face]
                out.w[btm_face] = out.w[top_face]
                out.h[btm_face] = out.h[top_face]

    return True


def simulate_build_sculpted_volume(
    data: SixSurfaceData,
    only_top_down=False,
    only_front_back=False,
    only_right_left=False,
):
    """
    Simulates buildSculptedVolume from cmd_easymetric.cpp.
    """
    has_top = data.has_pixels(kFaceTop)
    has_down = data.has_pixels(kFaceDown)
    has_front = data.has_pixels(kFaceFront)
    has_back = data.has_pixels(kFaceBack)
    has_right = data.has_pixels(kFaceRight)
    has_left = data.has_pixels(kFaceLeft)

    active_top_down = has_top or has_down
    active_front_back = has_front or has_back
    active_right_left = has_right or has_left

    vx = data.w[kFaceFront]
    vy = data.h[kFaceFront]
    vz = data.w[kFaceRight]

    vol = SculptedVoxelVolume(vx, vy, vz)

    for y in range(vol.vy):
        for z in range(vol.vz):
            for x in range(vol.vx):
                solid = True

                # Top-Down view
                if active_top_down:
                    u_top = clamp(int((x + 0.5) / vol.vx * data.w[kFaceTop]), 0, data.w[kFaceTop] - 1)
                    v_top = clamp(int((z + 0.5) / vol.vz * data.h[kFaceTop]), 0, data.h[kFaceTop] - 1)
                    top_solid = data.is_face_solid(kFaceTop, u_top, v_top)

                    if not data.colLinked[0] and has_down:
                        u_down = clamp(int((x + 0.5) / vol.vx * data.w[kFaceDown]), 0, data.w[kFaceDown] - 1)
                        v_down = clamp(int(((vol.vz - 1 - z) + 0.5) / vol.vz * data.h[kFaceDown]), 0, data.h[kFaceDown] - 1)
                        down_solid = data.is_face_solid(kFaceDown, u_down, v_down)
                        if data.hasBaseLayer[kFaceTop] and has_top:
                            # Base silhouette forms outer bounding hull: unlinked Down cannot expand voxels
                            pass
                        else:
                            top_solid = (top_solid or down_solid) if has_top else down_solid

                    if not top_solid:
                        solid = False

                # Front-Back view
                if solid and active_front_back:
                    u_front = clamp(int((x + 0.5) / vol.vx * data.w[kFaceFront]), 0, data.w[kFaceFront] - 1)
                    v_front = clamp(int((y + 0.5) / vy * data.h[kFaceFront]), 0, data.h[kFaceFront] - 1)
                    front_solid = data.is_face_solid(kFaceFront, u_front, v_front)

                    if not data.colLinked[1] and has_back:
                        u_back = clamp(int(((vol.vx - 1 - x) + 0.5) / vol.vx * data.w[kFaceBack]), 0, data.w[kFaceBack] - 1)
                        v_back = clamp(int((y + 0.5) / vol.vy * data.h[kFaceBack]), 0, data.h[kFaceBack] - 1)
                        back_solid = data.is_face_solid(kFaceBack, u_back, v_back)
                        if data.hasBaseLayer[kFaceFront] and has_front:
                            # Base silhouette forms outer bounding hull: unlinked Back cannot expand voxels
                            pass
                        else:
                            front_solid = (front_solid or back_solid) if has_front else back_solid

                    if not front_solid:
                        solid = False

                # Right-Left view
                if solid and active_right_left:
                    u_right = clamp(int(((vol.vz - 1 - z) + 0.5) / vol.vz * data.w[kFaceRight]), 0, data.w[kFaceRight] - 1)
                    v_right = clamp(int((y + 0.5) / vol.vy * data.h[kFaceRight]), 0, data.h[kFaceRight] - 1)
                    side_solid = data.is_face_solid(kFaceRight, u_right, v_right)

                    if not data.colLinked[2] and has_left:
                        u_left = clamp(int((z + 0.5) / vol.vz * data.w[kFaceLeft]), 0, data.w[kFaceLeft] - 1)
                        v_left = clamp(int((y + 0.5) / vol.vy * data.h[kFaceLeft]), 0, data.h[kFaceLeft] - 1)
                        left_solid = data.is_face_solid(kFaceLeft, u_left, v_left)
                        if data.hasBaseLayer[kFaceRight] and has_right:
                            # Base silhouette forms outer bounding hull: unlinked Left cannot expand voxels
                            pass
                        else:
                            side_solid = (side_solid or left_solid) if has_right else left_solid

                    if not side_solid:
                        solid = False

                if solid:
                    vol.set_solid(x, y, z, True)

    return vol


class TestBaseFirstSilhouetteOracle(unittest.TestCase):
    def test_01_simple_box_model(self):
        """
        Verify simple box model: all 6 faces have identical solid 24x24 Base & Texture.
        Expected: All surfaces extracted with full alpha, volume is solid 24x24x24 (13,824 voxels).
        """
        w, h = 24, 24
        white_solid = rgba(255, 255, 255, 255)
        blue_solid = rgba(0, 0, 255, 255)

        base_px = {f: [white_solid] * (w * h) for f in range(6)}
        tex_px = {f: [blue_solid] * (w * h) for f in range(6)}

        doc_info = {
            'has_base_layer': True,
            'has_tex_layer': True,
            'base_pixels': base_px,
            'tex_pixels': tex_px,
            'face_sizes': [(w, h)] * 6,
        }

        data = SixSurfaceData(24)
        simulate_extract_six_surfaces(doc_info, data, use_shared=True, col_linked=[True, True, True])

        for f in range(6):
            self.assertTrue(data.hasBaseLayer[f])
            self.assertTrue(data.has_pixels(f))
            for p in data.pixels[f]:
                # Since Texture is opaque blue, finalCol should be blue_solid
                self.assertEqual(p, blue_solid)
            for a in data.baseAlpha[f]:
                self.assertEqual(a, 255)

        vol = simulate_build_sculpted_volume(data)
        self.assertEqual(vol.vx, 24)
        self.assertEqual(vol.vy, 24)
        self.assertEqual(vol.vz, 24)
        self.assertEqual(vol.total_solid(), 24 * 24 * 24)

    def test_02_altered_base_silhouette_clamping(self):
        """
        Verify: Erasing pixels on Base sheet clamps Texture pixels.
        Texture has 24x24 solid pixels, but Base has a circular cutout (only center r<=6 is solid).
        Expected: Any pixel outside circle has finalCol == 0. Texture never leaks outside Base.
        """
        w, h = 24, 24
        cx, cy = 11.5, 11.5
        radius = 6.0

        base_px = {}
        tex_px = {}

        for f in range(6):
            base_face = [0] * (w * h)
            tex_face = [rgba(255, 0, 0, 255)] * (w * h)  # Opaque red texture everywhere
            for v in range(h):
                for u in range(w):
                    dist = math.hypot(u - cx, v - cy)
                    if dist <= radius:
                        base_face[v * fw_idx(w) + u] = rgba(255, 255, 255, 255)
                    else:
                        base_face[v * fw_idx(w) + u] = 0  # Erased on Base
            base_px[f] = base_face
            tex_px[f] = tex_face

        doc_info = {
            'has_base_layer': True,
            'has_tex_layer': True,
            'base_pixels': base_px,
            'tex_pixels': tex_px,
            'face_sizes': [(w, h)] * 6,
        }

        data = SixSurfaceData(24)
        simulate_extract_six_surfaces(doc_info, data, use_shared=True, col_linked=[True, True, True])

        for f in range(6):
            for v in range(h):
                for u in range(w):
                    dist = math.hypot(u - cx, v - cy)
                    pixel = data.pixels[f][v * w + u]
                    b_alpha = data.baseAlpha[f][v * w + u]
                    if dist > radius:
                        # OUTSIDE Base silhouette: MUST BE 0!
                        self.assertEqual(b_alpha, 0, f"Face {f} ({u},{v}) baseAlpha must be 0")
                        self.assertEqual(pixel, 0, f"Face {f} ({u},{v}) texture pixel leaked! value={pixel}")
                    else:
                        # INSIDE Base silhouette: MUST BE red
                        self.assertEqual(b_alpha, 255)
                        self.assertEqual(pixel, rgba(255, 0, 0, 255))

    def test_03_unlinked_secondary_face_never_expands_volume(self):
        """
        Critical Adversarial Test:
        Opposing faces unlinked: colLinked[0] == False (Top and Down unlinked).
        Face 0 (Top) has circular Base silhouette (radius 5).
        Face 1 (Down) is unlinked and user draws a gigantic solid 24x24 square on Down.
        Expected: In buildSculptedVolume, Down's voxels CANNOT expand outside Top's Base silhouette!
        Any voxel (x, y, z) where Top projection is outside circle MUST NOT be solid.
        """
        w, h = 24, 24
        cx, cy = 11.5, 11.5
        radius = 5.0

        base_px = {}
        tex_px = {}

        # Top face: circle base
        top_base = [0] * (w * h)
        top_tex = [rgba(0, 255, 0, 255)] * (w * h)
        for v in range(h):
            for u in range(w):
                if math.hypot(u - cx, v - cy) <= radius:
                    top_base[v * w + u] = rgba(255, 255, 255, 255)
        base_px[kFaceTop] = top_base
        tex_px[kFaceTop] = top_tex

        # Down face: completely solid 24x24 square (adversarial attempt to punch through)
        base_px[kFaceDown] = [rgba(255, 255, 255, 255)] * (w * h)
        tex_px[kFaceDown] = [rgba(0, 255, 255, 255)] * (w * h)

        # Front/Back/Right/Left: full solid box
        for f in [kFaceFront, kFaceBack, kFaceRight, kFaceLeft]:
            base_px[f] = [rgba(255, 255, 255, 255)] * (w * h)
            tex_px[f] = [rgba(200, 200, 200, 255)] * (w * h)

        doc_info = {
            'has_base_layer': True,
            'has_tex_layer': True,
            'base_pixels': base_px,
            'tex_pixels': tex_px,
            'face_sizes': [(w, h)] * 6,
        }

        data = SixSurfaceData(24)
        simulate_extract_six_surfaces(doc_info, data, use_shared=True, col_linked=[False, True, True])

        vol = simulate_build_sculpted_volume(data)

        # Test every voxel in the 24x24x24 volume
        for y in range(vol.vy):
            for z in range(vol.vz):
                for x in range(vol.vx):
                    is_voxel_solid = vol.is_solid(x, y, z)
                    u_top = clamp(int((x + 0.5) / vol.vx * data.w[kFaceTop]), 0, data.w[kFaceTop] - 1)
                    v_top = clamp(int((z + 0.5) / vol.vz * data.h[kFaceTop]), 0, data.h[kFaceTop] - 1)
                    top_base_solid = data.is_base_solid(kFaceTop, u_top, v_top)

                    if not top_base_solid:
                        # Must NEVER be solid! Down face must not expand through!
                        self.assertFalse(
                            is_voxel_solid,
                            f"Voxel ({x},{y},{z}) leaked outside Top Base silhouette! "
                            f"(uTop={u_top}, vTop={v_top})",
                        )
                    else:
                        # Should be solid since front, back, right, left are solid
                        self.assertTrue(is_voxel_solid)

    def test_04_extracel_live_brush_clamping(self):
        """
        Verify: Live brush painting on Surface_Texture is clamped to Base silhouette.
        Base has circle radius 6.
        ExtraCel (brush preview/stroke) covers whole canvas with purple pixels.
        Expected: Pixels outside Base circle are rejected (continue; clamped).
        """
        w, h = 24, 24
        cx, cy = 11.5, 11.5
        radius = 6.0

        base_px = {}
        for f in range(6):
            base_face = [0] * (w * h)
            for v in range(h):
                for u in range(w):
                    if math.hypot(u - cx, v - cy) <= radius:
                        base_face[v * w + u] = rgba(255, 255, 255, 255)
            base_px[f] = base_face

        extra_cel = {
            'active': True,
            'editing_base': False,
            'editing_tex': True,
            'pixels': {f: [rgba(128, 0, 128, 255)] * (w * h) for f in range(6)},
        }

        doc_info = {
            'has_base_layer': True,
            'has_tex_layer': True,
            'base_pixels': base_px,
            'tex_pixels': {f: [0] * (w * h) for f in range(6)},
            'face_sizes': [(w, h)] * 6,
        }

        data = SixSurfaceData(24)
        simulate_extract_six_surfaces(
            doc_info, data, use_shared=True, extra_cel=extra_cel, col_linked=[True, True, True]
        )

        for f in range(6):
            for v in range(h):
                for u in range(w):
                    pixel = data.pixels[f][v * w + u]
                    dist = math.hypot(u - cx, v - cy)
                    if dist > radius:
                        self.assertEqual(pixel, 0, f"ExtraCel leaked outside Base silhouette at ({u},{v})")
                    else:
                        self.assertEqual(pixel, rgba(128, 0, 128, 255))

    def test_05_legacy_doc_fallback_no_base_sheet(self):
        """
        Verify: Legacy document without Surface_Base (only Surface_Texture or unassigned layers).
        Expected:
        - hasBaseLayer is False for all faces.
        - Texture pixels are fully preserved.
        - isFaceSolid falls back to sampleSymmetric / rgba_geta > 16.
        - buildSculptedVolume carves correctly without crashes or empty volume.
        """
        w, h = 24, 24
        blue_solid = rgba(0, 120, 255, 255)
        tex_px = {f: [blue_solid] * (w * h) for f in range(6)}

        doc_info = {
            'has_base_layer': False,  # NO BASE LAYER!
            'has_tex_layer': True,
            'base_pixels': {},
            'tex_pixels': tex_px,
            'face_sizes': [(w, h)] * 6,
        }

        data = SixSurfaceData(24)
        simulate_extract_six_surfaces(doc_info, data, use_shared=True, col_linked=[True, True, True])

        for f in range(6):
            self.assertFalse(data.hasBaseLayer[f])
            self.assertTrue(data.has_pixels(f))
            for p in data.pixels[f]:
                self.assertEqual(p, blue_solid)
            for a in data.baseAlpha[f]:
                self.assertEqual(a, 0)
            # isFaceSolid should still be solid via sampleSymmetric fallback
            self.assertTrue(data.is_face_solid(f, 0, 0))

        vol = simulate_build_sculpted_volume(data)
        self.assertEqual(vol.total_solid(), 24 * 24 * 24)

    def test_06_erased_face_no_starter_pixel_resurrection(self):
        """
        Verify: Intentionally erasing an entire face when Surface_Base exists.
        Expected:
        1. starterSurfacePixel is NOT populated! Face remains empty (all 0s).
        2. If other views are active, the erased area on Base carves away voxels,
           and secondary views cannot fill them back in.
        3. If all faces are completely empty, buildSculptedVolume returns uncarved block (lines 403-406).
        """
        w, h = 24, 24
        # Case A: All faces empty -> starterSurfacePixel not resurrected
        doc_info_empty = {
            'has_base_layer': True,
            'has_tex_layer': True,
            'base_pixels': {f: [0] * (w * h) for f in range(6)},
            'tex_pixels': {f: [0] * (w * h) for f in range(6)},
            'face_sizes': [(w, h)] * 6,
        }

        data_empty = SixSurfaceData(24)
        simulate_extract_six_surfaces(doc_info_empty, data_empty, use_shared=True, col_linked=[False, False, False])

        for f in range(6):
            self.assertFalse(data_empty.has_pixels(f))
            for p in data_empty.pixels[f]:
                self.assertEqual(p, 0)

        # In buildSculptedVolume: if no views are active, fallback assigns 1 (uncarved)
        vol_empty = simulate_build_sculpted_volume(data_empty)
        self.assertEqual(vol_empty.total_solid(), 24 * 24 * 24)

        # Case B: Front & Right active (solid), Top partially erased (left half erased, right half solid)
        # Down is unlinked and solid everywhere (attempting to keep left half solid).
        top_base = [0] * (w * h)
        for v in range(h):
            for u in range(w):
                if u >= 12:  # Right half solid
                    top_base[v * w + u] = rgba(255, 255, 255, 255)

        base_px = {
            kFaceTop: top_base,
            kFaceDown: [rgba(255, 255, 255, 255)] * (w * h),  # Unlinked Down solid everywhere
            kFaceFront: [rgba(255, 255, 255, 255)] * (w * h),
            kFaceBack: [rgba(255, 255, 255, 255)] * (w * h),
            kFaceRight: [rgba(255, 255, 255, 255)] * (w * h),
            kFaceLeft: [rgba(255, 255, 255, 255)] * (w * h),
        }
        tex_px = {f: [rgba(255, 255, 255, 255)] * (w * h) for f in range(6)}

        doc_info_carve = {
            'has_base_layer': True,
            'has_tex_layer': True,
            'base_pixels': base_px,
            'tex_pixels': tex_px,
            'face_sizes': [(w, h)] * 6,
        }

        data_carve = SixSurfaceData(24)
        simulate_extract_six_surfaces(doc_info_carve, data_carve, use_shared=True, col_linked=[False, True, True])

        vol_carve = simulate_build_sculpted_volume(data_carve)

        # Left half of the volume (x < 12) MUST be completely empty (0), because Top Base is erased!
        # Even though Down has solid pixels at x < 12, it must not expand voxels!
        for y in range(vol_carve.vy):
            for z in range(vol_carve.vz):
                for x in range(vol_carve.vx):
                    is_solid = vol_carve.is_solid(x, y, z)
                    if x < 12:
                        self.assertFalse(is_solid, f"Voxel ({x},{y},{z}) in erased left half must not be solid!")
                    else:
                        self.assertTrue(is_solid, f"Voxel ({x},{y},{z}) in solid right half must be solid!")

        self.assertEqual(vol_carve.total_solid(), 12 * 24 * 24)

    def test_07_alpha_threshold_boundary_16_vs_17(self):
        """
        Verify alpha threshold boundary:
        bA = 16 -> considered transparent / clamped to 0.
        bA = 17 -> considered solid / active.
        """
        w, h = 4, 4
        base_face = [
            rgba(255, 255, 255, 16), rgba(255, 255, 255, 17), 0, 0,
            0, 0, 0, 0,
            0, 0, 0, 0,
            0, 0, 0, 0,
        ]
        tex_face = [rgba(200, 100, 50, 255)] * 16

        doc_info = {
            'has_base_layer': True,
            'has_tex_layer': True,
            'base_pixels': {f: base_face for f in range(6)},
            'tex_pixels': {f: tex_face for f in range(6)},
            'face_sizes': [(w, h)] * 6,
        }

        data = SixSurfaceData(4)
        simulate_extract_six_surfaces(doc_info, data, use_shared=True, col_linked=[True, True, True])

        # Pixel (0, 0) has bA = 16: MUST BE CLAMPED (0)
        self.assertEqual(data.pixels[0][0], 0)
        self.assertFalse(data.is_base_solid(0, 0, 0))

        # Pixel (1, 0) has bA = 17: MUST BE ACTIVE
        self.assertNotEqual(data.pixels[0][1], 0)
        self.assertTrue(data.is_base_solid(0, 1, 0))

    def test_08_asymmetric_face_dimensions_bounding(self):
        """
        Verify non-cube dimensions (e.g., Front 30x20, Right 10x20, Top 30x10).
        Ensure projection coordinates and clamping function smoothly without index out of bounds.
        """
        top_w, top_h = 30, 10
        front_w, front_h = 30, 20
        right_w, right_h = 10, 20

        face_sizes = [
            (top_w, top_h),       # Top
            (top_w, top_h),       # Down
            (front_w, front_h),   # Front
            (front_w, front_h),   # Back
            (right_w, right_h),   # Right
            (right_w, right_h),   # Left
        ]

        base_px = {f: [rgba(255, 255, 255, 255)] * (face_sizes[f][0] * face_sizes[f][1]) for f in range(6)}
        tex_px = {f: [rgba(10, 20, 30, 255)] * (face_sizes[f][0] * face_sizes[f][1]) for f in range(6)}

        doc_info = {
            'has_base_layer': True,
            'has_tex_layer': True,
            'base_pixels': base_px,
            'tex_pixels': tex_px,
            'face_sizes': face_sizes,
        }

        data = SixSurfaceData(30)
        simulate_extract_six_surfaces(doc_info, data, use_shared=True, col_linked=[True, True, True])

        vol = simulate_build_sculpted_volume(data)
        self.assertEqual(vol.vx, 30)
        self.assertEqual(vol.vy, 20)
        self.assertEqual(vol.vz, 10)
        self.assertEqual(vol.total_solid(), 30 * 20 * 10)

    def test_09_front_back_unlinked_bounding(self):
        """
        Verify: Opposing Front/Back unlinked (colLinked[1] == False).
        Front has 8x8 hole in center. Back is unlinked and solid everywhere.
        Expected: Any voxel whose Front projection falls in the hole is NOT solid,
        Back cannot push voxels into Front's Base hole.
        """
        w, h = 24, 24
        front_base = [rgba(255, 255, 255, 255)] * (w * h)
        # 8x8 hole in center (8..15, 8..15)
        for v in range(8, 16):
            for u in range(8, 16):
                front_base[v * w + u] = 0

        base_px = {
            kFaceTop: [rgba(255, 255, 255, 255)] * (w * h),
            kFaceDown: [rgba(255, 255, 255, 255)] * (w * h),
            kFaceFront: front_base,
            kFaceBack: [rgba(255, 255, 255, 255)] * (w * h),  # Unlinked solid Back
            kFaceRight: [rgba(255, 255, 255, 255)] * (w * h),
            kFaceLeft: [rgba(255, 255, 255, 255)] * (w * h),
        }
        tex_px = {f: [rgba(255, 255, 255, 255)] * (w * h) for f in range(6)}

        doc_info = {
            'has_base_layer': True,
            'has_tex_layer': True,
            'base_pixels': base_px,
            'tex_pixels': tex_px,
            'face_sizes': [(w, h)] * 6,
        }

        data = SixSurfaceData(24)
        simulate_extract_six_surfaces(doc_info, data, use_shared=True, col_linked=[True, False, True])

        vol = simulate_build_sculpted_volume(data)

        # Tunnel in Z through the hole
        for y in range(vol.vy):
            for z in range(vol.vz):
                for x in range(vol.vx):
                    is_solid = vol.is_solid(x, y, z)
                    if 8 <= x < 16 and 8 <= y < 16:
                        self.assertFalse(is_solid, f"Voxel ({x},{y},{z}) leaked inside Front Base hole!")
                    else:
                        self.assertTrue(is_solid)

    def test_10_right_left_unlinked_bounding(self):
        """
        Verify: Opposing Right/Left unlinked (colLinked[2] == False).
        Right has top half erased (y < 12). Left is unlinked and solid everywhere.
        Expected: Any voxel whose Right projection is in top half is NOT solid.
        """
        w, h = 24, 24
        right_base = [0] * (w * h)
        for v in range(h):
            for u in range(w):
                if v >= 12:  # Bottom half solid
                    right_base[v * w + u] = rgba(255, 255, 255, 255)

        base_px = {
            kFaceTop: [rgba(255, 255, 255, 255)] * (w * h),
            kFaceDown: [rgba(255, 255, 255, 255)] * (w * h),
            kFaceFront: [rgba(255, 255, 255, 255)] * (w * h),
            kFaceBack: [rgba(255, 255, 255, 255)] * (w * h),
            kFaceRight: right_base,
            kFaceLeft: [rgba(255, 255, 255, 255)] * (w * h),  # Unlinked solid Left
        }
        tex_px = {f: [rgba(255, 255, 255, 255)] * (w * h) for f in range(6)}

        doc_info = {
            'has_base_layer': True,
            'has_tex_layer': True,
            'base_pixels': base_px,
            'tex_pixels': tex_px,
            'face_sizes': [(w, h)] * 6,
        }

        data = SixSurfaceData(24)
        simulate_extract_six_surfaces(doc_info, data, use_shared=True, col_linked=[True, True, False])

        vol = simulate_build_sculpted_volume(data)

        for y in range(vol.vy):
            for z in range(vol.vz):
                for x in range(vol.vx):
                    is_solid = vol.is_solid(x, y, z)
                    if y < 12:
                        self.assertFalse(is_solid, f"Voxel ({x},{y},{z}) leaked in Right Base top-half cutout!")
                    else:
                        self.assertTrue(is_solid)

    def test_11_alpha_preservation_and_texture_blending(self):
        """
        Verify: Final pixel alpha always matches Base alpha (bA),
        even when texture is semi-transparent (e.g. aT = 128).
        """
        w, h = 4, 4
        base_color = rgba(200, 200, 200, 200)  # bA = 200
        tex_color = rgba(100, 0, 0, 128)       # aT = 128

        doc_info = {
            'has_base_layer': True,
            'has_tex_layer': True,
            'base_pixels': {f: [base_color] * 16 for f in range(6)},
            'tex_pixels': {f: [tex_color] * 16 for f in range(6)},
            'face_sizes': [(4, 4)] * 6,
        }

        data = SixSurfaceData(4)
        simulate_extract_six_surfaces(doc_info, data, use_shared=True, col_linked=[True, True, True])

        for f in range(6):
            for p in data.pixels[f]:
                # Alpha MUST be bA (200), not aT (128) and not 255
                self.assertEqual(rgba_geta(p), 200)

    def test_12_base_silhouette_fnv1a_hash_reactivity(self):
        """
        Verify: FNV-1a hash reacts ONLY to Base silhouette changes (>16 vs <=16),
        and does NOT react to texture changes.
        """
        def compute_base_hash(data):
            fnv_offset = 14695981039346656037
            fnv_prime = 1099511628211
            h = fnv_offset
            for f in range(kFaceCount):
                if data.hasBaseLayer[f]:
                    for a in data.baseAlpha[f]:
                        h ^= 1 if a > 16 else 0
                        h = (h * fnv_prime) & 0xFFFFFFFFFFFFFFFF
            return h

        w, h = 4, 4
        doc_info1 = {
            'has_base_layer': True,
            'has_tex_layer': True,
            'base_pixels': {f: [rgba(255, 255, 255, 255)] * 16 for f in range(6)},
            'tex_pixels': {f: [rgba(10, 20, 30, 255)] * 16 for f in range(6)},
            'face_sizes': [(4, 4)] * 6,
        }
        data1 = SixSurfaceData(4)
        simulate_extract_six_surfaces(doc_info1, data1, use_shared=True, col_linked=[True, True, True])
        hash1 = compute_base_hash(data1)

        # Change texture color only
        doc_info2 = dict(doc_info1)
        doc_info2['tex_pixels'] = {f: [rgba(99, 88, 77, 255)] * 16 for f in range(6)}
        data2 = SixSurfaceData(4)
        simulate_extract_six_surfaces(doc_info2, data2, use_shared=True, col_linked=[True, True, True])
        hash2 = compute_base_hash(data2)

        # Texture change MUST NOT alter base silhouette hash!
        self.assertEqual(hash1, hash2)

        # Erase one pixel on Base (silhouette change)
        base_erased = list(doc_info1['base_pixels'][0])
        base_erased[0] = 0
        doc_info3 = dict(doc_info1)
        doc_info3['base_pixels'] = dict(doc_info1['base_pixels'])
        doc_info3['base_pixels'][0] = base_erased
        data3 = SixSurfaceData(4)
        simulate_extract_six_surfaces(doc_info3, data3, use_shared=True, col_linked=[True, True, True])
        hash3 = compute_base_hash(data3)

        # Silhouette change MUST alter base silhouette hash!
        self.assertNotEqual(hash1, hash3)


def fw_idx(w):
    return w


if __name__ == '__main__':
    unittest.main()
