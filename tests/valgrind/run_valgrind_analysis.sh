#!/bin/bash

# SDL3 ForgeEngine Template - Comprehensive Valgrind Analysis Suite
# This script runs various Valgrind tools to analyze memory usage, thread safety, and performance

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
PURPLE='\033[0;35m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Configuration
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BIN_DIR="${PROJECT_ROOT}/bin/debug"
RESULTS_DIR="${PROJECT_ROOT}/test_results/valgrind"
SUPPRESSIONS_FILE="${PROJECT_ROOT}/tests/valgrind/valgrind_suppressions.supp"

# Create results directory
mkdir -p "${RESULTS_DIR}"

# Test executables to analyze
declare -A TEST_EXECUTABLES=(
    ["buffer_utilization"]="buffer_utilization_tests"
    ["ai_optimization"]="ai_optimization_tests"
    ["ai_scaling"]="ai_scaling_benchmark"
    ["event_manager"]="event_manager_tests"
    ["event_types"]="event_types_tests"
    ["thread_system"]="thread_system_tests"
    ["thread_safe_ai"]="thread_safe_ai_manager_tests"
    ["thread_safe_integration"]="thread_safe_ai_integration_tests"
    ["save_manager"]="save_manager_tests"
    ["weather_events"]="weather_event_tests"
    ["ui_stress"]="ui_stress_test"
    ["resource_manager"]="resource_manager_tests"
    ["world_resource_manager"]="world_resource_manager_tests"
    ["resource_template_manager"]="resource_template_manager_tests"
    ["resource_integration"]="resource_integration_tests"
    ["resource_change_event"]="resource_change_event_tests"
    ["inventory_component"]="inventory_component_tests"
    ["resource_edge_case"]="resource_edge_case_tests"
    ["collision_system"]="collision_system_tests"
    ["pathfinding_system"]="pathfinding_system_tests"
    ["collision_pathfinding_bench"]="collision_pathfinding_benchmark"
)

# Valgrind common options
COMMON_OPTS="--verbose --track-origins=yes --show-leak-kinds=all --leak-check=full"
THREAD_OPTS="--tool=helgrind --history-level=full"
CACHE_OPTS="--tool=cachegrind --cache-sim=yes"
MEMCHECK_OPTS="--tool=memcheck ${COMMON_OPTS}"

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}  SDL3 ForgeEngine Valgrind Analysis   ${NC}"
echo -e "${BLUE}========================================${NC}"
echo ""

# Function to run a single valgrind test
run_valgrind_test() {
    local test_name="$1"
    local executable="$2"
    local tool="$3"
    local options="$4"
    local output_suffix="$5"

    local exe_path="${BIN_DIR}/${executable}"
    local output_file="${RESULTS_DIR}/${test_name}_${output_suffix}.out"
    local log_file="${RESULTS_DIR}/${test_name}_${output_suffix}.log"

    echo -e "${CYAN}Running ${tool} analysis on ${test_name}...${NC}"

    if [[ ! -f "${exe_path}" ]]; then
        echo -e "${RED}ERROR: Executable ${exe_path} not found!${NC}"
        return 1
    fi

    # Run the test with timeout to prevent hanging
    timeout 300s valgrind ${options} \
        --log-file="${log_file}" \
        "${exe_path}" > "${output_file}" 2>&1 || {
        local exit_code=$?
        if [[ $exit_code -eq 124 ]]; then
            echo -e "${YELLOW}WARNING: ${test_name} timed out after 5 minutes${NC}"
        else
            echo -e "${YELLOW}WARNING: ${test_name} exited with code ${exit_code}${NC}"
        fi
    }

    echo -e "${GREEN}✓ ${test_name} ${tool} analysis complete${NC}"
}

