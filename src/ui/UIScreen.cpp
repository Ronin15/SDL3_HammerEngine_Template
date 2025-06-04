/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include "ui/UIScreen.hpp"
#include <algorithm>

UIScreen::UIScreen(const std::string& screenID) : m_screenID(screenID) {
}

UIScreen::~UIScreen() {
    if (m_created) {
        destroy();
    }
}

void UIScreen::show() {
    if (!m_created) {
        create();
        m_created = true;
    }

    m_visible = true;

    // Make all components visible
    auto& ui = getUIManager();
    for (const auto& componentID : m_components) {
        ui.setComponentVisible(componentID, true);
    }
}

void UIScreen::hide() {
    m_visible = false;

    // Hide all components
    auto& ui = getUIManager();
    for (const auto& componentID : m_components) {
        ui.setComponentVisible(componentID, false);
    }
}

void UIScreen::update(float /* deltaTime */) {
    if (!m_visible || !m_created) {
        return;
    }

    // Check for button clicks and handle events
    auto& ui = getUIManager();
    for (const auto& componentID : m_components) {
        if (ui.isButtonClicked(componentID)) {
            onButtonClicked(componentID);
        }
    }
}



void UIScreen::destroy() {
    if (!m_created) {
        return;
    }

    clearAllComponents();
    m_created = false;
    m_visible = false;
}

void UIScreen::addComponent(const std::string& componentID) {
    // Check if component already exists in our list
    auto it = std::find(m_components.begin(), m_components.end(), componentID);
    if (it == m_components.end()) {
        m_components.push_back(componentID);
    }
}

void UIScreen::removeComponent(const std::string& componentID) {
    auto& ui = getUIManager();
    ui.removeComponent(componentID);

    // Remove from our tracking list
    auto it = std::find(m_components.begin(), m_components.end(), componentID);
    if (it != m_components.end()) {
        m_components.erase(it);
    }
}

void UIScreen::clearAllComponents() {
    auto& ui = getUIManager();

    // Remove all tracked components
    for (const auto& componentID : m_components) {
        ui.removeComponent(componentID);
    }
    m_components.clear();

    // Remove all layouts
    for (const auto& layoutID : m_layouts) {
        (void)layoutID; // Mark as intentionally unused
        // Note: UIManager doesn't have removeLayout method, but components are cleaned up
    }
    m_layouts.clear();
}

void UIScreen::createLayout(const std::string& layoutID, UILayoutType type, const UIRect& bounds) {
    auto& ui = getUIManager();
    ui.createLayout(layoutID, type, bounds);

    // Track this layout
    auto it = std::find(m_layouts.begin(), m_layouts.end(), layoutID);
    if (it == m_layouts.end()) {
        m_layouts.push_back(layoutID);
    }
}

void UIScreen::addToLayout(const std::string& layoutID, const std::string& componentID) {
    auto& ui = getUIManager();
    ui.addComponentToLayout(layoutID, componentID);
}

void UIScreen::createButton(const std::string& id, const UIRect& bounds, const std::string& text, std::function<void()> onClick) {
    auto& ui = getUIManager();
    ui.createButton(id, bounds, text);

    if (onClick) {
        ui.setOnClick(id, onClick);
    }

    addComponent(id);
}

void UIScreen::createLabel(const std::string& id, const UIRect& bounds, const std::string& text) {
    auto& ui = getUIManager();
    ui.createLabel(id, bounds, text);
    addComponent(id);
}

void UIScreen::createPanel(const std::string& id, const UIRect& bounds) {
    auto& ui = getUIManager();
    ui.createPanel(id, bounds);
    addComponent(id);
}

void UIScreen::fadeIn(float /* duration */) {
    // Simple fade implementation - could be enhanced with actual alpha blending
    m_animating = true;
    m_fadeAlpha = 0.0f;

    // For now, just show the screen immediately
    // In a full implementation, you'd animate the alpha over time
    show();
    m_fadeAlpha = 1.0f;
    m_animating = false;
}

void UIScreen::fadeOut(float /* duration */) {
    m_animating = true;
    m_fadeAlpha = 1.0f;

    // For now, just hide the screen immediately
    // In a full implementation, you'd animate the alpha over time
    hide();
    m_fadeAlpha = 0.0f;
    m_animating = false;
}

void UIScreen::slideIn(const UIRect& fromBounds, float duration) {
    // Simple slide implementation
    // Move all components from fromBounds to their target positions
    auto& ui = getUIManager();

    for (const auto& componentID : m_components) {
        UIRect currentBounds = ui.getBounds(componentID);
        UIRect startBounds = fromBounds;

        // Set initial position off-screen
        ui.setComponentBounds(componentID, startBounds);

        // Animate to target position
        ui.animateMove(componentID, currentBounds, duration);
    }
}

void UIScreen::slideOut(const UIRect& toBounds, float duration) {
    auto& ui = getUIManager();

    for (const auto& componentID : m_components) {
        ui.animateMove(componentID, toBounds, duration, [this]() {
            hide();
        });
    }
}

void UIScreen::setComponentCallback(const std::string& id, std::function<void()> callback) {
    auto& ui = getUIManager();
    ui.setOnClick(id, callback);
}

void UIScreen::centerComponent(const std::string& id, int windowWidth, int windowHeight) {
    auto& ui = getUIManager();
    UIRect bounds = ui.getBounds(id);

    bounds.x = (windowWidth - bounds.width) / 2;
    bounds.y = (windowHeight - bounds.height) / 2;

    ui.setComponentBounds(id, bounds);
}

void UIScreen::positionRelative(const std::string& id, const std::string& relativeToID, int offsetX, int offsetY) {
    auto& ui = getUIManager();
    UIRect relativeBounds = ui.getBounds(relativeToID);
    UIRect bounds = ui.getBounds(id);

    bounds.x = relativeBounds.x + offsetX;
    bounds.y = relativeBounds.y + offsetY;

    ui.setComponentBounds(id, bounds);
}
