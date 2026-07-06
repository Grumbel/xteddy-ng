/*
 * xpng — a modern xteddy replacement
 *
 * Displays a PNG with a real 8-bit alpha channel on X11 using the
 * RENDER extension (XRender).  The window has no decorations and the
 * desktop shows through every transparent/semi-transparent pixel.
 *
 * Build:
 *   gcc -O2 -o xpng xpng.c \
 *       $(pkg-config --cflags --libs x11 xrender xext xcomposite libpng)
 *
 * Usage:
 *   xpng [options] <image.png>
 *
 * Options:
 *   -pos  +X+Y          initial position  (default: centre of screen)
 *   -scale FACTOR       scale factor, e.g. 0.5 or 2.0  (default: 1.0)
 *   -blend MODE         initial blend mode name (default: Over)
 *   -sticky             stay on top (sets override-redirect)
 *   -help               show this text and list all blend modes
 *
 * Interaction (once the window is open):
 *   Left-button drag    move the window
 *   Right-button        quit
 *   Scroll wheel        scale up / down  (10 % per notch)
 *   +  /  -             scale up / down by 10 %
 *   b / B               cycle blend mode forward / backward
 *   q  /  Escape        quit
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <getopt.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/extensions/Xrender.h>
#include <X11/extensions/Xcomposite.h>
#include <X11/extensions/shape.h>

#include <png.h>

/* ------------------------------------------------------------------ */
/*  Blend-mode table                                                   */
/* ------------------------------------------------------------------ */

typedef struct {
    const char *name;
    int         op;       /* PictOp* constant */
    const char *desc;
} BlendMode;

static const BlendMode BLEND_MODES[] = {
    /* Porter-Duff */
    { "Clear",              PictOpClear,              "Porter-Duff: Clear"             },
    { "Src",                PictOpSrc,                "Porter-Duff: Src"               },
    { "Dst",                PictOpDst,                "Porter-Duff: Dst (no-op)"       },
    { "Over",               PictOpOver,               "Porter-Duff: Src over Dst"      },
    { "OverReverse",        PictOpOverReverse,        "Porter-Duff: Dst over Src"      },
    { "In",                 PictOpIn,                 "Porter-Duff: Src in Dst"        },
    { "InReverse",          PictOpInReverse,          "Porter-Duff: Dst in Src"        },
    { "Out",                PictOpOut,                "Porter-Duff: Src out Dst"       },
    { "OutReverse",         PictOpOutReverse,         "Porter-Duff: Dst out Src"       },
    { "Atop",               PictOpAtop,               "Porter-Duff: Src atop Dst"      },
    { "AtopReverse",        PictOpAtopReverse,        "Porter-Duff: Dst atop Src"      },
    { "Xor",                PictOpXor,                "Porter-Duff: Src xor Dst"       },
    { "Add",                PictOpAdd,                "Porter-Duff: Add"               },
    { "Saturate",           PictOpSaturate,           "Porter-Duff: Saturate"          },
    /* Disjoint */
    { "DisjointClear",      PictOpDisjointClear,      "Disjoint: Clear"                },
    { "DisjointSrc",        PictOpDisjointSrc,        "Disjoint: Src"                  },
    { "DisjointDst",        PictOpDisjointDst,        "Disjoint: Dst"                  },
    { "DisjointOver",       PictOpDisjointOver,       "Disjoint: Over"                 },
    { "DisjointOverReverse",PictOpDisjointOverReverse,"Disjoint: OverReverse"          },
    { "DisjointIn",         PictOpDisjointIn,         "Disjoint: In"                   },
    { "DisjointInReverse",  PictOpDisjointInReverse,  "Disjoint: InReverse"            },
    { "DisjointOut",        PictOpDisjointOut,        "Disjoint: Out"                  },
    { "DisjointOutReverse", PictOpDisjointOutReverse, "Disjoint: OutReverse"           },
    { "DisjointAtop",       PictOpDisjointAtop,       "Disjoint: Atop"                 },
    { "DisjointAtopReverse",PictOpDisjointAtopReverse,"Disjoint: AtopReverse"          },
    { "DisjointXor",        PictOpDisjointXor,        "Disjoint: Xor"                  },
    /* Conjoint */
    { "ConjointClear",      PictOpConjointClear,      "Conjoint: Clear"                },
    { "ConjointSrc",        PictOpConjointSrc,        "Conjoint: Src"                  },
    { "ConjointDst",        PictOpConjointDst,        "Conjoint: Dst"                  },
    { "ConjointOver",       PictOpConjointOver,       "Conjoint: Over"                 },
    { "ConjointOverReverse",PictOpConjointOverReverse,"Conjoint: OverReverse"          },
    { "ConjointIn",         PictOpConjointIn,         "Conjoint: In"                   },
    { "ConjointInReverse",  PictOpConjointInReverse,  "Conjoint: InReverse"            },
    { "ConjointOut",        PictOpConjointOut,        "Conjoint: Out"                  },
    { "ConjointOutReverse", PictOpConjointOutReverse, "Conjoint: OutReverse"           },
    { "ConjointAtop",       PictOpConjointAtop,       "Conjoint: Atop"                 },
    { "ConjointAtopReverse",PictOpConjointAtopReverse,"Conjoint: AtopReverse"          },
    { "ConjointXor",        PictOpConjointXor,        "Conjoint: Xor"                  },
    /* SVG / CSS blend modes */
    { "Multiply",           PictOpMultiply,           "Blend: Multiply"                },
    { "Screen",             PictOpScreen,             "Blend: Screen"                  },
    { "Overlay",            PictOpOverlay,            "Blend: Overlay"                 },
    { "Darken",             PictOpDarken,             "Blend: Darken"                  },
    { "Lighten",            PictOpLighten,            "Blend: Lighten"                 },
    { "ColorDodge",         PictOpColorDodge,         "Blend: Color Dodge"             },
    { "ColorBurn",          PictOpColorBurn,          "Blend: Color Burn"              },
    { "HardLight",          PictOpHardLight,          "Blend: Hard Light"              },
    { "SoftLight",          PictOpSoftLight,          "Blend: Soft Light"              },
    { "Difference",         PictOpDifference,         "Blend: Difference"              },
    { "Exclusion",          PictOpExclusion,          "Blend: Exclusion"               },
    { "HSLHue",             PictOpHSLHue,             "Blend: HSL Hue"                 },
    { "HSLSaturation",      PictOpHSLSaturation,      "Blend: HSL Saturation"          },
    { "HSLColor",           PictOpHSLColor,           "Blend: HSL Color"               },
    { "HSLLuminosity",      PictOpHSLLuminosity,      "Blend: HSL Luminosity"          },
};
#define N_BLEND_MODES ((int)(sizeof(BLEND_MODES)/sizeof(BLEND_MODES[0])))

