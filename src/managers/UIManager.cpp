/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include "managers/UIManager.hpp"
#include "managers/FontManager.hpp"
#include "managers/InputManager.hpp"
#include "managers/TextureManager.hpp"
#include "core/GameEngine.hpp"
#include <algorithm>

bool UIManager::init() {
    if (m_isShutdown) {
        return false;
    }

    // Initialize with enhanced dark theme
    setDarkTheme();

    // Set global fonts to match what's loaded
    m_globalFontID = "fonts_UI_Arial";
    m_titleFontID = "fonts_Arial";
    m_uiFontID = "fonts_UI_Arial";

    // Clear any existing data and reserve capacity for performance
    m_components.clear();
    m_layouts.clear();
    m_animations.clear();
    m_animations.reserve(16);  // Reserve for typical UI animations
    m_clickedButtons.clear();
    m_clickedButtons.reserve(8);  // Reserve for typical button interactions
    m_hoveredComponents.clear();
    m_hoveredComponents.reserve(8);  // Reserve for typical hover states
    m_focusedComponent.clear();
    m_hoveredTooltip.clear();

    m_tooltipTimer = 0.0f;
    m_mousePressed = false;
    m_mouseReleased = false;

    return true;
}

void UIManager::update(float deltaTime) {
    if (m_isShutdown) {
        return;
    }

    // Clear frame-specific state
    m_clickedButtons.clear();
    m_mouseReleased = false;

    // Handle input
    handleInput();

    // Update animations
    updateAnimations(deltaTime);

    // Update tooltips
    updateTooltips(deltaTime);

    // Update event logs
    updateEventLogs(deltaTime);

    // Sort components by z-order for proper rendering
    sortComponentsByZOrder();
}

void UIManager::render(SDL_Renderer* renderer) {
    if (m_isShutdown || !renderer) {
        return;
    }

    // Create vector of components sorted by z-order
    std::vector<std::shared_ptr<UIComponent>> sortedComponents;
    for (const auto& [id, component] : m_components) {
        if (component && component->visible) {
            sortedComponents.push_back(component);
        }
    }
    
    // Sort by zOrder (lower values render first/behind)
    std::sort(sortedComponents.begin(), sortedComponents.end(), 
              [](const std::shared_ptr<UIComponent>& a, const std::shared_ptr<UIComponent>& b) {
                  return a->zOrder < b->zOrder;
              });
    
    // Render components in z-order
    for (const auto& component : sortedComponents) {
        renderComponent(renderer, component);
    }

    // Render tooltip last (on top)
    if (m_tooltipsEnabled && !m_hoveredTooltip.empty()) {
        renderTooltip(renderer);
    }

    // Debug rendering
    if (m_debugMode && m_drawDebugBounds) {
        for (const auto& [id, component] : m_components) {
            if (component && component->visible) {
                drawBorder(renderer, component->bounds, {255, 0, 0, 255}, 1);
            }
        }
    }

    // Note: Background color is now managed by GameEngine, not UIManager
    // This allows GameStates to set custom background colors for UI rendering
}

void UIManager::clean() {
    if (m_isShutdown) {
        return;
    }

    m_components.clear();
    m_layouts.clear();
    m_animations.clear();
    m_clickedButtons.clear();
    m_hoveredComponents.clear();
    m_focusedComponent.clear();
    m_hoveredTooltip.clear();

    m_isShutdown = true;
}

void UIManager::sortComponentsByZOrder() {
    // Components are rendered in order, so we don't need to actually sort the map
    // since flat_map maintains insertion order. The rendering order is handled
    // during the render phase by processing components by their zOrder value.
    // This method is kept for interface compatibility.
}

// Component creation methods
void UIManager::createButton(const std::string& id, const UIRect& bounds, const std::string& text) {
    auto component = std::make_shared<UIComponent>();
    component->id = id;
    component->type = UIComponentType::BUTTON;
    component->bounds = bounds;
    component->text = text;
    component->style = m_currentTheme.getStyle(UIComponentType::BUTTON);
    component->zOrder = 10; // Interactive elements on top

    m_components[id] = component;
}

void UIManager::createLabel(const std::string& id, const UIRect& bounds, const std::string& text) {
    auto component = std::make_shared<UIComponent>();
    component->id = id;
    component->type = UIComponentType::LABEL;
    component->bounds = bounds;
    component->text = text;
    component->style = m_currentTheme.getStyle(UIComponentType::LABEL);
    component->zOrder = 20; // Text on top

    m_components[id] = component;
}

void UIManager::createTitle(const std::string& id, const UIRect& bounds, const std::string& text) {
    auto component = std::make_shared<UIComponent>();
    component->id = id;
    component->type = UIComponentType::TITLE;
    component->bounds = bounds;
    component->text = text;
    component->style = m_currentTheme.getStyle(UIComponentType::TITLE);
    component->zOrder = 25; // Titles on top

    m_components[id] = component;
}

void UIManager::createPanel(const std::string& id, const UIRect& bounds) {
    auto component = std::make_shared<UIComponent>();
    component->id = id;
    component->type = UIComponentType::PANEL;
    component->bounds = bounds;
    component->style = m_currentTheme.getStyle(UIComponentType::PANEL);
    component->zOrder = 0; // Background panels

    m_components[id] = component;
}

void UIManager::createProgressBar(const std::string& id, const UIRect& bounds, float minVal, float maxVal) {
    auto component = std::make_shared<UIComponent>();
    component->id = id;
    component->type = UIComponentType::PROGRESS_BAR;
    component->bounds = bounds;
    component->minValue = minVal;
    component->maxValue = maxVal;
    component->value = minVal;
    component->style = m_currentTheme.getStyle(UIComponentType::PROGRESS_BAR);
    component->zOrder = 5; // UI elements

    m_components[id] = component;
}

void UIManager::createInputField(const std::string& id, const UIRect& bounds, const std::string& placeholder) {
    auto component = std::make_shared<UIComponent>();
    component->id = id;
    component->type = UIComponentType::INPUT_FIELD;
    component->bounds = bounds;
    component->placeholder = placeholder;
    component->style = m_currentTheme.getStyle(UIComponentType::INPUT_FIELD);
    component->zOrder = 15; // Interactive elements

    m_components[id] = component;
}

void UIManager::createImage(const std::string& id, const UIRect& bounds, const std::string& textureID) {
    auto component = std::make_shared<UIComponent>();
    component->id = id;
    component->type = UIComponentType::IMAGE;
    component->bounds = bounds;
    component->textureID = textureID;
    component->style = m_currentTheme.getStyle(UIComponentType::IMAGE);
    component->zOrder = 1; // Background images

    m_components[id] = component;
}

void UIManager::createSlider(const std::string& id, const UIRect& bounds, float minVal, float maxVal) {
    auto component = std::make_shared<UIComponent>();
    component->id = id;
    component->type = UIComponentType::SLIDER;
    component->bounds = bounds;
    component->minValue = minVal;
    component->maxValue = maxVal;
    component->value = minVal;
    component->style = m_currentTheme.getStyle(UIComponentType::SLIDER);
    component->zOrder = 12; // Interactive elements

    m_components[id] = component;
}

void UIManager::createCheckbox(const std::string& id, const UIRect& bounds, const std::string& text) {
    auto component = std::make_shared<UIComponent>();
    component->id = id;
    component->type = UIComponentType::CHECKBOX;
    component->bounds = bounds;
    component->text = text;
    component->checked = false;
    component->style = m_currentTheme.getStyle(UIComponentType::CHECKBOX);
    component->zOrder = 13; // Interactive elements

    m_components[id] = component;
}

void UIManager::createList(const std::string& id, const UIRect& bounds) {
    auto component = std::make_shared<UIComponent>();
    component->id = id;
    component->type = UIComponentType::LIST;
    component->bounds = bounds;
    component->selectedIndex = -1;
    component->style = m_currentTheme.getStyle(UIComponentType::LIST);
    component->zOrder = 8; // UI elements

    m_components[id] = component;
}

void UIManager::createTooltip(const std::string& id, const std::string& text) {
    auto component = std::make_shared<UIComponent>();
    component->id = id;
    component->type = UIComponentType::TOOLTIP;
    component->text = text;
    component->visible = false;
    component->style = m_currentTheme.getStyle(UIComponentType::TOOLTIP);
    component->zOrder = 1000; // Always on top

    m_components[id] = component;
}

void UIManager::createEventLog(const std::string& id, const UIRect& bounds, int maxEntries) {
    auto component = std::make_shared<UIComponent>();
    component->id = id;
    component->type = UIComponentType::EVENT_LOG;
    component->bounds = bounds;
    component->maxLength = maxEntries; // Store max entries in maxLength field
    component->style = m_currentTheme.getStyle(UIComponentType::EVENT_LOG); // Use event log styling
    component->zOrder = 6; // UI elements

    m_components[id] = component;
}

void UIManager::createDialog(const std::string& id, const UIRect& bounds) {
    auto component = std::make_shared<UIComponent>();
    component->id = id;
    component->type = UIComponentType::DIALOG;
    component->bounds = bounds;
    component->style = m_currentTheme.getStyle(UIComponentType::DIALOG);
    component->zOrder = -10; // Render behind other elements by default

    m_components[id] = component;
}

