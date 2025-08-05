/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#ifndef INPUT_MANAGER_HPP
#define INPUT_MANAGER_HPP

#include <SDL3/SDL.h>
#include <utility>
#include <memory>
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
    int getAxisX(int joy, int stick) const;
    int getAxisY(int joy, int stick) const;
    bool getButtonState(int joy, int buttonNumber) const;

    // Mouse events
    bool getMouseButtonState(int buttonNumber) const;
    const Vector2D& getMousePosition() const; // Returns const reference for safety

    /**
     * @brief Check if gamepad subsystem was initialized
     * @return true if gamepad subsystem needs to be quit
     */
    bool needsGamepadSubsystemCleanup() const { return m_gamepadSubsystemInitialized; }

    /**
     * @brief Quit the gamepad subsystem - called from GameEngine during final cleanup
     */
    void quitGamepadSubsystem();

 private:

    // Keyboard specific
    const bool* m_keystates{nullptr}; // Owned by SDL, don't delete
    std::vector<SDL_Scancode> m_pressedThisFrame{}; // Keys pressed this frame

    // Gamepad specific
    std::vector<std::pair<std::unique_ptr<Vector2D>, std::unique_ptr<Vector2D>>> m_joystickValues{};
    // Non-owning pointers to SDL_Gamepad objects, which are closed with SDL_CloseGamepad in clean()
    std::vector<SDL_Gamepad*> m_joysticks{};
    std::vector<std::vector<bool>> m_buttonStates{};
    const int m_joystickDeadZone{10000};
    bool m_gamePadInitialized{false};
    bool m_gamepadSubsystemInitialized{false};
    // Mouse specific
    std::vector<bool> m_mouseButtonStates{};
    std::unique_ptr<Vector2D> m_mousePosition{nullptr};
    
    // Shutdown state
    bool m_isShutdown{false};

    // Handle keyboard events
    void onKeyDown(const SDL_Event& event);
    void onKeyUp(const SDL_Event& event);

    // Handle mouse events
    void onMouseMove(const SDL_Event& event);
    void onMouseButtonDown(const SDL_Event& event);
    void onMouseButtonUp(const SDL_Event& event);

    // Handle gamepad events
    void onGamepadAxisMove(const SDL_Event& event);
    void onGamepadButtonDown(const SDL_Event& event);
    void onGamepadButtonUp(const SDL_Event& event);

    // Handle window events
    void onWindowResize(const SDL_Event& event);
    
    // Handle display events
    void onDisplayChange(const SDL_Event& event);

    // Delete copy constructor and assignment operator
    InputManager(const InputManager&) = delete; // Prevent copying
    InputManager& operator=(const InputManager&) = delete; // Prevent assignment

    InputManager();
};;

#endif  // INPUT_MANAGER_HPP
