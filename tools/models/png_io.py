# Minimal PNG reader and writer (stdlib zlib + numpy) for the fidelity checker (task T22e).
#
# Reads non-interlaced 8 and 16 bit greyscale, grey+alpha, RGB, RGBA and 1/2/4 bit greyscale
# or indexed PNGs into a uint8/uint16 array (H, W, C). Writes 8 bit greyscale / RGB / RGBA
# with a fixed zlib level, so the same array always gives the same bytes (committed masks).
# Interlaced files are refused (PngError): glTF exporters do not write them.

import struct
import zlib

import numpy as np

SIG = b"\x89PNG\r\n\x1a\n"


class PngError(Exception):
    pass


def _chunks(data):
    if data[:8] != SIG:
        raise PngError("not a PNG file")
    off = 8
    while off + 8 <= len(data):
        n, typ = struct.unpack_from(">I4s", data, off)
        body = data[off + 8:off + 8 + n]
        yield typ, body
        off += 12 + n
        if typ == b"IEND":
            return


def _unfilter(raw, h, stride, bpp):
    out = np.zeros((h, stride), dtype=np.uint8)
    prev = np.zeros(stride, dtype=np.int32)
    pos = 0
    for y in range(h):
        ft = raw[pos]
        line = np.frombuffer(raw, dtype=np.uint8, count=stride, offset=pos + 1).astype(np.int32)
        pos += stride + 1
        if ft == 0:
            cur = line
        elif ft == 1:
            cur = line.copy()
            for i in range(bpp, stride):
                cur[i] = (cur[i] + cur[i - bpp]) & 255
        elif ft == 2:
            cur = (line + prev) & 255
        elif ft == 3:
            cur = line.copy()
            for i in range(stride):
                left = cur[i - bpp] if i >= bpp else 0
                cur[i] = (cur[i] + ((left + prev[i]) >> 1)) & 255
        elif ft == 4:
            cur = line.copy()
            for i in range(stride):
                a = cur[i - bpp] if i >= bpp else 0
                b = prev[i]
                c = prev[i - bpp] if i >= bpp else 0
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pred = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                cur[i] = (cur[i] + pred) & 255
        else:
            raise PngError(f"bad filter type {ft}")
        out[y] = cur
        prev = cur
    return out


def _unfilter_fast(raw, h, stride, bpp):
    """Same as _unfilter but vectorised for filter types 0, 1 (cumulative sum per channel),
    2; types 3 and 4 fall back to the loop for that row only."""
    out = np.zeros((h, stride), dtype=np.uint8)
    prev = np.zeros(stride, dtype=np.int32)
    pos = 0
    for y in range(h):
        ft = raw[pos]
        line = np.frombuffer(raw, dtype=np.uint8, count=stride, offset=pos + 1).astype(np.int32)
        pos += stride + 1
        if ft == 0:
            cur = line
        elif ft == 1:
            cur = (np.cumsum(line.reshape(-1, bpp), axis=0) & 255).reshape(-1)
        elif ft == 2:
            cur = (line + prev) & 255
        elif ft == 3:
            cur = line.copy()
            for i in range(stride):
                left = cur[i - bpp] if i >= bpp else 0
                cur[i] = (cur[i] + ((left + prev[i]) >> 1)) & 255
        elif ft == 4:
            # Paeth: sequential in x; vectorise over the bpp channels by pixel
            cur = line.copy()
            n = stride // bpp
            c2 = cur.reshape(n, bpp)
            p2 = prev.reshape(n, bpp)
            left = np.zeros(bpp, dtype=np.int32)
            ul = np.zeros(bpp, dtype=np.int32)
            for x in range(n):
                up = p2[x]
                p = left + up - ul
                pa, pb, pc = np.abs(p - left), np.abs(p - up), np.abs(p - ul)
                pred = np.where((pa <= pb) & (pa <= pc), left, np.where(pb <= pc, up, ul))
                c2[x] = (c2[x] + pred) & 255
                left = c2[x]
                ul = up
            cur = c2.reshape(-1)
        else:
            raise PngError(f"bad filter type {ft}")
        out[y] = cur
        prev = np.asarray(cur, dtype=np.int32)
    return out


def read_png(data):
    """bytes -> (array (H, W, C) uint8 or uint16, info dict)."""
    ihdr = None
    plte = None
    idat = bytearray()
    for typ, body in _chunks(data):
        if typ == b"IHDR":
            ihdr = struct.unpack(">IIBBBBB", body)
        elif typ == b"PLTE":
            plte = np.frombuffer(body, dtype=np.uint8).reshape(-1, 3)
        elif typ == b"IDAT":
            idat.extend(body)
    if ihdr is None:
        raise PngError("no IHDR")
    w, h, depth, ctype, _comp, _filt, interlace = ihdr
    if interlace:
        raise PngError("interlaced PNG is not supported")
    chan = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}.get(ctype)
    if chan is None or depth not in (1, 2, 4, 8, 16):
        raise PngError(f"unsupported PNG type {ctype} depth {depth}")
    bits = chan * depth
    stride = (w * bits + 7) // 8
    bpp = max(1, bits // 8)
    raw = zlib.decompress(bytes(idat))
    if len(raw) != h * (stride + 1):
        raise PngError("PNG data has the wrong length")
    rows = _unfilter_fast(raw, h, stride, bpp)
    if depth == 8:
        arr = rows.reshape(h, w, chan)
    elif depth == 16:
        arr = rows.reshape(h, w * chan, 2)
        arr = (arr[..., 0].astype(np.uint16) << 8 | arr[..., 1]).reshape(h, w, chan)
    else:
        per = 8 // depth
        bitsarr = np.unpackbits(rows, axis=1).reshape(h, -1, depth)
        vals = np.zeros(bitsarr.shape[:2], dtype=np.uint8)
        for k in range(depth):
            vals = (vals << 1) | bitsarr[..., k]
        arr = vals[:, :w].reshape(h, w, 1)
        if ctype == 0:
            arr = (arr.astype(np.uint16) * 255 // (2 ** depth - 1)).astype(np.uint8)
        del per
    if ctype == 3:
        if plte is None:
            raise PngError("indexed PNG without palette")
        arr = plte[arr[..., 0]]
    return arr, {"width": w, "height": h, "depth": depth, "color_type": ctype}


def write_png(path, arr):
    """Write a uint8 array (H, W) or (H, W, 1|3|4) as a PNG (deterministic)."""
    arr = np.ascontiguousarray(arr)
    if arr.dtype != np.uint8:
        raise PngError("write_png takes uint8")
    if arr.ndim == 2:
        arr = arr[:, :, None]
    h, w, c = arr.shape
    ctype = {1: 0, 3: 2, 4: 6}[c]
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        raw.extend(arr[y].tobytes())

    def chunk(typ, body):
        return (struct.pack(">I", len(body)) + typ + body
                + struct.pack(">I", zlib.crc32(typ + body) & 0xFFFFFFFF))
    out = SIG + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, ctype, 0, 0, 0))
    out += chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b"")
    with open(path, "wb") as fh:
        fh.write(out)
