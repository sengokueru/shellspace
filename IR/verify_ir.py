# -*- coding: utf-8 -*-
"""生成したIRを実測で検証する。RT60(T20)・ピーク・モード・True Stereoのch順。"""
import numpy as np, wave, glob, os, sys

# 日本語を出すので標準出力をUTF-8に固定する。
# これが無いと、コンソールのコードページ次第で UnicodeEncodeError で落ちる
# (GitHub の Windows ランナーは cp1252)。
if hasattr(sys.stdout, 'reconfigure'):
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')

SR = 48000
D = sys.argv[1] if len(sys.argv) > 1 else 'IR'


def read24(p):
    with wave.open(p, 'rb') as w:
        assert w.getsampwidth() == 3, w.getsampwidth()
        ch, fr, n = w.getnchannels(), w.getframerate(), w.getnframes()
        raw = np.frombuffer(w.readframes(n), dtype=np.uint8).reshape(-1, 3)
    b = np.zeros((raw.shape[0], 4), dtype=np.uint8)
    b[:, 1:] = raw
    return b.view('<i4').ravel().astype(np.float64).reshape(-1, ch) / 2**31, fr, ch


def band(x, lo, hi):
    """時間領域の滲みを避けるため矩形でなくガウス(1oct)バンドパスで測る"""
    f = np.fft.rfftfreq(len(x), 1 / SR)
    m = np.exp(-0.5 * (np.log2(np.maximum(f, 1e-9) / np.sqrt(lo * hi)) / 0.5) ** 2)
    return np.fft.irfft(np.fft.rfft(x) * m, len(x))


def rt60(x):
    e = np.cumsum(x[::-1] ** 2)[::-1]
    db = 10 * np.log10(e / (e[0] + 1e-30) + 1e-30)
    i5, i25 = np.argmax(db <= -5), np.argmax(db <= -25)
    return float('nan') if i25 <= i5 else (i25 - i5) / SR * 3.0   # T20


BANDS = [(63, 125, '63-125', 2.9), (125, 250, '125-250', 2.9), (250, 500, '250-500', 2.9),
         (500, 1000, '500-1k', 2.6), (1000, 2000, '1k-2k', 2.2), (2000, 4000, '2k-4k', 1.6),
         (4000, 8000, '4k-8k', 1.8), (8000, 16000, '8k-16k', 1.2)]

for p in sorted(glob.glob(os.path.join(D, '*.wav'))):
    x, fr, ch = read24(p)
    mono = x.mean(axis=1)
    pk = np.max(np.abs(x))
    print('=' * 66)
    print(os.path.basename(p))
    print(f'  {fr}Hz {ch}ch 24bit  len={len(x)/fr:.3f}s  peak={20*np.log10(pk):+.2f}dBFS '
          f'clip={np.sum(np.abs(x)>=0.9999)}  DC={np.mean(mono):+.2e}')
    if 'Hall' in p:
        # 高域は24bit量子化ノイズ床に埋もれるので解析窓を実効長に絞る
        out = [f'{rt60(band(mono, lo, hi)[:int(w*SR)]):.2f}' for lo, hi, _, w in BANDS]
        print('   band :', '  '.join(f'{c:>8}' for _, _, c, _ in BANDS))
        print('   T20  :', '  '.join(f'{v:>8}' for v in out), '(秒)')
        print(f'   直接音(0-3ms)ピーク: {np.max(np.abs(mono[:int(0.003*SR)])):.5f}'
              f'   最初の反射: {np.argmax(np.abs(mono) > 0.05)/SR*1000:.1f}ms')
        if ch == 4:
            names = ['LL', 'LR', 'RL', 'RR']
            e = [10 * np.log10(np.sum(x[:int(0.16 * SR), i] ** 2) + 1e-30) for i in range(4)]
            print('   ER部(0-160ms)エネルギー dB:',
                  '  '.join(f'{n}={v:+.1f}' for n, v in zip(names, e)))
            print(f'   同側優位 LL>LR:{e[0]>e[1]}  RR>RL:{e[3]>e[2]}'
                  f'   左右対称(±1.5dB):{abs(e[0]-e[3])<1.5 and abs(e[1]-e[2])<1.5}')
    elif 'Cab_' in p:
        F = np.abs(np.fft.rfft(mono)) + 1e-30
        f = np.fft.rfftfreq(len(mono), 1 / SR)
        ref = np.interp(1000, f, F)
        points = [40, 58, 80, 85, 110, 1000, 3000, 5000, 8000]
        vals = [20 * np.log10(np.interp(hz, f, F) / ref) for hz in points]
        print('   1kHz基準:', '  '.join(f'{hz:>4}Hz={db:+5.1f}dB'
                                      for hz, db in zip(points, vals)))
        print(f'   L/R一致(mono安全): {np.allclose(x[:,0], x[:,1])}')
        if 'Marshall' in p:
            print(f'   G12T-75帯域(80Hz-5kHz): '
                  f'{vals[2] > -15 and vals[7] > -20}  8kHz減衰: {vals[8] < -20}')
        else:
            print(f'   SVT-810E低域(40Hz≈-10dB): {abs((vals[0]-vals[4]) + 10) < 4}  '
                  f'5kHzまで有効: {vals[7] > -10}')
    else:
        F = np.abs(np.fft.rfft(mono * np.hanning(len(mono))))
        f = np.fft.rfftfreq(len(mono), 1 / SR)
        peaks = []
        for i in np.argsort(F)[::-1]:
            if all(abs(f[i] - q) > 12 for q in peaks):
                peaks.append(f[i])
            if len(peaks) >= 6:
                break
        print('   主要モード(Hz):', ', '.join(f'{v:.0f}' for v in sorted(peaks)))
        print(f'   T20-RT60: {rt60(mono):.3f}s   L/R一致(mono安全): {np.allclose(x[:,0], x[:,1])}')
        sub = np.sum(band(mono, 15, 30) ** 2) / (np.sum(mono ** 2) + 1e-30)
        print(f'   20Hz付近の残留エネルギー比: {sub*100:.4f}%')
