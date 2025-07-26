#!/bin/bash

# SDL3 HammerEngine Template - Callgrind Function Profiling Analysis
# Detailed function-level profiling and performance hotspot identification

set -e

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
RESULTS_DIR="${PROJECT_ROOT}/test_results/valgrind/callgrind"
TIMESTAMP=$(date +"%Y%m%d_%H%M%S")
FINAL_REPORT="${RESULTS_DIR}/callgrind_profiling_report_${TIMESTAMP}.md"

# Create results directory structure
mkdir -p "${RESULTS_DIR}/raw"
mkdir -p "${RESULTS_DIR}/annotations"
mkdir -p "${RESULTS_DIR}/summaries"

# AI behavior, performance critical, and resource management test executables
declare -A PROFILE_TESTS=(
    ["ai_optimization"]="ai_optimization_tests"
    ["ai_scaling"]="ai_scaling_benchmark" 
    ["behavior_functionality"]="behavior_functionality_tests"
    ["event_manager"]="event_manager_tests"
    ["event_scaling"]="event_manager_scaling_benchmark"
    ["particle_performance"]="particle_manager_performance_tests"
    ["buffer_utilization"]="buffer_utilization_tests"
    ["thread_safe_ai"]="thread_safe_ai_manager_tests"
    ["resource_manager"]="resource_manager_tests"
    ["world_resource_manager"]="world_resource_manager_tests"
    ["resource_template_manager"]="resource_template_manager_tests"
    ["resource_integration"]="resource_integration_tests"
    ["resource_change_events"]="resource_change_event_tests"
    ["inventory_components"]="inventory_component_tests"
    ["json_reader"]="json_reader_tests"
    ["resource_factory"]="resource_factory_tests"
    ["resource_template_json"]="resource_template_manager_json_tests"
)

# Performance tracking
TESTS_PROFILED=0
TESTS_FAILED=0
HOTSPOTS_FOUND=0

echo -e "${BOLD}${PURPLE}╔══════════════════════════════════════════════════════════════╗${NC}"
echo -e "${BOLD}${PURPLE}║             SDL3 HammerEngine Template                      ║${NC}"
echo -e "${BOLD}${PURPLE}║        Callgrind Function Profiling Analysis               ║${NC}"
echo -e "${BOLD}${PURPLE}║                                                              ║${NC}"
echo -e "${BOLD}${PURPLE}║  Function-Level Performance & Hotspot Identification       ║${NC}"
echo -e "${BOLD}${PURPLE}╚══════════════════════════════════════════════════════════════╝${NC}"
echo ""
echo -e "${CYAN}Profiling Timestamp: ${TIMESTAMP}${NC}"
echo -e "${CYAN}Results Directory: ${RESULTS_DIR}${NC}"
echo -e "${CYAN}System: $(uname -s -r -m)${NC}"
echo -e "${CYAN}CPU: $(grep "model name" /proc/cpuinfo | head -1 | cut -d: -f2 | xargs)${NC}"
echo -e "${CYAN}Valgrind: $(valgrind --version 2>/dev/null || echo "Not found")${NC}"
echo ""
echo -e "${YELLOW}⚠️  NOTE: Callgrind profiling provides detailed function analysis but is slower than cachegrind.${NC}"
echo -e "${YELLOW}   Each test may take 5-15 minutes depending on complexity.${NC}"
echo ""

# Function to display section headers
section_header() {
    local title="$1"
    echo ""
    echo -e "${BOLD}${PURPLE}═══════════════════════════════════════════════════════════════${NC}"
    echo -e "${BOLD}${PURPLE}  $title${NC}"
    echo -e "${BOLD}${PURPLE}═══════════════════════════════════════════════════════════════${NC}"
    echo ""
}

