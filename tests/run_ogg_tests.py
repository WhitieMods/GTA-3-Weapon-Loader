#!/usr/bin/env python3
"""Verifies wl::decodeOgg against ffmpeg's libvorbis decode (max abs error < 1e-4) and fuzzes it under ASan/UBSan.
usage: run_ogg_tests.py <ogg_cli binary>        (needs ffmpeg with libvorbis + numpy; used only for generating references)"""
import subprocess, sys, os, glob, random, tempfile
import numpy as np
exe = sys.argv[1]; tmp = tempfile.mkdtemp(); random.seed(11)
def ff(*a): subprocess.run(['ffmpeg','-loglevel','error','-y',*a], check=True)
cases = {}
ff('-f','lavfi','-i','sine=frequency=440:duration=1:sample_rate=44100','-ac','1','-c:a','libvorbis','-q:a','5',tmp+'/sine.ogg'); cases['sine']=1
for q in (-1,2,6,10):
    ff('-f','lavfi','-i','anoisesrc=d=0.7:c=white:r=44100:a=0.5','-af',"aeval='val(0)*if(lt(t,0.05),1,exp(-8*(t-0.05)))|val(0)*0.6':c=stereo",'-c:a','libvorbis','-q:a',str(q),f'{tmp}/noise_q{q}.ogg')
ff('-f','lavfi','-i','sine=f=1000:d=0.5:r=22050','-ac','1','-c:a','libvorbis','-q:a','3',tmp+'/r22k.ogg')
fails = 0
for f in sorted(glob.glob(tmp+'/*.ogg')):
    raw, ref = f+'.raw', f+'.ref'
    p = subprocess.run([exe,f,raw],capture_output=True,text=True)
    if p.returncode: print('FAIL decode',os.path.basename(f),p.stderr[:200]); fails+=1; continue
    ch = int(p.stdout.split()[0]); ff('-i',f,'-f','f32le','-acodec','pcm_f32le',ref)
    a = np.fromfile(raw,dtype='<f4'); b = np.fromfile(ref,dtype='<f4'); n = min(len(a),len(b)); e = abs(a[:n]-b[:n]).max()
    print(f'{os.path.basename(f):16} maxerr={e:.2e}', 'OK' if e<1e-4 else 'MISMATCH'); fails += e>=1e-4
src = open(tmp+'/noise_q6.ogg','rb').read(); crashes = 0
for i in range(600):
    b = bytearray(src)
    if i % 3 == 0: b = b[:random.randrange(1,len(b))]
    else:
        for _ in range(random.randrange(1,8)): b[random.randrange(len(b))] = random.randrange(256)
    open(tmp+'/fz.ogg','wb').write(b); p = subprocess.run([exe,tmp+'/fz.ogg',tmp+'/fz.raw'],capture_output=True)
    if b'AddressSanitizer' in p.stderr or b'runtime error' in p.stderr or p.returncode not in (0,1): crashes += 1; print('CRASH',i,p.stderr[:300])
print('fuzz crashes:',crashes,'| mismatches/failures:',fails)
sys.exit(1 if fails or crashes else 0)
