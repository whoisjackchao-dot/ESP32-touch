#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""fb.py - 240x240 RGB565 little-endian frame buffer -> PNG (stdlib only).

The device `d` command emits::

    FBBIN 240 240 115200\\n
    <115200 bytes RGB565-LE, row-major>\\n
    FBEND <crc32-ieee-hex>\\n

`pet_serial.py --dump` strips that framing and writes the raw 115200-byte
payload.  This module turns that payload into a PNG using only ``zlib`` and
``struct`` (no Pillow/numpy), and can diff two frames pixel by pixel.

CLI::

    python fb.py in.bin out.png            # frame -> PNG
    python fb.py --diff a.bin b.bin        # per-pixel difference report
    python fb.py --selftest                # offline self-test (no device)

Exit codes: 0 ok, 1 self-test failure, 2 usage / file error.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
import sys
import tempfile
import zlib
from pathlib import Path

WIDTH = 240
HEIGHT = 240
FRAME_BYTES = WIDTH * HEIGHT * 2  # 115200

# Regions used by --diff so a report says *where* the change happened.
REGIONS = (
    ("status", 0, 0, 240, 12),
    ("pet", 72, 20, 96, 128),
    ("cards", 4, 152, 232, 84),
    ("footer", 0, 236, 240, 4),
)


class FrameError(ValueError):
    """Invalid / missing frame data."""


# --------------------------------------------------------------------------
# loading & validation
# --------------------------------------------------------------------------

def check_frame_size(data, label="frame"):
    if len(data) != FRAME_BYTES:
        raise FrameError(
            "%s: %d bytes, expected %d (240x240 RGB565LE). "
            "If this is raw `d` serial output, strip the FBBIN/FBEND framing "
            "first (pet_serial.py --dump)."
            % (label, len(data), FRAME_BYTES)
        )
    return data


def load_frame(path):
    p = Path(path)
    try:
        data = p.read_bytes()
    except OSError as exc:
        raise FrameError("cannot read %s: %s" % (p, exc)) from exc
    return check_frame_size(data, str(p))


def crc32_hex(frame):
    return "%08x" % (zlib.crc32(frame) & 0xFFFFFFFF)


def frame_sha256(frame):
    return hashlib.sha256(frame).hexdigest()


# --------------------------------------------------------------------------
# RGB565 -> RGB888
# --------------------------------------------------------------------------

def frame_to_rgb888(frame):
    """RGB565LE -> RGB888 with high-bit replication (r5<<3|r5>>2, g6<<2|g6>>4)."""
    check_frame_size(frame)
    out = bytearray(WIDTH * HEIGHT * 3)
    j = 0
    for i in range(0, FRAME_BYTES, 2):
        v = frame[i] | (frame[i + 1] << 8)
        r5 = (v >> 11) & 0x1F
        g6 = (v >> 5) & 0x3F
        b5 = v & 0x1F
        out[j] = (r5 << 3) | (r5 >> 2)
        out[j + 1] = (g6 << 2) | (g6 >> 4)
        out[j + 2] = (b5 << 3) | (b5 >> 2)
        j += 3
    return bytes(out)


# --------------------------------------------------------------------------
# PNG writer (stdlib only)
# --------------------------------------------------------------------------

def _png_chunk(typ, payload):
    return (
        struct.pack(">I", len(payload))
        + typ
        + payload
        + struct.pack(">I", zlib.crc32(typ + payload) & 0xFFFFFFFF)
    )


def encode_png(rgb, width=WIDTH, height=HEIGHT, level=9):
    stride = width * 3
    if len(rgb) != stride * height:
        raise FrameError("rgb buffer %d bytes, expected %d"
                         % (len(rgb), stride * height))
    raw = bytearray()
    for y in range(height):
        raw.append(0)  # filter type 0 (None)
        raw += rgb[y * stride:(y + 1) * stride]
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    return (
        b"\x89PNG\r\n\x1a\n"
        + _png_chunk(b"IHDR", ihdr)
        + _png_chunk(b"IDAT", zlib.compress(bytes(raw), level))
        + _png_chunk(b"IEND", b"")
    )


