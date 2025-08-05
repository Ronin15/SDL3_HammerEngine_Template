#!/bin/bash

# Script to run the WorldResourceManager tests

# Set up colored output
RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m' # No Color

# Directory where this script is located
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Path to the test executable
TEST_EXECUTABLE="$SCRIPT_DIR/../../bin/debug/world_resource_manager_tests"

# Check if the test executable exists
if [ ! -f "$TEST_EXECUTABLE" ]; then
  echo -e "${RED}Test executable not found: $TEST_EXECUTABLE${NC}"
  echo -e "${RED}Please build the tests first.${NC}"
  exit 1
fi

# Run the test
$TEST_EXECUTABLE
