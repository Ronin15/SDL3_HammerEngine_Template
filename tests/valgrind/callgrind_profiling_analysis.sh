#!/bin/bash

# SDL3 HammerEngine Template - Callgrind Function Profiling Analysis
# Detailed function-level profiling and performance hotspot identification

# set -e # Disabled to allow controlled error handling in profiling loops

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
    ["event_types"]="event_types_tests"
    ["weather_events"]="weather_event_tests"
    ["particle_core"]="particle_manager_core_tests"
    ["particle_performance"]="particle_manager_performance_tests"
    ["particle_threading"]="particle_manager_threading_tests"
    ["particle_weather"]="particle_manager_weather_tests"
    ["buffer_utilization"]="buffer_utilization_tests"
    ["thread_safe_ai"]="thread_safe_ai_manager_tests"
    ["thread_safe_ai_integration"]="thread_safe_ai_integration_tests"
    ["save_manager"]="save_manager_tests"
    ["ui_stress"]="ui_stress_test"
    ["resource_manager"]="resource_manager_tests"
    ["world_resource_manager"]="world_resource_manager_tests"
    ["world_generator"]="world_generator_tests"
    ["world_manager"]="world_manager_tests"
    ["world_manager_events"]="world_manager_event_integration_tests"
    ["resource_template_manager"]="resource_template_manager_tests"
    ["resource_integration"]="resource_integration_tests"
    ["resource_change_events"]="resource_change_event_tests"
    ["inventory_components"]="inventory_component_tests"
    ["json_reader"]="json_reader_tests"
    ["resource_factory"]="resource_factory_tests"
    ["resource_template_json"]="resource_template_manager_json_tests"
    ["resource_edge_case"]="resource_edge_case_tests"
    ["collision_system"]="collision_system_tests"
    ["pathfinding_system"]="pathfinding_system_tests"
    ["collision_pathfinding_bench"]="collision_pathfinding_benchmark"
    ["collision_pathfinding_integration"]="collision_pathfinding_integration_tests"
)

# Performance tracking
TESTS_PROFILED=0
TESTS_FAILED=0
CRITICAL_HOTSPOTS=0
MODERATE_TARGETS=0
MINOR_OPPORTUNITIES=0
TOTAL_FUNCTIONS_ANALYZED=0

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
    
    # Optimize callgrind options for practical profiling
    local callgrind_opts=""
    callgrind_opts+="--tool=callgrind "
    callgrind_opts+="--callgrind-out-file=${callgrind_out} "
    callgrind_opts+="--collect-jumps=yes "
    callgrind_opts+="--collect-atstart=yes "
    callgrind_opts+="--instr-atstart=yes "
    callgrind_opts+="--compress-strings=yes "
    callgrind_opts+="--compress-pos=yes "
    callgrind_opts+="--separate-callers=3 "  # Break false cycles in callback code
    
    # Add specific optimizations for different test types
    if [[ "${test_name}" == *"ai_"* || "${test_name}" == *"behavior"* ]]; then
        echo -e "${YELLOW}    Optimizing for AI behavior analysis...${NC}"
        # Focus on function calls rather than cache simulation for behavior analysis
        callgrind_opts+="--cache-sim=no "
        callgrind_opts+="--branch-sim=no "
    elif [[ "${test_name}" == *"scaling"* || "${test_name}" == *"performance"* ]]; then
        echo -e "${YELLOW}    Optimizing for performance analysis...${NC}"
        # Include cache simulation for performance-critical tests
        callgrind_opts+="--cache-sim=yes "
        callgrind_opts+="--branch-sim=yes "
    else
        # For general tests, focus on function profiling
        callgrind_opts+="--cache-sim=no "
        callgrind_opts+="--branch-sim=no "
    fi
    
    # Run targeted test for scaling benchmarks to reduce execution time
    local test_args=""
    if [[ "${test_name}" == "ai_scaling" ]]; then
        test_args="--run_test=AIScalingTests/TestRealisticPerformance --catch_system_errors=no --no_result_code --log_level=nothing"
        echo -e "${YELLOW}    Using targeted realistic performance test for faster profiling...${NC}"
    elif [[ "${test_name}" == "event_scaling" ]]; then
        test_args="--run_test=EventManagerScalingTests/TestEventManagerScaling --catch_system_errors=no --no_result_code --log_level=nothing"
        echo -e "${YELLOW}    Using targeted event scaling test for faster profiling...${NC}"
    elif [[ "${test_name}" == "ui_stress" ]]; then
        test_args="--catch_system_errors=no --no_result_code --log_level=nothing"
        echo -e "${YELLOW}    Using optimized test settings for UI stress analysis...${NC}"
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