void UIManager::createModal(const std::string& dialogId, const UIRect& bounds, const std::string& theme, int windowWidth, int windowHeight) {
    // Set theme first
    if (!theme.empty()) {
        setThemeMode(theme);
        // Refresh existing components to use new theme
        refreshAllComponentThemes();
    }
    
    // Create overlay to dim background
    createOverlay(windowWidth, windowHeight);
    
    // Create dialog box
    createDialog(dialogId, bounds);
}

void UIManager::refreshAllComponentThemes() {
    // Apply current theme to all existing components, preserving custom alignment
    for (const auto& [id, component] : m_components) {
        if (component) {
            UIAlignment preservedAlignment = component->style.textAlign;
            component->style = m_currentTheme.getStyle(component->type);
            component->style.textAlign = preservedAlignment;
        }
    }
}

// Component manipulation
void UIManager::removeComponent(const std::string& id) {
    m_components.erase(id);

    // Remove from any layouts
    for (auto& [layoutId, layout] : m_layouts) {
        auto& children = layout->childComponents;
        children.erase(std::remove(children.begin(), children.end(), id), children.end());
    }

    // Clear focus if this component was focused
    if (m_focusedComponent == id) {
        m_focusedComponent.clear();
    }
}

bool UIManager::hasComponent(const std::string& id) const {
    return m_components.find(id) != m_components.end();
}

void UIManager::setComponentVisible(const std::string& id, bool visible) {
    auto component = getComponent(id);
    if (component) {
        component->visible = visible;
    }
}

void UIManager::setComponentEnabled(const std::string& id, bool enabled) {
    auto component = getComponent(id);
    if (component) {
        component->enabled = enabled;
        if (!enabled && component->state != UIState::DISABLED) {
            component->state = UIState::DISABLED;
        } else if (enabled && component->state == UIState::DISABLED) {
            component->state = UIState::NORMAL;
        }
    }
}

void UIManager::setComponentBounds(const std::string& id, const UIRect& bounds) {
    auto component = getComponent(id);
    if (component) {
        component->bounds = bounds;
    }
}

void UIManager::setComponentZOrder(const std::string& id, int zOrder) {
    auto component = getComponent(id);
    if (component) {
        component->zOrder = zOrder;
    }
}

// Component property setters
void UIManager::setText(const std::string& id, const std::string& text) {
    auto component = getComponent(id);
    if (component) {
        component->text = text;
    }
}

void UIManager::setTexture(const std::string& id, const std::string& textureID) {
    auto component = getComponent(id);
    if (component) {
        component->textureID = textureID;
    }
}

void UIManager::setValue(const std::string& id, float value) {
    auto component = getComponent(id);
    if (component) {
        float clampedValue = std::clamp(value, component->minValue, component->maxValue);
        if (component->value != clampedValue) {
            component->value = clampedValue;
            if (component->onValueChanged) {
                component->onValueChanged(clampedValue);
            }
        }
    }
}

void UIManager::setChecked(const std::string& id, bool checked) {
    auto component = getComponent(id);
    if (component) {
        component->checked = checked;
    }
}

void UIManager::setStyle(const std::string& id, const UIStyle& style) {
    auto component = getComponent(id);
    if (component) {
        component->style = style;
    }
}

// Text background methods for label and title readability
void UIManager::enableTextBackground(const std::string& id, bool enable) {
    auto component = getComponent(id);
    if (component && (component->type == UIComponentType::LABEL || component->type == UIComponentType::TITLE)) {
        component->style.useTextBackground = enable;
    }
}

void UIManager::setTextBackgroundColor(const std::string& id, SDL_Color color) {
    auto component = getComponent(id);
    if (component && (component->type == UIComponentType::LABEL || component->type == UIComponentType::TITLE)) {
        component->style.textBackgroundColor = color;
    }
}

void UIManager::setTextBackgroundPadding(const std::string& id, int padding) {
    auto component = getComponent(id);
    if (component && (component->type == UIComponentType::LABEL || component->type == UIComponentType::TITLE)) {
        component->style.textBackgroundPadding = padding;
    }
}

// Component property getters
std::string UIManager::getText(const std::string& id) const {
    auto component = getComponent(id);
    return component ? component->text : "";
}

float UIManager::getValue(const std::string& id) const {
    auto component = getComponent(id);
    return component ? component->value : 0.0f;
}

bool UIManager::getChecked(const std::string& id) const {
    auto component = getComponent(id);
    return component ? component->checked : false;
}

UIRect UIManager::getBounds(const std::string& id) const {
    auto component = getComponent(id);
    return component ? component->bounds : UIRect{};
}

UIState UIManager::getComponentState(const std::string& id) const {
    auto component = getComponent(id);
    return component ? component->state : UIState::NORMAL;
}

// Event handling
bool UIManager::isButtonClicked(const std::string& id) const {
    return std::find(m_clickedButtons.begin(), m_clickedButtons.end(), id) != m_clickedButtons.end();
}

bool UIManager::isButtonPressed(const std::string& id) const {
    auto component = getComponent(id);
    return component && component->state == UIState::PRESSED;
}

bool UIManager::isButtonHovered(const std::string& id) const {
    return std::find(m_hoveredComponents.begin(), m_hoveredComponents.end(), id) != m_hoveredComponents.end();
}

bool UIManager::isComponentFocused(const std::string& id) const {
    return m_focusedComponent == id;
}

// Callback setters
void UIManager::setOnClick(const std::string& id, std::function<void()> callback) {
    auto component = getComponent(id);
    if (component) {
        component->onClick = callback;
    }
}

void UIManager::setOnValueChanged(const std::string& id, std::function<void(float)> callback) {
    auto component = getComponent(id);
    if (component) {
        component->onValueChanged = callback;
    }
}

void UIManager::setOnTextChanged(const std::string& id, std::function<void(const std::string&)> callback) {
    auto component = getComponent(id);
    if (component) {
        component->onTextChanged = callback;
    }
}

void UIManager::setOnHover(const std::string& id, std::function<void()> callback) {
    auto component = getComponent(id);
    if (component) {
        component->onHover = callback;
    }
}

void UIManager::setOnFocus(const std::string& id, std::function<void()> callback) {
    auto component = getComponent(id);
    if (component) {
        component->onFocus = callback;
    }
}

// Layout management
void UIManager::createLayout(const std::string& id, UILayoutType type, const UIRect& bounds) {
    auto layout = std::make_shared<UILayout>();
    layout->id = id;
    layout->type = type;
    layout->bounds = bounds;

    m_layouts[id] = layout;
}

void UIManager::addComponentToLayout(const std::string& layoutID, const std::string& componentID) {
    auto layout = getLayout(layoutID);
    if (layout && hasComponent(componentID)) {
        layout->childComponents.push_back(componentID);
        updateLayout(layoutID);
    }
}

void UIManager::removeComponentFromLayout(const std::string& layoutID, const std::string& componentID) {
    auto layout = getLayout(layoutID);
    if (layout) {
        auto& children = layout->childComponents;
        children.erase(std::remove(children.begin(), children.end(), componentID), children.end());
        updateLayout(layoutID);
    }
}

void UIManager::updateLayout(const std::string& layoutID) {
    auto layout = getLayout(layoutID);
    if (!layout) {
        return;
    }

    switch (layout->type) {
        case UILayoutType::ABSOLUTE:
            applyAbsoluteLayout(layout);
            break;
        case UILayoutType::FLOW:
            applyFlowLayout(layout);
            break;
        case UILayoutType::GRID:
            applyGridLayout(layout);
            break;
        case UILayoutType::STACK:
            applyStackLayout(layout);
            break;
        case UILayoutType::ANCHOR:
            applyAnchorLayout(layout);
            break;
    }
}

void UIManager::setLayoutSpacing(const std::string& layoutID, int spacing) {
    auto layout = getLayout(layoutID);
    if (layout) {
        layout->spacing = spacing;
        updateLayout(layoutID);
    }
}

void UIManager::setLayoutColumns(const std::string& layoutID, int columns) {
    auto layout = getLayout(layoutID);
    if (layout) {
        layout->columns = columns;
        updateLayout(layoutID);
    }
}

void UIManager::setLayoutAlignment(const std::string& layoutID, UIAlignment alignment) {
    auto layout = getLayout(layoutID);
    if (layout) {
        layout->alignment = alignment;
        updateLayout(layoutID);
    }
}

// Progress bar specific methods
void UIManager::updateProgressBar(const std::string& id, float value) {
    setValue(id, value);
}

void UIManager::setProgressBarRange(const std::string& id, float minVal, float maxVal) {
    auto component = getComponent(id);
    if (component && component->type == UIComponentType::PROGRESS_BAR) {
        component->minValue = minVal;
        component->maxValue = maxVal;
        component->value = std::clamp(component->value, minVal, maxVal);
    }
}

// List specific methods
void UIManager::addListItem(const std::string& listID, const std::string& item) {
    auto component = getComponent(listID);
    if (component && component->type == UIComponentType::LIST) {
        component->listItems.push_back(item);
    }
}

void UIManager::removeListItem(const std::string& listID, int index) {
    auto component = getComponent(listID);
    if (component && component->type == UIComponentType::LIST &&
        index >= 0 && index < static_cast<int>(component->listItems.size())) {
        component->listItems.erase(component->listItems.begin() + index);
        if (component->selectedIndex == index) {
            component->selectedIndex = -1;
        } else if (component->selectedIndex > index) {
            component->selectedIndex--;
        }
    }
}

