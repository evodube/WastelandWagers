import numpy as np, wave, sys, os
sr=44100
rng=np.random.default_rng(3)
def bandpass(x,lo,hi):
    X=np.fft.rfft(x); f=np.fft.rfftfreq(len(x),1/sr)
    X[(f<lo)|(f>hi)]=0
    return np.fft.irfft(X,len(x))
def save(name,x):
    x=x/np.max(np.abs(x))*0.8
    w=wave.open(name,'wb'); w.setnchannels(1); w.setsampwidth(2); w.setframerate(sr)
    w.writeframes((x*32767).astype('<i2').tobytes()); w.close()
def card(seed):
    r=np.random.default_rng(seed)
    n=int(sr*0.09); t=np.arange(n)/sr
    noise=r.standard_normal(n)
    # a soft slap: a short burst of mid/high noise, fast attack, quick decay, a little paper body
    env=np.minimum(1,t/0.0015)*np.exp(-t/(0.012+0.006*r.random()))
    x=bandpass(noise,900+300*r.random(),7000)*env
    body=bandpass(r.standard_normal(n),250,900)*np.exp(-t/0.02)*0.35
    return x+body
def slide(seed):
    r=np.random.default_rng(seed)
    n=int(sr*0.28); t=np.arange(n)/sr
    env=np.sin(np.pi*np.minimum(1,t/0.28))**1.5
    return bandpass(r.standard_normal(n),2500,9000)*env*0.5
def shuffle(seed):
    r=np.random.default_rng(seed)
    dur=1.1; n=int(sr*dur); x=np.zeros(n)
    # a riffle: ~45 card edges flicking, speeding up then slowing, over a soft hiss
    times=np.cumsum(r.uniform(0.012,0.03,60)); times=times/times[-1]*0.85+0.05
    for tt in times:
        i=int(tt*sr); k=int(sr*0.012)
        if i+k>=n: continue
        tk=np.arange(k)/sr
        x[i:i+k]+=bandpass(r.standard_normal(k),1500,8000)*np.exp(-tk/0.003)*r.uniform(0.5,1.0)
    tt=np.arange(n)/sr
    x+=bandpass(r.standard_normal(n),2000,7000)*0.06*np.sin(np.pi*tt/dur)
    # the bridge: a soft zip at the end
    k=int(sr*0.15); i=n-k-1000; tk=np.arange(k)/sr
    x[i:i+k]+=bandpass(r.standard_normal(k),1200,6000)*np.exp(-tk/0.05)*0.6
    return x
out=sys.argv[1]
for i in range(1,6): save(os.path.join(out,f'card_{i}.wav'),card(i))
for i in range(1,3): save(os.path.join(out,f'slide_{i}.wav'),slide(10+i))
for i in range(1,3): save(os.path.join(out,f'shuffle_{i}.wav'),shuffle(20+i))
print('ok')
