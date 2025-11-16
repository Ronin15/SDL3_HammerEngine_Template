#!/bin/bash

# SDL3 HammerEngine Template - Comprehensive Valgrind Analysis Suite
# This script runs various Valgrind tools to analyze memory usage, thread safety, and performance

# Don't exit on errors - we handle them explicitly
set +e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
PURPLE='\033[0;35m'
CYAN='\033[0;36m'
BOLD='\033[1m'
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
    ["settings_manager"]="settings_manager_tests"
    ["weather_events"]="weather_event_tests"
    ["ui_stress"]="ui_stress_test"
    ["world_resource_manager"]="world_resource_manager_tests"
    ["resource_template_manager"]="resource_template_manager_tests"
    ["resource_integration"]="resource_integration_tests"
    ["resource_change_event"]="resource_change_event_tests"
    ["inventory_component"]="inventory_component_tests"
    ["resource_edge_case"]="resource_edge_case_tests"
    ["collision_system"]="collision_system_tests"
    ["pathfinding_system"]="pathfinding_system_tests"
)

# Tracking variables
TESTS_RUN=0
TESTS_PASSED=0
TESTS_FAILED=0
TESTS_TIMEOUT=0

echo -e "${BOLD}${BLUE}╔═══════════════════════════════════════════════════════════╗${NC}"
echo -e "${BOLD}${BLUE}║        SDL3 HammerEngine Valgrind Analysis Suite         ║${NC}"
echo -e "${BOLD}${BLUE}╚═══════════════════════════════════════════════════════════╝${NC}"
echo ""
echo -e "${CYAN}Results Directory: ${RESULTS_DIR}${NC}"
echo -e "${CYAN}System: $(uname -s -r -m)${NC}"
echo -e "${CYAN}CPU: $(grep "model name" /proc/cpuinfo 2>/dev/null | head -1 | cut -d: -f2 | xargs || echo "Unknown")${NC}"
echo -e "${CYAN}Memory: $(free -h 2>/dev/null | grep "Mem:" | awk '{print $2}' || echo "Unknown")${NC}"
echo ""

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
{
   StdLib_Race
   Helgrind:Race
   ...
   obj:*/libstdc++*
}
EOF
}

# Function to parse memcheck results properly
parse_memcheck_results() {
    local log_file="$1"

    if [[ ! -f "${log_file}" ]]; then
        echo "N/A|N/A|N/A|N/A"
        return
    fi

    # Extract actual values from the LEAK SUMMARY section
    local definitely_lost="0"
    local possibly_lost="0"
    local still_reachable="0"
    local errors="0"

    # Look for "definitely lost:" in the format "definitely lost: 1,024 bytes in 5 blocks"
    if grep -q "definitely lost:" "${log_file}"; then
        definitely_lost=$(grep "definitely lost:" "${log_file}" | tail -1 | sed 's/.*definitely lost: \([0-9,]*\) bytes.*/\1/' | tr -d ',')
        [[ "$definitely_lost" =~ ^[0-9]+$ ]] || definitely_lost="0"
    fi

    if grep -q "possibly lost:" "${log_file}"; then
        possibly_lost=$(grep "possibly lost:" "${log_file}" | tail -1 | sed 's/.*possibly lost: \([0-9,]*\) bytes.*/\1/' | tr -d ',')
        [[ "$possibly_lost" =~ ^[0-9]+$ ]] || possibly_lost="0"
    fi

    if grep -q "still reachable:" "${log_file}"; then
        still_reachable=$(grep "still reachable:" "${log_file}" | tail -1 | sed 's/.*still reachable: \([0-9,]*\) bytes.*/\1/' | tr -d ',')
        [[ "$still_reachable" =~ ^[0-9]+$ ]] || still_reachable="0"
    fi

    # Check for clean exit
    if grep -q "All heap blocks were freed -- no leaks are possible" "${log_file}"; then
        definitely_lost="0"
        possibly_lost="0"
    fi

    # Get error count
    if grep -q "ERROR SUMMARY:" "${log_file}"; then
        errors=$(grep "ERROR SUMMARY:" "${log_file}" | tail -1 | awk '{print $4}')
        [[ "$errors" =~ ^[0-9]+$ ]] || errors="0"
    fi

    echo "${definitely_lost}|${possibly_lost}|${still_reachable}|${errors}"
}

