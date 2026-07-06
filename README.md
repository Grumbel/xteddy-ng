![xteddy-ng](xteddy-ng.png)

# xpng — a modern xteddy replacement

A lightweight X11 desktop companion that displays any PNG with **real
8-bit per-channel alpha compositing**.  Transparent and semi-transparent
pixels let the desktop (or any window underneath) show through correctly.
There are no decorations, no borders, no title bar — just your image,
floating on the screen.

## Features

| Feature | Details |
|---|---|
| True RGBA | Full 8-bit alpha channel via XRender `PictOpSrc` |
| Premultiplied alpha | Correct compositing, no fringing artefacts |
| XShape input mask | Clicks pass through fully-transparent regions |
| Drag to move | Left-button drag repositions the window |
| Scroll-to-zoom | Mouse wheel scales the image live |
| Bilinear scaling | Smooth rescaling at any size |
| Sticky / always-on-top | `-sticky` flag |
| No WM decorations | Motif hint + override-redirect for `-sticky` |

## Dependencies

| Library | Package (Debian/Ubuntu) |
|---|---|
| Xlib | `libx11-dev` |
| XRender | `libxrender-dev` |
| XShape | `libxext-dev` |
| XComposite | `libxcomposite-dev` |
| libpng | `libpng-dev` |

```
sudo apt-get install libx11-dev libxrender-dev libxext-dev \
                     libxcomposite-dev libpng-dev
```

## Build

```
make
# or explicitly:
gcc -O2 -o xpng xpng.c \
    $(pkg-config --cflags --libs x11 xrender xext xcomposite libpng) -lm
```

## Usage

```
xpng [options] <image.png>
```

### Options

| Option | Description |
|---|---|
| `-pos +X+Y` | Initial window position (default: centred on screen) |
| `-scale F` | Initial scale factor, e.g. `0.5` or `2.0` (default: `1.0`) |
| `-sticky` | Stay on top of all other windows |
| `-help` | Show usage |

### Keyboard & mouse

| Input | Action |
|---|---|
| Left-drag | Move the window |
| Right-click | Quit |
| Scroll up / down | Zoom in / out (10 % per notch) |
| `+` / `-` | Zoom in / out (10 % per keypress) |
| `q` or `Escape` | Quit |

### Examples

```sh
# show an icon at native size, centred
xpng icon.png

# show a mascot at half size in the top-right corner, always on top
xpng -scale 0.5 -sticky -pos +1400+50 mascot.png

# double-size tux
xpng -scale 2.0 tux.png
```

## How it works

1. **libpng** loads the PNG and converts it to 8-bit RGBA.
2. Every pixel is **premultiplied** (`R = R*A/255`, etc.) — this is
   what XRender expects.
3. The pixels are stored as 32-bit `ARGB` words (host byte order) and
   uploaded into a depth-32 `Pixmap`.
4. An **XRender Picture** wrapping that pixmap is composited onto the
   window with `PictOpSrc`, replacing whatever was there.
5. The window itself uses a **32-bit ARGB visual** and a matching
   `Colormap`, so the X compositor (e.g. picom, xcompmgr) sees it as
   a properly-transparent surface.
6. **XShape** is used to punch out fully-transparent pixels so that
   mouse clicks fall through to the desktop or windows underneath.

## Differences from classic xteddy

| xteddy | xpng |
|---|---|
| 1-bit mask (XShape only) | Full 8-bit alpha via XRender |
| Hardcoded bear image | Any PNG from the command line |
| No scaling | Live bilinear rescaling with scroll wheel |
| C89, Motif optional | C11, no toolkit required |