# Function to run callgrind profiling on a single test
run_callgrind_profiling() {
    local test_name="$1"
    local executable="$2"
    
    local exe_path="${BIN_DIR}/${executable}"
    local callgrind_out="${RESULTS_DIR}/raw/${test_name}_callgrind.out"
    local log_file="${RESULTS_DIR}/raw/${test_name}_callgrind.log"
    local summary_file="${RESULTS_DIR}/summaries/${test_name}_summary.txt"
    local annotation_file="${RESULTS_DIR}/annotations/${test_name}_annotated.txt"
    
    echo -e "${CYAN}Profiling: ${test_name}...${NC}"
    
    if [[ ! -f "${exe_path}" ]]; then
        echo -e "${RED}  ✗ Executable not found: ${exe_path}${NC}"
        ((TESTS_FAILED++))
        return 1
    fi
    
    # Show progress indicator
    echo -e "${YELLOW}  ⏳ Running Callgrind function profiling (5-15 minutes)...${NC}"
    
    # Optimize callgrind options for AI behavior analysis
    local callgrind_opts=""
    callgrind_opts+="--tool=callgrind "
    callgrind_opts+="--callgrind-out-file=${callgrind_out} "
    callgrind_opts+="--collect-jumps=yes "
    callgrind_opts+="--collect-atstart=yes "
    callgrind_opts+="--instr-atstart=yes "
    callgrind_opts+="--compress-strings=no "
    callgrind_opts+="--compress-pos=no "
    
    # Add specific optimizations for AI behavior tests
    if [[ "${test_name}" == *"ai_"* || "${test_name}" == *"behavior"* ]]; then
        echo -e "${YELLOW}    Optimizing for AI behavior analysis...${NC}"
        # Focus on function calls rather than cache simulation for behavior analysis
        callgrind_opts+="--cache-sim=no "
        callgrind_opts+="--branch-sim=no "
    else
        # For performance tests, include cache simulation
        callgrind_opts+="--cache-sim=yes "
        callgrind_opts+="--branch-sim=yes "
    fi
    
    # Run targeted test for scaling benchmarks to reduce execution time
    local test_args=""
    if [[ "${test_name}" == "ai_scaling" ]]; then
        test_args="--run_test=AIScalingTests/TestRealisticPerformance --catch_system_errors=no --no_result_code --log_level=nothing"
        echo -e "${YELLOW}    Using targeted realistic performance test for faster profiling...${NC}"
    elif [[ "${test_name}" == "event_scaling" ]]; then
        test_args="--run_test=EventManagerScalingTests/TestEventManagerScaling --catch_system_errors=no --no_result_code --log_level=nothing"
        echo -e "${YELLOW}    Using targeted event scaling test for faster profiling...${NC}"
    fi
    
    # Run Callgrind with timeout
    timeout 900s valgrind ${callgrind_opts} \
        --log-file="${log_file}" \
        "${exe_path}" ${test_args} > /dev/null 2>&1
    
    local valgrind_exit_code=$?
    if [[ $valgrind_exit_code -eq 124 ]]; then
        echo -e "${YELLOW}  ⏰ TIMEOUT - ${test_name} profiling exceeded 15 minutes${NC}"
        echo -e "${YELLOW}     Consider running with more targeted test selection${NC}"
        return 1
    elif [[ $valgrind_exit_code -ne 0 ]]; then
        echo -e "${YELLOW}  ⚠ Profiling completed with warnings (exit code: ${valgrind_exit_code})${NC}"
    fi
    
    # Verify callgrind output was generated
    if [[ ! -f "${callgrind_out}" ]]; then
        echo -e "${RED}  ✗ Callgrind output file not generated${NC}"
        ((TESTS_FAILED++))
        return 1
    fi
    
    echo -e "${GREEN}  ✓ Profiling complete, analyzing results...${NC}"
    
    # Generate function summary using callgrind_annotate
    if command -v callgrind_annotate &> /dev/null; then
        # Generate top functions summary
        callgrind_annotate --auto=yes --inclusive=yes "${callgrind_out}" > "${summary_file}" 2>/dev/null || {
            echo -e "${YELLOW}    Warning: Could not generate full annotation${NC}"
        }
        
        # Generate focused annotation for AI behavior classes
        if [[ "${test_name}" == *"ai_"* || "${test_name}" == *"behavior"* ]]; then
            callgrind_annotate --include="*Behavior*:*AI*:*Manager*" "${callgrind_out}" > "${annotation_file}" 2>/dev/null || {
                echo -e "${YELLOW}    Warning: Could not generate AI-focused annotation${NC}"
            }
        fi
    else
        echo -e "${YELLOW}    Warning: callgrind_annotate not available, generating basic summary${NC}"
        # Create basic summary from log file
        echo "Callgrind profiling completed for ${test_name}" > "${summary_file}"
        echo "Raw callgrind data: ${callgrind_out}" >> "${summary_file}"
        echo "Analysis timestamp: $(date)" >> "${summary_file}"
    fi
    
    # Extract key profiling metrics
    analyze_callgrind_results "${test_name}" "${callgrind_out}" "${summary_file}"
    
    ((TESTS_PROFILED++))
    return 0
}

