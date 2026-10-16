#!/usr/bin/env python3
"""Builds the GUI image assets from the reference screenshot.
 - assets/bg.png     : reference with every *dynamic* element removed (knobs, readouts,
                       meter bars, auto-gain LED, selection ring/dots). Static typography,
                       logo, tape machine and the four module illustrations are kept 1:1.
 - assets/tape_*.png : the tape-machine band graded per Colour Model.
Usage: python3 tools/make_assets.py <reference.png>"""
import sys, numpy as np
from PIL import Image, ImageFilter

ref = Image.open(sys.argv[1]).convert('RGB')
a = np.asarray(ref).astype(np.float32)
rng = np.random.default_rng(7)

def hfill(x0, y0, x1, y1, margin=3):
    for y in range(y0, y1):
        l = a[y, x0 - margin:x0].mean(0); r = a[y, x1:x1 + margin].mean(0)
        t = np.linspace(0, 1, x1 - x0)[:, None]
        a[y, x0:x1] = l * (1 - t) + r * t
    a[y0:y1, x0:x1] += rng.normal(0, 1.1, (y1 - y0, x1 - x0, 1))

def vfill(x0, y0, x1, y1, margin=3):
    for x in range(x0, x1):
        t0 = a[y0 - margin:y0, x].mean(0); b = a[y1:y1 + margin, x].mean(0)
        t = np.linspace(0, 1, y1 - y0)[:, None]
        a[y0:y1, x] = t0 * (1 - t) + b * t
    a[y0:y1, x0:x1] += rng.normal(0, 1.1, (y1 - y0, x1 - x0, 1))

for cx in (301, 608, 905, 1197):
    hfill(cx - 112, 388, cx + 112, 541)      # knob + glow
    hfill(cx - 60, 602, cx + 60, 624)        # numeric readout
hfill(1362, 513, 1478, 593)                  # MIX knob
hfill(1380, 440, 1462, 482)                  # AUTO GAIN LED
hfill(70, 196, 98, 342); hfill(1406, 196, 1434, 342)   # meter bars
# TUBE selection ring / glow: copy the clean (unselected) edge strips of the TRANSISTOR card
src = a.copy()
def copy(x0, y0, x1, y1, dx):
    a[y0:y1, x0:x1] = src[y0:y1, x0 + dx:x1 + dx]
copy(226, 660, 257, 902, 304)     # left edge
copy(498, 660, 545, 902, 303)     # right edge
copy(257, 660, 498, 690, 304)     # top edge
copy(257, 872, 498, 902, 304)     # bottom edge
for (x0, x1) in ((372, 392), (676, 697), (988, 1009), (1311, 1332)):
    hfill(x0, 853, x1, 872)       # selection dots (drawn procedurally)

a = np.clip(a, 0, 255)
bg = Image.fromarray(a.astype(np.uint8)); bg.save('assets/bg.png', optimize=True)

box = (150, 100, 1385, 362)
crop = np.asarray(bg.crop(box)).astype(np.float32) / 255

def hsv_shift(img, dh, ds, dv):
    im = Image.fromarray((img * 255).astype(np.uint8)).convert('HSV')
    h = np.asarray(im).astype(np.float32)
    h[..., 0] = (h[..., 0] + dh * 255) % 255; h[..., 1] *= ds; h[..., 2] *= dv
    return np.asarray(Image.fromarray(np.clip(h, 0, 255).astype(np.uint8), 'HSV').convert('RGB')).astype(np.float32) / 255

mx, mn = crop.max(2), crop.min(2); sat = (mx - mn) / (mx + 1e-4)
lum = crop.mean(2, keepdims=True)
variants = {'tube': crop}
w = np.clip((sat - 0.45) / 0.4, 0, 1)[..., None]            # TRANSISTOR: cool steel + hot orange accents
cool = np.concatenate([lum * 0.95, lum * 1.0, lum * 1.10], 2) * 1.05 + 0.01
variants['transistor'] = np.clip(cool * (1 - w) + np.clip(crop * 1.12, 0, 1) * w, 0, 1)
blur = np.asarray(Image.fromarray((crop * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(9))).astype(np.float32) / 255
soft = np.clip(crop * 0.82 + blur * 0.38, 0, 1)             # OPTO: soft bloom, creamy amber
variants['opto'] = np.clip(soft * np.array([1.06, 1.0, 0.86]) + 0.012, 0, 1)
variants['diode'] = np.clip(hsv_shift(crop, -0.052, 1.18, 0.93) * np.array([1.06, 0.92, 0.92]), 0, 1)  # deep red-orange
for k, v in variants.items():
    Image.fromarray((np.clip(v, 0, 1) * 255).astype(np.uint8)).save(f'assets/tape_{k}.png', optimize=True)
print('ok')