void UIManager::clearList(const std::string& listID) {
    auto component = getComponent(listID);
    if (component && component->type == UIComponentType::LIST) {
        component->listItems.clear();
        component->selectedIndex = -1;
    }
}

int UIManager::getSelectedListItem(const std::string& listID) const {
    auto component = getComponent(listID);
    return (component && component->type == UIComponentType::LIST) ? component->selectedIndex : -1;
}

void UIManager::setSelectedListItem(const std::string& listID, int index) {
    auto component = getComponent(listID);
    if (component && component->type == UIComponentType::LIST) {
        if (index >= 0 && index < static_cast<int>(component->listItems.size())) {
            component->selectedIndex = index;
        }
    }
}

void UIManager::setListMaxItems(const std::string& listID, int maxItems) {
    auto component = getComponent(listID);
    if (component && component->type == UIComponentType::LIST) {
        // Store max items in a custom property (we'll use the maxLength field for this)
        component->maxLength = maxItems;

        // Trim existing items if they exceed the new limit
        if (static_cast<int>(component->listItems.size()) > maxItems) {
            // Keep only the last maxItems entries
            auto& items = component->listItems;
            auto startIt = items.end() - maxItems;
            items.erase(items.begin(), startIt);

            // Adjust selected index if needed
            if (component->selectedIndex >= maxItems) {
                component->selectedIndex = -1; // Clear selection if it's now out of bounds
            }
        }
    }
}

void UIManager::addListItemWithAutoScroll(const std::string& listID, const std::string& item) {
    auto component = getComponent(listID);
    if (component && component->type == UIComponentType::LIST) {
        // Add the new item
        component->listItems.push_back(item);

        // Check if we need to enforce max items limit
        int maxItems = component->maxLength; // Using maxLength field to store max items
        if (maxItems > 0 && static_cast<int>(component->listItems.size()) > maxItems) {
            // Remove the oldest item
            component->listItems.erase(component->listItems.begin());

            // Adjust selected index if needed
            if (component->selectedIndex > 0) {
                component->selectedIndex--;
            } else if (component->selectedIndex == 0) {
                component->selectedIndex = -1; // Clear selection if first item was removed
            }
        }

        // Auto-scroll by selecting the last item (optional behavior)
        // Comment this out if you don't want auto-selection
        // component->selectedIndex = static_cast<int>(component->listItems.size()) - 1;
    }
}

void UIManager::clearListItems(const std::string& listID) {
    auto component = getComponent(listID);
    if (component && component->type == UIComponentType::LIST) {
        component->listItems.clear();
        component->selectedIndex = -1;
    }
}

void UIManager::addEventLogEntry(const std::string& logID, const std::string& entry) {
    auto component = getComponent(logID);
    if (component && component->type == UIComponentType::EVENT_LOG) {
        // Add the entry directly (let the caller handle timestamps if needed)
        component->listItems.push_back(entry);

        // Enforce max entries limit
        int maxEntries = component->maxLength;
        if (maxEntries > 0 && static_cast<int>(component->listItems.size()) > maxEntries) {
            // Remove oldest entries
            while (static_cast<int>(component->listItems.size()) > maxEntries) {
                component->listItems.erase(component->listItems.begin());
            }
        }

        // No selection for event logs (they're display-only)
        component->selectedIndex = -1;
    }
}

void UIManager::clearEventLog(const std::string& logID) {
    auto component = getComponent(logID);
    if (component && component->type == UIComponentType::EVENT_LOG) {
        component->listItems.clear();
        component->selectedIndex = -1;
    }
}

void UIManager::setEventLogMaxEntries(const std::string& logID, int maxEntries) {
    auto component = getComponent(logID);
    if (component && component->type == UIComponentType::EVENT_LOG) {
        component->maxLength = maxEntries;
        
        // Trim existing entries if needed
        if (static_cast<int>(component->listItems.size()) > maxEntries) {
            while (static_cast<int>(component->listItems.size()) > maxEntries) {
                component->listItems.erase(component->listItems.begin());
            }
        }
    }
}

void UIManager::setTitleAlignment(const std::string& titleID, UIAlignment alignment) {
    auto component = getComponent(titleID);
    if (component && component->type == UIComponentType::TITLE) {
        component->style.textAlign = alignment;
    }
}

void UIManager::setupDemoEventLog(const std::string& logID) {
    addEventLogEntry(logID, "Event log initialized");
    addEventLogEntry(logID, "Demo components created");

    // Enable auto-updates for this event log
    enableEventLogAutoUpdate(logID, 2.0f); // 2 second interval
}

void UIManager::enableEventLogAutoUpdate(const std::string& logID, float interval) {
    auto component = getComponent(logID);
    if (component && component->type == UIComponentType::EVENT_LOG) {
        EventLogState state;
        state.timer = 0.0f;
        state.messageIndex = 0;
        state.updateInterval = interval;
        state.autoUpdate = true;
        m_eventLogStates[logID] = state;
    }
}

void UIManager::disableEventLogAutoUpdate(const std::string& logID) {
    auto it = m_eventLogStates.find(logID);
    if (it != m_eventLogStates.end()) {
        it->second.autoUpdate = false;
    }
}

void UIManager::updateEventLogs(float deltaTime) {
    for (auto& [logID, state] : m_eventLogStates) {
        if (!state.autoUpdate) continue;

        state.timer += deltaTime;

        // Add a new log entry based on the interval
        if (state.timer >= state.updateInterval) {
            state.timer = 0.0f;

            std::vector<std::string> sampleMessages = {
                "System initialized successfully",
                "User interface components loaded",
                "Database connection established",
                "Configuration files validated",
                "Network module started",
                "Audio system ready",
                "Graphics renderer initialized",
                "Input handlers registered",
                "Memory pools allocated",
                "Security protocols activated"
            };

            addEventLogEntry(logID, sampleMessages[state.messageIndex % sampleMessages.size()]);
            state.messageIndex++;
        }
    }
}

// Input field specific methods
void UIManager::setInputFieldPlaceholder(const std::string& id, const std::string& placeholder) {
    auto component = getComponent(id);
    if (component && component->type == UIComponentType::INPUT_FIELD) {
        component->placeholder = placeholder;
    }
}

void UIManager::setInputFieldMaxLength(const std::string& id, int maxLength) {
    auto component = getComponent(id);
    if (component && component->type == UIComponentType::INPUT_FIELD) {
        component->maxLength = maxLength;
    }
}

bool UIManager::isInputFieldFocused(const std::string& id) const {
    return isComponentFocused(id);
}

// Animation system
void UIManager::animateMove(const std::string& id, const UIRect& targetBounds, float duration, std::function<void()> onComplete) {
    auto component = getComponent(id);
    if (!component) {
        return;
    }

    auto animation = std::make_shared<UIAnimation>();
    animation->componentID = id;
    animation->duration = duration;
    animation->elapsed = 0.0f;
    animation->active = true;
    animation->startBounds = component->bounds;
    animation->targetBounds = targetBounds;
    animation->onComplete = onComplete;

    // Remove any existing animation for this component
    stopAnimation(id);

    m_animations.push_back(animation);
}

void UIManager::animateColor(const std::string& id, const SDL_Color& targetColor, float duration, std::function<void()> onComplete) {
    auto component = getComponent(id);
    if (!component) {
        return;
    }

    auto animation = std::make_shared<UIAnimation>();
    animation->componentID = id;
    animation->duration = duration;
    animation->elapsed = 0.0f;
    animation->active = true;
    animation->startColor = component->style.backgroundColor;
    animation->targetColor = targetColor;
    animation->onComplete = onComplete;

    // Remove any existing animation for this component
    stopAnimation(id);

    m_animations.push_back(animation);
}

void UIManager::stopAnimation(const std::string& id) {
    m_animations.erase(
        std::remove_if(m_animations.begin(), m_animations.end(),
            [&id](const std::shared_ptr<UIAnimation>& anim) {
                return anim->componentID == id;
            }),
        m_animations.end()
    );
}

bool UIManager::isAnimating(const std::string& id) const {
    return std::any_of(m_animations.begin(), m_animations.end(),
        [&id](const std::shared_ptr<UIAnimation>& anim) {
            return anim->componentID == id && anim->active;
        });
}

// Theme management
void UIManager::loadTheme(const UITheme& theme) {
    m_currentTheme = theme;

    // Apply theme to all existing components, preserving custom alignment
    for (const auto& [id, component] : m_components) {
        if (component) {
            UIAlignment preservedAlignment = component->style.textAlign;
            component->style = m_currentTheme.getStyle(component->type);
            component->style.textAlign = preservedAlignment;
        }
    }
}

void UIManager::setDefaultTheme() {
    // Default theme now uses dark theme as the base
    setDarkTheme();
}

