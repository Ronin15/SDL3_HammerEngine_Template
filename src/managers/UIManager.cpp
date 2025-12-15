/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "managers/UIManager.hpp"
#include "managers/UIConstants.hpp"
#include "core/GameEngine.hpp"
#include "core/Logger.hpp"
#include "managers/FontManager.hpp"
#include "managers/InputManager.hpp"
#include "managers/TextureManager.hpp"
#include <algorithm>
#include <format>

bool UIManager::init() {
  if (m_isShutdown) {
    return false;
  }

  // Initialize with enhanced dark theme
  setDarkTheme();

  // Set global fonts to match what's loaded by FontManager's
  // loadFontsForDisplay
  m_globalFontID = UIConstants::FONT_DEFAULT;
  m_titleFontID = UIConstants::FONT_TITLE;
  m_uiFontID = UIConstants::FONT_UI;

  // Clear any existing data and reserve capacity for performance
  m_components.clear();
  m_layouts.clear();
  m_animations.clear();
  constexpr size_t ANIMATIONS_CAPACITY = 16;
  m_animations.reserve(
      ANIMATIONS_CAPACITY); // Reserve for typical UI animations
  m_clickedButtons.clear();
  constexpr size_t INTERACTIONS_CAPACITY = 8;
  m_clickedButtons.reserve(
      INTERACTIONS_CAPACITY); // Reserve for typical button interactions
  m_hoveredComponents.clear();
  m_hoveredComponents.reserve(
      INTERACTIONS_CAPACITY); // Reserve for typical hover states
  m_focusedComponent.clear();
  m_hoveredTooltip.clear();

  m_tooltipTimer = 0.0f;
  m_mousePressed = false;
  m_mouseReleased = false;

  // Initialize current logical dimensions from GameEngine
  const auto& gameEngine = GameEngine::Instance();
  m_currentLogicalWidth = gameEngine.getLogicalWidth();
  m_currentLogicalHeight = gameEngine.getLogicalHeight();
  UI_INFO(std::format("Initialized logical dimensions: {}x{}",
                      m_currentLogicalWidth, m_currentLogicalHeight));

  // Calculate and set resolution-aware UI scale (1920x1080 baseline, capped at 1.0)
  m_globalScale = calculateOptimalScale(m_currentLogicalWidth, m_currentLogicalHeight);
  UI_INFO(std::format("UI scale set to {} for resolution {}x{}",
                      m_globalScale, m_currentLogicalWidth, m_currentLogicalHeight));

  // Note: UIManager::onWindowResize() is called directly by InputManager when window resizes

  return true;
}

void UIManager::update(float deltaTime) {
  if (m_isShutdown) {
    return;
  }

  std::lock_guard<std::recursive_mutex> lock(m_componentsMutex);

  // Note: Window resize is now event-driven via onWindowResize(), not polled every frame

  // PERFORMANCE: Only iterate components if there are active bindings
  // This skips the O(n) loop entirely when no bindings exist
  if (m_activeBindingCount > 0) {
    // Process data bindings - ONLY for visible components to avoid unnecessary work
    for (auto const& [id, component] : m_components) {
        if (component && component->m_visible) {
            // Handle text bindings
            if (component->m_textBinding) {
                setText(id, component->m_textBinding());
            }
            // Handle list bindings (zero-allocation: uses reusable buffers)
            if (component->m_listBinding) {
                component->m_listBindingBuffer.clear();  // Reuse capacity, no deallocation
                component->m_listSortBuffer.clear();     // Reuse sort buffer capacity
                component->m_listBinding(component->m_listBindingBuffer, component->m_listSortBuffer);
                if (component->m_listItems.size() != component->m_listBindingBuffer.size() ||
                    !std::equal(component->m_listItems.begin(), component->m_listItems.end(),
                                component->m_listBindingBuffer.begin())) {
                    component->m_listItems = std::move(component->m_listBindingBuffer);
                    component->m_listBindingBuffer.clear();  // Reset for next use
                    component->m_listItemsDirty = true;
                }
            }
        }
    }
  }

  // Clear frame-specific state
  m_clickedButtons.clear();
  m_mouseReleased = false;

  // Handle input (may need component access)
  handleInput();

  // PERFORMANCE: Skip animation update if no animations exist
  if (!m_animations.empty()) {
    updateAnimations(deltaTime);
  }

  // Update tooltips
  updateTooltips(deltaTime);

  // PERFORMANCE: Skip event log update if no event logs exist
  if (!m_eventLogStates.empty()) {
    updateEventLogs(deltaTime);
  }

  // PERFORMANCE: Skip callback execution if no callbacks queued
  if (!m_deferredCallbacks.empty()) {
    executeDeferredCallbacks();
  }
}

void UIManager::executeDeferredCallbacks() {
    // Safely execute all queued callbacks
    for (const auto& callback : m_deferredCallbacks) {
        if (callback) {
            callback();
        }
    }
    // Clear the queue for the next frame
    m_deferredCallbacks.clear();
}

void UIManager::render(SDL_Renderer *renderer) {
  if (!renderer) {
    UI_ERROR("UIManager::render() called with null renderer");
    return;
  }

  std::lock_guard<std::recursive_mutex> lock(m_componentsMutex);

  // Early exit if no components to render
  if (m_components.empty()) {
    return;
  }

  // Render components in z-order (use const ref to avoid copy)
  const auto& sortedComponents = getSortedComponents();
  for (const auto &component : sortedComponents) {
    if (component && component->m_visible) {
      renderComponent(renderer, component);
    }
  }

  // Render tooltip last (on top)
  if (m_tooltipsEnabled && !m_hoveredTooltip.empty()) {
    renderTooltip(renderer);
  }

  // Debug rendering
  if (m_debugMode && m_drawDebugBounds) {
    for (const auto &[id, component] : m_components) {
      if (component && component->m_visible) {
        drawBorder(renderer, component->m_bounds, {255, 0, 0, 255}, 1);
      }
    }
  }
}

void UIManager::clean() {
  if (m_isShutdown) {
    return;
  }
  
  // Perform comprehensive cleanup to clear all cached textures
  cleanupForStateTransition();
  
  // Mark as shutdown
  m_isShutdown = true;
}

const std::vector<std::shared_ptr<UIComponent>>& UIManager::getSortedComponents() const {
  // Performance optimization: Only rebuild sorted list when components added/removed/z-order changed
  if (m_sortedComponentsDirty) {
    m_sortedComponentsCache.clear();
    m_sortedComponentsCache.reserve(m_components.size());

    for (const auto &[id, component] : m_components) {
      if (component) {
        m_sortedComponentsCache.push_back(component);
      }
    }

    std::sort(m_sortedComponentsCache.begin(), m_sortedComponentsCache.end(),
              [](const std::shared_ptr<UIComponent> &a,
                 const std::shared_ptr<UIComponent> &b) {
                return a->m_zOrder < b->m_zOrder;
              });

    m_sortedComponentsDirty = false;
  }

  return m_sortedComponentsCache;
}

// Performance optimization helper - centralized cache invalidation
void UIManager::invalidateComponentCache() {
  m_sortedComponentsDirty = true;
}

// Component creation methods
void UIManager::createButton(const std::string &id, const UIRect &bounds,
                             const std::string &text) {
  auto component = std::make_shared<UIComponent>();
  component->m_id = id;
  component->m_type = UIComponentType::BUTTON;
  // Apply global scale for resolution-aware sizing (1920x1080 baseline, capped at 1.0)
  component->m_bounds = scaleRect(bounds);
  component->m_text = text;
  component->m_style = m_currentTheme.getStyle(UIComponentType::BUTTON);
  component->m_zOrder = UIConstants::ZORDER_BUTTON; // Interactive elements on top

  std::lock_guard<std::recursive_mutex> lock(m_componentsMutex);
  m_components[id] = component;
  invalidateComponentCache();
}

void UIManager::createButtonDanger(const std::string &id, const UIRect &bounds,
                                   const std::string &text) {
  auto component = std::make_shared<UIComponent>();
  component->m_id = id;
  component->m_type = UIComponentType::BUTTON_DANGER;
  // Apply global scale for resolution-aware sizing
  component->m_bounds = scaleRect(bounds);
  component->m_text = text;
  component->m_style = m_currentTheme.getStyle(UIComponentType::BUTTON_DANGER);
  component->m_zOrder = UIConstants::ZORDER_BUTTON; // Interactive elements on top

  std::lock_guard<std::recursive_mutex> lock(m_componentsMutex);
  m_components[id] = component;
  invalidateComponentCache();
}

void UIManager::createButtonSuccess(const std::string &id, const UIRect &bounds,
                                    const std::string &text) {
  auto component = std::make_shared<UIComponent>();
  component->m_id = id;
  component->m_type = UIComponentType::BUTTON_SUCCESS;
  // Apply global scale for resolution-aware sizing
  component->m_bounds = scaleRect(bounds);
  component->m_text = text;
  component->m_style = m_currentTheme.getStyle(UIComponentType::BUTTON_SUCCESS);
  component->m_zOrder = UIConstants::ZORDER_BUTTON; // Interactive elements on top

  std::lock_guard<std::recursive_mutex> lock(m_componentsMutex);
  m_components[id] = component;
  invalidateComponentCache();
}

void UIManager::createButtonWarning(const std::string &id, const UIRect &bounds,
                                    const std::string &text) {
  auto component = std::make_shared<UIComponent>();
  component->m_id = id;
  component->m_type = UIComponentType::BUTTON_WARNING;
  // Apply global scale for resolution-aware sizing
  component->m_bounds = scaleRect(bounds);
  component->m_text = text;
  component->m_style = m_currentTheme.getStyle(UIComponentType::BUTTON_WARNING);
  component->m_zOrder = UIConstants::ZORDER_BUTTON; // Interactive elements on top

  std::lock_guard<std::recursive_mutex> lock(m_componentsMutex);
  m_components[id] = component;
  invalidateComponentCache();
}

void UIManager::createLabel(const std::string &id, const UIRect &bounds,
                            const std::string &text) {
  auto component = std::make_shared<UIComponent>();
  component->m_id = id;
  component->m_type = UIComponentType::LABEL;
  // Apply global scale for resolution-aware sizing
  component->m_bounds = scaleRect(bounds);
  component->m_text = text;
  component->m_style = m_currentTheme.getStyle(UIComponentType::LABEL);
  component->m_zOrder = UIConstants::ZORDER_LABEL; // Text on top

  std::lock_guard<std::recursive_mutex> lock(m_componentsMutex);
  m_components[id] = component;

  // Apply auto-sizing after creation
  calculateOptimalSize(component);
}

void UIManager::createTitle(const std::string &id, const UIRect &bounds,
                            const std::string &text) {
  auto component = std::make_shared<UIComponent>();
  component->m_id = id;
  component->m_type = UIComponentType::TITLE;
  // Apply global scale for resolution-aware sizing
  component->m_bounds = scaleRect(bounds);
  component->m_text = text;
  component->m_style = m_currentTheme.getStyle(UIComponentType::TITLE);
  component->m_zOrder = UIConstants::ZORDER_TITLE; // Titles on top

  m_components[id] = component;

  // Apply auto-sizing after creation
  calculateOptimalSize(component);
}

void UIManager::createPanel(const std::string &id, const UIRect &bounds) {
  auto component = std::make_shared<UIComponent>();
  component->m_id = id;
  component->m_type = UIComponentType::PANEL;
  // Apply global scale for resolution-aware sizing
  component->m_bounds = scaleRect(bounds);
  component->m_style = m_currentTheme.getStyle(UIComponentType::PANEL);
  component->m_zOrder = UIConstants::ZORDER_PANEL; // Background panels

  m_components[id] = component;
}

void UIManager::createProgressBar(const std::string &id, const UIRect &bounds,
                                  float minVal, float maxVal) {
  auto component = std::make_shared<UIComponent>();
  component->m_id = id;
  component->m_type = UIComponentType::PROGRESS_BAR;
  // Apply global scale for resolution-aware sizing
  component->m_bounds = scaleRect(bounds);
  component->m_minValue = minVal;
  component->m_maxValue = maxVal;
  component->m_value = minVal;
  component->m_style = m_currentTheme.getStyle(UIComponentType::PROGRESS_BAR);
  component->m_zOrder = UIConstants::ZORDER_PROGRESS_BAR; // UI elements

  m_components[id] = component;
}

void UIManager::createInputField(const std::string &id, const UIRect &bounds,
                                 const std::string &placeholder) {
  auto component = std::make_shared<UIComponent>();
  component->m_id = id;
  component->m_type = UIComponentType::INPUT_FIELD;
  // Apply global scale for resolution-aware sizing
  component->m_bounds = scaleRect(bounds);
  component->m_placeholder = placeholder;
  component->m_style = m_currentTheme.getStyle(UIComponentType::INPUT_FIELD);
  component->m_zOrder = UIConstants::ZORDER_INPUT_FIELD; // Interactive elements

  m_components[id] = component;
}

void UIManager::createImage(const std::string &id, const UIRect &bounds,
                            const std::string &textureID) {
  auto component = std::make_shared<UIComponent>();
  component->m_id = id;
  component->m_type = UIComponentType::IMAGE;
  // Apply global scale for resolution-aware sizing
  component->m_bounds = scaleRect(bounds);
  component->m_textureID = textureID;
  component->m_style = m_currentTheme.getStyle(UIComponentType::IMAGE);
  component->m_zOrder = UIConstants::ZORDER_IMAGE; // Background images

  m_components[id] = component;
}

void UIManager::createSlider(const std::string &id, const UIRect &bounds,
                             float minVal, float maxVal) {
  auto component = std::make_shared<UIComponent>();
  component->m_id = id;
  component->m_type = UIComponentType::SLIDER;
  // Apply global scale for resolution-aware sizing
  component->m_bounds = scaleRect(bounds);
  component->m_minValue = minVal;
  component->m_maxValue = maxVal;
  component->m_value = minVal;
  component->m_style = m_currentTheme.getStyle(UIComponentType::SLIDER);
  component->m_zOrder = UIConstants::ZORDER_SLIDER; // Interactive elements

  m_components[id] = component;
}

void UIManager::createCheckbox(const std::string &id, const UIRect &bounds,
                               const std::string &text) {
  auto component = std::make_shared<UIComponent>();
  component->m_id = id;
  component->m_type = UIComponentType::CHECKBOX;
  // Apply global scale for resolution-aware sizing
  component->m_bounds = scaleRect(bounds);
  component->m_text = text;
  component->m_checked = false;
  component->m_style = m_currentTheme.getStyle(UIComponentType::CHECKBOX);
  component->m_zOrder = UIConstants::ZORDER_CHECKBOX; // Interactive elements

  m_components[id] = component;
}

