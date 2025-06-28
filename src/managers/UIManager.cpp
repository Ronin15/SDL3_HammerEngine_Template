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

    // Set global fonts to match what's loaded by FontManager's loadFontsForDisplay
    m_globalFontID = "fonts_UI_Arial";
    m_titleFontID = "fonts_title_Arial";
    m_uiFontID = "fonts_UI_Arial";
    
    // Platform-specific scaling approach to ensure compatibility
    #ifdef __APPLE__
    // On macOS, use 1.0 scaling since our aspect ratio-based logical resolution handles proper sizing
    m_globalScale = 1.0f;
    UI_INFO("macOS: Global scale set to 1.0 (aspect ratio-based logical resolution handles scaling)");
    #else
    // On other platforms, use consistent scaling with logical presentation
    m_globalScale = 1.0f;
    UI_INFO("Non-macOS: Global scale set to 1.0 (SDL3 logical presentation handles scaling)");
    #endif

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

    // Handle input (may need component access)
    handleInput();

    // Update animations (has its own locking)
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

void UIManager::render() {
    if (m_cachedRenderer) {
        render(m_cachedRenderer);
    }
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
    m_cachedRenderer = nullptr;

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

void UIManager::createButtonDanger(const std::string& id, const UIRect& bounds, const std::string& text) {
    auto component = std::make_shared<UIComponent>();
    component->id = id;
    component->type = UIComponentType::BUTTON_DANGER;
    component->bounds = bounds;
    component->text = text;
    component->style = m_currentTheme.getStyle(UIComponentType::BUTTON_DANGER);
    component->zOrder = 10; // Interactive elements on top

    m_components[id] = component;
}

void UIManager::createButtonSuccess(const std::string& id, const UIRect& bounds, const std::string& text) {
    auto component = std::make_shared<UIComponent>();
    component->id = id;
    component->type = UIComponentType::BUTTON_SUCCESS;
    component->bounds = bounds;
    component->text = text;
    component->style = m_currentTheme.getStyle(UIComponentType::BUTTON_SUCCESS);
    component->zOrder = 10; // Interactive elements on top

    m_components[id] = component;
}

void UIManager::createButtonWarning(const std::string& id, const UIRect& bounds, const std::string& text) {
    auto component = std::make_shared<UIComponent>();
    component->id = id;
    component->type = UIComponentType::BUTTON_WARNING;
    component->bounds = bounds;
    component->text = text;
    component->style = m_currentTheme.getStyle(UIComponentType::BUTTON_WARNING);
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
    
    // Apply auto-sizing after creation
    calculateOptimalSize(component);
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
    
    // Apply auto-sizing after creation
    calculateOptimalSize(component);
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
    
    // Enable auto-sizing for dynamic content-based sizing
    calculateOptimalSize(component);
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
    return component ? component->bounds : UIRect{0, 0, 0, 0};
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
        // Trigger auto-sizing to accommodate new content
        calculateOptimalSize(component);
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
        // Trigger auto-sizing (grow-only behavior will prevent shrinking)
        calculateOptimalSize(component);
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
        
        // Trigger auto-sizing to accommodate new content
        calculateOptimalSize(component);
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

        // Enforce max entries limit - scroll old events out
        int maxEntries = component->maxLength;
        if (maxEntries > 0 && static_cast<int>(component->listItems.size()) > maxEntries) {
            // Remove oldest events (FIFO behavior)
            while (static_cast<int>(component->listItems.size()) > maxEntries) {
                component->listItems.erase(component->listItems.begin());
            }
        }

        // Event logs are display-only for game events, no selection
        component->selectedIndex = -1;
        
        // Auto-scroll to bottom to show newest events
    }
}

void UIManager::clearEventLog(const std::string& logID) {
    auto component = getComponent(logID);
    if (component && component->type == UIComponentType::EVENT_LOG) {
        component->listItems.clear();
        component->selectedIndex = -1;

        // Event logs use fixed size for game events display
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

        // Event logs use fixed size for game events display
    }
}

