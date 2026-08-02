# -*- coding: utf-8 -*-
"""IR generator — Cubase REVerence / 生ドラム・ドラム音源向け。

出力（48kHz / 24bit）:
  Hall_Yokosuka-type_Full_TrueStereo.wav   4ch LL,LR,RL,RR  ホール本来の低域
  Hall_Yokosuka-type_Full_Stereo.wav       2ch              同上・2ch版
  Hall_Yokosuka-type_Drum_TrueStereo.wav   4ch LL,LR,RL,RR  低域を締めたドラム用
  Hall_Yokosuka-type_Drum_Stereo.wav       2ch              同上・2ch版
  Shell_Kick_body.wav / Shell_Snare_body.wav / Shell_Tom_body.wav   2ch

重要: ホールIRに直接音は入っていない（センド/FXトラック前提）。
"""
import numpy as np, wave, os, sys

SR = 48000
OUT = sys.argv[1] if len(sys.argv) > 1 else 'IR'
os.makedirs(OUT, exist_ok=True)


def write_wav24(path, data):
    """data: (n, ch) float in [-1, 1]"""
    d = np.clip(data, -1.0, 1.0)
    ints = np.round(d * (2**23 - 1)).astype(np.int32)
    b = ints.astype('<i4').tobytes()
    arr = np.frombuffer(b, dtype=np.uint8).reshape(-1, 4)[:, :3].tobytes()
    with wave.open(path, 'wb') as w:
        w.setnchannels(data.shape[1])
        w.setsampwidth(3)
        w.setframerate(SR)
        w.writeframes(arr)


def band_masks(n, edges):
    """raised-cosine crossover masks over rfft bins, sum == 1"""
    f = np.fft.rfftfreq(n, 1 / SR)
    masks, lo = [], 0.0
    for i in range(len(edges) + 1):
        hi = edges[i] if i < len(edges) else SR / 2
        m = np.ones_like(f)
        if lo > 0:
            w = np.clip(np.log2(np.maximum(f, 1e-9) / (lo / 1.4142)) / 2.0, 0, 1)
            m *= np.sin(w * np.pi / 2) ** 2
        if i < len(edges):
            w = np.clip(np.log2(np.maximum(f, 1e-9) / (hi / 1.4142)) / 2.0, 0, 1)
            m *= np.cos(w * np.pi / 2) ** 2
        masks.append(m)
        lo = hi
    s = np.sum(masks, axis=0)
    s[s == 0] = 1
    return [m / s for m in masks]


def lowpass(x, fc, order=2):
    n = len(x)
    f = np.fft.rfftfreq(n, 1 / SR)
    return np.fft.irfft(np.fft.rfft(x) * (1.0 / (1.0 + (f / fc) ** (2 * order))), n)


def highpass(x, fc, order=1):
    n = len(x)
    f = np.fft.rfftfreq(n, 1 / SR)
    r = (f / fc) ** (2 * order)
    return np.fft.irfft(np.fft.rfft(x) * (r / (1 + r)), n)


# ================================================================== HALL
HALL_DUR = 2.9
HALL_EDGES = [90, 180, 355, 710, 1400, 2800, 5600, 11200]
# 実測T20が目標カーブ(低域2.2 / 500Hz-1k 1.9 / 8k 0.75秒)に乗るよう調整済み
HALL_RT = [1.78, 1.72, 1.62, 1.95, 1.86, 1.55, 1.02, 0.58, 0.26]
HALL_BG = [0.85, 1.00, 1.00, 0.98, 0.92, 0.80, 0.62, 0.42, 0.20]

# 初期反射: (ms, gain, pan(-1..1), lowpass Hz)  ※直接音は含めない
HALL_ER = [
    (11.5, 0.62, -0.85, 9000),   # 側壁 L（馬蹄形で客席幅が狭い＝早く返る）
    (14.2, 0.58,  0.85, 9000),   # 側壁 R
    (19.0, 0.46,  0.55, 7000),   # 1階バルコニー前面
    (23.5, 0.42, -0.45, 7000),
    (28.0, 0.36,  0.20, 6000),   # 2層目
    (34.5, 0.31, -0.25, 6000),
    (41.0, 0.27,  0.70, 5000),   # 3層目
    (48.0, 0.24, -0.65, 5000),
    (57.0, 0.20,  0.10, 4500),   # 4層目 / 側壁2次
    (66.0, 0.17, -0.15, 4500),
    (78.0, 0.15,  0.35, 4000),
    (92.0, 0.13, -0.40, 4000),
    (118.0, 0.20, 0.00, 3200),   # ★天井1次（高い天井＝ER群の後に単独で来る）
    (131.0, 0.17, -0.20, 3000),  # ★天井＋側壁
    (146.0, 0.15, 0.22, 3000),   # ★
    (168.0, 0.11, 0.00, 2600),   # 天井2次
    (205.0, 0.08, 0.30, 2200),   # 後壁
    (232.0, 0.07, -0.30, 2000),
]
ER_TAIL_SPLIT_MS = 160  # 天井1次までをERに含める推奨スプリット点


