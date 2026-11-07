#!/usr/bin/env python3
# Copyright (C) 2026 António Malheiro
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# Parts of the design (a recursive walk over the patcher, Max box ids mapped
# to Pd object indexes before anything is written, a placeholder for every box
# that can't be translated, $ and comma escaping) follow max-pd-converter:
#
#   MIT License, Copyright (c) 2020 Timo Hoogland
#   https://github.com/tmhglnd/max-pd-converter
#   Permission is hereby granted, free of charge, to any person obtaining a
#   copy of this software and associated documentation files (the "Software"),
#   to deal in the Software without restriction, including without limitation
#   the rights to use, copy, modify, merge, publish, distribute, sublicense,
#   and/or sell copies of the Software, and to permit persons to whom the
#   Software is furnished to do so, subject to the following conditions:
#   The above copyright notice and this permission notice shall be included
#   in all copies or substantial portions of the Software.
#   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND.

"""Convert a Max for Live device (.amxd) or a Max patch (.maxpat) into a
plugdata patch.

    m4l2pd.py DEVICE.amxd OUTDIR      -> OUTDIR/<name>/<name>.pd (+ abstractions)
    m4l2pd.py --install [--data DIR] [--lv2 DIR] DEVICE.amxd...

--install converts into ~/.local/share/spite9/m4l/<name>/ and writes one LV2
preset per converted device into ~/.lv2/spite-m4l.lv2/, for plugdata-fx
(audio effects) or plugdata (instruments and MIDI effects). In Spite, add
plugdata-fx or plugdata to a track and pick "M4L: <name>" from its presets.

The device's frozen files (abstractions, JavaScript, images) are unpacked next
to the patch, so abstractions convert too. Objects plugdata doesn't have stay
in the patch under their Max name, with their connections, so they show up as
broken objects to replace by hand. Devices encrypted by Ableton (a 'ciph'
chunk) are refused: they can't and mustn't be converted.
"""

import json
import os
import re
import struct
import subprocess
import sys

# ---------------------------------------------------------------- .amxd files

KINDS = {b'aaaa': 'audio_effect', b'mmmm': 'midi_effect', b'iiii': 'instrument'}


class NotConvertible(Exception):
    pass


def read_amxd(path):
    """-> (kind, [(file name, type, bytes)]); the first file is the device's patcher."""
    b = open(path, 'rb').read()
    if b[:4] != b'ampf':
        raise NotConvertible('not a Max for Live device')
    kind = KINDS.get(b[8:12], 'audio_effect')
    i = 12
    patch = None
    while i + 8 <= len(b):
        tag = b[i:i + 4]
        n = struct.unpack('<I', b[i + 4:i + 8])[0]
        if tag == b'ciph':
            raise NotConvertible('encrypted by Ableton')
        if tag == b'ptch':
            patch = b[i + 8:i + 8 + n]
        i += 8 + n
    if patch is None:
        raise NotConvertible('no patcher in the device')
    if patch[:4] != b'mx@c':
        j = patch.find(b'{')
        return kind, [('device.maxpat', 'JSON', patch[j:].rstrip(b'\x00'))]
    dlst = struct.unpack('>I', patch[12:16])[0]
    files = []
    k = dlst + 8
    end = dlst + struct.unpack('>I', patch[dlst + 4:dlst + 8])[0]
    while k + 8 <= min(end, len(patch)) and patch[k:k + 4] == b'dire':
        n = struct.unpack('>I', patch[k + 4:k + 8])[0]
        ent = {}
        m = k + 8
        while m + 8 <= k + n:
            t = patch[m:m + 4]
            ln = struct.unpack('>I', patch[m + 4:m + 8])[0]
            ent[t] = patch[m + 8:m + ln]
            m += ln
        name = ent.get(b'fnam', b'').rstrip(b'\x00').decode('utf-8', 'replace')
        size = struct.unpack('>I', ent[b'sz32'])[0]
        off = struct.unpack('>I', ent[b'of32'])[0]
        files.append((name, ent.get(b'type', b'????').decode('latin1'), patch[off:off + size]))
        k += n
    return kind, files


def load_json(blob):
    text = blob.decode('utf-8', 'replace')
    text = text[:text.rfind('}') + 1]
    return json.loads(text)


# ---------------------------------------------------------------- text

def pd_escape(text):
    """Max box text -> Pd atoms text: escape $ , ; and spaces inside quotes."""
    text = text.replace('\r', ' ').replace('\n', ' ')
    out = []
    quoted = False
    i = 0
    while i < len(text):
        c = text[i]
        if c == '"':
            quoted = not quoted
        elif c == '\\' and i + 1 < len(text):
            out.append('\\' + text[i + 1])
            i += 1
        elif quoted and c == ' ':
            out.append('\\ ')
        elif c == '$':
            out.append('\\$')
        elif c == ',':
            out.append(' \\, ')
        elif c == ';':
            out.append(' \\; ')
        else:
            out.append(c)
        i += 1
    s = ''.join(out)
    s = re.sub(r'#(\d)', r'\\$\1', s)  # abstraction arguments
    s = re.sub(r'(^|\s)---', r'\1\\$0-', s)  # Max for Live local names, one per device
    return re.sub(r' +', ' ', s).strip()


def num(v, default=0):
    try:
        f = float(v)
    except (TypeError, ValueError):
        return default
    return int(f) if f == int(f) else f


# ---------------------------------------------------------------- objects

# Max names whose plugdata object differs or needs a library prefix. Names not
# listed pass through: plugdata registers cyclone's Max clones under their
# Max names ([scale], [split], [gate], [zl], [counter]...).
RENAME = {
    'plugin~': 'adc~',
    'plugout~': 'dac~',
    'deferlow': 't a',
    'defer': 't a',
    'live.thisdevice': 'loadbang',
    'prepend': 'cyclone/prepend',
    'append': 'cyclone/append',
    'sprintf': 'cyclone/sprintf',
    'int': 'i',
    'float': 'f',
    'pattr': 't a',
    'qmetro': 'metro',
    'live.colors': 't a',  # theme colours for the device's look: inert here
}

# classes whose Pd translation has fewer outlets than Max's, so connections
# from the extra outlets are dropped (class -> Pd outlet count)
PD_OUTLETS = {
    'number': 1, 'flonum': 1, 'live.numbox': 1, 'live.dial': 1, 'live.slider': 1,
    'live.text': 1, 'live.toggle': 1, 'live.button': 1,
    'dial': 1, 'live.thisdevice': 1, 'slider': 1, 'toggle': 1, 'button': 1, 'pattr': 1,
    'live.colors': 1,
}


def fix_types(args):
    """Max type letters Pd lacks: i -> f in trigger/pack/unpack arguments."""
    return ' '.join('f' if a == 'i' else a for a in args.split(' ')) if args else args


def if_to_expr(args):
    """[if $i1 > 0 then 1 else 0] -> [expr if($i1 > 0, 1, 0)] when both
    results are numbers or expressions (not messages, not 'out2')"""
    m = re.match(r'(.+?)\s+then\s+(.+?)(?:\s+else\s+(.+))?$', args)
    if not m:
        return None
    cond, a, b = m.group(1), m.group(2), m.group(3) or '0'
    for part in (a, b):
        if re.search(r'[A-Za-z_]', re.sub(r'\$[if]\d', '', part)):
            return None
    return 'expr if(%s \\, %s \\, %s)' % (cond, a, b)


# Max's tempo-relative times, at a fixed 120 BPM (plugdata has no tempo-synced metro)
BPM = 120.0


def notevalue_ms(word):
    """'16n' -> 125.0, '4nd' -> 750.0, '8nt' -> 166.7, '480 ticks' handled by caller"""
    m = re.match(r'^(\d+)n([dt]?)$', word)
    if not m:
        return None
    beats = 4.0 / int(m.group(1))
    beats *= {'': 1.0, 'd': 1.5, 't': 2.0 / 3.0}[m.group(2)]
    return round(beats * 60000.0 / BPM, 3)


def strip_attributes(args):
    """'128n @active 1 @quantize 128n' -> ('128n', {'active': ['1'], 'quantize': ['128n']})"""
    words = args.split()
    keep, attrs, cur = [], {}, None
    for w in words:
        if w.startswith('@') and len(w) > 1:
            cur = w[1:]
            attrs[cur] = []
        elif cur is not None:
            attrs[cur].append(w)
        else:
            keep.append(w)
    return ' '.join(keep), attrs


def timed(name, args, warn):
    """metro/qmetro/phasor~/delay with note values or @attributes -> plain Pd"""
    plain, attrs = strip_attributes(args)
    words = plain.split()
    if 'interval' in attrs and not words:
        words = attrs['interval']
    if len(words) >= 2 and words[1] == 'ticks':
        words = ['%g' % (float(words[0]) / 480.0 * 60000.0 / BPM)]
    if words:
        ms = notevalue_ms(words[0])
        if ms is not None:
            warn('%s %s: tempo-synced time fixed at %g ms (%d BPM)' % (name, words[0], ms, BPM))
            words = ['%g' % (1000.0 / ms if name == 'phasor~' else ms)]
    if name == 'qmetro':
        name = 'metro'
    out = name + (' ' + words[0] if words else '')
    if attrs.get('active') == ['1'] and name == 'metro':
        warn('metro @active 1: starts only when it gets a 1')
    return out