void UIManager::setTitleAlignment(const std::string& titleID, UIAlignment alignment) {
    auto component = getComponent(titleID);
    if (component && component->type == UIComponentType::TITLE) {
        component->style.textAlign = alignment;
        
        // If setting to CENTER_CENTER and auto-sizing is enabled, recalculate position
        if (alignment == UIAlignment::CENTER_CENTER && component->autoSize && component->autoWidth) {
            const auto& gameEngine = GameEngine::Instance();
            int windowWidth = gameEngine.getLogicalWidth();
            component->bounds.x = (windowWidth - component->bounds.width) / 2;
        }
    }
}

void UIManager::centerTitleInContainer(const std::string& titleID, int containerX, int containerWidth) {
    auto component = getComponent(titleID);
    if (component && component->type == UIComponentType::TITLE) {
        // Center the auto-sized title within the container
        int titleWidth = component->bounds.width;
        component->bounds.x = containerX + (containerWidth - titleWidth) / 2;
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

    // Button Danger style - red buttons for Back, Quit, Exit, Delete, etc.
    UIStyle dangerButtonStyle = buttonStyle;
    dangerButtonStyle.backgroundColor = {180, 50, 50, 255};
    dangerButtonStyle.hoverColor = {200, 70, 70, 255};
    dangerButtonStyle.pressedColor = {160, 30, 30, 255};
    lightTheme.componentStyles[UIComponentType::BUTTON_DANGER] = dangerButtonStyle;

    // Button Success style - green buttons for Save, Confirm, Accept, etc.
    UIStyle successButtonStyle = buttonStyle;
    successButtonStyle.backgroundColor = {50, 150, 50, 255};
    successButtonStyle.hoverColor = {70, 170, 70, 255};
    successButtonStyle.pressedColor = {30, 130, 30, 255};
    lightTheme.componentStyles[UIComponentType::BUTTON_SUCCESS] = successButtonStyle;

    // Button Warning style - orange buttons for Caution, Reset, etc.
    UIStyle warningButtonStyle = buttonStyle;
    warningButtonStyle.backgroundColor = {200, 140, 50, 255};
    warningButtonStyle.hoverColor = {220, 160, 70, 255};
    warningButtonStyle.pressedColor = {180, 120, 30, 255};
    lightTheme.componentStyles[UIComponentType::BUTTON_WARNING] = warningButtonStyle;

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
    // Calculate list item height based on font metrics
    listStyle.listItemHeight = 32; // Will be calculated dynamically during rendering
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
    tooltipStyle.fontID = "fonts_tooltip_Arial";
    lightTheme.componentStyles[UIComponentType::TOOLTIP] = tooltipStyle;

    // Image component uses transparent background
    UIStyle imageStyle;
    imageStyle.backgroundColor = {0, 0, 0, 0};
    imageStyle.fontID = "fonts_UI_Arial";
    lightTheme.componentStyles[UIComponentType::IMAGE] = imageStyle;

    // Event log style - similar to list but optimized for display-only
    UIStyle eventLogStyle = listStyle;
    // Calculate event log item height based on font metrics
    eventLogStyle.listItemHeight = 24; // Will be calculated dynamically during rendering
    eventLogStyle.backgroundColor = {245, 245, 250, 160}; // Semi-transparent light background
    eventLogStyle.textColor = {0, 0, 0, 255}; // Black text for maximum contrast
    eventLogStyle.borderColor = {120, 120, 140, 180}; // Less transparent border
    lightTheme.componentStyles[UIComponentType::EVENT_LOG] = eventLogStyle;

    // Title style - large, prominent text for headings
    UIStyle titleStyle;
    titleStyle.backgroundColor = {0, 0, 0, 0}; // Transparent background
    titleStyle.textColor = { 0, 198, 230, 255}; // Dark Cyan color for titles
    titleStyle.fontSize = 24; // Use native 24px font size
    titleStyle.textAlign = UIAlignment::CENTER_LEFT;
    titleStyle.fontID = "fonts_title_Arial";
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

    // Button Danger style - red buttons for Back, Quit, Exit, Delete, etc.
    UIStyle dangerButtonStyle = buttonStyle;
    dangerButtonStyle.backgroundColor = {200, 60, 60, 255};
    dangerButtonStyle.hoverColor = {220, 80, 80, 255};
    dangerButtonStyle.pressedColor = {180, 40, 40, 255};
    darkTheme.componentStyles[UIComponentType::BUTTON_DANGER] = dangerButtonStyle;

    // Button Success style - green buttons for Save, Confirm, Accept, etc.
    UIStyle successButtonStyle = buttonStyle;
    successButtonStyle.backgroundColor = {60, 160, 60, 255};
    successButtonStyle.hoverColor = {80, 180, 80, 255};
    successButtonStyle.pressedColor = {40, 140, 40, 255};
    darkTheme.componentStyles[UIComponentType::BUTTON_SUCCESS] = successButtonStyle;

    // Button Warning style - orange buttons for Caution, Reset, etc.
    UIStyle warningButtonStyle = buttonStyle;
    warningButtonStyle.backgroundColor = {220, 150, 60, 255};
    warningButtonStyle.hoverColor = {240, 170, 80, 255};
    warningButtonStyle.pressedColor = {200, 130, 40, 255};
    darkTheme.componentStyles[UIComponentType::BUTTON_WARNING] = warningButtonStyle;

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
    // Calculate list item height based on font metrics
    listStyle.listItemHeight = 32; // Will be calculated dynamically during rendering
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
    tooltipStyle.fontID = "fonts_tooltip_Arial";
    darkTheme.componentStyles[UIComponentType::TOOLTIP] = tooltipStyle;

    // Image component uses transparent background
    UIStyle imageStyle;
    imageStyle.backgroundColor = {0, 0, 0, 0};
    imageStyle.fontID = "fonts_UI_Arial";
    darkTheme.componentStyles[UIComponentType::IMAGE] = imageStyle;

    // Event log style - similar to list but optimized for display-only
    UIStyle eventLogStyle = listStyle;
    // Calculate event log item height based on font metrics
    eventLogStyle.listItemHeight = 24; // Will be calculated dynamically during rendering
    eventLogStyle.backgroundColor = {25, 30, 35, 80}; // Highly transparent dark background
    eventLogStyle.textColor = {255, 255, 255, 255}; // Pure white text for maximum contrast
    eventLogStyle.borderColor = {100, 120, 140, 100}; // Highly transparent blue-gray border
    darkTheme.componentStyles[UIComponentType::EVENT_LOG] = eventLogStyle;

    // Title style - large, prominent text for headings
    UIStyle titleStyle;
    titleStyle.backgroundColor = {0, 0, 0, 0}; // Transparent background
    titleStyle.textColor = { 0, 198, 230, 255}; // Dark Cyan color for titles
    titleStyle.fontSize = 24; // Use native 24px font size
    titleStyle.textAlign = UIAlignment::CENTER_LEFT;
    titleStyle.fontID = "fonts_title_Arial";
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
    // Comprehensive cleanup for safe state transitions
    
    // Clear all UI components
    m_components.clear();
    
    // Clear all layouts
    m_layouts.clear();
    
    // Stop and clear all animations
    m_animations.clear();
    
    // Clear all interaction state
    m_clickedButtons.clear();
    m_hoveredComponents.clear();
    m_focusedComponent.clear();
    m_hoveredTooltip.clear();
    m_tooltipTimer = 0.0f;
    
    // Clear event log states
    m_eventLogStates.clear();
    
    // Remove overlay if present
    removeOverlay();
    
    // Reset to default theme
    resetToDefaultTheme();
    
    // Reset mouse state
    m_lastMousePosition = Vector2D(0, 0);
    m_mousePressed = false;
    m_mouseReleased = false;
    
    // Reset global settings to defaults
    m_globalStyle = UIStyle{};
    m_globalFontID = "fonts_UI_Arial";
    m_globalScale = 1.0f;
    
    UI_INFO("UIManager prepared for state transition");
}

void UIManager::prepareForStateTransition() {
    // Simplified public interface that delegates to the comprehensive cleanup
    cleanupForStateTransition();
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
    const auto& inputManager = InputManager::Instance();

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

        // InputManager already converts coordinates to logical coordinates
        int mouseX = static_cast<int>(mousePos.getX());
        int mouseY = static_cast<int>(mousePos.getY());

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
                component->type == UIComponentType::BUTTON_DANGER ||
                component->type == UIComponentType::BUTTON_SUCCESS ||
                component->type == UIComponentType::BUTTON_WARNING ||
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
                    if (component->type == UIComponentType::BUTTON ||
                        component->type == UIComponentType::BUTTON_DANGER ||
                        component->type == UIComponentType::BUTTON_SUCCESS ||
                        component->type == UIComponentType::BUTTON_WARNING) {
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
                // Calculate item height dynamically based on current font metrics
                auto& fontManager = FontManager::Instance();
                int lineHeight = 0;
                int itemHeight = 32; // Default fallback
                if (fontManager.getFontMetrics(component->style.fontID, &lineHeight, nullptr, nullptr)) {
                    itemHeight = lineHeight + 8; // Add padding for better mouse accuracy
                }
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
        case UIComponentType::BUTTON_DANGER:
        case UIComponentType::BUTTON_SUCCESS:
        case UIComponentType::BUTTON_WARNING:
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
        #ifdef __APPLE__
        // On macOS, use logical coordinates directly - SDL3 handles scaling automatically
        int centerX = component->bounds.x + component->bounds.width / 2;
        int centerY = component->bounds.y + component->bounds.height / 2;
        #else
        // Use logical coordinates directly - SDL3 logical presentation handles scaling
        int centerX = component->bounds.x + component->bounds.width / 2;
        int centerY = component->bounds.y + component->bounds.height / 2;
        #endif
        fontManager.drawTextAligned(component->text, component->style.fontID,
                                   centerX, centerY,
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
    
    #ifdef __APPLE__
    // On macOS, use logical coordinates directly - SDL3 handles scaling automatically
    int finalTextX = static_cast<int>(textX);
    int finalTextY = static_cast<int>(textY);
    #else
    // Use logical coordinates directly - SDL3 logical presentation handles scaling
    int finalTextX = static_cast<int>(textX);
    int finalTextY = static_cast<int>(textY);
    #endif
    
    // Use a custom text drawing method that renders background and text together
    drawTextWithBackground(component->text, component->style.fontID, finalTextX, finalTextY,
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
        #ifdef __APPLE__
        // On macOS, use logical coordinates directly - SDL3 handles scaling automatically
        int textX = component->bounds.x + component->style.padding;
        int textY = component->bounds.y + component->bounds.height / 2;
        #else
        // Use logical coordinates directly - SDL3 logical presentation handles scaling
        int textX = component->bounds.x + component->style.padding;
        int textY = component->bounds.y + component->bounds.height / 2;
        #endif
        fontManager.drawTextAligned(displayText, component->style.fontID,
                                   textX, textY,
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
        #ifdef __APPLE__
        // On macOS, use logical coordinates directly - SDL3 handles scaling automatically
        int textX = checkRect.x + checkRect.width + component->style.padding;
        int textY = component->bounds.y + component->bounds.height / 2;
        #else
        // Use logical coordinates directly - SDL3 logical presentation handles scaling
        int textX = checkRect.x + checkRect.width + component->style.padding;
        int textY = component->bounds.y + component->bounds.height / 2;
        #endif
        fontManager.drawTextAligned(component->text, component->style.fontID,
                                   textX, textY,
                                   component->style.textColor, renderer, 1); // 1 = left alignment
    }
}

void UIManager::renderList(SDL_Renderer* renderer, const std::shared_ptr<UIComponent>& component) {
    if (!component) return;

    // Draw background
    drawRect(renderer, component->bounds, component->style.backgroundColor, true);
    drawBorder(renderer, component->bounds, component->style.borderColor, 1);

    // Calculate item height dynamically based on current font metrics
    auto& fontManager = FontManager::Instance();
    int lineHeight = 0;
    int itemHeight = 32; // Default fallback
    if (fontManager.getFontMetrics(component->style.fontID, &lineHeight, nullptr, nullptr)) {
        itemHeight = lineHeight + 8; // Add padding for better mouse accuracy
    }
    int y = component->bounds.y + component->style.padding;
    int maxY = component->bounds.y + component->bounds.height - component->style.padding;

    for (size_t i = 0; i < component->listItems.size(); ++i) {
        if (y + itemHeight > maxY) break;

        UIRect itemRect = {component->bounds.x + component->style.padding, y, 
                          component->bounds.width - (component->style.padding * 2), itemHeight};

        // Highlight selected item
        if (static_cast<int>(i) == component->selectedIndex) {
            drawRect(renderer, itemRect, component->style.hoverColor, true);
        }

        // Draw item text
        #ifdef __APPLE__
        // On macOS, use logical coordinates directly - SDL3 handles scaling automatically
        int textX = itemRect.x + component->style.padding;
        int textY = itemRect.y + itemHeight / 2;
        #else
        // Use logical coordinates directly - SDL3 logical presentation handles scaling
        int textX = itemRect.x + component->style.padding;
        int textY = itemRect.y + itemHeight / 2;
        #endif
        fontManager.drawTextAligned(component->listItems[i], component->style.fontID,
                                   textX, textY,
                                   component->style.textColor, renderer, 1); // 1 = left alignment

        y += itemHeight;
    }
}

void UIManager::renderEventLog(SDL_Renderer* renderer, const std::shared_ptr<UIComponent>& component) {
    if (!component) return;

    // Draw background panel
    drawRect(renderer, component->bounds, component->style.backgroundColor, true);
    drawBorder(renderer, component->bounds, component->style.borderColor, 1);

    if (component->listItems.empty()) {
        return; // Nothing to render
    }

    // Calculate available display area
    int contentX = component->bounds.x + component->style.padding;
    int contentY = component->bounds.y + component->style.padding;
    int contentWidth = component->bounds.width - (component->style.padding * 2);
    int contentHeight = component->bounds.height - (component->style.padding * 2);
    int maxY = component->bounds.y + component->bounds.height - component->style.padding;

    auto& fontManager = FontManager::Instance();
    
    // Start from the bottom and work upward to show most recent entries
    std::vector<std::pair<std::string, int>> wrappedEntries;
    int totalHeight = 0;
    
    // Pre-process entries to calculate wrapped text heights (from newest to oldest)
    for (int i = static_cast<int>(component->listItems.size()) - 1; i >= 0; --i) {
        const std::string& entry = component->listItems[i];
        int entryWidth = 0, entryHeight = 0;
        
        // Use FontManager's word wrapping to measure text
        if (fontManager.measureTextWithWrapping(entry, component->style.fontID, contentWidth, &entryWidth, &entryHeight)) {
            wrappedEntries.insert(wrappedEntries.begin(), {entry, entryHeight + 4});
            totalHeight += entryHeight + 4;
        } else {
            // Fallback to single line height - calculate dynamically based on font metrics
            int fontLineHeight = 0;
            int lineHeight = 24; // Default fallback
            if (fontManager.getFontMetrics(component->style.fontID, &fontLineHeight, nullptr, nullptr)) {
                lineHeight = fontLineHeight + 4; // Tighter spacing for event log
            }
            wrappedEntries.insert(wrappedEntries.begin(), {entry, lineHeight + 4});
            totalHeight += lineHeight + 4;
        }
        
        // Stop if we have enough content to fill the display area
        if (totalHeight >= contentHeight) {
            break;
        }
    }
    
    // Render visible entries
    if (totalHeight > contentHeight) {
        // Find how many entries we can fit starting from the bottom
        int remainingHeight = contentHeight;
        int visibleEntries = 0;
        
        for (int i = static_cast<int>(wrappedEntries.size()) - 1; i >= 0; --i) {
            if (remainingHeight >= wrappedEntries[i].second) {
                remainingHeight -= wrappedEntries[i].second;
                visibleEntries++;
            } else {
                break;
            }
        }
        
        // Render only the visible entries
        int startIndex = static_cast<int>(wrappedEntries.size()) - visibleEntries;
        int y = contentY;
        
        for (int i = startIndex; i < static_cast<int>(wrappedEntries.size()); ++i) {
            if (y + wrappedEntries[i].second > maxY) break;
            
            #ifdef __APPLE__
            int textX = contentX;
            int textY = y;
            #else
            // Use logical coordinates directly - SDL3 logical presentation handles scaling
            int textX = contentX;
            int textY = y;
            #endif
            
            // Use FontManager's word wrapping to draw text
            fontManager.drawTextWithWrapping(wrappedEntries[i].first, component->style.fontID,
                                           textX, textY, contentWidth,
                                           component->style.textColor, renderer);
            
            y += wrappedEntries[i].second;
        }
    } else {
        // All content fits, render normally
        int y = contentY;
        
        for (const auto& entry : wrappedEntries) {
            if (y + entry.second > maxY) break;
            
            #ifdef __APPLE__
            int textX = contentX;
            int textY = y;
            #else
            // Use logical coordinates directly - SDL3 logical presentation handles scaling
            int textX = contentX;
            int textY = y;
            #endif
            
            // Use FontManager's word wrapping to draw text
            fontManager.drawTextWithWrapping(entry.first, component->style.fontID,
                                           textX, textY, contentWidth,
                                           component->style.textColor, renderer);
            
            y += entry.second;
        }
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

    // Skip tooltips for titles
    if (component->type == UIComponentType::TITLE) {
        return;
    }

    // Skip tooltips for multi-line text (contains newlines)
    if (component->text.find('\n') != std::string::npos) {
        return;
    }

    // Calculate actual text dimensions for content-aware sizing
    auto& fontManager = FontManager::Instance();
    auto tooltipTexture = fontManager.renderText(component->text, component->style.fontID,
                                               component->style.textColor, renderer);
    
    int tooltipWidth = 200; // fallback width
    int tooltipHeight = 32; // fallback height
    
    if (tooltipTexture) {
        float textW, textH;
        SDL_GetTextureSize(tooltipTexture.get(), &textW, &textH);
        tooltipWidth = static_cast<int>(textW) + 16; // Add padding
        tooltipHeight = static_cast<int>(textH) + 8; // Add padding
    }

    UIRect tooltipRect = {
        static_cast<int>(m_lastMousePosition.getX() + 10),
        static_cast<int>(m_lastMousePosition.getY() - tooltipHeight - 10),
        tooltipWidth, tooltipHeight
    };

    // Ensure tooltip stays on screen
    const auto& gameEngine = GameEngine::Instance();
    if (tooltipRect.x + tooltipRect.width > gameEngine.getLogicalWidth()) {
        tooltipRect.x = gameEngine.getLogicalWidth() - tooltipRect.width;
    }
    if (tooltipRect.y < 0) {
        tooltipRect.y = static_cast<int>(m_lastMousePosition.getY() + 20);
    }
    if (tooltipRect.y + tooltipRect.height > gameEngine.getLogicalHeight()) {
        tooltipRect.y = gameEngine.getLogicalHeight() - tooltipRect.height;
    }

    // Draw tooltip
    SDL_Color tooltipBg = {40, 40, 40, 240};
    drawRect(renderer, tooltipRect, tooltipBg, true);
    drawBorder(renderer, tooltipRect, {200, 200, 200, 255}, 1);

    #ifdef __APPLE__
    // On macOS, use logical coordinates directly - SDL3 handles scaling automatically
    int centerX = tooltipRect.x + tooltipRect.width / 2;
    int centerY = tooltipRect.y + tooltipRect.height / 2;
    #else
    // Use logical coordinates directly - SDL3 logical presentation handles scaling
    int centerX = tooltipRect.x + tooltipRect.width / 2;
    int centerY = tooltipRect.y + tooltipRect.height / 2;
    #endif
    fontManager.drawTextAligned(component->text, component->style.fontID,
                               centerX, centerY,
                               component->style.textColor, renderer, 0); // 0 = center alignment
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

// Auto-sizing implementation
void UIManager::calculateOptimalSize(const std::string& id) {
    auto component = getComponent(id);
    if (component) {
        calculateOptimalSize(component);
    }
}

void UIManager::calculateOptimalSize(std::shared_ptr<UIComponent> component) {
    if (!component || !component->autoSize) {
        return;
    }

    int contentWidth = 0;
    int contentHeight = 0;
    
    if (!measureComponentContent(component, &contentWidth, &contentHeight)) {
        return; // Failed to measure content
    }



    // Apply content padding
    int totalWidth = contentWidth + (component->contentPadding * 2);
    int totalHeight = contentHeight + (component->contentPadding * 2);

    // Implement grow-only behavior for lists to prevent shrinking
    if (component->type == UIComponentType::LIST) {
        // Update minimum bounds to current size to prevent shrinking
        component->minBounds.width = std::max(component->minBounds.width, component->bounds.width);
        component->minBounds.height = std::max(component->minBounds.height, component->bounds.height);
    }

    // Apply size constraints - ONLY modify width/height, preserve x/y position
    if (component->autoWidth) {
        int oldWidth = component->bounds.width;
        component->bounds.width = std::max(component->minBounds.width, 
                                         std::min(totalWidth, component->maxBounds.width));
        
 
        // Automatically center only titles and labels with CENTER alignment when width changes
        if (component->style.textAlign == UIAlignment::CENTER_CENTER && 
            component->bounds.width != oldWidth &&
            (component->type == UIComponentType::TITLE || component->type == UIComponentType::LABEL)) {
            // Get logical width for centering calculation
            const auto& gameEngine = GameEngine::Instance();
            int windowWidth = gameEngine.getLogicalWidth();
            component->bounds.x = (windowWidth - component->bounds.width) / 2;
        }
    }
    
    if (component->autoHeight) {
        component->bounds.height = std::max(component->minBounds.height, 
                                          std::min(totalHeight, component->maxBounds.height));
    }
    
    // CRITICAL: Never modify component->bounds.x or component->bounds.y
    // Auto-sizing only affects dimensions, not position

    // Trigger content changed callback if present
    if (component->onContentChanged) {
        component->onContentChanged();
    }
}

bool UIManager::measureComponentContent(const std::shared_ptr<UIComponent>& component, int* width, int* height) {
    if (!component || !width || !height) {
        return false;
    }

    auto& fontManager = FontManager::Instance();
    
    switch (component->type) {
        case UIComponentType::BUTTON:
        case UIComponentType::BUTTON_DANGER:
        case UIComponentType::BUTTON_SUCCESS:
        case UIComponentType::BUTTON_WARNING:
        case UIComponentType::LABEL:
        case UIComponentType::TITLE:
            if (!component->text.empty()) {
                // Check if text contains newlines - use multiline measurement if so
                if (component->text.find('\n') != std::string::npos) {
                    return fontManager.measureMultilineText(component->text, component->style.fontID, 0, width, height);
                } else {
                    return fontManager.measureText(component->text, component->style.fontID, width, height);
                }
            }
            *width = component->minBounds.width;
            *height = component->minBounds.height;
            return true;



        case UIComponentType::INPUT_FIELD:
            // For input fields, measure placeholder or current text
            if (!component->text.empty()) {
                fontManager.measureText(component->text, component->style.fontID, width, height);
            } else if (!component->placeholder.empty()) {
                fontManager.measureText(component->placeholder, component->style.fontID, width, height);
            } else {
                // Default to reasonable input field size
                fontManager.measureText("Sample Text", component->style.fontID, width, height);
            }
            // Input fields need extra space for cursor and interaction
            *width += 20;
            return true;

        case UIComponentType::LIST:
        {
            // Calculate height based on font metrics dynamically
            int lineHeight = 0;
            int itemHeight = 32; // Default fallback
            if (fontManager.getFontMetrics(component->style.fontID, &lineHeight, nullptr, nullptr)) {
                itemHeight = lineHeight + 8; // Add padding for better mouse accuracy
            } else {
                // If font metrics fail, use reasonable fallback based on expected font sizes
                // Assume 21px font (typical for UI) + 8px padding = 29px
                itemHeight = 29;
            }
            
            // Calculate based on list items and item height
            if (!component->listItems.empty()) {
                int maxItemWidth = 0;
                for (const auto& item : component->listItems) {
                    int itemWidth = 0;
                    if (fontManager.measureText(item, component->style.fontID, &itemWidth, nullptr)) {
                        maxItemWidth = std::max(maxItemWidth, itemWidth);
                    } else {
                        // If text measurement fails, estimate based on character count
                        // Assume ~12px per character for UI fonts
                        maxItemWidth = std::max(maxItemWidth, static_cast<int>(item.length() * 12));
                    }
                }
                *width = std::max(maxItemWidth + 20, 150); // Add scrollbar space, minimum 150px
                *height = itemHeight * static_cast<int>(component->listItems.size());
            } else {
                // Provide reasonable defaults for empty lists
                *width = 200; // Default width
                *height = itemHeight * 3; // Height for 3 items as reasonable default
            }
            return true;
        }

        case UIComponentType::EVENT_LOG:
            // Fixed size for game event display
            *width = component->bounds.width;
            *height = component->bounds.height;
            return true;

        case UIComponentType::TOOLTIP:
            if (!component->text.empty()) {
                return fontManager.measureText(component->text, component->style.fontID, width, height);
            }
            break;

        default:
            // For other component types, use current bounds or minimums
            *width = std::max(component->bounds.width, component->minBounds.width);
            *height = std::max(component->bounds.height, component->minBounds.height);
            return true;
    }

    // Fallback to minimum bounds
    *width = component->minBounds.width;
    *height = component->minBounds.height;
    return true;
}

void UIManager::invalidateLayout(const std::string& layoutID) {
    // Mark layout for recalculation on next update
    // For now, immediately recalculate
    recalculateLayout(layoutID);
}

void UIManager::recalculateLayout(const std::string& layoutID) {
    auto layout = getLayout(layoutID);
    if (!layout) {
        return;
    }

    // First, auto-size all child components
    for (const auto& componentID : layout->childComponents) {
        calculateOptimalSize(componentID);
    }

    // Then apply the layout with new sizes
    updateLayout(layoutID);
}

void UIManager::enableAutoSizing(const std::string& id, bool enable) {
    auto component = getComponent(id);
    if (component) {
        component->autoSize = enable;
        if (enable) {
            calculateOptimalSize(component);
        }
    }
}

void UIManager::setAutoSizingConstraints(const std::string& id, const UIRect& minBounds, const UIRect& maxBounds) {
    auto component = getComponent(id);
    if (component) {
        component->minBounds = minBounds;
        component->maxBounds = maxBounds;
        if (component->autoSize) {
            calculateOptimalSize(component);
        }
    }
}

// Auto-detection methods
int UIManager::getLogicalWidth() const {
    const auto& gameEngine = GameEngine::Instance();
    return gameEngine.getLogicalWidth();
}

int UIManager::getLogicalHeight() const {
    const auto& gameEngine = GameEngine::Instance();
    return gameEngine.getLogicalHeight();
}

// Auto-detecting overlay creation
void UIManager::createOverlay() {
    int logicalWidth = getLogicalWidth();
    int logicalHeight = getLogicalHeight();
    
    // Check if we're using logical presentation (macOS)
    int overlayWidth = logicalWidth;
    int overlayHeight = logicalHeight;
    
    if (m_cachedRenderer) {
        int actualWidth, actualHeight;
        if (SDL_GetCurrentRenderOutputSize(m_cachedRenderer, &actualWidth, &actualHeight)) {
            // If actual render size differs significantly from logical size, 
            // we're likely using logical presentation and should use actual size for overlay
            if (actualWidth != logicalWidth || actualHeight != logicalHeight) {
                overlayWidth = actualWidth;
                overlayHeight = actualHeight;
            }
        }
    }
    
    createOverlay(overlayWidth, overlayHeight);
}

// Convenience positioning methods
void UIManager::createTitleAtTop(const std::string& id, const std::string& text, int height) {
    int width = getLogicalWidth();
    createTitle(id, {0, 10, width, height}, text);
    setTitleAlignment(id, UIAlignment::CENTER_CENTER);
}

void UIManager::createButtonAtBottom(const std::string& id, const std::string& text, int width, int height) {
    int logicalHeight = getLogicalHeight();
    createButtonDanger(id, {20, logicalHeight - height - 20, width, height}, text);
}

void UIManager::createCenteredDialog(const std::string& id, int width, int height, const std::string& theme) {
    int logicalWidth = getLogicalWidth();
    int logicalHeight = getLogicalHeight();
    int x = (logicalWidth - width) / 2;
    int y = (logicalHeight - height) / 2;
    
    // Use actual render output size for overlay if available
    int overlayWidth = logicalWidth;
    int overlayHeight = logicalHeight;
    
    if (m_cachedRenderer) {
        int actualWidth, actualHeight;
        if (SDL_GetCurrentRenderOutputSize(m_cachedRenderer, &actualWidth, &actualHeight)) {
            if (actualWidth != logicalWidth || actualHeight != logicalHeight) {
                overlayWidth = actualWidth;
                overlayHeight = actualHeight;
            }
        }
    }
    
    createModal(id, {x, y, width, height}, theme, overlayWidth, overlayHeight);
}
