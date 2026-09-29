#!/usr/bin/env bash
set -euo pipefail

# Navigate to script directory
cd "$(dirname "$0")"

# Target output directory (defaults to 'build', or overridden via $1)
OUT_DIR="${1:-build}"

# ANSI color codes for CLI formatting
BLUE='\033[0;34m'
GREEN='\033[0;32m'
RED='\033[0;31m'
GRAY='\033[0;90m'
BOLD='\033[1m'
NC='\033[0m'

# 1. Verify that the Emscripten compiler (em++) is available in PATH
if ! command -v em++ >/dev/null 2>&1; then
    echo -e "${RED}[ERROR]${NC} em++ compiler not found." >&2
    echo -e "        Please activate Emscripten: ${BOLD}source <path-to-emsdk>/emsdk_env.sh${NC}" >&2
    exit 1
fi

echo -e "${BLUE}::${NC} ${BOLD}Building material-color-utilities${NC} -> ${OUT_DIR}/"
start_time=$(date +%s%N)

# 2. Ensure target directory exists
mkdir -p "$OUT_DIR"

# 3. Compile C++ sources to WebAssembly ES6 module
em++ -O3 -std=c++17 -I. -I.. \
  bindings.cc \
  cam/cam.cc \
  cam/hct.cc \
  cam/hct_solver.cc \
  cam/viewing_conditions.cc \
  dislike/dislike.cc \
  quantize/celebi.cc \
  quantize/lab.cc \
  quantize/wsmeans.cc \
  quantize/wu.cc \
  score/score.cc \
  utils/utils.cc \
  -s WASM=1 \
  -s ALLOW_MEMORY_GROWTH=1 \
  -s MODULARIZE=1 \
  -s EXPORT_ES6=1 \
  -s EXPORT_NAME="createMCUModule" \
  --bind \
  -o "${OUT_DIR}/mcu.js"

# 4. Compute elapsed build time and artifact sizes
end_time=$(date +%s%N)
elapsed_ms=$(( (end_time - start_time) / 1000000 ))
elapsed_sec=$(awk "BEGIN {printf \"%.2f\", ${elapsed_ms} / 1000}")

js_size=$(ls -lh "${OUT_DIR}/mcu.js" | awk '{print $5}')
wasm_size=$(ls -lh "${OUT_DIR}/mcu.wasm" | awk '{print $5}')

# 5. Output build summary
echo -e "${GRAY}   ├── ${OUT_DIR}/mcu.js   (${js_size})${NC}"
echo -e "${GRAY}   └── ${OUT_DIR}/mcu.wasm (${wasm_size})${NC}"
echo -e "${GREEN}✓${NC} Build completed in ${elapsed_sec}s"