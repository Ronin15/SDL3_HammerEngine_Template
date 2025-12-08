/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include "managers/InputManager.hpp"
#include "core/Logger.hpp"
#include "SDL3/SDL_gamepad.h"
#include "SDL3/SDL_joystick.h"
#include "utils/Vector2D.hpp"
#include <memory>
#include <algorithm>

// Removed global pointer - now managed as member variable

InputManager::InputManager()
    : m_keystates(nullptr),
      m_mousePosition(std::make_unique<Vector2D>(0, 0)) {
  // Reserve capacity for performance optimization
  m_pressedThisFrame.reserve(16);  // Typical max keys pressed per frame
  m_joystickValues.reserve(4);     // Max 4 gamepads typically
  m_joysticks.reserve(4);          // Max 4 gamepads typically
  m_buttonStates.reserve(4);       // Max 4 gamepads typically
  m_mouseButtonStates.reserve(3);  // 3 mouse buttons

  // Create button states for the mouse
  for (int i = 0; i < 3; i++) {
    m_mouseButtonStates.push_back(false);
  }
  

}



void InputManager::initializeGamePad() {
  // Check if gamepad subsystem is already initialized
  if (m_gamePadInitialized) {
    return;
  }

  // Initialize gamepad subsystem
  if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD)) {
    INPUT_CRITICAL("Unable to initialize gamepad subsystem: " + std::string(SDL_GetError()));
    return;
  }
  
  // Mark that we successfully initialized the gamepad subsystem

  // Get all available gamepads with RAII management
  int numGamepads = 0;
  auto gamepadIDs = std::unique_ptr<SDL_JoystickID[], decltype(&SDL_free)>(
      SDL_GetGamepads(&numGamepads), SDL_free);

  if (!gamepadIDs) {
    INPUT_ERROR("Failed to get gamepad IDs: " + std::string(SDL_GetError()));
    return;
  }

  if (numGamepads > 0) {
    INPUT_INFO("Number of Game Pads detected: " + std::to_string(numGamepads));
    // Open all available gamepads
    for (int i = 0; i < numGamepads; i++) {
      if (SDL_IsGamepad(gamepadIDs[i])) {
        SDL_Gamepad* gamepad = SDL_OpenGamepad(gamepadIDs[i]);
        if (gamepad) {
          m_joysticks.push_back(gamepad);
          INPUT_INFO("Gamepad connected: " + std::string(SDL_GetGamepadName(gamepad)));

          // Add default joystick values
          m_joystickValues.push_back(std::make_pair(std::make_unique<Vector2D>(0, 0), std::make_unique<Vector2D>(0, 0)));

          // Add default button states for this joystick
          std::vector<bool> tempButtons;
          tempButtons.reserve(16);  // Reserve capacity for gamepad buttons
          for (int j = 0; j < SDL_GAMEPAD_BUTTON_COUNT; j++) {
            tempButtons.push_back(false);
          }
          m_buttonStates.push_back(tempButtons);
        } else {
          INPUT_ERROR("Could not open gamepad: " + std::string(SDL_GetError()));
          return;
        }
      }
    }
  } else {
    INPUT_INFO("No gamepads found");
    // Still need to quit the subsystem we initialized
    SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
    return; //return without setting m_gamePadInitialized to true.
  }

  m_gamePadInitialized = true;
}

void InputManager::reset() {
  m_mouseButtonStates[LEFT] = false;
  m_mouseButtonStates[RIGHT] = false;
  m_mouseButtonStates[MIDDLE] = false;
}

bool InputManager::isKeyDown(SDL_Scancode key) const {
  if (m_keystates != nullptr) {
    return m_keystates[key] == 1;
  }
  return false;
}

int InputManager::getAxisX(int joy, int stick) const {
  if (joy < 0 || joy >= static_cast<int>(m_joystickValues.size())) {
    return 0;
  }

  if (stick == 1) {
    return m_joystickValues[joy].first->getX();
  } else if (stick == 2) {
    return m_joystickValues[joy].second->getX();
  }

  return 0;
}

