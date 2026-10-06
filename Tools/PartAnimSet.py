"""Makes an animation set for Unlimiter's per part animations (PartAnimations.h).

A set is a car's parts animations under a name of its own: the header's car name hash is the set's,
and every animation in it is named <SET>_ZAM_<NAME>. Put it next to the car's file as
CARS\\<CAR>\\PARTS_ANIMATIONS_<ANYTHING>.BIN, edit its frames and pivots in PartAnimatorulator, and
give the part a Key attribute naming the set: ANIM_TRUNK = <SET>, and so on.

    python PartAnimSet.py CARS\\G35\\PARTS_ANIMATIONS.BIN G35_HATCH CARS\\G35\\PARTS_ANIMATIONS_HATCH.BIN TRUNK_REG
    python PartAnimSet.py --list CARS\\G35\\PARTS_ANIMATIONS_HATCH.BIN

The animations named after the set's name (TRUNK_REG, LEFT_HOOD_SPLIT, ...) go into it, all 21
when none are named. A set leaves out what it does not change; the game takes that from the car.

A longer name than the car's does not fit in place in every animation (RIGHT_DOOR_REG has no room
at all), and renaming in PartAnimatorulator then eats into the data after the name and leaves the
_t track pointing at the old one. Here the animation is laid out again instead: the names stay
where PartAnimatorulator looks for them, and the data after them moves down, every pointer to it
with it.
"""

import struct
import sys

ANIM_CHUNK = 0x00E34010
HEADER_CHUNK = 0x00037100

ANIM_NAMES = [
    'HOOD_REG', 'HOOD_FRONT', 'LEFT_HOOD_SPLIT', 'RIGHT_HOOD_SPLIT', 'FRONT_HOOD_SPLIT', 'BACK_HOOD_SPLIT',
    'LEFT_DOOR_REG', 'LEFT_DOOR_SCISSOR', 'LEFT_DOOR_SUICIDE', 'RIGHT_DOOR_REG', 'RIGHT_DOOR_SCISSOR',
    'RIGHT_DOOR_SUICIDE', 'TRUNK_REG', 'LEFT_DOOR_WIDE1', 'LEFT_DOOR_WIDE2', 'LEFT_DOOR_WIDE3',
    'LEFT_DOOR_WIDE4', 'RIGHT_DOOR_WIDE1', 'RIGHT_DOOR_WIDE2', 'RIGHT_DOOR_WIDE3', 'RIGHT_DOOR_WIDE4',
]


def bstringhash(s):
    h = 0xFFFFFFFF
    for c in s.encode('ascii'):
        h = (h * 33 + c) & 0xFFFFFFFF
    return h


def read_chunks(data):
    off = 0
    while off + 8 <= len(data):
        cid, size = struct.unpack_from('<II', data, off)
        yield cid, data[off + 8: off + 8 + size]
        off += 8 + size


def make_chunk(cid, body):
    if (8 + len(body)) % 16:
        raise ValueError('chunk %08X would be %d bytes, not a multiple of 16' % (cid, 8 + len(body)))
    return struct.pack('<II', cid, len(body)) + body


def cstr(buf, off):
    end = buf.index(b'\0', off)
    return buf[off:end].decode('ascii')


