#!/bin/bash
# PreToolUse hook to prevent concurrent ninja/cmake builds
# Waits up to 5 minutes for existing builds to complete

set -e

# Log file for debugging
LOG_FILE="/tmp/build_guard.log"

# Configuration
MAX_WAIT=300  # 5 minutes in seconds
CHECK_INTERVAL=2  # Check every 2 seconds
PROGRESS_INTERVAL=10  # Report progress every 10 seconds

# Extract command from stdin JSON
COMMAND=$(jq -r '.tool_input.command // ""')

# Log the check
echo "[$(date '+%Y-%m-%d %H:%M:%S')] Checking command: $COMMAND" >> "$LOG_FILE"

# Only check ninja and cmake commands
if ! echo "$COMMAND" | grep -qE '^(ninja|cmake)'; then
    echo "[$(date '+%Y-%m-%d %H:%M:%S')] Not a build command, allowing" >> "$LOG_FILE"
    exit 0
fi

echo "[$(date '+%Y-%m-%d %H:%M:%S')] Build command detected, checking for running processes" >> "$LOG_FILE"

# Function to check if build processes are running
check_running_builds() {
    local ninja_count=0
    local cmake_count=0

    # Check for ninja processes in the build directory
    # Exclude pgrep itself and build_guard.sh invocations to avoid false positives
    ninja_count=$(pgrep -f "ninja -C build" -a 2>/dev/null | \
                  grep -v "pgrep" | \
                  grep -v "build_guard.sh" | \
                  wc -l)

    # Check for cmake processes targeting the build directory
    # Exclude pgrep itself and build_guard.sh invocations to avoid false positives
    cmake_count=$(pgrep -f "cmake -B build" -a 2>/dev/null | \
                  grep -v "pgrep" | \
                  grep -v "build_guard.sh" | \
                  wc -l)

    echo $((ninja_count + cmake_count))
}

# Initial check
RUNNING=$(check_running_builds)

if [ "$RUNNING" -eq 0 ]; then
    echo "[$(date '+%Y-%m-%d %H:%M:%S')] No builds running, allowing command" >> "$LOG_FILE"
    echo "Build guard: No concurrent builds detected. Proceeding." >&2
    exit 0
fi

# Build(s) detected, start waiting
echo "[$(date '+%Y-%m-%d %H:%M:%S')] Found $RUNNING build process(es), starting wait loop" >> "$LOG_FILE"
echo "Build guard: Detected $RUNNING running build process(es). Waiting for completion..." >&2

ELAPSED=0
LAST_PROGRESS=0

while [ $ELAPSED -lt $MAX_WAIT ]; do
    RUNNING=$(check_running_builds)

    if [ "$RUNNING" -eq 0 ]; then
        echo "[$(date '+%Y-%m-%d %H:%M:%S')] Builds completed after ${ELAPSED}s, allowing command" >> "$LOG_FILE"
        echo "Build guard: Build(s) completed after ${ELAPSED}s. Proceeding with your command." >&2
        exit 0
    fi

    # Show progress every PROGRESS_INTERVAL seconds
    if [ $((ELAPSED - LAST_PROGRESS)) -ge $PROGRESS_INTERVAL ]; then
        REMAINING=$((MAX_WAIT - ELAPSED))
        echo "Build guard: Still waiting... ($RUNNING process(es) running, ${REMAINING}s remaining)" >&2
        echo "[$(date '+%Y-%m-%d %H:%M:%S')] Still waiting: $RUNNING process(es) running, ${ELAPSED}s elapsed" >> "$LOG_FILE"
        LAST_PROGRESS=$ELAPSED
    fi

    sleep $CHECK_INTERVAL
    ELAPSED=$((ELAPSED + CHECK_INTERVAL))
done

# Timeout reached
echo "[$(date '+%Y-%m-%d %H:%M:%S')] Timeout reached, $RUNNING process(es) still running, blocking command" >> "$LOG_FILE"
echo "Build guard: Timeout reached after ${MAX_WAIT}s. Build process(es) still running." >&2
echo "Build guard: Please wait for the existing build to complete before starting a new one." >&2
exit 2
