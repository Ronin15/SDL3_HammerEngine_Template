#!/bin/bash

# SDL3 ForgeEngine Template - Quick Memory Analysis
# Fast Valgrind memory leak detection for development workflow

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Configuration
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BIN_DIR="${PROJECT_ROOT}/bin/debug"
RESULTS_DIR="${PROJECT_ROOT}/test_results/valgrind"

# Create results directory
mkdir -p "${RESULTS_DIR}"

# Quick test executables (subset for fast analysis)
declare -A QUICK_TESTS=(
    ["buffer_util"]="buffer_utilization_tests"
    ["event_mgr"]="event_manager_tests"
    ["weather_events"]="weather_event_tests"
    ["ai_opt"]="ai_optimization_tests"
    ["thread_sys"]="thread_system_tests"
    ["particle_core"]="particle_manager_core_tests"
    ["resource_mgr"]="resource_manager_tests"
    ["world_resource"]="world_resource_manager_tests"
    ["json_reader"]="json_reader_tests"
    ["resource_factory"]="resource_factory_tests"
    ["resource_edge_case"]="resource_edge_case_tests"
    ["save_mgr"]="save_manager_tests"
    ["game_state_mgr"]="game_state_manager_tests"
    ["world_gen"]="world_generator_tests"
    ["world_mgr_evt"]="world_manager_event_integration_tests"
    ["world_mgr"]="world_manager_tests"
)

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}  Quick Memory Analysis (Valgrind)     ${NC}"
echo -e "${BLUE}========================================${NC}"
echo ""

run_quick_memcheck() {
    local test_name="$1"
    local executable="$2"
    local exe_path="${BIN_DIR}/${executable}"
    local log_file="${RESULTS_DIR}/${test_name}_quick.log"

    echo -e "${CYAN}Checking ${test_name}...${NC}"

    if [[ ! -f "${exe_path}" ]]; then
        echo -e "${RED}ERROR: ${exe_path} not found!${NC}"
        return 1
    fi

    # Quick memcheck with essential options
    timeout 60s valgrind \
        --tool=memcheck \
        --leak-check=yes \
        --show-leak-kinds=definite \
        --track-origins=no \
        --log-file="${log_file}" \
        "${exe_path}" >/dev/null 2>&1 || {
        local exit_code=$?
        if [[ $exit_code -eq 124 ]]; then
            echo -e "${YELLOW}  WARNING: Timed out after 60s${NC}"
        fi
    }

    # Quick analysis
    if [[ -f "${log_file}" ]]; then
        local def_lost=$(grep "definitely lost:" "${log_file}" | tail -1 | awk '{print $4,$5}' 2>/dev/null || echo "")
        local errors=$(grep "ERROR SUMMARY:" "${log_file}" | tail -1 | awk '{print $4}' 2>/dev/null || echo "0")
        local all_freed=$(grep "All heap blocks were freed" "${log_file}" 2>/dev/null)

        # Check if it's a clean run (all freed) or has specific leak data
        if [[ -n "${all_freed}" && "${errors}" == "0" ]]; then
            echo -e "${GREEN}  ✓ Clean - No leaks or errors${NC}"
        elif [[ "${errors}" == "0" && ( "${def_lost}" == "0 bytes" || -z "${def_lost}" ) ]]; then
            echo -e "${GREEN}  ✓ Clean - No leaks or errors${NC}"
        else
            # Handle empty def_lost gracefully
            local leak_info="${def_lost}"
            [[ -z "${leak_info}" ]] && leak_info="unknown"
            echo -e "${YELLOW}  ⚠ Issues found - Errors: ${errors}, Leaks: ${leak_info}${NC}"
        fi
    else
        echo -e "${RED}  ✗ Analysis failed${NC}"
    fi
}

# Run quick tests
echo -e "${BLUE}Running quick memory checks...${NC}"
echo ""

for test_name in "${!QUICK_TESTS[@]}"; do
    run_quick_memcheck "${test_name}" "${QUICK_TESTS[$test_name]}"
done

echo ""
echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}  Quick Memory Analysis Complete       ${NC}"
echo -e "${GREEN}========================================${NC}"
echo ""
echo -e "${CYAN}Expected Behaviors:${NC}"
echo -e "  • ThreadSystem: 1 error from intentional overflow protection test"
echo -e "  • Other components: Zero leaks indicates excellent memory management"
echo ""
echo -e "For detailed analysis, run: ${CYAN}./tests/valgrind/run_valgrind_analysis.sh${NC}"
echo -e "Log files saved to: ${CYAN}${RESULTS_DIR}${NC}"
