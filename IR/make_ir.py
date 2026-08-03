# -*- coding: utf-8 -*-
"""IR generator — ShellSpace / Cubase REVerence向け。

出力（48kHz / 24bit）:
  Hall_Yokosuka-type_Full_TrueStereo.wav   4ch LL,LR,RL,RR  ホール本来の低域
  Hall_Yokosuka-type_Full_Stereo.wav       2ch              同上・2ch版
  Hall_Yokosuka-type_Drum_TrueStereo.wav   4ch LL,LR,RL,RR  低域を締めたドラム用
  Hall_Yokosuka-type_Drum_Stereo.wav       2ch              同上・2ch版
  Shell_<Kind>_<Material>_<Character>.wav     2ch  胴材×胴構造の性格
  Cab_Guitar_4x12.wav                         2ch  ギターキャビ
  Cab_Bass_8x10.wav                           2ch  ベースキャビ

重要: ホールIRに直接音は入っていない（センド/FXトラック前提）。
"""
import numpy as np, wave, os, sys, zlib

# 日本語を出すので標準出力をUTF-8に固定する。
# これが無いと、コンソールのコードページ次第で UnicodeEncodeError で落ちる
# (GitHub の Windows ランナーは cp1252)。
if hasattr(sys.stdout, 'reconfigure'):
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')

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


def _fft_filter(x, gain_fn):
    """周波数領域でフィルタを掛ける。ゼロ詰めして線形畳み込みにする。

    ゼロ詰めせずに rfft/irfft すると巡回畳み込みになり、冒頭の強い立ち上がりに
    対するフィルタ応答が**バッファの末尾へ回り込む**。実際これで胴鳴りIRの
    末尾に -54dB の偽の尻尾が出ていた（畳み込むと入力の約1秒後にゴーストが付く）。
    """
    n0 = len(x)
    n = 1
    while n < n0 * 2:
        n *= 2

    padded = np.zeros(n)
    padded[:n0] = x

    f = np.fft.rfftfreq(n, 1 / SR)
    y = np.fft.irfft(np.fft.rfft(padded) * gain_fn(f), n)
    return y[:n0]


def lowpass(x, fc, order=2):
    return _fft_filter(x, lambda f: 1.0 / (1.0 + (f / fc) ** (2 * order)))


def highpass(x, fc, order=1):
    def gain(f):
        r = (f / fc) ** (2 * order)
        return r / (1 + r)
    return _fft_filter(x, gain)


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


# 胴材による傾向。基準(Maple)に対する倍率で表す。
#   f       : 全モードの周波数倍率（胴の厚み・硬さの差）
#   ampLow  / ampHigh : 基音側 / 高次側の音量倍率
#   decLow  / decHigh : 基音側 / 高次側の減衰時間倍率
#   noise   : 胴の鳴きノイズ成分の倍率
#
# メイプル … 素直で基準。低域から高域までまんべんなく出る
# バーチ   … 低域が締まりアタック帯が張り出す。減衰は短め
# マホガニー… 低域が太く高次倍音が落ちる。低域はよく伸びる
MATERIALS = {
    'Maple':    dict(f=1.00, ampLow=1.00, ampHigh=1.00, decLow=1.00, decHigh=1.00, noise=1.00),
    'Birch':    dict(f=1.03, ampLow=0.78, ampHigh=1.40, decLow=0.82, decHigh=0.95, noise=1.20),
    'Mahogany': dict(f=0.96, ampLow=1.28, ampHigh=0.62, decLow=1.18, decHigh=0.78, noise=0.85),
    'Oak':      dict(f=1.05, ampLow=0.94, ampHigh=1.32, decLow=0.94, decHigh=1.02, noise=1.28),
}

# 胴の構造（プライ数・厚み・フープ・エッジ）が音に与える傾向を、
# 基準胴のモード列へ掛ける「性格」として抽象化したもの。
# 特定製品の実測IRではなく、構造から導いた合成モデル。
KIT_CHARACTERS = {
    # 薄めのプライ + 高テンションのラグ + 鋭いエッジ:
    # 芯のある低域、明瞭な発音、不要共振が少ない。録音向けのまとまり。
    'Studio':     dict(f=0.995, ampLow=1.10, ampHigh=1.03,
                       decLow=1.03, decHigh=0.82, noise=0.76),
    # 厚いプライ + 太いフープ + 胴を締め付けない支持:
    # 投射、強いアタック、大きな低域、自由なサステイン。
    'Projection': dict(f=1.018, ampLow=1.18, ampHigh=1.22,
                       decLow=1.04, decHigh=1.08, noise=1.24),
    # 厚めの胴 + 細いフープ:
    # 短い減衰、速いアタック、タイトな分離。
    'Tight':      dict(f=1.028, ampLow=0.86, ampHigh=1.12,
                       decLow=0.78, decHigh=0.84, noise=0.92),
    # 薄い胴 + 逆巻きフープ:
    # 暖かく明るい、比較的開いた共鳴。
    'Open':       dict(f=0.990, ampLow=1.13, ampHigh=1.07,
                       decLow=1.12, decHigh=1.15, noise=0.90),
}


