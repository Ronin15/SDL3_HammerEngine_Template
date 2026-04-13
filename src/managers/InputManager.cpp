/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include "managers/InputManager.hpp"
#include "core/Logger.hpp"
#include "core/GameEngine.hpp"
#include "SDL3/SDL_gamepad.h"
#include "SDL3/SDL_joystick.h"
#include "utils/Vector2D.hpp"
#include <algorithm>
#include <cmath>
#include <format>
#include <memory>
#include <optional>

// Removed global pointer - now managed as member variable

InputManager::InputManager()
    : m_keystates(nullptr) {
  // Reserve capacity for performance optimization
  m_pressedThisFrame.reserve(16);  // Typical max keys pressed per frame
  m_gamepads.reserve(4);           // Max 4 gamepads typically
  m_mouseButtonStates.reserve(3);  // 3 mouse buttons

  // Create button states for the mouse
  for (int i = 0; i < 3; i++) {
    m_mouseButtonStates.push_back(false);
  }
}

bool InputManager::init() {
  if (m_isInitialized && !m_isShutdown) {
    INPUT_WARN("InputManager already initialized");
    return true;
  }

  INPUT_INFO("Initializing InputManager");

  // Get initial keyboard state from SDL (may be null on devices without keyboard)
  m_keystates = SDL_GetKeyboardState(nullptr);
  if (!m_keystates) {
    INPUT_WARN("No keyboard state available - device may not have keyboard input");
  }

  if (m_mouseButtonStates.empty()) {
    m_mouseButtonStates.assign(3, false);
  } else {
    reset();
  }

  m_pressedThisFrame.clear();
  m_isInitialized = true;
  m_isShutdown = false;
  INPUT_INFO("InputManager initialized successfully");
  return true;
}