void UIManager::setLightTheme() {
    UITheme lightTheme;
    lightTheme.name = "light";
    m_currentThemeMode = "light";

    // Button style - improved contrast and professional appearance
    UIStyle buttonStyle;
    buttonStyle.backgroundColor = {60, 120, 180, 255};
    buttonStyle.borderColor = {255, 255, 255, 255};
    buttonStyle.textColor = {255, 255, 255, 255};
    buttonStyle.hoverColor = {80, 140, 200, 255};
    buttonStyle.pressedColor = {40, 100, 160, 255};
    buttonStyle.borderWidth = 1;
    buttonStyle.textAlign = UIAlignment::CENTER_CENTER;
    buttonStyle.fontID = "fonts_UI_Arial";
    lightTheme.componentStyles[UIComponentType::BUTTON] = buttonStyle;

    // Label style - enhanced contrast
    UIStyle labelStyle;
    labelStyle.backgroundColor = {0, 0, 0, 0}; // Transparent
    labelStyle.textColor = {20, 20, 20, 255}; // Dark text for light backgrounds
    labelStyle.textAlign = UIAlignment::CENTER_LEFT;
    labelStyle.fontID = "fonts_UI_Arial";
    // Text background enabled by default for readability on any background
    labelStyle.useTextBackground = true;
    labelStyle.textBackgroundColor = {255, 255, 255, 100}; // More transparent white
    labelStyle.textBackgroundPadding = 6;
    lightTheme.componentStyles[UIComponentType::LABEL] = labelStyle;

    // Panel style - light overlay for subtle UI separation
    UIStyle panelStyle;
    panelStyle.backgroundColor = {0, 0, 0, 40}; // Very light overlay (15% opacity)
    panelStyle.borderWidth = 0;
    panelStyle.fontID = "fonts_UI_Arial";
    lightTheme.componentStyles[UIComponentType::PANEL] = panelStyle;

    // Progress bar style - enhanced visibility
    UIStyle progressStyle;
    progressStyle.backgroundColor = {40, 40, 40, 255};
    progressStyle.borderColor = {180, 180, 180, 255}; // Stronger borders
    progressStyle.hoverColor = {0, 180, 0, 255}; // Green fill
    progressStyle.borderWidth = 1;
    progressStyle.fontID = "fonts_UI_Arial";
    lightTheme.componentStyles[UIComponentType::PROGRESS_BAR] = progressStyle;

    // Input field style - light background with dark text
    UIStyle inputStyle;
    inputStyle.backgroundColor = {245, 245, 245, 255};
    inputStyle.textColor = {20, 20, 20, 255}; // Dark text for good contrast
    inputStyle.borderColor = {180, 180, 180, 255};
    inputStyle.hoverColor = {235, 245, 255, 255};
    inputStyle.borderWidth = 1;
    inputStyle.textAlign = UIAlignment::CENTER_LEFT;
    inputStyle.fontID = "fonts_UI_Arial";
    lightTheme.componentStyles[UIComponentType::INPUT_FIELD] = inputStyle;

    // List style - light background with enhanced item height
    UIStyle listStyle;
    listStyle.backgroundColor = {240, 240, 240, 255};
    listStyle.borderColor = {180, 180, 180, 255};
    listStyle.textColor = {20, 20, 20, 255}; // Dark text on light background
    listStyle.hoverColor = {180, 200, 255, 255}; // Light blue selection
    listStyle.borderWidth = 1;
    listStyle.listItemHeight = 36; // Enhanced mouse accuracy
    listStyle.fontID = "fonts_UI_Arial";
    lightTheme.componentStyles[UIComponentType::LIST] = listStyle;

    // Slider style - enhanced borders
    UIStyle sliderStyle;
    sliderStyle.backgroundColor = {100, 100, 100, 255};
    sliderStyle.borderColor = {180, 180, 180, 255};
    sliderStyle.hoverColor = {60, 120, 180, 255}; // Blue handle
    sliderStyle.pressedColor = {40, 100, 160, 255};
    sliderStyle.borderWidth = 1;
    sliderStyle.fontID = "fonts_UI_Arial";
    lightTheme.componentStyles[UIComponentType::SLIDER] = sliderStyle;

    // Checkbox style - enhanced visibility
    UIStyle checkboxStyle = buttonStyle;
    checkboxStyle.backgroundColor = {180, 180, 180, 255};
    checkboxStyle.hoverColor = {200, 200, 200, 255};
    checkboxStyle.textColor = {20, 20, 20, 255}; // Dark text for light backgrounds
    checkboxStyle.textAlign = UIAlignment::CENTER_LEFT;
    checkboxStyle.fontID = "fonts_UI_Arial";
    lightTheme.componentStyles[UIComponentType::CHECKBOX] = checkboxStyle;

    // Tooltip style
    UIStyle tooltipStyle = panelStyle;
    tooltipStyle.backgroundColor = {40, 40, 40, 230}; // More opaque for tooltips
    tooltipStyle.borderColor = {180, 180, 180, 255};
    tooltipStyle.borderWidth = 1;
    tooltipStyle.textColor = {255, 255, 255, 255}; // White text for dark tooltip background
    tooltipStyle.fontID = "fonts_UI_Arial";
    lightTheme.componentStyles[UIComponentType::TOOLTIP] = tooltipStyle;

    // Image component uses transparent background
    UIStyle imageStyle;
    imageStyle.backgroundColor = {0, 0, 0, 0};
    imageStyle.fontID = "fonts_UI_Arial";
    lightTheme.componentStyles[UIComponentType::IMAGE] = imageStyle;

    // Event log style - similar to list but optimized for display-only
    UIStyle eventLogStyle = listStyle;
    eventLogStyle.listItemHeight = 28; // Better spacing for event log
    eventLogStyle.backgroundColor = {245, 245, 250, 160}; // Semi-transparent light background
    eventLogStyle.textColor = {0, 0, 0, 255}; // Black text for maximum contrast
    eventLogStyle.borderColor = {120, 120, 140, 180}; // Less transparent border
    lightTheme.componentStyles[UIComponentType::EVENT_LOG] = eventLogStyle;

    // Title style - large, prominent text for headings
    UIStyle titleStyle;
    titleStyle.backgroundColor = {0, 0, 0, 0}; // Transparent background
    titleStyle.textColor = {255, 245, 120, 255}; // Gold color for titles
    titleStyle.fontSize = 24; // Use native 24px font size
    titleStyle.textAlign = UIAlignment::CENTER_LEFT;
    titleStyle.fontID = "fonts_Arial";
    // Text background enabled by default for readability on any background
    titleStyle.useTextBackground = true;
    titleStyle.textBackgroundColor = {20, 20, 20, 120}; // More transparent dark for gold text
    titleStyle.textBackgroundPadding = 8;
    lightTheme.componentStyles[UIComponentType::TITLE] = titleStyle;

    // Dialog style - solid background for modal dialogs
    UIStyle dialogStyle;
    dialogStyle.backgroundColor = {245, 245, 245, 255}; // Light solid background
    dialogStyle.borderColor = {120, 120, 120, 255}; // Dark border for definition
    dialogStyle.borderWidth = 2;
    dialogStyle.fontID = "fonts_UI_Arial";
    lightTheme.componentStyles[UIComponentType::DIALOG] = dialogStyle;

    m_currentTheme = lightTheme;

    // Apply theme to all existing components, preserving custom alignment
    for (const auto& [id, component] : m_components) {
        if (component) {
            UIAlignment preservedAlignment = component->style.textAlign;
            component->style = m_currentTheme.getStyle(component->type);
            component->style.textAlign = preservedAlignment;
        }
    }
}