def apply_character(modes, character):
    """モード列へ低次→高次のキャラクター倍率を線形に掛ける。"""
    k = character
    n = max(1, len(modes) - 1)
    out = []
    for i, (f0, a, dec, glide) in enumerate(modes):
        t = i / n
        amp = a   * (k['ampLow'] + (k['ampHigh'] - k['ampLow']) * t)
        d   = dec * (k['decLow'] + (k['decHigh'] - k['decLow']) * t)
        out.append((f0 * k['f'], amp, d, glide))
    return out


def stable_seed(text):
    """Pythonのランダム化hash()に依存せず、再生成で同じIRを得る。"""
    return zlib.crc32(text.encode('ascii')) & 0xffffffff


def shell(kind, material, kit):
    dur, modes, noises, hp = SHELLS[kind]
    modes = apply_character(modes, MATERIALS[material])
    modes = apply_character(modes, KIT_CHARACTERS[kit])
    noise_mult = MATERIALS[material]['noise'] * KIT_CHARACTERS[kit]['noise']
    noises = [(fc, bw, amp * noise_mult, dec) for fc, bw, amp, dec in noises]
    n = int(SR * dur)
    t = np.arange(n) / SR
    seed = stable_seed(f'{kind}:{material}:{kit}')
    y = modal(n, modes, seed=seed)
    rng = np.random.default_rng(seed ^ 0x5a17c9e3)
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


# ========================================================== GUITAR/BASS CABS
def minimum_phase_cab(anchors, duration=0.085):
    """周波数/dBアンカーから因果的な最小位相キャビネットIRを作る。"""
    n = 1
    while n < int(SR * duration):
        n *= 2

    f = np.fft.rfftfreq(n, 1 / SR)
    af = np.array([a[0] for a in anchors], dtype=float)
    adb = np.array([a[1] for a in anchors], dtype=float)
    db = np.interp(np.log2(np.maximum(f, af[0])), np.log2(af), adb)
    log_mag = np.log(np.maximum(10 ** (db / 20.0), 1e-8))

    cep = np.fft.irfft(log_mag, n)
    min_cep = np.zeros(n)
    min_cep[0] = cep[0]
    min_cep[1:n // 2] = 2.0 * cep[1:n // 2]
    min_cep[n // 2] = cep[n // 2]
    ir = np.fft.irfft(np.exp(np.fft.rfft(min_cep)), n)

    # 末尾の循環成分を完全に落とし、左右同一で位相安全にする。
    fade_start = int(n * 0.72)
    ir[fade_start:] *= np.linspace(1.0, 0.0, n - fade_start)
    return np.stack([ir, ir], axis=1)


# ギター 4x12（斜め front-loaded / 12インチ4発）の典型:
# 締まった低域・押し出す中域・落ちる高域。Fs 85Hz前後、5kHz以上は急落。
# 特定製品の実測ではなく、この構成に共通する帯域特性をアンカー化したもの。
GUITAR_4X12 = [
    (20, -55), (40, -35), (60, -18), (80, -4), (85, 0),
    (120, 2), (250, 0), (500, -2), (900, 2), (1500, 4),
    (2200, 0), (3200, 5), (4200, -3), (5000, -10),
    (7000, -35), (12000, -60), (24000, -80),
]

# ベース 8x10（密閉2発×4室のInfinite Baffle / 10インチ8発）の典型:
# 58Hz付近から伸び、40Hzで大きく落ちる。中低域が押し、高域は素直に減衰。
# 特定製品の実測ではなく、この構成に共通する帯域特性をアンカー化したもの。
BASS_8X10 = [
    (20, -45), (30, -24), (40, -10), (58, -3), (75, 1),
    (110, 3), (180, 1), (350, -1), (700, 1), (1200, 2),
    (2200, 0), (3500, -1), (5000, -3), (6500, -18),
    (9000, -45), (24000, -80),
]


# ==================================================================== run
def norm(x, peak_db=-1.0):
    return x / np.max(np.abs(x)) * (10 ** (peak_db / 20))


jobs = {
    'Hall_Yokosuka-type_Full_TrueStereo.wav': hall_true_stereo(1.00, 20260802),
    'Hall_Yokosuka-type_Full_Stereo.wav':     hall_pair(0, 20260802, 1.00),
    'Hall_Yokosuka-type_Drum_TrueStereo.wav': hall_true_stereo(0.80, 20260803),
    'Hall_Yokosuka-type_Drum_Stereo.wav':     hall_pair(0, 20260803, 0.80),
    'Cab_Guitar_4x12.wav': minimum_phase_cab(GUITAR_4X12),
    'Cab_Bass_8x10.wav':   minimum_phase_cab(BASS_8X10),
}
for kind in SHELLS:
    for material in MATERIALS:
        for kit in KIT_CHARACTERS:
            name = f'Shell_{kind.capitalize()}_{material}_{kit}.wav'
            jobs[name] = shell(kind, material, kit)

for name, ir in jobs.items():
    write_wav24(os.path.join(OUT, name), norm(ir))
    print(f'wrote {name}  {ir.shape[1]}ch {ir.shape[0]/SR:.2f}s')
print(f'\nREVerence ER Tail Split 推奨値: {ER_TAIL_SPLIT_MS} ms')
