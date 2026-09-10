"""Image predicates over the frame the program renders for itself.

Deliberately dependency free: it reads binary and ASCII PPM, which is the
format a self-contained C++ program can write in ten lines without pulling
in a library. PNG is read through Pillow when it happens to be installed.

The predicates are chosen so that each one fails for a different reason.
Coverage catches an empty or a full frame. Shading spread catches a flat
silhouette with no lighting. Background holes catch a torus rendered as a
disc. Mirror score catches a shape that should be symmetric and is not.
"""

import math
import struct


class ImageError(Exception):
    pass


class Image(object):
    def __init__(self, w, h, pixels):
        self.w, self.h = w, h
        self.px = pixels  # [(r, g, b)] row major, 0..255

    @staticmethod
    def load(path):
        with open(path, "rb") as fh:
            head = fh.read(2)
            fh.seek(0)
            if head in (b"P6", b"P3"):
                return Image._load_ppm(fh)
        try:
            from PIL import Image as PILImage
        except ImportError:
            raise ImageError("%s is not a PPM and Pillow is not installed" % path)
        im = PILImage.open(path).convert("RGB")
        return Image(im.width, im.height, list(im.getdata()))

    @staticmethod
    def _tokens(fh):
        while True:
            line = fh.readline()
            if not line:
                return
            if line.lstrip().startswith(b"#"):
                continue
            for t in line.split():
                yield t

    @staticmethod
    def _load_ppm(fh):
        it = Image._tokens(fh)
        magic = next(it)
        w, h, maxv = int(next(it)), int(next(it)), int(next(it))
        if w <= 0 or h <= 0:
            raise ImageError("bad PPM dimensions %dx%d" % (w, h))
        if magic == b"P3":
            vals = []
            for t in it:
                vals.append(int(t))
                if len(vals) >= w * h * 3:
                    break
        else:
            # P6: the maxval token is followed by exactly one whitespace byte
            data = fh.read()
            need = w * h * 3 * (2 if maxv > 255 else 1)
            data = data[-need:] if len(data) > need else data
            if len(data) < need:
                raise ImageError("PPM truncated: %d bytes, need %d" % (len(data), need))
            if maxv > 255:
                vals = list(struct.unpack(">%dH" % (w * h * 3), data))
            else:
                vals = list(data)
        if len(vals) < w * h * 3:
            raise ImageError("PPM has %d samples, needs %d" % (len(vals), w * h * 3))
        scale = 255.0 / maxv if maxv else 1.0
        px = [(int(vals[i] * scale), int(vals[i + 1] * scale), int(vals[i + 2] * scale))
              for i in range(0, w * h * 3, 3)]
        return Image(w, h, px)

    # ------------------------------------------------------- predicates

    def luma(self):
        return [0.2126 * r + 0.7152 * g + 0.0722 * b for (r, g, b) in self.px]

    def background_colour(self):
        """The most common pixel, which for a rendered object is the ground."""
        counts = {}
        for p in self.px:
            counts[p] = counts.get(p, 0) + 1
        return max(counts.items(), key=lambda kv: kv[1])

    def foreground_mask(self, tol=12):
        bg, _n = self.background_colour()
        return [max(abs(r - bg[0]), abs(g - bg[1]), abs(b - bg[2])) > tol
                for (r, g, b) in self.px]

    def coverage(self):
        m = self.foreground_mask()
        return sum(1 for x in m if x) / float(len(m))

    def shading_spread(self):
        """Standard deviation of luminance inside the object.

        A model that fills the silhouette with one colour has not lit
        anything, and this is what separates that from real shading.
        """
        m = self.foreground_mask()
        lum = [l for l, on in zip(self.luma(), m) if on]
        if len(lum) < 8:
            return 0.0
        mean = sum(lum) / len(lum)
        return math.sqrt(sum((x - mean) ** 2 for x in lum) / len(lum))

    def distinct_luma_levels(self, bucket=8):
        m = self.foreground_mask()
        return len({int(l // bucket) for l, on in zip(self.luma(), m) if on})

    def background_components(self):
        """Connected background regions.

        The frame border is one of them. A torus seen at an angle shows a
        second, the hole. That single integer is a topology check on the
        picture rather than on the mesh, so the two have to agree.
        """
        m = self.foreground_mask()
        w, h = self.w, self.h
        seen = [False] * (w * h)
        comps = 0
        for start in range(w * h):
            if m[start] or seen[start]:
                continue
            comps += 1
            stack = [start]
            while stack:
                i = stack.pop()
                if seen[i] or m[i]:
                    continue
                seen[i] = True
                x, y = i % w, i // w
                if x > 0:     stack.append(i - 1)
                if x < w - 1: stack.append(i + 1)
                if y > 0:     stack.append(i - w)
                if y < h - 1: stack.append(i + w)
        return comps

    def silhouette_mirror(self):
        """Left to right symmetry of the shape, not of its shading.

        Intersection over union of the foreground mask against its mirror.
        Luminance symmetry is the wrong measure for this: a Lambert light
        placed off axis breaks it while the silhouette stays perfectly
        symmetric, which is exactly what a lit render of a symmetric object
        looks like.
        """
        m = self.foreground_mask()
        inter = union = 0
        for y in range(self.h):
            row = y * self.w
            for x in range(self.w):
                a = m[row + x]
                b = m[row + self.w - 1 - x]
                if a and b:
                    inter += 1
                if a or b:
                    union += 1
        return (inter / float(union)) if union else 1.0

    def mirror_score(self):
        """1.0 is perfect left to right symmetry, 0.0 is none."""
        lum = self.luma()
        num = den = 0.0
        for y in range(self.h):
            row = y * self.w
            for x in range(self.w // 2):
                a = lum[row + x]
                b = lum[row + self.w - 1 - x]
                num += abs(a - b)
                den += max(a, b, 1.0)
        return 1.0 - (num / den if den else 0.0)


def describe(path):
    im = Image.load(path)
    bg, bgn = im.background_colour()
    return {
        "width": im.w, "height": im.h,
        "coverage": im.coverage(),
        "shading_spread": im.shading_spread(),
        "distinct_luma_levels": im.distinct_luma_levels(),
        "background_components": im.background_components(),
        "silhouette_mirror": im.silhouette_mirror(),
        "mirror_score": im.mirror_score(),
        "background_colour": bg,
        "background_fraction": bgn / float(im.w * im.h),
    }
