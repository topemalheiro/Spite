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
    """metro/qmetro/phasor~ with note values or @attributes -> plain Pd"""
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


class Converter:
    def __init__(self, outdir, abstractions):
        self.outdir = outdir
        self.abstractions = abstractions  # name -> patcher JSON, converted on demand
        self.done = set()
        self.warnings = []
        self.boxes = 0
        self.taps = 0

    def warn(self, msg):
        self.warnings.append(msg)

    def convert_abstraction(self, name):
        if name in self.done or name not in self.abstractions:
            return
        self.done.add(name)
        lines = ['#N canvas 0 50 900 600 12;']
        lines += self.patcher(self.abstractions[name]['patcher'])
        with open(os.path.join(self.outdir, name + '.pd'), 'w') as f:
            f.write('\n'.join(lines) + '\n')

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

    def patcher(self, p):
        """-> list of Pd record lines for the boxes and lines of one patcher"""
        boxes = [b['box'] for b in p.get('boxes', [])]
        self.delay_lines(boxes, p.get('lines', []))
        index = {b['id']: i for i, b in enumerate(boxes)}
        outlets = {}
        lines = []
        for b in boxes:
            self.boxes += 1
            text, n_out = self.box(b)
            lines.extend(text)
            if n_out is not None:
                outlets[b['id']] = n_out
        # Max sends a fan-out right to left (then bottom to top); Pd sends
        # in the order connections were made: write them in Max's order
        conns = []
        for l in p.get('lines', []):
            pl = l.get('patchline', l)
            src, so = pl['source'][0], pl['source'][1]
            dst, di = pl['destination'][0], pl['destination'][1]
            if src not in index or dst not in index or pl.get('_drop'):
                continue
            if so >= outlets.get(src, 1 << 30):
                self.warn('dropped a connection from outlet %d of %s' % (so, self.label(boxes[index[src]])))
                continue
            rect = boxes[index[dst]].get('patching_rect', [0, 0, 0, 0])
            conns.append((index[src], so, -rect[0], -rect[1], index[dst], di))
        conns.sort()
        for s, so, _, _, d, di in conns:
            lines.append('#X connect %d %d %d %d;' % (s, so, d, di))
        return lines

    @staticmethod
    def label(b):
        return b.get('text') or b.get('maxclass', '?')

    def box(self, b):
        """-> ([record lines], Pd outlet count or None if unchanged)"""
        cls = b.get('maxclass', 'newobj')
        x, y = (int(v) for v in b.get('patching_rect', [0, 0])[:2])
        xy = '%d %d' % (x, y)
        text = b.get('text', '') or ''

        if cls == 'newobj':
            words = text.split(' ', 1)
            name, args = words[0], (words[1] if len(words) > 1 else '')
            if name in ('p', 'patcher') and 'patcher' in b:
                sub = pd_escape(args) or 'sub'
                return (['#N canvas 0 50 700 500 %s 0;' % sub.replace(' ', '_')] + self.patcher(b['patcher'])
                        + ['#X restore %s pd %s;' % (xy, sub)], None)
            if name == 'tapin~' and '_delay' in b:
                size = (args.split() or ['1000'])[0]
                return (['#X obj %s delwrite~ \\$0-%s %s;' % (xy, b['_delay'], size)], 0)
            if name == 'tapout~' and '_delay' in b:
                taps = args.split() or ['0']
                objs, conns = [], []
                for i, t in enumerate(taps):
                    objs.append('#X obj %d 20 inlet;' % (20 + 100 * i))
                    objs.append('#X obj %d 60 delread~ \\$0-%s %s;' % (20 + 100 * i, b['_delay'], t))
                    objs.append('#X obj %d 100 outlet~;' % (20 + 100 * i))
                    conns += [(3 * i, 0, 3 * i + 1, 0), (3 * i + 1, 0, 3 * i + 2, 0)]
                return (subpatch(xy, 'tapout~', objs, conns), None)
            if name == 'poly~':
                self.warn('poly~ is not converted yet: ' + text)
            if name in ('metro', 'qmetro', 'phasor~') and re.search(r'@|\d+n[dt]?\b|ticks', args):
                return (['#X obj %s %s;' % (xy, pd_escape(timed(name, args, self.warn)))], None)
            if name == 'zmap':
                expr = zmap_expr(args)
                if expr:
                    return (['#X obj %s %s;' % (xy, expr.replace('$', '\\$'))], None)
            if name == 'ctlout' and args.split() and not re.match(r'^[-\d$#]', args.split()[0]):
                args = ' '.join(args.split()[1:])  # a MIDI port name, plugdata has one port
            if name == 'pattr':
                args = ''
            if '@' in args and name not in ('poly~', 'js', 'v8', 'node.script', 'gen~'):
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
                    return (['#X obj %s %s;' % (xy, expr.replace('$', '\\$'))], None)
                self.warn('kept [if %s]' % args)
            if name in self.abstractions:
                self.convert_abstraction(name)
            name = RENAME.get(name, name)
            return (['#X obj %s %s;' % (xy, pd_escape((name + ' ' + args).strip()))], PD_OUTLETS.get(words[0]))

        if cls == 'message':
            return (['#X msg %s %s;' % (xy, pd_escape(text))], None)
        if cls in ('comment', 'live.comment'):
            return (['#X text %s %s;' % (xy, pd_escape(text) or '.')], None)
        if cls == 'inlet':
            sig = 'signal' in (b.get('outlettype') or [''])
            return (['#X obj %s %s;' % (xy, 'inlet~' if sig else 'inlet')], None)
        if cls == 'outlet':
            return (['#X obj %s %s;' % (xy, 'outlet~' if b.get('_signal') else 'outlet')], None)
        if cls in ('number', 'flonum', 'live.numbox'):
            return (['#X floatatom %s 5 0 0 0 - - - 0;' % xy], 1)
        if cls in ('toggle', 'live.toggle') or (cls == 'live.text' and b.get('mode', 1) == 1):
            return (['#X obj %s tgl 15 0 empty empty empty 17 7 0 10 #fcfcfc #000000 #000000 0 1;' % xy], 1)
        if cls in ('button', 'live.button', 'live.text', 'textbutton', 'ubutton'):
            return (['#X obj %s bng 15 250 50 0 empty empty empty 17 7 0 10 #fcfcfc #000000 #000000;' % xy], 1)
        if cls in ('dial', 'live.dial'):
            lo, hi, init, name = param_info(b) if cls == 'live.dial' else (0, 127, 0, '')
            return (['#X obj %s else/knob 40 %s %s 0 %s empty empty;' % (xy, lo, hi, init)], 1)
        if cls in ('live.gain~', 'gain~'):
            return (gain_sub(xy, b), None)
        if cls == 'gswitch2':
            return (gswitch2_sub(xy), None)
        if cls == 'gswitch':
            return (gswitch_sub(xy, max(2, b.get('numinlets', 3) - 1)), None)
        if cls in ('live.tab', 'live.menu', 'umenu'):
            return (menu_sub(xy, b), None)
        if cls in ('slider', 'live.slider'):
            lo, hi, init, name = param_info(b) if cls == 'live.slider' else (0, 127, 0, '')
            w, h = b.get('patching_rect', [0, 0, 20, 140])[2:4]
            if w > h:
                return (['#X obj %s hsl 128 15 %s %s 0 0 empty empty empty -2 -8 0 10 #fcfcfc #000000 #000000 0 1;' % (xy, lo, hi)], 1)
            return (['#X obj %s vsl 15 128 %s %s 0 0 empty empty empty 0 -9 0 10 #fcfcfc #000000 #000000 0 1;' % (xy, lo, hi)], 1)
        if cls == 'radiogroup':
            items = b.get('saved_attribute_attributes', {}).get('valueof', {}).get('parameter_enum') or b.get('items') or []
            n = max(2, len([i for i in items if i != ',']) if isinstance(items, list) else 2)
            return (['#X obj %s hradio 15 1 0 %d empty empty empty 0 -8 0 10 #fcfcfc #000000 #000000 0;' % (xy, n)], 1)
        if cls == 'panel':
            w, h = (int(v) for v in b.get('patching_rect', [0, 0, 100, 60])[2:4])
            return (['#X obj %s cnv 15 %d %d empty empty empty 20 12 0 14 #e0e0e0 #404040 0;' % (xy, max(w, 1), max(h, 1))], 0)
        if cls in ('live.line', 'fpic', 'live.colors', 'swatch'):
            return (['#X text %s (%s);' % (xy, cls)], None)
        if cls == 'bpatcher':
            if 'patcher' in b:
                return (['#N canvas 0 50 700 500 bpatcher 0;'] + self.patcher(b['patcher'])
                        + ['#X restore %s pd bpatcher;' % xy], None)
            name = os.path.splitext(b.get('name', 'bpatcher'))[0]
            args = ' '.join(str(a) for a in b.get('args', []))
            if name in self.abstractions:
                self.convert_abstraction(name)
            else:
                self.warn('bpatcher file %s is not in the device' % b.get('name'))
            return (['#X obj %s %s;' % (xy, pd_escape((name + ' ' + args).strip()))], None)

        # anything else stays, under its Max name, as a broken object with its connections
        self.warn('not converted: ' + cls)
        return (['#X obj %s %s;' % (xy, pd_escape(cls))], None)


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
    lines = ['#N canvas 0 50 1000 700 12;'] + conv.patcher(main['patcher'])
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
    value = int.from_bytes(data, 'little')
    chars = (len(data) * 8 + 5) // 6
    return '%d.%s' % (len(data), ''.join(JUCE_B64[(value >> (6 * i)) & 63] for i in range(chars)))


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