/* Lookup by name (case-insensitive).  Returns index or -1. */
static int blend_find(const char *name)
{
    for (int i = 0; i < N_BLEND_MODES; i++) {
        const char *a = BLEND_MODES[i].name, *b = name;
        /* strcasecmp without POSIX dependency */
        int match = 1;
        while (*a || *b) {
            char ca = *a >= 'A' && *a <= 'Z' ? (*a|32) : *a;
            char cb = *b >= 'A' && *b <= 'Z' ? (*b|32) : *b;
            if (ca != cb) { match = 0; break; }
            a++; b++;
        }
        if (match) return i;
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/*  RGBA image in host memory                                          */
/* ------------------------------------------------------------------ */

typedef struct {
    unsigned char *data;   /* packed ARGB32 (native byte order for XRender) */
    int            width;
    int            height;
} RGBAImage;

static void rgba_free(RGBAImage *img)
{
    free(img->data);
    img->data = NULL;
}

/* Load PNG → RGBAImage.  Returns 0 on success. */
static int load_png(const char *path, RGBAImage *out)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) { perror(path); return -1; }

    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING,
                                             NULL, NULL, NULL);
    if (!png) { fclose(fp); return -1; }

    png_infop info = png_create_info_struct(png);
    if (!info) { png_destroy_read_struct(&png, NULL, NULL); fclose(fp); return -1; }

    if (setjmp(png_jmpbuf(png))) {
        png_destroy_read_struct(&png, &info, NULL);
        fclose(fp);
        return -1;
    }

    png_init_io(png, fp);
    png_read_info(png, info);

    int width      = png_get_image_width(png, info);
    int height     = png_get_image_height(png, info);
    int color_type = png_get_color_type(png, info);
    int bit_depth  = png_get_bit_depth(png, info);

    /* Normalise to 8-bit RGBA */
    if (bit_depth == 16)                        png_set_strip_16(png);
    if (color_type == PNG_COLOR_TYPE_PALETTE)   png_set_palette_to_rgb(png);
    if (color_type == PNG_COLOR_TYPE_GRAY ||
        color_type == PNG_COLOR_TYPE_GRAY_ALPHA) png_set_gray_to_rgb(png);
    if (png_get_valid(png, info, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png);
    if (color_type == PNG_COLOR_TYPE_RGB ||
        color_type == PNG_COLOR_TYPE_GRAY ||
        color_type == PNG_COLOR_TYPE_PALETTE)    png_set_filler(png, 0xff,
                                                                PNG_FILLER_AFTER);
    png_read_update_info(png, info);

    /* Read rows */
    unsigned char **rows = malloc(height * sizeof(unsigned char *));
    unsigned char  *buf  = malloc(width * height * 4);
    if (!rows || !buf) { free(rows); free(buf);
                         png_destroy_read_struct(&png, &info, NULL);
                         fclose(fp); return -1; }

    for (int y = 0; y < height; y++)
        rows[y] = buf + y * width * 4;

    png_read_image(png, rows);
    free(rows);
    png_destroy_read_struct(&png, &info, NULL);
    fclose(fp);

    /*
     * Convert RGBA (libpng) → premultiplied ARGB32 (XRender native).
     * XRender ARGB32 stores A in the high byte on little-endian, i.e.
     * the 32-bit word is  0xAARRGGBB  in host byte order.
     */
    unsigned char *pix = buf;
    for (int i = 0; i < width * height; i++, pix += 4) {
        unsigned int r = pix[0], g = pix[1], b = pix[2], a = pix[3];
        /* premultiply */
        r = (r * a + 127) / 255;
        g = (g * a + 127) / 255;
        b = (b * a + 127) / 255;
        /* pack as ARGB32 */
        ((unsigned int *)buf)[i] = (a << 24) | (r << 16) | (g << 8) | b;
    }

    out->data   = buf;
    out->width  = width;
    out->height = height;
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Simple nearest-neighbour / bilinear scale into a new RGBAImage    */
/* ------------------------------------------------------------------ */

static int scale_image(const RGBAImage *src, RGBAImage *dst,
                       int dw, int dh)
{
    unsigned int *in  = (unsigned int *)src->data;
    unsigned int *out = malloc(dw * dh * 4);
    if (!out) return -1;

    int sw = src->width, sh = src->height;
    for (int dy = 0; dy < dh; dy++) {
        for (int dx = 0; dx < dw; dx++) {
            /* bilinear sampling */
            float sx = (dx + 0.5f) * sw / dw - 0.5f;
            float sy = (dy + 0.5f) * sh / dh - 0.5f;
            int   x0 = (int)floorf(sx), y0 = (int)floorf(sy);
            int   x1 = x0 + 1,         y1 = y0 + 1;
            float fx = sx - x0,        fy = sy - y0;

            /* clamp */
            if (x0 < 0)    x0 = 0;
            if (x0 >= sw)  x0 = sw - 1;
            if (x1 < 0)    x1 = 0;
            if (x1 >= sw)  x1 = sw - 1;
            if (y0 < 0)    y0 = 0;
            if (y0 >= sh)  y0 = sh - 1;
            if (y1 < 0)    y1 = 0;
            if (y1 >= sh)  y1 = sh - 1;

#define CHAN(p, sh) (((p) >> (sh)) & 0xff)
            unsigned int p00 = in[y0*sw+x0], p10 = in[y0*sw+x1];
            unsigned int p01 = in[y1*sw+x0], p11 = in[y1*sw+x1];

            unsigned int res = 0;
            int shifts[] = {0, 8, 16, 24};
            for (int c = 0; c < 4; c++) {
                int sh2 = shifts[c];
                float v = (1-fx)*(1-fy)*CHAN(p00,sh2)
                         +    fx*(1-fy)*CHAN(p10,sh2)
                         + (1-fx)*   fy*CHAN(p01,sh2)
                         +    fx*    fy*CHAN(p11,sh2);
                int vi = (int)(v + 0.5f);
                if (vi < 0)   vi = 0;
                if (vi > 255) vi = 255;
                res |= (unsigned int)vi << sh2;
            }
#undef CHAN
            out[dy * dw + dx] = res;
        }
    }

    dst->data   = (unsigned char *)out;
    dst->width  = dw;
    dst->height = dh;
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Upload RGBAImage → XRender Picture                                 */
/* ------------------------------------------------------------------ */

static Picture upload_picture(Display *dpy, Visual *argb_visual,
                               Window root,
                               const RGBAImage *img, int *pw, int *ph)
{
    int width  = img->width;
    int height = img->height;

    /*
     * XCreatePixmap depth must match the visual depth we will draw into.
     * We always use the ARGB32 visual (depth 32) found earlier.
     */
    Pixmap pix = XCreatePixmap(dpy, root, width, height, 32);

    /*
     * Build an XImage that describes our premultiplied ARGB32 buffer.
     * We must pass the correct 32-bit visual here — using DefaultVisual
     * (which is typically depth 24) causes BadMatch on XPutImage.
     *
     * The GC is created directly on the pixmap; that is always legal
     * regardless of visual, and avoids any dummy-window dance.
     */
    XImage *xi = XCreateImage(dpy,
                               argb_visual,      /* must be depth-32 visual */
                               32,               /* depth */
                               ZPixmap, 0,
                               (char *)img->data,
                               (unsigned)width, (unsigned)height,
                               32,               /* bitmap_pad */
                               width * 4);       /* bytes_per_line */
    xi->byte_order = LSBFirst;  /* our ARGB32 packing is always LE */

    GC gc = XCreateGC(dpy, pix, 0, NULL);
    XPutImage(dpy, pix, gc, xi, 0, 0, 0, 0, (unsigned)width, (unsigned)height);
    XFreeGC(dpy, gc);

    /* XDestroyImage would free xi->data, which we don't own — detach it. */
    xi->data = NULL;
    XDestroyImage(xi);

    /* Wrap pixmap in an XRender Picture */
    XRenderPictFormat *fmt = XRenderFindStandardFormat(dpy, PictStandardARGB32);
    Picture pic = XRenderCreatePicture(dpy, pix, fmt, 0, NULL);
    XFreePixmap(dpy, pix);

    if (pw) *pw = width;
    if (ph) *ph = height;
    return pic;
}

/* ------------------------------------------------------------------ */
/*  Find an ARGB32 visual for the window                               */
/* ------------------------------------------------------------------ */

static Visual *find_argb32_visual(Display *dpy, int screen, int *depth_out)
{
    XVisualInfo vt;
    vt.screen = screen;
    vt.depth  = 32;
    int n;
    XVisualInfo *vi = XGetVisualInfo(dpy,
                                     VisualScreenMask | VisualDepthMask,
                                     &vt, &n);
    if (!vi || n == 0) return NULL;

    /* Pick the first one that has an alpha mask */
    Visual *v = NULL;
    for (int i = 0; i < n; i++) {
        XRenderPictFormat *fmt =
            XRenderFindVisualFormat(dpy, vi[i].visual);
        if (fmt && fmt->type == PictTypeDirect && fmt->direct.alphaMask) {
            v = vi[i].visual;
            if (depth_out) *depth_out = vi[i].depth;
            break;
        }
    }
    XFree(vi);
    return v;
}

/* ------------------------------------------------------------------ */
/*  Grab desktop background Picture (for blend-against)               */
/* ------------------------------------------------------------------ */

/*
 * Most blend modes (Multiply, Screen, Difference, …) compute a result
 * that depends on BOTH the source (our PNG) AND the destination (what is
 * already painted on screen behind the window).  Because our window is
 * ARGB and has just been cleared, the destination is always transparent,
 * so the blend never has anything to work against.
 *
 * The fix is a two-step composite:
 *   1.  Render: blend(src, bg)  →  tmp   (an off-screen ARGB pixmap)
 *   2.  Render: tmp  Over  window        (respects the PNG's alpha mask)
 *
 * For the Porter-Duff ops (Src, Over, In, …) step 1 reduces to the
 * same thing as before (bg is transparent ⇒ same result), so correctness
 * is preserved for all modes.
 *
 * We obtain the desktop background pixmap via the _XROOTPMAP_ID atom that
 * virtually every wallpaper setter (feh, nitrogen, xsetroot, swaybg…)
 * publishes.  If that atom is absent we fall back to a solid black fill.
 */
static Picture get_background_picture(Display *dpy, Window root,
                                       int screen,
                                       int wx, int wy,   /* window origin on root */
                                       int ww, int wh,
                                       Visual *argb_vis)
{
    /* Try to find the root pixmap */
    Pixmap root_pix = None;
    const char *atom_names[] = { "_XROOTPMAP_ID", "ESETROOT_PMAP_ID" };
    for (int a = 0; a < 2 && root_pix == None; a++) {
        Atom atom = XInternAtom(dpy, atom_names[a], True);
        if (atom == None) continue;
        Atom actual_type; int actual_format;
        unsigned long nitems, bytes_after;
        unsigned char *prop = NULL;
        if (XGetWindowProperty(dpy, root, atom, 0, 1, False,
                               XA_PIXMAP, &actual_type, &actual_format,
                               &nitems, &bytes_after, &prop) == Success
            && prop) {
            root_pix = *(Pixmap *)prop;
            XFree(prop);
        }
    }

    /* Build a depth-32 ARGB temporary pixmap the size of our window */
    Pixmap tmp = XCreatePixmap(dpy, root, ww, wh, 32);
    XRenderPictFormat *fmt32 = XRenderFindStandardFormat(dpy, PictStandardARGB32);
    Picture tmp_pic = XRenderCreatePicture(dpy, tmp, fmt32, 0, NULL);
    XFreePixmap(dpy, tmp);   /* picture holds a reference */

    if (root_pix != None) {
        /* Wrap the root pixmap as a Picture at the screen's default depth */
        XRenderPictFormat *rfmt = XRenderFindVisualFormat(
                                      dpy, DefaultVisual(dpy, screen));
        XRenderPictureAttributes pa;
        pa.repeat = RepeatNormal;   /* tile if smaller than screen */
        Picture root_pic = XRenderCreatePicture(dpy, root_pix, rfmt,
                                                CPRepeat, &pa);

        /* Copy the slice of root that sits behind our window into tmp */
        XRenderComposite(dpy, PictOpSrc,
                         root_pic, None, tmp_pic,
                         wx, wy,          /* src x,y within root pixmap */
                         0, 0,
                         0, 0,            /* dst x,y */
                         ww, wh);
        XRenderFreePicture(dpy, root_pic);
    } else {
        /* No wallpaper atom — fill with opaque black */
        XRenderColor black = { 0, 0, 0, 0xffff };
        XRenderFillRectangle(dpy, PictOpSrc, tmp_pic, &black, 0, 0, ww, wh);
    }
    (void)argb_vis;
    return tmp_pic;   /* caller must XRenderFreePicture */
}

/* ------------------------------------------------------------------ */
/*  Compositing helper                                                 */
/* ------------------------------------------------------------------ */

static void composite_to_window(Display *dpy, Window win,
                                  Visual *vis,
                                  int screen,
                                  Window root,
                                  int wx, int wy,
                                  int ww, int wh,
                                  Picture src_pic,
                                  Visual *argb_vis,
                                  int blend_op)
{
    /*
     * Step 1 – blend src onto a copy of the desktop background.
     *
     * We create a fresh ARGB32 pixmap ("canvas"), copy the background
     * into it, then apply the chosen blend op on top.  This gives the
     * blend modes something meaningful to operate against.
     *
     * For Porter-Duff ops the background is still needed as the initial
     * "Dst" in ops like Over/In/Out/Atop.
     */
    Picture bg = get_background_picture(dpy, root, screen,
                                        wx, wy, ww, wh, argb_vis);

    /* Temporary ARGB canvas = background copy we blend onto */
    Pixmap canvas_pix = XCreatePixmap(dpy, root, ww, wh, 32);
    XRenderPictFormat *fmt32 = XRenderFindStandardFormat(dpy, PictStandardARGB32);
    Picture canvas = XRenderCreatePicture(dpy, canvas_pix, fmt32, 0, NULL);
    XFreePixmap(dpy, canvas_pix);

    /* Copy bg → canvas with PictOpSrc (initialise destination) */
    XRenderComposite(dpy, PictOpSrc,
                     bg, None, canvas,
                     0, 0, 0, 0, 0, 0, ww, wh);

    /* Apply chosen blend op: src blended onto canvas (which holds bg) */
    XRenderComposite(dpy, blend_op,
                     src_pic, None, canvas,
                     0, 0, 0, 0, 0, 0, ww, wh);

    /*
     * Step 2 – write result into the window.
     *
     * We want the window to be transparent where the PNG is transparent,
     * and show the blended result where it is opaque/semi-transparent.
     * Use src_pic as a mask so the PNG's own alpha gates visibility,
     * then composite the blended canvas Over the cleared window.
     */
    XRenderPictFormat *wfmt = XRenderFindVisualFormat(dpy, vis);
    Picture dst = XRenderCreatePicture(dpy, win, wfmt, 0, NULL);

    /* Clear window to transparent */
    XRenderColor clear = { 0, 0, 0, 0 };
    XRenderFillRectangle(dpy, PictOpSrc, dst, &clear, 0, 0, ww, wh);

    /* Composite canvas into window, masked by the PNG's alpha channel */
    XRenderComposite(dpy, PictOpOver,
                     canvas, src_pic, dst,
                     0, 0, 0, 0, 0, 0, ww, wh);

    XRenderFreePicture(dpy, canvas);
    XRenderFreePicture(dpy, bg);
    XRenderFreePicture(dpy, dst);
}

/* ------------------------------------------------------------------ */
/*  Application state                                                  */
/* ------------------------------------------------------------------ */

typedef struct {
    Display    *dpy;
    int         screen;
    Window      root;
    Window      win;
    Visual     *vis;
    int         win_depth;
    Colormap    cmap;

    /* original image */
    RGBAImage   orig;

    /* scaled image + its Picture */
    RGBAImage   scaled;
    Picture     pic;

    int         win_w, win_h;
    int         win_x, win_y;   /* current window position on root (for bg sample) */
    double      scale;
    int         blend_idx;   /* index into BLEND_MODES[] */

    /* dragging */
    int         dragging;
    int         drag_x, drag_y;   /* pointer pos at drag start */
    int         drag_win_x, drag_win_y;    /* window pos at drag start */

    /* atoms */
    Atom        wm_delete;
    Atom        net_wm_state;
    Atom        net_wm_state_above;
} App;

/* ------------------------------------------------------------------ */
/*  Rebuild scaled picture                                             */
/* ------------------------------------------------------------------ */

static void rebuild_scaled(App *app)
{
    if (app->pic) XRenderFreePicture(app->dpy, app->pic);
    if (app->scaled.data && app->scaled.data != app->orig.data)
        rgba_free(&app->scaled);

    int nw = (int)(app->orig.width  * app->scale + 0.5);
    int nh = (int)(app->orig.height * app->scale + 0.5);
    if (nw < 1) nw = 1;
    if (nh < 1) nh = 1;

    if (nw == app->orig.width && nh == app->orig.height) {
        app->scaled = app->orig;
    } else {
        scale_image(&app->orig, &app->scaled, nw, nh);
    }

    app->pic   = upload_picture(app->dpy, app->vis, app->root, &app->scaled,
                                &app->win_w, &app->win_h);
    app->win_w = nw;
    app->win_h = nh;

    XResizeWindow(app->dpy, app->win, nw, nh);

    /* Update the SHAPE mask so the window click-through where alpha=0 */
    XShapeSelectInput(app->dpy, app->win, ShapeNotifyMask);

    /* Build a rectangular clip for now; XRender compositing handles
       actual transparency.  For click-through we use XShape with
       per-pixel threshold. */
    unsigned int *px = (unsigned int *)app->scaled.data;
    /* Collect spans where alpha > 0 */
    XRectangle *rects = malloc(nw * nh * sizeof(XRectangle));
    int nr = 0;
    for (int y = 0; y < nh; y++) {
        int x = 0;
        while (x < nw) {
            /* skip fully transparent */
            while (x < nw && ((px[y*nw+x] >> 24) == 0)) x++;
            if (x >= nw) break;
            int start = x;
            while (x < nw && ((px[y*nw+x] >> 24) != 0)) x++;
            rects[nr].x      = start;
            rects[nr].y      = y;
            rects[nr].width  = x - start;
            rects[nr].height = 1;
            nr++;
        }
    }
    XShapeCombineRectangles(app->dpy, app->win, ShapeBounding,
                            0, 0, rects, nr, ShapeSet, YXBanded);
    free(rects);
}

/* ------------------------------------------------------------------ */
/*  Redraw                                                             */
/* ------------------------------------------------------------------ */

static void redraw(App *app)
{
    /* Refresh window position so background sampling is accurate */
    {
        Window child;
        XTranslateCoordinates(app->dpy, app->win, app->root,
                              0, 0, &app->win_x, &app->win_y, &child);
    }
    composite_to_window(app->dpy, app->win, app->vis,
                        app->screen, app->root,
                        app->win_x, app->win_y,
                        app->win_w, app->win_h,
                        app->pic,
                        app->vis,   /* argb_vis — same visual */
                        BLEND_MODES[app->blend_idx].op);
    XFlush(app->dpy);
}

/* Set blend mode by index and print a one-line status to stdout */
static void blend_set(App *app, int idx)
{
    app->blend_idx = idx;
    printf("blend: [%2d/%d] %-24s  %s\n",
           idx + 1, N_BLEND_MODES,
           BLEND_MODES[idx].name,
           BLEND_MODES[idx].desc);
    fflush(stdout);
    redraw(app);
}

/* ------------------------------------------------------------------ */
/*  Usage                                                              */
/* ------------------------------------------------------------------ */

static void usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s [options] <image.png>\n"
        "\n"
        "Options:\n"
        "  -pos  +X+Y      initial window position (default: centred)\n"
        "  -scale FACTOR   initial scale factor   (default: 1.0)\n"
        "  -blend MODE     initial blend mode     (default: Over)\n"
        "  -sticky         stay on top of other windows\n"
        "  -help           show this message and list blend modes\n"
        "\n"
        "Interaction:\n"
        "  Left-drag       move window\n"
        "  Right-click     quit\n"
        "  Scroll up/down  zoom in / out  (10 %% per notch)\n"
        "  + / -           zoom in / out  (10 %% per key)\n"
        "  b / B           cycle blend mode forward / backward\n"
        "  q / Escape      quit\n"
        "\n"
        "Blend modes:\n",
        prog);
    for (int i = 0; i < N_BLEND_MODES; i++)
        fprintf(stderr, "  %-26s %s\n",
                BLEND_MODES[i].name, BLEND_MODES[i].desc);
}

