/*
 * xournal-qt: the smallest program that draws text the way the core does (Pango on Cairo), into a PNG and a PDF.
 * A diagnostic for platforms where text fails (qt/scripts/windows-smoke.sh builds and runs it on Windows). Each step
 * is announced on stderr before it runs, so the last line printed says where a crash happened.
 *
 *   text-probe OUT_BASE [utf8]      writes OUT_BASE.png and OUT_BASE.pdf; "utf8": setlocale(LC_CTYPE, ".UTF-8")
 *                                   first, as the app does on Windows (see qt/src/app/WindowsSetup.cpp)
 *
 * PANGOCAIRO_BACKEND=fc or =win32 picks Pango's font backend.
 *
 * @license GNU GPLv2 or later
 */
#include <locale.h>
#include <stdio.h>
#include <string.h>

#include <cairo-pdf.h>
#include <cairo.h>
#include <pango/pangocairo.h>

static void step(const char* what) {
    fprintf(stderr, "[text-probe] %s\n", what);
    fflush(stderr);
}

static int draw(cairo_t* cr) {
    step("pango_cairo_create_layout");
    PangoLayout* layout = pango_cairo_create_layout(cr);
    PangoFontDescription* desc = pango_font_description_from_string("Sans 20");
    pango_layout_set_font_description(layout, desc);
    pango_font_description_free(desc);
    pango_layout_set_text(layout, "Xournal++ \xc3\xa4\xc3\xb6\xc3\xbc \xc3\x9f (/\xcb\x8cz\xc9\x9ano/)", -1);
    step("pango_layout_get_pixel_size (shaping, font loading)");
    int width = 0;
    int height = 0;
    pango_layout_get_pixel_size(layout, &width, &height);
    fprintf(stderr, "[text-probe] layout %d x %d px\n", width, height);
    PangoFont* font =
            pango_context_load_font(pango_layout_get_context(layout), pango_layout_get_font_description(layout));
    if (font) {
        PangoFontDescription* used = pango_font_describe(font);
        char* name = pango_font_description_to_string(used);
        fprintf(stderr, "[text-probe] font: %s (%s)\n", name, G_OBJECT_TYPE_NAME(font));
        g_free(name);
        pango_font_description_free(used);
        g_object_unref(font);
    }
    step("pango_cairo_show_layout (rasterising glyphs)");
    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_move_to(cr, 10, 10);
    pango_cairo_show_layout(cr, layout);
    g_object_unref(layout);
    return width > 0 && height > 0;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s OUT_BASE [utf8]\n", argv[0]);
        return 2;
    }
    if (argc > 2 && strcmp(argv[2], "utf8") == 0) {
        step("setlocale(LC_CTYPE, \".UTF-8\")");
        if (!setlocale(LC_CTYPE, ".UTF-8")) {
            step("no UTF-8 C locale");
        }
    }
    const char* backend = g_getenv("PANGOCAIRO_BACKEND");
    fprintf(stderr, "[text-probe] PANGOCAIRO_BACKEND=%s, Pango %s, Cairo %s\n", backend ? backend : "(unset)",
            pango_version_string(), cairo_version_string());
    step("pango_cairo_font_map_get_default");
    PangoFontMap* map = pango_cairo_font_map_get_default();
    fprintf(stderr, "[text-probe] font map: %s\n", map ? G_OBJECT_TYPE_NAME(map) : "none");

    char png[1024];
    char pdf[1024];
    snprintf(png, sizeof png, "%s.png", argv[1]);
    snprintf(pdf, sizeof pdf, "%s.pdf", argv[1]);

    step("PNG: image surface");
    cairo_surface_t* image = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 500, 80);
    cairo_t* cr = cairo_create(image);
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_paint(cr);
    int ok = draw(cr);
    cairo_destroy(cr);
    step("cairo_surface_write_to_png");
    cairo_status_t status = cairo_surface_write_to_png(image, png);
    fprintf(stderr, "[text-probe] PNG: %s\n", cairo_status_to_string(status));
    ok = ok && status == CAIRO_STATUS_SUCCESS;
    cairo_surface_destroy(image);

    step("PDF: pdf surface");
    cairo_surface_t* doc = cairo_pdf_surface_create(pdf, 500, 80);
    cr = cairo_create(doc);
    ok = draw(cr) && ok;
    cairo_destroy(cr);
    cairo_surface_finish(doc);
    status = cairo_surface_status(doc);
    fprintf(stderr, "[text-probe] PDF: %s\n", cairo_status_to_string(status));
    ok = ok && status == CAIRO_STATUS_SUCCESS;
    cairo_surface_destroy(doc);

    step(ok ? "done" : "done, with errors");
    return ok ? 0 : 1;
}