void UIManager::setDarkTheme() {
    UITheme darkTheme;
    darkTheme.name = "dark";
    m_currentThemeMode = "dark";

    // Button style - enhanced contrast for dark theme
    UIStyle buttonStyle;
    buttonStyle.backgroundColor = {50, 50, 60, 255};
    buttonStyle.borderColor = {180, 180, 180, 255}; // Brighter borders
    buttonStyle.textColor = {255, 255, 255, 255};
    buttonStyle.hoverColor = {70, 70, 80, 255};
    buttonStyle.pressedColor = {30, 30, 40, 255};
    buttonStyle.borderWidth = 1;
    buttonStyle.textAlign = UIAlignment::CENTER_CENTER;
    buttonStyle.fontID = "fonts_UI_Arial";
    darkTheme.componentStyles[UIComponentType::BUTTON] = buttonStyle;

    // Label style - pure white text for maximum contrast
    UIStyle labelStyle;
    labelStyle.backgroundColor = {0, 0, 0, 0}; // Transparent
    labelStyle.textColor = {255, 255, 255, 255}; // Pure white
    labelStyle.textAlign = UIAlignment::CENTER_LEFT;
    labelStyle.fontID = "fonts_UI_Arial";
    // Text background enabled by default for readability on any background
    labelStyle.useTextBackground = true;
    labelStyle.textBackgroundColor = {0, 0, 0, 100}; // More transparent black
    labelStyle.textBackgroundPadding = 6;
    darkTheme.componentStyles[UIComponentType::LABEL] = labelStyle;

    // Panel style - slightly more overlay for dark theme
    UIStyle panelStyle;
    panelStyle.backgroundColor = {0, 0, 0, 50}; // 19% opacity
    panelStyle.borderWidth = 0;
    panelStyle.fontID = "fonts_UI_Arial";
    darkTheme.componentStyles[UIComponentType::PANEL] = panelStyle;

    // Progress bar style
    UIStyle progressStyle;
    progressStyle.backgroundColor = {20, 20, 20, 255};
    progressStyle.borderColor = {180, 180, 180, 255};
    progressStyle.hoverColor = {0, 180, 0, 255}; // Green fill
    progressStyle.borderWidth = 1;
    progressStyle.fontID = "fonts_UI_Arial";
    darkTheme.componentStyles[UIComponentType::PROGRESS_BAR] = progressStyle;

    // Input field style - dark theme
    UIStyle inputStyle;
    inputStyle.backgroundColor = {40, 40, 40, 255};
    inputStyle.textColor = {255, 255, 255, 255}; // White text
    inputStyle.borderColor = {180, 180, 180, 255};
    inputStyle.hoverColor = {50, 50, 50, 255};
    inputStyle.borderWidth = 1;
    inputStyle.textAlign = UIAlignment::CENTER_LEFT;
    inputStyle.fontID = "fonts_UI_Arial";
    darkTheme.componentStyles[UIComponentType::INPUT_FIELD] = inputStyle;

    // List style - dark theme
    UIStyle listStyle;
    listStyle.backgroundColor = {35, 35, 35, 255};
    listStyle.borderColor = {180, 180, 180, 255};
    listStyle.textColor = {255, 255, 255, 255}; // White text
    listStyle.hoverColor = {60, 80, 150, 255}; // Blue selection
    listStyle.borderWidth = 1;
    listStyle.listItemHeight = 36; // Enhanced mouse accuracy
    listStyle.fontID = "fonts_UI_Arial";
    darkTheme.componentStyles[UIComponentType::LIST] = listStyle;

    // Slider style
    UIStyle sliderStyle;
    sliderStyle.backgroundColor = {30, 30, 30, 255};
    sliderStyle.borderColor = {180, 180, 180, 255};
    sliderStyle.hoverColor = {60, 120, 180, 255}; // Blue handle
    sliderStyle.pressedColor = {40, 100, 160, 255};
    sliderStyle.borderWidth = 1;
    sliderStyle.fontID = "fonts_UI_Arial";
    darkTheme.componentStyles[UIComponentType::SLIDER] = sliderStyle;

    // Checkbox style
    UIStyle checkboxStyle = buttonStyle;
    checkboxStyle.backgroundColor = {60, 60, 60, 255};
    checkboxStyle.hoverColor = {80, 80, 80, 255};
    checkboxStyle.textColor = {255, 255, 255, 255};
    checkboxStyle.textAlign = UIAlignment::CENTER_LEFT;
    checkboxStyle.fontID = "fonts_UI_Arial";
    darkTheme.componentStyles[UIComponentType::CHECKBOX] = checkboxStyle;

    // Tooltip style
    UIStyle tooltipStyle;
    tooltipStyle.backgroundColor = {20, 20, 20, 240};
    tooltipStyle.borderColor = {180, 180, 180, 255};
    tooltipStyle.borderWidth = 1;
    tooltipStyle.textColor = {255, 255, 255, 255};
    tooltipStyle.fontID = "fonts_UI_Arial";
    darkTheme.componentStyles[UIComponentType::TOOLTIP] = tooltipStyle;

    // Image component uses transparent background
    UIStyle imageStyle;
    imageStyle.backgroundColor = {0, 0, 0, 0};
    imageStyle.fontID = "fonts_UI_Arial";
    darkTheme.componentStyles[UIComponentType::IMAGE] = imageStyle;

    // Event log style - similar to list but optimized for display-only
    UIStyle eventLogStyle = listStyle;
    eventLogStyle.listItemHeight = 28; // Better spacing for event log
    eventLogStyle.backgroundColor = {25, 30, 35, 80}; // Highly transparent dark background
    eventLogStyle.textColor = {255, 255, 255, 255}; // Pure white text for maximum contrast
    eventLogStyle.borderColor = {100, 120, 140, 100}; // Highly transparent blue-gray border
    darkTheme.componentStyles[UIComponentType::EVENT_LOG] = eventLogStyle;

    // Title style - large, prominent text for headings
    UIStyle titleStyle;
    titleStyle.backgroundColor = {0, 0, 0, 0}; // Transparent background
    titleStyle.textColor = {255, 245, 120, 255}; // Gold color for titles
    titleStyle.fontSize = 24; // Use native 24px font size
    titleStyle.textAlign = UIAlignment::CENTER_LEFT;
    titleStyle.fontID = "fonts_Arial";
    // Text background enabled by default for readability on any background
    titleStyle.useTextBackground = true;
    titleStyle.textBackgroundColor = {0, 0, 0, 120}; // More transparent black for gold text
    titleStyle.textBackgroundPadding = 8;
    darkTheme.componentStyles[UIComponentType::TITLE] = titleStyle;

    // Dialog style - solid background for modal dialogs
    UIStyle dialogStyle;
    dialogStyle.backgroundColor = {45, 45, 45, 255}; // Dark solid background
    dialogStyle.borderColor = {160, 160, 160, 255}; // Light border for definition
    dialogStyle.borderWidth = 2;
    dialogStyle.fontID = "fonts_UI_Arial";
    darkTheme.componentStyles[UIComponentType::DIALOG] = dialogStyle;

    m_currentTheme = darkTheme;

    // Apply theme to all existing components, preserving custom alignment
    for (const auto& [id, component] : m_components) {
        if (component) {
            UIAlignment preservedAlignment = component->style.textAlign;
            component->style = m_currentTheme.getStyle(component->type);
            component->style.textAlign = preservedAlignment;
        }
    }
}

void UIManager::setThemeMode(const std::string& mode) {
    if (mode == "light") {
        setLightTheme();
    } else if (mode == "dark") {
        setDarkTheme();
    } else if (mode == "default") {
        // For backward compatibility, default now uses dark theme
        setDarkTheme();
    }
}

std::string UIManager::getCurrentThemeMode() const {
    return m_currentThemeMode;
}

void UIManager::createOverlay(int windowWidth, int windowHeight) {
    // Remove existing overlay if it exists
    removeOverlay();

    // Create semi-transparent overlay panel using current theme's panel style
    createPanel("__overlay", {0, 0, windowWidth, windowHeight});
}

void UIManager::removeOverlay() {
    // Remove the overlay panel if it exists
    if (hasComponent("__overlay")) {
        removeComponent("__overlay");
    }
}

void UIManager::removeComponentsWithPrefix(const std::string& prefix) {
    // Collect components to remove (can't modify map while iterating)
    std::vector<std::string> componentsToRemove;
    componentsToRemove.reserve(32);  // Reserve capacity for performance

    for (const auto& [id, component] : m_components) {
        if (id.substr(0, prefix.length()) == prefix) {
            componentsToRemove.push_back(id);
        }
    }

    // Remove collected components
    for (const auto& id : componentsToRemove) {
        removeComponent(id);
    }
}

void UIManager::clearAllComponents() {
    // Enhanced clearAllComponents - preserve theme background but clear everything else
    std::vector<std::string> componentsToRemove;
    componentsToRemove.reserve(64);  // Reserve capacity for performance

    for (const auto& [id, component] : m_components) {
        if (id != "__overlay") {
            componentsToRemove.push_back(id);
        }
    }

    for (const auto& id : componentsToRemove) {
        removeComponent(id);
    }

    // Clear other collections
    m_layouts.clear();
    m_animations.clear();
    m_clickedButtons.clear();
    m_hoveredComponents.clear();
    m_focusedComponent.clear();
    m_hoveredTooltip.clear();
}

void UIManager::resetToDefaultTheme() {
    // Reset to default dark theme (only used by states that actually change themes)
    setDarkTheme();
    m_currentThemeMode = "dark";
}

void UIManager::cleanupForStateTransition() {
    // Remove all components (complete cleanup)
    m_components.clear();
    
    // Remove overlay
    removeOverlay();
    
    // Reset to default theme
    resetToDefaultTheme();
    
    // Clear any remaining UI state
    m_focusedComponent.clear();
    m_hoveredTooltip.clear();
    
    // Reset global settings
    m_globalStyle = UIStyle{};
    m_globalFontID = "fonts_UI_Arial";
}

void UIManager::applyThemeToComponent(const std::string& id, UIComponentType type) {
    auto component = getComponent(id);
    if (component) {
        component->style = m_currentTheme.getStyle(type);
    }
}

void UIManager::setGlobalStyle(const UIStyle& style) {
    m_globalStyle = style;
}

// Utility methods
void UIManager::setGlobalFont(const std::string& fontID) {
    m_globalFontID = fontID;

    // Update all components to use the new font
    for (const auto& [id, component] : m_components) {
        if (component) {
            component->style.fontID = fontID;
        }
    }
}

void UIManager::setGlobalScale(float scale) {
    m_globalScale = scale;
}

// Private helper methods
std::shared_ptr<UIComponent> UIManager::getComponent(const std::string& id) {
    auto it = m_components.find(id);
    return (it != m_components.end()) ? it->second : nullptr;
}

std::shared_ptr<const UIComponent> UIManager::getComponent(const std::string& id) const {
    auto it = m_components.find(id);
    return (it != m_components.end()) ? it->second : nullptr;
}

std::shared_ptr<UILayout> UIManager::getLayout(const std::string& id) {
    auto it = m_layouts.find(id);
    return (it != m_layouts.end()) ? it->second : nullptr;
}