int InputManager::getAxisY(int joy, int stick) const {
  if (joy < 0 || joy >= static_cast<int>(m_joystickValues.size())) {
    return 0;
  }

  if (stick == 1) {
    return m_joystickValues[joy].first->getY();
  } else if (stick == 2) {
    return m_joystickValues[joy].second->getY();
  }

  return 0;
}

bool InputManager::getButtonState(int joy, int buttonNumber) const {
  if (joy < 0 || joy >= static_cast<int>(m_buttonStates.size()) ||
      buttonNumber < 0 ||
      buttonNumber >= static_cast<int>(m_buttonStates[joy].size())) {
    return false;
  }

  return m_buttonStates[joy][buttonNumber];
}

bool InputManager::getMouseButtonState(int buttonNumber) const {
  if (buttonNumber < 0 ||
      buttonNumber >= static_cast<int>(m_mouseButtonStates.size())) {
    return false;
  }

  return m_mouseButtonStates[buttonNumber];
}

const Vector2D& InputManager::getMousePosition() const {
  return *m_mousePosition;
}

bool InputManager::wasKeyPressed(SDL_Scancode key) const {
  // Check if this key was pressed this frame using std::any_of
  return std::any_of(m_pressedThisFrame.begin(), m_pressedThisFrame.end(),
                     [key](SDL_Scancode pressedKey) { return pressedKey == key; });
}

void InputManager::clearFrameInput() {
  // This is now handled automatically in update()
  // but keeping for backward compatibility
  m_pressedThisFrame.clear();
}



void InputManager::update() {
  // SDL event polling moved to GameEngine::handleEvents()
  // Keep this method for API consistency with other managers
  clearFrameInput();
}

void InputManager::onKeyDown(const SDL_Event& event) {
  // Store the keyboard state
  m_keystates = SDL_GetKeyboardState(0);

  // Track this key as pressed this frame (for wasKeyPressed)
  // Check for duplicates to avoid multiple entries for the same key in one frame
  bool alreadyTracked = std::any_of(m_pressedThisFrame.begin(), m_pressedThisFrame.end(),
                                   [scancode = event.key.scancode](SDL_Scancode pressedKey) {
                                     return pressedKey == scancode;
                                   });

  if (!alreadyTracked) {
    m_pressedThisFrame.push_back(event.key.scancode);
  }
}

void InputManager::onKeyUp(const SDL_Event& /*event*/) {
  // TODO may not be needed and need to clean upStore the keyboard state
  m_keystates = SDL_GetKeyboardState(0);

  // Key-specific processing can be handled by game states
  // using the isKeyDown() method
}

void InputManager::onMouseMove(const SDL_Event& event) {
  m_mousePosition->setX(event.motion.x);
  m_mousePosition->setY(event.motion.y);
}

void InputManager::onMouseButtonDown(const SDL_Event& event) {
  if (event.button.button == SDL_BUTTON_LEFT) {
    m_mouseButtonStates[LEFT] = true;
    INPUT_DEBUG("Mouse button Left clicked!");
  }
  if (event.button.button == SDL_BUTTON_MIDDLE) {
    m_mouseButtonStates[MIDDLE] = true;
    INPUT_DEBUG("Mouse button Middle clicked!");
  }
  if (event.button.button == SDL_BUTTON_RIGHT) {
    m_mouseButtonStates[RIGHT] = true;
    INPUT_DEBUG("Mouse button Right clicked!");
  }
}

void InputManager::onMouseButtonUp(const SDL_Event& event) {
  if (event.button.button == SDL_BUTTON_LEFT) {
    m_mouseButtonStates[LEFT] = false;
  }
  if (event.button.button == SDL_BUTTON_MIDDLE) {
    m_mouseButtonStates[MIDDLE] = false;
  }
  if (event.button.button == SDL_BUTTON_RIGHT) {
    m_mouseButtonStates[RIGHT] = false;
  }
}