void InputManager::initializeGamePad() {
  // Check if gamepad subsystem is already initialized
  if (m_gamePadInitialized) {
    return;
  }

  // Gamepad subsystem is initialized by GameEngine::init() with SDL_INIT_GAMEPAD
  // Just detect and open available gamepads here

  // Get all available gamepads with RAII management
  int numGamepads = 0;
  auto gamepadIDs = std::unique_ptr<SDL_JoystickID[], decltype(&SDL_free)>(
      SDL_GetGamepads(&numGamepads), SDL_free);

  if (!gamepadIDs) {
    INPUT_ERROR(std::format("Failed to get gamepad IDs: {}", SDL_GetError()));
    return;
  }

  if (numGamepads > 0) {
    INPUT_INFO(std::format("Number of Game Pads detected: {}", numGamepads));
    bool openedAnyGamepad = false;
    for (int i = 0; i < numGamepads; i++) {
      if (openGamepad(gamepadIDs[i])) {
        openedAnyGamepad = true;
      }
    }

    m_gamePadInitialized = openedAnyGamepad;
  } else {
    INPUT_INFO("No gamepads found");
    // Subsystem stays initialized - SDL_Quit() will clean up all subsystems
    return;
  }

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

float InputManager::getAxisX(int joy, int stick) const {
  if (joy < 0 || joy >= static_cast<int>(m_gamepads.size())) {
    return 0;
  }

  if (stick == 1) {
    return m_gamepads[joy].leftStick.getX();
  } else if (stick == 2) {
    return m_gamepads[joy].rightStick.getX();
  }

  return 0;
}

float InputManager::getAxisY(int joy, int stick) const {
  if (joy < 0 || joy >= static_cast<int>(m_gamepads.size())) {
    return 0;
  }

  if (stick == 1) {
    return m_gamepads[joy].leftStick.getY();
  } else if (stick == 2) {
    return m_gamepads[joy].rightStick.getY();
  }

  return 0;
}

bool InputManager::getButtonState(int joy, int buttonNumber) const {
  if (joy < 0 || joy >= static_cast<int>(m_gamepads.size()) ||
      buttonNumber < 0 ||
      buttonNumber >= static_cast<int>(m_gamepads[joy].buttonStates.size())) {
    return false;
  }

  return m_gamepads[joy].buttonStates[buttonNumber];
}

bool InputManager::getMouseButtonState(int buttonNumber) const {
  if (buttonNumber < 0 ||
      buttonNumber >= static_cast<int>(m_mouseButtonStates.size())) {
    return false;
  }

  return m_mouseButtonStates[buttonNumber];
}

const Vector2D& InputManager::getMousePosition() const {
  return m_mousePosition;
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
  if (event.key.repeat) {
    return;
  }

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
  // Keyboard state is already updated by SDL event system

  // Key-specific processing can be handled by game states
  // using the isKeyDown() method
}

void InputManager::onMouseMove(const SDL_Event& event) {
  updateMousePositionFromWindowCoords(event.motion.x, event.motion.y);
}

void InputManager::onMouseButtonDown(const SDL_Event& event) {
  updateMousePositionFromWindowCoords(event.button.x, event.button.y);

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
  updateMousePositionFromWindowCoords(event.button.x, event.button.y);

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
  auto index = findGamepadIndex(event.gaxis.which);
  if (!index.has_value()) {
    return;
  }

  const size_t whichOne = *index;
  GamepadState& gamepadState = m_gamepads[whichOne];

  // Get axis name for debug messages
  const char* axisName;
  switch (event.gaxis.axis)
  {
    case SDL_GAMEPAD_AXIS_LEFTX: axisName = "Left Stick X"; break;
    case SDL_GAMEPAD_AXIS_LEFTY: axisName = "Left Stick Y"; break;
    case SDL_GAMEPAD_AXIS_RIGHTX: axisName = "Right Stick X"; break;
    case SDL_GAMEPAD_AXIS_RIGHTY: axisName = "Right Stick Y"; break;
    case SDL_GAMEPAD_AXIS_LEFT_TRIGGER: axisName = "Left Trigger"; break;
    case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER: axisName = "Right Trigger"; break;
    default: axisName = "Unknown Axis";
  }

  if (event.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTX) {
    gamepadState.leftStick.setX(normalizeGamepadAxisValue(event.gaxis.value, m_joystickDeadZone));
  }
  if (event.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTY) {
    gamepadState.leftStick.setY(normalizeGamepadAxisValue(event.gaxis.value, m_joystickDeadZone));
  }
  if (event.gaxis.axis == SDL_GAMEPAD_AXIS_RIGHTX) {
    gamepadState.rightStick.setX(normalizeGamepadAxisValue(event.gaxis.value, m_joystickDeadZone));
  }
  if (event.gaxis.axis == SDL_GAMEPAD_AXIS_RIGHTY) {
    gamepadState.rightStick.setY(normalizeGamepadAxisValue(event.gaxis.value, m_joystickDeadZone));
  }

  // Process left trigger (L2/LT)
  if (event.gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER) {
    INPUT_DEBUG_IF(event.gaxis.value > m_joystickDeadZone,
        std::format("Gamepad {} - {} pressed: {}", whichOne, axisName, event.gaxis.value));
  }

  // Process right trigger (R2/RT)
  if (event.gaxis.axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) {
    INPUT_DEBUG_IF(event.gaxis.value > m_joystickDeadZone,
        std::format("Gamepad {} - {} pressed: {}", whichOne, axisName, event.gaxis.value));
  }
}

void InputManager::onGamepadButtonDown(const SDL_Event& event) {
  auto index = findGamepadIndex(event.gbutton.which);
  if (!index.has_value()) {
    return;
  }

  GamepadState& gamepadState = m_gamepads[*index];
  if (event.gbutton.button >= static_cast<int>(gamepadState.buttonStates.size())) {
    return;
  }

  gamepadState.buttonStates[event.gbutton.button] = true;

  // Get button name for debug message
  const char* buttonName;
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
  INPUT_DEBUG(std::format("Gamepad {} Button '{}' ({}) pressed!",
                          *index, buttonName, static_cast<int>(event.gbutton.button)));
}

void InputManager::onGamepadButtonUp(const SDL_Event& event) {
  auto index = findGamepadIndex(event.gbutton.which);
  if (!index.has_value()) {
    return;
  }

  GamepadState& gamepadState = m_gamepads[*index];
  if (event.gbutton.button >= static_cast<int>(gamepadState.buttonStates.size())) {
    return;
  }

  gamepadState.buttonStates[event.gbutton.button] = false;
}

void InputManager::onGamepadAdded(const SDL_Event& event) {
  if (openGamepad(event.gdevice.which)) {
    m_gamePadInitialized = true;
  }
}

void InputManager::onGamepadRemoved(const SDL_Event& event) {
  closeGamepad(event.gdevice.which);
  m_gamePadInitialized = !m_gamepads.empty();
}

void InputManager::onGamepadRemapped(const SDL_Event& event) {
  if (findGamepadIndex(event.gdevice.which).has_value()) {
    INPUT_INFO(std::format("Gamepad remapped: instance {}", event.gdevice.which));
    return;
  }

  if (openGamepad(event.gdevice.which)) {
    m_gamePadInitialized = true;
    INPUT_INFO(std::format("Opened remapped gamepad instance {}", event.gdevice.which));
  }
}

void InputManager::onFocusLost() {
  // SDL tracks keyboard state internally. Clear it on the main thread so held keys
  // don't remain logically pressed when focus returns.
  SDL_ResetKeyboard();

  m_pressedThisFrame.clear();
  reset();
  clearGamepadState();
}

void InputManager::clean() {
  if (m_isShutdown) {
    return;
  }

  // Close gamepad handles
  if (m_gamePadInitialized) {
    size_t count = m_gamepads.size();
    for (auto& gamepad : m_gamepads) {
      if (gamepad.pGamepad) {
        SDL_CloseGamepad(gamepad.pGamepad);
        gamepad.pGamepad = nullptr;
      }
    }
    m_gamepads.clear();
    m_gamePadInitialized = false;

    if (count > 0) {
      INPUT_INFO(std::format("Closed {} gamepad handles", count));
    }
  } else {
    INPUT_INFO("No gamepads to free");
  }

  // Clear mouse states
  m_mouseButtonStates.clear();

  // Set shutdown flag
  m_isShutdown = true;
  INPUT_INFO("InputManager resources cleaned");
}

std::optional<size_t> InputManager::findGamepadIndex(SDL_JoystickID instanceId) const {
  auto it = std::find_if(
      m_gamepads.begin(), m_gamepads.end(),
      [instanceId](const GamepadState& gamepadState) {
        return gamepadState.instanceId == instanceId;
      });

  if (it == m_gamepads.end()) {
    return std::nullopt;
  }

  return static_cast<size_t>(std::distance(m_gamepads.begin(), it));
}

bool InputManager::openGamepad(SDL_JoystickID instanceId) {
  if (!SDL_IsGamepad(instanceId)) {
    return false;
  }

  if (findGamepadIndex(instanceId).has_value()) {
    return true;
  }

  SDL_Gamepad* gamepad = SDL_OpenGamepad(instanceId);
  if (!gamepad) {
    INPUT_ERROR(std::format("Could not open gamepad {}: {}", instanceId, SDL_GetError()));
    return false;
  }

  GamepadState gamepadState;
  gamepadState.instanceId = instanceId;
  gamepadState.pGamepad = gamepad;
  gamepadState.buttonStates.assign(SDL_GAMEPAD_BUTTON_COUNT, false);

  const char* pName = SDL_GetGamepadName(gamepad);
  INPUT_INFO(std::format("Gamepad connected: {} (instance {})",
                         pName ? pName : "Unknown",
                         instanceId));

  m_gamepads.push_back(std::move(gamepadState));
  return true;
}

void InputManager::closeGamepad(SDL_JoystickID instanceId) {
  auto index = findGamepadIndex(instanceId);
  if (!index.has_value()) {
    return;
  }

  GamepadState& gamepadState = m_gamepads[*index];
  if (gamepadState.pGamepad) {
    SDL_CloseGamepad(gamepadState.pGamepad);
  }

  m_gamepads.erase(m_gamepads.begin() + static_cast<std::ptrdiff_t>(*index));
  INPUT_INFO(std::format("Gamepad disconnected: instance {}", instanceId));
}

void InputManager::updateMousePositionFromWindowCoords(float x, float y) {
  // GPU renders at pixel resolution, but SDL mouse events are in window coordinates.
  // Scale by pixel density to convert window coords to pixel coords.
  float scale = 1.0f;
  SDL_Window* window = GameEngine::Instance().getWindow();
  if (window) {
    scale = SDL_GetWindowPixelDensity(window);
  }

  m_mousePosition.setX(x * scale);
  m_mousePosition.setY(y * scale);
}

void InputManager::clearGamepadState() {
  for (auto& gamepadState : m_gamepads) {
    gamepadState.leftStick = Vector2D(0.0f, 0.0f);
    gamepadState.rightStick = Vector2D(0.0f, 0.0f);
    std::fill(gamepadState.buttonStates.begin(), gamepadState.buttonStates.end(),
              false);
  }
}

float InputManager::normalizeGamepadAxisValue(Sint16 value, int deadZone) {
  if (std::abs(value) <= deadZone) {
    return 0.0f;
  }

  const float absValue = static_cast<float>(std::abs(value));
  const float normalizedMagnitude =
      std::clamp((absValue - static_cast<float>(deadZone)) /
                     (32767.0f - static_cast<float>(deadZone)),
                 0.0f, 1.0f);

  return (value < 0) ? -normalizedMagnitude : normalizedMagnitude;
}
