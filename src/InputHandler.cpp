#include "InputHandler.hpp"
#include <iostream>
#include "GameEngine.hpp"
#include "SDL3/SDL_gamepad.h"
#include "SDL3/SDL_joystick.h"
#include "Vector2D.hpp"

InputHandler* InputHandler::sp_Instance{nullptr};  // Initialize static instance
SDL_JoystickID* gamepadIDs{nullptr};

InputHandler::InputHandler()
    : m_keystates(nullptr),
      m_gamePadInitialized(false),
      m_mousePosition(new Vector2D(0, 0)) {
  // Create button states for the mouse
  for (int i = 0; i < 3; i++) {
    m_mouseButtonStates.push_back(false);
  }
}

InputHandler::~InputHandler() {
  // Clean up allocated memory
  delete m_mousePosition;

  // Clear all vectors
  m_joystickValues.clear();
  m_joysticks.clear();
  m_buttonStates.clear();
  m_mouseButtonStates.clear();
}

void InputHandler::initializeGamePad() {
  // Check if gamepad subsystem is already initialized
  if (m_gamePadInitialized) {
    return;
  }

  // Get all available gamepads
  int numGamepads = 0;
  SDL_JoystickID* gamepadIDs = SDL_GetGamepads(&numGamepads);

  if (gamepadIDs == nullptr) {
    std::cerr << "Forge Game Engine - Failed to get gamepad IDs: "
              << SDL_GetError() << std::endl;
    return;
  }

  if (numGamepads > 0) {
    std::cout << "Forge Engine - Number of Game Pads detected: " << numGamepads << std::endl;
    // Open all available gamepads
    for (int i = 0; i < numGamepads; i++) {
      if (SDL_IsGamepad(gamepadIDs[i])) {
        SDL_Gamepad* gamepad = SDL_OpenGamepad(gamepadIDs[i]);
        if (gamepad) {
          m_joysticks.push_back(gamepad);
          std::cout << "Forge Engine - Gamepad opened: " << SDL_GetGamepadName(gamepad) << std::endl;

          // Add default joystick values
          m_joystickValues.push_back(std::make_pair(new Vector2D(0, 0), new Vector2D(0, 0)));

          // Add default button states for this joystick
          std::vector<bool> tempButtons;
          for (int j = 0; j < SDL_GAMEPAD_BUTTON_COUNT; j++) {
            tempButtons.push_back(false);
          }
          m_buttonStates.push_back(tempButtons);
        } else {
          std::cerr << "Forge Engine - Could not open gamepad: " << SDL_GetError() << std::endl;
        }
      }
    }
  } else {
    std::cout << "Forge Engine - No gamepads connected." << std::endl;
  }

  m_gamePadInitialized = true;
}

void InputHandler::reset() {
  m_mouseButtonStates[LEFT] = false;
  m_mouseButtonStates[RIGHT] = false;
  m_mouseButtonStates[MIDDLE] = false;
}

bool InputHandler::isKeyDown(SDL_Scancode key) const {
  if (m_keystates != nullptr) {
    return m_keystates[key] == 1;
  }
  return false;
}

