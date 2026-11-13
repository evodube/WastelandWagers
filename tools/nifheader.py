import struct
class Nif:
    def __init__(self, path):
        d = self.d = open(path, 'rb').read()
        i = d.index(b'\n') + 1
        self.hdr_start = 0
        self.ver, = struct.unpack_from('<I', d, i); o = i + 4
        self.endian = d[o]; o += 1
        self.user, self.nblocks, self.uv2 = struct.unpack_from('<III', d, o); o += 12
        # export info: author, process script, export script (byte-length strings); uv2 >= 131 adds one more
        self.exp = []
        n = 4 if self.uv2 >= 103 else 3
        for _ in range(n):
            l = d[o]; self.exp.append(d[o + 1:o + 1 + l]); o += 1 + l
        self.ntypes, = struct.unpack_from('<H', d, o); o += 2
        self.types = []
        for _ in range(self.ntypes):
            l, = struct.unpack_from('<I', d, o); self.types.append(d[o + 4:o + 4 + l].decode()); o += 4 + l
        self.idx = list(struct.unpack_from('<%dH' % self.nblocks, d, o)); o += 2 * self.nblocks
        self.sizes = list(struct.unpack_from('<%dI' % self.nblocks, d, o)); o += 4 * self.nblocks
        self.str_off = o
        self.nstr, self.maxlen = struct.unpack_from('<II', d, o); o += 8
        self.strings = []
        for _ in range(self.nstr):
            l, = struct.unpack_from('<I', d, o); self.strings.append(d[o + 4:o + 4 + l].decode('latin1')); o += 4 + l
        self.str_end = o
        self.ngroups, = struct.unpack_from('<I', d, o); o += 4 + 4 * self.ngroups
        self.blocks_off = o
        self.offsets = []
        for s in self.sizes:
            self.offsets.append(o); o += s
        self.tail = d[o:]
    def btype(self, i): return self.types[self.idx[i]]
    def block(self, i): return self.d[self.offsets[i]:self.offsets[i] + self.sizes[i]]
    def s(self, k): return self.strings[k] if 0 <= k < len(self.strings) else None
    def write(self, path, strings=None):
        strings = self.strings if strings is None else strings
        d = self.d
        out = bytearray(d[:self.str_off])
        enc = [x.encode('latin1') for x in strings]
        out += struct.pack('<II', len(enc), max(len(x) for x in enc))
        for x in enc: out += struct.pack('<I', len(x)) + x
        out += d[self.str_end:]
        open(path, 'wb').write(out)

def avobject(b):
    # NiObjectNET: name(int), extra count + refs, controller; NiAVObject: flags u32, translation, rotation 3x3, scale, collision
    o = 0
    name, = struct.unpack_from('<i', b, o); o += 4
    n, = struct.unpack_from('<I', b, o); o += 4
    extra = list(struct.unpack_from('<%di' % n, b, o)); o += 4 * n
    ctrl, = struct.unpack_from('<i', b, o); o += 4
    flags, = struct.unpack_from('<I', b, o); o += 4
    t = struct.unpack_from('<3f', b, o); o += 12
    r = struct.unpack_from('<9f', b, o); o += 36
    sc, = struct.unpack_from('<f', b, o); o += 4
    col, = struct.unpack_from('<i', b, o); o += 4
    return dict(name=name, extra=extra, ctrl=ctrl, flags=flags, t=t, r=r, s=sc, col=col, end=o)

def trishape(b):
    import numpy as np
    a = avobject(b); o = a['end']
    o += 16  # bounding sphere
    skin, shader, alpha = struct.unpack_from('<iii', b, o); o += 12
    desc, = struct.unpack_from('<Q', b, o); o += 8
    ntri, = struct.unpack_from('<I', b, o); o += 4
    nv, = struct.unpack_from('<H', b, o); o += 2
    dsize, = struct.unpack_from('<I', b, o); o += 4
    vsize = (desc & 0xF) * 4
    uvoff = ((desc >> 8) & 0xF) * 4
    flags = desc >> 44
    full = bool(flags & 0x400)
    pos = []; uv = []
    for i in range(nv):
        v = o + i * vsize
        if full: p = struct.unpack_from('<3f', b, v)
        else: p = struct.unpack_from('<3e', b, v)
        pos.append(p)
        uv.append(struct.unpack_from('<2e', b, v + uvoff))
    o += vsize * nv
    tris = [struct.unpack_from('<3H', b, o + 6 * k) for k in range(ntri)]
    return a, np.array(pos), np.array(uv), tris, shader