def decode_png_rgb(data):
    """Minimal PNG decoder (8-bit RGB, all five scanline filters).

    Only used by ``--selftest`` to prove the writer is lossless.  Returns
    ``(rgb_bytes, width, height)``.
    """
    sig = b"\x89PNG\r\n\x1a\n"
    if not data.startswith(sig):
        raise FrameError("not a PNG file")
    pos = len(sig)
    idat = bytearray()
    width = height = None
    while pos + 12 <= len(data):
        (length,) = struct.unpack_from(">I", data, pos)
        typ = data[pos + 4:pos + 8]
        payload = data[pos + 8:pos + 8 + length]
        (crc,) = struct.unpack_from(">I", data, pos + 8 + length)
        if (zlib.crc32(typ + payload) & 0xFFFFFFFF) != crc:
            raise FrameError("PNG chunk %r CRC mismatch" % typ)
        if typ == b"IHDR":
            width, height, depth, ctype, comp, filt, inter = struct.unpack(
                ">IIBBBBB", payload)
            if (depth, ctype, comp, filt, inter) != (8, 2, 0, 0, 0):
                raise FrameError("unsupported PNG header: depth=%d type=%d"
                                 % (depth, ctype))
        elif typ == b"IDAT":
            idat += payload
        elif typ == b"IEND":
            break
        pos += 12 + length
    if width is None or height is None:
        raise FrameError("PNG has no IHDR")
    raw = zlib.decompress(bytes(idat))
    stride = width * 3
    out = bytearray(width * height * 3)
    prev = bytearray(stride)
    p = 0
    for y in range(height):
        if p >= len(raw):
            raise FrameError("PNG data truncated")
        ft = raw[p]
        p += 1
        line = bytearray(raw[p:p + stride])
        if len(line) != stride:
            raise FrameError("PNG scanline truncated")
        p += stride
        if ft == 0:
            pass
        elif ft == 1:
            for i in range(3, stride):
                line[i] = (line[i] + line[i - 3]) & 0xFF
        elif ft == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 0xFF
        elif ft == 3:
            for i in range(stride):
                left = line[i - 3] if i >= 3 else 0
                line[i] = (line[i] + ((left + prev[i]) >> 1)) & 0xFF
        elif ft == 4:
            for i in range(stride):
                a = line[i - 3] if i >= 3 else 0
                b = prev[i]
                c = prev[i - 3] if i >= 3 else 0
                pa = abs(b - c)
                pb = abs(a - c)
                pc = abs(a + b - 2 * c)
                if pa <= pb and pa <= pc:
                    pr = a
                elif pb <= pc:
                    pr = b
                else:
                    pr = c
                line[i] = (line[i] + pr) & 0xFF
        else:
            raise FrameError("unsupported PNG filter %d" % ft)
        out[y * stride:(y + 1) * stride] = line
        prev = line
    return bytes(out), width, height


def write_png(path, rgb, width=WIDTH, height=HEIGHT):
    p = Path(path)
    payload = encode_png(rgb, width, height)
    tmp = p.with_name(p.name + ".tmp")
    try:
        p.parent.mkdir(parents=True, exist_ok=True)
        tmp.write_bytes(payload)
        tmp.replace(p)
    except OSError as exc:
        try:
            tmp.unlink()
        except OSError:
            pass
        raise FrameError("cannot write %s: %s" % (p, exc)) from exc
    return p


def check_crc(frame, expected, label="frame"):
    actual = crc32_hex(frame)
    exp = expected.strip().lower()
    if exp.startswith("0x"):
        exp = exp[2:]
    if exp != actual:
        raise FrameError("%s: CRC32 mismatch: expected %s, actual %s"
                         % (label, exp, actual))
    return actual


def _sidecar_crc(path):
    side = path.with_name(path.name + ".crc")
    if not side.is_file():
        return None
    try:
        text = side.read_text(encoding="utf-8", errors="replace").strip()
    except OSError:
        return None
    for token in text.replace(",", " ").split():
        token = token.strip().lower()
        if token.startswith("0x"):
            token = token[2:]
        if len(token) == 8 and all(c in "0123456789abcdef" for c in token):
            return token
    return None


# --------------------------------------------------------------------------
# frame -> PNG one-shot (used by run_g1 / run_g3)
# --------------------------------------------------------------------------

