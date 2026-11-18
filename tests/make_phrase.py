# tests/make_phrase.py - a sung phrase for the vocal-chop tests, synthesized here (CC0: a source-filter voice of our
# own, no recorded audio): "oh yeah, we go so high tonight" over two bars at 120 BPM, 48 kHz mono float32 in
# kit/10.f32, and the syllables' start times in kit/10.txt (seconds), which the test host locks STRT to.
import math, random, struct
SR = 48000
random.seed(11)

# phonemes: formants (F1..F4 Hz), bandwidths, voicing 0..1, noise (amount, centre Hz), nasal
V = {
    'o': ((450, 800, 2830, 3500), 1.0), 'ae': ((660, 1720, 2410, 3400), 1.0), 'i': ((280, 2250, 2890, 3600), 1.0),
    'u': ((310, 870, 2250, 3400), 1.0), 'a': ((750, 1220, 2600, 3500), 1.0), 'ai': ((700, 1200, 2600, 3500), 1.0),
    'e': ((530, 1840, 2480, 3500), 1.0), 'ou': ((480, 900, 2600, 3400), 1.0), 'schwa': ((500, 1500, 2500, 3500), 1.0),
}
BW = (70, 90, 140, 200)

# the phrase: (consonant, vowel, offglide vowel or None, note, start beat, beats)
# beats at 120 BPM: 0.5 s
PH = [
    (None, 'o', 'u', 62, 0.0, 1.0),     # oh
    ('y', 'ae', 'schwa', 65, 1.0, 1.0),  # yeah
    ('w', 'i', None, 67, 2.0, 0.5),     # we
    ('g', 'o', 'u', 69, 2.5, 1.0),      # go
    ('s', 'o', 'u', 67, 3.5, 0.5),      # so
    ('h', 'a', 'i', 72, 4.0, 1.5),      # high
    ('t', 'u', None, 70, 5.5, 0.5),     # to-
    ('n', 'a', 'i', 69, 6.0, 1.75),     # -night (t at the end)
]
BEAT = 0.5
total = int(SR * (8 * BEAT + 0.6))

def mtof(m): return 440.0 * 2 ** ((m - 69) / 12)

# control tracks per sample
f0 = [0.0] * total; amp = [0.0] * total; noise = [0.0] * total; ncent = [5000.0] * total
F = [[500.0] * total for _ in range(4)]
starts = []
for idx, (c, v, off, note, b, nb) in enumerate(PH):
    t0 = b * BEAT; t1 = (b + nb) * BEAT - 0.04
    i0, i1 = int(t0 * SR), int(t1 * SR)
    starts.append(t0)
    prev_note = PH[idx - 1][3] if idx else note
    vf = V[v][0]; of = V[off][0] if off else vf
    cons_len = {'y': 0.07, 'w': 0.07, 'g': 0.05, 's': 0.11, 'h': 0.08, 't': 0.05, 'n': 0.07, None: 0.0}[c]
    ic = int(cons_len * SR)
    for i in range(i0, min(i1, total)):
        t = (i - i0) / SR; dur = (i1 - i0) / SR
        # pitch: a glide from the last note over 50 ms, a little scoop, vibrato after 250 ms
        g = min(1.0, t / 0.05)
        m = prev_note + (note - prev_note) * (g * g * (3 - 2 * g)) - 0.3 * math.exp(-t / 0.04)
        vib = 0.35 * math.sin(2 * math.pi * 5.3 * t) * min(1.0, max(0.0, (t - 0.25) / 0.2))
        f0[i] = mtof(m + vib) * (1 + 0.003 * random.uniform(-1, 1))
        # formants: consonant -> vowel -> offglide
        k = (i - i0)
        if c == 'y': cf = (300, 2200, 3000, 3600)
        elif c == 'w': cf = (300, 650, 2200, 3300)
        elif c == 'n': cf = (280, 1500, 2600, 3400)
        elif c == 'g': cf = (350, 1800, 2400, 3400)
        else: cf = vf
        if k < ic:
            fm = cf
        else:
            u = (k - ic) / max(1, (i1 - i0 - ic)); ga = min(1.0, (k - ic) / (0.06 * SR))
            w = max(0.0, (u - 0.55) / 0.45)   # the offglide in the last 45 %
            fm = tuple(a + (bb - a) * ga for a, bb in zip(cf, vf))
            fm = tuple(a + (bb - a) * w for a, bb in zip(fm, of))
        for j in range(4): F[j][i] = fm[j]
        # amplitude: voiced onset, release; consonants
        rel = min(1.0, (i1 - i) / (0.06 * SR))
        a = min(1.0, t / 0.03) * rel
        nz = 0.0; nc = 5000.0
        if c == 's' and k < ic: a *= 0.0; nz = 0.35 * math.sin(math.pi * k / ic); nc = 6500
        elif c == 'h' and k < ic: a *= 0.15; nz = 0.4 * (1 - k / ic); nc = 1500
        elif c == 't' and k < ic: a = 0.0; nz = 0.5 * math.exp(-k / (0.01 * SR)) if k > 0.02 * SR else 0.0; nc = 4500
        elif c == 'g' and k < ic: a = 0.2 if k < 0.03 * SR else 0.5; nz = 0.3 * math.exp(-max(0, k - 0.03 * SR) / (0.008 * SR)) if k > 0.03 * SR else 0.0; nc = 2200
        elif c == 'n' and k < ic: a *= 0.45
        amp[i] = a * (0.85 + 0.15 * math.sin(2 * math.pi * 0.7 * t))
        noise[i] = nz + 0.025 * a   # breath under the voice
        ncent[i] = nc
    # the final t of "night"
    if idx == len(PH) - 1:
        it = min(total - 1, i1 + int(0.03 * SR))
        for i in range(it, min(total, it + int(0.05 * SR))):
            noise[i] = 0.45 * math.exp(-(i - it) / (0.012 * SR)); ncent[i] = 4500; F[0][i] = 400

