#include <chrono>
#include <fmod.hpp>
#include <iostream>
#include <thread>
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
void fmodTest() {
  // Initialize FMOD
  FMOD::System* system = nullptr;
  FMOD::System_Create(&system);
  system->init(512, FMOD_INIT_NORMAL, nullptr);

  // Load sound
  FMOD::Sound* sound = nullptr;
  system->createSound("res/sfx/level_complete.wav", FMOD_DEFAULT, nullptr, &sound);

  // Play sound
  FMOD::Channel* channel = nullptr;
  system->playSound(sound, nullptr, false, &channel);

  // Wait for sound to finish
  bool isPlaying = true;
  while (isPlaying) {
    channel->isPlaying(&isPlaying);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  // Clean up
  sound->release();
  system->close();
  system->release();
}

void fmodTest();
void simulateGameLoop();