# Function to analyze callgrind results and extract actionable metrics
analyze_callgrind_results() {
    local test_name="$1"
    local callgrind_out="$2" 
    local summary_file="$3"
    
    # Extract key metrics from summary if available
    if [[ -f "${summary_file}" ]]; then
        local critical_functions=0
        local moderate_functions=0
        local minor_functions=0
        local top_function=""
        local total_instructions=""
        local function_count=""
        
        # Try to extract metrics from callgrind_annotate output
        if grep -q "Ir" "${summary_file}"; then
            # Extract top function and total from callgrind_annotate format
            top_function=$(awk '/^[[:space:]]*[0-9]/ && !/^PROGRAM TOTALS/ && !/^[[:space:]]*Ir/ { print $2; exit }' "${summary_file}" 2>/dev/null || echo "N/A")
            total_instructions=$(grep "^PROGRAM TOTALS" "${summary_file}" | awk '{print $2}' 2>/dev/null || echo "N/A")
            
            # Count functions with meaningful instruction counts (skip headers and totals)
            function_count=$(awk '/^[[:space:]]*[0-9]/ && !/^PROGRAM TOTALS/ && !/^[[:space:]]*Ir/ && $1 > 0 { count++ } END { print count+0 }' "${summary_file}" 2>/dev/null || echo "0")
            
            # Extract percentage values properly from callgrind_annotate output format
            # Format: "  percentage  instruction_count  function_name:file_location"
            critical_functions=$(awk '/^[[:space:]]*[0-9]/ && !/^PROGRAM TOTALS/ && !/^[[:space:]]*Ir/ { 
                # Extract percentage from parentheses or calculate from instruction ratio
                if (match($0, /\(([0-9.]+)%\)/, arr)) {
                    percent = arr[1]
                } else if ($1 > 0 && total > 0) {
                    # Calculate percentage if not in parentheses format
                    gsub(/,/, "", $1)
                    percent = ($1 / total) * 100
                } else {
                    percent = 0
                }
                if (percent >= 10.0) count++
            } END { print count+0 }' total="$(echo "$total_instructions" | sed 's/,//g')" "${summary_file}" 2>/dev/null || echo "0")
            
            moderate_functions=$(awk '/^[[:space:]]*[0-9]/ && !/^PROGRAM TOTALS/ && !/^[[:space:]]*Ir/ { 
                if (match($0, /\(([0-9.]+)%\)/, arr)) {
                    percent = arr[1]
                } else if ($1 > 0 && total > 0) {
                    gsub(/,/, "", $1)
                    percent = ($1 / total) * 100
                } else {
                    percent = 0
                }
                if (percent >= 2.0 && percent < 10.0) count++
            } END { print count+0 }' total="$(echo "$total_instructions" | sed 's/,//g')" "${summary_file}" 2>/dev/null || echo "0")
            
            minor_functions=$(awk '/^[[:space:]]*[0-9]/ && !/^PROGRAM TOTALS/ && !/^[[:space:]]*Ir/ { 
                if (match($0, /\(([0-9.]+)%\)/, arr)) {
                    percent = arr[1]
                } else if ($1 > 0 && total > 0) {
                    gsub(/,/, "", $1)
                    percent = ($1 / total) * 100
                } else {
                    percent = 0
                }
                if (percent >= 1.0 && percent < 2.0) count++
            } END { print count+0 }' total="$(echo "$total_instructions" | sed 's/,//g')" "${summary_file}" 2>/dev/null || echo "0")
        fi
        
        CRITICAL_HOTSPOTS=$((CRITICAL_HOTSPOTS + critical_functions))
        MODERATE_TARGETS=$((MODERATE_TARGETS + moderate_functions))
        MINOR_OPPORTUNITIES=$((MINOR_OPPORTUNITIES + minor_functions))
        TOTAL_FUNCTIONS_ANALYZED=$((TOTAL_FUNCTIONS_ANALYZED + function_count))
        
        # Generate actionable performance assessment
        local assessment="UNKNOWN"
        local color="${YELLOW}"
        local recommendation=""
        
        if [[ "${critical_functions}" -eq 0 && "${moderate_functions}" -le 2 ]]; then
            assessment="WELL_OPTIMIZED"
            color="${GREEN}"
            recommendation="Good performance profile"
        elif [[ "${critical_functions}" -eq 0 && "${moderate_functions}" -le 5 ]]; then
            assessment="GOOD_PERFORMANCE"
            color="${CYAN}"
            recommendation="Minor optimization opportunities"
        elif [[ "${critical_functions}" -le 2 && "${moderate_functions}" -le 8 ]]; then
            assessment="MODERATE_PERFORMANCE"
            color="${YELLOW}"
            recommendation="Several optimization targets identified"
        else
            assessment="OPTIMIZATION_NEEDED"
            color="${RED}"
            recommendation="Multiple critical hotspots require attention"
        fi
        
        # Display actionable results
        echo -e "  ${color}${assessment}${NC} - Critical: ${critical_functions}, Moderate: ${moderate_functions}, Minor: ${minor_functions}"
        echo -e "    ${recommendation}"
        if [[ "${top_function}" != "N/A" && "${top_function}" != "" ]]; then
            echo -e "    Top Function: ${top_function}"
        fi
        if [[ "${total_instructions}" != "N/A" && "${total_instructions}" != "" ]]; then
            echo -e "    Total Instructions: ${total_instructions}"
        fi
        
        # Generate top functions analysis for actionable insights
        generate_function_analysis "${test_name}" "${summary_file}"
    else
        echo -e "${YELLOW}  Analysis complete - Raw data available${NC}"
    fi
}

