#ifndef INPUT_HANDLER_HPP
#define INPUT_HANDLER_HPP

#include <SDL3/SDL.h>
#include <vector>
#include <utility>

class Vector2D;
class GameEngine;

enum mouse_buttons {
    LEFT = 0,
    MIDDLE = 1,
    RIGHT = 2
};

class InputHandler {
public:
    static InputHandler* Instance() {
        if (sp_Instance == nullptr) {
            sp_Instance = new InputHandler();
        }
        return sp_Instance;
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
    Vector2D* getMousePosition() const;

private:
    InputHandler();
    ~InputHandler();

    // Singleton instance
    static InputHandler* sp_Instance;

    // Keyboard specific
    const bool* m_keystates;

    // Gamepad specific
    std::vector<std::pair<Vector2D*, Vector2D*>> m_joystickValues;
    std::vector<SDL_Gamepad*> m_joysticks;
    std::vector<std::vector<bool>> m_buttonStates;
    const int m_joystickDeadZone = 10000;
    bool m_gamePadInitialized;

    // Mouse specific
    std::vector<bool> m_mouseButtonStates;
    Vector2D* m_mousePosition;

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
};

#endif // INPUT_HANDLER_HPP