def zmap_expr(args):
    """[zmap a b c d]: scale a..b to c..d and clip"""
    w = args.split()
    if len(w) != 4:
        return None
    a, b, c, d = w
    lo, hi = sorted((float(c), float(d)))
    return 'expr min(max(($f1 - %s) / (%s - %s) * (%s - %s) + %s \\, %g) \\, %g)' % (a, b, a, d, c, c, lo, hi)


def subpatch(xy, name, objects, conns):
    """an inline [pd name] built from (text) objects and (src, outlet, dst, inlet) connections"""
    lines = ['#N canvas 0 50 450 300 %s 0;' % name]
    lines += objects
    lines += ['#X connect %d %d %d %d;' % c for c in conns]
    lines.append('#X restore %s pd %s;' % (xy, name))
    return lines


def gswitch2_sub(xy):
    """[gswitch2]: left inlet 0/1 picks the outlet the right inlet's data goes to"""
    objs = ['#X obj 20 20 inlet;', '#X obj 120 20 inlet;', '#X obj 20 60 + 1;',
            '#X obj 20 100 cyclone/gate 2;', '#X obj 20 140 outlet;', '#X obj 120 140 outlet;']
    return subpatch(xy, 'gswitch2', objs, [(0, 0, 2, 0), (2, 0, 3, 0), (1, 0, 3, 1), (3, 0, 4, 0), (3, 1, 5, 0)])


def gswitch_sub(xy, n):
    """[gswitch]: left inlet picks which of the data inlets reaches the outlet"""
    objs = ['#X obj 20 20 inlet;'] + ['#X obj %d 20 inlet;' % (100 + 60 * i) for i in range(n)]
    objs += ['#X obj 20 60 + 1;', '#X obj 20 100 cyclone/switch %d;' % n, '#X obj 20 140 outlet;']
    plus, sw, out = n + 1, n + 2, n + 3
    conns = [(0, 0, plus, 0), (plus, 0, sw, 0), (sw, 0, out, 0)]
    conns += [(1 + i, 0, sw, 1 + i) for i in range(n)]
    return subpatch(xy, 'gswitch', objs, conns)


def gain_sub(xy, box):
    """[live.gain~] / [gain~]: signal in, scaled by a dB slider, out; then the value"""
    types = box.get('outlettype') or ['signal', 'signal', '']
    nsig = max(1, sum(1 for t in types if t == 'signal'))
    nctl = len(types) - nsig
    lo, hi, init, name = param_info(box)
    if box.get('maxclass') == 'gain~' or hi <= lo:
        lo, hi = -70, 6
    objs = []
    for c in range(nsig):
        objs.append('#X obj %d 20 inlet~;' % (20 + 80 * c))
    sl = len(objs)
    objs.append('#X obj %d 20 vsl 15 100 %s %s 0 0 empty empty empty 0 -9 0 10 #fcfcfc #000000 #000000 0 1;' % (20 + 80 * nsig, lo, hi))
    objs += ['#X obj %d 140 + 100;' % (20 + 80 * nsig), '#X obj %d 170 dbtorms;' % (20 + 80 * nsig),
             '#X obj %d 200 pack f 20;' % (20 + 80 * nsig), '#X obj %d 230 line~;' % (20 + 80 * nsig)]
    line = sl + 4
    conns = [(sl, 0, sl + 1, 0), (sl + 1, 0, sl + 2, 0), (sl + 2, 0, sl + 3, 0), (sl + 3, 0, line, 0)]
    for c in range(nsig):
        objs.append('#X obj %d 270 *~;' % (20 + 80 * c))
        mul = len(objs) - 1
        objs.append('#X obj %d 310 outlet~;' % (20 + 80 * c))
        conns += [(c, 0, mul, 0), (line, 0, mul, 1), (mul, 0, len(objs) - 1, 0)]
    for k in range(nctl):
        objs.append('#X obj %d 310 outlet;' % (20 + 80 * (nsig + 1 + k)))
        if k == 0:
            conns.append((sl, 0, len(objs) - 1, 0))
    return subpatch(xy, 'gain', objs, conns)


def menu_items(box):
    v = box.get('saved_attribute_attributes', {}).get('valueof', {})
    items = v.get('parameter_enum') or box.get('items') or []
    if isinstance(items, str):
        items = items.split(',')
    out, cur = [], []
    for i in items:
        if i == ',':
            out.append(' '.join(cur))
            cur = []
        else:
            cur.append(str(i))
    if cur:
        out.append(' '.join(cur))
    return [i.strip() for i in out if i.strip()] if box.get('maxclass') in ('umenu',) else [str(i) for i in items if str(i) != ',']


def menu_sub(xy, box):
    """[live.menu] / [umenu] / [live.tab]: index out of the left outlet, item name out of the next"""
    items = menu_items(box) or ['0', '1']
    n = len(items)
    nout = max(1, box.get('numoutlets', 2))
    objs = ['#X obj 20 20 inlet;',
            '#X obj 20 50 hradio 15 1 0 %d empty empty empty 0 -8 0 10 #fcfcfc #000000 #000000 0;' % n,
            '#X obj 20 90 t f f;', '#X obj 20 200 outlet;']
    conns = [(0, 0, 1, 0), (1, 0, 2, 0), (2, 1, 3, 0)]
    if nout > 1:
        objs.append('#X obj 120 200 outlet;')
        out1 = len(objs) - 1
        objs.append('#X obj 120 120 sel %s;' % ' '.join(str(i) for i in range(n)))
        sel = len(objs) - 1
        conns.append((2, 0, sel, 0))
        for i, it in enumerate(items):
            objs.append('#X msg %d 160 symbol %s;' % (120 + 70 * i, pd_escape(it).replace(' ', '\\ ') or '-'))
            conns += [(sel, i, len(objs) - 1, 0), (len(objs) - 1, 0, out1, 0)]
        for k in range(2, nout):
            objs.append('#X obj %d 200 outlet;' % (220 + 60 * k))
    return subpatch(xy, 'menu', objs, conns)


def param_info(box):
    """range and initial value of a live.* parameter"""
    v = box.get('saved_attribute_attributes', {}).get('valueof', {})
    lo = num(v.get('parameter_mmin', 0))
    hi = num(v.get('parameter_mmax', 127 if box.get('maxclass') != 'live.toggle' else 1))
    init = v.get('parameter_initial', [lo])
    init = num(init[0] if isinstance(init, list) and init else init, lo)
    name = v.get('parameter_longname') or box.get('varname') or ''
    return lo, hi, init, name


# ---------------------------------------------------------------- the device's face

# Live 12's dark theme, measured on screenshots of Live's device view
LIVE = {
    'bg': '#181818',      # device background
    'track': '#3d3d3d',   # dial track
    'arc': '#03c3d5',     # dial value arc and needle
    'value': '#ffad56',   # numbers and menus
    'text': '#9f9f9f',    # labels
    'on': '#ffad56',      # live.text / live.toggle when on
    'off': '#2e2e2e',     # live.text / live.toggle when off
    'cell': '#262626',    # live.tab cell
    'cellon': '#7d7d7d',  # live.tab selected cell
}
DEVICE_H = 169  # every Max for Live device is 169 px tall
UNITS = {2: 'ms', 3: 'Hz', 4: 'dB', 5: '%', 7: 'st'}  # parameter_unitstyle -> unit text


def color(c, default):
    """Max RGBA floats (or {'color': [...]}) -> '#rrggbb'"""
    if isinstance(c, dict):
        c = c.get('color')
    if isinstance(c, list) and len(c) >= 3:
        return '#%02x%02x%02x' % tuple(max(0, min(255, int(round(float(v) * 255)))) for v in c[:3])
    return default


def pd_symbol(text):
    """one Pd atom: spaces, commas, semicolons and dollars escaped"""
    s = str(text).replace('\\', '').replace(' ', '\\ ').replace(',', '\\,').replace(';', '\\;').replace('$', '\\$')
    return s or '-'


def item_msg(item):
    """a menu or tab item as a Pd message: a number stays a float, anything else is a symbol"""
    try:
        float(str(item))
        return str(item)
    except ValueError:
        return 'symbol ' + pd_symbol(item)


def presented(b):
    """the box's presentation rectangle, or None when it is not on the face"""
    if b.get('presentation') and not b.get('hidden') and b.get('presentation_rect'):
        return [float(v) for v in b['presentation_rect'][:4]]
    return None


def face_size(p):
    """device width x height: devicewidth, else the presentation's extent"""
    rects = [presented(b['box']) for b in p.get('boxes', [])]
    rects = [r for r in rects if r]
    if not rects:
        return None
    w = num(p.get('devicewidth', 0))
    if not w:
        w = max(r[0] + r[2] for r in rects)
    h = max(r[1] + r[3] for r in rects)
    return int(round(w)), DEVICE_H if h > DEVICE_H - 20 else int(round(h))


class PdObj:
    """one top-level Pd object (a subpatch spans several lines)"""
    def __init__(self, lines, bounds=None, nin=1, nout=1, pres=None):
        self.lines = lines if isinstance(lines, list) else [lines]
        self.bounds = bounds    # (x, y, w, h): plugdata's getPdBounds (), estimated, for tidy cords
        self.nin, self.nout = nin, nout
        self.pres = pres        # (x, y) on the face, or None