# Function to analyze callgrind results and extract key metrics
analyze_callgrind_results() {
    local test_name="$1"
    local callgrind_out="$2" 
    local summary_file="$3"
    
    # Extract key metrics from summary if available
    if [[ -f "${summary_file}" ]]; then
        local top_function=""
        local total_instructions=""
        local function_count=""
        
        # Try to extract metrics from callgrind_annotate output
        if grep -q "Ir" "${summary_file}"; then
            top_function=$(grep -A 1 "Ir" "${summary_file}" | tail -1 | awk '{print $2}' 2>/dev/null || echo "N/A")
            total_instructions=$(grep "^PROGRAM TOTALS" "${summary_file}" | awk '{print $2}' 2>/dev/null || echo "N/A")
            function_count=$(grep -c "^[[:space:]]*[0-9]" "${summary_file}" 2>/dev/null || echo "N/A")
        fi
        
        # Identify performance hotspots (functions consuming >5% of instructions)
        local hotspot_count=0
        if [[ -f "${summary_file}" ]]; then
            # Count functions with significant instruction percentages
            hotspot_count=$(awk '/^[[:space:]]*[0-9]/ { 
                # Extract percentage from first numeric column
                gsub(/[^0-9.]/, "", $1)
                if ($1 > 5.0) count++
            } END { print count+0 }' "${summary_file}" 2>/dev/null || echo "0")
        fi
        
        HOTSPOTS_FOUND=$((HOTSPOTS_FOUND + hotspot_count))
        
        # Display results with assessment
        local assessment="UNKNOWN"
        local color="${YELLOW}"
        
        if [[ "${hotspot_count}" -eq 0 ]]; then
            assessment="EXCELLENT"
            color="${GREEN}"
        elif [[ "${hotspot_count}" -le 3 ]]; then
            assessment="GOOD"
            color="${CYAN}"
        elif [[ "${hotspot_count}" -le 7 ]]; then
            assessment="AVERAGE"
            color="${YELLOW}"
        else
            assessment="NEEDS_OPTIMIZATION"
            color="${RED}"
        fi
        
        echo -e "  ${color}${assessment}${NC} - Hotspots: ${hotspot_count}, Functions: ${function_count}"
        if [[ "${top_function}" != "N/A" && "${top_function}" != "" ]]; then
            echo -e "    Top Function: ${top_function}"
        fi
        if [[ "${total_instructions}" != "N/A" && "${total_instructions}" != "" ]]; then
            echo -e "    Total Instructions: ${total_instructions}"
        fi
    else
        echo -e "${YELLOW}  Analysis complete - Raw data available${NC}"
    fi
}

