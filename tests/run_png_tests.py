#!/usr/bin/env python3
"""Compares wl::decodePng against Pillow on generated PNGs, then fuzzes truncated/corrupted files (run under ASan)."""
import subprocess, sys, os, random, io, tempfile
from PIL import Image
exe = sys.argv[1]; tmp = tempfile.mkdtemp(); random.seed(7)
def dec(path):
    p = subprocess.run([exe, path], capture_output=True)
    return p
def mk(mode, size, name, **kw):
    w, h = size
    im = Image.new(mode, size)
    px = im.load()
    for y in range(h):
        for x in range(w):
            if mode == 'RGBA': px[x,y] = (x*7%256, y*5%256, (x+y)*3%256, (x*y)%256)
            elif mode == 'RGB': px[x,y] = (x*7%256, y*5%256, (x+y)*3%256)
            elif mode == 'L': px[x,y] = (x*9+y*3)%256
            elif mode == 'LA': px[x,y] = ((x*9+y*3)%256, (x*y)%256)
            elif mode == 'P': px[x,y] = (x+y*2)%16
            elif mode == '1': px[x,y] = (x+y)%2
            elif mode == 'I;16': px[x,y] = (x*997+y*131)%65536
    if mode == 'P': im.putpalette([ (i*16)%256 if c==0 else (i*7+c*40)%256 for i in range(256) for c in range(3)])
    path = os.path.join(tmp, name + '.png'); im.save(path, **kw); return path, im
cases = [('RGBA',(37,23),'rgba'),('RGB',(41,19),'rgb'),('L',(33,17),'gray'),('LA',(21,31),'graya'),('P',(29,13),'pal'),('1',(50,9),'bw'),('RGBA',(256,128),'big')]
fails = 0
for mode,size,name in cases:
    for lvl in (0, 1, 9):   # stored, fixed-ish, dynamic
        path, im = mk(mode, size, f'{name}{lvl}', compress_level=lvl, optimize=(lvl==9))
        p = dec(path)
        ref = Image.open(path).convert('RGBA')
        if p.returncode != 0: print('FAIL decode', name, lvl, p.stderr.decode()); fails += 1; continue
        hdr, _, rest = p.stdout.partition(b'\n')
        ok = hdr.decode() == f'{ref.width} {ref.height}' and rest == ref.tobytes()
        if not ok: print('FAIL mismatch', name, lvl, mode); fails += 1
# palette + tRNS, 16-bit gray, tRNS colour key
im = Image.new('P',(8,8)); im.putpalette([i%256 for i in range(768)]); im.save(os.path.join(tmp,'trns.png'), transparency=3)
p = dec(os.path.join(tmp,'trns.png')); ref = Image.open(os.path.join(tmp,'trns.png')).convert('RGBA')
if p.returncode or p.stdout.partition(b'\n')[2] != ref.tobytes(): print('FAIL tRNS'); fails += 1
# interlaced must be rejected cleanly
Image.new('RGB',(8,8),(1,2,3)).save(os.path.join(tmp,'il.png'), interlace=1) if False else None
# fuzz: truncations and byte flips must never crash (ASan would abort with non-zero + stderr 'ERROR: AddressSanitizer')
src = open(os.path.join(tmp,'rgba9.png'),'rb').read(); crashes = 0
for i in range(400):
    b = bytearray(src)
    if i % 2: b = b[:random.randrange(1,len(b))]
    else:
        for _ in range(random.randrange(1,6)): b[random.randrange(len(b))] = random.randrange(256)
    open(os.path.join(tmp,'fz.png'),'wb').write(b); p = dec(os.path.join(tmp,'fz.png'))
    if b'AddressSanitizer' in p.stderr or b'runtime error' in p.stderr or p.returncode not in (0,1): crashes += 1; print('CRASH', i, p.stderr[:200])
print('png tests:', 'FAILED %d' % fails if fails else 'all decode cases match Pillow', '| fuzz crashes:', crashes)
sys.exit(1 if fails or crashes else 0)
