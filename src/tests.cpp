#include <chrono>
#include <iostream>
#include <thread>
#include <SDL3/SDL.h>
#include <SDL3_mixer/SDL_mixer.h>
#include "EntityIdleState.hpp"
#include "EntityJumpingState.hpp"
#include "EntityRunningState.hpp"
#include "EntityStateManager.hpp"
#include "EntityWalkingState.hpp"
#include "GamePlayState.hpp"
#include "GameStateManager.hpp"
#include "MainMenuState.hpp"
#include "PauseState.hpp"

// Simulated game loop
void simulateGameLoop() {
  // Create state manager
  GameStateManager gameStateManager;
  EntityStateManager entityStateManager;

  // Add all possible states
  gameStateManager.addState(std::make_unique<MainMenuState>());
  gameStateManager.addState(std::make_unique<GamePlayState>());
  gameStateManager.addState(std::make_unique<PauseState>());

  // add all possible entitiy states
  entityStateManager.addState("Idle", std::make_unique<EntityIdleState>());
  entityStateManager.addState("Walking", std::make_unique<EntityWalkingState>());
  entityStateManager.addState("Running", std::make_unique<EntityRunningState>());
  entityStateManager.addState("Jumping", std::make_unique<EntityJumpingState>());

  // Simulate game flow
  std::cout << "Starting game simulation..." << std::endl;

  // Start in Main Menu
  gameStateManager.setState("MainMenuState");
  gameStateManager.update();
  std::this_thread::sleep_for(std::chrono::seconds(2));

  // Transition to Gameplay
  gameStateManager.setState("GamePlayState");
  gameStateManager.update();
  std::this_thread::sleep_for(std::chrono::seconds(2));
  entityStateManager.setState("Idle");
  entityStateManager.update();
  std::this_thread::sleep_for(std::chrono::seconds(2));
  entityStateManager.setState("Walking");
  entityStateManager.update();
  std::this_thread::sleep_for(std::chrono::seconds(2));
  entityStateManager.setState("Running");
  entityStateManager.update();
  std::this_thread::sleep_for(std::chrono::seconds(2));
  entityStateManager.setState("Jumping");
  entityStateManager.update();
  std::this_thread::sleep_for(std::chrono::seconds(3));

  // Pause the game
  gameStateManager.setState("PauseState");
  gameStateManager.update();
  std::this_thread::sleep_for(std::chrono::seconds(2));

  // Resume gameplay
  gameStateManager.setState("GamePlayState");
  gameStateManager.update();
  std::this_thread::sleep_for(std::chrono::seconds(2));
  entityStateManager.setState("Idle");
  entityStateManager.update();
  std::this_thread::sleep_for(std::chrono::seconds(2));
  entityStateManager.setState("Walking");
  entityStateManager.update();
  std::this_thread::sleep_for(std::chrono::seconds(2));
  entityStateManager.setState("Running");
  entityStateManager.update();
  std::this_thread::sleep_for(std::chrono::seconds(2));
  entityStateManager.setState("Jumping");
  entityStateManager.update();
  std::this_thread::sleep_for(std::chrono::seconds(3));

  // Return to main menu
  gameStateManager.setState("MainMenuState");
  gameStateManager.update();
  std::this_thread::sleep_for(std::chrono::seconds(2));

  // Try changing to a non-existent state to test error handling
  try {
    gameStateManager.setState("NonExistentState");
  } catch (const std::exception& e) {
    std::cerr << "Error changing state: " << e.what() << std::endl;
  }

  // Remove a state and verify
  std::cout << "Removing PauseState..." << std::endl;
  gameStateManager.removeState("PauseState");

  // Verify state removal
  if (!gameStateManager.hasState("PauseState")) {
    std::cout << "PauseState successfully removed." << std::endl;
  }

  // Final state cleanup
  gameStateManager.clearAllStates();
  std::cout << "Game simulation complete." << std::endl;
}
void audioTest() {
  // Initialize SDL3_mixer
  if (!SDL_Init(SDL_INIT_AUDIO)) {
    std::cerr << "Error initializing SDL Audio: " << SDL_GetError() << std::endl;
    return;
  }

  // Initialize SDL3_mixer with default settings
  // Mix_Init returns a MIX_InitFlags (Uint32) of which flags were initialized
  MIX_InitFlags initFlags = Mix_Init(MIX_INIT_MP3 | MIX_INIT_OGG);
  // Check if any requested formats were initialized
  if (initFlags == 0) {
    std::cerr << "Error initializing SDL_mixer: " << SDL_GetError() << std::endl;
    SDL_Quit();
    return;
  }

  // Open audio device
  SDL_AudioSpec desired_spec;
  SDL_zero(desired_spec);
  desired_spec.freq = 44100;
  desired_spec.format = SDL_AUDIO_F32;
  desired_spec.channels = 2;

  SDL_AudioDeviceID deviceId = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &desired_spec);
  if (!deviceId) {
    std::cerr << "Error opening audio device: " << SDL_GetError() << std::endl;
    Mix_Quit();
    SDL_Quit();
    return;
  }

  // Initialize SDL_mixer with the opened audio device
  if (!Mix_OpenAudio(deviceId, &desired_spec)) {
    std::cerr << "Error initializing SDL_mixer: " << SDL_GetError() << std::endl;
    SDL_CloseAudioDevice(deviceId);
    Mix_Quit();
    SDL_Quit();
    return;
  }

  std::cout << "SDL3_mixer initialized successfully" << std::endl;

  // Load sound file
  Mix_Chunk* sound = Mix_LoadWAV("res/sfx/level_complete.wav");
  if (!sound) {
    std::cerr << "Error loading sound: " << SDL_GetError() << std::endl;
    Mix_CloseAudio();
    Mix_Quit();
    SDL_Quit();
    return;
  }

  std::cout << "Playing sound..." << std::endl;

  // Play sound
  int channel = Mix_PlayChannel(-1, sound, 0);
  if (channel == -1) {
    std::cerr << "Error playing sound: " << SDL_GetError() << std::endl;
    Mix_FreeChunk(sound);
    Mix_CloseAudio();
    Mix_Quit();
    SDL_Quit();
    return;
  }

  // Wait for sound to finish
  while (Mix_Playing(channel)) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  std::cout << "Sound playback complete" << std::endl;

  // Clean up
  Mix_FreeChunk(sound);
  Mix_CloseAudio();
  Mix_Quit();
  SDL_CloseAudioDevice(deviceId);
  SDL_Quit();
}

void audioTest();
void simulateGameLoop();
