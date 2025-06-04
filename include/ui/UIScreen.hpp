/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#ifndef UI_SCREEN_HPP
#define UI_SCREEN_HPP

#include <string>
#include <boost/container/small_vector.hpp>
#include <functional>
#include "managers/UIManager.hpp"

// Base class for UI screens that GameStates can use
class UIScreen {
public:
    UIScreen(const std::string& screenID);
    virtual ~UIScreen();

    // Core lifecycle methods
    virtual void create() = 0;
    virtual void show();
    virtual void hide();
    virtual void update(float deltaTime);
    virtual void destroy();

    // Screen management
    const std::string& getID() const { return m_screenID; }
    bool isVisible() const { return m_visible; }
    bool isCreated() const { return m_created; }

    // Component management helpers
    void addComponent(const std::string& componentID);
    void removeComponent(const std::string& componentID);
    void clearAllComponents();

    // Layout helpers
    void createLayout(const std::string& layoutID, UILayoutType type, const UIRect& bounds);
    void addToLayout(const std::string& layoutID, const std::string& componentID);

    // Common UI patterns
    void createButton(const std::string& id, const UIRect& bounds, const std::string& text, std::function<void()> onClick = nullptr);
    void createLabel(const std::string& id, const UIRect& bounds, const std::string& text);
    void createPanel(const std::string& id, const UIRect& bounds);

    // Event handling
    virtual void onButtonClicked(const std::string& /* buttonID */) {}
    virtual void onValueChanged(const std::string& /* componentID */, float /* value */) {}
    virtual void onTextChanged(const std::string& /* componentID */, const std::string& /* text */) {}

    // Animation helpers
    void fadeIn(float duration = 0.3f);
    void fadeOut(float duration = 0.3f);
    void slideIn(const UIRect& fromBounds, float duration = 0.5f);
    void slideOut(const UIRect& toBounds, float duration = 0.5f);

protected:
    std::string m_screenID;
    bool m_visible{false};
    bool m_created{false};
    boost::container::small_vector<std::string, 32> m_components{};
    boost::container::small_vector<std::string, 8> m_layouts{};

    // Helper methods for derived classes
    UIManager& getUIManager() { return UIManager::Instance(); }
    void setComponentCallback(const std::string& id, std::function<void()> callback);
    void centerComponent(const std::string& id, int windowWidth, int windowHeight);
    void positionRelative(const std::string& id, const std::string& relativeToID, int offsetX, int offsetY);

private:
    // Internal state
    bool m_animating{false};
    float m_fadeAlpha{1.0f};
};

#endif // UI_SCREEN_HPP