void UIManager::createList(const std::string &id, const UIRect &bounds) {
  auto component = std::make_shared<UIComponent>();
  component->m_id = id;
  component->m_type = UIComponentType::LIST;
  // Apply global scale for resolution-aware sizing
  component->m_bounds = scaleRect(bounds);
  component->m_selectedIndex = -1;
  component->m_style = m_currentTheme.getStyle(UIComponentType::LIST);
  component->m_zOrder = UIConstants::ZORDER_LIST; // UI elements

  std::lock_guard<std::recursive_mutex> lock(m_componentsMutex);
  m_components[id] = component;

  // Enable auto-sizing for dynamic content-based sizing
  calculateOptimalSize(component);
}

void UIManager::createTooltip(const std::string &id, const std::string &text) {
  auto component = std::make_shared<UIComponent>();
  component->m_id = id;
  component->m_type = UIComponentType::TOOLTIP;
  component->m_text = text;
  component->m_visible = false;
  component->m_style = m_currentTheme.getStyle(UIComponentType::TOOLTIP);
  component->m_zOrder = UIConstants::ZORDER_TOOLTIP; // Always on top

  m_components[id] = component;
}

void UIManager::createEventLog(const std::string &id, const UIRect &bounds,
                               int maxEntries) {
  auto component = std::make_shared<UIComponent>();
  component->m_id = id;
  component->m_type = UIComponentType::EVENT_LOG;
  // Apply global scale for resolution-aware sizing
  component->m_bounds = scaleRect(bounds);
  component->m_maxLength = maxEntries; // Store max entries in maxLength field
  component->m_style = m_currentTheme.getStyle(
      UIComponentType::EVENT_LOG); // Use event log styling
  component->m_zOrder = UIConstants::ZORDER_EVENT_LOG;           // UI elements

  std::lock_guard<std::recursive_mutex> lock(m_componentsMutex);
  m_components[id] = component;
  invalidateComponentCache();
}

void UIManager::createDialog(const std::string &id, const UIRect &bounds) {
  auto component = std::make_shared<UIComponent>();
  component->m_id = id;
  component->m_type = UIComponentType::DIALOG;
  // Apply global scale for resolution-aware sizing
  component->m_bounds = scaleRect(bounds);
  component->m_style = m_currentTheme.getStyle(UIComponentType::DIALOG);
  component->m_zOrder = UIConstants::ZORDER_DIALOG; // Render behind other elements by default

  m_components[id] = component;
}

