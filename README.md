# ShellSpace

ドラムの**胴鳴り**、楽器キャビネット、ホールの**残響**を扱うコンボリューション・プラグイン。

- 形式: **VST3 / Windows x64・macOS universal、Audio Unit / macOS universal**
- 最新機能版: **v0.4.1**
- フレームワーク: [JUCE](https://juce.com/) 9
- Visual C++ 再頒布可能パッケージは**不要**（CRT を静的リンク済み）

**[→ ダウンロード（Releases）](../../releases/latest)**

```
IN ─┬──────────────────────────── DRY ──────────────┐
    │                                                │
    ├─→ [BODY conv] ─────→ ×Level ─┐               ├─→ ×Output → OUT
    │                              ├─→ Σ → [Wet HPF]┘
    └─→ [Predelay] → [SPACE conv] → ×Level ─┘
```

BODY と SPACE が**直列ではなく並列**なのが要点。直列だと胴鳴りがホールに突っ込まれて濁る。
Wet HPF は **WET 側だけ**に掛かるので、原音の低域を削らずに残響の溜まりだけ切れる。

## 機能

| | |
|---|---|
| **BODY** | キック / スネア / タム、ギター 4x12、ベース 8x10。**Tune** は IR を時間軸ごと伸縮 |
| **Shell Material** | Maple / Birch / Mahogany / Oak |
| **Shell Character** | Studio / Projection / Tight / Open（胴の構造による鳴り方の違い） |
| **SPACE** | 天井の高いオペラハウス型ホール。Full / ドラム用（低域を締めた版）。**True Stereo**（4ch IR）対応 |
| **IR 差し替え** | 各セクションから自分の wav を読み込める。パスはプロジェクトに保存される |
| **プリセット** | 7 種 |

Level 系は既定が **-60dB（＝切）**。挿しただけでは音が変わらない。

## インストール

### 配布パッケージ

- Windows: `ShellSpace-0.4.1-Setup.exe`、または`ShellSpace-0.4.1-Windows-x64.zip`
- macOS: VST3とAUを選べる`ShellSpace-0.4.1-macOS.pkg`、またはフォーマット別ZIP

> **v0.4.0 は使わないこと。** クラッシュ・モノラルでの素通し・IR 末尾のゴーストがある。
> バイナリは取り下げ済み。

未署名ビルドのため、OSのセキュリティ確認が表示される場合がある。

### WindowsでZIPを使う場合

1. **zip のブロックを解除してから展開する。** ネットから落とした zip は Windows が
   ブロック扱いにすることがあり、そのまま展開すると中の DLL が読み込めない。
   右クリック → プロパティ → 「許可する」があればチェック → **そのあとで展開**。

2. **`ShellSpace.vst3` フォルダをまるごとコピーする。** VST3 はバンドル形式。

   ```
   ShellSpace.vst3\          ← これをコピー（フォルダ）
     Contents\
       x86_64-win\
         ShellSpace.vst3     ← これは中身。同じ名前のファイルなので間違えやすい
   ```

   **中の DLL だけをコピーすると認識されない。**

3. コピー先:

   | コピー先 | |
   |---|---|
   | `C:\Program Files\Common Files\VST3\` | **推奨。** 規格上の正式な場所で、すべての DAW が必ず走査する（管理者権限が必要） |
   | `%LOCALAPPDATA%\Programs\Common\VST3\` | 権限不要だが、**後から追加された場所なので古い DAW は見に行かない** |

4. DAW で再スキャン。メーカー名 `Yokosuka`、プラグイン名 `ShellSpace`。

5. **出てこないときは Blocklist を確認する。** 一度読み込みに失敗したプラグインは
   Cubase がブロックリストに入れ、以後どれだけ再スキャンしても無視する。
   `スタジオ > VST プラグインマネージャー > Blocklist` から再有効化する。

### macOSでZIPを使う場合

- VST3: `/Library/Audio/Plug-Ins/VST3/`
- Audio Unit: `/Library/Audio/Plug-Ins/Components/`

配置後にDAWを再起動し、必要ならプラグインを再スキャンする。

### 診断スクリプト

配置・構造・ブロック状態を自動で調べる。何も書き換えない。

```powershell
.\check-install.ps1          # 診断のみ
.\check-install.ps1 -Fix     # Mark of the Web のブロックを解除する
```

## IR について

`IR/` は**生成スクリプト**。wav は生成物なのでリポジトリには入れていない。

```bash
py -3 IR/make_ir.py IR        # 生成
py -3 IR/verify_ir.py IR      # 実測検証（RT60・ピーク・モード・ch順）
```

生成される wav は **Cubase の REVerence にそのまま読ませられる**（4ch 版は True Stereo として扱われる）。
その場合 **ER Tail Split を 160ms** にすること。このホール IR の「上への広がり」は
118 / 131 / 146ms に置いた**天井の 1 次反射**が作っているので、Split を 100ms などにすると
天井がテール扱いになって効果が消える。

> **ホール IR は現地で実測したものではない。**
> 「馬蹄形で天井が高いホールはどう鳴るか」を設計して合成したもの。

胴の性格とキャビも**特定製品の実測IRではない**。プライ数・厚み・フープ・エッジ、
スピーカー構成といった構造が音に与える一般的な傾向を、合成IRへ翻訳したモデル。
根拠と数値は [IR/README.md](IR/README.md) に記載している。

## ビルド

必要なもの: Visual Studio 2022 Build Tools（C++ ワークロード）/ CMake 3.22+ / JUCE 8 / Python + numpy

```powershell
cd ShellSpace
.\build.ps1
```

`build.ps1` は wav が無ければ `make_ir.py` を先に走らせる。

> **パスに非 ASCII 文字があると JUCE の `juceaide` が `.rc` 生成で落ちる。**
> `build.ps1` は日本語パスを検出すると `mklink /J` で ASCII のジャンクションを作って回避する。

## 検証

```powershell
# DSP の頑健性（ブロック長・サンプルレート・チャンネル数）
.\build\ShellSpaceDspTest_artefacts\Release\ShellSpaceDspTest.exe

# VST3 として実ロードして音を通す
.\build\ShellSpaceTest_artefacts\Release\ShellSpaceTest.exe "<path to .vst3>"

# エディタを PNG に書き出す（はみ出し・要素の重なりも検査）
.\build\ShellSpaceUI_artefacts\Release\ShellSpaceUI.exe ui.png
```

外部検証には [pluginval](https://github.com/Tracktion/pluginval) を strictness 10 で使用。

### 実測値

| 項目 | 実測 |
|---|---|
| キック胴鳴りの基音 | 49.8 Hz |
| Tune +12 半音での基音 | 99.6 Hz（比 2.000） |
| Tune +12 半音での減衰 | ×0.591 |
| Mahogany / Open キック | 基音46.9 Hz、−20dB減衰166.3 ms |
| ギター 4x12 / ベース 8x10 キャビ | 両方出音し、主成分帯と特性差を確認 |
| Predelay 40ms の立ち上がり | 43.4 ms |
| ホールの残響（−20dB まで） | 410.9 ms |
| 追加レイテンシ | 0（立ち上がり 0.02ms） |
| ブロック長超過時 | 小ブロック処理と**ビット単位で一致** |
| 44.1k / 48k / 96kHz・モノラル | すべて動作 |
| pluginval strictness 10 | 6 シードすべて SUCCESS |

**未検証**: DAW（Cubase 等）での実動作、高 DPI 表示、CPU 負荷、オートメーション、長時間動作。

## フィードバック

- [不具合報告](../../issues/new?template=bug_report.yml): OS、DAW、VST3/AU、再現手順
- [音・UXの提案](../../issues/new?template=sound_feedback.yml): BODY、胴材、キット、キャビ、試聴条件

比較音源やスクリーンショットを添えると調整へ反映しやすい。

## 既知の制約

- **Tune を動かすと反映まで最大120msの遅れがある。** 値が変わるたびに IR を
  作り直すため、掃引中は再構築をまとめている。オートメーションしても重くならないが、
  瞬時には追従しない
- 挿した直後のごく短い間、IR の読み込みが終わっていない可能性がある（別スレッドで読むため）
- **DAW での実動作は未検証**（開発環境に DAW が無いため）。高DPI表示、CPU負荷、
  長時間動作も確認していない

## ライセンス

JUCE を使用しているため、本リポジトリのソースは **AGPLv3** で公開する。
JUCE 自体のライセンスは https://juce.com/get-juce/ を参照。

VST3 は Steinberg Media Technologies GmbH の商標。
