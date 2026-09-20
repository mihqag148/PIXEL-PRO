#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
QMK_SPEC="${QMK_SPEC:-42e50c9f96cb1bc57d59c9ebf4f52df54a5f045c}"
TINYUSB_SPEC="${TINYUSB_SPEC:-1a89cb79bd833aac67afec1974fc644369d019d5}"
WORK="$ROOT/.qmk-real-build"
QMK="$WORK/qmk"
IDF_PROJ="$ROOT/firmware/qmk-idf"

rm -rf "$WORK"
mkdir -p "$WORK"

echo "==> Fetch real QMK ESP32-S2 fork @ $QMK_SPEC"
git init -q "$QMK"
git -C "$QMK" remote add origin https://github.com/morganvenable/lalboard-qmk-clone.git
git -C "$QMK" fetch -q --depth 1 origin "$QMK_SPEC"
git -C "$QMK" checkout -q FETCH_HEAD

echo "==> Fetch QMK compatibility submodules"
git -C "$QMK" submodule sync -- lib/lufa lib/printf
git -C "$QMK" submodule update --init --depth 1 lib/lufa lib/printf

echo "==> Fetch compatible Espressif TinyUSB @ $TINYUSB_SPEC"
rm -rf "$QMK/lib/tinyusb"
git init -q "$QMK/lib/tinyusb"
git -C "$QMK/lib/tinyusb" remote add origin https://github.com/morganvenable/lalboard-tinyusb-clone.git
git -C "$QMK/lib/tinyusb" fetch -q --depth 1 origin "$TINYUSB_SPEC"
git -C "$QMK/lib/tinyusb" checkout -q FETCH_HEAD

rm -rf "$QMK/keyboards/pixel_pro"
cp -R "$ROOT/firmware/qmk/keyboard" "$QMK/keyboards/pixel_pro"
python3 "$ROOT/firmware/qmk/tools/patch_qmk.py" "$QMK"

python3 -m pip install -q -r "$QMK/requirements.txt"
# This 2021-era QMK CLI uses MILCInterface._entrypoint, which was removed
# from newer MILC releases.
python3 -m pip install -q "milc==1.6.0"

echo "==> Generate ESP-IDF include paths"
cd "$IDF_PROJ"
rm -rf build-probe build sdkconfig sdkconfig.old
idf.py set-target esp32s2
idf.py -B build-probe reconfigure -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
python3 "$ROOT/firmware/qmk/tools/extract_idf_includes.py" \
    "$IDF_PROJ/build-probe/compile_commands.json" > "$WORK/idf-includes.txt"

echo "==> Build QMK core + VIA as an ESP32-S2 static archive"
cd "$QMK"
export CFLAGS="$(cat "$WORK/idf-includes.txt")"
export CXXFLAGS="$CFLAGS"
make pixel_pro:via ALLOW_WARNINGS=yes VERBOSE=false

QMK_ARCHIVE="$(find "$QMK" -maxdepth 1 -type f -name 'pixel_pro_via.a' -print -quit)"
if [[ -z "$QMK_ARCHIVE" ]]; then
    QMK_ARCHIVE="$(find "$QMK/.build" -type f -name '*.a' -print -quit || true)"
fi
if [[ -z "$QMK_ARCHIVE" ]]; then
    echo "QMK archive was not produced" >&2
    find "$QMK" -maxdepth 3 -type f -name '*.a' -print >&2 || true
    exit 1
fi

cp "$QMK_ARCHIVE" "$IDF_PROJ/main/qmk_core.a"

echo "==> Link QMK archive into an ESP-IDF flash image"
cd "$IDF_PROJ"
rm -rf build
idf.py -B build build
idf.py -B build merge-bin -o PIXEL_PRO_QMK_merged.bin -f raw

mkdir -p "$ROOT/dist/qmk"
cp "$IDF_PROJ/build/pixel_pro_qmk.bin" "$ROOT/dist/qmk/PIXEL_PRO_QMK_app.bin"
cp "$IDF_PROJ/build/PIXEL_PRO_QMK_merged.bin" "$ROOT/dist/qmk/PIXEL_PRO_QMK_merged.bin"
cp "$QMK_ARCHIVE" "$ROOT/dist/qmk/PIXEL_PRO_QMK_core.a"

cat > "$ROOT/dist/qmk/build-info.txt" <<EOF
PIXEL PRO real QMK proof build
QMK fork: morganvenable/lalboard-qmk-clone@$QMK_SPEC
TinyUSB: espressif/tinyusb@$TINYUSB_SPEC
Target: ESP32-S2
VIA: real quantum/via.c
Matrix: 2x4 keys + EC11 push as virtual row
Encoder: GPIO37/GPIO38
VID:PID: 303A:4009
EOF

echo "==> Real QMK firmware artifacts:"
ls -lh "$ROOT/dist/qmk"