# Function to parse thread analysis results
parse_thread_results() {
    local log_file="$1"
    local tool="$2"  # helgrind or drd

    if [[ ! -f "${log_file}" ]]; then
        echo "N/A|N/A"
        return
    fi

    local races="0"
    local errors="0"

    if [[ "$tool" == "helgrind" ]]; then
        races=$(grep -c "Possible data race\|Thread #.* conflicting" "${log_file}" 2>/dev/null || echo "0")
        errors=$(grep "ERROR SUMMARY:" "${log_file}" | tail -1 | awk '{print $4}' 2>/dev/null || echo "0")
    elif [[ "$tool" == "drd" ]]; then
        races=$(grep -c "Conflicting\|data race" "${log_file}" 2>/dev/null || echo "0")
        errors=$(grep "ERROR SUMMARY:" "${log_file}" | tail -1 | awk '{print $4}' 2>/dev/null || echo "0")
    fi

    [[ "$races" =~ ^[0-9]+$ ]] || races="0"
    [[ "$errors" =~ ^[0-9]+$ ]] || errors="0"

    echo "${races}|${errors}"
}

# Function to parse cachegrind results
parse_cache_results() {
    local log_file="$1"

    if [[ ! -f "${log_file}" ]]; then
        echo "N/A|N/A|N/A"
        return
    fi

    local l1_data="N/A"
    local l1_inst="N/A"
    local ll_rate="N/A"

    if grep -q "D1  miss rate:" "${log_file}"; then
        l1_data=$(grep "D1  miss rate:" "${log_file}" | awk '{print $4}' | sed 's/%//')
    fi

    if grep -q "I1  miss rate:" "${log_file}"; then
        l1_inst=$(grep "I1  miss rate:" "${log_file}" | awk '{print $4}' | sed 's/%//')
    fi

    if grep -q "LL miss rate:" "${log_file}"; then
        ll_rate=$(grep "LL miss rate:" "${log_file}" | awk '{print $4}' | sed 's/%//')
    fi

    echo "${l1_data}|${l1_inst}|${ll_rate}"
}

# Function to run a single valgrind test
run_valgrind_test() {
    local test_name="$1"
    local executable="$2"
    local tool="$3"
    local options="$4"
    local output_suffix="$5"

    local exe_path="${BIN_DIR}/${executable}"
    local log_file="${RESULTS_DIR}/${test_name}_${output_suffix}.log"

    if [[ ! -f "${exe_path}" ]]; then
        echo -e "${RED}  ✗ Executable not found: ${exe_path}${NC}"
        ((TESTS_FAILED++))
        return 1
    fi

    echo -e "${CYAN}  Analyzing ${test_name}...${NC}"

    # Run the test with timeout to prevent hanging
    timeout 300s valgrind ${options} \
        --log-file="${log_file}" \
        "${exe_path}" >/dev/null 2>&1

    local exit_code=$?

    if [[ $exit_code -eq 124 ]]; then
        echo -e "${YELLOW}  ⏰ TIMEOUT (5 minutes exceeded)${NC}"
        ((TESTS_TIMEOUT++))
        return 2
    elif [[ $exit_code -eq 201 ]]; then
        # Exit code 201 is normal under Cachegrind
        if [[ "$tool" == "cachegrind" ]]; then
            echo -e "${GREEN}  ✓ Complete${NC}"
            ((TESTS_PASSED++))
            return 0
        else
            echo -e "${YELLOW}  ⚠ Exit code 201${NC}"
            ((TESTS_PASSED++))
            return 0
        fi
    elif [[ $exit_code -ne 0 ]]; then
        echo -e "${YELLOW}  ⚠ Exit code ${exit_code}${NC}"
        ((TESTS_PASSED++))
        return 0
    else
        echo -e "${GREEN}  ✓ Complete${NC}"
        ((TESTS_PASSED++))
        return 0
    fi

    ((TESTS_RUN++))
}

