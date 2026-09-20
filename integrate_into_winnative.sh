#!/bin/bash
# integrate_into_winnative.sh
# Скрипт для интеграции LSFG-VK в локальную копию WinNative
#
# Использование:
#   WINNATIVE_DIR=/path/to/WinNative ./integrate_into_winnative.sh

set -euo pipefail

LSFG_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WN_DIR="${WINNATIVE_DIR:-}"

# ─── Проверка аргументов ─────────────────────────────────────────────────────
if [[ -z "$WN_DIR" ]]; then
    echo "Использование: WINNATIVE_DIR=/path/to/WinNative $0"
    echo ""
    echo "Или клонируйте WinNative сначала:"
    echo "  git clone -b Snap888-patch-1 https://github.com/Snap888/WinNative.git"
    echo "  WINNATIVE_DIR=./WinNative $0"
    exit 1
fi

if [[ ! -f "$WN_DIR/app/build.gradle" ]]; then
    echo "ОШИБКА: $WN_DIR не выглядит как проект WinNative (нет app/build.gradle)"
    exit 1
fi

echo "=== Интеграция LSFG-VK → WinNative ==="
echo "LSFG-VK: $LSFG_DIR"
echo "WinNative: $WN_DIR"
echo ""

# ─── 1. Скомпилировать .so (если нет) ────────────────────────────────────────
SO_PATH="$LSFG_DIR/build/libVkLayer_lsfg_frame_generation.so"

if [[ ! -f "$SO_PATH" ]]; then
    echo "[1/5] .so не найден — запускаем сборку..."
    if command -v cmake &>/dev/null && [[ -n "${ANDROID_NDK:-}" ]]; then
        mkdir -p "$LSFG_DIR/build"
        cmake -S "$LSFG_DIR" -B "$LSFG_DIR/build" \
            -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK/build/cmake/android.toolchain.cmake" \
            -DANDROID_ABI=arm64-v8a \
            -DANDROID_PLATFORM=android-26 \
            -DCMAKE_BUILD_TYPE=Release
        cmake --build "$LSFG_DIR/build" --parallel "$(nproc)"
    else
        echo "  ✗ cmake/ANDROID_NDK не найдены — поместите .so вручную в:"
        echo "    $SO_PATH"
        exit 1
    fi
else
    echo "[1/5] .so уже собран: $SO_PATH"
fi

# ─── 2. Копируем .so в jniLibs ────────────────────────────────────────────────
echo "[2/5] Копируем .so в WinNative jniLibs..."
JNILIBS="$WN_DIR/app/src/main/jniLibs/arm64-v8a"
mkdir -p "$JNILIBS"
cp "$SO_PATH" "$JNILIBS/libVkLayer_lsfg_frame_generation.so"
echo "  ✓ $JNILIBS/libVkLayer_lsfg_frame_generation.so"

# ─── 3. Копируем манифест в assets ───────────────────────────────────────────
echo "[3/5] Копируем JSON манифест в assets..."
ASSETS="$WN_DIR/app/src/main/assets"
mkdir -p "$ASSETS"
cp "$LSFG_DIR/manifest/VkLayer_lsfg_frame_generation.json" \
   "$ASSETS/VkLayer_lsfg_frame_generation.json"
echo "  ✓ $ASSETS/VkLayer_lsfg_frame_generation.json"

# ─── 4. Копируем Kotlin обёртки ───────────────────────────────────────────────
echo "[4/5] Копируем Kotlin обёртки..."
KT_DST="$WN_DIR/app/src/main/kotlin/com/winlator/cmod/runtime/display/framegen"
mkdir -p "$KT_DST"

for KT_FILE in \
    "$LSFG_DIR/winnative/src/main/kotlin/com/winlator/cmod/runtime/display/framegen/LsfgVkLayer.kt" \
    "$LSFG_DIR/winnative/src/main/kotlin/com/winlator/cmod/runtime/display/framegen/FrameGenConfig.kt"
do
    if [[ -f "$KT_FILE" ]]; then
        cp "$KT_FILE" "$KT_DST/"
        echo "  ✓ $(basename $KT_FILE)"
    fi
done

# ─── 5. Проверка ──────────────────────────────────────────────────────────────
echo "[5/5] Проверка интеграции..."
echo ""
echo "Добавленные файлы:"
find "$WN_DIR/app/src/main/jniLibs" -name "*lsfg*" -o -name "*VkLayer_lsfg*" 2>/dev/null \
    | while read f; do echo "  ✓ ${f#$WN_DIR/}  ($(du -sh "$f" | cut -f1))"; done
find "$WN_DIR/app/src/main/assets" -name "*lsfg*" -o -name "*VkLayer_lsfg*" 2>/dev/null \
    | while read f; do echo "  ✓ ${f#$WN_DIR/}"; done
find "$WN_DIR/app/src/main/kotlin" -name "*Lsfg*" -o -name "*FrameGenConfig*" 2>/dev/null \
    | while read f; do echo "  ✓ ${f#$WN_DIR/}"; done

echo ""
echo "=== ✅ Интеграция завершена ==="
echo ""
echo "Следующий шаг — сборка WinNative APK:"
echo "  cd $WN_DIR"
echo "  ./gradlew assembleStandardDebug"
echo ""
echo "Для активации в WinNative добавьте в переменные окружения контейнера:"
echo "  VK_INSTANCE_LAYERS=VK_LAYER_LSFG_frame_generation"
echo "  LSFG_MULTIPLIER=2"
echo "  LSFG_STEREO_AWARE=1"