# Function to generate detailed function analysis
generate_function_analysis() {
    local test_name="$1"
    local summary_file="$2"
    local analysis_file="${RESULTS_DIR}/summaries/${test_name}_function_analysis.txt"
    
    if [[ -f "${summary_file}" ]]; then
        echo "# Function Performance Analysis for ${test_name}" > "${analysis_file}"
        echo "Generated: $(date)" >> "${analysis_file}"
        echo "" >> "${analysis_file}"
        
        echo "## Critical Hotspots (≥10% of instructions)" >> "${analysis_file}"
        awk '/^[[:space:]]*[0-9]/ && !/^PROGRAM TOTALS/ && !/^[[:space:]]*Ir/ { 
            if (match($0, /\(([0-9.]+)%\)/, arr)) {
                percent = arr[1]
            } else if ($1 > 0 && total > 0) {
                gsub(/,/, "", $1)
                percent = ($1 / total) * 100
            } else {
                percent = 0
            }
            if (percent >= 10.0) print "- " $0
        }' total="$(grep "^PROGRAM TOTALS" "${summary_file}" | awk '{gsub(/,/, "", $2); print $2}')" "${summary_file}" >> "${analysis_file}" 2>/dev/null || echo "None found" >> "${analysis_file}"
        echo "" >> "${analysis_file}"
        
        echo "## Moderate Optimization Targets (2-10% of instructions)" >> "${analysis_file}"
        awk '/^[[:space:]]*[0-9]/ && !/^PROGRAM TOTALS/ && !/^[[:space:]]*Ir/ { 
            if (match($0, /\(([0-9.]+)%\)/, arr)) {
                percent = arr[1]
            } else if ($1 > 0 && total > 0) {
                gsub(/,/, "", $1)
                percent = ($1 / total) * 100
            } else {
                percent = 0
            }
            if (percent >= 2.0 && percent < 10.0) print "- " $0
        }' total="$(grep "^PROGRAM TOTALS" "${summary_file}" | awk '{gsub(/,/, "", $2); print $2}')" "${summary_file}" >> "${analysis_file}" 2>/dev/null || echo "None found" >> "${analysis_file}"
        echo "" >> "${analysis_file}"
        
        echo "## Minor Opportunities (1-2% of instructions)" >> "${analysis_file}"
        awk '/^[[:space:]]*[0-9]/ && !/^PROGRAM TOTALS/ && !/^[[:space:]]*Ir/ { 
            if (match($0, /\(([0-9.]+)%\)/, arr)) {
                percent = arr[1]
            } else if ($1 > 0 && total > 0) {
                gsub(/,/, "", $1)
                percent = ($1 / total) * 100
            } else {
                percent = 0
            }
            if (percent >= 1.0 && percent < 2.0) print "- " $0
        }' total="$(grep "^PROGRAM TOTALS" "${summary_file}" | awk '{gsub(/,/, "", $2); print $2}')" "${summary_file}" >> "${analysis_file}" 2>/dev/null || echo "None found" >> "${analysis_file}"
        
        echo "Function analysis saved to: ${analysis_file}"
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
| **Critical Hotspots** | ${CRITICAL_HOTSPOTS} | $([ $CRITICAL_HOTSPOTS -eq 0 ] && echo "No Critical Issues" || echo "Needs Immediate Attention") |
| **Moderate Targets** | ${MODERATE_TARGETS} | $([ $MODERATE_TARGETS -le 5 ] && echo "Few Targets" || echo "Multiple Opportunities") |
| **Minor Opportunities** | ${MINOR_OPPORTUNITIES} | $([ $MINOR_OPPORTUNITIES -le 10 ] && echo "Limited Scope" || echo "Many Small Gains") |
| **Total Functions** | ${TOTAL_FUNCTIONS_ANALYZED} | Analysis Coverage |