# Function to analyze valgrind results
analyze_results() {
    local test_name="$1"
    local tool="$2"
    local log_file="${RESULTS_DIR}/${test_name}_${tool}.log"

    if [[ ! -f "${log_file}" ]]; then
        return
    fi

    case "${tool}" in
        "memcheck")
            local leaks=$(grep -c "definitely lost\|indirectly lost\|possibly lost" "${log_file}" 2>/dev/null || echo "0")
            local errors=$(grep -c "Invalid read\|Invalid write\|Conditional jump" "${log_file}" 2>/dev/null || echo "0")
            echo -e "  Memory Leaks: ${leaks}, Memory Errors: ${errors}"
            ;;
        "helgrind")
            local race_conditions=$(grep -c "Possible data race\|lock order violated" "${log_file}" 2>/dev/null || echo "0")
            echo -e "  Thread Issues: ${race_conditions}"
            ;;
        "cachegrind")
            local l1_misses=$(grep "D1  miss rate:" "${log_file}" | awk '{print $4}' 2>/dev/null || echo "N/A")
            local ll_misses=$(grep "LL miss rate:" "${log_file}" | awk '{print $4}' 2>/dev/null || echo "N/A")
            echo -e "  L1 Data Miss Rate: ${l1_misses}, Last Level Miss Rate: ${ll_misses}"
            ;;
    esac
}