/* ------------------------------------------------------------------ */
/*  Main                                                               */
/* ------------------------------------------------------------------ */

int main(int argc, char **argv)
{
    const char *png_path  = NULL;
    double      scale     = 1.0;
    int         init_x    = -1, init_y = -1;
    int         sticky    = 0;
    int         has_pos   = 0;
    int         blend_idx = blend_find("Over");  /* default: Over */

    /* Parse arguments (simple hand-rolled parser, no getopt_long dep) */
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-help") || !strcmp(argv[i], "--help")) {
            usage(argv[0]); return 0;
        } else if (!strcmp(argv[i], "-sticky")) {
            sticky = 1;
        } else if (!strcmp(argv[i], "-scale") && i+1 < argc) {
            scale = atof(argv[++i]);
            if (scale < 0.05) scale = 0.05;
        } else if (!strcmp(argv[i], "-blend") && i+1 < argc) {
            int idx = blend_find(argv[++i]);
            if (idx < 0) {
                fprintf(stderr, "Unknown blend mode '%s'. Use -help to list modes.\n",
                        argv[i]);
                return 1;
            }
            blend_idx = idx;
        } else if (!strcmp(argv[i], "-pos") && i+1 < argc) {
            /* accept +X+Y or X,Y */
            const char *p = argv[++i];
            if (*p == '+') p++;
            int x, y;
            if (sscanf(p, "%d+%d", &x, &y) == 2 ||
                sscanf(p, "%d,%d", &x, &y) == 2) {
                init_x = x; init_y = y; has_pos = 1;
            }
        } else if (argv[i][0] != '-') {
            png_path = argv[i];
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            usage(argv[0]); return 1;
        }
    }

    if (!png_path) {
        fprintf(stderr, "Error: no PNG file specified.\n\n");
        usage(argv[0]);
        return 1;
    }

    /* Load image */
    App app;
    memset(&app, 0, sizeof(app));
    app.scale     = scale;
    app.blend_idx = blend_idx;

    if (load_png(png_path, &app.orig) != 0) {
        fprintf(stderr, "Failed to load '%s'\n", png_path);
        return 1;
    }

    /* Connect to X */
    app.dpy = XOpenDisplay(NULL);
    if (!app.dpy) {
        fprintf(stderr, "Cannot open display.\n");
        return 1;
    }
    app.screen = DefaultScreen(app.dpy);
    app.root   = RootWindow(app.dpy, app.screen);

    /* Check extensions */
    {
        int ev, er;
        if (!XRenderQueryExtension(app.dpy, &ev, &er)) {
            fprintf(stderr, "XRender extension not available.\n");
            return 1;
        }
        if (!XShapeQueryExtension(app.dpy, &ev, &er)) {
            fprintf(stderr, "XShape extension not available.\n");
            return 1;
        }
    }

    /* Find ARGB visual */
    app.vis = find_argb32_visual(app.dpy, app.screen, &app.win_depth);
    if (!app.vis) {
        fprintf(stderr, "No 32-bit ARGB visual found.\n");
        return 1;
    }

    /* Colormap for the ARGB visual */
    app.cmap = XCreateColormap(app.dpy, app.root, app.vis, AllocNone);

    /* Compute initial window size */
    int nw = (int)(app.orig.width  * scale + 0.5);
    int nh = (int)(app.orig.height * scale + 0.5);

    /* Default position: centre of screen */
    if (!has_pos) {
        init_x = (DisplayWidth(app.dpy, app.screen)  - nw) / 2;
        init_y = (DisplayHeight(app.dpy, app.screen) - nh) / 2;
    }

    /* Create window */
    XSetWindowAttributes swa;
    memset(&swa, 0, sizeof(swa));
    swa.colormap          = app.cmap;
    swa.border_pixel      = 0;           /* required when depth != parent depth */
    swa.background_pixmap = None;        /* transparent background */
    swa.override_redirect = sticky ? True : False;
    swa.event_mask = ExposureMask | ButtonPressMask | ButtonReleaseMask
                   | PointerMotionMask | KeyPressMask | StructureNotifyMask;

    app.win = XCreateWindow(app.dpy, app.root,
                            init_x, init_y, nw, nh, 0,
                            app.win_depth, InputOutput, app.vis,
                            CWColormap | CWBorderPixel | CWBackPixmap
                            | CWEventMask | CWOverrideRedirect,
                            &swa);

    /* Window manager hints */
    XSizeHints *sh = XAllocSizeHints();
    sh->flags = USPosition | USSize;
    sh->x = init_x; sh->y = init_y;
    sh->width = nw;  sh->height = nh;
    XSetWMNormalHints(app.dpy, app.win, sh);
    XFree(sh);

    /* Class hint */
    XClassHint *ch = XAllocClassHint();
    ch->res_name  = "xpng";
    ch->res_class = "Xpng";
    XSetClassHint(app.dpy, app.win, ch);
    XFree(ch);

    /* Title */
    XStoreName(app.dpy, app.win, png_path);

    /* EWMH: remove all decorations (motif hint) */
    {
        Atom mwm = XInternAtom(app.dpy, "_MOTIF_WM_HINTS", False);
        long hints[5] = { 2, 0, 0, 0, 0 }; /* MWM_HINTS_DECORATIONS = 2 */
        XChangeProperty(app.dpy, app.win, mwm, mwm, 32, PropModeReplace,
                        (unsigned char *)hints, 5);
    }

    /* EWMH: stay-on-top if requested */
    app.net_wm_state       = XInternAtom(app.dpy, "_NET_WM_STATE",       False);
    app.net_wm_state_above = XInternAtom(app.dpy, "_NET_WM_STATE_ABOVE", False);
    if (sticky) {
        XChangeProperty(app.dpy, app.win,
                        app.net_wm_state, XA_ATOM, 32,
                        PropModeReplace,
                        (unsigned char *)&app.net_wm_state_above, 1);
    }

    /* WM_DELETE_WINDOW */
    app.wm_delete = XInternAtom(app.dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(app.dpy, app.win, &app.wm_delete, 1);

    /* Build initial scaled picture */
    app.win_w = nw;
    app.win_h = nh;
    rebuild_scaled(&app);

    /* Map */
    XMapWindow(app.dpy, app.win);
    XFlush(app.dpy);

    /* ---- Event loop ---- */
    for (;;) {
        XEvent e;
        XNextEvent(app.dpy, &e);

        if (e.type == Expose && e.xexpose.count == 0) {
            redraw(&app);

        } else if (e.type == ClientMessage) {
            if ((Atom)e.xclient.data.l[0] == app.wm_delete)
                break;

        } else if (e.type == ButtonPress) {
            if (e.xbutton.button == Button1) {
                /* start drag */
                app.dragging   = 1;
                app.drag_x     = e.xbutton.x_root;
                app.drag_y     = e.xbutton.y_root;
                /* snapshot window position */
                Window child;
                XTranslateCoordinates(app.dpy, app.win, app.root,
                                      0, 0, &app.drag_win_x, &app.drag_win_y,
                                      &child);
                app.win_x = app.drag_win_x;
                app.win_y = app.drag_win_y;
                XDefineCursor(app.dpy, app.win,
                    XCreateFontCursor(app.dpy, 52 /* XC_fleur */));
            } else if (e.xbutton.button == Button3) {
                break; /* right-click = quit */
            } else if (e.xbutton.button == Button4) {
                /* scroll up → zoom in */
                app.scale *= 1.10;
                rebuild_scaled(&app);
                redraw(&app);
            } else if (e.xbutton.button == Button5) {
                /* scroll down → zoom out */
                app.scale /= 1.10;
                if (app.scale < 0.05) app.scale = 0.05;
                rebuild_scaled(&app);
                redraw(&app);
            }

        } else if (e.type == ButtonRelease) {
            if (e.xbutton.button == Button1) {
                app.dragging = 0;
                XUndefineCursor(app.dpy, app.win);
            }

        } else if (e.type == MotionNotify) {
            if (app.dragging) {
                int dx = e.xmotion.x_root - app.drag_x;
                int dy = e.xmotion.y_root - app.drag_y;
                XMoveWindow(app.dpy, app.win,
                            app.drag_win_x + dx, app.drag_win_y + dy);
                /* Redraw so background sampling tracks the new position */
                redraw(&app);
            }

        } else if (e.type == KeyPress) {
            KeySym ks = XLookupKeysym(&e.xkey, 0);
            if (ks == XK_q || ks == XK_Q || ks == XK_Escape) {
                break;
            } else if (ks == XK_plus || ks == XK_equal || ks == XK_KP_Add) {
                app.scale *= 1.10;
                rebuild_scaled(&app);
                redraw(&app);
            } else if (ks == XK_minus || ks == XK_KP_Subtract) {
                app.scale /= 1.10;
                if (app.scale < 0.05) app.scale = 0.05;
                rebuild_scaled(&app);
                redraw(&app);
            } else if (ks == XK_b) {
                /* cycle forward */
                blend_set(&app, (app.blend_idx + 1) % N_BLEND_MODES);
            } else if (ks == XK_B) {
                /* cycle backward */
                blend_set(&app, (app.blend_idx + N_BLEND_MODES - 1) % N_BLEND_MODES);
            }

        } else if (e.type == ConfigureNotify) {
            /* Window was moved/resized by WM — resample the background */
            redraw(&app);
        }
    }

    /* Cleanup */
    if (app.pic) XRenderFreePicture(app.dpy, app.pic);
    if (app.scaled.data && app.scaled.data != app.orig.data)
        rgba_free(&app.scaled);
    rgba_free(&app.orig);
    XDestroyWindow(app.dpy, app.win);
    XFreeColormap(app.dpy, app.cmap);
    XCloseDisplay(app.dpy);
    return 0;
}
