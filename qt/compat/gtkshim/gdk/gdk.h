/*
 * xournal-qt: minimal GDK compatibility header for compiling the Xournal++ core without GTK.
 *
 * Provides only plain data types (with GDK 3 values) and a few pure-cairo helper functions that the
 * reused core code needs. Everything else is intentionally missing: using a real GDK/GTK function in
 * code compiled into the Qt build is a compile error.
 *
 * The helper implementations are ports of GDK 3 (gdk/gdkcairo.c, LGPL-2.1-or-later) so that rendering
 * stays pixel-identical to upstream Xournal++.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <cairo.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gio/gio.h>
#include <glib-object.h>
#include <glib.h>
#include <pango/pangocairo.h>

#define XOJ_GTK_SHIM 1

G_BEGIN_DECLS

typedef struct _GdkRGBA {
    gdouble red;
    gdouble green;
    gdouble blue;
    gdouble alpha;
} GdkRGBA;

typedef cairo_rectangle_int_t GdkRectangle;

typedef enum {
    GDK_SOURCE_MOUSE,
    GDK_SOURCE_PEN,
    GDK_SOURCE_ERASER,
    GDK_SOURCE_CURSOR,
    GDK_SOURCE_KEYBOARD,
    GDK_SOURCE_TOUCHSCREEN,
    GDK_SOURCE_TOUCHPAD,
    GDK_SOURCE_TRACKPOINT,
    GDK_SOURCE_TABLET_PAD
} GdkInputSource;

typedef enum {
    GDK_SHIFT_MASK = 1 << 0,
    GDK_LOCK_MASK = 1 << 1,
    GDK_CONTROL_MASK = 1 << 2,
    GDK_MOD1_MASK = 1 << 3,
    GDK_MOD2_MASK = 1 << 4,
    GDK_MOD3_MASK = 1 << 5,
    GDK_MOD4_MASK = 1 << 6,
    GDK_MOD5_MASK = 1 << 7,
    GDK_BUTTON1_MASK = 1 << 8,
    GDK_BUTTON2_MASK = 1 << 9,
    GDK_BUTTON3_MASK = 1 << 10,
    GDK_BUTTON4_MASK = 1 << 11,
    GDK_BUTTON5_MASK = 1 << 12,
    GDK_SUPER_MASK = 1 << 26,
    GDK_HYPER_MASK = 1 << 27,
    GDK_META_MASK = 1 << 28,
    GDK_RELEASE_MASK = 1 << 30,
    GDK_MODIFIER_MASK = 0x5c001fff
} GdkModifierType;

/* Opaque handles: may be stored and compared, never dereferenced by the Qt build. */
typedef struct _GdkDevice GdkDevice;
typedef struct _GdkDisplay GdkDisplay;
typedef struct _GdkWindow GdkWindow;
typedef struct _GdkCursor GdkCursor;
typedef struct _GdkEventSequence GdkEventSequence;
typedef union _GdkEvent GdkEvent;

static inline void gdk_cairo_set_source_rgba(cairo_t* cr, const GdkRGBA* rgba) {
    cairo_set_source_rgba(cr, rgba->red, rgba->green, rgba->blue, rgba->alpha);
}

static inline void gdk_cairo_rectangle(cairo_t* cr, const GdkRectangle* rectangle) {
    cairo_rectangle(cr, rectangle->x, rectangle->y, rectangle->width, rectangle->height);
}

static inline void gdk_cairo_region(cairo_t* cr, const cairo_region_t* region) {
    cairo_rectangle_int_t box;
    const int n = cairo_region_num_rectangles(region);
    for (int i = 0; i < n; i++) {
        cairo_region_get_rectangle(region, i, &box);
        cairo_rectangle(cr, box.x, box.y, box.width, box.height);
    }
}

