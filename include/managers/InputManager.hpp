/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#ifndef INPUT_MANAGER_HPP
#define INPUT_MANAGER_HPP

#include <SDL3/SDL.h>
#include <memory>
#include <optional>
#include <utility>
#include <vector>
#include "utils/Vector2D.hpp"

enum mouse_buttons { LEFT = 0, MIDDLE = 1, RIGHT = 2 };

class InputManager {
 public:
    ~InputManager() {
        if (!m_isShutdown) {
            clean();
        }
    }

    static InputManager& Instance(){
        static InputManager instance;
        return instance;
    }

    // Initialize input system
    bool init();

    // Check if initialized
    bool isInitialized() const { return m_isInitialized; }

    // Initialize gamepad
    void initializeGamePad();

    // Update method
    void update();

    // Reset mouse button states
    void reset();

    // Clean up
    void clean();

    // Check if InputManager has been shut down
    bool isShutdown() const { return m_isShutdown; }

    // Keyboard events
    bool isKeyDown(SDL_Scancode key) const;
    bool wasKeyPressed(SDL_Scancode key) const;  // True once per press
    void clearFrameInput();  // Call once per frame to clear pressed keys

    // Joystick events
    float getAxisX(int joy, int stick) const;
    float getAxisY(int joy, int stick) const;
    bool getButtonState(int joy, int buttonNumber) const;

    // Mouse events
    bool getMouseButtonState(int buttonNumber) const;
    const Vector2D& getMousePosition() const; // Returns const reference for safety

    // Input event handlers (called by GameEngine during SDL event polling)
    void onKeyDown(const SDL_Event& event);
    void onKeyUp(const SDL_Event& event);
    void onMouseMove(const SDL_Event& event);
    void onMouseButtonDown(const SDL_Event& event);
    void onMouseButtonUp(const SDL_Event& event);
    void onGamepadAxisMove(const SDL_Event& event);
    void onGamepadButtonDown(const SDL_Event& event);
    void onGamepadButtonUp(const SDL_Event& event);
    void onGamepadAdded(const SDL_Event& event);
    void onGamepadRemoved(const SDL_Event& event);
    void onGamepadRemapped(const SDL_Event& event);
    void onFocusLost();

 private:
    struct GamepadState {
        SDL_JoystickID instanceId{0};
        SDL_Gamepad* pGamepad{nullptr};
        Vector2D leftStick{0.0f, 0.0f};
        Vector2D rightStick{0.0f, 0.0f};
        std::vector<bool> buttonStates{};
    };

    // Keyboard specific
    const bool* m_keystates{nullptr}; // Owned by SDL, don't delete
    std::vector<SDL_Scancode> m_pressedThisFrame{}; // Keys pressed this frame

    // Gamepad specific
    std::vector<GamepadState> m_gamepads{};
    const int m_joystickDeadZone{10000};
    bool m_gamePadInitialized{false};
    // Mouse specific
    std::vector<bool> m_mouseButtonStates{};
    Vector2D m_mousePosition{0.0f, 0.0f};

    // Initialization and shutdown state
    bool m_isInitialized{false};
    bool m_isShutdown{false};

    // Delete copy constructor and assignment operator
    InputManager(const InputManager&) = delete; // Prevent copying
    InputManager& operator=(const InputManager&) = delete; // Prevent assignment

    std::optional<size_t> findGamepadIndex(SDL_JoystickID instanceId) const;
    bool openGamepad(SDL_JoystickID instanceId);
    void closeGamepad(SDL_JoystickID instanceId);
    void updateMousePositionFromWindowCoords(float x, float y);
    void clearGamepadState();
    static float normalizeGamepadAxisValue(Sint16 value, int deadZone);

    InputManager();
};

#endif  // INPUT_MANAGER_HPP