def frame_to_png(src, dst, expected_crc=None):
    frame = load_frame(src)
    crc = crc32_hex(frame)
    if expected_crc:
        check_crc(frame, expected_crc, str(src))
    else:
        side = _sidecar_crc(Path(src))
        if side is not None:
            check_crc(frame, side, str(src))
    rgb = frame_to_rgb888(frame)
    out = write_png(dst, rgb)
    return {
        "src": str(src),
        "png": str(out),
        "bytes": len(frame),
        "crc32": crc,
        "sha256": frame_sha256(frame),
        "png_sha256": hashlib.sha256(Path(out).read_bytes()).hexdigest(),
        "png_bytes": out.stat().st_size,
    }


# --------------------------------------------------------------------------
# diff
# --------------------------------------------------------------------------

def _region_of(x, y):
    for name, rx, ry, rw, rh in REGIONS:
        if rx <= x < rx + rw and ry <= y < ry + rh:
            return name
    return "other"


def diff_frames(a, b):
    check_frame_size(a, "a")
    check_frame_size(b, "b")
    total = WIDTH * HEIGHT
    pixels = 0
    x0 = y0 = 10 ** 9
    x1 = y1 = -1
    regions = {name: 0 for name, *_ in REGIONS}
    regions["other"] = 0
    first = []
    for y in range(HEIGHT):
        row = y * WIDTH * 2
        for x in range(WIDTH):
            o = row + x * 2
            if a[o] == b[o] and a[o + 1] == b[o + 1]:
                continue
            pixels += 1
            if x < x0:
                x0 = x
            if x > x1:
                x1 = x
            if y < y0:
                y0 = y
            if y > y1:
                y1 = y
            regions[_region_of(x, y)] += 1
            if len(first) < 8:
                va = a[o] | (a[o + 1] << 8)
                vb = b[o] | (b[o + 1] << 8)
                first.append({"x": x, "y": y,
                              "a": "0x%04X" % va, "b": "0x%04X" % vb})
    return {
        "same": pixels == 0,
        "pixels": pixels,
        "total": total,
        "ratio": (pixels / total) if total else 0.0,
        "bbox": None if pixels == 0 else {"x0": x0, "y0": y0, "x1": x1, "y1": y1},
        "regions": regions,
        "first": first,
        "a_crc32": crc32_hex(a),
        "b_crc32": crc32_hex(b),
        "a_sha256": frame_sha256(a),
        "b_sha256": frame_sha256(b),
    }


def format_diff(d):
    if d["same"]:
        return ("IDENTICAL: 0/%d pixels differ (crc32 %s)"
                % (d["total"], d["a_crc32"]))
    bb = d["bbox"]
    parts = [
        "DIFF: %d/%d pixels differ (%.2f%%)" % (
            d["pixels"], d["total"], d["ratio"] * 100.0),
        "bbox: x=%d..%d, y=%d..%d" % (bb["x0"], bb["x1"], bb["y0"], bb["y1"]),
        "regions: " + ", ".join("%s=%d" % kv for kv in d["regions"].items()),
        "first: " + ", ".join(
            "(%d,%d) %s->%s" % (f["x"], f["y"], f["a"], f["b"])
            for f in d["first"]),
    ]
    return "\n".join(parts)


# --------------------------------------------------------------------------
# self-test (no device needed)
# --------------------------------------------------------------------------

def _make_test_frame():
    """Deterministic 240x240 pattern with known RGB565 values."""
    frame = bytearray(FRAME_BYTES)
    for y in range(HEIGHT):
        row = y * WIDTH * 2
        for x in range(WIDTH):
            v = ((x & 0x1F) << 11) | ((y & 0x3F) << 5) | ((x + y) & 0x1F)
            frame[row + x * 2] = v & 0xFF
            frame[row + x * 2 + 1] = (v >> 8) & 0xFF
    return bytes(frame)