class Tr:
    """the translation of one Max box: Pd objects, and how Max's inlets and outlets map onto them"""
    def __init__(self, objs, outmap=None, inmap=None, conns=(), nout=None):
        self.objs = objs if isinstance(objs, list) else [objs]
        self.outmap = outmap    # {max outlet: (k, pd outlet)}; None = the same outlet of objs[0]
        self.inmap = inmap      # {max inlet: (k, pd inlet)}; None = the same inlet of objs[0]
        self.conns = list(conns)  # cords inside the translation: (k, outlet, k2, inlet)
        self.nout = nout        # with outmap None: outlets of objs[0]; cords from beyond are dropped


def text_bounds(x, y, text, nin=1, nout=1, h=20):
    """plugdata's size of a text object: Inter at 15 px, about 7 px a character"""
    w = max(min(int(7 * len(text)) + 13, 420), 18 * max(nin, nout, 1), 7)
    return (x, y, w, h)


# ---- tidy cords: Max midpoints -> plugdata's segmented connection path

MARGIN = 6  # plugdata Object::margin


def iolet_centre(bounds, idx, inlet, n_in, n_out):
    """plugdata's Iolet::getCanvasBounds ().getCentre (), in Pd coordinates (default iolet layout)"""
    x, y, w, h = bounds
    W, H = w + 2 * MARGIN, h + 2 * MARGIN
    max_w = min((W - 12) // max(n_in, 1) - 4, (W - 12) // max(n_out, 1) - 4)
    s = max(min(13, max_w, int(H / 2.0 - 2)), 10)
    border = int(7 + (s - 10) * 5 / 3.0)
    n = n_in if inlet else n_out
    space = max(0, min(border, W - 6 * n - border))
    iy = int((MARGIN + 1 if inlet else H - MARGIN) - s / 2.0)
    if n <= 1:
        ix = int(W // 2 - (s - 1) / 2.0) if W < 25 + s else space
    else:
        ix = int(space + (W - 2 * space - s) / float(n - 1) * idx)
    return x - MARGIN + ix + s // 2, y - MARGIN + iy + s // 2


def simplify(pts):
    """drop repeated points and the middle points of straight runs"""
    out = []
    for p in pts:
        if out and p == out[-1]:
            continue
        if len(out) >= 2 and ((out[-2][0] == out[-1][0] == p[0]) or (out[-2][1] == out[-1][1] == p[1])):
            out[-1] = p
            continue
        out.append(p)
    return out


def cord_path(midpoints, src_rect, dst_rect, o, i, dy=0):
    """Max's corners (a flat x, y list) -> plugdata's path symbol: JUCE base64 of int32 x, y pairs
    relative to the outlet centre o, from the outlet to the inlet i. None when the cord is not a
    tidy one, so plugdata draws its usual curve."""
    if not midpoints or len(midpoints) % 2:
        return None
    mids = [(int(x), int(y) + dy) for x, y in zip(midpoints[0::2], midpoints[1::2])]
    o_max = (mids[0][0], int(src_rect[1] + src_rect[3]) + dy)
    i_max = (mids[-1][0], int(dst_rect[1]) + dy)
    pts = simplify([o_max] + mids + [i_max])
    if len(pts) < 4:
        return None
    if not all(a[0] == b[0] or a[1] == b[1] for a, b in zip(pts, pts[1:])):
        return None
    vert = [a[0] == b[0] for a, b in zip(pts, pts[1:])]
    if not vert[0] or not vert[-1] or any(s == t for s, t in zip(vert, vert[1:])):
        return None
    if (pts[1][1] >= o_max[1]) != (pts[1][1] >= o[1]) or (pts[-2][1] <= i_max[1]) != (pts[-2][1] <= i[1]):
        return None
    plan = [o, (o[0], pts[1][1])] + pts[2:-2] + [(i[0], pts[-2][1]), i]
    return juce_base64(b''.join(struct.pack('<ii', px - o[0], py - o[1]) for px, py in plan))


# ---- Live's controls, drawn by plugdata's objects in Live's colours

def param_flags(b):
    v = b.get('saved_attribute_attributes', {}).get('valueof', {})
    return v, bool(v.get('parameter_initial_enable', 0))


def dial(b, xy, rect, face):
    """live.dial -> [else/knob] with its name above (a [note]) and its value below"""
    lo, hi, init, _ = param_info(b)
    v, init_on = param_flags(b)
    w, h = rect[2], rect[3]
    appearance = b.get('appearance', 0)  # 0 vertical, 1 tiny, 2 panel, 3 large
    showname = b.get('showname', 1) and appearance != 1
    shownumber = b.get('shownumber', 1) and appearance != 1
    name_h = 11 if showname else 0
    num_h = 11 if shownumber else 0
    size = max(10, int(min(w, h - name_h - num_h)))
    kx, ky = int((w - size) / 2), name_h
    exp = num(v.get('parameter_exponent', 1))
    start = 0 if lo < 0 < hi else lo
    steps = int(num(v.get('parameter_steps', 0)))
    fg = color(b.get('activeneedlecolor') or b.get('activedialcolor'), LIVE['arc'])
    knob = ('else/knob %d %s %s %s %s empty empty %s %s %s 0 0 %d %d 1 300 0 0 %s empty empty %d 11 %d %d 0 %d 0 0 0 1'
            % (size, lo, hi, 0 if exp == 1 else exp, init, LIVE['bg'], LIVE['track'], fg, steps,
               1 if v.get('parameter_type') == 1 else 0, start, 1 if shownumber else 0,
               int(size / 2) - 12, size + 1, 1 if init_on else 0))
    x, y = xy
    objs = [PdObj('#X obj %d %d %s;' % (x + kx, y + ky, knob), (x + kx, y + ky, size + 1, size + 1),
                  1, 1, (int(face[0]) + kx, int(face[1]) + ky) if face else None)]
    if showname:
        name = v.get('parameter_shortname') or v.get('parameter_longname') or ''
        objs.append(note(name, (x, y), int(w), color(b.get('textcolor'), LIVE['text']), 1,
                         (int(face[0]), int(face[1])) if face else None))
    objs.append(normalized(lo, hi, exp, (x, y + ky + size + 14)))
    return Tr(objs, outmap={0: (0, 0), 1: (len(objs) - 1, 0)}, inmap={0: (0, 0)}, conns=[(0, 0, len(objs) - 1, 0)])


def normalized(lo, hi, exp, xy):
    """Max's live.dial / live.numbox / live.slider send their value normalized to 0-1 from the
    right outlet: an [expr] after the Pd object gives that outlet"""
    span = (hi - lo) or 1
    e = 'expr pow(max(min(($f1 - %s) / %s \\, 1) \\, 0) \\, %s)' % (lo, span, 1.0 / exp if exp else 1)
    return PdObj('#X obj %d %d %s;' % (xy[0], xy[1], e.replace('$', '\\$')), text_bounds(xy[0], xy[1], e, 1, 1), 1, 1)


def note(text, xy, w, col, just, pres, size=11):
    """a label: ELSE's [note] (plugdata draws it, and clicks pass through it in run mode)"""
    r, g, b = (int(col[i:i + 2], 16) for i in (1, 3, 5))
    words = ' '.join(pd_symbol(t) for t in str(text).split()) or '-'
    return PdObj('#X obj %d %d note %d %d Inter empty 0 %d %d %d 0 0 0 0 0 %d %s;'
                 % (xy[0], xy[1], max(w, 8), size, r, g, b, just, words), None, 1, 0, pres)


def numbox(b, xy, rect, face):
    """live.numbox -> [nbx] in Live's orange, its unit as the label"""
    lo, hi, init, _ = param_info(b)
    v, init_on = param_flags(b)
    w, h = rect[2], rect[3]
    unit = UNITS.get(int(num(v.get('parameter_unitstyle', 1))), '')
    digits = max(3, int((w - (6 * len(unit) + 4 if unit else 0)) / 6))
    fg = color(b.get('textcolor') or b.get('activeslidercolor'), LIVE['value'])
    lines = ('#X obj %d %d nbx %d %d %s %s 0 %d empty empty %s %d %d 0 10 %s %s %s %s 256;'
             % (xy[0], xy[1], digits, max(8, int(h) - 1), lo, hi, 1 if init_on else 0,
                pd_symbol(unit) if unit else 'empty', digits * 6 + int(h / 2) + 2, int(h / 2),
                LIVE['bg'], fg, fg, init))
    exp = num(v.get('parameter_exponent', 1)) or 1
    return Tr([PdObj(lines, (xy[0], xy[1], int(w), int(h)), 1, 1, (int(face[0]), int(face[1])) if face else None),
               normalized(lo, hi, exp, (xy[0], xy[1] + int(h) + 14))],
              outmap={0: (0, 0), 1: (1, 0)}, inmap={0: (0, 0)}, conns=[(0, 0, 1, 0)])


def popmenu(b, xy, rect, face):
    """live.menu / umenu -> ELSE [popmenu] (index out) and a helper giving the item's name"""
    items = menu_items(b) or ['-']
    v, init_on = param_flags(b)
    init = int(num((v.get('parameter_initial') or [0])[0] if isinstance(v.get('parameter_initial'), list) else v.get('parameter_initial', 0)))
    w, h = int(rect[2]), int(rect[3])
    fg = color(b.get('lcdcolor') or b.get('textcolor'), LIVE['value'])
    menu = ('#X obj %d %d popmenu %d %d 10 %s %s empty empty empty empty empty 0 0 %d %d 0 1 0 0 0 0 0 %s;'
            % (xy[0], xy[1], max(w, 20), max(h, 12), LIVE['bg'], fg, init, 1 if init_on else 0,
               ' '.join(pd_symbol(i) for i in items)))
    objs = [PdObj(menu, (xy[0], xy[1], w, h), 1, 1, (int(face[0]), int(face[1])) if face else None)]
    sel = ['#X obj 20 20 inlet;', '#X obj 20 50 sel %s;' % ' '.join(str(k) for k in range(len(items))),
           '#X obj 20 200 outlet;']
    conns = [(0, 0, 1, 0)]
    for k, it in enumerate(items):
        sel.append('#X msg %d 100 %s;' % (20 + 80 * k, item_msg(it)))
        conns += [(1, k, 3 + k, 0), (3 + k, 0, 2, 0)]
    objs.append(PdObj(subpatch('%d %d' % (xy[0], xy[1] + h + 12), 'menu-item', sel, conns),
                      text_bounds(xy[0], xy[1] + h + 12, 'pd menu-item'), 1, 1))
    return Tr(objs, outmap={0: (0, 0), 1: (1, 0)}, inmap={0: (0, 0)}, conns=[(0, 0, 1, 0)])


def toggle_button(b, xy, rect, face, conv):
    """live.text / live.toggle / live.button -> ELSE [button] (toggle or bang) with its text on top"""
    cls = b.get('maxclass')
    w, h = int(rect[2]), int(rect[3])
    mode = 1 if (cls == 'live.toggle' or (cls == 'live.text' and b.get('mode', 1) == 1)) else 2
    bg = color(b.get('bgcolor') or b.get('activebgcolor'), LIVE['off'])
    fg = color(b.get('bgoncolor') or b.get('activebgoncolor'), LIVE['on'])
    objs = [PdObj('#X obj %d %d button %d %d %s %s %d 0 0 0 0 empty empty;' % (xy[0], xy[1], max(w, 8), max(h, 8), bg, fg, mode),
                  (xy[0], xy[1], w, h), 1, 1, (int(face[0]), int(face[1])) if face else None)]
    text = b.get('text') if cls == 'live.text' else ''
    if text:
        objs.append(note(text, (xy[0], xy[1] + max(0, int((h - 13) / 2))), w, color(b.get('textcolor'), '#e0e0e0'), 1,
                         (int(face[0]), int(face[1]) + max(0, int((h - 13) / 2))) if face else None))
    return Tr(objs, outmap={0: (0, 0)}, inmap={0: (0, 0)})


def tab(b, xy, rect, face, conv):
    """live.tab -> a grid of cells; with pictures, the device's SVGs rendered to PNG on [pic]s"""
    items = menu_items(b) or ['0', '1']
    n = len(items)
    rows = max(1, int(num(b.get('num_lines_presentation' if face else 'num_lines_patching', 1)) or 1))
    cols = max(1, -(-n // rows))
    w, h = rect[2], rect[3]
    cw, ch = w / cols, h / rows
    conv.tabs += 1
    tag = 'tab%d' % conv.tabs
    pics = conv.pictures(b.get('pictures') or [], int(cw), int(ch)) if b.get('pictures') else []
    v, init_on = param_flags(b)
    init = int(num((v.get('parameter_initial') or [0])[0] if isinstance(v.get('parameter_initial'), list) else 0))
    objs = []
    for k in range(n):
        r, c = divmod(k, cols)
        cx, cy = int(c * cw), int(r * ch)
        pres = (int(face[0]) + cx, int(face[1]) + cy) if face else None
        objs.append(PdObj('#X obj %d %d cnv 1 %d %d empty \\$0-%s-c%d empty 0 0 0 10 %s #000000 0;'
                          % (xy[0] + cx, xy[1] + cy, max(int(cw) - 1, 2), max(int(ch) - 1, 2), tag, k,
                             LIVE['cellon'] if k == init else LIVE['cell']), None, 0, 0, pres))
    for k in range(n):
        r, c = divmod(k, cols)
        cx, cy = int(c * cw), int(r * ch)
        pres = (int(face[0]) + cx, int(face[1]) + cy) if face else None
        if k < len(pics) and pics[k]:
            objs.append(PdObj('#X obj %d %d pic 0 %s \\$0-%s-%d empty 0 0;' % (xy[0] + cx, xy[1] + cy, pics[k], tag, k),
                              None, 1, 1, pres))
        else:
            objs.append(PdObj('#X obj %d %d button %d %d %s %s 2 0 1 0 0 \\$0-%s-%d empty;'
                              % (xy[0] + cx, xy[1] + cy, max(int(cw) - 1, 4), max(int(ch) - 1, 4), LIVE['cell'], LIVE['cellon'], tag, k),
                              None, 1, 1, pres))
            objs.append(note(items[k], (xy[0] + cx, xy[1] + cy + max(0, int((ch - 13) / 2))), int(cw), LIVE['text'], 1,
                             (pres[0], pres[1] + max(0, int((ch - 13) / 2))) if pres else None))
    # the logic: a click (or a number into the inlet) selects, lights the cell, outputs index and
    # name. The cells are coloured through [s] objects: plugdata's Pd doesn't expand $0 in
    # message boxes, so '; $0-...' messages would go nowhere.
    sub = ['#X obj 20 20 inlet;', '#X obj 20 160 t f f f;', '#X obj 20 400 outlet;', '#X obj 200 400 outlet;',
           '#X obj 300 200 sel %s;' % ' '.join(str(k) for k in range(n)),
           '#X msg 500 240 color %s #000000;' % LIVE['cell'],
           '#X obj 200 200 sel %s;' % ' '.join(str(k) for k in range(n)),
           '#X obj 20 60 loadbang;', '#X msg 20 90 %d;' % init, '#X obj 20 120 spigot %d;' % (1 if init_on else 0)]
    conns = [(0, 0, 1, 0), (1, 0, 2, 0), (1, 1, 6, 0), (1, 2, 4, 0), (7, 0, 8, 0), (8, 0, 9, 0), (9, 0, 1, 0)]
    base = len(sub)
    for k in range(n):
        sub.append('#X obj %d 20 r \\$0-%s-%d;' % (120 + 60 * k, tag, k))
        sub.append('#X msg %d 50 %d;' % (120 + 60 * k, k))
        conns += [(base + 2 * k, 0, base + 2 * k + 1, 0), (base + 2 * k + 1, 0, 1, 0)]
    base = len(sub)
    for k in range(n):
        x = 200 + 90 * k
        sub.append('#X msg %d 300 %s;' % (x, item_msg(items[k])))
        sub.append('#X msg %d 330 color %s #000000;' % (x, LIVE['cellon']))
        sub.append('#X obj %d 360 s \\$0-%s-c%d;' % (x, tag, k))
        name, on, send = base + 3 * k, base + 3 * k + 1, base + 3 * k + 2
        # every cell off first (the shared message reaches all the [s]), then this one on
        conns += [(6, k, name, 0), (name, 0, 3, 0), (4, k, 5, 0), (4, k, on, 0), (on, 0, send, 0), (5, 0, send, 0)]
    objs.append(PdObj(subpatch('%d %d' % (xy[0], xy[1] + int(h) + 12), tag, sub, conns),
                      text_bounds(xy[0], xy[1] + int(h) + 12, 'pd ' + tag), 1, 2))
    logic = len(objs) - 1
    return Tr(objs, outmap={0: (logic, 0), 1: (logic, 1)}, inmap={0: (logic, 0)})


def panel(b, xy, rect, face):
    w, h = max(int(rect[2]), 1), max(int(rect[3]), 1)
    bg = color(b.get('bgfillcolor') or b.get('bgcolor'), '#2e2e2e')
    return Tr(PdObj('#X obj %d %d cnv 1 %d %d empty empty empty 20 12 0 14 %s #404040 0;' % (xy[0], xy[1], w, h, bg),
                    None, 0, 0, (int(face[0]), int(face[1])) if face else None), nout=0)


def comment(b, xy, rect, face):
    size = int(round(num(b.get('fontsize', 10)) * 1.2))
    col = color(b.get('textcolor'), LIVE['text'])
    return Tr(note(b.get('text', ''), xy, int(rect[2]), col, int(num(b.get('textjustification', 0))),
                   (int(face[0]), int(face[1])) if face else None, size), nout=0)


def line(b, xy, rect, face):
    w, h = max(int(rect[2]), 1), max(int(rect[3]), 1)
    w, h = (w, 1) if w >= h else (1, h)
    return Tr(PdObj('#X obj %d %d cnv 1 %d %d empty empty empty 20 12 0 14 %s #404040 0;'
                    % (xy[0], xy[1], w, h, color(b.get('linecolor'), LIVE['track'])),
                    None, 0, 0, (int(face[0]), int(face[1])) if face else None), nout=0)


# ---- Flechtwerk: its compiled Plaits port, rebuilt on ELSE's [plaits~]

def flechtwerk_voice(xy):
    """the voice of trnr.Flechtwerk (poly~ with one voice): the same messages and receives as the
    compiled trnr.core.flechtwerk~, scaled for [plaits~]. MIDI comes straight from [notein]."""
    o = []
    c = []
    def obj(line):
        o.append(line)
        return len(o) - 1
    inl = obj('#X obj 20 20 inlet;')
    ni = obj('#X obj 20 60 notein;')
    sn = obj('#X obj 20 90 stripnote;')
    tf = obj('#X obj 20 120 t b f;')
    pitch = obj('#X obj 60 160 sig~;')
    freq = obj('#X obj 140 160 r~ \\$0-freq;')
    add = obj('#X obj 60 200 +~;')
    clk = obj('#X obj 20 200 click~;')
    vel = obj('#X obj 140 120 / 127;')
    pl = obj('#X obj 60 320 plaits~ -midi;')
    m1 = obj('#X obj 60 360 *~ 0;')
    m2 = obj('#X obj 160 360 *~ 0;')
    o1 = obj('#X obj 60 400 outlet~;')
    o2 = obj('#X obj 160 400 outlet~;')
    c += [(ni, 0, sn, 0), (ni, 1, sn, 1), (sn, 0, tf, 0), (sn, 1, vel, 0), (tf, 1, pitch, 0), (tf, 0, clk, 0),
          (pitch, 0, add, 0), (freq, 0, add, 1), (add, 0, pl, 0), (clk, 0, pl, 1),
          (vel, 0, m1, 1), (vel, 0, m2, 1), (pl, 0, m1, 0), (pl, 1, m2, 0), (m1, 0, o1, 0), (m2, 0, o2, 0)]
    rc = obj('#X obj 300 20 r \\$0-ctrl;')
    rt = obj('#X obj 300 50 route harmonics timbre morph freq_mod timbre_mod morph_mod lpg_colour lpg_decay;')
    c.append((rc, 0, rt, 0))
    scale = [None] * 8  # Flechtwerk sends these from the dials' normalized (0-1) outlets
    name = ['harmonics', 'timbre', 'morph', 'freq_mod', 'timbre_mod', 'morph_mod', 'cutoff', 'decay']
    for k in range(8):
        x = 300 + 75 * k
        m = obj('#X msg %d 130 %s \\$1;' % (x, name[k]))
        if scale[k]:
            s = obj('#X obj %d 95 %s;' % (x, scale[k]))
            c += [(rt, k, s, 0), (s, 0, m, 0)]
        else:
            c.append((rt, k, m, 0))
        c.append((m, 0, pl, 0))
    for rx, recv, msg in ((300, 'model', 'model \\$1'), (420, 'lpg', 'tr_active \\$1')):
        r = obj('#X obj %d 190 r \\$0-%s;' % (rx, recv))
        m = obj('#X msg %d 220 %s;' % (rx, msg))
        c += [(r, 0, m, 0), (m, 0, pl, 0)]
    # the defaults the dials send at load reach plaits~ through the receives above
    return subpatch(xy, 'flechtwerk-voice', o, c)


KNOWN_EXTERNALS = {
    # poly~ voice abstraction whose DSP is a compiled external -> builder of a Pd voice
    'trnr.core.flechtwerk~': flechtwerk_voice,
}


def poly_voice_external(patcher):
    for b in patcher.get('boxes', []):
        name = ((b['box'].get('text') or '').split(' ') or [''])[0]
        if name in KNOWN_EXTERNALS:
            return name
    return None


class Converter:
    def __init__(self, outdir, abstractions):
        self.outdir = outdir
        self.abstractions = abstractions  # name -> patcher JSON, converted on demand
        self.done = set()
        self.warnings = []
        self.boxes = 0
        self.taps = 0
        self.tabs = 0
        self.canvases = 0
        self.bpatcher_gop = {}  # abstraction name -> (w, h, x margin, y margin) when shown in a bpatcher

    def warn(self, msg):
        self.warnings.append(msg)

    def convert_abstraction(self, name):
        if name in self.done or name not in self.abstractions:
            return
        self.done.add(name)
        gop = self.bpatcher_gop.get(name)
        lines = ['#N canvas 0 50 900 600 12;']
        lines += self.patcher(self.abstractions[name]['patcher'], face=gop is not None, gop=gop)
        if gop:
            lines.append('#X coords 0 -1 1 1 %d %d 2 %d %d;' % gop)
        with open(os.path.join(self.outdir, name + '.pd'), 'w') as f:
            f.write('\n'.join(lines) + '\n')

    def pictures(self, names, w, h):
        """the device's pictures (SVGs are rendered with rsvg-convert) -> PNG names next to the patch"""
        out = []
        for name in names:
            base = os.path.basename(str(name))
            src = os.path.join(self.outdir, base)
            if not os.path.exists(src):
                self.warn('picture %s is not in the device' % base)
                out.append(None)
                continue
            if not base.lower().endswith('.svg'):
                out.append(base)
                continue
            png = '%s-%dx%d.png' % (os.path.splitext(base)[0], w, h)
            # Inkscape's PDF imports clip each path to a page rectangle that rsvg draws as
            # nothing, while Max ignores it: render without clip paths
            svg = re.sub(r'\sclip-path="url\([^)]*\)"', '', open(src, encoding='utf-8', errors='replace').read())
            tmp = os.path.join(self.outdir, '.render-' + base)
            with open(tmp, 'w', encoding='utf-8') as f:
                f.write(svg)
            try:
                subprocess.run(['rsvg-convert', '-w', str(w), '-h', str(h), '-a', '-o',
                                os.path.join(self.outdir, png), tmp], check=True, capture_output=True)
                out.append(png)
            except (OSError, subprocess.CalledProcessError):
                self.warn('could not render %s: rsvg-convert is needed for pictures' % base)
                out.append(None)
            finally:
                os.remove(tmp)
        return out

    def delay_lines(self, boxes, lines):
        """Max joins [tapin~] to [tapout~] with a cord; Pd names the delay line.
        Give each tapin~ a name, and drop the cords between them."""
        cls = {b['id']: ((b.get('text') or '').split(' ') or [''])[0] for b in boxes}
        for b in boxes:
            if cls[b['id']] == 'tapin~':
                self.taps += 1
                b['_delay'] = 'tap%d' % self.taps
        for l in lines:
            pl = l.get('patchline', l)
            src, dst = pl['source'][0], pl['destination'][0]
            if cls.get(src) == 'tapin~' and cls.get(dst) == 'tapout~':
                for b in boxes:
                    if b['id'] == src:
                        name = b['_delay']
                    if b['id'] == dst:
                        target = b
                target['_delay'] = name
                pl['_drop'] = True

    def window_targets(self, boxes, lines):
        """[pcontrol] opens the subpatchers its outlet is wired to: name those canvases, drop the cords"""
        cls = {b['id']: ((b.get('text') or '').split(' ') or [''])[0] for b in boxes}
        for l in lines:
            pl = l.get('patchline', l)
            src, dst = pl['source'][0], pl['destination'][0]
            if cls.get(src) == 'pcontrol':
                for b in boxes:
                    if b['id'] == dst:
                        if '_canvas' not in b:
                            self.canvases += 1
                            b['_canvas'] = 'cv%d' % self.canvases
                        for s in boxes:
                            if s['id'] == src:
                                s.setdefault('_opens', []).append(b['_canvas'])
                pl['_drop'] = True

    def patcher(self, p, face=False, gop=None):
        """-> Pd record lines for the boxes and cords of one patcher. face: also write its
        presentation layout (the device itself, or a bpatcher); gop: (w, h, x margin, y margin)."""
        boxes = [b['box'] for b in p.get('boxes', [])]
        self.delay_lines(boxes, p.get('lines', []))
        self.window_targets(boxes, p.get('lines', []))
        own = None
        if any(((b.get('text') or '').split(' ') or [''])[0] == 'thispatcher' for b in boxes):
            self.canvases += 1
            own = 'cv%d' % self.canvases
        lines, ports, pres, count = [], {}, [], 0
        if face and not gop and face_size(p):
            # Live draws a device on its own background (#363636 in Live 12's dark theme);
            # behind everything on the face, above the patch in edit mode
            w, h = face_size(p)
            lines.append('#X obj 0 -%d cnv 1 %d %d empty empty empty 20 12 0 14 #363636 #404040 0;' % (h + 40, w, h))
            pres.append((0, (0, 0)))
            count = 1
        # Max lists boxes front to back, and draws 'background' boxes behind the rest;
        # Pd draws in creation order: write them back to front
        order = [b for b in reversed(boxes) if b.get('background')] + [b for b in reversed(boxes) if not b.get('background')]
        for b in order:
            self.boxes += 1
            tr = self.box(b, face, own, gop)
            ports[b['id']] = (count, tr, b)
            for k, o in enumerate(tr.objs):
                lines.extend(o.lines)
                if face and o.pres is not None:
                    pres.append((count + k, o.pres))
            count += len(tr.objs)
        if own:
            lines.append('#X obj 10 10 namecanvas \\$0-%s;' % own)
            count += 1
        conns = []
        for base, tr, b in ports.values():
            for k, so, k2, di in tr.conns:
                conns.append(((base + k, so, 0, len(conns)), base + k2, di, None))
        # Max sends a fan-out in its own order (saved as 'order' since Max 8, else right to left,
        # then bottom to top); Pd sends in the order the connections were made: write Max's order
        cords = []
        for l in p.get('lines', []):
            pl = l.get('patchline', l)
            src, so = pl['source'][0], pl['source'][1]
            dst, di = pl['destination'][0], pl['destination'][1]
            if src not in ports or dst not in ports or pl.get('_drop'):
                continue
            sbase, stra, sbox = ports[src]
            dbase, dtra, dbox = ports[dst]
            if stra.outmap is None:
                if stra.nout is not None and so >= stra.nout:
                    self.warn('dropped a connection from outlet %d of %s' % (so, self.label(sbox)))
                    continue
                sk, sout = 0, so
            elif so in stra.outmap:
                sk, sout = stra.outmap[so]
            else:
                self.warn('dropped a connection from outlet %d of %s' % (so, self.label(sbox)))
                continue
            if dtra.inmap is None:
                dk, din = 0, di
            elif di in dtra.inmap:
                dk, din = dtra.inmap[di]
            else:
                self.warn('dropped a connection to inlet %d of %s' % (di, self.label(dbox)))
                continue
            path = None
            so_obj, do_obj = stra.objs[sk], dtra.objs[dk]
            if pl.get('midpoints') and so_obj.bounds and do_obj.bounds:
                o = iolet_centre(so_obj.bounds, sout, False, so_obj.nin, so_obj.nout)
                i = iolet_centre(do_obj.bounds, din, True, do_obj.nin, do_obj.nout)
                path = cord_path(pl['midpoints'], sbox.get('patching_rect', [0, 0, 0, 0]),
                                 dbox.get('patching_rect', [0, 0, 0, 0]), o, i)
            rect = dbox.get('patching_rect', [0, 0, 0, 0])
            cords.append(((sbase + sk, sout), dbase + dk, din, path, pl.get('order'), (-rect[0], -rect[1])))
        groups = {}
        for c in cords:
            groups.setdefault(c[0], []).append(c)
        for key, group in groups.items():
            if all(c[4] is not None for c in group):
                group.sort(key=lambda c: c[4])
            else:
                group.sort(key=lambda c: c[5])
            for n, c in enumerate(group):
                conns.append(((key[0], key[1], 1, n), c[1], c[2], c[3]))
        conns.sort(key=lambda c: c[0])
        for (s, so, _, _), d, di, path in conns:
            lines.append('#X connect %d %d %d %d%s;' % (s, so, d, di, ' ' + path if path else ''))
        if face and pres:
            size = (gop[0], gop[1]) if gop else face_size(p)
            if size:
                lines.append('#X presentation size %d %d;' % size)
            for idx, (px, py) in pres:
                lines.append('#X presentation obj %d %d %d;' % (idx, px, py))
        return lines

    @staticmethod
    def label(b):
        return b.get('text') or b.get('maxclass', '?')

    def box(self, b, face_on, own, gop):
        """-> Tr: the Pd objects for one Max box"""
        cls = b.get('maxclass', 'newobj')
        prect = [float(v) for v in (b.get('patching_rect') or [0, 0, 20, 20])[:4]]
        x, y = int(prect[0]), int(prect[1])
        xy = '%d %d' % (x, y)
        text = b.get('text', '') or ''
        nin, nout = b.get('numinlets', 1), b.get('numoutlets', 1)
        face = presented(b) if face_on else None
        rect = face or prect
        pres = (int(face[0]), int(face[1])) if face else None

        def obj(pdtext, nout_pd=None, h=20):
            return Tr(PdObj('#X obj %s %s;' % (xy, pdtext), text_bounds(x, y, pdtext, nin, nout, h), nin, nout),
                      nout=nout_pd)

        def sub(lines, name, n_in=None, n_out=None):
            return Tr(PdObj(lines, text_bounds(x, y, 'pd ' + name, nin, nout, 19),
                            nin if n_in is None else n_in, nout if n_out is None else n_out))

        if cls == 'newobj':
            words = text.split(' ', 1)
            name, args = words[0], (words[1] if len(words) > 1 else '')
            if name in ('p', 'patcher') and 'patcher' in b:
                subname = (pd_escape(args) or 'sub').replace(' ', '_')
                inner = self.patcher(b['patcher'])
                if '_canvas' in b:
                    inner.append('#X obj 10 10 namecanvas \\$0-%s;' % b['_canvas'])
                return sub(['#N canvas 0 50 700 500 %s 0;' % subname] + inner
                           + ['#X restore %s pd %s;' % (xy, pd_escape(args) or 'sub')], subname)
            if name == 'tapin~' and '_delay' in b:
                size = (args.split() or ['1000'])[0]
                return obj('delwrite~ \\$0-%s %s' % (b['_delay'], size), 0)
            if name == 'tapout~' and '_delay' in b:
                taps = args.split() or ['0']
                objs, conns = [], []
                for i, t in enumerate(taps):
                    objs.append('#X obj %d 20 inlet;' % (20 + 100 * i))
                    objs.append('#X obj %d 60 delread~ \\$0-%s %s;' % (20 + 100 * i, b['_delay'], t))
                    objs.append('#X obj %d 100 outlet~;' % (20 + 100 * i))
                    conns += [(3 * i, 0, 3 * i + 1, 0), (3 * i + 1, 0, 3 * i + 2, 0)]
                return sub(subpatch(xy, 'tapout~', objs, conns), 'tapout~')
            if name == 'poly~':
                return self.poly(b, xy, args, sub)
            if name == 'pcontrol':
                return sub(self.pcontrol_sub(xy, b.get('_opens', [])), 'pcontrol', 1, 1)
            if name == 'thispatcher':
                return sub(self.thispatcher_sub(xy, own, gop), 'thispatcher', 1, 2)
            if name in ('metro', 'qmetro', 'phasor~', 'del', 'delay') and re.search(r'@|\d+n[dt]?\b|ticks', args):
                return obj(pd_escape(timed(name, args, self.warn)))
            if name == 'zmap':
                expr = zmap_expr(args)
                if expr:
                    return obj(expr.replace('$', '\\$'))
            if name == 'ctlout' and args.split() and not re.match(r'^[-\d$#]', args.split()[0]):
                args = ' '.join(args.split()[1:])  # a MIDI port name, plugdata has one port
            if name == 'pattr':
                args = ''
            if name.startswith('zl.') and len(name) > 3:
                name, args = 'zl', (name[3:] + ' ' + args).strip()  # cyclone's [zl] takes the mode first
            if '@' in args and name not in ('js', 'v8', 'node.script', 'gen~'):
                args, attrs = strip_attributes(args)
                self.warn('%s: dropped Max attributes %s' % (name, ', '.join('@' + a for a in attrs)))
            if name in ('t', 'trigger', 'pack', 'pak', 'unpack'):
                args = fix_types(args)
                if name == 'pak':
                    name = 'pack'
            if name in ('plugin~', 'plugout~') and not args.strip():
                args = '1 2'
            if name in ('if', 'If'):
                expr = if_to_expr(args)
                if expr:
                    return obj(expr.replace('$', '\\$'))
                m = re.match(r'(.+?)\s+then\s+bang$', args)
                if m:  # [if a then bang] -> [expr if(a, 1, 0)] -> [sel 1]
                    e = 'expr if(%s \\, 1 \\, 0)' % m.group(1)
                    return Tr([PdObj('#X obj %s %s;' % (xy, e.replace('$', '\\$')), text_bounds(x, y, e, nin, 1), nin, 1),
                               PdObj('#X obj %d %d sel 1;' % (x, y + 24), text_bounds(x, y + 24, 'sel 1', 2, 2), 2, 2)],
                              outmap={0: (1, 0)}, inmap={k: (0, k) for k in range(nin)}, conns=[(0, 0, 1, 0)])
                self.warn('kept [if %s]' % args)
            if name in self.abstractions:
                self.convert_abstraction(name)
            name = RENAME.get(name, name)
            return obj(pd_escape((name + ' ' + args).strip()), PD_OUTLETS.get(words[0]))

        if cls == 'message':
            return Tr(PdObj('#X msg %s %s;' % (xy, pd_escape(text)), (x, y, max(min(7 * len(text) + 13, 420), 14), 20), 1, 1))
        if cls in ('comment', 'live.comment'):
            if face:
                return comment(b, (x, y), rect, face)
            return Tr(PdObj('#X text %s %s;' % (xy, pd_escape(text) or '.'), None, 0, 0), nout=0)
        if cls == 'inlet':
            sig = 'signal' in (b.get('outlettype') or [''])
            return obj('inlet~' if sig else 'inlet')
        if cls == 'outlet':
            return obj('outlet~' if b.get('_signal') else 'outlet')
        if cls == 'live.numbox':
            return numbox(b, (x, y), rect, face)
        if cls in ('number', 'flonum'):
            return Tr(PdObj('#X floatatom %s 5 0 0 0 - - - 0;' % xy, (x, y, 38, 19), 1, 1, pres), nout=1)
        if cls in ('live.text', 'live.toggle', 'live.button'):
            return toggle_button(b, (x, y), rect, face, self)
        if cls == 'toggle':
            return Tr(PdObj('#X obj %s tgl 15 0 empty empty empty 17 7 0 10 #fcfcfc #000000 #000000 0 1;' % xy,
                            (x, y, 16, 16), 1, 1, pres), nout=1)
        if cls in ('button', 'textbutton', 'ubutton'):
            return Tr(PdObj('#X obj %s bng 15 250 50 0 empty empty empty 17 7 0 10 #fcfcfc #000000 #000000;' % xy,
                            (x, y, 16, 16), 1, 1, pres), nout=1)
        if cls == 'live.dial':
            return dial(b, (x, y), rect, face)
        if cls == 'dial':
            return Tr(PdObj('#X obj %s else/knob 40 0 127 0 0 empty empty %s %s %s 0 0 0 0 1 300 0 0 0 empty empty 0 11 0 41 0 0 0 0 0 1;'
                            % (xy, LIVE['bg'], LIVE['track'], LIVE['arc']), (x, y, 41, 41), 1, 1, pres), nout=1)
        if cls in ('live.gain~', 'gain~'):
            return sub(gain_sub(xy, b), 'gain')
        if cls == 'gswitch2':
            return sub(gswitch2_sub(xy), 'gswitch2', 2, 2)
        if cls == 'gswitch':
            n = max(2, b.get('numinlets', 3) - 1)
            return sub(gswitch_sub(xy, n), 'gswitch', n + 1, 1)
        if cls in ('live.menu', 'umenu'):
            return popmenu(b, (x, y), rect, face)
        if cls == 'live.tab':
            return tab(b, (x, y), rect, face, self)
        if cls in ('slider', 'live.slider'):
            lo, hi, init, name = param_info(b) if cls == 'live.slider' else (0, 127, 0, '')
            w, h = rect[2], rect[3]
            if w > h:
                return Tr(PdObj('#X obj %s hsl %d %d %s %s 0 0 empty empty empty -2 -8 0 10 %s %s #000000 0 1;'
                                % (xy, max(int(w), 8), max(int(h), 8), lo, hi, LIVE['off'], LIVE['arc']),
                                (x - 3, y, int(w) + 6, int(h) + 1), 1, 1, pres), nout=1)
            return Tr(PdObj('#X obj %s vsl %d %d %s %s 0 0 empty empty empty 0 -9 0 10 %s %s #000000 0 1;'
                            % (xy, max(int(w), 8), max(int(h), 8), lo, hi, LIVE['off'], LIVE['arc']),
                            (x, y - 2, int(w) + 1, int(h) + 6), 1, 1, pres), nout=1)
        if cls == 'radiogroup':
            items = b.get('saved_attribute_attributes', {}).get('valueof', {}).get('parameter_enum') or b.get('items') or []
            n = max(2, len([i for i in items if i != ',']) if isinstance(items, list) else 2)
            return Tr(PdObj('#X obj %s hradio 15 1 0 %d empty empty empty 0 -8 0 10 #fcfcfc #000000 #000000 0;' % (xy, n),
                            (x, y, 15 * n + 1, 16), 1, 1, pres), nout=1)
        if cls == 'panel':
            return panel(b, (x, y), rect, face)
        if cls == 'live.line':
            return line(b, (x, y), rect, face)
        if cls == 'fpic':
            return self.fpic(b, (x, y), pres)
        if cls in ('live.colors', 'swatch'):
            return Tr(PdObj('#X text %s (%s);' % (xy, cls), None, 0, 0), nout=0)
        if cls == 'bpatcher':
            return self.bpatcher(b, x, y, rect, pres)

        # anything else stays, under its Max name, as a broken object with its connections
        self.warn('not converted: ' + cls)
        return obj(pd_escape(cls))

    def bpatcher(self, b, x, y, rect, pres):
        """a bpatcher shows its patcher's presentation layout through a window of its own size:
        a graph-on-parent subpatch whose margins are the bpatcher's offset"""
        w, h = max(int(rect[2]), 1), max(int(rect[3]), 1)
        off = b.get('offset') or [0, 0]
        gop = (w, h, -int(num(off[0])), -int(num(off[1])))
        nin, nout = b.get('numinlets', 0), b.get('numoutlets', 0)
        if 'patcher' in b:
            inner = self.patcher(b['patcher'], face=True, gop=gop)
            if '_canvas' in b:
                inner.append('#X obj 10 10 namecanvas \\$0-%s;' % b['_canvas'])
            lines = (['#N canvas 0 50 700 500 bpatcher 0;'] + inner
                     + ['#X coords 0 -1 1 1 %d %d 2 %d %d;' % gop, '#X restore %d %d pd bpatcher;' % (x, y)])
            return Tr(PdObj(lines, (x, y, w, h), nin, nout, pres))
        name = os.path.splitext(b.get('name', 'bpatcher'))[0]
        args = ' '.join(str(a) for a in b.get('args', []))
        if name in self.abstractions:
            self.bpatcher_gop.setdefault(name, gop)
            self.convert_abstraction(name)
        else:
            self.warn('bpatcher file %s is not in the device' % b.get('name'))
        return Tr(PdObj('#X obj %d %d %s;' % (x, y, pd_escape((name + ' ' + args).strip())), (x, y, w, h), nin, nout, pres))

    def poly(self, b, xy, args, sub):
        """poly~: a voice whose DSP is a known compiled external is rebuilt in Pd; other voice
        patchers are inlined as one voice"""
        words = args.split()
        voice = words[0] if words else ''
        n = int(num(words[1], 1)) if len(words) > 1 else 1
        if voice in self.abstractions:
            ext = poly_voice_external(self.abstractions[voice]['patcher'])
            if ext:
                self.warn('%s: rebuilt with ELSE [plaits~]' % ext if ext == 'trnr.core.flechtwerk~' else '%s rebuilt' % ext)
                return sub(KNOWN_EXTERNALS[ext](xy), 'flechtwerk-voice')
            if n > 1:
                self.warn('poly~ %s: %d voices played as one' % (voice, n))
            inner = self.voice_patcher(self.abstractions[voice]['patcher'])
            return sub(['#N canvas 0 50 700 500 %s 0;' % voice] + inner + ['#X restore %s pd %s;' % (xy, voice)], voice)
        self.warn('poly~ %s: the voice patcher is not in the device' % voice)
        return sub(['#N canvas 0 50 450 300 poly~ 0;', '#X restore %s pd poly~;' % xy], 'poly~')

    def voice_patcher(self, p):
        """a poly~ voice as a subpatch: [in N]/[out N] become inlets/outlets in N's order"""
        p = json.loads(json.dumps(p))
        for bb in p.get('boxes', []):
            box = bb['box']
            words = (box.get('text') or '').split()
            if words and words[0] in ('in', 'in~', 'out', 'out~'):
                k = int(num(words[1], 1)) if len(words) > 1 else 1
                box['text'] = {'in': 'inlet', 'in~': 'inlet~', 'out': 'outlet', 'out~': 'outlet~'}[words[0]]
                box['patching_rect'] = [20 + 60 * (k - 1), box.get('patching_rect', [0, 0])[1]] + box.get('patching_rect', [0, 0, 40, 20])[2:]
            elif words and words[0] in ('thispoly~', 'polymidiin'):
                box['text'] = 't a'
        return self.patcher(p)

    @staticmethod
    def pcontrol_sub(xy, names):
        """[pcontrol]: open / close the subpatchers it was wired to"""
        objs = ['#X obj 20 20 inlet;', '#X obj 20 50 route open close;', '#X msg 20 80 vis 1;', '#X msg 100 80 vis 0;',
                '#X obj 200 200 outlet;']
        conns = [(0, 0, 1, 0), (1, 0, 2, 0), (1, 1, 3, 0)]
        for k, n in enumerate(names):
            objs.append('#X obj %d 120 s \\$0-%s;' % (20 + 120 * k, n))
            conns += [(2, 0, len(objs) - 1, 0), (3, 0, len(objs) - 1, 0)]
        return subpatch(xy, 'pcontrol', objs, conns)

    def thispatcher_sub(self, xy, own, gop):
        """[thispatcher]: scroll a bpatcher (offset), size, open and close this canvas"""
        objs = ['#X obj 20 20 inlet;', '#X obj 20 50 route offset window front wclose;',
                '#X obj 20 300 s \\$0-%s;' % own, '#X obj 300 300 outlet;', '#X obj 400 300 outlet;',
                '#X obj 120 90 route size;', '#X msg 120 130 setbounds \\$1 \\$2 \\$3 \\$4;',
                '#X msg 220 130 vis 1;', '#X msg 300 130 vis 0;']
        conns = [(0, 0, 1, 0), (1, 1, 5, 0), (5, 0, 6, 0), (6, 0, 2, 0), (1, 2, 7, 0), (1, 3, 8, 0), (7, 0, 2, 0), (8, 0, 2, 0)]
        if gop:
            objs += ['#X obj 20 90 expr -\\$f1 \\; -\\$f2;', '#X obj 20 130 pack f f;',
                     '#X msg 20 170 coords 0 -1 1 1 %d %d 2 \\$1 \\$2;' % (gop[0], gop[1])]
            conns += [(1, 0, 9, 0), (9, 0, 10, 0), (9, 1, 10, 1), (10, 0, 11, 0), (11, 0, 2, 0)]
        return subpatch(xy, 'thispatcher', objs, conns)

    def fpic(self, b, xy, pres):
        """fpic -> ELSE [pic] with the picture next to the patch (embedded pictures are written out)"""
        data = b.get('data')
        name = os.path.basename(str(b.get('pic') or ''))
        if isinstance(data, str) and '.' in data:
            self.taps += 1
            name = 'fpic%d.png' % self.taps
            try:
                with open(os.path.join(self.outdir, name), 'wb') as f:
                    f.write(juce_unbase64(data))
            except ValueError:
                name = ''
        if not name:
            self.warn('fpic without a picture')
            return Tr(PdObj('#X text %d %d (fpic);' % xy, None, 0, 0), nout=0)
        return Tr(PdObj('#X obj %d %d pic 0 %s empty empty 0 0;' % (xy[0], xy[1], pd_symbol(name)), None, 1, 1, pres))


def mark_signal_outlets(p):
    """an [outlet] fed by a signal becomes [outlet~]"""
    boxes = {b['box']['id']: b['box'] for b in p.get('boxes', [])}
    for l in p.get('lines', []):
        pl = l.get('patchline', l)
        src = boxes.get(pl['source'][0])
        dst = boxes.get(pl['destination'][0])
        if src and dst and dst.get('maxclass') == 'outlet':
            types = src.get('outlettype') or []
            if pl['source'][1] < len(types) and types[pl['source'][1]] == 'signal':
                dst['_signal'] = True
    for b in boxes.values():
        if 'patcher' in b:
            mark_signal_outlets(b['patcher'])


def convert(src, outroot):
    """-> (output .pd path, device kind, warnings)"""
    base = re.sub(r'[^\w.+-]+', '_', os.path.splitext(os.path.basename(src))[0])
    outdir = os.path.join(outroot, base)
    os.makedirs(outdir, exist_ok=True)
    if src.lower().endswith('.amxd'):
        kind, files = read_amxd(src)
    else:
        kind, files = 'audio_effect', [(os.path.basename(src), 'JSON', open(src, 'rb').read())]

    main = load_json(files[0][2])
    abstractions = {}
    for name, typ, blob in files[1:]:
        if name.endswith('.maxpat') or name.endswith('.maxhelp'):
            try:
                abstractions[os.path.splitext(name)[0]] = load_json(blob)
            except ValueError:
                pass
        elif name:
            # JavaScript, images, samples... next to the patch, as Max had them
            with open(os.path.join(outdir, os.path.basename(name)), 'wb') as f:
                f.write(blob)
    for a in abstractions.values():
        mark_signal_outlets(a['patcher'])
    mark_signal_outlets(main['patcher'])

    conv = Converter(outdir, abstractions)
    has_face = face_size(main['patcher']) is not None
    lines = ['#N canvas 0 50 1000 700 12;'] + conv.patcher(main['patcher'], face=has_face)
    out = os.path.join(outdir, base + '.pd')
    with open(out, 'w') as f:
        f.write('\n'.join(lines) + '\n')
    return out, kind, conv.warnings


# ---------------------------------------------------------------- plugdata presets

PLUGDATA_URI = {
    'audio_effect': 'https://github.com/timothyschoen/plugdata-fx',
    'midi_effect': 'https://github.com/timothyschoen/plugdata',
    'instrument': 'https://github.com/timothyschoen/plugdata',
}


def xml_attr(s):
    return (s.replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;')
            .replace('"', '&quot;').replace('\n', '&#10;').replace('\r', ''))


def plugdata_state(patch_path):
    """plugdata's plugin state (PluginProcessor::setStateInformation) opening
    patch_path: the patch text embedded, its folder kept for abstractions"""
    content = open(patch_path).read()
    xml = ('<plugdata_save Version="0.9.4" Legacy="0" Oversampling="0" Latency="0" TailLength="0.0"'
           ' Width="1000" Height="660"><Patches><Patch Content="%s" Location="%s" SplitIndex="0"'
           ' PluginMode="0" PluginModeScale="100"/></Patches></plugdata_save>'
           % (xml_attr(content), xml_attr(os.path.abspath(patch_path))))
    x = xml.encode('utf-8')
    blob = struct.pack('<iiif', 0, 0, 0, 0.0)  # no legacy patches, latency, oversampling, tail
    blob += struct.pack('<i', 8 + len(x) + 1)
    blob += struct.pack('<II', 0x21324356, len(x)) + x + b'\x00'  # JUCE copyXmlToBinary
    return blob


JUCE_B64 = '.ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+'


def juce_base64(data):
    """juce::MemoryBlock::toBase64Encoding (): '<size>.' then 6-bit groups taken
    from the bytes' bits, least significant first, in JUCE's own alphabet. JUCE
    plugins read their LV2 state back with fromBase64Encoding ()."""
    out, acc, bits = [], 0, 0
    for byte in data:  # streamed: a plugin state can be megabytes
        acc |= byte << bits
        bits += 8
        while bits >= 6:
            out.append(JUCE_B64[acc & 63])
            acc >>= 6
            bits -= 6
    if bits:
        out.append(JUCE_B64[acc & 63])
    return '%d.%s' % (len(data), ''.join(out))


def juce_unbase64(s):
    """the inverse of juce_base64 ()"""
    size, _, rest = s.partition('.')
    out, acc, bits = bytearray(), 0, 0
    for c in rest:
        acc |= JUCE_B64.index(c) << bits
        bits += 6
        while bits >= 8:
            out.append(acc & 255)
            acc >>= 8
            bits -= 8
    return bytes(out[:int(size)])


def preset_label(name):
    return 'M4L: ' + name


def write_presets(data_dir, lv2_dir):
    """one LV2 preset per converted device, for plugdata(-fx), in lv2_dir/spite-m4l.lv2"""
    bundle = os.path.join(lv2_dir, 'spite-m4l.lv2')
    os.makedirs(bundle, exist_ok=True)
    for f in os.listdir(bundle):
        if f.endswith('.ttl'):
            os.remove(os.path.join(bundle, f))
    head = ('@prefix lv2: <http://lv2plug.in/ns/lv2core#> .\n'
            '@prefix pset: <http://lv2plug.in/ns/ext/presets#> .\n'
            '@prefix rdfs: <http://www.w3.org/2000/01/rdf-schema#> .\n'
            '@prefix state: <http://lv2plug.in/ns/ext/state#> .\n\n')
    manifest = [head]
    count = 0
    for name in sorted(os.listdir(data_dir)):
        info = os.path.join(data_dir, name, 'device.json')
        if not os.path.exists(info):
            continue
        meta = json.load(open(info))
        uri = PLUGDATA_URI[meta['kind']]
        state = juce_base64(plugdata_state(os.path.join(data_dir, name, name + '.pd')))
        ttl = name + '.ttl'
        with open(os.path.join(bundle, ttl), 'w') as f:
            f.write(head + '<>\n    a pset:Preset ;\n    lv2:appliesTo <%s> ;\n    rdfs:label "%s" ;\n'
                    '    state:state [\n        <%s:StateString> "%s"\n    ] .\n'
                    % (uri, preset_label(name).replace('"', "'"), uri, state))
        manifest.append('<%s>\n    a pset:Preset ;\n    lv2:appliesTo <%s> ;\n    rdfs:seeAlso <%s> .\n\n' % (ttl, uri, ttl))
        count += 1
    with open(os.path.join(bundle, 'manifest.ttl'), 'w') as f:
        f.write(''.join(manifest))
    return bundle, count


def default_dirs():
    data = os.path.join(os.environ.get('XDG_DATA_HOME') or os.path.expanduser('~/.local/share'), 'spite9', 'm4l')
    return data, os.path.expanduser('~/.lv2')


def main(argv):
    args = argv[1:]
    if args and args[0] == '--install':
        # m4l2pd.py --install [--data DIR] [--lv2 DIR] DEVICE.amxd...
        data, lv2 = default_dirs()
        rest = []
        i = 1
        while i < len(args):
            if args[i] in ('--data', '--lv2') and i + 1 < len(args):
                if args[i] == '--data':
                    data = args[i + 1]
                else:
                    lv2 = args[i + 1]
                i += 2
            else:
                rest.append(args[i])
                i += 1
        status = 0
        for src in rest:
            try:
                out, kind, warnings = convert(src, data)
            except NotConvertible as e:
                sys.stderr.write('%s: %s\n' % (src, e))
                status = 1
                continue
            with open(os.path.join(os.path.dirname(out), 'device.json'), 'w') as f:
                json.dump({'source': os.path.abspath(src), 'kind': kind, 'warnings': warnings}, f, indent=1)
            print('%s (%s, %d warnings)' % (out, kind, len(warnings)))
        bundle, n = write_presets(data, lv2)
        print('%d presets in %s' % (n, bundle))
        return status

    if len(args) != 2:
        sys.stderr.write(__doc__)
        return 2
    try:
        out, kind, warnings = convert(args[0], args[1])
    except NotConvertible as e:
        sys.stderr.write('%s: %s\n' % (args[0], e))
        return 1
    print(out)
    print('kind:', kind)
    seen = {}
    for w in warnings:
        seen[w] = seen.get(w, 0) + 1
    for w, n in sorted(seen.items()):
        print('  ' + w + (' (x%d)' % n if n > 1 else ''))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