# Create suppressions file for known false positives
create_suppressions_file() {
    cat > "${SUPPRESSIONS_FILE}" << 'EOF'
{
   SDL3_Init_Leak
   Memcheck:Leak
   ...
   fun:SDL_Init*
}
{
   SDL3_CreateWindow_Leak
   Memcheck:Leak
   ...
   fun:SDL_CreateWindow*
}
{
   SDL3_Audio_Leak
   Memcheck:Leak
   ...
   fun:SDL_*Audio*
}
{
   FontCache_Leak
   Memcheck:Leak
   ...
   fun:*font*
   fun:*Font*
}
{
   GLX_Leak
   Memcheck:Leak
   ...
   obj:*/libGL.so*
}
{
   Mesa_Leak
   Memcheck:Leak
   ...
   obj:*/mesa/*
}
EOF
}

# Function to run memory leak analysis
run_memcheck_analysis() {
    echo -e "${PURPLE}=== Memory Leak Analysis (Memcheck) ===${NC}"

    for test_name in "${!TEST_EXECUTABLES[@]}"; do
        local executable="${TEST_EXECUTABLES[$test_name]}"
        run_valgrind_test "${test_name}" "${executable}" "memcheck" \
            "${MEMCHECK_OPTS} --suppressions=${SUPPRESSIONS_FILE}" "memcheck"
    done

    echo -e "${PURPLE}Memory Leak Analysis Summary:${NC}"
    for test_name in "${!TEST_EXECUTABLES[@]}"; do
        echo -e "${CYAN}${test_name}:${NC}"
        analyze_results "${test_name}" "memcheck"
    done
    echo ""
}

# Function to run thread error analysis
run_helgrind_analysis() {
    echo -e "${PURPLE}=== Thread Error Analysis (Helgrind) ===${NC}"

    # Only run helgrind on thread-related tests to save time
    local thread_tests=("thread_system" "thread_safe_ai" "thread_safe_integration" "ai_scaling" "event_manager")

    for test_name in "${thread_tests[@]}"; do
        if [[ -n "${TEST_EXECUTABLES[$test_name]}" ]]; then
            local executable="${TEST_EXECUTABLES[$test_name]}"
            run_valgrind_test "${test_name}" "${executable}" "helgrind" \
                "${THREAD_OPTS}" "helgrind"
        fi
    done

    echo -e "${PURPLE}Thread Error Analysis Summary:${NC}"
    for test_name in "${thread_tests[@]}"; do
        if [[ -n "${TEST_EXECUTABLES[$test_name]}" ]]; then
            echo -e "${CYAN}${test_name}:${NC}"
            analyze_results "${test_name}" "helgrind"
        fi
    done
    echo ""
}

# Function to run cache analysis (extended from your existing analysis)
run_cachegrind_analysis() {
    echo -e "${PURPLE}=== Cache Performance Analysis (Cachegrind) ===${NC}"

    # Run cachegrind on key performance tests
    local cache_tests=("buffer_utilization" "ai_optimization" "ai_scaling" "event_manager" "thread_system")

    for test_name in "${cache_tests[@]}"; do
        if [[ -n "${TEST_EXECUTABLES[$test_name]}" ]]; then
            local executable="${TEST_EXECUTABLES[$test_name]}"
            run_valgrind_test "${test_name}" "${executable}" "cachegrind" \
                "${CACHE_OPTS} --cachegrind-out-file=${RESULTS_DIR}/${test_name}_cachegrind.out" "cachegrind"
        fi
    done

    echo -e "${PURPLE}Cache Performance Summary:${NC}"
    for test_name in "${cache_tests[@]}"; do
        if [[ -n "${TEST_EXECUTABLES[$test_name]}" ]]; then
            echo -e "${CYAN}${test_name}:${NC}"
            analyze_results "${test_name}" "cachegrind"
        fi
    done
    echo ""
}

# Function to run DRD (Data Race Detector) analysis
run_drd_analysis() {
    echo -e "${PURPLE}=== Data Race Detection (DRD) ===${NC}"

    local thread_tests=("thread_system" "thread_safe_ai" "ai_scaling")

    for test_name in "${thread_tests[@]}"; do
        if [[ -n "${TEST_EXECUTABLES[$test_name]}" ]]; then
            local executable="${TEST_EXECUTABLES[$test_name]}"
            run_valgrind_test "${test_name}" "${executable}" "drd" \
                "--tool=drd --check-stack-var=yes" "drd"
        fi
    done

    echo -e "${PURPLE}Data Race Detection Summary:${NC}"
    for test_name in "${thread_tests[@]}"; do
        if [[ -n "${TEST_EXECUTABLES[$test_name]}" ]]; then
            local log_file="${RESULTS_DIR}/${test_name}_drd.log"
            if [[ -f "${log_file}" ]]; then
                local races=$(grep -c "Conflicting access\|data race" "${log_file}" 2>/dev/null || echo "0")
                echo -e "${CYAN}${test_name}:${NC} Data Races: ${races}"
            fi
        fi
    done
    echo ""
}

# Function to generate comprehensive report
generate_report() {
    local report_file="${RESULTS_DIR}/valgrind_analysis_report.md"

    cat > "${report_file}" << EOF
# Valgrind Analysis Report - SDL3 ForgeEngine Template

Generated on: $(date)
Analysis Duration: Comprehensive multi-tool analysis

## Executive Summary

This report provides a comprehensive analysis of the SDL3 ForgeEngine Template using multiple Valgrind tools:
- **Memcheck**: Memory leak and error detection
- **Helgrind**: Thread error detection
- **Cachegrind**: Cache performance analysis
- **DRD**: Data race detection

## Test Environment

- **System**: $(uname -a)
- **Valgrind Version**: $(valgrind --version)
- **CPU Info**: $(grep "model name" /proc/cpuinfo | head -1 | cut -d: -f2 | xargs)
- **Memory**: $(free -h | grep "Mem:" | awk '{print $2}')

## Analysis Results

### Memory Leak Analysis (Memcheck)
EOF

    # Add memcheck results
    for test_name in "${!TEST_EXECUTABLES[@]}"; do
        local log_file="${RESULTS_DIR}/${test_name}_memcheck.log"
        if [[ -f "${log_file}" ]]; then
            echo "#### ${test_name}" >> "${report_file}"
            local definitely_lost=$(grep "definitely lost:" "${log_file}" | tail -1 | awk '{print $4,$5}' 2>/dev/null || echo "0 bytes")
            local possibly_lost=$(grep "possibly lost:" "${log_file}" | tail -1 | awk '{print $4,$5}' 2>/dev/null || echo "0 bytes")
            local errors=$(grep "ERROR SUMMARY:" "${log_file}" | tail -1 | awk '{print $4}' 2>/dev/null || echo "0")

            cat >> "${report_file}" << EOF
- **Definitely Lost**: ${definitely_lost}
- **Possibly Lost**: ${possibly_lost}
- **Total Errors**: ${errors}

EOF
        fi
    done

    cat >> "${report_file}" << EOF
### Thread Safety Analysis (Helgrind)
EOF

    # Add helgrind results for thread tests
    local thread_tests=("thread_system" "thread_safe_ai" "thread_safe_integration" "ai_scaling" "event_manager")
    for test_name in "${thread_tests[@]}"; do
        local log_file="${RESULTS_DIR}/${test_name}_helgrind.log"
        if [[ -f "${log_file}" ]]; then
            echo "#### ${test_name}" >> "${report_file}"
            local races=$(grep -c "Possible data race" "${log_file}" 2>/dev/null || echo "0")
            local lock_errors=$(grep -c "lock order violated" "${log_file}" 2>/dev/null || echo "0")

            cat >> "${report_file}" << EOF
- **Possible Data Races**: ${races}
- **Lock Order Violations**: ${lock_errors}

EOF
        fi
    done

    cat >> "${report_file}" << EOF
### Cache Performance Analysis (Cachegrind)
EOF

    # Add cachegrind results
    local cache_tests=("buffer_utilization" "ai_optimization" "ai_scaling" "event_manager" "thread_system")
    for test_name in "${cache_tests[@]}"; do
        local log_file="${RESULTS_DIR}/${test_name}_cachegrind.log"
        if [[ -f "${log_file}" ]]; then
            echo "#### ${test_name}" >> "${report_file}"
            local l1_data=$(grep "D1  miss rate:" "${log_file}" | awk '{print $4}' 2>/dev/null || echo "N/A")
            local l1_inst=$(grep "I1  miss rate:" "${log_file}" | awk '{print $4}' 2>/dev/null || echo "N/A")
            local ll_rate=$(grep "LL miss rate:" "${log_file}" | awk '{print $4}' 2>/dev/null || echo "N/A")

            cat >> "${report_file}" << EOF
- **L1 Data Miss Rate**: ${l1_data}
- **L1 Instruction Miss Rate**: ${l1_inst}
- **Last Level Miss Rate**: ${ll_rate}

EOF
        fi
    done

    cat >> "${report_file}" << EOF
## Recommendations

Based on this analysis:

1. **Memory Management**: Review any reported memory leaks
2. **Thread Safety**: Address any data races or lock issues
3. **Performance**: Monitor cache performance for optimization opportunities

## Files Generated

All detailed logs and outputs are available in: \`${RESULTS_DIR}/\`

EOF

    echo -e "${GREEN}✓ Comprehensive report generated: ${report_file}${NC}"
}

# Main execution
main() {
    echo -e "${BLUE}Starting comprehensive Valgrind analysis...${NC}"
    echo -e "${BLUE}Results will be saved to: ${RESULTS_DIR}${NC}"
    echo ""

    # Create suppressions file
    create_suppressions_file

    # Check if we should run specific analyses
    case "${1:-all}" in
        "memcheck"|"memory")
            run_memcheck_analysis
            ;;
        "helgrind"|"threads")
            run_helgrind_analysis
            ;;
        "cachegrind"|"cache")
            run_cachegrind_analysis
            ;;
        "drd"|"races")
            run_drd_analysis
            ;;
        "all"|*)
            run_memcheck_analysis
            run_helgrind_analysis
            run_cachegrind_analysis
            run_drd_analysis
            ;;
    esac

    # Generate comprehensive report
    generate_report

    echo -e "${GREEN}========================================${NC}"
    echo -e "${GREEN}  Valgrind Analysis Complete!          ${NC}"
    echo -e "${GREEN}========================================${NC}"
    echo -e "${GREEN}Results saved to: ${RESULTS_DIR}${NC}"
    echo -e "${GREEN}Report available: ${RESULTS_DIR}/valgrind_analysis_report.md${NC}"
}

# Show usage if help requested
if [[ "$1" == "--help" || "$1" == "-h" ]]; then
    echo "Usage: $0 [analysis_type]"
    echo ""
    echo "Analysis types:"
    echo "  all        - Run all analyses (default)"
    echo "  memcheck   - Memory leak detection only"
    echo "  helgrind   - Thread error detection only"
    echo "  cachegrind - Cache performance analysis only"
    echo "  drd        - Data race detection only"
    echo ""
    echo "Examples:"
    echo "  $0              # Run all analyses"
    echo "  $0 memcheck     # Run only memory leak detection"
    echo "  $0 cache        # Run only cache analysis"
    exit 0
fi

# Run main function
main "$@"
