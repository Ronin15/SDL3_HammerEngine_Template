#!/bin/bash

# AI Scaling Benchmark Analysis Script - Enhanced Dynamic Version
# Analyzes performance data from benchmark results with interactive features

echo "============================================"
echo "AI SCALING BENCHMARK ANALYSIS TOOL"
echo "============================================"
echo ""

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
PURPLE='\033[0;35m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Function to print colored output
print_colored() {
    local color=$1
    local text=$2
    echo -e "${color}${text}${NC}"
}

# Function to display usage
show_usage() {
    echo "Usage: $0 [OPTIONS] [FILES...]"
    echo ""
    echo "OPTIONS:"
    echo "  -h, --help              Show this help message"
    echo "  -i, --interactive       Interactive mode"
    echo "  -c, --compare FILES     Compare multiple files"
    echo "  -s, --summary          Show summary only"
    echo "  -t, --trend            Show performance trends"
    echo "  -f, --filter PATTERN   Filter by entity count pattern"
    echo "  -o, --output FILE      Save results to file"
    echo ""
    echo "EXAMPLES:"
    echo "  $0                                    # Analyze all files in test_results/"
    echo "  $0 -i                                # Interactive mode"
    echo "  $0 -c file1.txt file2.txt           # Compare two files"
    echo "  $0 -f \"5000\"                        # Show only 5000 entity results"
    echo "  $0 -t                                # Show performance trends over time"
    echo ""
}

# Function to find benchmark files
find_benchmark_files() {
    local pattern="$1"
    local test_results_dir=""

    # Determine test_results directory location
    if [ -d "test_results" ]; then
        test_results_dir="test_results"
    elif [ -d "../test_results" ]; then
        test_results_dir="../test_results"
    else
        return 1
    fi

    if [ -n "$pattern" ]; then
        find "$test_results_dir" -name "*ai_scaling_benchmark*${pattern}*.txt" 2>/dev/null | sort
    else
        find "$test_results_dir" -name "*ai_scaling_benchmark*.txt" 2>/dev/null | sort
    fi
}

# Function to extract performance data from a specific test case
extract_performance_data() {
    local file="$1"
    local entity_count="$2"
    local test_name="$3"

    if [ ! -f "$file" ]; then
        return 1
    fi

    # Extract the performance line for this entity count
    local result=$(grep -A 10 -B 5 "${entity_count} entities" "$file" 2>/dev/null | grep "Entity updates per second:" | head -1)
    if [ -n "$result" ]; then
        local updates_per_sec=$(echo "$result" | grep -o '[0-9]\+')
        local behavior_updates=$(grep -A 15 "${entity_count} entities" "$file" 2>/dev/null | grep "Total behavior updates:" | head -1 | grep -o '[0-9]\+')
        local filename=$(basename "$file")
        local date_time=$(echo "$filename" | grep -o '[0-9]\{8\}_[0-9]\{6\}' | head -1)

        if [ -z "$date_time" ]; then
            date_time="unknown"
        fi

        echo "$date_time,$entity_count,$updates_per_sec,$behavior_updates,$test_name,$filename"
    fi
}

# Function to calculate expected behavior updates
calculate_expected_behaviors() {
    local entities=$1
    local updates_per_run=$2
    local num_runs=3

    # Determine updates per run based on entity count
    case $entities in
        100000) updates_per_run=10 ;;
        *) updates_per_run=20 ;;
    esac

    echo $((entities * updates_per_run * num_runs))
}

# Function to analyze single file
analyze_file() {
    local file="$1"
    local show_details="${2:-true}"

    if [ ! -f "$file" ]; then
        print_colored $RED "Error: File '$file' not found"
        return 1
    fi

    if [ "$show_details" = "true" ]; then
        print_colored $BLUE "\n=== Analyzing: $(basename $file) ==="
    fi

    # Extract data for different entity counts
    local entity_counts=("150" "200" "1000" "5000" "100000")
    local found_data=false

    for count in "${entity_counts[@]}"; do
        local data=$(extract_performance_data "$file" "$count" "Test_${count}")
        if [ -n "$data" ]; then
            found_data=true
            if [ "$show_details" = "true" ]; then
                local updates_per_sec=$(echo "$data" | cut -d',' -f3)
                local behavior_updates=$(echo "$data" | cut -d',' -f4)
                local expected=$(calculate_expected_behaviors "$count")
                local completion_pct=$((behavior_updates * 100 / expected))

                printf "  %-8s entities: %'12d updates/sec, %'8d behaviors (%d%% completion)\n" \
                    "$count" "$updates_per_sec" "$behavior_updates" "$completion_pct"
            else
                echo "$data"
            fi
        fi
    done

    if [ "$found_data" = "false" ] && [ "$show_details" = "true" ]; then
        print_colored $YELLOW "  No benchmark data found in this file"
    fi
}