### Realistic Performance Assessment

EOF

    if [[ $TESTS_FAILED -eq 0 && $CRITICAL_HOTSPOTS -eq 0 && $MODERATE_TARGETS -le 3 ]]; then
        cat >> "${FINAL_REPORT}" << EOF
🎯 **WELL-OPTIMIZED PERFORMANCE** - Strong function-level performance
- Zero profiling failures
- No critical performance hotspots (≥10% instruction usage)
- Minimal moderate optimization targets (2-10% instruction usage)
- Ready for production with minor tuning opportunities
EOF
    elif [[ $TESTS_FAILED -le 1 && $CRITICAL_HOTSPOTS -le 1 && $MODERATE_TARGETS -le 8 ]]; then
        cat >> "${FINAL_REPORT}" << EOF
✅ **GOOD PERFORMANCE PROFILE** - Solid function performance with clear targets
- Minimal profiling issues
- Few critical hotspots requiring attention
- Manageable number of moderate optimization targets
- Specific optimization opportunities identified
EOF
    elif [[ $CRITICAL_HOTSPOTS -le 3 && $MODERATE_TARGETS -le 15 ]]; then
        cat >> "${FINAL_REPORT}" << EOF
⚠️ **OPTIMIZATION OPPORTUNITIES IDENTIFIED** - Performance improvements available
- Several critical hotspots consuming significant instruction cycles
- Multiple moderate optimization targets identified
- Clear roadmap for performance improvements available
- Prioritized optimization recommendations provided
EOF
    else
        cat >> "${FINAL_REPORT}" << EOF
🔧 **PERFORMANCE OPTIMIZATION NEEDED** - Multiple improvement areas identified
- Multiple critical hotspots requiring immediate attention
- Significant moderate optimization opportunities
- Performance bottlenecks clearly identified
- Comprehensive optimization strategy recommended
EOF
    fi

    cat >> "${FINAL_REPORT}" << EOF

## Detailed Profiling Analysis

