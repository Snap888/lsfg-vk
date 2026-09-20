#!/bin/bash
# SPDX-License-Identifier: MIT
# LSFG-VK — Shader compilation script
# Compiles all GLSL compute shaders to SPIR-V using glslangValidator
# and places the output in shaders/compiled/

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SHADER_DIR="${SCRIPT_DIR}/shaders"
OUT_DIR="${SHADER_DIR}/compiled"

mkdir -p "${OUT_DIR}"

# ─── Find glslangValidator ────────────────────────────────────────────────────
if command -v glslangValidator &>/dev/null; then
    GLSLANG="glslangValidator"
elif [[ -n "${ANDROID_NDK:-}" ]] && \
     [[ -f "${ANDROID_NDK}/shader-tools/linux-x86_64/glslangValidator" ]]; then
    GLSLANG="${ANDROID_NDK}/shader-tools/linux-x86_64/glslangValidator"
else
    echo "ERROR: glslangValidator not found. Install it or set ANDROID_NDK."
    echo "  Ubuntu/Debian: apt install glslang-tools"
    echo "  Or download Vulkan SDK: https://vulkan.lunarg.com/"
    exit 1
fi

echo "Using: ${GLSLANG}"
echo "Output: ${OUT_DIR}"
echo ""

# ─── Compile each shader ─────────────────────────────────────────────────────
SHADERS=(
    "optical_flow_pyramid.comp"
    "optical_flow_warp.comp"
    "optical_flow_blend.comp"
    "motion_vectors.comp"
    "ui_mask.comp"
)

for SHADER in "${SHADERS[@]}"; do
    SRC="${SHADER_DIR}/${SHADER}"
    NAME="${SHADER%.comp}"
    SPV="${OUT_DIR}/${NAME}.spv"

    echo -n "Compiling ${SHADER} ... "
    "${GLSLANG}" \
        --target-env vulkan1.1 \
        --client vulkan100 \
        -V "${SRC}" \
        -o "${SPV}"
    echo "OK → ${SPV}"

    # Validate
    if command -v spirv-val &>/dev/null; then
        spirv-val "${SPV}" && echo "  spirv-val: PASS"
    fi
done

echo ""
echo "All shaders compiled successfully."
echo ""
echo "To use pre-compiled shaders in CMake (without glslangValidator):"
echo "  cmake -DUSE_PRECOMPILED_SHADERS=ON .."
