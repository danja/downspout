"""Compare the C++ engine's harmonic spectra with the Python reference renderer.

Usage (from plugins/pratt):
  g++ -std=c++17 -O2 -Iinclude tools/pratt_dump.cpp src/*.cpp -o /tmp/pratt_dump
  PRATT_SYNTH_DIR=~/github/pratt-synth PRATT_DUMP=/tmp/pratt_dump python3 tools/pratt_parity.py
Needs numpy and scipy. Last result: every case within 0.12 dB over harmonics 1-12.
"""
import os, sys, subprocess, numpy as np
sys.path.insert(0, os.path.expanduser(os.environ.get("PRATT_SYNTH_DIR", "~/github/pratt-synth")))
DUMP = os.environ.get("PRATT_DUMP", "./pratt_dump")
import pratt_midi_synth as P
from midi_file import Note
FS=44100
RAW=os.path.join(os.environ.get('TMPDIR','/tmp'),'pratt_parity.raw')
def harm(x, f0, t0, t1, n=16):
    seg = x[int(t0*FS):int(t1*FS)]; w=np.hanning(len(seg)); sp=np.abs(np.fft.rfft(seg*w)); fr=np.fft.rfftfreq(len(seg),1/FS)
    out=[]
    for k in range(1,n+1):
        m=(abs(fr-k*f0)<f0*0.15); out.append(sp[m].max() if m.any() else 0)
    out=np.array(out); return out/out[0]
cases=[("organ",2,19,60,100,1.2,(0.3,0.9)),("piano",0,0,60,100,1.2,(0.02,0.12)),("piano",0,0,60,100,1.2,(0.5,1.0)),("strings",5,40,48,90,1.2,(0.3,0.9)),("flute",8,73,72,100,1.2,(0.3,0.9)),("reed",7,65,55,100,1.2,(0.3,0.9))]
for name,preset,prog,pitch,vel,hold,(t0,t1) in cases:
    note=Note(0.0,hold,hold,pitch,vel,prog,0,0)
    wave,n,h=P.melodic_voice(note,{"bend":[(0,0)]},3.0)
    subprocess.run([DUMP,str(preset),str(pitch),str(vel),str(hold),RAW],check=True)
    eng=np.fromfile(RAW,dtype=np.float32)
    f0=440*2**((pitch-69)/12)
    a=harm(np.asarray(wave),f0,t0,t1); b=harm(eng,f0,t0,t1)
    err=np.max(abs(20*np.log10((a[:12]+1e-9)/(b[:12]+1e-9))))
    print(f"{name:8s} n={n} window {t0}-{t1}s  max harmonic diff (1-12) = {err:.2f} dB   ref h2..h4 {np.round(a[1:4],3)} eng {np.round(b[1:4],3)}")