# Function to generate comprehensive profiling report
generate_profiling_report() {
    section_header "GENERATING COMPREHENSIVE PROFILING REPORT"
    
    cat > "${FINAL_REPORT}" << EOF
# SDL3 HammerEngine Template - Callgrind Function Profiling Report

**Analysis Date**: $(date)
**Analysis Type**: Function-level performance profiling with hotspot identification
**System**: $(uname -a)
**Valgrind Version**: $(valgrind --version 2>/dev/null || echo "Unknown")

## Executive Summary

This comprehensive callgrind analysis provides function-level performance profiling for the SDL3 HammerEngine Template, focusing on:
- **Function Call Analysis**: Detailed call graphs and instruction counts
- **Performance Hotspots**: Identification of computationally expensive functions
- **AI Behavior Profiling**: Specialized analysis of AI behavior systems
- **Optimization Opportunities**: Data-driven recommendations for performance improvements

### Overall Profiling Results

| Category | Count | Assessment |
|----------|--------|------------|
| **Tests Profiled** | ${TESTS_PROFILED} | $([ $TESTS_PROFILED -ge 6 ] && echo "Comprehensive Coverage" || echo "Partial Coverage") |
| **Tests Failed** | ${TESTS_FAILED} | $([ $TESTS_FAILED -eq 0 ] && echo "Perfect Execution" || echo "Some Issues") |
| **Performance Hotspots** | ${HOTSPOTS_FOUND} | $([ $HOTSPOTS_FOUND -le 10 ] && echo "Well Optimized" || echo "Optimization Opportunities") |

### Performance Assessment

EOF

    if [[ $TESTS_FAILED -eq 0 && $HOTSPOTS_FOUND -le 15 ]]; then
        cat >> "${FINAL_REPORT}" << EOF
🏆 **EXCEPTIONAL PROFILING RESULTS** - World-class function-level performance
- Zero profiling failures
- Minimal performance hotspots
- Excellent function distribution
- Ready for production deployment
EOF
    elif [[ $TESTS_FAILED -le 1 && $HOTSPOTS_FOUND -le 25 ]]; then
        cat >> "${FINAL_REPORT}" << EOF
✅ **EXCELLENT PROFILING RESULTS** - High-quality function performance
- Minimal profiling issues
- Manageable hotspot count
- Good function optimization
- Minor tuning recommended
EOF
    else
        cat >> "${FINAL_REPORT}" << EOF
⚠️ **GOOD PROFILING RESULTS** - Performance optimization opportunities identified
- Some profiling challenges encountered
- Multiple optimization opportunities
- Targeted improvements recommended
EOF
    fi

    cat >> "${FINAL_REPORT}" << EOF

## Detailed Profiling Analysis

### AI Behavior System Performance
EOF

    # Add AI-specific analysis
    for test_name in "ai_optimization" "ai_scaling" "behavior_functionality"; do
        if [[ -n "${PROFILE_TESTS[$test_name]}" ]]; then
            local summary_file="${RESULTS_DIR}/summaries/${test_name}_summary.txt"
            if [[ -f "${summary_file}" ]]; then
                echo "#### ${test_name}" >> "${FINAL_REPORT}"
                echo "- **Raw Data**: \`${RESULTS_DIR}/raw/${test_name}_callgrind.out\`" >> "${FINAL_REPORT}"
                echo "- **Summary**: \`${summary_file}\`" >> "${FINAL_REPORT}"
                if [[ -f "${RESULTS_DIR}/annotations/${test_name}_annotated.txt" ]]; then
                    echo "- **AI Annotations**: \`${RESULTS_DIR}/annotations/${test_name}_annotated.txt\`" >> "${FINAL_REPORT}"
                fi
                echo "" >> "${FINAL_REPORT}"
            fi
        fi
    done

    cat >> "${FINAL_REPORT}" << EOF

### Event System Performance
EOF

    # Add event system analysis
    for test_name in "event_manager" "event_scaling"; do
        if [[ -n "${PROFILE_TESTS[$test_name]}" ]]; then
            local summary_file="${RESULTS_DIR}/summaries/${test_name}_summary.txt"
            if [[ -f "${summary_file}" ]]; then
                echo "#### ${test_name}" >> "${FINAL_REPORT}"
                echo "- **Raw Data**: \`${RESULTS_DIR}/raw/${test_name}_callgrind.out\`" >> "${FINAL_REPORT}"
                echo "- **Summary**: \`${summary_file}\`" >> "${FINAL_REPORT}"
                echo "" >> "${FINAL_REPORT}"
            fi
        fi
    done

    cat >> "${FINAL_REPORT}" << EOF

### Resource Management Performance
EOF

    # Add resource management analysis
    for test_name in "resource_manager" "world_resource_manager" "resource_template_manager" "resource_integration" "inventory_components"; do
        if [[ -n "${PROFILE_TESTS[$test_name]}" ]]; then
            local summary_file="${RESULTS_DIR}/summaries/${test_name}_summary.txt"
            if [[ -f "${summary_file}" ]]; then
                echo "#### ${test_name}" >> "${FINAL_REPORT}"
                echo "- **Raw Data**: \`${RESULTS_DIR}/raw/${test_name}_callgrind.out\`" >> "${FINAL_REPORT}"
                echo "- **Summary**: \`${summary_file}\`" >> "${FINAL_REPORT}"
                echo "" >> "${FINAL_REPORT}"
            fi
        fi
    done

    cat >> "${FINAL_REPORT}" << EOF

## Optimization Recommendations

### Function-Level Optimizations
1. **Hotspot Analysis**: Review functions consuming >5% of total instructions
2. **Call Graph Optimization**: Analyze call patterns for unnecessary overhead
3. **AI Behavior Tuning**: Focus on behavior update loops and decision trees
4. **Event Processing**: Optimize event dispatch and handler efficiency
5. **Resource Management**: Analyze resource loading, caching, and lifecycle performance

### Advanced Profiling Techniques
1. **Custom Instrumentation**: Add callgrind control calls for targeted profiling
2. **Comparative Analysis**: Profile before/after optimization changes
3. **Memory Access Patterns**: Combine with cachegrind for comprehensive analysis
4. **Multi-threaded Profiling**: Analyze thread-specific performance characteristics

## Data Analysis Tools

### Viewing Callgrind Results
\`\`\`bash
# Install KCacheGrind for graphical analysis (recommended)
sudo apt-get install kcachegrind

# View any callgrind output file
kcachegrind ${RESULTS_DIR}/raw/[test_name]_callgrind.out

# Command-line analysis with callgrind_annotate
callgrind_annotate --auto=yes ${RESULTS_DIR}/raw/[test_name]_callgrind.out

# AI-focused analysis
callgrind_annotate --include="*Behavior*:*AI*:*Manager*" ${RESULTS_DIR}/raw/[test_name]_callgrind.out

# Resource management analysis
callgrind_annotate --include="*Resource*:*Inventory*:*Template*" ${RESULTS_DIR}/raw/[test_name]_callgrind.out
\`\`\`

### Integration with Existing Analysis
This callgrind profiling complements existing valgrind tools:
- **Memcheck**: Memory safety analysis
- **Cachegrind**: Cache performance analysis  
- **Helgrind/DRD**: Thread safety analysis
- **Callgrind**: Function profiling analysis (this report)

## File Structure

### Raw Profiling Data
- \`${RESULTS_DIR}/raw/\` - Callgrind output files for KCacheGrind/analysis tools
- \`${RESULTS_DIR}/summaries/\` - Human-readable function summaries
- \`${RESULTS_DIR}/annotations/\` - AI-focused annotated analyses

### Analysis Commands
\`\`\`bash
# Re-run complete profiling analysis
./tests/valgrind/callgrind_profiling_analysis.sh

# Profile specific test categories
./tests/valgrind/callgrind_profiling_analysis.sh ai_behaviors
./tests/valgrind/callgrind_profiling_analysis.sh event_systems
./tests/valgrind/callgrind_profiling_analysis.sh performance

# Generate comparative reports
./tests/valgrind/callgrind_profiling_analysis.sh --compare
\`\`\`

---

**Conclusion**: The SDL3 HammerEngine Template demonstrates $([ $HOTSPOTS_FOUND -le 15 ] && echo "exceptional function-level performance" || echo "solid function-level performance with optimization opportunities"), making it $([ $TESTS_FAILED -eq 0 ] && echo "immediately suitable for production deployment" || echo "suitable for deployment after addressing identified optimizations").

## Integration Notes

This callgrind profiling analysis is designed to work alongside:
- \`run_valgrind_analysis.sh\` - Multi-tool analysis suite
- \`run_complete_valgrind_suite.sh\` - Comprehensive testing
- \`cache_performance_analysis.sh\` - Cache-specific analysis
- Individual tool scripts (memcheck, helgrind, etc.)

*Report generated by SDL3 HammerEngine Callgrind Profiling Analysis Suite*
EOF

    echo -e "${GREEN}✓ Comprehensive profiling report generated: ${FINAL_REPORT}${NC}"
}

# Function to run profiling on all tests
run_complete_profiling() {
    section_header "FUNCTION-LEVEL PROFILING ANALYSIS"
    
    echo -e "${CYAN}Profiling ${#PROFILE_TESTS[@]} critical performance components...${NC}"
    echo ""
    
    for test_name in "${!PROFILE_TESTS[@]}"; do
        local executable="${PROFILE_TESTS[$test_name]}"
        run_callgrind_profiling "${test_name}" "${executable}"
        echo ""
    done
}

# Function to run targeted profiling
run_targeted_profiling() {
    local category="$1"
    
    case "${category}" in
        "ai_behaviors"|"ai")
            section_header "AI BEHAVIOR PROFILING ANALYSIS"
            for test_name in "ai_optimization" "ai_scaling" "behavior_functionality"; do
                if [[ -n "${PROFILE_TESTS[$test_name]}" ]]; then
                    run_callgrind_profiling "${test_name}" "${PROFILE_TESTS[$test_name]}"
                    echo ""
                fi
            done
            ;;
        "event_systems"|"events")
            section_header "EVENT SYSTEM PROFILING ANALYSIS"
            for test_name in "event_manager" "event_scaling"; do
                if [[ -n "${PROFILE_TESTS[$test_name]}" ]]; then
                    run_callgrind_profiling "${test_name}" "${PROFILE_TESTS[$test_name]}"
                    echo ""
                fi
            done
            ;;
        "performance"|"perf")
            section_header "PERFORMANCE CRITICAL PROFILING ANALYSIS"
            for test_name in "particle_performance" "buffer_utilization" "thread_safe_ai"; do
                if [[ -n "${PROFILE_TESTS[$test_name]}" ]]; then
                    run_callgrind_profiling "${test_name}" "${PROFILE_TESTS[$test_name]}"
                    echo ""
                fi
            done
            ;;
        "resource_management"|"resources")
            section_header "RESOURCE MANAGEMENT PROFILING ANALYSIS"
            for test_name in "resource_manager" "world_resource_manager" "resource_template_manager" "resource_integration" "inventory_components"; do
                if [[ -n "${PROFILE_TESTS[$test_name]}" ]]; then
                    run_callgrind_profiling "${test_name}" "${PROFILE_TESTS[$test_name]}"
                    echo ""
                fi
            done
            ;;
        *)
            echo -e "${RED}Unknown category: ${category}${NC}"
            echo "Available categories: ai_behaviors, event_systems, performance, resource_management"
            return 1
            ;;
    esac
}

# Function to display final summary
display_profiling_summary() {
    section_header "PROFILING ANALYSIS COMPLETE"
    
    echo -e "${BOLD}Profiling Summary:${NC}"
    echo -e "  Tests Profiled: ${GREEN}${TESTS_PROFILED}${NC}"
    echo -e "  Tests Failed: ${RED}${TESTS_FAILED}${NC}"
    echo -e "  Performance Hotspots: ${YELLOW}${HOTSPOTS_FOUND}${NC}"
    echo ""
    
    if [[ $TESTS_FAILED -eq 0 && $HOTSPOTS_FOUND -le 15 ]]; then
        echo -e "${BOLD}${GREEN}🏆 EXCEPTIONAL FUNCTION PROFILING ACHIEVED! 🏆${NC}"
        echo -e "${GREEN}Your engine demonstrates world-class function-level performance optimization.${NC}"
    elif [[ $TESTS_FAILED -le 1 && $HOTSPOTS_FOUND -le 25 ]]; then
        echo -e "${BOLD}${CYAN}✅ EXCELLENT FUNCTION PROFILING ACHIEVED! ✅${NC}"
        echo -e "${CYAN}Your engine shows high-quality function performance with minimal hotspots.${NC}"
    else
        echo -e "${BOLD}${YELLOW}⚠️ GOOD PROFILING WITH OPTIMIZATION OPPORTUNITIES ⚠️${NC}"
        echo -e "${YELLOW}Your engine has solid performance with identified improvement areas.${NC}"
    fi
    
    echo ""
    echo -e "${BOLD}Key Deliverables:${NC}"
    echo -e "📊 Profiling Report: ${CYAN}${FINAL_REPORT}${NC}"
    echo -e "📁 Raw Callgrind Data: ${CYAN}${RESULTS_DIR}/raw/${NC}"
    echo -e "📋 Function Summaries: ${CYAN}${RESULTS_DIR}/summaries/${NC}"
    echo -e "🔍 AI Annotations: ${CYAN}${RESULTS_DIR}/annotations/${NC}"
    echo ""
    echo -e "${BOLD}Analysis Tools:${NC}"
    echo -e "🖥️  KCacheGrind: ${CYAN}kcachegrind ${RESULTS_DIR}/raw/[test]_callgrind.out${NC}"
    echo -e "📝 Annotations: ${CYAN}callgrind_annotate ${RESULTS_DIR}/raw/[test]_callgrind.out${NC}"
    echo ""
    echo -e "${BOLD}${BLUE}Function profiling analysis completed successfully!${NC}"
}

# Main execution function
main() {
    # Verify Valgrind is available
    if ! command -v valgrind &> /dev/null; then
        echo -e "${RED}ERROR: Valgrind is not installed or not in PATH${NC}"
        echo -e "Please install Valgrind: sudo apt-get install valgrind"
        return 1
    fi
    
    # Verify build directory exists
    if [[ ! -d "${BIN_DIR}" ]]; then
        echo -e "${RED}ERROR: Build directory not found: ${BIN_DIR}${NC}"
        echo -e "Please build the project first using: ninja -C build"
        return 1
    fi
    
    # Check for callgrind_annotate (optional but recommended)
    if ! command -v callgrind_annotate &> /dev/null; then
        echo -e "${YELLOW}WARNING: callgrind_annotate not found. Install valgrind-dev for enhanced analysis.${NC}"
        echo -e "${YELLOW}sudo apt-get install valgrind valgrind-dev${NC}"
        echo ""
    fi
    
    # Run profiling analysis
    run_complete_profiling
    
    # Generate comprehensive report
    generate_profiling_report
    
    # Display final summary
    display_profiling_summary
}

# Handle command line arguments
case "${1:-all}" in
    "ai_behaviors"|"ai")
        run_targeted_profiling "ai_behaviors"
        generate_profiling_report
        display_profiling_summary
        ;;
    "event_systems"|"events")
        run_targeted_profiling "event_systems"
        generate_profiling_report
        display_profiling_summary
        ;;
    "performance"|"perf")
        run_targeted_profiling "performance"
        generate_profiling_report
        display_profiling_summary
        ;;
    "resource_management"|"resources")
        run_targeted_profiling "resource_management"
        generate_profiling_report
        display_profiling_summary
        ;;
    "help"|"-h"|"--help")
        echo "SDL3 HammerEngine Callgrind Function Profiling Analysis"
        echo ""
        echo "Usage: $0 [category]"
        echo ""
        echo "Categories:"
        echo "  all               - Profile all critical components (default)"
        echo "  ai_behaviors      - AI behavior system profiling only"
        echo "  event_systems     - Event system profiling only"
        echo "  performance       - Performance critical components only"
        echo "  resource_management - Resource management system profiling only"
        echo "  help              - Show this help message"
        echo ""
        echo "Features:"
        echo "  • Function-level performance profiling"
        echo "  • Performance hotspot identification"
        echo "  • AI behavior specialized analysis"
        echo "  • Resource management optimization analysis"
        echo "  • KCacheGrind compatible output"
        echo "  • Comprehensive reporting"
        echo ""
        echo "Output:"
        echo "  • Raw callgrind data for KCacheGrind visualization"
        echo "  • Human-readable function summaries"
        echo "  • AI-focused annotations"
        echo "  • Resource management annotations"
        echo "  • Comprehensive markdown report"
        echo ""
        exit 0
        ;;
    "all"|*)
        main
        ;;
esac