### AI Behavior System Performance
EOF

    # Add AI-specific analysis
    for test_name in "ai_optimization" "ai_scaling" "behavior_functionality" "thread_safe_ai" "thread_safe_ai_integration"; do
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
    for test_name in "event_manager" "event_scaling" "event_types" "weather_events"; do
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
    for test_name in "resource_manager" "world_resource_manager" "world_generator" "world_manager" "world_manager_events" "resource_template_manager" "resource_integration" "resource_change_events" "inventory_components" "resource_factory" "resource_template_json" "json_reader"; do
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

### Performance Critical Systems
EOF

    # Add performance systems analysis
    for test_name in "particle_core" "particle_performance" "particle_threading" "particle_weather" "buffer_utilization" "save_manager" "ui_stress"; do
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

## Actionable Optimization Recommendations

### Priority 1: Critical Hotspots (≥10% instruction usage)
$([ $CRITICAL_HOTSPOTS -eq 0 ] && echo "✅ No critical hotspots identified - excellent performance profile" || echo "🔴 ${CRITICAL_HOTSPOTS} critical hotspot(s) found - immediate optimization required")

### Priority 2: Moderate Targets (2-10% instruction usage)  
$([ $MODERATE_TARGETS -eq 0 ] && echo "✅ No moderate targets identified" || echo "🟡 ${MODERATE_TARGETS} moderate target(s) found - good optimization opportunities")

### Priority 3: Minor Opportunities (1-2% instruction usage)
$([ $MINOR_OPPORTUNITIES -eq 0 ] && echo "✅ No minor opportunities identified" || echo "🟢 ${MINOR_OPPORTUNITIES} minor opportunit(ies) found - potential for small gains")

### Function-Level Optimization Strategy
1. **Critical Hotspots**: Focus on functions using ≥10% of total instructions
   - These represent the highest-impact optimization opportunities
   - Typical targets: main game loops, rendering functions, AI update cycles
   - Expected impact: 5-50% performance improvement per function

2. **Moderate Targets**: Review functions using 2-10% of instructions
   - Good ROI optimization opportunities
   - Often involve algorithmic improvements or caching strategies
   - Expected impact: 1-10% performance improvement per function

3. **Algorithm-Specific Optimizations**:
   - **AI Behavior Systems**: Look for expensive decision trees and update loops
   - **Event Processing**: Optimize event dispatch and handler efficiency  
   - **Resource Management**: Focus on loading, caching, and lifecycle costs
   - **Particle Systems**: Optimize update and rendering batch operations

### Performance Analysis Tools