def hall_pair(src, seed, bass_mult):
    """1音源位置ぶんのステレオIRを返す。src: -1=左, 0=中央, +1=右"""
    n = int(SR * HALL_DUR)
    t = np.arange(n) / SR
    rng = np.random.default_rng(seed)

    rt, bg = list(HALL_RT), list(HALL_BG)
    for i in range(3):                       # 低域3バンドを締める（ドラム用）
        rt[i] *= bass_mult
        bg[i] *= 0.85 + 0.15 * bass_mult
    masks = band_masks(n, HALL_EDGES)

    tail = np.zeros((n, 2))
    for ch in range(2):
        N = np.fft.rfft(rng.standard_normal(n))
        acc = np.zeros(n)
        for m, r, g in zip(masks, rt, bg):
            acc += np.fft.irfft(N * m, n) * np.exp(-6.9078 * t / r) * g
        tail[:, ch] = acc
    # 低域は左右を相関させる（縦長容積の低音は定在波的にまとまる）
    lowm = band_masks(n, [140])[0]
    ref = np.fft.irfft(np.fft.rfft(tail[:, 0]) * lowm, n)
    for ch in range(2):
        cur = np.fft.irfft(np.fft.rfft(tail[:, ch]) * lowm, n)
        tail[:, ch] += (ref - cur) * 0.6
    tail *= (np.clip(t / 0.040, 0, 1) ** 1.6)[:, None]   # 拡散の立ち上がり
    tail *= 0.30

    ir = tail
    for ms, g, pan, fc in HALL_ER:
        k = src * pan          # 音源が左寄りなら左側壁の反射は早く・強く返る
        idx = int(ms * (1 - 0.08 * k) / 1000 * SR)
        g_ = g * (1 + 0.30 * k)
        imp = np.zeros(n)
        imp[idx] = 1.0
        imp = lowpass(imp, fc, order=2)
        sl = int(0.0035 * SR)  # 面の粗さぶん滲ませる
        sp = rng.standard_normal(sl) * np.exp(-np.arange(sl) / (0.0008 * SR))
        imp = np.convolve(imp, sp / np.max(np.abs(sp)) * 0.45, 'full')[:n]
        ir[:, 0] += imp * g_ * np.sqrt((1 - pan) / 2) * 1.4142
        ir[:, 1] += imp * g_ * np.sqrt((1 + pan) / 2) * 1.4142

    # 音源側の耳が近い分の優位。後期残響は拡散して差が消えるので指数で減衰させる
    if src != 0:
        side = 4.5 * src * np.exp(-t / 0.12)
        ir[:, 0] *= 10 ** (-side / 20)
        ir[:, 1] *= 10 ** (+side / 20)

    for ch in range(2):
        ir[:, ch] = highpass(ir[:, ch], 22.0, order=1)
    fade = int(0.15 * SR)
    ir[-fade:] *= np.linspace(1, 0, fade)[:, None]
    return ir


def hall_true_stereo(bass_mult, seed0):
    """REVerence規定の 4ch = LL, LR, RL, RR"""
    l = hall_pair(-1, seed0, bass_mult)         # 左音源 -> (LL, LR)
    r = hall_pair(+1, seed0 + 777, bass_mult)   # 右音源 -> (RL, RR)
    return np.stack([l[:, 0], l[:, 1], r[:, 0], r[:, 1]], axis=1)


