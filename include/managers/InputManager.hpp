/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#ifndef INPUT_MANAGER_HPP
#define INPUT_MANAGER_HPP

#include <SDL3/SDL.h>
#include <utility>
#include <memory>
#include <boost/container/small_vector.hpp>
#include "utils/Vector2D.hpp"

enum mouse_buttons { LEFT = 0, MIDDLE = 1, RIGHT = 2 };

class InputManager {
 public:
    ~InputManager();

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

    // Keyboard events
    bool isKeyDown(SDL_Scancode key) const;

    // Joystick events
    int getAxisX(int joy, int stick) const;
    int getAxisY(int joy, int stick) const;
    bool getButtonState(int joy, int buttonNumber) const;

    // Mouse events
    bool getMouseButtonState(int buttonNumber) const;
    Vector2D* getMousePosition() const; // Returns raw pointer for backward compatibility

 private:

    // Keyboard specific
    const bool* m_keystates{nullptr}; // Owned by SDL, don't delete

    // Gamepad specific
    boost::container::small_vector<std::pair<std::unique_ptr<Vector2D>, std::unique_ptr<Vector2D>>, 4> m_joystickValues{};
    // Non-owning pointers to SDL_Gamepad objects, which are closed with SDL_CloseGamepad in clean()
    boost::container::small_vector<SDL_Gamepad*, 4> m_joysticks{};
    boost::container::small_vector<boost::container::small_vector<bool, 16>, 4> m_buttonStates{};
    const int m_joystickDeadZone{10000};
    bool m_gamePadInitialized{false};
    // Mouse specific
    boost::container::small_vector<bool, 3> m_mouseButtonStates{};
    std::unique_ptr<Vector2D> m_mousePosition{nullptr};

    // Handle keyboard events
    void onKeyDown(SDL_Event& event);
    void onKeyUp(SDL_Event& event);

    // Handle mouse events
    void onMouseMove(SDL_Event& event);
    void onMouseButtonDown(SDL_Event& event);
    void onMouseButtonUp(SDL_Event& event);

    // Handle gamepad events
    void onGamepadAxisMove(SDL_Event& event);
    void onGamepadButtonDown(SDL_Event& event);
    void onGamepadButtonUp(SDL_Event& event);

    // Delete copy constructor and assignment operator
    InputManager(const InputManager&) = delete; // Prevent copying
    InputManager& operator=(const InputManager&) = delete; // Prevent assignment

    InputManager();
};;

#endif  // INPUT_MANAGER_HPP
