#ifndef TESTS_HPP
#define TESTS_HPP

// Function to test SDL3_mixer audio capabilities
// Initializes SDL audio and SDL3_mixer, loads and plays a WAV file,
// and cleans up resources when finished
void audioTest();

// Function to simulate the game loop
// Creates state managers, adds various states, and simulates
// state transitions in a typical game flow
void simulateGameLoop();

#endif // TESTS_HPP