# synthesis: an LF-like glottal pulse train + noise, through four cascaded resonators; a nasal murmur on n
out = []
ph = 0.0
st = [[0.0, 0.0] for _ in range(4)]
nst = [0.0, 0.0]
def reso(s, x, fc, bw):
    r = math.exp(-math.pi * bw / SR); c = 2 * r * math.cos(2 * math.pi * fc / SR)
    y = (1 - c + r * r) * x + c * s[0] - r * r * s[1]; s[1] = s[0]; s[0] = y; return y
lp = 0.0
for i in range(total):
    x = 0.0
    if f0[i] > 0 and amp[i] > 0:
        ph += f0[i] / SR
        if ph >= 1.0: ph -= 1.0
        # glottal flow derivative: open 60 %, a sharp closure
        oq = 0.6
        g = math.sin(math.pi * ph / oq) ** 2 if ph < oq else 0.0
        dg = (g - lp); lp = g
        x = dg * 8.0 * amp[i]
    n = random.uniform(-1, 1)
    # the noise shaped around its centre
    nb = reso(nst, n, ncent[i], ncent[i] * 0.6) * noise[i] * 3.0
    y = x + nb * 0.5 + n * noise[i] * 0.08
    for j in range(4):
        y = reso(st[j], y, F[j][i], BW[j] * (1.6 if j == 0 and amp[i] < 0.5 else 1.0))
    out.append(y + nb * 0.6)
# even out the syllables (as a singer would): each to about the same loudness, crossfaded over 20 ms
gains = []
for idx, (c, v, off, note, b, nb) in enumerate(PH):
    i0 = int((b * BEAT + 0.08) * SR); i1 = int(((b + nb) * BEAT - 0.06) * SR)
    rms = math.sqrt(sum(out[i] ** 2 for i in range(i0, i1)) / max(1, i1 - i0))
    gains.append(min(6.0, 0.25 / max(rms, 1e-6)))
gain_at = [gains[0]] * total
for idx, (c, v, off, note, b, nb) in enumerate(PH):
    i0 = int(b * BEAT * SR)
    for i in range(i0, total):
        u = min(1.0, (i - i0) / (0.02 * SR))
        gain_at[i] = (gains[idx - 1] if idx else gains[0]) * (1 - u) + gains[idx] * u
out = [v * g for v, g in zip(out, gain_at)]
peak = max(abs(v) for v in out)
out = [0.85 * v / peak for v in out]
with open("kit/10.f32", "wb") as f:
    f.write(struct.pack(f"<{len(out)}f", *out))
with open("kit/10.txt", "w") as f:
    f.write(" ".join(f"{t:.3f}" for t in starts) + f"\n{len(out) / SR:.4f}\n")
print("phrase", len(out) / SR, "s; syllables at", starts)
