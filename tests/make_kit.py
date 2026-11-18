# tests/make_kit.py - the test kit: eight samples synthesized here (CC0, no recorded or third-party audio), 48 kHz
# mono float32 (kit/NN.f32), played from pool slots 1..8 by the test host. Run once; build.sh test reads them.
import math, random, struct
SR = 48000
random.seed(7)
def save(n, x):
    peak = max(abs(v) for v in x) or 1.0
    with open(f"kit/{n:02d}.f32", "wb") as f:
        f.write(struct.pack(f"<{len(x)}f", *[0.9 * v / peak for v in x]))
def env(i, a, d):
    t = i / SR
    return min(1.0, t / a) * math.exp(-t / d) if a > 0 else math.exp(-t / d)
# 1 pluck: Karplus-Strong, C3
def pluck(f0=130.81, secs=1.6):
    N = int(SR / f0); buf = [random.uniform(-1, 1) for _ in range(N)]; out = []
    for i in range(int(SR * secs)):
        v = buf[i % N]; nxt = buf[(i + 1) % N]; buf[i % N] = 0.996 * 0.5 * (v + nxt); out.append(v)
    return out
# 2 kick
def kick():
    out = []; ph = 0
    for i in range(int(SR * 0.6)):
        t = i / SR; f = 48 + 120 * math.exp(-t * 28); ph += 2 * math.pi * f / SR
        out.append(math.sin(ph) * math.exp(-t * 6) + 0.3 * random.uniform(-1, 1) * math.exp(-t * 300))
    return out
# 3 snare
def snare():
    out = []; ph = 0; lp = 0
    for i in range(int(SR * 0.35)):
        t = i / SR; ph += 2 * math.pi * 185 / SR; n = random.uniform(-1, 1); lp += 0.5 * (n - lp)
        out.append(0.5 * math.sin(ph) * math.exp(-t * 30) + 0.7 * lp * math.exp(-t * 14))
    return out
# 4 bell: two-op FM, C4
def bell(f0=261.63):
    return [math.sin(2 * math.pi * f0 * i / SR + 3.0 * math.exp(-i / SR * 2.5) * math.sin(2 * math.pi * f0 * 3.5 * i / SR))
            * env(i, 0.002, 0.9) for i in range(int(SR * 2.5))]
# 5 vox: a sawtooth through three formants ("ah"), C3
def vox(f0=130.81):
    def bp(x, fc, q):
        w = 2 * math.pi * fc / SR; al = math.sin(w) / (2 * q); b0 = al; a0 = 1 + al; a1 = -2 * math.cos(w); a2 = 1 - al
        y = []; x1 = x2 = y1 = y2 = 0
        for v in x:
            o = (b0 * v - b0 * x2 - a1 * y1 - a2 * y2) / a0; x2, x1 = x1, v; y2, y1 = y1, o; y.append(o)
        return y
    n = int(SR * 1.4); saw = []; ph = 0
    for i in range(n):
        vib = 1 + 0.006 * math.sin(2 * math.pi * 5.2 * i / SR) * min(1, i / SR / 0.4)
        ph = (ph + f0 * vib / SR) % 1.0; saw.append(2 * ph - 1)
    f = [a + b + c for a, b, c in zip(bp(saw, 700, 8), [0.5 * v for v in bp(saw, 1220, 10)], [0.25 * v for v in bp(saw, 2600, 12)])]
    return [v * min(1, i / SR / 0.08) * min(1, (n - i) / SR / 0.15) for i, v in enumerate(f)]
# 6 pad: a C minor chord of detuned saws
def pad():
    n = int(SR * 2.5); out = [0.0] * n; lp = 0.0
    for f in (130.81, 155.56, 196.0, 261.63):
        for d in (-0.004, 0.004):
            ph = random.random()
            for i in range(n):
                ph = (ph + f * (1 + d) / SR) % 1.0; out[i] += 2 * ph - 1
    y = []
    for i, v in enumerate(out):
        lp += 0.12 * (v - lp); y.append(lp * min(1, i / SR / 0.3) * min(1, (n - i) / SR / 0.5))
    return y
# 7 bass: a filtered saw, C2
def bass(f0=65.41):
    out = []; ph = 0; s1 = s2 = 0
    for i in range(int(SR * 1.0)):
        t = i / SR; ph = (ph + f0 / SR) % 1.0; x = 2 * ph - 1
        fc = 120 + 2200 * math.exp(-t * 9); g = 2 * math.sin(math.pi * fc / SR)
        s1 += g * (x - s1 - 0.6 * (s1 - s2)); s2 += g * (s1 - s2)
        out.append(s2 * env(i, 0.003, 0.5))
    return out
# 8 hat
def hat():
    out = []; hp = prev = 0
    for i in range(int(SR * 0.18)):
        n = random.uniform(-1, 1); hp = 0.6 * (hp + n - prev); prev = n
        out.append(hp * math.exp(-i / SR * 40))
    return out
for k, x in enumerate([pluck(), kick(), snare(), bell(), vox(), pad(), bass(), hat()], 1):
    save(k, x)
print("kit: 8 samples")
# 9 a sung phrase (formant synthesis, four syllables "ha - lo - mi - ya" on C3 Eb3 G3 F3, 0.8 s each, with breath
# and consonant noise): the vocal test material
def sung():
    out = []
    notes = [130.81, 155.56, 196.0, 174.61]
    vowels = [(730, 1090, 2440), (570, 840, 2410), (270, 2290, 3010), (730, 1090, 2440)]
    cons = ['h', 'l', 'm', 'y']
    syl = int(SR * 0.8); ph = 0.0
    state = [[0.0] * 4 for _ in range(3)]
    def res(k, x, fc, bw):
        r = math.exp(-math.pi * bw / SR); c = 2 * r * math.cos(2 * math.pi * fc / SR)
        s = state[k]; y = (1 - r) * x + c * s[0] - r * r * s[1]; s[1] = s[0]; s[0] = y; return y
    prev_f = vowels[0]
    for si in range(4):
        f0 = notes[si]; F = vowels[si]
        for i in range(syl):
            t = i / SR
            # formants glide from the previous vowel over 60 ms
            g = min(1.0, t / 0.06)
            fr = [a + (b - a) * g for a, b in zip(prev_f, F)]
            vib = 1 + 0.008 * math.sin(2 * math.pi * 5.5 * t) * min(1, t / 0.3)
            ph = (ph + f0 * vib / SR) % 1.0
            pulse = (1 - 2 * ph) * 0.5 + (0.6 if ph < 0.02 else 0.0)
            onset = min(1.0, t / 0.04)
            voice_amp = onset * min(1.0, (syl - i) / SR / 0.05)
            noise = random.uniform(-1, 1)
            if cons[si] == 'h': breath = math.exp(-t / 0.05) * 0.8
            elif cons[si] == 'm': voice_amp *= 0.4 + 0.6 * min(1, t / 0.12); breath = 0.0
            else: breath = math.exp(-t / 0.02) * 0.3
            x = pulse * voice_amp + noise * (breath + 0.03 * voice_amp)
            y = res(0, x, fr[0], 80) * 1.0 + res(1, x, fr[1], 100) * 0.5 + res(2, x, fr[2], 140) * 0.3
            out.append(y)
        prev_f = F
    n = len(out)
    return [v * min(1, (n - i) / SR / 0.2) for i, v in enumerate(out)]
save(9, sung())
print("kit: the sung phrase in slot 9")
