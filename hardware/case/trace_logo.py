#!/usr/bin/env python3
"""Traces the logo template into logo.svg, ready to print as a white inlay. Run it after changing the template.

    pip install numpy opencv-python-headless
    python hardware/case/trace_logo.py [preview.png]

The template's shape is in its alpha channel. Anything too fine for a 0.4 mm nozzle is
removed, and the padlocks on the remote become plain dots, because they would print as blobs.
"""
import os
import sys

import cv2
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
TEMPLATE = os.path.join(HERE, "..", "..", "logo_template_for_3d_model.png")
OUT = os.path.join(HERE, "logo.svg")

WIDTH_MM = 66.0  # Printed width. build_case.py places it, so change both together.
PX_PER_MM = 40
SMOOTH_MM = 0.08  # Evens out the wobble on the template's long edges.
MIN_INK_MM = 0.5  # Thinnest white line that prints.
MIN_GAP_MM = 0.5  # A black gap narrower than this gets wider.
MIN_GAP_AREA_MM2 = 0.08  # Smaller than this, it is an inside corner and not a gap.
GAP_GROWTH_MM = 0.3  # How much wider a narrow gap gets.
MIN_AREA_MM2 = 1.0  # A smaller island could lift off the bed, and a smaller hole would fill in.
TOLERANCE_MM = 0.02  # How far the outline may stray from the traced shape.
DOT_RADIUS_MM = 0.8
REMOTE_ICON_FROM = 0.87  # The remote icon's buttons start past this far across the logo.
MARGIN_PX = PX_PER_MM


def kernel(mm):
    size = max(3, round(mm * PX_PER_MM) | 1)
    return cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (size, size))


def load_mask():
    alpha = cv2.imread(TEMPLATE, cv2.IMREAD_UNCHANGED)[:, :, 3]
    ys, xs = np.nonzero(alpha >= 128)
    alpha = alpha[ys.min() : ys.max() + 1, xs.min() : xs.max() + 1]
    scale = WIDTH_MM * PX_PER_MM / alpha.shape[1]
    big = cv2.resize(alpha, None, fx=scale, fy=scale, interpolation=cv2.INTER_CUBIC)
    big = cv2.GaussianBlur(big, (0, 0), SMOOTH_MM * PX_PER_MM)
    mask = (big >= 128).astype(np.uint8) * 255
    # The margin stops the cleanup from clipping the logo at the image's edge.
    return cv2.copyMakeBorder(mask, MARGIN_PX, MARGIN_PX, MARGIN_PX, MARGIN_PX, cv2.BORDER_CONSTANT, value=0)


def replace_padlocks(mask):
    """The remote's buttons are holes with a padlock inside. Each padlock becomes a dot."""
    contours, hierarchy = cv2.findContours(mask, cv2.RETR_TREE, cv2.CHAIN_APPROX_SIMPLE)
    from_x = MARGIN_PX + REMOTE_ICON_FROM * (mask.shape[1] - 2 * MARGIN_PX)
    done = 0
    for i, contour in enumerate(contours):
        has_child, parent = hierarchy[0][i][2], hierarchy[0][i][3]
        is_hole = parent >= 0 and hierarchy[0][parent][3] < 0
        x = cv2.boundingRect(contour)[0]
        if is_hole and has_child >= 0 and x >= from_x:
            (cx, cy), _ = cv2.minEnclosingCircle(contour)
            cv2.drawContours(mask, [contour], -1, 0, cv2.FILLED)
            cv2.circle(mask, (round(cx), round(cy)), round(DOT_RADIUS_MM * PX_PER_MM), 255, cv2.FILLED)
            done += 1
    return done


def widen_gaps(mask):
    """A gap too narrow to print would fill in, so its edges are pushed apart, which keeps the letters separate."""
    gaps = cv2.subtract(cv2.morphologyEx(mask, cv2.MORPH_CLOSE, kernel(MIN_GAP_MM)), mask)
    count, labels, stats, _ = cv2.connectedComponentsWithStats(gaps)
    # Inside corners also show up here, as tiny specks. Those are left alone.
    real = [i for i in range(1, count) if stats[i, cv2.CC_STAT_AREA] >= MIN_GAP_AREA_MM2 * PX_PER_MM**2]
    gaps = np.isin(labels, real).astype(np.uint8) * 255
    return cv2.subtract(mask, cv2.dilate(gaps, kernel(GAP_GROWTH_MM)))


def clean(mask):
    return cv2.morphologyEx(widen_gaps(mask), cv2.MORPH_OPEN, kernel(MIN_INK_MM))


def trace(mask):
    """Returns closed outlines in mm. Holes come back as their own outlines."""
    contours, hierarchy = cv2.findContours(mask, cv2.RETR_CCOMP, cv2.CHAIN_APPROX_NONE)
    outlines, dropped, islands = [], 0, 0
    for contour, link in zip(contours, hierarchy[0]):
        if cv2.contourArea(contour) / PX_PER_MM**2 < MIN_AREA_MM2:
            dropped += 1
            continue
        islands += link[3] < 0
        points = cv2.approxPolyDP(contour, TOLERANCE_MM * PX_PER_MM, True)[:, 0, :]
        outlines.append((points.astype(float) - MARGIN_PX) / PX_PER_MM)
    return outlines, dropped, islands


def write_svg(outlines, height_mm, islands):
    paths = []
    for points in outlines:
        steps = " L ".join(f"{x:.3f} {y:.3f}" for x, y in points)
        paths.append(f"M {steps} Z")
    with open(OUT, "w", encoding="utf-8", newline="\n") as f:
        f.write(
            f'<svg xmlns="http://www.w3.org/2000/svg" width="{WIDTH_MM:g}mm" height="{height_mm:.3f}mm" '
            f'viewBox="0 0 {WIDTH_MM:g} {height_mm:.3f}" data-islands="{islands}">\n'
            f'<path fill="black" fill-rule="evenodd" d="{" ".join(paths)}"/>\n</svg>\n'
        )


def main():
    mask = load_mask()
    original = mask.copy()
    dots = replace_padlocks(mask)
    if dots != 2:
        sys.exit(f"Expected 2 padlocks on the remote, but found {dots}")
    mask = clean(mask)
    outlines, dropped, islands = trace(mask)
    height_mm = (mask.shape[0] - 2 * MARGIN_PX) / PX_PER_MM
    write_svg(outlines, height_mm, islands)

    changed = cv2.countNonZero(cv2.absdiff(original, mask)) / cv2.countNonZero(original)
    segments = sum(len(o) for o in outlines)
    print(f"{WIDTH_MM:g} x {height_mm:.1f} mm, {islands} islands, {len(outlines)} outlines, {segments} segments")
    print(f"Ink changed {100 * changed:.1f}%, padlocks replaced {dots}, small islands dropped {dropped}")

    if len(sys.argv) > 1:
        # White on black, as printed, with what the cleanup removed in red.
        preview = cv2.merge((mask, mask, cv2.max(mask, cv2.absdiff(original, mask))))
        cv2.imwrite(sys.argv[1], preview)


if __name__ == "__main__":
    main()