# Function to compare multiple files
compare_files() {
    local files=("$@")

    print_colored $CYAN "\n=== PERFORMANCE COMPARISON ==="
    printf "%-12s" "Entity Count"
    for file in "${files[@]}"; do
        printf " | %-15s" "$(basename "$file" | cut -c1-15)"
    done
    echo ""

    # Print separator
    printf "%-12s" "============"
    for file in "${files[@]}"; do
        printf " | %-15s" "==============="
    done
    echo ""

    # Compare each entity count
    local entity_counts=("150" "200" "1000" "5000" "100000")
    for count in "${entity_counts[@]}"; do
        printf "%-12s" "${count}"
        for file in "${files[@]}"; do
            local data=$(extract_performance_data "$file" "$count" "Test")
            if [ -n "$data" ]; then
                local updates_per_sec=$(echo "$data" | cut -d',' -f3)
                printf " | %'13d" "$updates_per_sec"
            else
                printf " | %-15s" "N/A"
            fi
        done
        echo ""
    done
}

# Function to show performance trends
show_trends() {
    local files=("$@")

    print_colored $CYAN "\n=== PERFORMANCE TRENDS OVER TIME ==="

    # Create temporary file for data
    local temp_file=$(mktemp)

    # Extract all data
    for file in "${files[@]}"; do
        analyze_file "$file" false >> "$temp_file"
    done

    # Sort by timestamp and entity count
    sort -t',' -k1,1 -k2,2n "$temp_file" > "${temp_file}.sorted"

    # Group by entity count and show trends
    local entity_counts=("150" "200" "1000" "5000" "100000")
    for count in "${entity_counts[@]}"; do
        echo ""
        print_colored $YELLOW "=== ${count} Entities Trend ==="
        grep ",${count}," "${temp_file}.sorted" | while IFS=',' read -r timestamp entities updates behaviors test_name filename; do
            local expected=$(calculate_expected_behaviors "$entities")
            local completion_pct=$((behaviors * 100 / expected))
            printf "  %-15s: %'12d updates/sec (%d%% completion) - %s\n" \
                "$timestamp" "$updates" "$completion_pct" "$filename"
        done
    done

    # Clean up
    rm -f "$temp_file" "${temp_file}.sorted"
}

# Function for interactive mode
interactive_mode() {
    while true; do
        echo ""
        print_colored $CYAN "=== INTERACTIVE MODE ==="
        echo "1. Analyze all files"
        echo "2. Analyze specific file"
        echo "3. Compare files"
        echo "4. Show trends"
        echo "5. Filter by entity count"
        echo "6. Search files by pattern"
        echo "7. Show file list"
        echo "8. Exit"
        echo ""
        read -p "Select option (1-8): " choice

        case $choice in
            1)
                local files=($(find_benchmark_files))
                if [ ${#files[@]} -eq 0 ]; then
                    print_colored $RED "No benchmark files found"
                else
                    for file in "${files[@]}"; do
                        analyze_file "$file"
                    done
                fi
                ;;
            2)
                echo ""
                read -p "Enter filename (or pattern): " filename
                local matching_files=($(find_benchmark_files "$filename"))
                if [ ${#matching_files[@]} -eq 0 ]; then
                    print_colored $RED "No files found matching '$filename'"
                else
                    for file in "${matching_files[@]}"; do
                        analyze_file "$file"
                    done
                fi
                ;;
            3)
                echo ""
                read -p "Enter filenames (space-separated): " filenames
                local files_array=($filenames)
                if [ ${#files_array[@]} -lt 2 ]; then
                    print_colored $RED "Need at least 2 files to compare"
                else
                    compare_files "${files_array[@]}"
                fi
                ;;
            4)
                local files=($(find_benchmark_files))
                if [ ${#files[@]} -eq 0 ]; then
                    print_colored $RED "No benchmark files found"
                else
                    show_trends "${files[@]}"
                fi
                ;;
            5)
                echo ""
                read -p "Enter entity count to filter by: " entity_filter
                local files=($(find_benchmark_files))
                for file in "${files[@]}"; do
                    local data=$(extract_performance_data "$file" "$entity_filter" "Filtered")
                    if [ -n "$data" ]; then
                        analyze_file "$file"
                    fi
                done
                ;;
            6)
                echo ""
                read -p "Enter search pattern: " pattern
                local matching_files=($(find_benchmark_files "$pattern"))
                if [ ${#matching_files[@]} -eq 0 ]; then
                    print_colored $RED "No files found matching '$pattern'"
                else
                    print_colored $GREEN "Found ${#matching_files[@]} files:"
                    for file in "${matching_files[@]}"; do
                        echo "  $(basename "$file")"
                    done
                fi
                ;;
            7)
                local files=($(find_benchmark_files))
                print_colored $GREEN "Available benchmark files:"
                for file in "${files[@]}"; do
                    echo "  $(basename "$file")"
                done
                ;;
            8)
                print_colored $GREEN "Goodbye!"
                exit 0
                ;;
            *)
                print_colored $RED "Invalid option. Please select 1-8."
                ;;
        esac
    done
}

# Parse command line arguments
INTERACTIVE=false
COMPARE_MODE=false
SUMMARY_ONLY=false
SHOW_TRENDS=false
ENTITY_FILTER=""
OUTPUT_FILE=""
FILES_TO_ANALYZE=()

while [[ $# -gt 0 ]]; do
    case $1 in
        -h|--help)
            show_usage
            exit 0
            ;;
        -i|--interactive)
            INTERACTIVE=true
            shift
            ;;
        -c|--compare)
            COMPARE_MODE=true
            shift
            ;;
        -s|--summary)
            SUMMARY_ONLY=true
            shift
            ;;
        -t|--trend)
            SHOW_TRENDS=true
            shift
            ;;
        -f|--filter)
            ENTITY_FILTER="$2"
            shift 2
            ;;
        -o|--output)
            OUTPUT_FILE="$2"
            shift 2
            ;;
        -*)
            print_colored $RED "Unknown option: $1"
            show_usage
            exit 1
            ;;
        *)
            FILES_TO_ANALYZE+=("$1")
            shift
            ;;
    esac
