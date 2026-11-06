#!/usr/bin/env python3
"""Writes DDS textures with a full mip chain (Pillow writes only the top level).

    save_dds(image, path, fmt)   fmt: "DXT1" (BC1), "DXT5" (BC3) or "BC5" (two channels, for normal maps)
"""
import io
import struct

from PIL import Image


def _encode(level: Image.Image, fmt: str) -> tuple[bytes, bytes]:
    buf = io.BytesIO()
    level.save(buf, format="DDS", pixel_format=fmt)
    data = buf.getvalue()
    header_len = 128
    if data[84:88] == b"DX10":
        header_len += 20
    return data[:header_len], data[header_len:]


def save_dds(image: Image.Image, path: str, fmt: str = "DXT1") -> None:
    mode = "RGBA" if fmt == "DXT5" else "RGB"
    image = image.convert(mode)
    levels = [image]
    w, h = image.size
    while w > 1 or h > 1:
        w, h = max(1, w // 2), max(1, h // 2)
        levels.append(levels[-1].resize((w, h), Image.LANCZOS))
    header = None
    body = b""
    for lv in levels:
        hdr, data = _encode(lv, fmt)
        header = header or bytearray(hdr)
        body += data
    # flags |= DDSD_MIPMAPCOUNT; mip count; caps |= COMPLEX | MIPMAP
    flags, = struct.unpack_from("<I", header, 8)
    struct.pack_into("<I", header, 8, flags | 0x20000)
    struct.pack_into("<I", header, 28, len(levels))
    caps, = struct.unpack_from("<I", header, 108)
    struct.pack_into("<I", header, 108, caps | 0x8 | 0x400000 | 0x1000)
    with open(path, "wb") as f:
        f.write(bytes(header) + body)
