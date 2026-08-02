# ShellSpace — BODY / CAB / SPACE コンボリューション

胴鳴りまたは楽器キャビネット（BODY）とホール（SPACE）を**並列**に走らせるコンボリューション。
[../IR](../IR) で作ったIRをプラグインに埋め込むので、単体で完結する。

- 形式: **VST3 / Windows x64・macOS universal、Audio Unit / macOS universal**
- 配布版: **v0.4.0**
- フレームワーク: JUCE（`juce::dsp::Convolution` = 分割FFT畳み込み）

## 信号の流れ

```
IN ─┬─────────────────────────── DRY ──────────────┐
    │                                              │
    ├─→ [BODY conv] ──→ ×Level ─┐                  ├─→ ×Output → OUT
    │                           ├─→ Σ → [Wet HPF] ─┘
    └─→ [Predelay] → [SPACE conv] → ×Level ─┘
```

BODYとSPACEが**直列ではなく並列**なのが要点。胴鳴りがホールに突っ込まれて濁らない。
HPFは**WETだけ**に掛かるので、原音の低域を削らずに残響の溜まりだけ切れる。

## パラメータ

| セクション | パラメータ | 範囲 | 既定 | 備考 |
|---|---|---|---|---|
| BODY | Type | Kick / Snare / Tom / Guitar 1960A 4x12 / Bass Ampeg 8x10 | Kick | 埋め込みIRを切替 |
| BODY | Shell Material | Maple / Birch / Mahogany / Oak | Birch | ドラム選択時のみ有効 |
| BODY | Kit Model | Recording / Live / Stage / Tour Custom | Recording Custom | 公開仕様を抽象化した合成キャラクター |
| BODY | Tune | -12〜+12 半音 | 0 | **IRを時間軸ごと伸縮**。実機のチューニングと同じ挙動（減衰も同比率で変わる） |
| BODY | Level | -60〜+12 dB | -60（切） | |
| SPACE | Type | Hall Full / Hall Drum | Hall Drum | Drumは低域を締めた版 |
| SPACE | Predelay | 0〜120 ms | 20 | IR自体も最初の反射が約10msにある |
| SPACE | Level | -60〜+12 dB | -60（切） | |
| 共通 | Wet HPF | 20〜400 Hz | 20 | WET側のみ |
| 共通 | Dry | -60〜+6 dB | 0 | |
| 共通 | Output | -24〜+24 dB | 0 | |

Level系は既定が **-60dB（＝無音）**。挿しただけでは音が変わらないので、
使う側から上げていく。

フェーダー下の数値は**ダブルクリックで直接入力**できる。`off` または「切」で最小値になる。

## インストールとフィードバック

- Windows: `ShellSpace-0.4.0-Setup.exe`、またはポータブルZIP内の
  `ShellSpace.vst3` をVST3フォルダへ配置。
- macOS: `ShellSpace-0.4.0-macOS.pkg`、またはVST3/AU別のZIPを展開して
  `/Library/Audio/Plug-Ins/VST3` / `/Library/Audio/Plug-Ins/Components` へ配置。
- 未署名ビルドのため、OSのセキュリティ確認が表示される場合がある。