void UIManager::handleInput() {
    auto& inputManager = InputManager::Instance();

    // Get mouse position
    Vector2D mousePos = inputManager.getMousePosition();
    m_lastMousePosition = mousePos;

    // Check mouse state
    bool mouseDown = inputManager.getMouseButtonState(LEFT);
    bool mouseJustPressed = mouseDown && !m_mousePressed;
    bool mouseJustReleased = !mouseDown && m_mousePressed;

    m_mousePressed = mouseDown;
    m_mouseReleased = mouseJustReleased;

    // Clear previous hover state
    m_hoveredComponents.clear();

    // Process components in reverse z-order (top to bottom)
    std::vector<std::pair<std::string, std::shared_ptr<UIComponent>>> sortedComponents;
    sortedComponents.reserve(32);  // Reserve capacity for performance
    for (const auto& [id, component] : m_components) {
        if (component && component->visible && component->enabled) {
            sortedComponents.emplace_back(id, component);
        }
    }

    std::sort(sortedComponents.begin(), sortedComponents.end(),
        [](const std::pair<std::string, std::shared_ptr<UIComponent>>& a, 
           const std::pair<std::string, std::shared_ptr<UIComponent>>& b) {
            return a.second->zOrder > b.second->zOrder;
        });

    bool mouseHandled = false;

    for (const auto& [id, component] : sortedComponents) {
        if (mouseHandled) {
            // Reset state for components below
            if (component->state == UIState::HOVERED || component->state == UIState::PRESSED) {
                component->state = UIState::NORMAL;
            }
            continue;
        }

        // Convert window coordinates to logical coordinates for hit testing
        // This handles all SDL3 logical presentation modes:
        // - SDL_LOGICAL_PRESENTATION_LETTERBOX: Maintains aspect ratio with black bars
        // - SDL_LOGICAL_PRESENTATION_STRETCH: Stretches to fill window (may distort)
        // - SDL_LOGICAL_PRESENTATION_OVERSCAN: Crops content to maintain aspect ratio
        // - SDL_LOGICAL_PRESENTATION_DISABLED: No coordinate conversion needed
        int mouseX, mouseY;
        auto* renderer = GameEngine::Instance().getRenderer();

        // Check if logical presentation is active
        int logicalW, logicalH;
        SDL_RendererLogicalPresentation presentation;
        if (SDL_GetRenderLogicalPresentation(renderer, &logicalW, &logicalH, &presentation) &&
            presentation != SDL_LOGICAL_PRESENTATION_DISABLED) {

            // Convert coordinates for any logical presentation mode
            float logicalX, logicalY;
            SDL_RenderCoordinatesFromWindow(renderer, mousePos.getX(), mousePos.getY(),
                                           &logicalX, &logicalY);
            mouseX = static_cast<int>(logicalX);
            mouseY = static_cast<int>(logicalY);
        } else {
            // No logical presentation - use window coordinates directly
            mouseX = static_cast<int>(mousePos.getX());
            mouseY = static_cast<int>(mousePos.getY());
        }

        bool isHovered = component->bounds.contains(mouseX, mouseY);

        if (isHovered) {
            m_hoveredComponents.push_back(id);

            // Handle hover state
            if (component->state == UIState::NORMAL) {
                component->state = UIState::HOVERED;
                if (component->onHover) {
                    component->onHover();
                }
            }

            // Handle click/press for interactive components
            if (component->type == UIComponentType::BUTTON ||
                component->type == UIComponentType::CHECKBOX ||
                component->type == UIComponentType::SLIDER) {

                if (mouseJustPressed) {
                    component->state = UIState::PRESSED;
                    m_focusedComponent = id;
                    if (component->onFocus) {
                        component->onFocus();
                    }
                }

                if (mouseJustReleased && component->state == UIState::PRESSED) {
                    // Handle click
                    if (component->type == UIComponentType::BUTTON) {
                        m_clickedButtons.push_back(id);
                        if (component->onClick) {
                            component->onClick();
                        }
                    } else if (component->type == UIComponentType::CHECKBOX) {
                        component->checked = !component->checked;
                        if (component->onClick) {
                            component->onClick();
                        }
                    }
                    component->state = UIState::HOVERED;
                }

                // Handle slider dragging
                if (component->type == UIComponentType::SLIDER && component->state == UIState::PRESSED) {
                    float relativeX = (mousePos.getX() - component->bounds.x) / static_cast<float>(component->bounds.width);
                    float newValue = component->minValue + relativeX * (component->maxValue - component->minValue);
                    setValue(id, newValue);
                }

                mouseHandled = true;
            }

            // Handle input field focus
            if (component->type == UIComponentType::INPUT_FIELD && mouseJustPressed) {
                m_focusedComponent = id;
                component->state = UIState::FOCUSED;
                if (component->onFocus) {
                    component->onFocus();
                }
                mouseHandled = true;
            }

            // Handle list selection
            if (component->type == UIComponentType::LIST && mouseJustPressed) {
                // Calculate which item was clicked using configurable item height
                int itemHeight = component->style.listItemHeight;
                int itemIndex = static_cast<int>((mousePos.getY() - component->bounds.y) / itemHeight);
                if (itemIndex >= 0 && itemIndex < static_cast<int>(component->listItems.size())) {
                    component->selectedIndex = itemIndex;
                    if (component->onClick) {
                        component->onClick();
                    }
                }
                mouseHandled = true;
            }
        } else {
            // Not hovered
            if (component->state == UIState::HOVERED) {
                component->state = UIState::NORMAL;
            }
            if (component->state == UIState::PRESSED && mouseJustReleased) {
                component->state = UIState::NORMAL;
            }
        }
    }

    // Handle focus loss
    if (mouseJustPressed && !mouseHandled) {
        if (!m_focusedComponent.empty()) {
            auto focusedComponent = getComponent(m_focusedComponent);
            if (focusedComponent && focusedComponent->state == UIState::FOCUSED) {
                focusedComponent->state = UIState::NORMAL;
            }
        }
        m_focusedComponent.clear();
    }
}

void UIManager::updateAnimations(float deltaTime) {
    for (auto it = m_animations.begin(); it != m_animations.end();) {
        auto& anim = *it;
        if (!anim->active) {
            it = m_animations.erase(it);
            continue;
        }

        anim->elapsed += deltaTime;
        float t = std::min(anim->elapsed / anim->duration, 1.0f);

        auto component = getComponent(anim->componentID);
        if (component) {
            // Apply animation
            if (anim->startBounds.width > 0) {
                // Position/size animation
                component->bounds = interpolateRect(anim->startBounds, anim->targetBounds, t);
            } else {
                // Color animation
                component->style.backgroundColor = interpolateColor(anim->startColor, anim->targetColor, t);
            }
        }

        if (t >= 1.0f) {
            // Animation complete
            anim->active = false;
            if (anim->onComplete) {
                anim->onComplete();
            }
            it = m_animations.erase(it);
        } else {
            ++it;
        }
    }
}

void UIManager::updateTooltips(float deltaTime) {
    if (!m_tooltipsEnabled) {
        return;
    }

    if (!m_hoveredComponents.empty()) {
        m_hoveredTooltip = m_hoveredComponents.back(); // Use topmost hovered component
        m_tooltipTimer += deltaTime;
    } else {
        m_hoveredTooltip.clear();
        m_tooltipTimer = 0.0f;
    }
}

void UIManager::renderComponent(SDL_Renderer* renderer, const std::shared_ptr<UIComponent>& component) {
    if (!component) return;

    switch (component->type) {
        case UIComponentType::BUTTON:
            renderButton(renderer, component);
            break;
        case UIComponentType::LABEL:
            renderLabel(renderer, component);
            break;
        case UIComponentType::PANEL:
            renderPanel(renderer, component);
            break;
        case UIComponentType::DIALOG:
            renderPanel(renderer, component);  // Dialogs render like panels
            break;
        case UIComponentType::PROGRESS_BAR:
            renderProgressBar(renderer, component);
            break;
        case UIComponentType::INPUT_FIELD:
            renderInputField(renderer, component);
            break;
        case UIComponentType::IMAGE:
            renderImage(renderer, component);
            break;
        case UIComponentType::SLIDER:
            renderSlider(renderer, component);
            break;
        case UIComponentType::CHECKBOX:
            renderCheckbox(renderer, component);
            break;
        case UIComponentType::LIST:
            renderList(renderer, component);
            break;
        case UIComponentType::EVENT_LOG:
            renderEventLog(renderer, component);
            break;
        case UIComponentType::TITLE:
            renderLabel(renderer, component); // Titles render like labels but with different styling
            break;
        case UIComponentType::TOOLTIP:
            // Tooltips are rendered separately
            break;
    }
}

void UIManager::renderButton(SDL_Renderer* renderer, const std::shared_ptr<UIComponent>& component) {
    if (!component) return;

    SDL_Color bgColor = component->style.backgroundColor;

    // Apply state-based color
    switch (component->state) {
        case UIState::HOVERED:
            bgColor = component->style.hoverColor;
            break;
        case UIState::PRESSED:
            bgColor = component->style.pressedColor;
            break;
        case UIState::DISABLED:
            bgColor = component->style.disabledColor;
            break;
        default:
            break;
    }

    // Draw background
    drawRect(renderer, component->bounds, bgColor, true);

    // Draw border
    if (component->style.borderWidth > 0) {
        drawBorder(renderer, component->bounds, component->style.borderColor, component->style.borderWidth);
    }

    // Draw text
    if (!component->text.empty()) {
        auto& fontManager = FontManager::Instance();
        fontManager.drawTextAligned(component->text, component->style.fontID,
                                   component->bounds.x + component->bounds.width / 2,
                                   component->bounds.y + component->bounds.height / 2,
                                   component->style.textColor, renderer, 0); // 0 = center
    }
}