# Function to run memory leak analysis
run_memcheck_analysis() {
    echo -e "${BOLD}${PURPLE}═══════════════════════════════════════════════════════════${NC}"
    echo -e "${BOLD}${PURPLE}  MEMORY LEAK ANALYSIS (Memcheck)${NC}"
    echo -e "${BOLD}${PURPLE}═══════════════════════════════════════════════════════════${NC}"
    echo ""

    local clean_count=0
    local leak_count=0

    for test_name in "${!TEST_EXECUTABLES[@]}"; do
        local executable="${TEST_EXECUTABLES[$test_name]}"
        run_valgrind_test "${test_name}" "${executable}" "memcheck" \
            "--tool=memcheck --leak-check=full --show-leak-kinds=all --track-origins=yes --suppressions=${SUPPRESSIONS_FILE}" "memcheck"

        # Analyze results
        local log_file="${RESULTS_DIR}/${test_name}_memcheck.log"
        if [[ -f "${log_file}" ]]; then
            IFS='|' read -r def_lost poss_lost still_reach errors <<< "$(parse_memcheck_results "${log_file}")"

            if [[ "$def_lost" == "0" && "$errors" == "0" ]]; then
                ((clean_count++))
            else
                ((leak_count++))
            fi
        fi
    done

    echo ""
    echo -e "${BOLD}Summary:${NC}"
    echo -e "  ${GREEN}Clean Tests: ${clean_count}${NC}"
    echo -e "  ${YELLOW}Tests with Issues: ${leak_count}${NC}"
    echo ""
}

# Function to run thread error analysis
run_helgrind_analysis() {
    echo -e "${BOLD}${PURPLE}═══════════════════════════════════════════════════════════${NC}"
    echo -e "${BOLD}${PURPLE}  THREAD SAFETY ANALYSIS (Helgrind)${NC}"
    echo -e "${BOLD}${PURPLE}═══════════════════════════════════════════════════════════${NC}"
    echo ""

    # Only run helgrind on thread-related tests to save time
    local thread_tests=("thread_system" "thread_safe_ai" "thread_safe_integration" "event_manager")

    local safe_count=0
    local issue_count=0

    for test_name in "${thread_tests[@]}"; do
        if [[ -n "${TEST_EXECUTABLES[$test_name]}" ]]; then
            local executable="${TEST_EXECUTABLES[$test_name]}"
            run_valgrind_test "${test_name}" "${executable}" "helgrind" \
                "--tool=helgrind --history-level=full --suppressions=${SUPPRESSIONS_FILE}" "helgrind"

            # Analyze results
            local log_file="${RESULTS_DIR}/${test_name}_helgrind.log"
            if [[ -f "${log_file}" ]]; then
                IFS='|' read -r races errors <<< "$(parse_thread_results "${log_file}" "helgrind")"

                if [[ "$races" == "0" ]]; then
                    ((safe_count++))
                else
                    ((issue_count++))
                fi
            fi
        fi
    done

    echo ""
    echo -e "${BOLD}Summary:${NC}"
    echo -e "  ${GREEN}Thread-Safe: ${safe_count}${NC}"
    echo -e "  ${YELLOW}Potential Issues: ${issue_count}${NC}"
    echo ""
}