def _selftest():
    failures = []
    checks = [0]

    def check(name, ok, detail):
        checks[0] += 1
        print("%s %s: %s" % ("PASS" if ok else "FAIL", name, detail))
        if not ok:
            failures.append(name)

    a = _make_test_frame()
    check("frame size", len(a) == FRAME_BYTES, "115200 bytes")

    rgb = frame_to_rgb888(a)
    ok = True
    for (x, y) in ((0, 0), (17, 33), (239, 239)):
        v = a[(y * WIDTH + x) * 2] | (a[(y * WIDTH + x) * 2 + 1] << 8)
        r5, g6, b5 = (v >> 11) & 0x1F, (v >> 5) & 0x3F, v & 0x1F
        off = (y * WIDTH + x) * 3
        expect = ((r5 << 3) | (r5 >> 2), (g6 << 2) | (g6 >> 4),
                  (b5 << 3) | (b5 >> 2))
        got = (rgb[off], rgb[off + 1], rgb[off + 2])
        ok = ok and got == expect
    check("rgb565->rgb888", ok, "high-bit replication at 3 sample pixels")

    png = encode_png(rgb)
    with tempfile.TemporaryDirectory() as td:
        png_path = Path(td) / "roundtrip.png"
        png_path.write_bytes(png)
        decoded, w, h = decode_png_rgb(png)
        same = (decoded == rgb and w == WIDTH and h == HEIGHT)
        check("png roundtrip", same,
              "240x240, %d bytes PNG, 57600 px identical" % len(png))

    b = bytearray(a)
    for y in range(20, 23):
        for x in range(10, 12):
            o = (y * WIDTH + x) * 2
            b[o] = 0xAA
            b[o + 1] = 0xBB
    d = diff_frames(a, bytes(b))
    expect_bbox = {"x0": 10, "y0": 20, "x1": 11, "y1": 22}
    check("diff pixels/bbox",
          d["pixels"] == 6 and d["bbox"] == expect_bbox,
          "6/57600 px, bbox (10,20)-(11,22), regions=%s"
          % ", ".join("%s=%d" % kv for kv in d["regions"].items()))
    check("diff identical", diff_frames(a, a)["same"],
          "same frame -> 0 px")

    try:
        check_frame_size(b"x" * 1000, "truncated")
        check("size reject", False, "truncated frame was accepted")
    except FrameError as exc:
        check("size reject", "expected 115200" in str(exc),
              "1 KB buffer rejected with clear message")

    try:
        load_frame("does-not-exist.bin")
        check("missing file reject", False, "missing file was accepted")
    except FrameError as exc:
        check("missing file reject", "cannot read" in str(exc),
              "missing file rejected")

    print("SELFTEST %s %d/%d" % (
        "PASS" if not failures else "FAIL",
        checks[0] - len(failures), checks[0]))
    return 0 if not failures else 1


# --------------------------------------------------------------------------
# CLI
# --------------------------------------------------------------------------

def _dump_json(obj):
    print(json.dumps(obj, ensure_ascii=False, indent=2, sort_keys=True))


def main(argv=None):
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(encoding="utf-8", errors="replace")
        except (AttributeError, ValueError):
            pass
    p = argparse.ArgumentParser(
        prog="fb.py",
        description="240x240 RGB565LE frame buffer -> PNG / diff (stdlib only)")
    p.add_argument("src", nargs="?", help="input .bin (115200 bytes)")
    p.add_argument("dst", nargs="?", help="output .png")
    p.add_argument("--diff", nargs=2, metavar=("A", "B"),
                   help="per-pixel difference of two .bin frames")
    p.add_argument("--crc", help="expected crc32 (hex) of the input frame")
    p.add_argument("--json", action="store_true", help="machine-readable output")
    p.add_argument("--selftest", action="store_true",
                   help="run offline self-test (writes only to a temp dir)")
    args = p.parse_args(argv)

    if args.selftest:
        return _selftest()

    if args.diff:
        try:
            a = load_frame(args.diff[0])
            b = load_frame(args.diff[1])
        except FrameError as exc:
            print("ERROR: %s" % exc, file=sys.stderr)
            return 2
        d = diff_frames(a, b)
        if args.json:
            _dump_json(d)
        else:
            print(format_diff(d))
        return 0

    if not args.src or not args.dst:
        p.error("need SRC and DST (or --diff A B / --selftest)")
    try:
        info = frame_to_png(args.src, args.dst, expected_crc=args.crc)
    except FrameError as exc:
        print("ERROR: %s" % exc, file=sys.stderr)
        return 2
    if args.json:
        _dump_json(info)
    else:
        print("OK %s -> %s" % (info["src"], info["png"]))
        print("  frame: %d bytes crc32=%s sha256=%s"
              % (info["bytes"], info["crc32"], info["sha256"][:16]))
        print("  png:   %d bytes sha256=%s"
              % (info["png_bytes"], info["png_sha256"][:16]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