/* Port of gdk_cairo_surface_paint_pixbuf() from GDK 3 (identical premultiplication and rounding). */
static inline void xoj_gtkshim_surface_paint_pixbuf(cairo_surface_t* surface, const GdkPixbuf* pixbuf) {
    if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        return;
    }
    cairo_surface_flush(surface);
    const int width = gdk_pixbuf_get_width(pixbuf);
    const int height = gdk_pixbuf_get_height(pixbuf);
    const guchar* gdk_pixels = gdk_pixbuf_read_pixels(pixbuf);
    const int gdk_rowstride = gdk_pixbuf_get_rowstride(pixbuf);
    const int n_channels = gdk_pixbuf_get_n_channels(pixbuf);
    const int cairo_stride = cairo_image_surface_get_stride(surface);
    guchar* cairo_pixels = cairo_image_surface_get_data(surface);

    for (int j = height; j; j--) {
        const guchar* p = gdk_pixels;
        guchar* q = cairo_pixels;
        if (n_channels == 3) {
            const guchar* end = p + 3 * width;
            while (p < end) {
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
                q[0] = p[2];
                q[1] = p[1];
                q[2] = p[0];
                q[3] = 0xFF;
#else
                q[0] = 0xFF;
                q[1] = p[0];
                q[2] = p[1];
                q[3] = p[2];
#endif
                p += 3;
                q += 4;
            }
        } else {
            const guchar* end = p + 4 * width;
            guint t1, t2, t3;
#define XOJ_SHIM_MULT(d, c, a, t) \
    G_STMT_START {                \
        t = c * a + 0x80;         \
        d = ((t >> 8) + t) >> 8;  \
    }                             \
    G_STMT_END
            while (p < end) {
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
                XOJ_SHIM_MULT(q[0], p[2], p[3], t1);
                XOJ_SHIM_MULT(q[1], p[1], p[3], t2);
                XOJ_SHIM_MULT(q[2], p[0], p[3], t3);
                q[3] = p[3];
#else
                q[0] = p[3];
                XOJ_SHIM_MULT(q[1], p[0], p[3], t1);
                XOJ_SHIM_MULT(q[2], p[1], p[3], t2);
                XOJ_SHIM_MULT(q[3], p[2], p[3], t3);
#endif
                p += 4;
                q += 4;
            }
#undef XOJ_SHIM_MULT
        }
        gdk_pixels += gdk_rowstride;
        cairo_pixels += cairo_stride;
    }
    cairo_surface_mark_dirty(surface);
}

static inline cairo_surface_t* gdk_cairo_surface_create_from_pixbuf(const GdkPixbuf* pixbuf, int /*scale*/,
                                                                    GdkWindow* /*for_window*/) {
    const cairo_format_t format =
            gdk_pixbuf_get_n_channels(pixbuf) == 3 ? CAIRO_FORMAT_RGB24 : CAIRO_FORMAT_ARGB32;
    cairo_surface_t* surface =
            cairo_image_surface_create(format, gdk_pixbuf_get_width(pixbuf), gdk_pixbuf_get_height(pixbuf));
    xoj_gtkshim_surface_paint_pixbuf(surface, pixbuf);
    return surface;
}

static inline void gdk_cairo_set_source_pixbuf(cairo_t* cr, const GdkPixbuf* pixbuf, gdouble pixbuf_x,
                                               gdouble pixbuf_y) {
    const cairo_format_t format =
            gdk_pixbuf_get_n_channels(pixbuf) == 3 ? CAIRO_FORMAT_RGB24 : CAIRO_FORMAT_ARGB32;
    cairo_surface_t* surface = cairo_surface_create_similar_image(
            cairo_get_target(cr), format, gdk_pixbuf_get_width(pixbuf), gdk_pixbuf_get_height(pixbuf));
    xoj_gtkshim_surface_paint_pixbuf(surface, pixbuf);
    cairo_set_source_surface(cr, surface, pixbuf_x, pixbuf_y);
    cairo_surface_destroy(surface);
}

G_END_DECLS
