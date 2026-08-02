#!/usr/bin/env bash
# ShellSpace をビルドして VST3 / AU を出力する（macOS・Linux 用）。
# Windows は build.ps1 のほう。
#
#   ./build.sh            … Release ビルド
#   ./build.sh --clean    … build/ を消してから
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
build="$root/build"

if [[ "${1:-}" == "--clean" ]]; then
  rm -rf "$build"
fi

# --- IR が生成済みか確認 ---------------------------------------------------
ir_dir="$root/../IR"
if [[ ! -f "$ir_dir/Shell_Kick_body.wav" ]]; then
  echo "IRが未生成なので生成します..."
  python3 "$ir_dir/make_ir.py" "$ir_dir"
fi

cmake -B "$build" -S "$root" -DCMAKE_BUILD_TYPE=Release
cmake --build "$build" --config Release --parallel

echo
echo "=== 成果物 ==="
find "$build" \( -name '*.vst3' -o -name '*.component' \) -maxdepth 6 -print 2>/dev/null || true

if [[ "$(uname)" == "Darwin" ]]; then
  cat <<'EOS'

=== macOS でのインストール先 ===
  VST3 : ~/Library/Audio/Plug-Ins/VST3/        （自分だけ）
         /Library/Audio/Plug-Ins/VST3/         （全ユーザー・要管理者）
  AU   : ~/Library/Audio/Plug-Ins/Components/

COPY_PLUGIN_AFTER_BUILD が有効なら、上のユーザー側へ自動でコピーされます。

署名していないバイナリは Gatekeeper に止められます。その場合:
  xattr -dr com.apple.quarantine "<プラグインのパス>"
EOS
fi