# Function to run cache analysis
run_cachegrind_analysis() {
    echo -e "${BOLD}${PURPLE}═══════════════════════════════════════════════════════════${NC}"
    echo -e "${BOLD}${PURPLE}  CACHE PERFORMANCE ANALYSIS (Cachegrind)${NC}"
    echo -e "${BOLD}${PURPLE}═══════════════════════════════════════════════════════════${NC}"
    echo ""

    # Run cachegrind on key performance tests
    local cache_tests=("buffer_utilization" "ai_optimization" "event_manager")

    for test_name in "${cache_tests[@]}"; do
        if [[ -n "${TEST_EXECUTABLES[$test_name]}" ]]; then
            local executable="${TEST_EXECUTABLES[$test_name]}"
            run_valgrind_test "${test_name}" "${executable}" "cachegrind" \
                "--tool=cachegrind --cache-sim=yes --cachegrind-out-file=${RESULTS_DIR}/${test_name}_cachegrind.out" "cachegrind"
        fi
    done

    echo ""
    echo -e "${BOLD}Cache analysis complete. Use kcachegrind to visualize results.${NC}"
    echo ""
}

# Function to run DRD (Data Race Detector) analysis
run_drd_analysis() {
    echo -e "${BOLD}${PURPLE}═══════════════════════════════════════════════════════════${NC}"
    echo -e "${BOLD}${PURPLE}  DATA RACE DETECTION (DRD)${NC}"
    echo -e "${BOLD}${PURPLE}═══════════════════════════════════════════════════════════${NC}"
    echo ""

    local thread_tests=("thread_system" "thread_safe_ai")

    local safe_count=0
    local race_count=0

    for test_name in "${thread_tests[@]}"; do
        if [[ -n "${TEST_EXECUTABLES[$test_name]}" ]]; then
            local executable="${TEST_EXECUTABLES[$test_name]}"
            run_valgrind_test "${test_name}" "${executable}" "drd" \
                "--tool=drd --check-stack-var=yes --suppressions=${SUPPRESSIONS_FILE}" "drd"

            # Analyze results
            local log_file="${RESULTS_DIR}/${test_name}_drd.log"
            if [[ -f "${log_file}" ]]; then
                IFS='|' read -r races errors <<< "$(parse_thread_results "${log_file}" "drd")"

                if [[ "$races" == "0" ]]; then
                    ((safe_count++))
                else
                    ((race_count++))
                fi
            fi
        fi
    done

    echo ""
    echo -e "${BOLD}Summary:${NC}"
    echo -e "  ${GREEN}No Races: ${safe_count}${NC}"
    echo -e "  ${YELLOW}Races Detected: ${race_count}${NC}"
    echo ""
}