void InputManager::onGamepadAxisMove(const SDL_Event& event) {
  int whichOne = 0;

  // Find which gamepad this event belongs to
  for (size_t i = 0; i < m_joysticks.size(); i++) {
    // In SDL3, use SDL_GetGamepadID directly
    if (SDL_GetGamepadID(m_joysticks[i]) == event.gaxis.which) {
      whichOne = static_cast<int>(i);
      break;
    }
  }

  // Make sure the gamepad index is valid
  if (whichOne >= static_cast<int>(m_joystickValues.size())) {
    return;
  }

  // Get axis name for debug messages
  std::string axisName = "Unknown";
  switch (event.gaxis.axis) {
    case 0: axisName = "Left Stick X"; break;
    case 1: axisName = "Left Stick Y"; break;
    case 2: axisName = "Right Stick X"; break;
    case 3: axisName = "Right Stick Y"; break;
    case 4: axisName = "Left Trigger"; break;
    case 5: axisName = "Right Trigger"; break;
    default: axisName = "Unknown Axis";
  }

  // Process left stick X-axis
  if (event.gaxis.axis == 0) {
    if (event.gaxis.value > m_joystickDeadZone) {
      m_joystickValues[whichOne].first->setX(1);
      INPUT_DEBUG("Gamepad " + std::to_string(whichOne) + " - " + axisName + " moving RIGHT!");
    } else if (event.gaxis.value < -m_joystickDeadZone) {
      m_joystickValues[whichOne].first->setX(-1);
      INPUT_DEBUG("Gamepad " + std::to_string(whichOne) + " - " + axisName + " moving LEFT!");
    } else {
      m_joystickValues[whichOne].first->setX(0);
    }
  }

  // Process left stick Y-axis
  if (event.gaxis.axis == 1) {
    if (event.gaxis.value > m_joystickDeadZone) {
      m_joystickValues[whichOne].first->setY(1);
      INPUT_DEBUG("Gamepad " + std::to_string(whichOne) + " - " + axisName + " moving DOWN!");
    } else if (event.gaxis.value < -m_joystickDeadZone) {
      m_joystickValues[whichOne].first->setY(-1);
      INPUT_DEBUG("Gamepad " + std::to_string(whichOne) + " - " + axisName + " moving UP!");
    } else {
      m_joystickValues[whichOne].first->setY(0);
    }
  }

  // Process right stick X-axis
  if (event.gaxis.axis == 2) {
    if (event.gaxis.value > m_joystickDeadZone) {
      m_joystickValues[whichOne].second->setX(1);
      INPUT_DEBUG("Gamepad " + std::to_string(whichOne) + " - " + axisName + " moving RIGHT!");
    } else if (event.gaxis.value < -m_joystickDeadZone) {
      m_joystickValues[whichOne].second->setX(-1);
      INPUT_DEBUG("Gamepad " + std::to_string(whichOne) + " - " + axisName + " moving LEFT!");
    } else {
      m_joystickValues[whichOne].second->setX(0);
    }
  }

  // Process right stick Y-axis
  if (event.gaxis.axis == 3) {
    if (event.gaxis.value > m_joystickDeadZone) {
      m_joystickValues[whichOne].second->setY(1);
      INPUT_DEBUG("Gamepad " + std::to_string(whichOne) + " - " + axisName + " moving DOWN!");
    } else if (event.gaxis.value < -m_joystickDeadZone) {
      m_joystickValues[whichOne].second->setY(-1);
      INPUT_DEBUG("Gamepad " + std::to_string(whichOne) + " - " + axisName + " moving UP!");
    } else {
      m_joystickValues[whichOne].second->setY(0);
    }
  }

  // Process left trigger (L2/LT)
  if (event.gaxis.axis == 4) {
    if (event.gaxis.value > m_joystickDeadZone) {
      INPUT_DEBUG("Gamepad " + std::to_string(whichOne) + " - " + axisName + " pressed: " + std::to_string(event.gaxis.value));
    }
  }

  // Process right trigger (R2/RT)
  if (event.gaxis.axis == 5) {
    if (event.gaxis.value > m_joystickDeadZone) {
      INPUT_DEBUG("Gamepad " + std::to_string(whichOne) + " - " + axisName + " pressed: " + std::to_string(event.gaxis.value));
    }
  }
}