# ============================================================ DRUM SHELLS
def modal(n, modes, seed):
    """modes: [(freq, amp, decay_s, glide_ratio)]"""
    t = np.arange(n) / SR
    rng = np.random.default_rng(seed)
    y = np.zeros(n)
    for f0, a, dec, glide in modes:
        if glide != 1.0:
            finst = f0 * (glide + (1 - glide) * np.exp(-t / (dec * 0.45)))
        else:
            finst = np.full(n, f0)
        ph = 2 * np.pi * np.cumsum(finst) / SR + rng.uniform(0, 2 * np.pi)
        y += a * np.sin(ph) * np.exp(-6.9078 * t / dec)
    return y


SHELLS = {
    # 長さ秒, モード[Hz, amp, decay(s), glide], ノイズ[中心Hz, 幅oct, amp, decay], HP(Hz)
    'kick': (1.10, [
        (52, 1.00, 0.62, 0.90), (61, 0.55, 0.48, 0.92),
        (78, 0.34, 0.34, 1.00), (96, 0.22, 0.26, 1.00),
        (132, 0.14, 0.19, 1.00), (178, 0.09, 0.13, 1.00),
        (243, 0.05, 0.09, 1.00), (330, 0.03, 0.06, 1.00),
    ], [(2600, 1.2, 0.020, 0.030)], 30.0),
    'tom': (1.30, [
        (118, 1.00, 0.90, 0.94), (139, 0.62, 0.78, 0.95),
        (176, 0.40, 0.60, 1.00), (212, 0.28, 0.48, 1.00),
        (268, 0.20, 0.36, 1.00), (355, 0.13, 0.26, 1.00),
        (470, 0.08, 0.18, 1.00), (640, 0.05, 0.12, 1.00),
        (880, 0.03, 0.08, 1.00),
    ], [(3200, 1.2, 0.025, 0.035)], 45.0),
    'snare': (0.75, [
        (186, 1.00, 0.32, 0.97), (223, 0.70, 0.28, 0.98),
        (287, 0.48, 0.22, 1.00), (334, 0.36, 0.19, 1.00),
        (412, 0.26, 0.15, 1.00), (505, 0.20, 0.12, 1.00),
        (640, 0.15, 0.10, 1.00), (830, 0.11, 0.08, 1.00),
        (1120, 0.07, 0.06, 1.00), (1480, 0.05, 0.05, 1.00),
    ], [(4200, 1.4, 0.070, 0.130), (7500, 1.0, 0.040, 0.090)], 70.0),
}


def shell(kind):
    dur, modes, noises, hp = SHELLS[kind]
    n = int(SR * dur)
    t = np.arange(n) / SR
    y = modal(n, modes, seed=abs(hash(kind)) % 10000)
    rng = np.random.default_rng(abs(hash(kind)) % 9999)
    f = np.fft.rfftfreq(n, 1 / SR)
    for fc, bw, amp, dec in noises:
        nz = np.fft.irfft(np.fft.rfft(rng.standard_normal(n)) *
                          np.exp(-0.5 * (np.log2(np.maximum(f, 1e-9) / fc) / bw) ** 2), n)
        y += nz / (np.max(np.abs(nz)) + 1e-12) * amp * np.exp(-6.9078 * t / dec)
    y = highpass(y, hp, order=2)   # ミックスで邪魔なサブを落とす
    fade = int(0.05 * SR)
    y[-fade:] *= np.linspace(1, 0, fade)
    y[:24] *= np.linspace(0, 1, 24)
    return np.stack([y, y], axis=1)   # dual mono（位相を崩さない）


# ==================================================================== run
def norm(x, peak_db=-1.0):
    return x / np.max(np.abs(x)) * (10 ** (peak_db / 20))


jobs = {
    'Hall_Yokosuka-type_Full_TrueStereo.wav': hall_true_stereo(1.00, 20260802),
    'Hall_Yokosuka-type_Full_Stereo.wav':     hall_pair(0, 20260802, 1.00),
    'Hall_Yokosuka-type_Drum_TrueStereo.wav': hall_true_stereo(0.80, 20260803),
    'Hall_Yokosuka-type_Drum_Stereo.wav':     hall_pair(0, 20260803, 0.80),
    'Shell_Kick_body.wav':  shell('kick'),
    'Shell_Snare_body.wav': shell('snare'),
    'Shell_Tom_body.wav':   shell('tom'),
}
for name, ir in jobs.items():
    write_wav24(os.path.join(OUT, name), norm(ir))
    print(f'wrote {name}  {ir.shape[1]}ch {ir.shape[0]/SR:.2f}s')
print(f'\nREVerence ER Tail Split 推奨値: {ER_TAIL_SPLIT_MS} ms')