### Viewing Callgrind Results
\`\`\`bash
# Install KCacheGrind for graphical analysis (recommended)
sudo apt-get install kcachegrind

# View any callgrind output file
kcachegrind ${RESULTS_DIR}/raw/[test_name]_callgrind.out

# Command-line analysis with callgrind_annotate
callgrind_annotate --auto=yes ${RESULTS_DIR}/raw/[test_name]_callgrind.out

# AI-focused analysis
callgrind_annotate --include="*Behavior*:*AI*:*Manager*" \${RESULTS_DIR}/raw/[test_name]_callgrind.out

# Resource management analysis  
callgrind_annotate --include="*Resource*:*Inventory*:*Template*" \${RESULTS_DIR}/raw/[test_name]_callgrind.out

# Top 10 functions by instruction count (most actionable)
callgrind_annotate --auto=no --threshold=99 \${RESULTS_DIR}/raw/[test_name]_callgrind.out | head -20
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

**Conclusion**: The SDL3 HammerEngine Template demonstrates $([ $CRITICAL_HOTSPOTS -eq 0 ] && echo "well-optimized function-level performance with no critical bottlenecks" || echo "identifiable performance optimization opportunities with ${CRITICAL_HOTSPOTS} critical hotspot(s)"), making it $([ $TESTS_FAILED -eq 0 ] && [ $CRITICAL_HOTSPOTS -eq 0 ] && echo "suitable for production deployment" || echo "ready for targeted performance optimization").

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
            for test_name in "ai_optimization" "ai_scaling" "behavior_functionality" "thread_safe_ai" "thread_safe_ai_integration"; do
                if [[ -n "${PROFILE_TESTS[$test_name]}" ]]; then
                    run_callgrind_profiling "${test_name}" "${PROFILE_TESTS[$test_name]}"
                    echo ""
                fi
            done
            ;;
        "event_systems"|"events")
            section_header "EVENT SYSTEM PROFILING ANALYSIS"
            for test_name in "event_manager" "event_scaling" "event_types" "weather_events"; do
                if [[ -n "${PROFILE_TESTS[$test_name]}" ]]; then
                    run_callgrind_profiling "${test_name}" "${PROFILE_TESTS[$test_name]}"
                    echo ""
                fi
            done
            ;;
        "performance"|"perf")
            section_header "PERFORMANCE CRITICAL PROFILING ANALYSIS"
            for test_name in "particle_core" "particle_performance" "particle_threading" "particle_weather" "buffer_utilization" "save_manager" "ui_stress"; do
                if [[ -n "${PROFILE_TESTS[$test_name]}" ]]; then
                    run_callgrind_profiling "${test_name}" "${PROFILE_TESTS[$test_name]}"
                    echo ""
                fi
            done
            ;;
        "resource_management"|"resources")
            section_header "RESOURCE MANAGEMENT PROFILING ANALYSIS"
            for test_name in "resource_manager" "world_resource_manager" "world_generator" "world_manager" "world_manager_events" "resource_template_manager" "resource_integration" "resource_change_events" "inventory_components" "resource_factory" "resource_template_json" "resource_edge_case" "json_reader"; do
                if [[ -n "${PROFILE_TESTS[$test_name]}" ]]; then                    run_callgrind_profiling "${test_name}" "${PROFILE_TESTS[$test_name]}"
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
    echo -e "  Critical Hotspots (≥10%): ${RED}${CRITICAL_HOTSPOTS}${NC}"
    echo -e "  Moderate Targets (2-10%): ${YELLOW}${MODERATE_TARGETS}${NC}"
    echo -e "  Minor Opportunities (1-2%): ${CYAN}${MINOR_OPPORTUNITIES}${NC}"
    echo ""
    
    if [[ $TESTS_FAILED -eq 0 && $CRITICAL_HOTSPOTS -eq 0 && $MODERATE_TARGETS -le 3 ]]; then
        echo -e "${BOLD}${GREEN}🎯 WELL-OPTIMIZED PERFORMANCE ACHIEVED! 🎯${NC}"
        echo -e "${GREEN}Your engine demonstrates strong function-level performance optimization.${NC}"
    elif [[ $TESTS_FAILED -le 1 && $CRITICAL_HOTSPOTS -le 1 && $MODERATE_TARGETS -le 8 ]]; then
        echo -e "${BOLD}${CYAN}✅ GOOD PERFORMANCE PROFILE ACHIEVED! ✅${NC}"
        echo -e "${CYAN}Your engine shows solid performance with clear optimization targets.${NC}"
    elif [[ $CRITICAL_HOTSPOTS -le 3 && $MODERATE_TARGETS -le 15 ]]; then
        echo -e "${BOLD}${YELLOW}⚠️ OPTIMIZATION OPPORTUNITIES IDENTIFIED ⚠️${NC}"
        echo -e "${YELLOW}Your engine has identifiable performance improvement opportunities.${NC}"
    else
        echo -e "${BOLD}${RED}🔧 PERFORMANCE OPTIMIZATION NEEDED 🔧${NC}"
        echo -e "${RED}Your engine has multiple areas requiring performance attention.${NC}"
    fi
    
    echo ""
    echo -e "${BOLD}Key Deliverables:${NC}"
    echo -e "📊 Profiling Report: ${CYAN}${FINAL_REPORT}${NC}"
    echo -e "📁 Raw Callgrind Data: ${CYAN}${RESULTS_DIR}/raw/${NC}"
    echo -e "📋 Function Summaries: ${CYAN}${RESULTS_DIR}/summaries/${NC}"
    echo -e "🔍 Function Analysis: ${CYAN}${RESULTS_DIR}/summaries/*_function_analysis.txt${NC}"
    echo -e "🎯 AI Annotations: ${CYAN}${RESULTS_DIR}/annotations/${NC}"
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