#!/bin/bash

# Backward compatibility wrapper for run_all_tests.sh
# This script has been moved to tests/test_scripts/run_all_tests.sh

echo "NOTE: Test scripts have been moved to tests/test_scripts/"
echo "Redirecting to tests/test_scripts/run_all_tests.sh..."
echo

# Get the directory where this script is located
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Execute the actual script in its new location
exec "$SCRIPT_DIR/tests/test_scripts/run_all_tests.sh" "$@"