不具合は [Bug report](https://github.com/sengokueru/shellspace/issues/new?template=bug_report.yml)、
音・操作感は [Sound and UX feedback](https://github.com/sengokueru/shellspace/issues/new?template=sound_feedback.yml)
から、OS・DAW・フォーマット・選択中のBODY/胴材/キットと共に送ってほしい。

## ドラムキット・キャビネットモデル

いずれもメーカー実測IRの複製ではない。メーカー一次情報にある材、シェル厚、エッジ、
ラグ／フープ構造、スピーカー構成と帯域を、モード減衰と最小位相周波数応答へ翻訳した合成モデル。

| Kit Model | 確認できる公式仕様 | 合成モデルでの解釈 |
|---|---|---|
| Recording Custom | 100% Birch 6ply、重量級一体ラグ、30° edge | 芯の低域、明瞭な発音、不要共振を抑えた短めの高次減衰 |
| Live Custom | Oak/Phenolic 7ply、2.3mm hoop、YESS III | 強い投射とアタック、大きな低域、比較的自由なサステイン |
| Stage Custom | Birch 6ply 7.2mm、1.5mm hoop | 短い減衰、速いアタック、タイトな分離 |
| Tour Custom | Maple 6ply 5.6mm、2.3mm inverse hoop | 暖かく明るい、開いた共鳴 |

参照した一次情報: [Recording Custom](https://usa.yamaha.com/products/musical_instruments/drums/ac_drums/drum_sets/recording_custom_2016/features.html)、
[Live Custom Hybrid Oak](https://usa.yamaha.com/products/musical_instruments/drums/ac_drums/drum_sets/live-custom-hybrid-oak/specs.html)、
[Stage Custom Birch](https://ca.yamaha.com/en/musical-instruments/drums/products/drum-sets/stage-custom-birch/specs.html)、
[Tour Custom](https://usa.yamaha.com/products/musical_instruments/drums/ac_drums/drum_sets/tour_custom2/specs.html)。

| Cab | 確認できる公式仕様 | 合成IR |
|---|---|---|
| Guitar 1960A 4x12 | Celestion G12T-75×4、80Hz–5kHz、Fs 85Hz | 85Hz付近から立ち上がる締まった低域、攻撃的中域、5kHz以降を急減衰 |
| Bass Ampeg 8x10 | 密閉2発×4室、58Hz–5kHz ±3dB、40Hz −10dB | 40Hzから使える丸い低域、110Hz付近のパンチ、速い過渡応答 |

参照した一次情報: [Marshall 1960A](https://www.marshall.com/us/en/product/1960a-4x12-angled-cabinet)、
[Celestion G12T-75](https://celestion.com/product/g12t-75/)、
[Ampeg SVT-810E](https://ampeg.com/products/classic/cabs.html)、
[Ampeg公式マニュアル](https://ampeg.com/data/6/0a020a4112e10660efb652b000/application/pdf/Owner%E2%80%99s%20Manual%20-%20English%20.pdf)。

## ビルド

必要なもの:
- Visual Studio 2022 **Build Tools**（C++ワークロード + Windows SDK）
- CMake 3.22 以上
- JUCE（既定では `C:/Users/user/JUCE`。別の場所なら `-DJUCE_PATH=...`）
- Python + numpy（IR生成用）

```powershell
.\build.ps1
```

`build.ps1` はIRが無ければ `make_ir.py` を先に走らせる。
ビルドが通ると `COPY_PLUGIN_AFTER_BUILD` で
`C:\Program Files\Common Files\VST3\ShellSpace.vst3` に自動配置される。
Cubaseはプラグインマネージャーで再スキャンが必要。

手で叩く場合:

```powershell
cmake -B build -S . -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

## 検証

`Tests/host_test.cpp` が検証ホスト。ビルドしたVST3を**実際にロードして音を通し、出力を測る**。
「ビルドが通った」「VST3として読める」と「音が出る」は別物なので、ここまでやって確認とする。

```powershell
.\build\ShellSpaceTest_artefacts\Release\ShellSpaceTest.exe `
  "$env:LOCALAPPDATA\Programs\Common\VST3\ShellSpace.vst3"
```

### 実測結果（2026-08-02 / ALL PASSED）

| 項目 | 実測 | 判定 |
|---|---|---|
| VST3として認識・インスタンス生成 | in=2 out=2 | ✅ |
| 主要パラメータ11個の登録 | 胴材・キットを含め既定値も期待どおり | ✅ |
| 胴材／キット切替 | Mahogany/Tour は Birch/Recording より低い基音・長い減衰 | ✅ |
| キャビ切替 | Guitar/Bassとも出音し、主成分帯と特性差を確認 | ✅ |
| BODY (Kick) の基音 | **49.8 Hz** | ✅ 設計値49Hzのモードと一致 |
| BODY の減衰 (-20dB) | 155.1 ms | ✅ |
| **Tune +12半音での基音** | **99.6 Hz（比 2.000）** | ✅ 理論値2.0 |
| Tune +12半音での減衰比 | **0.591** | ✅ IR全体が短くなる |
| SPACE Predelay 40ms の立ち上がり | **43.4 ms** | ✅ |
| SPACE の残響 (-20dB) | 410.9 ms | ✅ |
| 全Level -60dB で無音 | peak 0.000000 | ✅ |

**測定時の注意**: キャプチャ前に `reset()` と数秒の無音送出で内部状態を吐き出すこと。
ホールIRは2.9秒あるので、これを怠ると前のテストの残響が次のテストの頭から出てきて
「プリディレイが効いていない」という**偽のバグ**に見える。実際にこれで一度騙された。

## UI

448 × 550 px。BODY / SPACE / DRY / MASTER の4本のチャンネルストリップ。

```
ShellSpace              convolution / body + hall
┌────────────────────────────────────────────┐
│ BODY          SPACE          DRY    MASTER │
│ [Kick ▾]      [Hall Drum ▾]                │
│ [Birch ▾]                                 │
│ [Recording Custom ▾]                      │
│  TUNE         PREDELAY             WET HPF│
│   ◯              ◯                    ◯   │
│  0.00           20.0                  20  │
│  MUTE           MUTE          MUTE  BYPASS│
│   │              │              │      │  │
│ -60dB          -60dB           0dB    0dB │
└────────────────────────────────────────────┘
```

### 実ホストで触るには（Cubaseが手元に無いとき）

JUCE同梱の **AudioPluginHost** を使う。VST3 SDK の Plug-in Test Host と違い、
ダウンロードもライセンス同意も不要（JUCEに入っている）。

```powershell
# 初回だけビルド（extras は JUCE ルートから JUCE_BUILD_EXTRAS=ON で構成する。
# extras/AudioPluginHost を単体で configure すると juce_add_gui_app が無くて失敗する）
cmake -B C:\ws\aph-build -S C:\Users\user\JUCE -G "Visual Studio 17 2022" -A x64 -DJUCE_BUILD_EXTRAS=ON
cmake --build C:\ws\aph-build --config Release --target AudioPluginHost

# 起動
C:\ws\aph-build\extras\AudioPluginHost\AudioPluginHost_artefacts\Release\AudioPluginHost.exe
```

起動後: `Options > Edit the List of Available Plug-ins` → `Options > Scan for new or updated VST3 plug-ins`
→ グラフ上で右クリックして ShellSpace を追加 → ダブルクリックでエディタが開く。

VST3規格の適合テスト（conformity tests）が要る場合だけ、Steinbergの VST3 SDK に同梱の
VST3PluginTestHost を検討する。AudioPluginHost にその機能は無い。

### UIを見るには

**座標を手で組んだレイアウトを、見ずに「こうなっているはず」と言わないこと。**
実際に描画してPNGに出すツールがある。

```powershell
.\build\ShellSpaceUI_artefacts\Release\ShellSpaceUI.exe shellspace_ui.png
```

エディタのサイズ・子要素の外接矩形・はみ出し量・要素の重なりも同時に出力する。
VST3経由ではなくプラグインのソースを直接リンクしている
（VST3のエディタはネイティブ子ウィンドウなので、ホスト経由ではスナップショットが取れない）。

初回はこれで2つバグが見つかった:
- 下段のノブがウィンドウ外にはみ出していた（paint と resized で別々に座標を書いていたのが原因。
  いまは `sectionBounds()` を両方が共有する）
- **コンボボックスが空だった。** `ComboBoxAttachment` は項目を作ってくれない。
  `addItemList()` で入れてからアタッチする必要がある。
  そのためアタッチメントは `unique_ptr` にして、コンストラクタ本体で生成している。

## 設計上の割り切り

- **True Stereo（4ch IR）は未対応。** 2chのStereo版IRを埋め込んでいる。
  4chを扱うには畳み込みを4系統に増やす必要があり、v1では入れていない。
  True Stereoが要るときは REVerence に `Hall_..._TrueStereo.wav` を読ませるほうが早い。
- BODYのIRはデュアルモノなので、モノ互換で位相が崩れない。
- Tune変更時はIRを再構築して `loadImpulseResponse` に渡す。JUCE側が
  バックグラウンドで差し替えるので音は途切れないが、**オートメーションで
  高速に動かす用途は想定していない**。

## IRを差し替えたいとき

1. `../IR/make_ir.py` を編集して wav を作り直す
2. `.\build.ps1` で再ビルド（wavはバイナリ埋め込みなので再ビルドが要る）

埋め込むファイルは `CMakeLists.txt` の `IR_FILES` と3重ループで決まる。
増やす場合は `Source/PluginProcessor.cpp` の命名規則 / `kSpaceFiles` と
`createLayout()` の選択肢も揃えること。
