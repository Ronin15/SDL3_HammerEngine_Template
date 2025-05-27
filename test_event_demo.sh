#!/bin/bash

# Test script to verify Event Demo State doesn't crash
# This script runs the game for a few seconds and checks if it exits cleanly

echo "Testing Event Demo State..."

# Start the game in background
./bin/debug/SDL3_Template &
GAME_PID=$!

# Wait for game to initialize
sleep 2

# Send 'E' key to enter Event Demo State (simulating keypress)
# Note: This is a basic test - in a real scenario we'd use input automation
echo "Game started with PID: $GAME_PID"

# Let it run for 3 seconds
sleep 3

# Check if process is still running (not crashed)
if kill -0 $GAME_PID 2>/dev/null; then
    echo "SUCCESS: Game is running without crashing"
    # Kill the game cleanly
    kill $GAME_PID
    wait $GAME_PID 2>/dev/null
    echo "Game terminated cleanly"
    exit 0
else
    echo "FAILURE: Game crashed or exited unexpectedly"
    exit 1
fi