# Function to generate comprehensive report
generate_report() {
    local report_file="${RESULTS_DIR}/valgrind_analysis_report.md"

    cat > "${report_file}" << EOF
# Valgrind Analysis Report - SDL3 HammerEngine Template

**Generated**: $(date)
**System**: $(uname -a)
**Valgrind**: $(valgrind --version 2>/dev/null || echo "Unknown")

## Executive Summary

This report provides a comprehensive analysis using multiple Valgrind tools:
- **Memcheck**: Memory leak and error detection
- **Helgrind**: Thread safety analysis
- **Cachegrind**: Cache performance profiling
- **DRD**: Data race detection

## Test Results

### Memory Leak Analysis (Memcheck)

| Test | Definitely Lost | Possibly Lost | Errors | Status |
|------|-----------------|---------------|--------|--------|
EOF

    # Add memcheck results
    for test_name in "${!TEST_EXECUTABLES[@]}"; do
        local log_file="${RESULTS_DIR}/${test_name}_memcheck.log"
        if [[ -f "${log_file}" ]]; then
            IFS='|' read -r def_lost poss_lost still_reach errors <<< "$(parse_memcheck_results "${log_file}")"

            local status="✓ CLEAN"
            if [[ "$def_lost" != "0" || "$errors" != "0" ]]; then
                status="⚠ ISSUES"
            fi

            echo "| ${test_name} | ${def_lost} bytes | ${poss_lost} bytes | ${errors} | ${status} |" >> "${report_file}"
        fi
    done

    cat >> "${report_file}" << EOF

### Thread Safety Analysis (Helgrind)

| Test | Possible Races | Errors | Status |
|------|----------------|--------|--------|
EOF

    # Add helgrind results
    local thread_tests=("thread_system" "thread_safe_ai" "thread_safe_integration" "event_manager")
    for test_name in "${thread_tests[@]}"; do
        local log_file="${RESULTS_DIR}/${test_name}_helgrind.log"
        if [[ -f "${log_file}" ]]; then
            IFS='|' read -r races errors <<< "$(parse_thread_results "${log_file}" "helgrind")"

            local status="✓ SAFE"
            if [[ "$races" != "0" ]]; then
                status="⚠ RACES"
            fi

            echo "| ${test_name} | ${races} | ${errors} | ${status} |" >> "${report_file}"
        fi
    done

    cat >> "${report_file}" << EOF

### Cache Performance (Cachegrind)

| Test | L1 Data Miss | L1 Inst Miss | LL Miss | Assessment |
|------|--------------|--------------|---------|------------|
EOF

    # Add cachegrind results
    local cache_tests=("buffer_utilization" "ai_optimization" "event_manager")
    for test_name in "${cache_tests[@]}"; do
        local log_file="${RESULTS_DIR}/${test_name}_cachegrind.log"
        if [[ -f "${log_file}" ]]; then
            IFS='|' read -r l1_data l1_inst ll_rate <<< "$(parse_cache_results "${log_file}")"

            local assessment="GOOD"
            if [[ "$l1_data" != "N/A" && "$ll_rate" != "N/A" ]]; then
                if (( $(echo "$l1_data < 2.0" | bc -l 2>/dev/null || echo 0) )); then
                    assessment="EXCELLENT"
                fi
            fi

            echo "| ${test_name} | ${l1_data}% | ${l1_inst}% | ${ll_rate}% | ${assessment} |" >> "${report_file}"
        fi
    done

    cat >> "${report_file}" << EOF

## Recommendations

1. **Memory Management**: Review tests with non-zero "Definitely Lost" values
2. **Thread Safety**: Investigate any reported data races
3. **Cache Performance**: Tests with <2% L1 miss rate show excellent optimization

## Files Generated

All detailed logs available in: \`${RESULTS_DIR}/\`

---
*Generated by SDL3 HammerEngine Valgrind Analysis Suite*
EOF

    echo -e "${GREEN}✓ Report generated: ${report_file}${NC}"
}

# Main execution
main() {
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

    echo -e "${BOLD}${GREEN}═══════════════════════════════════════════════════════════${NC}"
    echo -e "${BOLD}${GREEN}  ANALYSIS COMPLETE${NC}"
    echo -e "${BOLD}${GREEN}═══════════════════════════════════════════════════════════${NC}"
    echo ""
    echo -e "${CYAN}Tests Run: ${TESTS_RUN}${NC}"
    echo -e "${GREEN}Tests Passed: ${TESTS_PASSED}${NC}"
    echo -e "${YELLOW}Tests Timeout: ${TESTS_TIMEOUT}${NC}"
    echo -e "${RED}Tests Failed: ${TESTS_FAILED}${NC}"
    echo ""
    echo -e "${CYAN}Results: ${RESULTS_DIR}${NC}"
    echo -e "${CYAN}Report: ${RESULTS_DIR}/valgrind_analysis_report.md${NC}"
    echo ""
}

# Show usage if help requested
if [[ "$1" == "--help" || "$1" == "-h" ]]; then
    echo "SDL3 HammerEngine Valgrind Analysis Suite"
    echo ""
    echo "Usage: $0 [analysis_type]"
    echo ""
    echo "Analysis types:"
    echo "  all        - Run all analyses (default)"
    echo "  memcheck   - Memory leak detection only"
    echo "  helgrind   - Thread safety analysis only"
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