void UIManager::renderLabel(SDL_Renderer* renderer, const std::shared_ptr<UIComponent>& component) {
    if (!component || component->text.empty()) return;

    int textX, textY, alignment;

    switch (component->style.textAlign) {
        case UIAlignment::CENTER_CENTER:
            textX = component->bounds.x + component->bounds.width / 2;
            textY = component->bounds.y + component->bounds.height / 2;
            alignment = 0; // center
            break;
        case UIAlignment::CENTER_RIGHT:
            textX = component->bounds.x + component->bounds.width - component->style.padding;
            textY = component->bounds.y + component->bounds.height / 2;
            alignment = 2; // right
            break;
        case UIAlignment::CENTER_LEFT:
            textX = component->bounds.x + component->style.padding;
            textY = component->bounds.y + component->bounds.height / 2;
            alignment = 1; // left
            break;
        case UIAlignment::TOP_CENTER:
            textX = component->bounds.x + component->bounds.width / 2;
            textY = component->bounds.y + component->style.padding;
            alignment = 4; // top-center
            break;
        case UIAlignment::TOP_LEFT:
            textX = component->bounds.x + component->style.padding;
            textY = component->bounds.y + component->style.padding;
            alignment = 3; // top-left
            break;
        case UIAlignment::TOP_RIGHT:
            textX = component->bounds.x + component->bounds.width - component->style.padding;
            textY = component->bounds.y + component->style.padding;
            alignment = 5; // top-right
            break;
        default:
            // CENTER_LEFT is default
            textX = component->bounds.x + component->style.padding;
            textY = component->bounds.y + component->bounds.height / 2;
            alignment = 1; // left
            break;
    }

    // Only use text backgrounds for components with transparent backgrounds
    bool needsBackground = component->style.useTextBackground && 
                          component->style.backgroundColor.a == 0;
    
    // Use a custom text drawing method that renders background and text together
    drawTextWithBackground(component->text, component->style.fontID, textX, textY, 
                          component->style.textColor, renderer, alignment, 
                          needsBackground, 
                          component->style.textBackgroundColor, 
                          component->style.textBackgroundPadding);
}

void UIManager::renderPanel(SDL_Renderer* renderer, const std::shared_ptr<UIComponent>& component) {
    if (!component) return;

    // Draw background
    drawRect(renderer, component->bounds, component->style.backgroundColor, true);

    // Draw border
    if (component->style.borderWidth > 0) {
        drawBorder(renderer, component->bounds, component->style.borderColor, component->style.borderWidth);
    }
}

void UIManager::renderProgressBar(SDL_Renderer* renderer, const std::shared_ptr<UIComponent>& component) {
    if (!component) return;

    // Draw background
    drawRect(renderer, component->bounds, component->style.backgroundColor, true);

    // Draw border
    if (component->style.borderWidth > 0) {
        drawBorder(renderer, component->bounds, component->style.borderColor, component->style.borderWidth);
    }

    // Calculate fill width
    float progress = (component->value - component->minValue) / (component->maxValue - component->minValue);
    progress = std::clamp(progress, 0.0f, 1.0f);

    int fillWidth = static_cast<int>(component->bounds.width * progress);
    if (fillWidth > 0) {
        UIRect fillRect = {component->bounds.x, component->bounds.y, fillWidth, component->bounds.height};
        drawRect(renderer, fillRect, component->style.hoverColor, true);
    }
}

void UIManager::renderInputField(SDL_Renderer* renderer, const std::shared_ptr<UIComponent>& component) {
    if (!component) return;

    SDL_Color bgColor = component->style.backgroundColor;
    if (component->state == UIState::FOCUSED) {
        bgColor = component->style.hoverColor;
    }

    // Draw background
    drawRect(renderer, component->bounds, bgColor, true);

    // Draw border
    SDL_Color borderColor = component->style.borderColor;
    if (component->state == UIState::FOCUSED) {
        borderColor = {100, 150, 255, 255}; // Blue focus border
    }
    drawBorder(renderer, component->bounds, borderColor, component->style.borderWidth);

    // Draw text or placeholder
    std::string displayText = component->text.empty() ? component->placeholder : component->text;
    if (!displayText.empty()) {
        SDL_Color textColor = component->text.empty() ?
            SDL_Color{128, 128, 128, 255} : component->style.textColor;

        auto& fontManager = FontManager::Instance();
        fontManager.drawTextAligned(displayText, component->style.fontID,
                                   component->bounds.x + component->style.padding,
                                   component->bounds.y + component->bounds.height / 2,
                                   textColor, renderer, 1); // 1 = left alignment
    }

    // Draw cursor if focused
    if (component->state == UIState::FOCUSED) {
        int cursorX = component->bounds.x + component->style.padding +
                     static_cast<int>(component->text.length() * 8); // Approximate char width
        drawRect(renderer, {cursorX, component->bounds.y + component->style.padding / 2,
                           1, component->bounds.height - component->style.padding},
                component->style.textColor, true);
    }
}

void UIManager::renderImage(SDL_Renderer* renderer, const std::shared_ptr<UIComponent>& component) {
    if (!component || component->textureID.empty()) return;

    auto& textureManager = TextureManager::Instance();
    if (textureManager.isTextureInMap(component->textureID)) {
        textureManager.draw(component->textureID, component->bounds.x, component->bounds.y,
                          component->bounds.width, component->bounds.height, renderer);
    }
}

void UIManager::renderSlider(SDL_Renderer* renderer, const std::shared_ptr<UIComponent>& component) {
    if (!component) return;

    // Draw track
    UIRect trackRect = {component->bounds.x, component->bounds.y + component->bounds.height / 2 - 2,
                       component->bounds.width, 4};
    drawRect(renderer, trackRect, component->style.backgroundColor, true);
    drawBorder(renderer, trackRect, component->style.borderColor, 1);

    // Calculate handle position
    float progress = (component->value - component->minValue) / (component->maxValue - component->minValue);
    progress = std::clamp(progress, 0.0f, 1.0f);

    int handleX = component->bounds.x + static_cast<int>((component->bounds.width - 16) * progress);
    UIRect handleRect = {handleX, component->bounds.y, 16, component->bounds.height};

    SDL_Color handleColor = component->style.hoverColor;
    if (component->state == UIState::PRESSED) {
        handleColor = component->style.pressedColor;
    }

    drawRect(renderer, handleRect, handleColor, true);
    drawBorder(renderer, handleRect, component->style.borderColor, 1);
}

void UIManager::renderCheckbox(SDL_Renderer* renderer, const std::shared_ptr<UIComponent>& component) {
    if (!component) return;

    int checkboxSize = std::min(component->bounds.height - 4, 20);
    UIRect checkRect = {component->bounds.x + 2, component->bounds.y + (component->bounds.height - checkboxSize) / 2,
                       checkboxSize, checkboxSize};

    // Draw checkbox background
    SDL_Color bgColor = component->checked ? component->style.hoverColor : component->style.backgroundColor;
    drawRect(renderer, checkRect, bgColor, true);
    drawBorder(renderer, checkRect, component->style.borderColor, 1);

    // Draw checkmark if checked
    if (component->checked) {
        // Use dark color for checkmark on light checkbox background
        SDL_SetRenderDrawColor(renderer, 20, 20, 20, 255);

        int cx = checkRect.x + checkRect.width / 2;
        int cy = checkRect.y + checkRect.height / 2;

        SDL_RenderLine(renderer, cx - 4, cy, cx - 1, cy + 3);
        SDL_RenderLine(renderer, cx - 1, cy + 3, cx + 4, cy - 2);
    }

    // Draw label text
    if (!component->text.empty()) {
        auto& fontManager = FontManager::Instance();
        fontManager.drawTextAligned(component->text, component->style.fontID,
                                   checkRect.x + checkRect.width + component->style.padding,
                                   component->bounds.y + component->bounds.height / 2,
                                   component->style.textColor, renderer, 1); // 1 = left alignment
    }
}

void UIManager::renderList(SDL_Renderer* renderer, const std::shared_ptr<UIComponent>& component) {
    if (!component) return;

    // Draw background
    drawRect(renderer, component->bounds, component->style.backgroundColor, true);
    drawBorder(renderer, component->bounds, component->style.borderColor, 1);

    // Draw items using configurable item height for better mouse accuracy
    int itemHeight = component->style.listItemHeight;
    int y = component->bounds.y;

    for (size_t i = 0; i < component->listItems.size(); ++i) {
        if (y >= component->bounds.y + component->bounds.height) break;

        UIRect itemRect = {component->bounds.x, y, component->bounds.width, itemHeight};

        // Highlight selected item
        if (static_cast<int>(i) == component->selectedIndex) {
            drawRect(renderer, itemRect, component->style.hoverColor, true);
        }

        // Draw item text
        auto& fontManager = FontManager::Instance();
        fontManager.drawTextAligned(component->listItems[i], component->style.fontID,
                                   itemRect.x + component->style.padding, itemRect.y + itemHeight / 2,
                                   component->style.textColor, renderer, 1); // 1 = left alignment

        y += itemHeight;
    }
}

void UIManager::renderEventLog(SDL_Renderer* renderer, const std::shared_ptr<UIComponent>& component) {
    if (!component) return;

    // Draw background
    drawRect(renderer, component->bounds, component->style.backgroundColor, true);
    drawBorder(renderer, component->bounds, component->style.borderColor, 1);

    // Draw event log entries (no selection, just display)
    int itemHeight = component->style.listItemHeight;
    int y = component->bounds.y;

    for (size_t i = 0; i < component->listItems.size(); ++i) {
        if (y >= component->bounds.y + component->bounds.height) break;

        UIRect itemRect = {component->bounds.x, y, component->bounds.width, itemHeight};

        // No selection highlighting for event logs (display-only)

        // Draw event entry text
        auto& fontManager = FontManager::Instance();
        fontManager.drawTextAligned(component->listItems[i], component->style.fontID,
                                   itemRect.x + component->style.padding + 8, itemRect.y + (itemHeight / 2) + 2,
                                   component->style.textColor, renderer, 1); // 1 = left alignment

        y += itemHeight;
    }
}