int InputHandler::getAxisX(int joy, int stick) const {
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

int InputHandler::getAxisY(int joy, int stick) const {
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

bool InputHandler::getButtonState(int joy, int buttonNumber) const {
  if (joy < 0 || joy >= static_cast<int>(m_buttonStates.size()) ||
      buttonNumber < 0 ||
      buttonNumber >= static_cast<int>(m_buttonStates[joy].size())) {
    return false;
  }

  return m_buttonStates[joy][buttonNumber];
}

bool InputHandler::getMouseButtonState(int buttonNumber) const {
  if (buttonNumber < 0 ||
      buttonNumber >= static_cast<int>(m_mouseButtonStates.size())) {
    return false;
  }

  return m_mouseButtonStates[buttonNumber];
}

Vector2D* InputHandler::getMousePosition() const {
  return m_mousePosition;
}

void InputHandler::update() {
  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    switch (event.type) {
      case SDL_EVENT_QUIT:
        std::cout << "Forge Engine - Shutting down! Forge Stopping {}===]>\n";
        GameEngine::Instance()->setRunning(false);
        break;

      case SDL_EVENT_GAMEPAD_AXIS_MOTION:
        onGamepadAxisMove(event);
        break;

      case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        onGamepadButtonDown(event);
        break;

      case SDL_EVENT_GAMEPAD_BUTTON_UP:
        onGamepadButtonUp(event);
        break;

      case SDL_EVENT_MOUSE_MOTION:
        onMouseMove(event);
        break;

      case SDL_EVENT_MOUSE_BUTTON_DOWN:
        onMouseButtonDown(event);
        break;

      case SDL_EVENT_MOUSE_BUTTON_UP:
        onMouseButtonUp(event);
        break;

      case SDL_EVENT_KEY_DOWN:
        onKeyDown(event);
        break;

      case SDL_EVENT_KEY_UP:
        onKeyUp(event);
        break;

      default:
        break;
    }
  }
}

void InputHandler::onKeyDown(SDL_Event& /*event*/) {
  // Store the keyboard state
  m_keystates = SDL_GetKeyboardState(0);

}

void InputHandler::onKeyUp(SDL_Event& /*event*/) {
  // Store the keyboard state
  m_keystates = SDL_GetKeyboardState(0);

  // Key-specific processing can be handled by game states
  // using the isKeyDown() method
}

void InputHandler::onMouseMove(SDL_Event& event) {
  m_mousePosition->setX(event.motion.x);
  m_mousePosition->setY(event.motion.y);
}

void InputHandler::onMouseButtonDown(SDL_Event& event) {
  if (event.button.button == SDL_BUTTON_LEFT) {
    m_mouseButtonStates[LEFT] = true;
    std::cout << "Forge Engine - Mouse button Left clicked!\n";
  }
  if (event.button.button == SDL_BUTTON_MIDDLE) {
    m_mouseButtonStates[MIDDLE] = true;
    std::cout << "Forge Engine - Mouse button Middle clicked!\n";
  }
  if (event.button.button == SDL_BUTTON_RIGHT) {
    m_mouseButtonStates[RIGHT] = true;
    std::cout << "Forge Engine - Mouse button Right clicked!\n";
  }
}

void InputHandler::onMouseButtonUp(SDL_Event& event) {
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

void InputHandler::onGamepadAxisMove(SDL_Event& event) {
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

  // Process left stick X-axis
  if (event.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTX) {
    if (event.gaxis.value > m_joystickDeadZone) {
      m_joystickValues[whichOne].first->setX(1);
      std::cout << "Forge Engine - Left Stick X moving RIGHT!\n";
    } else if (event.gaxis.value < -m_joystickDeadZone) {
      m_joystickValues[whichOne].first->setX(-1);
      std::cout << "Forge Engine - Left Stick X moving LEFT!\n";
    } else {
      m_joystickValues[whichOne].first->setX(0);
    }
  }

  // Process left stick Y-axis
  if (event.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTY) {
    if (event.gaxis.value > m_joystickDeadZone) {
      m_joystickValues[whichOne].first->setY(1);
      std::cout << "Forge Engine - Left Stick Y moving DOWN!\n";
    } else if (event.gaxis.value < -m_joystickDeadZone) {
      m_joystickValues[whichOne].first->setY(-1);
      std::cout << "Forge Engine - Left Stick Y moving UP!\n";
    } else {
      m_joystickValues[whichOne].first->setY(0);
    }
  }

  // Process right stick X-axis
  if (event.gaxis.axis == SDL_GAMEPAD_AXIS_RIGHTX) {
    if (event.gaxis.value > m_joystickDeadZone) {
      m_joystickValues[whichOne].second->setX(1);
    } else if (event.gaxis.value < -m_joystickDeadZone) {
      m_joystickValues[whichOne].second->setX(-1);
    } else {
      m_joystickValues[whichOne].second->setX(0);
    }
  }

  // Process right stick Y-axis
  if (event.gaxis.axis == SDL_GAMEPAD_AXIS_RIGHTY) {
    if (event.gaxis.value > m_joystickDeadZone) {
      m_joystickValues[whichOne].second->setY(1);
    } else if (event.gaxis.value < -m_joystickDeadZone) {
      m_joystickValues[whichOne].second->setY(-1);
    } else {
      m_joystickValues[whichOne].second->setY(0);
    }
  }
}

void InputHandler::onGamepadButtonDown(SDL_Event& event) {
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
}

void InputHandler::onGamepadButtonUp(SDL_Event& event) {
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

void InputHandler::clean() {
  // Close all gamepads
  for (auto& gamepad : m_joysticks) {
    SDL_CloseGamepad(gamepad);
  }

  // Free all joystick values
  for (auto& joystickPair : m_joystickValues) {
    delete joystickPair.first;
    delete joystickPair.second;
  }

  m_joysticks.clear();
  m_joystickValues.clear();
  m_buttonStates.clear();
  SDL_free(gamepadIDs);
  m_gamePadInitialized = false;
}
