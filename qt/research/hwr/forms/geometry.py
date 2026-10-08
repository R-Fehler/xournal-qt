"""Plane geometry of a form, in millimetres from the page's top-left corner (y grows downwards).

An angle is the writing direction in degrees, clockwise on the page (0 left to right, 90 downwards, -90 upwards,
180 upside down): the convention of the manifest (DESIGN.md) and of ink::Word::angle in the app.
"""
import math

EPS = 1e-6


class Frame:
    """A turned frame: local (u, v) with u along the writing direction and v "down" in the frame, centred on (cx, cy)."""

    def __init__(self, cx, cy, angle=0.0):
        self.cx, self.cy, self.angle = float(cx), float(cy), float(angle)
        a = math.radians(self.angle)
        self.c, self.s = math.cos(a), math.sin(a)
        # Keep right angles exact, so upright and quarter-turned boxes have exact corners.
        for v in ("c", "s"):
            r = round(getattr(self, v))
            if abs(getattr(self, v) - r) < 1e-12:
                setattr(self, v, float(r))

    def point(self, u, v):
        """Local (u, v) to page (x, y): a clockwise turn on the page by the frame's angle."""
        return (self.cx + u * self.c - v * self.s, self.cy + u * self.s + v * self.c)

    def rect(self, u0, v0, u1, v1):
        """The polygon (4 page points, in order) of the local rectangle [u0, u1] x [v0, v1]."""
        return [self.point(u0, v0), self.point(u1, v0), self.point(u1, v1), self.point(u0, v1)]


def box_polygon(x, y, w, h, angle=0.0):
    """The corners of a box given upright (x, y, w, h) and turned by `angle` around its centre."""
    f = Frame(x + w / 2, y + h / 2, angle)
    return f.rect(-w / 2, -h / 2, w / 2, h / 2)


def aabb(poly):
    xs = [p[0] for p in poly]
    ys = [p[1] for p in poly]
    return min(xs), min(ys), max(xs), max(ys)


def _axes(poly):
    n = len(poly)
    for i in range(n):
        x0, y0 = poly[i]
        x1, y1 = poly[(i + 1) % n]
        ex, ey = x1 - x0, y1 - y0
        ln = math.hypot(ex, ey)
        if ln > EPS:
            yield (-ey / ln, ex / ln)


def _project(poly, ax):
    d = [p[0] * ax[0] + p[1] * ax[1] for p in poly]
    return min(d), max(d)


def overlap_depth(a, b):
    """How far two convex polygons overlap (the smallest separating depth, mm); 0 or less when they don't."""
    depth = float("inf")
    for ax in list(_axes(a)) + list(_axes(b)):
        a0, a1 = _project(a, ax)
        b0, b1 = _project(b, ax)
        d = min(a1, b1) - max(a0, b0)
        if d <= 0:
            return d
        depth = min(depth, d)
    return depth


def polygons_overlap(a, b, tol=0.05):
    """True when two convex polygons overlap by more than `tol` mm (touching edges are allowed)."""
    return overlap_depth(a, b) > tol


def polygon_distance_ok(a, b, gap):
    """True when the convex polygons are at least `gap` mm apart along some separating axis."""
    return overlap_depth(a, b) <= -gap + 1e-9


def inside(poly, x0, y0, x1, y1, tol=1e-6):
    return all(x0 - tol <= x <= x1 + tol and y0 - tol <= y <= y1 + tol for x, y in poly)


def contains(outer, inner, margin=0.0):
    """True when every point of `inner` lies inside the convex polygon `outer`, at least `margin` mm from its edges."""
    # Orientation of the outer polygon decides which side of an edge is inside.
    area = 0.0
    n = len(outer)
    for i in range(n):
        x0, y0 = outer[i]
        x1, y1 = outer[(i + 1) % n]
        area += x0 * y1 - x1 * y0
    sign = 1.0 if area > 0 else -1.0
    for i in range(n):
        x0, y0 = outer[i]
        x1, y1 = outer[(i + 1) % n]
        ex, ey = x1 - x0, y1 - y0
        ln = math.hypot(ex, ey)
        if ln < EPS:
            continue
        for px, py in inner:
            cross = (ex * (py - y0) - ey * (px - x0)) / ln * sign
            if cross < margin - 1e-6:
                return False
    return True