done

# Redirect output if specified
if [ -n "$OUTPUT_FILE" ]; then
    exec > >(tee "$OUTPUT_FILE")
fi

# Main execution logic
if [ "$INTERACTIVE" = "true" ]; then
    interactive_mode
elif [ "$COMPARE_MODE" = "true" ]; then
    if [ ${#FILES_TO_ANALYZE[@]} -lt 2 ]; then
        print_colored $RED "Error: Compare mode requires at least 2 files"
        exit 1
    fi
    compare_files "${FILES_TO_ANALYZE[@]}"
elif [ "$SHOW_TRENDS" = "true" ]; then
    if [ ${#FILES_TO_ANALYZE[@]} -eq 0 ]; then
        FILES_TO_ANALYZE=($(find_benchmark_files))
    fi
    show_trends "${FILES_TO_ANALYZE[@]}"
else
    # Default mode: analyze specified files or all files
    if [ ${#FILES_TO_ANALYZE[@]} -eq 0 ]; then
        FILES_TO_ANALYZE=($(find_benchmark_files "$ENTITY_FILTER"))
        if [ ${#FILES_TO_ANALYZE[@]} -eq 0 ]; then
            if [ ! -d "test_results" ] && [ ! -d "../test_results" ]; then
                print_colored $RED "Error: test_results directory not found (checked current dir and parent dir)"
                exit 1
            fi
        fi
    fi

    if [ ${#FILES_TO_ANALYZE[@]} -eq 0 ]; then
        print_colored $RED "No benchmark files found"
        exit 1
    fi

    print_colored $GREEN "Found ${#FILES_TO_ANALYZE[@]} benchmark files to analyze"

    for file in "${FILES_TO_ANALYZE[@]}"; do
        if [ "$SUMMARY_ONLY" = "true" ]; then
            analyze_file "$file" false
        else
            analyze_file "$file" true
        fi
    done

    # Show summary statistics
    if [ ${#FILES_TO_ANALYZE[@]} -gt 1 ] && [ "$SUMMARY_ONLY" != "true" ]; then
        echo ""
        print_colored $CYAN "=== SUMMARY STATISTICS ==="

        # Calculate averages for each entity count
        entity_counts=("150" "200" "1000" "5000" "100000")
        for count in "${entity_counts[@]}"; do
            total=0
            file_count=0

            for file in "${FILES_TO_ANALYZE[@]}"; do
                data=$(extract_performance_data "$file" "$count" "Summary")
                if [ -n "$data" ]; then
                    updates_per_sec=$(echo "$data" | cut -d',' -f3)
                    total=$((total + updates_per_sec))
                    file_count=$((file_count + 1))
                fi
            done

            if [ $file_count -gt 0 ]; then
                average=$((total / file_count))
                printf "  %-8s entities: %'12d avg updates/sec (%d files)\n" \
                    "$count" "$average" "$file_count"
            fi
        done
    fi
fi

print_colored $GREEN "\nAnalysis complete!"