class Anim:
    """An EAGL animation chunk: 0x11 padding, then a relocatable ELF with one .data section."""

    def __init__(self, body):
        self.pad = body.find(b'\x7fELF')
        if self.pad < 0:
            raise ValueError('animation chunk without an ELF')
        self.body = bytearray(body)
        e = self.e = self.body[self.pad:]

        self.shoff, = struct.unpack_from('<I', e, 0x20)
        self.shentsize, self.shnum, self.shstrndx = struct.unpack_from('<HHH', e, 0x2E)
        self.sections = [list(struct.unpack_from('<10I', e, self.shoff + i * self.shentsize)) for i in range(self.shnum)]
        names = self.sections[self.shstrndx]

        def name(s):
            return cstr(e, names[4] + s[0])

        self.data_index = next(i for i, s in enumerate(self.sections) if name(s) == '.data')
        self.symtab_index = next(i for i, s in enumerate(self.sections) if s[1] == 2)
        self.rel_index = next(i for i, s in enumerate(self.sections) if s[1] == 9 and s[7] == self.data_index)

        data = self.sections[self.data_index]
        self.data = e[data[4]: data[4] + data[5]]
        rel = self.sections[self.rel_index]
        self.relocs = [list(struct.unpack_from('<II', e, rel[4] + j)) for j in range(0, rel[5], 8)]

        # The two track names, found by the pointers to them: the one to a name ending in _q, and the
        # other one into the same area (PartAnimatorulator's renames can leave it pointing anywhere there).
        values = {ro: struct.unpack_from('<I', self.data, ro)[0] for ro, ri in self.relocs}
        self.q_ptr = next(ro for ro, v in values.items() if v < len(self.data) and cstr(self.data, v).endswith('_q'))
        self.q_off = values[self.q_ptr]
        q_name = cstr(self.data, self.q_off)
        names_end = self.q_off + len(q_name) + 1
        while names_end < len(self.data) and self.data[names_end]:
            names_end += 1  # the _t name, wherever it is
        self.after_names = min(v for v in values.values() if v > names_end)
        self.t_ptr = next(ro for ro, v in values.items() if ro != self.q_ptr and self.q_off <= v < self.after_names)
        self.base = q_name[:-2]

    @property
    def prefix(self):
        return self.base[:self.base.index('_ZAM_')]

    @property
    def slot(self):
        return self.base[self.base.index('_ZAM_') + 5:]

    def renamed(self, prefix):
        base = prefix + '_ZAM_' + self.slot
        names = (base + '_q\0' + base + '_t\0').encode('ascii')
        t_off = self.q_off + len(base) + 3

        old = self.data
        if any(old[self.q_off + len(self.base) * 2 + 6: self.after_names]):
            raise ValueError('%s: data between the names and the animation' % self.base)

        start = self.after_names
        end = self.q_off + len(names)
        delta = max(0, ((end + 15) & ~15) - start)

        data = bytearray(old[:self.q_off]) + names
        data += bytes(start + delta - len(data)) + old[start:]

        def moved(off):
            return off + delta if off >= start else off

        relocs = []
        for ro, ri in self.relocs:
            at = moved(ro)
            value, = struct.unpack_from('<I', data, at)
            if ro == self.q_ptr:
                value = self.q_off
            elif ro == self.t_ptr:
                value = t_off
            else:
                value = moved(value)
            struct.pack_into('<I', data, at, value)
            relocs.append((at, ri))

        e = self.e
        data_section = self.sections[self.data_index]
        out = bytearray(e[:data_section[4]]) + data + e[data_section[4] + data_section[5]:]

        shoff = self.shoff + delta if self.shoff > data_section[4] else self.shoff
        struct.pack_into('<I', out, 0x20, shoff)
        for i, s in enumerate(self.sections):
            s = list(s)
            if i == self.data_index:
                s[5] += delta
            elif s[4] > data_section[4]:
                s[4] += delta
            struct.pack_into('<10I', out, shoff + i * self.shentsize, *s)

        rel = self.sections[self.rel_index]
        rel_off = rel[4] + delta if rel[4] > data_section[4] else rel[4]
        for j, (ro, ri) in enumerate(relocs):
            struct.pack_into('<II', out, rel_off + j * 8, ro, ri)

        symtab = self.sections[self.symtab_index]
        sym_off = symtab[4] + delta if symtab[4] > data_section[4] else symtab[4]
        for j in range(0, symtab[5], 16):
            value, = struct.unpack_from('<I', out, sym_off + j + 4)
            shndx, = struct.unpack_from('<H', out, sym_off + j + 14)
            if shndx == self.data_index:
                struct.pack_into('<I', out, sym_off + j + 4, moved(value))

        return bytes(self.body[:self.pad]) + bytes(out)


def header_hash_offset(body):
    i = 0
    while body[i] == 0x11:
        i += 1
    return i + 4


def make_set(car_file, set_name, out_file, wanted):
    data = open(car_file, 'rb').read()
    anims, header = [], None
    for cid, body in read_chunks(data):
        if cid == ANIM_CHUNK:
            anims.append(Anim(body))
        elif cid == HEADER_CHUNK and header is None:
            header = bytearray(body)
    if header is None:
        raise SystemExit('%s has no parts animations header' % car_file)

    wanted = [w.upper().removeprefix('ZAM_') for w in wanted] or [a.slot for a in anims]
    missing = [w for w in wanted if w not in [a.slot for a in anims]]
    if missing:
        raise SystemExit('not in %s: %s (it has %s)' % (car_file, ', '.join(missing), ', '.join(a.slot for a in anims)))

    out = b''
    for a in anims:
        if a.slot in wanted:
            out += make_chunk(ANIM_CHUNK, a.renamed(set_name))

    struct.pack_into('<I', header, header_hash_offset(header), bstringhash(set_name))
    out += make_chunk(HEADER_CHUNK, bytes(header))

    open(out_file, 'wb').write(out)
    print('%s: set %s (0x%08X), %s' % (out_file, set_name, bstringhash(set_name), ', '.join(wanted)))


def list_file(path):
    data = open(path, 'rb').read()
    names = []
    for cid, body in read_chunks(data):
        if cid == ANIM_CHUNK:
            names.append(Anim(body).base)
        elif cid == HEADER_CHUNK:
            h, = struct.unpack_from('<I', body, header_hash_offset(body))
            prefixes = sorted({n[:n.index('_ZAM_')] for n in names})
            known = [p for p in prefixes if bstringhash(p) == h]
            print('header 0x%08X %s' % (h, known[0] if known else '(name not among its animations)'))
            for n in names:
                print('   ', n)
            names = []


if __name__ == '__main__':
    args = sys.argv[1:]
    if len(args) == 2 and args[0] == '--list':
        list_file(args[1])
    elif len(args) >= 3:
        make_set(args[0], args[1], args[2], args[3:])
    else:
        print(__doc__)
