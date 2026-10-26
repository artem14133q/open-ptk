#!/usr/bin/env python3
# reloaded.py
#
# EN: Takes out of «Петька 2: Судный день» Reloaded (Steam) the pictures the
# original game lacks, and puts them in the game's folder as RELOADED.STR for
# the launcher's "restored dialogues".
# RU: Достаёт из «Петьки 2: Судный день» Reloaded (Steam) картинки, которых
# нет в самой игре, и кладёт их в папку игры архивом RELOADED.STR для пункта
# «Восстановленные диалоги» лаунчера.
#
#   python3 reloaded.py <Reloaded's folder> <the game's folder>
#
# Each picture is one of Reloaded's sprites -- cut, as all of them are, out
# of a 640x480 frame of the original's -- put back at its place in such a
# frame and written as a one-frame FLC: a 256-colour palette whose entry 0
# is the colour key, the frame's pixels as they are (a COPY chunk). The
# store is the game's own kind ("StOR", docs/str-format.md), looked up by
# the launcher after the game's stores.
#
# Needs UnityPy and Pillow: pip install UnityPy Pillow

import os
import struct
import sys

W, H = 640, 480
KEY = (255, 0, 255)

# (Reloaded's sprite, the path in RELOADED.STR -- launcher/src/restored.c's
# kClips2 -- what it is.)
CLIPS = [
    ('S-T-22__001', 'PART_1\\PASEKA.BG\\DED\\DEDVSE.FLC', "the apiary's beekeeper dead drunk"),
]


def sprites(reloaded):
    """Reloaded's sprites by name: (the picture, its place in the 640x480 frame)."""
    import UnityPy
    path = os.path.join(reloaded, 'petka2_Data', 'resources.assets')
    env = UnityPy.load(path)
    want = {c[0] for c in CLIPS}
    found = {}
    for obj in env.objects:
        if obj.type.name != 'Sprite':
            continue
        s = obj.read()
        if s.m_Name in want and s.m_Name not in found:
            img = s.image.convert('RGBA')
            off = s.m_RD.textureRectOffset
            # Unity counts from the bottom left; the frame from the top left.
            left = int(round(off.x))
            top = H - int(round(off.y)) - img.height
            found[s.m_Name] = (img, left, top)
    return found


def rgb565(c):
    """A colour as the 16 bits the launcher compares keys by (as the exe does)."""
    return (c[0] >> 3) << 11 | (c[1] >> 2) << 5 | c[2] >> 3


def flc(img, left, top):
    """A one-frame 640x480 FLC with the picture at (left, top)."""
    from PIL import Image
    alpha = img.getchannel('A')
    rgb = img.convert('RGB')
    q = rgb.quantize(colors=255, method=Image.Quantize.MEDIANCUT)
    pal = q.getpalette()[:255 * 3]
    colours = [KEY] + [tuple(pal[i:i + 3]) for i in range(0, len(pal), 3)]
    colours += [(0, 0, 0)] * (256 - len(colours))
    # No colour of the picture may pass for the key.
    for i in range(1, 256):
        if rgb565(colours[i]) == rgb565(KEY):
            r, g, b = colours[i]
            colours[i] = (r, min(g + 8, 255), b)
    pixels = bytearray(W * H)
    qp, ap = q.load(), alpha.load()
    for y in range(img.height):
        fy = top + y
        if not 0 <= fy < H:
            continue
        for x in range(img.width):
            fx = left + x
            if 0 <= fx < W and ap[x, y] >= 128:
                pixels[fy * W + fx] = qp[x, y] + 1

    palette = struct.pack('<HBB', 1, 0, 0) + bytes(c for col in colours for c in col)
    chunks = [(4, palette), (16, bytes(pixels))]
    body = b''
    for kind, data in chunks:
        if len(data) % 2:
            data += b'\0'
        body += struct.pack('<IH', 6 + len(data), kind) + data
    frame = struct.pack('<IHHH6x', 16 + len(body), 0xF1FA, len(chunks), 0) + body
    ring = struct.pack('<IHHH6x', 16, 0xF1FA, 0, 0)
    oframe1 = 128
    oframe2 = oframe1 + len(frame)
    size = oframe2 + len(ring)
    header = struct.pack('<IHHHHHHI', size, 0xAF12, 1, W, H, 8, 3, 100)
    header += b'\0' * (80 - len(header)) + struct.pack('<II', oframe1, oframe2)
    header += b'\0' * (128 - len(header))
    return header + frame + ring


def store(entries):
    """The game's own kind of store: "StOR", the data, then the index."""
    data = b''
    offsets = []
    for name, blob in entries:
        offsets.append((8 + len(data), len(blob)))
        data += blob
    index = struct.pack('<I', len(entries))
    for off, length in offsets:
        index += struct.pack('<III', 0, off, length)
    for name, _ in entries:
        index += name.encode('cp866') + b'\0'
    return b'StOR' + struct.pack('<I', 8 + len(data)) + data + index


def main():
    if len(sys.argv) != 3:
        print('usage: python3 reloaded.py <Reloaded folder> <game folder>')
        return 2
    reloaded, game = sys.argv[1], sys.argv[2]
    found = sprites(reloaded)
    entries = []
    for sprite, path, what in CLIPS:
        if sprite not in found:
            print(f'not in Reloaded: {sprite} ({what})')
            continue
        img, left, top = found[sprite]
        entries.append((path, flc(img, left, top)))
        print(f'{path}: {sprite} at {left},{top}, {img.width}x{img.height} -- {what}')
    if not entries:
        return 1
    out = os.path.join(game, 'RELOADED.STR')
    with open(out, 'wb') as f:
        f.write(store(entries))
    print(f'written: {out}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