void UIManager::createModal(const std::string &dialogId, const UIRect &bounds,
                            const std::string &theme, int windowWidth,
                            int windowHeight) {
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

void UIManager::refreshAllComponentThemes() const {
  // Apply current theme to all existing components, preserving custom alignment
  for (const auto &[id, component] : m_components) {
    if (component) {
      UIAlignment preservedAlignment = component->m_style.textAlign;
      component->m_style = m_currentTheme.getStyle(component->m_type);
      component->m_style.textAlign = preservedAlignment;
    }
  }
}

// Component manipulation
void UIManager::removeComponent(const std::string &id) {
  std::lock_guard<std::recursive_mutex> lock(m_componentsMutex);

  m_components.erase(id);
  invalidateComponentCache();

  // Clear from value/text caches
  m_valueCache.erase(id);
  m_textCache.erase(id);

  // Remove from any layouts
  for (auto &[layoutId, layout] : m_layouts) {
    auto &children = layout->m_childComponents;
    children.erase(std::remove(children.begin(), children.end(), id),
                   children.end());
  }

  // Clear focus if this component was focused
  if (m_focusedComponent == id) {
    m_focusedComponent.clear();
  }
}

bool UIManager::hasComponent(const std::string &id) const {
  return m_components.find(id) != m_components.end();
}

void UIManager::setComponentVisible(const std::string &id, bool visible) {
  auto component = getComponent(id);
  if (component) {
    component->m_visible = visible;
  }
}

void UIManager::setComponentEnabled(const std::string &id, bool enabled) {
  auto component = getComponent(id);
  if (component) {
    component->m_enabled = enabled;
    if (!enabled && component->m_state != UIState::DISABLED) {
      component->m_state = UIState::DISABLED;
    } else if (enabled && component->m_state == UIState::DISABLED) {
      component->m_state = UIState::NORMAL;
    }
  }
}

void UIManager::setComponentBounds(const std::string &id,
                                   const UIRect &bounds) {
  auto component = getComponent(id);
  if (component) {
    // Apply global scale for resolution-aware sizing
    component->m_bounds = scaleRect(bounds);
  }
}

void UIManager::setComponentZOrder(const std::string &id, int zOrder) {
  auto component = getComponent(id);
  if (component) {
    component->m_zOrder = zOrder;
    invalidateComponentCache();
  }
}

// Component property setters
void UIManager::setText(const std::string &id, const std::string &text) {
  // Performance optimization: Check cache first to avoid mutex lock + hash lookup when text unchanged
  auto cacheIt = m_textCache.find(id);
  if (cacheIt != m_textCache.end() && cacheIt->second == text) {
    return; // Text unchanged, skip expensive getComponent() call
  }

  auto component = getComponent(id);
  if (component) {
    component->m_text = text;
    m_textCache[id] = text; // Update cache
  }
}

void UIManager::setTexture(const std::string &id,
                           const std::string &textureID) {
  auto component = getComponent(id);
  if (component) {
    component->m_textureID = textureID;
  }
}

void UIManager::setValue(const std::string &id, float value) {
  // Performance optimization: Check cache first to avoid mutex lock + hash lookup when value unchanged
  auto cacheIt = m_valueCache.find(id);
  if (cacheIt != m_valueCache.end() && cacheIt->second == value) {
    return; // Value unchanged, skip expensive getComponent() call
  }

  auto component = getComponent(id);
  if (component) {
    float clampedValue =
        std::clamp(value, component->m_minValue, component->m_maxValue);
    if (component->m_value != clampedValue) {
      component->m_value = clampedValue;
      m_valueCache[id] = clampedValue; // Update cache
      if (component->m_onValueChanged) {
        component->m_onValueChanged(clampedValue);
      }
    } else {
      // Value was already at clampedValue, ensure cache is updated
      m_valueCache[id] = clampedValue;
    }
  }
}

void UIManager::setChecked(const std::string &id, bool checked) {
  auto component = getComponent(id);
  if (component) {
    component->m_checked = checked;
  }
}

void UIManager::setStyle(const std::string &id, const UIStyle &style) {
  auto component = getComponent(id);
  if (component) {
    component->m_style = style;
  }
}

// Data binding methods
void UIManager::bindText(const std::string &id,
                         std::function<std::string()> binding) {
  auto component = getComponent(id);
  if (component) {
    // PERFORMANCE: Track binding count for early exit optimization in update()
    if (!component->m_textBinding && binding) {
      ++m_activeBindingCount;
    } else if (component->m_textBinding && !binding) {
      --m_activeBindingCount;
    }
    component->m_textBinding = binding;
  }
}

void UIManager::bindList(
    const std::string &id,
    std::function<void(std::vector<std::string>&, std::vector<std::pair<std::string, int>>&)> binding) {
  auto component = getComponent(id);
  if (component) {
    // PERFORMANCE: Track binding count for early exit optimization in update()
    if (!component->m_listBinding && binding) {
      ++m_activeBindingCount;
    } else if (component->m_listBinding && !binding) {
      --m_activeBindingCount;
    }
    component->m_listBinding = binding;
  }
}

// Text background methods for label and title readability
void UIManager::enableTextBackground(const std::string &id, bool enable) {
  auto component = getComponent(id);
  if (component && (component->m_type == UIComponentType::LABEL ||
                    component->m_type == UIComponentType::TITLE)) {
    component->m_style.useTextBackground = enable;
  }
}

void UIManager::setTextBackgroundColor(const std::string &id, SDL_Color color) {
  auto component = getComponent(id);
  if (component && (component->m_type == UIComponentType::LABEL ||
                    component->m_type == UIComponentType::TITLE)) {
    component->m_style.textBackgroundColor = color;
  }
}

void UIManager::setTextBackgroundPadding(const std::string &id, int padding) {
  auto component = getComponent(id);
  if (component && (component->m_type == UIComponentType::LABEL ||
                    component->m_type == UIComponentType::TITLE)) {
    component->m_style.textBackgroundPadding = padding;
  }
}

// Component property getters
std::string UIManager::getText(const std::string &id) const {
  auto component = getComponent(id);
  return component ? component->m_text : "";
}

float UIManager::getValue(const std::string &id) const {
  auto component = getComponent(id);
  return component ? component->m_value : 0.0f;
}

bool UIManager::getChecked(const std::string &id) const {
  auto component = getComponent(id);
  return component ? component->m_checked : false;
}

UIRect UIManager::getBounds(const std::string &id) const {
  auto component = getComponent(id);
  return component ? component->m_bounds : UIRect{0, 0, 0, 0};
}

UIState UIManager::getComponentState(const std::string &id) const {
  auto component = getComponent(id);
  return component ? component->m_state : UIState::NORMAL;
}

// Event handling
bool UIManager::isButtonClicked(const std::string &id) const {
  return std::find(m_clickedButtons.begin(), m_clickedButtons.end(), id) !=
         m_clickedButtons.end();
}

bool UIManager::isButtonPressed(const std::string &id) const {
  auto component = getComponent(id);
  return component && component->m_state == UIState::PRESSED;
}

bool UIManager::isButtonHovered(const std::string &id) const {
  return std::find(m_hoveredComponents.begin(), m_hoveredComponents.end(),
                   id) != m_hoveredComponents.end();
}

bool UIManager::isComponentFocused(const std::string &id) const {
  return m_focusedComponent == id;
}

// Callback setters
void UIManager::setOnClick(const std::string &id,
                           std::function<void()> callback) {
  auto component = getComponent(id);
  if (component) {
    component->m_onClick = callback;
  }
}

void UIManager::setOnValueChanged(const std::string &id,
                                  std::function<void(float)> callback) {
  auto component = getComponent(id);
  if (component) {
    component->m_onValueChanged = callback;
  }
}

void UIManager::setOnTextChanged(
    const std::string &id, std::function<void(const std::string &)> callback) {
  auto component = getComponent(id);
  if (component) {
    component->m_onTextChanged = callback;
  }
}

void UIManager::setOnHover(const std::string &id,
                           std::function<void()> callback) {
  auto component = getComponent(id);
  if (component) {
    component->m_onHover = callback;
  }
}

void UIManager::setOnFocus(const std::string &id,
                           std::function<void()> callback) {
  auto component = getComponent(id);
  if (component) {
    component->m_onFocus = callback;
  }
}

// Layout management
void UIManager::createLayout(const std::string &id, UILayoutType type,
                             const UIRect &bounds) {
  auto layout = std::make_shared<UILayout>();
  layout->m_id = id;
  layout->m_type = type;
  // Apply global scale for resolution-aware sizing
  layout->m_bounds = scaleRect(bounds);

  m_layouts[id] = layout;
}

void UIManager::addComponentToLayout(const std::string &layoutID,
                                     const std::string &componentID) {
  auto layout = getLayout(layoutID);
  if (layout && hasComponent(componentID)) {
    layout->m_childComponents.push_back(componentID);
    updateLayout(layoutID);
  }
}

void UIManager::removeComponentFromLayout(const std::string &layoutID,
                                          const std::string &componentID) {
  auto layout = getLayout(layoutID);
  if (layout) {
    auto &children = layout->m_childComponents;
    children.erase(std::remove(children.begin(), children.end(), componentID),
                   children.end());
    updateLayout(layoutID);
  }
}

void UIManager::updateLayout(const std::string &layoutID) {
  auto layout = getLayout(layoutID);
  if (!layout) {
    return;
  }

  switch (layout->m_type) {
  case UILayoutType::ABSOLUTE_POS:
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

void UIManager::setLayoutSpacing(const std::string &layoutID, int spacing) {
  auto layout = getLayout(layoutID);
  if (layout) {
    layout->m_spacing = spacing;
    updateLayout(layoutID);
  }
}

void UIManager::setLayoutColumns(const std::string &layoutID, int columns) {
  auto layout = getLayout(layoutID);
  if (layout) {
    layout->m_columns = columns;
    updateLayout(layoutID);
  }
}

void UIManager::setLayoutAlignment(const std::string &layoutID,
                                   UIAlignment alignment) {
  auto layout = getLayout(layoutID);
  if (layout) {
    layout->m_alignment = alignment;
    updateLayout(layoutID);
  }
}

// Progress bar specific methods
void UIManager::updateProgressBar(const std::string &id, float value) {
  setValue(id, value);
}

void UIManager::setProgressBarRange(const std::string &id, float minVal,
                                    float maxVal) {
  auto component = getComponent(id);
  if (component && component->m_type == UIComponentType::PROGRESS_BAR) {
    component->m_minValue = minVal;
    component->m_maxValue = maxVal;
    component->m_value = std::clamp(component->m_value, minVal, maxVal);
  }
}

// List specific methods
void UIManager::addListItem(const std::string &listID,
                            const std::string &item) {
  std::lock_guard<std::recursive_mutex> lock(m_componentsMutex);

  auto component = getComponent(listID);
  if (component && component->m_type == UIComponentType::LIST) {
    component->m_listItems.push_back(item);
    component->m_listItemsDirty = true;
    // Trigger auto-sizing to accommodate new content
    calculateOptimalSize(component);
  }
}

void UIManager::removeListItem(const std::string &listID, int index) {
  std::lock_guard<std::recursive_mutex> lock(m_componentsMutex);

  auto component = getComponent(listID);
  if (component && component->m_type == UIComponentType::LIST && index >= 0 &&
      index < static_cast<int>(component->m_listItems.size())) {
    component->m_listItems.erase(component->m_listItems.begin() + index);
    component->m_listItemsDirty = true;
    if (component->m_selectedIndex == index) {
      component->m_selectedIndex = -1;
    } else if (component->m_selectedIndex > index) {
      component->m_selectedIndex--;
    }
    // Trigger auto-sizing (grow-only behavior will prevent shrinking)
    calculateOptimalSize(component);
  }
}

void UIManager::clearList(const std::string &listID) {
  auto component = getComponent(listID);
  if (component && component->m_type == UIComponentType::LIST) {
    component->m_listItems.clear();
    component->m_listItemsDirty = true;
    component->m_selectedIndex = -1;
  }
}

int UIManager::getSelectedListItem(const std::string &listID) const {
  auto component = getComponent(listID);
  return (component && component->m_type == UIComponentType::LIST)
             ? component->m_selectedIndex
             : -1;
}

void UIManager::setSelectedListItem(const std::string &listID, int index) {
  auto component = getComponent(listID);
  if (component && component->m_type == UIComponentType::LIST) {
    if (index >= 0 && index < static_cast<int>(component->m_listItems.size())) {
      component->m_selectedIndex = index;
    }
  }
}

void UIManager::setListMaxItems(const std::string &listID, int maxItems) {
  auto component = getComponent(listID);
  if (component && component->m_type == UIComponentType::LIST) {
    // Store max items in a custom property (we'll use the maxLength field for
    // this)
    component->m_maxLength = maxItems;

    // Trim existing items if they exceed the new limit
    if (static_cast<int>(component->m_listItems.size()) > maxItems) {
      // Keep only the last maxItems entries
      auto &items = component->m_listItems;
      auto startIt = items.end() - maxItems;
      items.erase(items.begin(), startIt);
      component->m_listItemsDirty = true;

      // Adjust selected index if needed
      if (component->m_selectedIndex >= maxItems) {
        component->m_selectedIndex =
            -1; // Clear selection if it's now out of bounds
      }
    }
  }
}

void UIManager::addListItemWithAutoScroll(const std::string &listID,
                                          const std::string &item) {
  std::lock_guard<std::recursive_mutex> lock(m_componentsMutex);

  auto component = getComponent(listID);
  if (component && component->m_type == UIComponentType::LIST) {
    // Add the new item
    component->m_listItems.push_back(item);
    component->m_listItemsDirty = true;

    // Check if we need to enforce max items limit
    int maxItems =
        component->m_maxLength; // Using maxLength field to store max items
    if (maxItems > 0 &&
        static_cast<int>(component->m_listItems.size()) > maxItems) {
      // Remove the oldest item
      component->m_listItems.erase(component->m_listItems.begin());

      // Adjust selected index if needed
      if (component->m_selectedIndex > 0) {
        component->m_selectedIndex--;
      } else if (component->m_selectedIndex == 0) {
        component->m_selectedIndex =
            -1; // Clear selection if first item was removed
      }
    }

    // Auto-scroll by selecting the last item (optional behavior)
    // Comment this out if you don't want auto-selection
    // component->m_selectedIndex = static_cast<int>(component->m_listItems.size())
    // - 1;

    // Trigger auto-sizing to accommodate new content
    calculateOptimalSize(component);
  }
}

void UIManager::clearListItems(const std::string &listID) {
  std::lock_guard<std::recursive_mutex> lock(m_componentsMutex);

  auto component = getComponent(listID);
  if (component && component->m_type == UIComponentType::LIST) {
    component->m_listItems.clear();
    component->m_listItemsDirty = true;
    component->m_selectedIndex = -1;
  }
}

void UIManager::addEventLogEntry(const std::string &logID,
                                 const std::string &entry) {
  std::lock_guard<std::recursive_mutex> lock(m_componentsMutex);

  auto component = getComponent(logID);
  if (component && component->m_type == UIComponentType::EVENT_LOG) {
    // Add the entry directly (let the caller handle timestamps if needed)
    component->m_listItems.push_back(entry);
    component->m_listItemsDirty = true;

    // Enforce max entries limit - scroll old events out
    int maxEntries = component->m_maxLength;
    if (maxEntries > 0 &&
        static_cast<int>(component->m_listItems.size()) > maxEntries) {
      // Remove oldest events (FIFO behavior)
      while (static_cast<int>(component->m_listItems.size()) > maxEntries) {
        component->m_listItems.erase(component->m_listItems.begin());
      }
    }

    // Event logs are display-only for game events, no selection
    component->m_selectedIndex = -1;

    // Auto-scroll to bottom to show newest events
  }
}

void UIManager::clearEventLog(const std::string &logID) {
  std::lock_guard<std::recursive_mutex> lock(m_componentsMutex);

  auto component = getComponent(logID);
  if (component && component->m_type == UIComponentType::EVENT_LOG) {
    component->m_listItems.clear();
    component->m_listItemsDirty = true;
    component->m_selectedIndex = -1;

    // Event logs use fixed size for game events display
  }
}

void UIManager::setEventLogMaxEntries(const std::string &logID,
                                      int maxEntries) {
  std::lock_guard<std::recursive_mutex> lock(m_componentsMutex);

  auto component = getComponent(logID);
  if (component && component->m_type == UIComponentType::EVENT_LOG) {
    component->m_maxLength = maxEntries;
    component->m_listItemsDirty = true;

    // Trim existing entries if needed
    if (static_cast<int>(component->m_listItems.size()) > maxEntries) {
      while (static_cast<int>(component->m_listItems.size()) > maxEntries) {
        component->m_listItems.erase(component->m_listItems.begin());
      }
    }

    // Event logs use fixed size for game events display
  }
}

void UIManager::setTitleAlignment(const std::string &titleID,
                                  UIAlignment alignment) {
  auto component = getComponent(titleID);
  if (component && component->m_type == UIComponentType::TITLE) {
    component->m_style.textAlign = alignment;

    // If setting to CENTER_CENTER and auto-sizing is enabled, recalculate
    // position
    if (alignment == UIAlignment::CENTER_CENTER && component->m_autoSize &&
        component->m_autoWidth) {
      const auto &gameEngine = GameEngine::Instance();
      int windowWidth = gameEngine.getLogicalWidth();
      component->m_bounds.x = (windowWidth - component->m_bounds.width) / 2;
    }
  }
}

void UIManager::centerTitleInContainer(const std::string &titleID,
                                       int containerX, int containerWidth) {
  auto component = getComponent(titleID);
  if (component && component->m_type == UIComponentType::TITLE) {
    // Center the auto-sized title within the container
    int titleWidth = component->m_bounds.width;
    component->m_bounds.x = containerX + (containerWidth - titleWidth) / 2;
  }
}

void UIManager::setLabelAlignment(const std::string &labelID,
                                  UIAlignment alignment) {
  auto component = getComponent(labelID);
  if (component && component->m_type == UIComponentType::LABEL) {
    component->m_style.textAlign = alignment;
  }
}

void UIManager::setupDemoEventLog(const std::string &logID) {
  addEventLogEntry(logID, "Event log initialized");
  addEventLogEntry(logID, "Demo components created");

  // Enable auto-updates for this event log
  enableEventLogAutoUpdate(logID, 2.0f); // 2 second interval
}

void UIManager::enableEventLogAutoUpdate(const std::string &logID,
                                         float interval) {
  auto component = getComponent(logID);
  if (component && component->m_type == UIComponentType::EVENT_LOG) {
    EventLogState state;
    state.m_timer = 0.0f;
    state.m_messageIndex = 0;
    state.m_updateInterval = interval;
    state.m_autoUpdate = true;
    m_eventLogStates[logID] = state;
  }
}

void UIManager::disableEventLogAutoUpdate(const std::string &logID) {
  auto it = m_eventLogStates.find(logID);
  if (it != m_eventLogStates.end()) {
    it->second.m_autoUpdate = false;
  }
}

void UIManager::updateEventLogs(float deltaTime) {
  for (auto &[logID, state] : m_eventLogStates) {
    if (!state.m_autoUpdate)
      continue;

    state.m_timer += deltaTime;

    // Add a new log entry based on the interval
    if (state.m_timer >= state.m_updateInterval) {
      state.m_timer = 0.0f;

      std::vector<std::string> sampleMessages = {
          "System initialized successfully", "User interface components loaded",
          "Database connection established", "Configuration files validated",
          "Network module started",          "Audio system ready",
          "Graphics renderer initialized",   "Input handlers registered",
          "Memory pools allocated",          "Security protocols activated"};

      addEventLogEntry(
          logID, sampleMessages[state.m_messageIndex % sampleMessages.size()]);
      state.m_messageIndex++;
    }
  }
}

// Input field specific methods
void UIManager::setInputFieldPlaceholder(const std::string &id,
                                         const std::string &placeholder) {
  auto component = getComponent(id);
  if (component && component->m_type == UIComponentType::INPUT_FIELD) {
    component->m_placeholder = placeholder;
  }
}

void UIManager::setInputFieldMaxLength(const std::string &id, int maxLength) {
  auto component = getComponent(id);
  if (component && component->m_type == UIComponentType::INPUT_FIELD) {
    component->m_maxLength = maxLength;
  }
}

bool UIManager::isInputFieldFocused(const std::string &id) const {
  return isComponentFocused(id);
}

// Animation system
void UIManager::animateMove(const std::string &id, const UIRect &targetBounds,
                            float duration, std::function<void()> onComplete) {
  auto component = getComponent(id);
  if (!component) {
    return;
  }

  auto animation = std::make_shared<UIAnimation>();
  animation->m_componentID = id;
  animation->m_duration = duration;
  animation->m_elapsed = 0.0f;
  animation->m_active = true;
  animation->m_startBounds = component->m_bounds;
  animation->m_targetBounds = targetBounds;
  animation->m_onComplete = onComplete;

  // Remove any existing animation for this component
  stopAnimation(id);

  m_animations.push_back(animation);
}

void UIManager::animateColor(const std::string &id,
                             const SDL_Color &targetColor, float duration,
                             std::function<void()> onComplete) {
  auto component = getComponent(id);
  if (!component) {
    return;
  }

  auto animation = std::make_shared<UIAnimation>();
  animation->m_componentID = id;
  animation->m_duration = duration;
  animation->m_elapsed = 0.0f;
  animation->m_active = true;
  animation->m_startColor = component->m_style.backgroundColor;
  animation->m_targetColor = targetColor;
  animation->m_onComplete = onComplete;

  // Remove any existing animation for this component
  stopAnimation(id);

  m_animations.push_back(animation);
}

void UIManager::stopAnimation(const std::string &id) {
  m_animations.erase(
      std::remove_if(m_animations.begin(), m_animations.end(),
                     [&id](const std::shared_ptr<UIAnimation> &anim) {
                       return anim->m_componentID == id;
                     }),
      m_animations.end());
}

bool UIManager::isAnimating(const std::string &id) const {
  return std::any_of(m_animations.begin(), m_animations.end(),
                     [&id](const std::shared_ptr<UIAnimation> &anim) {
                       return anim->m_componentID == id && anim->m_active;
                     });
}

// Theme management
void UIManager::loadTheme(const UITheme &theme) {
  m_currentTheme = theme;

  // Apply theme to all existing components, preserving custom alignment
  for (const auto &[id, component] : m_components) {
    if (component) {
      UIAlignment preservedAlignment = component->m_style.textAlign;
      component->m_style = m_currentTheme.getStyle(component->m_type);
      component->m_style.textAlign = preservedAlignment;
    }
  }
}

void UIManager::setDefaultTheme() {
  // Default theme now uses dark theme as the base
  setDarkTheme();
}

void UIManager::setLightTheme() {
  UITheme lightTheme;
  lightTheme.m_name = "light";
  m_currentThemeMode = "light";

  // Button style - improved contrast and professional appearance
  UIStyle buttonStyle;
  buttonStyle.backgroundColor = {60, 120, 180, 255};
  buttonStyle.borderColor = {255, 255, 255, 255};
  buttonStyle.textColor = {255, 255, 255, 255};
  buttonStyle.hoverColor = {80, 140, 200, 255};
  buttonStyle.pressedColor = {40, 100, 160, 255};
  buttonStyle.borderWidth = UIConstants::BORDER_WIDTH_NORMAL;
  buttonStyle.textAlign = UIAlignment::CENTER_CENTER;
  buttonStyle.fontID = UIConstants::FONT_UI;
  lightTheme.m_componentStyles[UIComponentType::BUTTON] = buttonStyle;

  // Button Danger style - red buttons for Back, Quit, Exit, Delete, etc.
  UIStyle dangerButtonStyle = buttonStyle;
  dangerButtonStyle.backgroundColor = {180, 50, 50, 255};
  dangerButtonStyle.hoverColor = {200, 70, 70, 255};
  dangerButtonStyle.pressedColor = {160, 30, 30, 255};
  lightTheme.m_componentStyles[UIComponentType::BUTTON_DANGER] =
      dangerButtonStyle;

  // Button Success style - green buttons for Save, Confirm, Accept, etc.
  UIStyle successButtonStyle = buttonStyle;
  successButtonStyle.backgroundColor = {50, 150, 50, 255};
  successButtonStyle.hoverColor = {70, 170, 70, 255};
  successButtonStyle.pressedColor = {30, 130, 30, 255};
  lightTheme.m_componentStyles[UIComponentType::BUTTON_SUCCESS] =
      successButtonStyle;

  // Button Warning style - orange buttons for Caution, Reset, etc.
  UIStyle warningButtonStyle = buttonStyle;
  warningButtonStyle.backgroundColor = {200, 140, 50, 255};
  warningButtonStyle.hoverColor = {220, 160, 70, 255};
  warningButtonStyle.pressedColor = {180, 120, 30, 255};
  lightTheme.m_componentStyles[UIComponentType::BUTTON_WARNING] =
      warningButtonStyle;

  // Label style - enhanced contrast
  UIStyle labelStyle;
  labelStyle.backgroundColor = {0, 0, 0, 0}; // Transparent
  labelStyle.textColor = {20, 20, 20, 255};  // Dark text for light backgrounds
  labelStyle.textAlign = UIAlignment::CENTER_LEFT;
  labelStyle.fontID = UIConstants::FONT_UI;
  // Text background enabled by default for readability on any background
  labelStyle.useTextBackground = true;
  labelStyle.textBackgroundColor = {255, 255, 255,
                                    100}; // More transparent white
  labelStyle.textBackgroundPadding = UIConstants::LABEL_TEXT_BG_PADDING;
  lightTheme.m_componentStyles[UIComponentType::LABEL] = labelStyle;

  // Panel style - light overlay for subtle UI separation
  UIStyle panelStyle;
  panelStyle.backgroundColor = {0, 0, 0,
                                40}; // Very light overlay (15% opacity)
  panelStyle.borderWidth = UIConstants::BORDER_WIDTH_NONE;
  panelStyle.fontID = UIConstants::FONT_UI;
  lightTheme.m_componentStyles[UIComponentType::PANEL] = panelStyle;

  // Progress bar style - enhanced visibility
  UIStyle progressStyle;
  progressStyle.backgroundColor = {40, 40, 40, 255};
  progressStyle.borderColor = {180, 180, 180, 255}; // Stronger borders
  progressStyle.hoverColor = {0, 180, 0, 255};      // Green fill
  progressStyle.borderWidth = UIConstants::BORDER_WIDTH_NORMAL;
  progressStyle.fontID = UIConstants::FONT_UI;
  lightTheme.m_componentStyles[UIComponentType::PROGRESS_BAR] = progressStyle;

  // Input field style - light background with dark text
  UIStyle inputStyle;
  inputStyle.backgroundColor = {245, 245, 245, 255};
  inputStyle.textColor = {20, 20, 20, 255}; // Dark text for good contrast
  inputStyle.borderColor = {180, 180, 180, 255};
  inputStyle.hoverColor = {235, 245, 255, 255};
  inputStyle.borderWidth = UIConstants::BORDER_WIDTH_NORMAL;
  inputStyle.textAlign = UIAlignment::CENTER_LEFT;
  inputStyle.fontID = UIConstants::FONT_UI;
  lightTheme.m_componentStyles[UIComponentType::INPUT_FIELD] = inputStyle;

  // List style - light background with enhanced item height
  UIStyle listStyle;
  listStyle.backgroundColor = {240, 240, 240, 255};
  listStyle.borderColor = {180, 180, 180, 255};
  listStyle.textColor = {20, 20, 20, 255};     // Dark text on light background
  listStyle.hoverColor = {180, 200, 255, 255}; // Light blue selection
  listStyle.borderWidth = UIConstants::BORDER_WIDTH_NORMAL;
  // Calculate list item height based on font metrics
  listStyle.listItemHeight =
      UIConstants::DEFAULT_LIST_ITEM_HEIGHT; // Will be calculated dynamically during rendering
  listStyle.fontID = UIConstants::FONT_UI;
  lightTheme.m_componentStyles[UIComponentType::LIST] = listStyle;

  // Slider style - enhanced borders
  UIStyle sliderStyle;
  sliderStyle.backgroundColor = {100, 100, 100, 255};
  sliderStyle.borderColor = {180, 180, 180, 255};
  sliderStyle.hoverColor = {60, 120, 180, 255}; // Blue handle
  sliderStyle.pressedColor = {40, 100, 160, 255};
  sliderStyle.borderWidth = UIConstants::BORDER_WIDTH_NORMAL;
  sliderStyle.fontID = UIConstants::FONT_UI;
  lightTheme.m_componentStyles[UIComponentType::SLIDER] = sliderStyle;

  // Checkbox style - enhanced visibility
  UIStyle checkboxStyle = buttonStyle;
  checkboxStyle.backgroundColor = {180, 180, 180, 255};
  checkboxStyle.hoverColor = {200, 200, 200, 255};
  checkboxStyle.textColor = {20, 20, 20,
                             255}; // Dark text for light backgrounds
  checkboxStyle.textAlign = UIAlignment::CENTER_LEFT;
  checkboxStyle.fontID = UIConstants::FONT_UI;
  lightTheme.m_componentStyles[UIComponentType::CHECKBOX] = checkboxStyle;

  // Tooltip style
  UIStyle tooltipStyle = panelStyle;
  tooltipStyle.backgroundColor = {40, 40, 40, 230}; // More opaque for tooltips
  tooltipStyle.borderColor = {180, 180, 180, 255};
  tooltipStyle.borderWidth = UIConstants::BORDER_WIDTH_NORMAL;
  tooltipStyle.textColor = {255, 255, 255,
                            255}; // White text for dark tooltip background
  tooltipStyle.fontID = UIConstants::FONT_TOOLTIP;
  lightTheme.m_componentStyles[UIComponentType::TOOLTIP] = tooltipStyle;

  // Image component uses transparent background
  UIStyle imageStyle;
  imageStyle.backgroundColor = {0, 0, 0, 0};
  imageStyle.fontID = UIConstants::FONT_UI;
  lightTheme.m_componentStyles[UIComponentType::IMAGE] = imageStyle;

  // Event log style - similar to list but optimized for display-only
  UIStyle eventLogStyle = listStyle;
  // Calculate event log item height based on font metrics
  eventLogStyle.listItemHeight =
      24; // Will be calculated dynamically during rendering
  eventLogStyle.backgroundColor = {245, 245, 250,
                                   160};    // Semi-transparent light background
  eventLogStyle.textColor = {0, 0, 0, 255}; // Black text for maximum contrast
  eventLogStyle.borderColor = {120, 120, 140, 180}; // Less transparent border
  lightTheme.m_componentStyles[UIComponentType::EVENT_LOG] = eventLogStyle;

  // Title style - large, prominent text for headings
  UIStyle titleStyle;
  titleStyle.backgroundColor = {0, 0, 0, 0}; // Transparent background
  titleStyle.textColor = {0, 198, 230, 255}; // Dark Cyan color for titles
  titleStyle.fontSize = UIConstants::TITLE_FONT_SIZE;                  // Use native title font size
  titleStyle.textAlign = UIAlignment::CENTER_LEFT;
  titleStyle.fontID = UIConstants::FONT_TITLE;
  // Text background enabled by default for readability on any background
  titleStyle.useTextBackground = true;
  titleStyle.textBackgroundColor = {20, 20, 20,
                                    120}; // More transparent dark for gold text
  titleStyle.textBackgroundPadding = UIConstants::TITLE_TEXT_BG_PADDING;
  lightTheme.m_componentStyles[UIComponentType::TITLE] = titleStyle;

  // Dialog style - solid background for modal dialogs
  UIStyle dialogStyle;
  dialogStyle.backgroundColor = {245, 245, 245, 255}; // Light solid background
  dialogStyle.borderColor = {120, 120, 120, 255}; // Dark border for definition
  dialogStyle.borderWidth = UIConstants::BORDER_WIDTH_DIALOG;
  dialogStyle.fontID = UIConstants::FONT_UI;
  lightTheme.m_componentStyles[UIComponentType::DIALOG] = dialogStyle;

  m_currentTheme = lightTheme;

  // Apply theme to all existing components, preserving custom alignment
  for (const auto &[id, component] : m_components) {
    if (component) {
      UIAlignment preservedAlignment = component->m_style.textAlign;
      component->m_style = m_currentTheme.getStyle(component->m_type);
      component->m_style.textAlign = preservedAlignment;
    }
  }
}

void UIManager::setDarkTheme() {
  UITheme darkTheme;
  darkTheme.m_name = "dark";
  m_currentThemeMode = "dark";

  // Button style - enhanced contrast for dark theme
  UIStyle buttonStyle;
  buttonStyle.backgroundColor = {50, 50, 60, 255};
  buttonStyle.borderColor = {180, 180, 180, 255}; // Brighter borders
  buttonStyle.textColor = {255, 255, 255, 255};
  buttonStyle.hoverColor = {70, 70, 80, 255};
  buttonStyle.pressedColor = {30, 30, 40, 255};
  buttonStyle.borderWidth = UIConstants::BORDER_WIDTH_NORMAL;
  buttonStyle.textAlign = UIAlignment::CENTER_CENTER;
  buttonStyle.fontID = UIConstants::FONT_UI;
  darkTheme.m_componentStyles[UIComponentType::BUTTON] = buttonStyle;

  // Button Danger style - red buttons for Back, Quit, Exit, Delete, etc.
  UIStyle dangerButtonStyle = buttonStyle;
  dangerButtonStyle.backgroundColor = {200, 60, 60, 255};
  dangerButtonStyle.hoverColor = {220, 80, 80, 255};
  dangerButtonStyle.pressedColor = {180, 40, 40, 255};
  darkTheme.m_componentStyles[UIComponentType::BUTTON_DANGER] = dangerButtonStyle;

  // Button Success style - green buttons for Save, Confirm, Accept, etc.
  UIStyle successButtonStyle = buttonStyle;
  successButtonStyle.backgroundColor = {60, 160, 60, 255};
  successButtonStyle.hoverColor = {80, 180, 80, 255};
  successButtonStyle.pressedColor = {40, 140, 40, 255};
  darkTheme.m_componentStyles[UIComponentType::BUTTON_SUCCESS] =
      successButtonStyle;

  // Button Warning style - orange buttons for Caution, Reset, etc.
  UIStyle warningButtonStyle = buttonStyle;
  warningButtonStyle.backgroundColor = {220, 150, 60, 255};
  warningButtonStyle.hoverColor = {240, 170, 80, 255};
  warningButtonStyle.pressedColor = {200, 130, 40, 255};
  darkTheme.m_componentStyles[UIComponentType::BUTTON_WARNING] =
      warningButtonStyle;

  // Label style - pure white text for maximum contrast
  UIStyle labelStyle;
  labelStyle.backgroundColor = {0, 0, 0, 0};   // Transparent
  labelStyle.textColor = {255, 255, 255, 255}; // Pure white
  labelStyle.textAlign = UIAlignment::CENTER_LEFT;
  labelStyle.fontID = UIConstants::FONT_UI;
  // Text background enabled by default for readability on any background
  labelStyle.useTextBackground = true;
  labelStyle.textBackgroundColor = {0, 0, 0, 100}; // More transparent black
  labelStyle.textBackgroundPadding = UIConstants::LABEL_TEXT_BG_PADDING;
  darkTheme.m_componentStyles[UIComponentType::LABEL] = labelStyle;

  // Panel style - slightly more overlay for dark theme
  UIStyle panelStyle;
  panelStyle.backgroundColor = {0, 0, 0, 50}; // 19% opacity
  panelStyle.borderWidth = UIConstants::BORDER_WIDTH_NONE;
  panelStyle.fontID = UIConstants::FONT_UI;
  darkTheme.m_componentStyles[UIComponentType::PANEL] = panelStyle;

  // Progress bar style
  UIStyle progressStyle;
  progressStyle.backgroundColor = {20, 20, 20, 255};
  progressStyle.borderColor = {180, 180, 180, 255};
  progressStyle.hoverColor = {0, 180, 0, 255}; // Green fill
  progressStyle.borderWidth = UIConstants::BORDER_WIDTH_NORMAL;
  progressStyle.fontID = UIConstants::FONT_UI;
  darkTheme.m_componentStyles[UIComponentType::PROGRESS_BAR] = progressStyle;

  // Input field style - dark theme
  UIStyle inputStyle;
  inputStyle.backgroundColor = {40, 40, 40, 255};
  inputStyle.textColor = {255, 255, 255, 255}; // White text
  inputStyle.borderColor = {180, 180, 180, 255};
  inputStyle.hoverColor = {50, 50, 50, 255};
  inputStyle.borderWidth = UIConstants::BORDER_WIDTH_NORMAL;
  inputStyle.textAlign = UIAlignment::CENTER_LEFT;
  inputStyle.fontID = UIConstants::FONT_UI;
  darkTheme.m_componentStyles[UIComponentType::INPUT_FIELD] = inputStyle;

  // List style - dark theme
  UIStyle listStyle;
  listStyle.backgroundColor = {35, 35, 35, 255};
  listStyle.borderColor = {180, 180, 180, 255};
  listStyle.textColor = {255, 255, 255, 255}; // White text
  listStyle.hoverColor = {60, 80, 150, 255};  // Blue selection
  listStyle.borderWidth = UIConstants::BORDER_WIDTH_NORMAL;
  // Calculate list item height based on font metrics
  listStyle.listItemHeight =
      UIConstants::DEFAULT_LIST_ITEM_HEIGHT; // Will be calculated dynamically during rendering
  listStyle.fontID = UIConstants::FONT_UI;
  darkTheme.m_componentStyles[UIComponentType::LIST] = listStyle;

  // Slider style
  UIStyle sliderStyle;
  sliderStyle.backgroundColor = {30, 30, 30, 255};
  sliderStyle.borderColor = {180, 180, 180, 255};
  sliderStyle.hoverColor = {60, 120, 180, 255}; // Blue handle
  sliderStyle.pressedColor = {40, 100, 160, 255};
  sliderStyle.borderWidth = UIConstants::BORDER_WIDTH_NORMAL;
  sliderStyle.fontID = UIConstants::FONT_UI;
  darkTheme.m_componentStyles[UIComponentType::SLIDER] = sliderStyle;

  // Checkbox style
  UIStyle checkboxStyle = buttonStyle;
  checkboxStyle.backgroundColor = {60, 60, 60, 255};
  checkboxStyle.hoverColor = {80, 80, 80, 255};
  checkboxStyle.textColor = {255, 255, 255, 255};
  checkboxStyle.textAlign = UIAlignment::CENTER_LEFT;
  checkboxStyle.fontID = UIConstants::FONT_UI;
  darkTheme.m_componentStyles[UIComponentType::CHECKBOX] = checkboxStyle;

  // Tooltip style
  UIStyle tooltipStyle;
  tooltipStyle.backgroundColor = {20, 20, 20, 240};
  tooltipStyle.borderColor = {180, 180, 180, 255};
  tooltipStyle.borderWidth = UIConstants::BORDER_WIDTH_NORMAL;
  tooltipStyle.textColor = {255, 255, 255, 255};
  tooltipStyle.fontID = UIConstants::FONT_TOOLTIP;
  darkTheme.m_componentStyles[UIComponentType::TOOLTIP] = tooltipStyle;

  // Image component uses transparent background
  UIStyle imageStyle;
  imageStyle.backgroundColor = {0, 0, 0, 0};
  imageStyle.fontID = UIConstants::FONT_UI;
  darkTheme.m_componentStyles[UIComponentType::IMAGE] = imageStyle;

  // Event log style - similar to list but optimized for display-only
  UIStyle eventLogStyle = listStyle;
  // Calculate event log item height based on font metrics
  eventLogStyle.listItemHeight =
      24; // Will be calculated dynamically during rendering
  eventLogStyle.backgroundColor = {25, 30, 35,
                                   80}; // Highly transparent dark background
  eventLogStyle.textColor = {255, 255, 255,
                             255}; // Pure white text for maximum contrast
  eventLogStyle.borderColor = {100, 120, 140,
                               100}; // Highly transparent blue-gray border
  darkTheme.m_componentStyles[UIComponentType::EVENT_LOG] = eventLogStyle;

  // Title style - large, prominent text for headings
  UIStyle titleStyle;
  titleStyle.backgroundColor = {0, 0, 0, 0}; // Transparent background
  titleStyle.textColor = {0, 198, 230, 255}; // Dark Cyan color for titles
  titleStyle.fontSize = UIConstants::TITLE_FONT_SIZE;                  // Use native title font size
  titleStyle.textAlign = UIAlignment::CENTER_LEFT;
  titleStyle.fontID = UIConstants::FONT_TITLE;
  // Text background enabled by default for readability on any background
  titleStyle.useTextBackground = true;
  titleStyle.textBackgroundColor = {
      0, 0, 0, 120}; // More transparent black for gold text
  titleStyle.textBackgroundPadding = UIConstants::TITLE_TEXT_BG_PADDING;
  darkTheme.m_componentStyles[UIComponentType::TITLE] = titleStyle;

  // Dialog style - solid background for modal dialogs
  UIStyle dialogStyle;
  dialogStyle.backgroundColor = {45, 45, 45, 255}; // Dark solid background
  dialogStyle.borderColor = {160, 160, 160, 255}; // Light border for definition
  dialogStyle.borderWidth = UIConstants::BORDER_WIDTH_DIALOG;
  dialogStyle.fontID = UIConstants::FONT_UI;
  darkTheme.m_componentStyles[UIComponentType::DIALOG] = dialogStyle;

  m_currentTheme = darkTheme;

  // Apply theme to all existing components, preserving custom alignment
  for (const auto &[id, component] : m_components) {
    if (component) {
      UIAlignment preservedAlignment = component->m_style.textAlign;
      component->m_style = m_currentTheme.getStyle(component->m_type);
      component->m_style.textAlign = preservedAlignment;
    }
  }
}

void UIManager::setThemeMode(const std::string &mode) {
  if (mode == "light") {
    setLightTheme();
  } else if (mode == "dark") {
    setDarkTheme();
  } else if (mode == "default") {
    // For backward compatibility, default now uses dark theme
    setDarkTheme();
  }
}

const std::string &UIManager::getCurrentThemeMode() const {
  return m_currentThemeMode;
}

void UIManager::createOverlay(int windowWidth, int windowHeight) {
  // Remove existing overlay if it exists
  removeOverlay();

  // Create semi-transparent overlay panel using current theme's panel style
  createPanel("__overlay", {0, 0, windowWidth, windowHeight});

  // Set positioning to always fill window on resize (fixedWidth/Height = -1 means full window dimensions)
  // This ensures overlay properly resizes during fullscreen toggles and window resize events
  setComponentPositioning("__overlay", {UIPositionMode::TOP_ALIGNED, 0, 0, -1, -1});
}

void UIManager::removeOverlay() {
  // Remove the overlay panel if it exists
  if (hasComponent("__overlay")) {
    removeComponent("__overlay");
  }
}

void UIManager::removeComponentsWithPrefix(const std::string &prefix) {
  std::lock_guard<std::recursive_mutex> lock(m_componentsMutex);

  // Collect components to remove (can't modify map while iterating)
  std::vector<std::string> componentsToRemove;
  componentsToRemove.reserve(UIConstants::DEFAULT_COMPONENT_BATCH_SIZE); // Reserve capacity for performance

  for (const auto &[id, component] : m_components) {
    if (id.substr(0, prefix.length()) == prefix) {
      componentsToRemove.push_back(id);
    }
  }

  // Remove collected components
  for (const auto &id : componentsToRemove) {
    m_components.erase(id);
    // Remove from any layouts
    for (auto &[layoutId, layout] : m_layouts) {
      auto &children = layout->m_childComponents;
      children.erase(std::remove(children.begin(), children.end(), id),
                     children.end());
    }
  }
}

void UIManager::clearAllComponents() {
  // Enhanced clearAllComponents - preserve theme background but clear
  // everything else
  std::vector<std::string> componentsToRemove;
  componentsToRemove.reserve(UIConstants::MAX_COMPONENT_BATCH_SIZE); // Reserve capacity for performance

  for (const auto &[id, component] : m_components) {
    if (id != "__overlay") {
      componentsToRemove.push_back(id);
    }
  }

  for (const auto &id : componentsToRemove) {
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
  // Reset to default dark theme (only used by states that actually change
  // themes)
  setDarkTheme();
  m_currentThemeMode = "dark";
}

void UIManager::cleanupForStateTransition() {
  // Comprehensive cleanup for safe state transitions

  // Clear all UI components
  m_components.clear();
  invalidateComponentCache();

  // Clear value/text caches
  m_valueCache.clear();
  m_textCache.clear();

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
  m_globalFontID = UIConstants::FONT_DEFAULT;
  // NOTE: m_globalScale is NOT reset here - it's resolution-dependent and should
  // persist across state transitions. Only init() and onWindowResize() modify it.

  UI_INFO("UIManager prepared for state transition");
}

void UIManager::prepareForStateTransition() {
  // Simplified public interface that delegates to the comprehensive cleanup
  cleanupForStateTransition();
}

void UIManager::applyThemeToComponent(const std::string &id,
                                      UIComponentType type) {
  auto component = getComponent(id);
  if (component) {
    component->m_style = m_currentTheme.getStyle(type);
  }
}

void UIManager::setGlobalStyle(const UIStyle &style) { m_globalStyle = style; }

// Utility methods
void UIManager::setGlobalFont(const std::string &fontID) {
  m_globalFontID = fontID;

  // Update all components to use the new font
  for (const auto &[id, component] : m_components) {
    if (component) {
      component->m_style.fontID = fontID;
    }
  }
}

void UIManager::setGlobalScale(float scale) { m_globalScale = scale; }

float UIManager::calculateOptimalScale(int width, int height) const {
  // Use baseline resolution from UIConstants for consistent UI scaling
  // Cap at max UI scale to prevent UI from scaling larger than original on high resolutions
  float scale = std::min(width / UIConstants::BASELINE_WIDTH_F, height / UIConstants::BASELINE_HEIGHT_F);
  return std::min(UIConstants::MAX_UI_SCALE, scale);  // Cap at maximum UI scale
}

// Private helper methods
std::shared_ptr<UIComponent> UIManager::getComponent(const std::string &id) {
  std::lock_guard<std::recursive_mutex> lock(m_componentsMutex);
  auto it = m_components.find(id);
  return (it != m_components.end()) ? it->second : nullptr;
}

std::shared_ptr<const UIComponent>
UIManager::getComponent(const std::string &id) const {
  std::lock_guard<std::recursive_mutex> lock(m_componentsMutex);
  auto it = m_components.find(id);
  return (it != m_components.end()) ? it->second : nullptr;
}

std::shared_ptr<UILayout> UIManager::getLayout(const std::string &id) {
  auto it = m_layouts.find(id);
  return (it != m_layouts.end()) ? it->second : nullptr;
}

void UIManager::handleInput() {
  const auto &inputManager = InputManager::Instance();

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

  // Process components in reverse z-order (top to bottom, const ref avoids copy)
  bool mouseHandled = false;
  const auto& sortedComponents = getSortedComponents();
  for (auto it = sortedComponents.rbegin(); it != sortedComponents.rend(); ++it) {
    auto& component = *it;
    if (!component || !component->m_visible || !component->m_enabled) {
        continue;
    }

    if (mouseHandled) {
      // Reset state for components below
      if (component->m_state == UIState::HOVERED ||
          component->m_state == UIState::PRESSED) {
        component->m_state = UIState::NORMAL;
      }
      continue;
    }

    // InputManager already converts coordinates to logical coordinates
    int mouseX = static_cast<int>(mousePos.getX());
    int mouseY = static_cast<int>(mousePos.getY());

    bool isHovered = component->m_bounds.contains(mouseX, mouseY);

    if (isHovered) {
      m_hoveredComponents.push_back(component->m_id);

      // Handle hover state
      if (component->m_state == UIState::NORMAL) {
        component->m_state = UIState::HOVERED;
        if (component->m_onHover) {
          m_deferredCallbacks.push_back(component->m_onHover);
        }
      }

      // Handle click/press for interactive components
      if (component->m_type == UIComponentType::BUTTON ||
          component->m_type == UIComponentType::BUTTON_DANGER ||
          component->m_type == UIComponentType::BUTTON_SUCCESS ||
          component->m_type == UIComponentType::BUTTON_WARNING ||
          component->m_type == UIComponentType::CHECKBOX ||
          component->m_type == UIComponentType::SLIDER) {

        if (mouseJustPressed) {
          component->m_state = UIState::PRESSED;
          m_focusedComponent = component->m_id;
          if (component->m_onFocus) {
            m_deferredCallbacks.push_back(component->m_onFocus);
          }
        }

        if (mouseJustReleased && component->m_state == UIState::PRESSED) {
          // Handle click
          if (component->m_type == UIComponentType::BUTTON ||
              component->m_type == UIComponentType::BUTTON_DANGER ||
              component->m_type == UIComponentType::BUTTON_SUCCESS ||
              component->m_type == UIComponentType::BUTTON_WARNING) {
            m_clickedButtons.push_back(component->m_id);
            if (component->m_onClick) {
              m_deferredCallbacks.push_back(component->m_onClick);
            }
          } else if (component->m_type == UIComponentType::CHECKBOX) {
            component->m_checked = !component->m_checked;
            if (component->m_onClick) {
              m_deferredCallbacks.push_back(component->m_onClick);
            }
          }
          component->m_state = UIState::HOVERED;
          mouseHandled = true;
        }

        // Handle slider dragging
        if (component->m_type == UIComponentType::SLIDER &&
            component->m_state == UIState::PRESSED) {
          float relativeX = (mousePos.getX() - component->m_bounds.x) /
                            static_cast<float>(component->m_bounds.width);
          float newValue =
              component->m_minValue +
              relativeX * (component->m_maxValue - component->m_minValue);
          setValue(component->m_id, newValue);
         }

      }

      // Handle input field focus
      if (component->m_type == UIComponentType::INPUT_FIELD && mouseJustPressed) {
        m_focusedComponent = component->m_id;
        component->m_state = UIState::FOCUSED;
        if (component->m_onFocus) {
          m_deferredCallbacks.push_back(component->m_onFocus);
        }
        mouseHandled = true;
      }

      // Handle list selection
      if (component->m_type == UIComponentType::LIST && mouseJustPressed) {
        // Calculate item height dynamically based on current font metrics
        auto &fontManager = FontManager::Instance();
        int lineHeight = 0;
        int itemHeight = UIConstants::DEFAULT_LIST_ITEM_HEIGHT; // Default fallback
        if (fontManager.getFontMetrics(component->m_style.fontID, &lineHeight,
                                       nullptr, nullptr)) {
          itemHeight = lineHeight + UIConstants::LIST_ITEM_PADDING; // Add padding for better mouse accuracy
        }
        int itemIndex = static_cast<int>(
            (mousePos.getY() - component->m_bounds.y) / itemHeight);
        if (itemIndex >= 0 &&
            itemIndex < static_cast<int>(component->m_listItems.size())) {
          component->m_selectedIndex = itemIndex;
          if (component->m_onClick) {
            m_deferredCallbacks.push_back(component->m_onClick);
          }
        }
        mouseHandled = true;
      }
    } else {
      // Not hovered
      if (component->m_state == UIState::HOVERED) {
        component->m_state = UIState::NORMAL;
      }
      if (component->m_state == UIState::PRESSED && mouseJustReleased) {
        component->m_state = UIState::NORMAL;
      }
    }
  }

  // Handle focus loss
  if (mouseJustPressed && !mouseHandled) {
    if (!m_focusedComponent.empty()) {
      auto focusedComponent = getComponent(m_focusedComponent);
      if (focusedComponent && focusedComponent->m_state == UIState::FOCUSED) {
        focusedComponent->m_state = UIState::NORMAL;
      }
    }
    m_focusedComponent.clear();
  }
}

void UIManager::updateAnimations(float deltaTime) {
  for (auto it = m_animations.begin(); it != m_animations.end();) {
    auto &anim = *it;
    if (!anim->m_active) {
      it = m_animations.erase(it);
      continue;
    }

    anim->m_elapsed += deltaTime;
    float t = std::min(anim->m_elapsed / anim->m_duration, 1.0f);

    auto component = getComponent(anim->m_componentID);
    if (component) {
      // Apply animation
      if (anim->m_startBounds.width > 0) {
        // Position/size animation
        component->m_bounds =
            interpolateRect(anim->m_startBounds, anim->m_targetBounds, t);
      } else {
        // Color animation
        component->m_style.backgroundColor =
            interpolateColor(anim->m_startColor, anim->m_targetColor, t);
      }
    }

    if (t >= 1.0f) {
      // Animation complete
      anim->m_active = false;
      if (anim->m_onComplete) {
        anim->m_onComplete();
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
    m_hoveredTooltip =
        m_hoveredComponents.back(); // Use topmost hovered component
    m_tooltipTimer += deltaTime;
  } else {
    m_hoveredTooltip.clear();
    m_tooltipTimer = 0.0f;
  }
}

void UIManager::renderComponent(SDL_Renderer *renderer,
                                const std::shared_ptr<UIComponent> &component) {
  if (!component)
    return;

  switch (component->m_type) {
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
    renderPanel(renderer, component); // Dialogs render like panels
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
    renderLabel(
        renderer,
        component); // Titles render like labels but with different styling
    break;
  case UIComponentType::TOOLTIP:
    // Tooltips are rendered separately
    break;
  }
}

void UIManager::renderButton(SDL_Renderer *renderer,
                             const std::shared_ptr<UIComponent> &component) {
  if (!component || !renderer)
    return;

  SDL_Color bgColor = component->m_style.backgroundColor;

  // Apply state-based color
  switch (component->m_state) {
  case UIState::HOVERED:
    bgColor = component->m_style.hoverColor;
    break;
  case UIState::PRESSED:
    bgColor = component->m_style.pressedColor;
    break;
  case UIState::DISABLED:
    bgColor = component->m_style.disabledColor;
    break;
  default:
    break;
  }

  // Draw background
  drawRect(renderer, component->m_bounds, bgColor, true);

  // Draw border
  if (component->m_style.borderWidth > 0) {
    drawBorder(renderer, component->m_bounds, component->m_style.borderColor,
               component->m_style.borderWidth);
  }

  // Draw text
  if (!component->m_text.empty()) {
    auto &fontManager = FontManager::Instance();
#ifdef __APPLE__
    // On macOS, use logical coordinates directly - SDL3 handles scaling
    // automatically
    int centerX = component->m_bounds.x + component->m_bounds.width / 2;
    int centerY = component->m_bounds.y + component->m_bounds.height / 2;
#else
    // Use logical coordinates directly - SDL3 logical presentation handles
    // scaling
    int centerX = component->m_bounds.x + component->m_bounds.width / 2;
    int centerY = component->m_bounds.y + component->m_bounds.height / 2;
#endif
    fontManager.drawTextAligned(component->m_text, component->m_style.fontID,
                                centerX, centerY, component->m_style.textColor,
                                renderer, 0); // 0 = center
  }
}

void UIManager::renderLabel(SDL_Renderer *renderer,
                            const std::shared_ptr<UIComponent> &component) {
  if (!component || component->m_text.empty())
    return;

  int textX, textY, alignment;

  // Scale padding for resolution-aware spacing
  int scaledPadding = static_cast<int>(component->m_style.padding * m_globalScale);

  switch (component->m_style.textAlign) {
  case UIAlignment::CENTER_CENTER:
    textX = component->m_bounds.x + component->m_bounds.width / 2;
    textY = component->m_bounds.y + component->m_bounds.height / 2;
    alignment = 0; // center
    break;
  case UIAlignment::CENTER_RIGHT:
    textX = component->m_bounds.x + component->m_bounds.width - scaledPadding;
    textY = component->m_bounds.y + component->m_bounds.height / 2;
    alignment = 2; // right
    break;
  case UIAlignment::CENTER_LEFT:
    textX = component->m_bounds.x + scaledPadding;
    textY = component->m_bounds.y + component->m_bounds.height / 2;
    alignment = 1; // left
    break;
  case UIAlignment::TOP_CENTER:
    textX = component->m_bounds.x + component->m_bounds.width / 2;
    textY = component->m_bounds.y + scaledPadding;
    alignment = 4; // top-center
    break;
  case UIAlignment::TOP_LEFT:
    textX = component->m_bounds.x + scaledPadding;
    textY = component->m_bounds.y + scaledPadding;
    alignment = 3; // top-left
    break;
  case UIAlignment::TOP_RIGHT:
    textX = component->m_bounds.x + component->m_bounds.width - scaledPadding;
    textY = component->m_bounds.y + scaledPadding;
    alignment = 5; // top-right
    break;
  default:
    // CENTER_LEFT is default
    textX = component->m_bounds.x + scaledPadding;
    textY = component->m_bounds.y + component->m_bounds.height / 2;
    alignment = 1; // left
    break;
  }

  // Only use text backgrounds for components with transparent backgrounds
  bool needsBackground = component->m_style.useTextBackground &&
                         component->m_style.backgroundColor.a == 0;

#ifdef __APPLE__
  // On macOS, use logical coordinates directly - SDL3 handles scaling
  // automatically
  int finalTextX = static_cast<int>(textX);
  int finalTextY = static_cast<int>(textY);
#else
  // Use logical coordinates directly - SDL3 logical presentation handles
  // scaling
  int finalTextX = static_cast<int>(textX);
  int finalTextY = static_cast<int>(textY);
#endif

  // Use a custom text drawing method that renders background and text together
  int scaledTextBgPadding = static_cast<int>(component->m_style.textBackgroundPadding * m_globalScale);
  drawTextWithBackground(component->m_text, component->m_style.fontID, finalTextX,
                         finalTextY, component->m_style.textColor, renderer,
                         alignment, needsBackground,
                         component->m_style.textBackgroundColor,
                         scaledTextBgPadding);
}

void UIManager::renderPanel(SDL_Renderer *renderer,
                            const std::shared_ptr<UIComponent> &component) {
  if (!component)
    return;

  // Draw background
  drawRect(renderer, component->m_bounds, component->m_style.backgroundColor, true);

  // Draw border
  if (component->m_style.borderWidth > 0) {
    drawBorder(renderer, component->m_bounds, component->m_style.borderColor,
               component->m_style.borderWidth);
  }
}

void UIManager::renderProgressBar(
    SDL_Renderer *renderer, const std::shared_ptr<UIComponent> &component) {
  if (!component)
    return;

  // Draw background
  drawRect(renderer, component->m_bounds, component->m_style.backgroundColor, true);

  // Draw border
  if (component->m_style.borderWidth > 0) {
    drawBorder(renderer, component->m_bounds, component->m_style.borderColor,
               component->m_style.borderWidth);
  }

  // Calculate fill width
  float progress = (component->m_value - component->m_minValue) /
                   (component->m_maxValue - component->m_minValue);
  progress = std::clamp(progress, 0.0f, 1.0f);

  int fillWidth = static_cast<int>(component->m_bounds.width * progress);
  if (fillWidth > 0) {
    UIRect fillRect = {component->m_bounds.x, component->m_bounds.y, fillWidth,
                       component->m_bounds.height};
    drawRect(renderer, fillRect, component->m_style.hoverColor, true);
  }
}

void UIManager::renderInputField(
    SDL_Renderer *renderer, const std::shared_ptr<UIComponent> &component) {
  if (!component)
    return;

  SDL_Color bgColor = component->m_style.backgroundColor;
  if (component->m_state == UIState::FOCUSED) {
    bgColor = component->m_style.hoverColor;
  }

  // Draw background
  drawRect(renderer, component->m_bounds, bgColor, true);

  // Draw border
  SDL_Color borderColor = component->m_style.borderColor;
  if (component->m_state == UIState::FOCUSED) {
    borderColor = {100, 150, 255, 255}; // Blue focus border
  }
  drawBorder(renderer, component->m_bounds, borderColor,
             component->m_style.borderWidth);

  // Scale padding for resolution-aware spacing
  int scaledPadding = static_cast<int>(component->m_style.padding * m_globalScale);

  // Draw text or placeholder
  std::string displayText =
      component->m_text.empty() ? component->m_placeholder : component->m_text;
  if (!displayText.empty()) {
    SDL_Color textColor = component->m_text.empty()
                              ? SDL_Color{128, 128, 128, 255}
                              : component->m_style.textColor;

    auto &fontManager = FontManager::Instance();
#ifdef __APPLE__
    // On macOS, use logical coordinates directly - SDL3 handles scaling
    // automatically
    int textX = component->m_bounds.x + scaledPadding;
    int textY = component->m_bounds.y + component->m_bounds.height / 2;
#else
    // Use logical coordinates directly - SDL3 logical presentation handles
    // scaling
    int textX = component->m_bounds.x + scaledPadding;
    int textY = component->m_bounds.y + component->m_bounds.height / 2;
#endif
    fontManager.drawTextAligned(displayText, component->m_style.fontID, textX,
                                textY, textColor, renderer,
                                1); // 1 = left alignment
  }

  // Draw cursor if focused
  if (component->m_state == UIState::FOCUSED) {
    int cursorX = component->m_bounds.x + scaledPadding +
                  static_cast<int>(component->m_text.length() *
                                   UIConstants::INPUT_CURSOR_CHAR_WIDTH); // Approximate char width
    drawRect(renderer,
             {cursorX, component->m_bounds.y + scaledPadding / 2, 1,
              component->m_bounds.height - scaledPadding},
             component->m_style.textColor, true);
  }
}

void UIManager::renderImage(SDL_Renderer *renderer,
                            const std::shared_ptr<UIComponent> &component) {
  if (!component || component->m_textureID.empty())
    return;

  auto &textureManager = TextureManager::Instance();
  if (textureManager.isTextureInMap(component->m_textureID)) {
    textureManager.draw(component->m_textureID, component->m_bounds.x,
                        component->m_bounds.y, component->m_bounds.width,
                        component->m_bounds.height, renderer);
  }
}

void UIManager::renderSlider(SDL_Renderer *renderer,
                             const std::shared_ptr<UIComponent> &component) {
  if (!component)
    return;

  // Draw track
  UIRect trackRect = {component->m_bounds.x,
                      component->m_bounds.y + component->m_bounds.height / 2 - UIConstants::SLIDER_TRACK_OFFSET,
                      component->m_bounds.width, UIConstants::SLIDER_TRACK_HEIGHT};
  drawRect(renderer, trackRect, component->m_style.backgroundColor, true);
  drawBorder(renderer, trackRect, component->m_style.borderColor, UIConstants::BORDER_WIDTH_NORMAL);

  // Calculate handle position
  float progress = (component->m_value - component->m_minValue) /
                   (component->m_maxValue - component->m_minValue);
  progress = std::clamp(progress, 0.0f, 1.0f);

  int handleX = component->m_bounds.x +
                static_cast<int>((component->m_bounds.width - UIConstants::SLIDER_HANDLE_WIDTH) * progress);
  UIRect handleRect = {handleX, component->m_bounds.y, UIConstants::SLIDER_HANDLE_WIDTH,
                       component->m_bounds.height};

  SDL_Color handleColor = component->m_style.hoverColor;
  if (component->m_state == UIState::PRESSED) {
    handleColor = component->m_style.pressedColor;
  }

  drawRect(renderer, handleRect, handleColor, true);
  drawBorder(renderer, handleRect, component->m_style.borderColor, 1);
}

void UIManager::renderCheckbox(SDL_Renderer *renderer,
                               const std::shared_ptr<UIComponent> &component) {
  if (!component)
    return;

  // Scale checkbox size and padding for resolution-aware sizing
  int scaledCheckboxSize = static_cast<int>(UIConstants::CHECKBOX_SIZE * m_globalScale);
  int scaledPadding = static_cast<int>(component->m_style.padding * m_globalScale);

  // Draw box
  UIRect boxBounds;
  boxBounds.x = component->m_bounds.x;
  boxBounds.y = component->m_bounds.y +
                (component->m_bounds.height - scaledCheckboxSize) / 2;
  boxBounds.width = scaledCheckboxSize;
  boxBounds.height = scaledCheckboxSize;

  SDL_Color boxColor = component->m_style.backgroundColor;
  if (component->m_state == UIState::HOVERED) {
    boxColor = component->m_style.hoverColor;
  }
  drawRect(renderer, boxBounds, boxColor, true);

  // Draw checkmark if checked
  if (component->m_checked) {
    auto &fontManager = FontManager::Instance();
    fontManager.drawTextAligned("X", component->m_style.fontID,
                                boxBounds.x + boxBounds.width / 2,
                                boxBounds.y + boxBounds.height / 2,
                                component->m_style.textColor, renderer,
                                0); // Center
  }

  // Draw text
  if (!component->m_text.empty()) {
    auto &fontManager = FontManager::Instance();
    int textX = boxBounds.x + boxBounds.width + scaledPadding;
    int textY = component->m_bounds.y + component->m_bounds.height / 2;

    // Map UIAlignment enum to FontManager alignment codes
    int alignmentCode = 1; // default to left alignment
    switch (component->m_style.textAlign) {
    case UIAlignment::CENTER_CENTER:
      alignmentCode = 0; // center
      break;
    case UIAlignment::CENTER_RIGHT:
      alignmentCode = 2; // right
      break;
    case UIAlignment::CENTER_LEFT:
      alignmentCode = 1; // left
      break;
    case UIAlignment::TOP_CENTER:
      alignmentCode = 4; // top-center
      break;
    case UIAlignment::TOP_LEFT:
      alignmentCode = 3; // top-left
      break;
    case UIAlignment::TOP_RIGHT:
      alignmentCode = 5; // top-right
      break;
    default:
      alignmentCode = 1; // left
      break;
    }

    fontManager.drawTextAligned(component->m_text, component->m_style.fontID,
                                textX, textY, component->m_style.textColor,
                                renderer, alignmentCode);
  }
}




bool UIManager::isClickOnUI(const Vector2D& screenPos) const {
    int mouseX = static_cast<int>(screenPos.getX());
    int mouseY = static_cast<int>(screenPos.getY());

    for (const auto& [id, component] : m_components) {
        if (component && component->m_visible && component->m_enabled) {
            if (component->m_bounds.contains(mouseX, mouseY)) {
                return true; // Click is on a UI element
            }
        }
    }

    return false; // Click is not on any UI element
}


void regenerateListTextures(const std::shared_ptr<UIComponent>& component, SDL_Renderer* renderer) {
    if (!component) {
        return;
    }

    auto& fontManager = FontManager::Instance();
    
    // Implement cache size limit to prevent GPU memory pressure
    constexpr size_t MAX_CACHED_TEXTURES = 1000;
    
    // Always regenerate if list items changed or textures don't match current list
    bool needsFullRegeneration = component->m_listItemTextures.size() > MAX_CACHED_TEXTURES ||
                                component->m_listItemTextures.size() != component->m_listItems.size() ||
                                component->m_listItemsDirty;
    
    if (needsFullRegeneration) {
        component->m_listItemTextures.clear();
        component->m_listItemTextures.resize(std::min(component->m_listItems.size(), MAX_CACHED_TEXTURES));
    } else {
        // Resize to match current list size
        component->m_listItemTextures.resize(component->m_listItems.size());
    }

    // Generate textures for new or changed items
    for (size_t i = 0; i < component->m_listItems.size() && i < MAX_CACHED_TEXTURES; ++i) {
        const auto& itemText = component->m_listItems[i];
        
        // Ensure vector is large enough
        if (i >= component->m_listItemTextures.size()) {
            component->m_listItemTextures.resize(i + 1);
        }
        
        if (itemText.empty()) {
            component->m_listItemTextures[i] = nullptr;
            continue;
        }
        
        // Only regenerate if texture doesn't exist or if we cleared all textures
        if (!component->m_listItemTextures[i] || needsFullRegeneration) {
            SDL_Texture* texture = fontManager.renderText(itemText, component->m_style.fontID, 
                                                        component->m_style.textColor, renderer, true);
            if (texture) {
                component->m_listItemTextures[i] = std::shared_ptr<SDL_Texture>(texture, SDL_DestroyTexture);
            }
        }
    }

    component->m_listItemsDirty = false;
}

void UIManager::renderList(SDL_Renderer *renderer,
                           const std::shared_ptr<UIComponent> &component) {
  if (!component)
    return;

  // Regenerate textures if the list has changed
  regenerateListTextures(component, renderer);

  // Draw background
  drawRect(renderer, component->m_bounds, component->m_style.backgroundColor,
           true);

  // Draw border
  if (component->m_style.borderWidth > 0) {
    drawBorder(renderer, component->m_bounds, component->m_style.borderColor,
               component->m_style.borderWidth);
  }

  // Scale padding and listItemHeight for resolution-aware spacing
  int scaledPadding = static_cast<int>(component->m_style.padding * m_globalScale);
  int scaledItemHeight = static_cast<int>(component->m_style.listItemHeight * m_globalScale);

  // Draw list items using cached textures
  int itemY = component->m_bounds.y + scaledPadding;
  int itemHeight = scaledItemHeight;

  const size_t numItems = std::min(component->m_listItemTextures.size(), component->m_listItems.size());

  for (size_t i = 0; i < numItems; ++i) {
    UIRect itemBounds = {component->m_bounds.x, itemY, component->m_bounds.width,
                         itemHeight};

    // Highlight selected item
    if (static_cast<int>(i) == component->m_selectedIndex) {
      drawRect(renderer, itemBounds, component->m_style.hoverColor, true);
    }

    // Draw item texture with bounds checking
    if (i < component->m_listItemTextures.size()) {
        auto& texture = component->m_listItemTextures[i];
        if (texture) {
            float texW, texH;
            SDL_GetTextureSize(texture.get(), &texW, &texH);
            int textX = component->m_bounds.x + scaledPadding * 2;
            int textY = itemY + (itemHeight - static_cast<int>(texH)) / 2; // Center vertically
            SDL_FRect destRect = {static_cast<float>(textX), static_cast<float>(textY), texW, texH};
            SDL_RenderTexture(renderer, texture.get(), nullptr, &destRect);
        }
    }

    itemY += itemHeight;
  }
}

void UIManager::renderEventLog(SDL_Renderer *renderer,
                               const std::shared_ptr<UIComponent> &component) {
  if (!component)
    return;

  // Regenerate textures if the log has changed
  regenerateListTextures(component, renderer);

  // Draw background
  drawRect(renderer, component->m_bounds, component->m_style.backgroundColor,
           true);

  // Draw border
  if (component->m_style.borderWidth > 0) {
    drawBorder(renderer, component->m_bounds, component->m_style.borderColor,
               component->m_style.borderWidth);
  }

  // Scale padding and listItemHeight for resolution-aware spacing
  int scaledPadding = static_cast<int>(component->m_style.padding * m_globalScale);
  int scaledItemHeight = static_cast<int>(component->m_style.listItemHeight * m_globalScale);

  // Event logs scroll from bottom to top (newest entries at bottom)
  int itemHeight = scaledItemHeight;
  int availableHeight = component->m_bounds.height - (2 * scaledPadding);
  int maxVisibleItems = availableHeight / itemHeight;

  // Calculate which items to show (most recent at bottom)
  size_t startIndex = 0;
  if (component->m_listItems.size() > static_cast<size_t>(maxVisibleItems)) {
    startIndex = component->m_listItems.size() - maxVisibleItems;
  }

  int itemY = component->m_bounds.y + scaledPadding;
  size_t renderedCount = 0;

  for (size_t i = startIndex; i < component->m_listItems.size() && renderedCount < static_cast<size_t>(maxVisibleItems); ++i, ++renderedCount) {
    // Draw item texture with bounds checking
    if (i < component->m_listItemTextures.size()) {
        auto& texture = component->m_listItemTextures[i];
        if (texture) {
            float texW, texH;
            SDL_GetTextureSize(texture.get(), &texW, &texH);
            int textX = component->m_bounds.x + scaledPadding;
            int textY = itemY + (itemHeight - static_cast<int>(texH)) / 2; // Center vertically
            SDL_FRect destRect = {static_cast<float>(textX), static_cast<float>(textY), texW, texH};
            SDL_RenderTexture(renderer, texture.get(), nullptr, &destRect);
        }
    }

    itemY += itemHeight;
  }
}

void UIManager::renderTooltip(SDL_Renderer *renderer) {
  if (m_hoveredTooltip.empty() || m_tooltipTimer < m_tooltipDelay) {
    return;
  }

  auto component = getComponent(m_hoveredTooltip);
  if (!component || component->m_text.empty()) {
    return;
  }

  // Skip tooltips for titles
  if (component->m_type == UIComponentType::TITLE) {
    return;
  }

  // Skip tooltips for multi-line text (contains newlines)
  if (component->m_text.find('\n') != std::string::npos) {
    return;
  }

  // Scale padding for resolution-aware sizing
  int scaledPaddingWidth = static_cast<int>(UIConstants::TOOLTIP_PADDING_WIDTH * m_globalScale);
  int scaledPaddingHeight = static_cast<int>(UIConstants::TOOLTIP_PADDING_HEIGHT * m_globalScale);
  int scaledMouseOffset = static_cast<int>(UIConstants::TOOLTIP_MOUSE_OFFSET * m_globalScale);

  // Calculate actual text dimensions for content-aware sizing
  auto &fontManager = FontManager::Instance();
  auto tooltipTexture =
      fontManager.renderText(component->m_text, component->m_style.fontID,
                             component->m_style.textColor, renderer);

  int tooltipWidth = UIConstants::TOOLTIP_FALLBACK_WIDTH; // fallback width
  int tooltipHeight = UIConstants::TOOLTIP_FALLBACK_HEIGHT; // fallback height

  if (tooltipTexture) {
    float textW, textH;
    SDL_GetTextureSize(tooltipTexture.get(), &textW, &textH);
    tooltipWidth = static_cast<int>(textW) + scaledPaddingWidth; // Add padding
    tooltipHeight = static_cast<int>(textH) + scaledPaddingHeight; // Add padding
  }

  UIRect tooltipRect = {
      static_cast<int>(m_lastMousePosition.getX() + scaledMouseOffset),
      static_cast<int>(m_lastMousePosition.getY() - tooltipHeight - scaledMouseOffset),
      tooltipWidth, tooltipHeight};

  // Ensure tooltip stays on screen
  const auto &gameEngine = GameEngine::Instance();
  if (tooltipRect.x + tooltipRect.width > gameEngine.getLogicalWidth()) {
    tooltipRect.x = gameEngine.getLogicalWidth() - tooltipRect.width;
  }
  if (tooltipRect.y < 0) {
    tooltipRect.y = static_cast<int>(m_lastMousePosition.getY() + scaledMouseOffset * 2);
  }
  if (tooltipRect.y + tooltipRect.height > gameEngine.getLogicalHeight()) {
    tooltipRect.y = gameEngine.getLogicalHeight() - tooltipRect.height;
  }

  // Draw tooltip
  SDL_Color tooltipBg = {40, 40, 40, 240};
  drawRect(renderer, tooltipRect, tooltipBg, true);
  drawBorder(renderer, tooltipRect, {200, 200, 200, 255}, 1);

#ifdef __APPLE__
  // On macOS, use logical coordinates directly - SDL3 handles scaling
  // automatically
  int centerX = tooltipRect.x + tooltipRect.width / 2;
  int centerY = tooltipRect.y + tooltipRect.height / 2;
#else
  // Use logical coordinates directly - SDL3 logical presentation handles
  // scaling
  int centerX = tooltipRect.x + tooltipRect.width / 2;
  int centerY = tooltipRect.y + tooltipRect.height / 2;
#endif
  fontManager.drawTextAligned(component->m_text, component->m_style.fontID, centerX,
                              centerY, component->m_style.textColor, renderer,
                              0); // 0 = center alignment
}

// Layout implementations
void UIManager::applyAbsoluteLayout(
    const std::shared_ptr<UILayout> & /* layout */) {
  // Absolute layout doesn't change component positions
}

void UIManager::applyFlowLayout(const std::shared_ptr<UILayout> &layout) {
  if (!layout)
    return;

  int currentX = layout->m_bounds.x;
  int currentY = layout->m_bounds.y;
  int maxHeight = 0;

  for (const auto &componentID : layout->m_childComponents) {
    auto component = getComponent(componentID);
    if (!component)
      continue;

    // Check if we need to wrap to next line
    if (currentX + component->m_bounds.width >
        layout->m_bounds.x + layout->m_bounds.width) {
      currentX = layout->m_bounds.x;
      currentY += maxHeight + layout->m_spacing;
      maxHeight = 0;
    }

    component->m_bounds.x = currentX;
    component->m_bounds.y = currentY;

    currentX += component->m_bounds.width + layout->m_spacing;
    maxHeight = std::max(maxHeight, component->m_bounds.height);
  }
}

void UIManager::applyGridLayout(const std::shared_ptr<UILayout> &layout) {
  if (!layout || layout->m_columns <= 0)
    return;

  int cellWidth = layout->m_bounds.width / layout->m_columns;
  int cellHeight = layout->m_bounds.height / layout->m_rows;

  for (size_t i = 0; i < layout->m_childComponents.size(); ++i) {
    auto component = getComponent(layout->m_childComponents[i]);
    if (!component)
      continue;

    int col = i % layout->m_columns;
    int row = i / layout->m_columns;

    component->m_bounds.x = layout->m_bounds.x + col * cellWidth;
    component->m_bounds.y = layout->m_bounds.y + row * cellHeight;
    component->m_bounds.width = cellWidth - layout->m_spacing;
    component->m_bounds.height = cellHeight - layout->m_spacing;
  }
}

void UIManager::applyStackLayout(const std::shared_ptr<UILayout> &layout) {
  if (!layout)
    return;

  int currentY = layout->m_bounds.y;

  for (const auto &componentID : layout->m_childComponents) {
    auto component = getComponent(componentID);
    if (!component)
      continue;

    component->m_bounds.x = layout->m_bounds.x;
    component->m_bounds.y = currentY;
    component->m_bounds.width = layout->m_bounds.width;

    currentY += component->m_bounds.height + layout->m_spacing;
  }
}

void UIManager::applyAnchorLayout(const std::shared_ptr<UILayout> &layout) {
  // TODO: Implement anchor-based layout
  applyAbsoluteLayout(layout);
}

// Utility helper implementations
void UIManager::drawRect(SDL_Renderer *renderer, const UIRect &rect,
                         const SDL_Color &color, bool filled) {
  SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);

  SDL_FRect sdlRect = {static_cast<float>(rect.x), static_cast<float>(rect.y),
                       static_cast<float>(rect.width),
                       static_cast<float>(rect.height)};
  if (filled) {
    SDL_RenderFillRect(renderer, &sdlRect);
  } else {
    SDL_RenderRect(renderer, &sdlRect);
  }
}

void UIManager::drawBorder(SDL_Renderer *renderer, const UIRect &rect,
                           const SDL_Color &color, int width) {
  SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);

  for (int i = 0; i < width; ++i) {
    SDL_FRect borderRect = {static_cast<float>(rect.x - i),
                            static_cast<float>(rect.y - i),
                            static_cast<float>(rect.width + 2 * i),
                            static_cast<float>(rect.height + 2 * i)};
    SDL_RenderRect(renderer, &borderRect);
  }
}

void UIManager::drawTextWithBackground(const std::string &text,
                                       const std::string &fontID, int x, int y,
                                       SDL_Color textColor,
                                       SDL_Renderer *renderer, int alignment,
                                       bool useBackground,
                                       SDL_Color backgroundColor, int padding) {
  auto &fontManager = FontManager::Instance();

  // First render the text to get actual dimensions
  auto texture = fontManager.renderText(text, fontID, textColor, renderer);
  if (!texture)
    return;

  // Get the actual texture size
  float w, h;
  SDL_GetTextureSize(texture.get(), &w, &h);
  int width = static_cast<int>(w);
  int height = static_cast<int>(h);

  // Calculate position based on alignment (same as
  // FontManager::drawTextAligned)
  float destX, destY;

  switch (alignment) {
  case 1: // Left alignment
    destX = static_cast<float>(x);
    destY = static_cast<float>(y - height / 2.0f);
    break;
  case 2: // Right alignment
    destX = static_cast<float>(x - width);
    destY = static_cast<float>(y - height / 2.0f);
    break;
  case 3: // Top-left alignment
    destX = static_cast<float>(x);
    destY = static_cast<float>(y);
    break;
  case 4: // Top-center alignment
    destX = static_cast<float>(x - width / 2.0f);
    destY = static_cast<float>(y);
    break;
  case 5: // Top-right alignment
    destX = static_cast<float>(x - width);
    destY = static_cast<float>(y);
    break;
  default: // Center alignment (0)
    destX = static_cast<float>(x - width / 2.0f);
    destY = static_cast<float>(y - height / 2.0f);
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
  SDL_FRect dstRect = {destX, destY, static_cast<float>(width),
                       static_cast<float>(height)};
  SDL_RenderTexture(renderer, texture.get(), nullptr, &dstRect);
}


SDL_Color UIManager::interpolateColor(const SDL_Color &start,
                                      const SDL_Color &end, float t) {
  return {static_cast<Uint8>(start.r + (end.r - start.r) * t),
          static_cast<Uint8>(start.g + (end.g - start.g) * t),
          static_cast<Uint8>(start.b + (end.b - start.b) * t),
          static_cast<Uint8>(start.a + (end.a - start.a) * t)};
}

UIRect UIManager::interpolateRect(const UIRect &start, const UIRect &end,
                                  float t) {
  return {static_cast<int>(start.x + (end.x - start.x) * t),
          static_cast<int>(start.y + (end.y - start.y) * t),
          static_cast<int>(start.width + (end.width - start.width) * t),
          static_cast<int>(start.height + (end.height - start.height) * t)};
}

// Auto-sizing implementation
void UIManager::calculateOptimalSize(const std::string &id) {
  auto component = getComponent(id);
  if (component) {
    calculateOptimalSize(component);
  }
}

void UIManager::calculateOptimalSize(std::shared_ptr<UIComponent> component) {
  if (!component || !component->m_autoSize) {
    return;
  }

  int contentWidth = 0;
  int contentHeight = 0;

  if (!measureComponentContent(component, &contentWidth, &contentHeight)) {
    return; // Failed to measure content
  }

  // Apply content padding (scaled for resolution-aware sizing)
  int scaledContentPadding = static_cast<int>(component->m_contentPadding * m_globalScale);

  // Implement grow-only behavior for lists to prevent shrinking
  if (component->m_type == UIComponentType::LIST) {
    // Update minimum bounds to current size to prevent shrinking
    component->m_minBounds.width =
        std::max(component->m_minBounds.width, component->m_bounds.width);
    component->m_minBounds.height =
        std::max(component->m_minBounds.height, component->m_bounds.height);
  }

  // Apply size constraints - ONLY modify width/height, preserve x/y position
  if (component->m_autoWidth) {
    int totalWidth = contentWidth + (scaledContentPadding * 2);
    int oldWidth = component->m_bounds.width;
    component->m_bounds.width =
        std::max(component->m_minBounds.width,
                 std::min(totalWidth, component->m_maxBounds.width));

    // Automatically center only titles and labels with CENTER alignment when
    // width changes
    if (component->m_style.textAlign == UIAlignment::CENTER_CENTER &&
        component->m_bounds.width != oldWidth &&
        (component->m_type == UIComponentType::TITLE ||
         component->m_type == UIComponentType::LABEL)) {
      // Get logical width for centering calculation
      const auto &gameEngine = GameEngine::Instance();
      int windowWidth = gameEngine.getLogicalWidth();
      component->m_bounds.x = (windowWidth - component->m_bounds.width) / 2;
    }
  }

  if (component->m_autoHeight) {
    int totalHeight = contentHeight + (scaledContentPadding * 2);
    component->m_bounds.height =
        std::max(component->m_minBounds.height,
                 std::min(totalHeight, component->m_maxBounds.height));
  }

  // CRITICAL: Never modify component->m_bounds.x or component->m_bounds.y
  // Auto-sizing only affects dimensions, not position

  // Trigger content changed callback if present
  if (component->m_onContentChanged) {
    component->m_onContentChanged();
  }
}

bool UIManager::measureComponentContent(
    const std::shared_ptr<UIComponent> &component, int *width, int *height) {
  if (!component || !width || !height) {
    return false;
  }

  auto &fontManager = FontManager::Instance();

  switch (component->m_type) {
  case UIComponentType::BUTTON:
  case UIComponentType::BUTTON_DANGER:
  case UIComponentType::BUTTON_SUCCESS:
  case UIComponentType::BUTTON_WARNING:
  case UIComponentType::LABEL:
  case UIComponentType::TITLE:
    if (!component->m_text.empty()) {
      // Check if text contains newlines - use multiline measurement if so
      if (component->m_text.find('\n') != std::string::npos) {
        return fontManager.measureMultilineText(
            component->m_text, component->m_style.fontID, 0, width, height);
      } else {
        return fontManager.measureText(component->m_text, component->m_style.fontID,
                                       width, height);
      }
    }
    *width = component->m_minBounds.width;
    *height = component->m_minBounds.height;
    return true;

  case UIComponentType::INPUT_FIELD:
    // For input fields, measure placeholder or current text
    if (!component->m_text.empty()) {
      fontManager.measureText(component->m_text, component->m_style.fontID, width,
                              height);
    } else if (!component->m_placeholder.empty()) {
      fontManager.measureText(component->m_placeholder, component->m_style.fontID,
                              width, height);
    } else {
      // Default to reasonable input field size
      fontManager.measureText("Sample Text", component->m_style.fontID, width,
                              height);
    }
    // Input fields need extra space for cursor and interaction (scaled)
    *width += static_cast<int>(UIConstants::INPUT_CURSOR_SPACE * m_globalScale);
    return true;

  case UIComponentType::LIST: {
    // Calculate height based on font metrics dynamically
    int lineHeight = 0;
    int itemHeight = UIConstants::DEFAULT_LIST_ITEM_HEIGHT; // Default fallback
    int scaledPadding = static_cast<int>(UIConstants::LIST_ITEM_PADDING * m_globalScale);
    if (fontManager.getFontMetrics(component->m_style.fontID, &lineHeight,
                                   nullptr, nullptr)) {
      itemHeight = lineHeight + scaledPadding; // Add padding for better mouse accuracy
    } else {
      // If font metrics fail, use reasonable fallback based on expected font
      // sizes Assume 21px font (typical for UI) + 8px padding = 29px
      itemHeight = UIConstants::FALLBACK_LIST_ITEM_HEIGHT;
    }

    // Calculate based on list items and item height
    if (!component->m_listItems.empty()) {
      int maxItemWidth = 0;
      for (const auto &item : component->m_listItems) {
        int itemWidth = 0;
        if (fontManager.measureText(item, component->m_style.fontID, &itemWidth,
                                    nullptr)) {
          maxItemWidth = std::max(maxItemWidth, itemWidth);
        } else {
          // If text measurement fails, estimate based on character count
          // Assume ~12px per character for UI fonts
          maxItemWidth =
              std::max(maxItemWidth, static_cast<int>(item.length() * UIConstants::CHAR_WIDTH_ESTIMATE));
        }
      }
      int scaledScrollbarSpace = static_cast<int>(UIConstants::SCROLLBAR_WIDTH * m_globalScale);
      *width = std::max(maxItemWidth + scaledScrollbarSpace,
                        UIConstants::MIN_LIST_WIDTH); // Add scrollbar space, minimum list width
      *height = itemHeight * static_cast<int>(component->m_listItems.size());
    } else {
      // Provide reasonable defaults for empty lists
      *width = UIConstants::DEFAULT_LIST_WIDTH;             // Default width
      *height = itemHeight * UIConstants::DEFAULT_LIST_VISIBLE_ITEMS; // Height for default visible items
    }
    return true;
  }

  case UIComponentType::EVENT_LOG:
    // Fixed size for game event display
    *width = component->m_bounds.width;
    *height = component->m_bounds.height;
    return true;

  case UIComponentType::TOOLTIP:
    if (!component->m_text.empty()) {
      return fontManager.measureText(component->m_text, component->m_style.fontID,
                                     width, height);
    }
    break;

  default:
    // For other component types, use current bounds or minimums
    *width = std::max(component->m_bounds.width, component->m_minBounds.width);
    *height = std::max(component->m_bounds.height, component->m_minBounds.height);
    return true;
  }

  // Fallback to minimum bounds
  *width = component->m_minBounds.width;
  *height = component->m_minBounds.height;
  return true;
}

void UIManager::invalidateLayout(const std::string &layoutID) {
  // Mark layout for recalculation on next update
  // For now, immediately recalculate
  recalculateLayout(layoutID);
}

void UIManager::recalculateLayout(const std::string &layoutID) {
  auto layout = getLayout(layoutID);
  if (!layout) {
    return;
  }

  // First, auto-size all child components
  for (const auto &componentID : layout->m_childComponents) {
    calculateOptimalSize(componentID);
  }

  // Then apply the layout with new sizes
  updateLayout(layoutID);
}

void UIManager::enableAutoSizing(const std::string &id, bool enable) {
  auto component = getComponent(id);
  if (component) {
    component->m_autoSize = enable;
    if (enable) {
      calculateOptimalSize(component);
    }
  }
}

void UIManager::setAutoSizingConstraints(const std::string &id,
                                         const UIRect &minBounds,
                                         const UIRect &maxBounds) {
  auto component = getComponent(id);
  if (component) {
    component->m_minBounds = minBounds;
    component->m_maxBounds = maxBounds;
    calculateOptimalSize(component);
  }
}

// Auto-detection methods
int UIManager::getLogicalWidth() const {
  const auto &gameEngine = GameEngine::Instance();
  return gameEngine.getLogicalWidth();
}

int UIManager::getLogicalHeight() const {
  const auto &gameEngine = GameEngine::Instance();
  return gameEngine.getLogicalHeight();
}

// Auto-detecting overlay creation
void UIManager::createOverlay() {
  // Use baseline dimensions from UIConstants - createOverlay(width, height) will scale to logical space
  createOverlay(UIConstants::BASELINE_WIDTH, UIConstants::BASELINE_HEIGHT);
}

// Convenience positioning methods
void UIManager::createTitleAtTop(const std::string &id, const std::string &text,
                                 int height) {
  // Use baseline width from UIConstants - createTitle() will scale to logical space
  createTitle(id, {0, UIConstants::TITLE_TOP_OFFSET, UIConstants::BASELINE_WIDTH, height}, text);
  setTitleAlignment(id, UIAlignment::CENTER_CENTER);

  // Apply positioning using unified API
  setComponentPositioning(id, {UIPositionMode::TOP_ALIGNED,
                               0,
                               UIConstants::TITLE_TOP_OFFSET,
                               -1,  // -1 = use full window width
                               height});
}

void UIManager::createButtonAtBottom(const std::string &id,
                                     const std::string &text, int width,
                                     int height) {
  // Use baseline height from UIConstants - createButtonDanger() will scale to logical space
  createButtonDanger(id, {UIConstants::BUTTON_BOTTOM_OFFSET, UIConstants::BASELINE_HEIGHT - height - UIConstants::BUTTON_BOTTOM_OFFSET, width, height},
                     text);

  // Apply positioning using unified API
  setComponentPositioning(id, {UIPositionMode::BOTTOM_ALIGNED,
                               UIConstants::BUTTON_BOTTOM_OFFSET,
                               UIConstants::BUTTON_BOTTOM_OFFSET,
                               width,
                               height});
}

void UIManager::createCenteredDialog(const std::string &id, int width,
                                     int height, const std::string &theme) {
  // Use baseline dimensions from UIConstants - createModal() will scale to logical space
  // Calculate centered position in baseline space
  int x = (UIConstants::BASELINE_WIDTH - width) / 2;
  int y = (UIConstants::BASELINE_HEIGHT - height) / 2;

  // Overlay also uses baseline dimensions
  createModal(id, {x, y, width, height}, theme, UIConstants::BASELINE_WIDTH, UIConstants::BASELINE_HEIGHT);

  // Apply positioning using unified API (dialog itself)
  setComponentPositioning(id, {UIPositionMode::CENTERED_BOTH,
                               0,
                               0,
                               width,
                               height});

  // Apply positioning for overlay background using unified API
  setComponentPositioning("overlay_background", {UIPositionMode::TOP_ALIGNED,
                                                 0,
                                                 0,
                                                 -1,   // Full width
                                                 -1}); // Full height
}

void UIManager::createCenteredButton(const std::string &id, int offsetY,
                                     int width, int height,
                                     const std::string &text) {
  // Calculate baseline center position
  int centerX = (UIConstants::BASELINE_WIDTH - width) / 2;
  int centerY = UIConstants::BASELINE_HEIGHT / 2 + offsetY;

  createButton(id, {centerX, centerY, width, height}, text);

  // Apply positioning using unified API
  setComponentPositioning(id, {UIPositionMode::CENTERED_BOTH,
                               0,
                               offsetY,
                               width,
                               height});
}

// Auto-repositioning system implementation
void UIManager::onWindowResize(int newLogicalWidth, int newLogicalHeight) {
  std::lock_guard<std::recursive_mutex> lock(m_componentsMutex);

  UI_DEBUG(std::format("Window resized: {}x{} - auto-repositioning UI components",
                       newLogicalWidth, newLogicalHeight));

  // Recalculate UI scale for new resolution (1920x1080 baseline, capped at 1.0)
  m_globalScale = calculateOptimalScale(newLogicalWidth, newLogicalHeight);
  UI_INFO(std::format("UI scale updated to {} for new resolution {}x{}",
                      m_globalScale, newLogicalWidth, newLogicalHeight));

  repositionAllComponents(newLogicalWidth, newLogicalHeight);
  m_currentLogicalWidth = newLogicalWidth;
  m_currentLogicalHeight = newLogicalHeight;
}

void UIManager::repositionAllComponents(int width, int height) {
  // Note: Called from onWindowResize() which already holds m_componentsMutex
  for (auto &[id, component] : m_components) {
    if (component) {
      applyPositioning(component, width, height);
    }
  }
}

void UIManager::applyPositioning(std::shared_ptr<UIComponent> component,
                                 int width, int height) {
  if (!component) {
    return;
  }

  auto &pos = component->m_positioning;
  auto &bounds = component->m_bounds;

  // Update dimensions if fixed sizes specified, applying global scale for resolution adaptation
  // Special cases:
  //   widthPercent/heightPercent > 0 = percentage of window dimension (takes precedence)
  //   -1 = use full window dimension
  //   < -1 = use full window dimension minus the absolute value (for margins)
  if (pos.widthPercent > 0.0f && pos.widthPercent <= 1.0f) {
    bounds.width = static_cast<int>(width * pos.widthPercent);  // Percentage-based width
  } else if (pos.fixedWidth == -1) {
    bounds.width = width;
  } else if (pos.fixedWidth < -1) {
    bounds.width = width + static_cast<int>(pos.fixedWidth * m_globalScale);  // Scale negative margin
  } else if (pos.fixedWidth > 0) {
    bounds.width = static_cast<int>(pos.fixedWidth * m_globalScale);  // Scale fixed width
  }

  if (pos.heightPercent > 0.0f && pos.heightPercent <= 1.0f) {
    bounds.height = static_cast<int>(height * pos.heightPercent);  // Percentage-based height
  } else if (pos.fixedHeight == -1) {
    bounds.height = height;
  } else if (pos.fixedHeight < -1) {
    bounds.height = height + static_cast<int>(pos.fixedHeight * m_globalScale);  // Scale negative margin
  } else if (pos.fixedHeight > 0) {
    bounds.height = static_cast<int>(pos.fixedHeight * m_globalScale);  // Scale fixed height
  }

  // Apply positioning based on mode, with scaled offsets for resolution adaptation
  int scaledOffsetX = static_cast<int>(pos.offsetX * m_globalScale);
  int scaledOffsetY = static_cast<int>(pos.offsetY * m_globalScale);

  switch (pos.mode) {
  case UIPositionMode::ABSOLUTE:
    // No change - keep current position
    break;

  case UIPositionMode::CENTERED_H:
    // Horizontally centered + offsetX, fixed offsetY
    bounds.x = (width - bounds.width) / 2 + scaledOffsetX;
    bounds.y = scaledOffsetY;
    break;

  case UIPositionMode::CENTERED_V:
    // Vertically centered + offsetY, fixed offsetX
    bounds.x = scaledOffsetX;
    bounds.y = (height - bounds.height) / 2 + scaledOffsetY;
    break;

  case UIPositionMode::CENTERED_BOTH:
    // Center both axes + offsets
    bounds.x = (width - bounds.width) / 2 + scaledOffsetX;
    bounds.y = (height - bounds.height) / 2 + scaledOffsetY;
    break;

  case UIPositionMode::TOP_ALIGNED:
    // Top-left: x = offsetX, y = offsetY
    bounds.x = scaledOffsetX;
    bounds.y = scaledOffsetY;
    break;

  case UIPositionMode::TOP_RIGHT:
    // Top-right: x = right - width - offsetX, y = offsetY
    bounds.x = width - bounds.width - scaledOffsetX;
    bounds.y = scaledOffsetY;
    break;

  case UIPositionMode::BOTTOM_ALIGNED:
    // Bottom edge - height - offsetY, fixed offsetX
    bounds.x = scaledOffsetX;
    bounds.y = height - bounds.height - scaledOffsetY;
    break;

  case UIPositionMode::BOTTOM_CENTERED:
    // Bottom edge - height - offsetY, horizontally centered + offsetX
    bounds.x = (width - bounds.width) / 2 + scaledOffsetX;
    bounds.y = height - bounds.height - scaledOffsetY;
    break;

  case UIPositionMode::BOTTOM_RIGHT:
    // Bottom-right corner: right edge - width - offsetX, bottom edge - height - offsetY
    bounds.x = width - bounds.width - scaledOffsetX;
    bounds.y = height - bounds.height - scaledOffsetY;
    break;

  case UIPositionMode::LEFT_ALIGNED:
    // Left edge + offsetX, vertically centered + offsetY
    bounds.x = scaledOffsetX;
    bounds.y = (height - bounds.height) / 2 + scaledOffsetY;
    break;

  case UIPositionMode::RIGHT_ALIGNED:
    // Right edge - width - offsetX, vertically centered + offsetY
    bounds.x = width - bounds.width - scaledOffsetX;
    bounds.y = (height - bounds.height) / 2 + scaledOffsetY;
    break;
  }
}

void UIManager::setComponentPositioning(const std::string &id,
                                        const UIPositioning &positioning) {
  std::lock_guard<std::recursive_mutex> lock(m_componentsMutex);
  auto component = getComponent(id);
  if (component) {
    component->m_positioning = positioning;
    // Immediately apply the new positioning
    applyPositioning(component, m_currentLogicalWidth, m_currentLogicalHeight);
  }
}
