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

    // Initialize default theme
    setDefaultTheme();

    // Set global font to match what's loaded for UI
    m_globalFontID = "fonts_UI_Arial";

    // Clear any existing data
    m_components.clear();
    m_layouts.clear();
    m_animations.clear();
    m_clickedButtons.clear();
    m_hoveredComponents.clear();
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

    // Sort components by z-order for proper rendering
    sortComponentsByZOrder();
}

void UIManager::render(SDL_Renderer* renderer) {
    if (m_isShutdown || !renderer) {
        return;
    }

    // Render components in z-order
    for (const auto& [id, component] : m_components) {
        if (component && component->visible) {
            renderComponent(renderer, component);
        }
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

    // Reset renderer to GameEngine's default color (FORGE_GRAY: 31, 32, 34, 255)
    SDL_SetRenderDrawColor(renderer, 31, 32, 34, 255);
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

    m_components[id] = component;
}

void UIManager::createLabel(const std::string& id, const UIRect& bounds, const std::string& text) {
    auto component = std::make_shared<UIComponent>();
    component->id = id;
    component->type = UIComponentType::LABEL;
    component->bounds = bounds;
    component->text = text;
    component->style = m_currentTheme.getStyle(UIComponentType::LABEL);

    m_components[id] = component;
}

void UIManager::createPanel(const std::string& id, const UIRect& bounds) {
    auto component = std::make_shared<UIComponent>();
    component->id = id;
    component->type = UIComponentType::PANEL;
    component->bounds = bounds;
    component->style = m_currentTheme.getStyle(UIComponentType::PANEL);

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

    m_components[id] = component;
}

void UIManager::createInputField(const std::string& id, const UIRect& bounds, const std::string& placeholder) {
    auto component = std::make_shared<UIComponent>();
    component->id = id;
    component->type = UIComponentType::INPUT_FIELD;
    component->bounds = bounds;
    component->placeholder = placeholder;
    component->style = m_currentTheme.getStyle(UIComponentType::INPUT_FIELD);

    m_components[id] = component;
}

void UIManager::createImage(const std::string& id, const UIRect& bounds, const std::string& textureID) {
    auto component = std::make_shared<UIComponent>();
    component->id = id;
    component->type = UIComponentType::IMAGE;
    component->bounds = bounds;
    component->textureID = textureID;
    component->style = m_currentTheme.getStyle(UIComponentType::IMAGE);

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

    m_components[id] = component;
}

void UIManager::createList(const std::string& id, const UIRect& bounds) {
    auto component = std::make_shared<UIComponent>();
    component->id = id;
    component->type = UIComponentType::LIST;
    component->bounds = bounds;
    component->selectedIndex = -1;
    component->style = m_currentTheme.getStyle(UIComponentType::LIST);

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

void UIManager::clearAllComponents() {
    m_components.clear();
    m_layouts.clear();
    m_focusedComponent.clear();
    m_hoveredTooltip.clear();
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
    if (component && component->type == UIComponentType::LIST &&
        index >= -1 && index < static_cast<int>(component->listItems.size())) {
        component->selectedIndex = index;
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

    // Apply theme to all existing components
    for (const auto& [id, component] : m_components) {
        if (component) {
            component->style = m_currentTheme.getStyle(component->type);
        }
    }
}

void UIManager::setDefaultTheme() {
    UITheme defaultTheme;
    defaultTheme.name = "default";

    // Button style
    UIStyle buttonStyle;
    buttonStyle.backgroundColor = {70, 70, 70, 255};
    buttonStyle.borderColor = {100, 100, 100, 255};
    buttonStyle.textColor = {255, 255, 255, 255};
    buttonStyle.hoverColor = {90, 90, 90, 255};
    buttonStyle.pressedColor = {50, 50, 50, 255};
    buttonStyle.textAlign = UIAlignment::CENTER_CENTER;
    buttonStyle.fontID = "fonts_UI_Arial";
    defaultTheme.componentStyles[UIComponentType::BUTTON] = buttonStyle;

    // Label style
    UIStyle labelStyle;
    labelStyle.backgroundColor = {0, 0, 0, 0}; // Transparent
    labelStyle.textColor = {255, 255, 255, 255};
    labelStyle.textAlign = UIAlignment::CENTER_LEFT;
    labelStyle.fontID = "fonts_UI_Arial";
    defaultTheme.componentStyles[UIComponentType::LABEL] = labelStyle;

    // Panel style
    UIStyle panelStyle;
    panelStyle.backgroundColor = {40, 40, 40, 200};
    panelStyle.borderColor = {80, 80, 80, 255};
    defaultTheme.componentStyles[UIComponentType::PANEL] = panelStyle;

    // Progress bar style
    UIStyle progressStyle;
    progressStyle.backgroundColor = {30, 30, 30, 255};
    progressStyle.borderColor = {100, 100, 100, 255};
    progressStyle.hoverColor = {0, 150, 0, 255}; // Green fill
    defaultTheme.componentStyles[UIComponentType::PROGRESS_BAR] = progressStyle;

    // Set default styles for other components
    UIStyle inputStyle = buttonStyle;
    inputStyle.fontID = "fonts_UI_Arial";
    UIStyle sliderStyle = buttonStyle;
    sliderStyle.fontID = "fonts_UI_Arial";
    UIStyle checkboxStyle = buttonStyle;
    checkboxStyle.fontID = "fonts_UI_Arial";
    UIStyle listStyle = panelStyle;
    listStyle.fontID = "fonts_UI_Arial";
    UIStyle tooltipStyle = panelStyle;
    tooltipStyle.fontID = "fonts_UI_Arial";

    defaultTheme.componentStyles[UIComponentType::INPUT_FIELD] = inputStyle;
    defaultTheme.componentStyles[UIComponentType::IMAGE] = panelStyle;
    defaultTheme.componentStyles[UIComponentType::SLIDER] = sliderStyle;
    defaultTheme.componentStyles[UIComponentType::CHECKBOX] = checkboxStyle;
    defaultTheme.componentStyles[UIComponentType::LIST] = listStyle;
    defaultTheme.componentStyles[UIComponentType::TOOLTIP] = tooltipStyle;

    m_currentTheme = defaultTheme;
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
    boost::container::small_vector<std::pair<std::string, std::shared_ptr<UIComponent>>, 32> sortedComponents;
    for (const auto& [id, component] : m_components) {
        if (component && component->visible && component->enabled) {
            sortedComponents.emplace_back(id, component);
        }
    }

    std::sort(sortedComponents.begin(), sortedComponents.end(),
        [](const auto& a, const auto& b) {
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
                // Calculate which item was clicked
                int itemHeight = 20; // TODO: Make this configurable
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

    auto& fontManager = FontManager::Instance();

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

    fontManager.drawTextAligned(component->text, component->style.fontID, textX, textY, component->style.textColor, renderer, alignment);
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

    // Draw items
    int itemHeight = 20;
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

void UIManager::renderTooltip(SDL_Renderer* renderer) {
    if (m_hoveredTooltip.empty() || m_tooltipTimer < m_tooltipDelay) {
        return;
    }

    auto component = getComponent(m_hoveredTooltip);
    if (!component || component->text.empty()) {
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
