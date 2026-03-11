#!/bin/bash

# GPU Frame Timing Benchmark Runner
# Runs the standalone SDL3_GPU frame timing benchmark utility.

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m'

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

BUILD_TYPE="debug"
WARMUP_FRAMES=120
MEASURE_FRAMES=300
QUAD_COUNT=2000
MODE="particle"

while [[ $# -gt 0 ]]; do
    case $1 in
        --release)
            BUILD_TYPE="release"
            shift
            ;;
        --warmup)
            WARMUP_FRAMES="$2"
            shift 2
            ;;
        --frames)
            MEASURE_FRAMES="$2"
            shift 2
            ;;
        --quads)
            QUAD_COUNT="$2"
            shift 2
            ;;
        --mode)
            MODE="$2"
            shift 2
            ;;
        --help|-h)
            echo "GPU Frame Timing Benchmark Runner"
            echo ""
            echo "Usage: $0 [options]"
            echo ""
            echo "Options:"
            echo "  --mode NAME      Workload mode: particle, primitive, sprite, ui, mixed"
            echo "  --release        Run the release benchmark binary"
            echo "  --warmup N       Warmup frames before sampling (default: 120)"
            echo "  --frames N       Measured frames (default: 300)"
            echo "  --quads N        Colored quads written per frame (default: 2000)"
            echo "  --help, -h       Show this help message"
            echo ""
            echo "Examples:"
            echo "  $0"
            echo "  $0 --frames 600 --quads 500"
            echo "  $0 --frames 600 --quads 6000"
            exit 0
            ;;
        *)
            echo -e "${RED}Unknown option: $1${NC}"
            exit 1
            ;;
    esac
done

RESULTS_DIR="${PROJECT_ROOT}/test_results/gpu"
mkdir -p "${RESULTS_DIR}"

BENCHMARK_EXECUTABLE="${PROJECT_ROOT}/bin/${BUILD_TYPE}/gpu_frame_timing_benchmark"
RESULTS_FILE="${RESULTS_DIR}/gpu_frame_timing_benchmark_${BUILD_TYPE}.txt"

echo -e "${BLUE}==================================${NC}"
echo -e "${BLUE}  GPU Frame Timing Benchmark${NC}"
echo -e "${BLUE}  Build: ${BUILD_TYPE}${NC}"
echo -e "${BLUE}==================================${NC}"
echo ""

if [[ ! -f "${BENCHMARK_EXECUTABLE}" ]]; then
    echo -e "${RED}Benchmark executable not found:${NC} ${BENCHMARK_EXECUTABLE}"
    echo -e "${YELLOW}Build it first with:${NC}"
    echo "  ninja -C build gpu_frame_timing_benchmark"
    exit 1
fi

echo -e "${CYAN}Writing results to:${NC} ${RESULTS_FILE}"
echo "GPU Frame Timing Benchmark - $(date)" > "${RESULTS_FILE}"
echo "Build: ${BUILD_TYPE}" >> "${RESULTS_FILE}"
echo "Mode: ${MODE}" >> "${RESULTS_FILE}"
echo "Warmup frames: ${WARMUP_FRAMES}" >> "${RESULTS_FILE}"
echo "Measured frames: ${MEASURE_FRAMES}" >> "${RESULTS_FILE}"
echo "Quads/frame: ${QUAD_COUNT}" >> "${RESULTS_FILE}"
echo "" >> "${RESULTS_FILE}"

CMD=(
    "${BENCHMARK_EXECUTABLE}"
    "--mode" "${MODE}"
    "--warmup" "${WARMUP_FRAMES}"
    "--frames" "${MEASURE_FRAMES}"
    "--quads" "${QUAD_COUNT}"
)

echo -e "${YELLOW}Command:${NC} ${CMD[*]}"
echo ""

(
    cd "${PROJECT_ROOT}"
    "${CMD[@]}"
) 2>&1 | tee -a "${RESULTS_FILE}"

echo ""
echo -e "${GREEN}Benchmark complete${NC}"
echo -e "${CYAN}Results saved to:${NC} ${RESULTS_FILE}"
echo -e "${YELLOW}Run this from a normal desktop session for meaningful swapchain/VSync timings.${NC}"