void InputManager::onGamepadButtonDown(const SDL_Event& event) {
  int whichOne = 0;

  // Find which gamepad this event belongs to
  for (size_t i = 0; i < m_joysticks.size(); i++) {
    // In SDL3, use SDL_GetGamepadID directly
    if (SDL_GetGamepadID(m_joysticks[i]) == event.gdevice.which) {
      whichOne = static_cast<int>(i);
      break;
    }
  }

  // Make sure the gamepad and button indices are valid
  if (whichOne >= static_cast<int>(m_buttonStates.size()) ||
      event.gbutton.button >=
          static_cast<int>(m_buttonStates[whichOne].size())) {
    return;
  }

  m_buttonStates[whichOne][event.gbutton.button] = true;

  // Get button name for debug message
  std::string buttonName = "Unknown";
  switch (event.gbutton.button) {
    case 0: buttonName = "A or CROSS"; break;
    case 1: buttonName = "B or CIRCLE"; break;
    case 2: buttonName = "X or SQUARE"; break;
    case 3: buttonName = "Y or TRIANGLE"; break;
    case 4: buttonName = "Back"; break;
    case 5: buttonName = "Guide"; break;
    case 6: buttonName = "Start"; break;
    case 7: buttonName = "Left Stick"; break;
    case 8: buttonName = "Right Stick"; break;
    case 9: buttonName = "Left Shoulder"; break;
    case 10: buttonName = "Right Shoulder"; break;
    case 11: buttonName = "D-Pad Up"; break;
    case 12: buttonName = "D-Pad Down"; break;
    case 13: buttonName = "D-Pad Left"; break;
    case 14: buttonName = "D-Pad Right"; break;
    default: buttonName = "Unknown";
  }

  // Debug message for button press with button name
  INPUT_DEBUG("Gamepad " + std::to_string(whichOne) + " Button '" + buttonName + "' (" +
              std::to_string(static_cast<int>(event.gbutton.button)) + ") pressed!");
}

void InputManager::onGamepadButtonUp(const SDL_Event& event) {
  int whichOne = 0;

  // Find which gamepad this event belongs to
  for (size_t i = 0; i < m_joysticks.size(); i++) {
    // In SDL3, use SDL_GetGamepadID directly
    if (SDL_GetGamepadID(m_joysticks[i]) == event.gdevice.which) {
      whichOne = static_cast<int>(i);
      break;
    }
  }

  // Make sure the gamepad and button indices are valid
  if (whichOne >= static_cast<int>(m_buttonStates.size()) ||
      event.gbutton.button >=
          static_cast<int>(m_buttonStates[whichOne].size())) {
    return;
  }

  m_buttonStates[whichOne][event.gbutton.button] = false;
}

void InputManager::clean() {
  if (m_isShutdown) {
    return;
  }

  if(m_gamePadInitialized) {
    int gamepadCount{0};
    // Close all gamepads if detected
    for (auto& gamepad : m_joysticks) {
      if (gamepad) {
        SDL_CloseGamepad(gamepad);
        gamepadCount++;
      }
    }

    m_joysticks.clear();
    m_joystickValues.clear();
    m_gamePadInitialized = false;
    SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
    INPUT_INFO(std::to_string(gamepadCount) + " gamepads freed");

  } else {
    INPUT_INFO("No gamepads to free");
  }

  // Clear all button states and mouse states
  m_buttonStates.clear();
  m_mouseButtonStates.clear();

  // Set shutdown flag
  m_isShutdown = true;
  INPUT_INFO("InputManager resources cleaned");
}