void UIManager::renderTooltip(SDL_Renderer* renderer) {
    if (m_hoveredTooltip.empty() || m_tooltipTimer < m_tooltipDelay) {
        return;
    }

    auto component = getComponent(m_hoveredTooltip);
    if (!component || component->text.empty()) {
        return;
    }

    // Skip tooltips for multi-line text (contains newlines)
    if (component->text.find('\n') != std::string::npos) {
        return;
    }

    // Position tooltip near mouse
    int tooltipWidth = static_cast<int>(component->text.length() * 8 + 16);
    int tooltipHeight = 24;

    UIRect tooltipRect = {
        static_cast<int>(m_lastMousePosition.getX() + 10),
        static_cast<int>(m_lastMousePosition.getY() - tooltipHeight - 10),
        tooltipWidth, tooltipHeight
    };

    // Ensure tooltip stays on screen
    auto& gameEngine = GameEngine::Instance();
    if (tooltipRect.x + tooltipRect.width > gameEngine.getWindowWidth()) {
        tooltipRect.x = gameEngine.getWindowWidth() - tooltipRect.width;
    }
    if (tooltipRect.y < 0) {
        tooltipRect.y = static_cast<int>(m_lastMousePosition.getY() + 20);
    }

    // Draw tooltip
    SDL_Color tooltipBg = {40, 40, 40, 240};
    drawRect(renderer, tooltipRect, tooltipBg, true);
    drawBorder(renderer, tooltipRect, {200, 200, 200, 255}, 1);

    auto& fontManager = FontManager::Instance();
    fontManager.drawTextAligned(component->text, m_globalFontID,
                               tooltipRect.x + tooltipRect.width / 2,
                               tooltipRect.y + tooltipRect.height / 2,
                               {255, 255, 255, 255}, renderer, 0); // 0 = center alignment
}

// Layout implementations
void UIManager::applyAbsoluteLayout(const std::shared_ptr<UILayout>& /* layout */) {
    // Absolute layout doesn't change component positions
}

void UIManager::applyFlowLayout(const std::shared_ptr<UILayout>& layout) {
    if (!layout) return;

    int currentX = layout->bounds.x;
    int currentY = layout->bounds.y;
    int maxHeight = 0;

    for (const auto& componentID : layout->childComponents) {
        auto component = getComponent(componentID);
        if (!component) continue;

        // Check if we need to wrap to next line
        if (currentX + component->bounds.width > layout->bounds.x + layout->bounds.width) {
            currentX = layout->bounds.x;
            currentY += maxHeight + layout->spacing;
            maxHeight = 0;
        }

        component->bounds.x = currentX;
        component->bounds.y = currentY;

        currentX += component->bounds.width + layout->spacing;
        maxHeight = std::max(maxHeight, component->bounds.height);
    }
}

void UIManager::applyGridLayout(const std::shared_ptr<UILayout>& layout) {
    if (!layout || layout->columns <= 0) return;

    int cellWidth = layout->bounds.width / layout->columns;
    int cellHeight = layout->bounds.height / layout->rows;

    for (size_t i = 0; i < layout->childComponents.size(); ++i) {
        auto component = getComponent(layout->childComponents[i]);
        if (!component) continue;

        int col = i % layout->columns;
        int row = i / layout->columns;

        component->bounds.x = layout->bounds.x + col * cellWidth;
        component->bounds.y = layout->bounds.y + row * cellHeight;
        component->bounds.width = cellWidth - layout->spacing;
        component->bounds.height = cellHeight - layout->spacing;
    }
}

void UIManager::applyStackLayout(const std::shared_ptr<UILayout>& layout) {
    if (!layout) return;

    int currentY = layout->bounds.y;

    for (const auto& componentID : layout->childComponents) {
        auto component = getComponent(componentID);
        if (!component) continue;

        component->bounds.x = layout->bounds.x;
        component->bounds.y = currentY;
        component->bounds.width = layout->bounds.width;

        currentY += component->bounds.height + layout->spacing;
    }
}

void UIManager::applyAnchorLayout(const std::shared_ptr<UILayout>& layout) {
    // TODO: Implement anchor-based layout
    applyAbsoluteLayout(layout);
}

// Utility helper implementations
void UIManager::drawRect(SDL_Renderer* renderer, const UIRect& rect, const SDL_Color& color, bool filled) {
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);

    SDL_FRect sdlRect = {static_cast<float>(rect.x), static_cast<float>(rect.y),
                        static_cast<float>(rect.width), static_cast<float>(rect.height)};
    if (filled) {
        SDL_RenderFillRect(renderer, &sdlRect);
    } else {
        SDL_RenderRect(renderer, &sdlRect);
    }
}

void UIManager::drawBorder(SDL_Renderer* renderer, const UIRect& rect, const SDL_Color& color, int width) {
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);

    for (int i = 0; i < width; ++i) {
        SDL_FRect borderRect = {static_cast<float>(rect.x - i), static_cast<float>(rect.y - i),
                               static_cast<float>(rect.width + 2*i), static_cast<float>(rect.height + 2*i)};
        SDL_RenderRect(renderer, &borderRect);
    }
}

void UIManager::drawTextWithBackground(const std::string& text, const std::string& fontID,
                                     int x, int y, SDL_Color textColor, SDL_Renderer* renderer,
                                     int alignment, bool useBackground, SDL_Color backgroundColor, int padding) {
    auto& fontManager = FontManager::Instance();
    
    // First render the text to get actual dimensions
    auto texture = fontManager.renderText(text, fontID, textColor, renderer);
    if (!texture) return;

    // Get the actual texture size
    float w, h;
    SDL_GetTextureSize(texture.get(), &w, &h);
    int width = static_cast<int>(w);
    int height = static_cast<int>(h);

    // Calculate position based on alignment (same as FontManager::drawTextAligned)
    float destX, destY;
    
    switch (alignment) {
        case 1: // Left alignment
            destX = static_cast<float>(x);
            destY = static_cast<float>(y - height/2.0f);
            break;
        case 2: // Right alignment
            destX = static_cast<float>(x - width);
            destY = static_cast<float>(y - height/2.0f);
            break;
        case 3: // Top-left alignment
            destX = static_cast<float>(x);
            destY = static_cast<float>(y);
            break;
        case 4: // Top-center alignment
            destX = static_cast<float>(x - width/2.0f);
            destY = static_cast<float>(y);
            break;
        case 5: // Top-right alignment
            destX = static_cast<float>(x - width);
            destY = static_cast<float>(y);
            break;
        default: // Center alignment (0)
            destX = static_cast<float>(x - width/2.0f);
            destY = static_cast<float>(y - height/2.0f);
            break;
    }

    // Render background if enabled
    if (useBackground) {
        UIRect bgRect;
        bgRect.x = static_cast<int>(destX) - padding;
        bgRect.y = static_cast<int>(destY) - padding;
        bgRect.width = width + (padding * 2);
        bgRect.height = height + (padding * 2);
        
        drawRect(renderer, bgRect, backgroundColor, true);
    }

    // Create a destination rectangle and render the text
    SDL_FRect dstRect = {destX, destY, static_cast<float>(width), static_cast<float>(height)};
    SDL_RenderTexture(renderer, texture.get(), nullptr, &dstRect);
}

UIRect UIManager::calculateTextBounds(const std::string& text, const std::string& /* fontID */,
                                     const UIRect& container, UIAlignment alignment) {
    // Simplified text bounds calculation
    int textWidth = static_cast<int>(text.length() * 8); // Approximate
    int textHeight = 16; // Approximate

    UIRect bounds = container;

    switch (alignment) {
        case UIAlignment::CENTER_CENTER:
            bounds.x = container.x + (container.width - textWidth) / 2;
            bounds.y = container.y + (container.height - textHeight) / 2;
            break;
        case UIAlignment::CENTER_LEFT:
            bounds.x = container.x;
            bounds.y = container.y + (container.height - textHeight) / 2;
            break;
        case UIAlignment::CENTER_RIGHT:
            bounds.x = container.x + container.width - textWidth;
            bounds.y = container.y + (container.height - textHeight) / 2;
            break;
        default:
            break;
    }

    bounds.width = textWidth;
    bounds.height = textHeight;
    return bounds;
}

SDL_Color UIManager::interpolateColor(const SDL_Color& start, const SDL_Color& end, float t) {
    return {
        static_cast<Uint8>(start.r + (end.r - start.r) * t),
        static_cast<Uint8>(start.g + (end.g - start.g) * t),
        static_cast<Uint8>(start.b + (end.b - start.b) * t),
        static_cast<Uint8>(start.a + (end.a - start.a) * t)
    };
}

UIRect UIManager::interpolateRect(const UIRect& start, const UIRect& end, float t) {
    return {
        static_cast<int>(start.x + (end.x - start.x) * t),
        static_cast<int>(start.y + (end.y - start.y) * t),
        static_cast<int>(start.width + (end.width - start.width) * t),
        static_cast<int>(start.height + (end.height - start.height) * t)
    };
}
