/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#ifndef UI_MANAGER_HPP
#define UI_MANAGER_HPP

#include "utils/Vector2D.hpp"
#include <SDL3/SDL.h>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// Forward declarations
class FontManager;
class InputManager;
class TextureManager;

// UI Component Types
enum class UIComponentType {
  BUTTON,
  BUTTON_DANGER,  // Red colored buttons (Back, Quit, Exit, Delete, etc.)
  BUTTON_SUCCESS, // Green colored buttons (Save, Confirm, Accept, etc.)
  BUTTON_WARNING, // Orange/Yellow colored buttons (Caution, Reset, etc.)
  LABEL,
  TITLE,
  PANEL,
  PROGRESS_BAR,
  INPUT_FIELD,
  IMAGE,
  SLIDER,
  CHECKBOX,
  LIST,
  TOOLTIP,
  EVENT_LOG,
  DIALOG
};

// Layout Types
enum class UILayoutType { ABSOLUTE, FLOW, GRID, STACK, ANCHOR };

// UI States
enum class UIState { NORMAL, HOVERED, PRESSED, DISABLED, FOCUSED };

// Alignment options
enum class UIAlignment {
  LEFT,
  CENTER,
  RIGHT,
  TOP,
  BOTTOM,
  TOP_LEFT,
  TOP_CENTER,
  TOP_RIGHT,
  CENTER_LEFT,
  CENTER_CENTER,
  CENTER_RIGHT,
  BOTTOM_LEFT,
  BOTTOM_CENTER,
  BOTTOM_RIGHT
};

// UI Rectangle structure
struct UIRect {
  int x{0};
  int y{0};
  int width{0};
  int height{0};

  UIRect() = default;
  UIRect(int x_, int y_, int w_, int h_)
      : x(x_), y(y_), width(w_), height(h_) {}

  bool contains(int px, int py) const {
    return px >= x && px < x + width && py >= y && py < y + height;
  }

  SDL_Rect toSDLRect() const { return {x, y, width, height}; }
};

// UI Style structure
struct UIStyle {
  SDL_Color backgroundColor{50, 50, 50, 255};
  SDL_Color borderColor{100, 100, 100, 255};
  SDL_Color textColor{255, 255, 255, 255};
  SDL_Color hoverColor{70, 70, 70, 255};
  SDL_Color pressedColor{30, 30, 30, 255};
  SDL_Color disabledColor{80, 80, 80, 128};

  // Text background properties (for labels and titles)
  SDL_Color textBackgroundColor{0, 0, 0,
                                128}; // Semi-transparent black by default
  bool useTextBackground{false};      // Enable text background for readability
  int textBackgroundPadding{4};       // Extra padding around text background

  int borderWidth{1};
  int padding{8};
  int margin{4};
  int listItemHeight{32}; // Configurable height for list items (increased from
                          // 20 for better mouse accuracy)

  std::string fontID{"fonts_UI_Arial"};
  int fontSize{16};

  UIAlignment textAlign{UIAlignment::CENTER_CENTER};
};

// Base UI Component
struct UIComponent {
  std::string m_id{};
  UIComponentType m_type{};
  UIRect m_bounds{};
  UIState m_state{UIState::NORMAL};
  UIStyle m_style{};
  bool m_visible{true};
  bool m_enabled{true};
  int m_zOrder{0};

  // Auto-sizing properties
  bool m_autoSize{true}; // Enable content-aware auto-sizing by default
  UIRect m_minBounds{0, 0, 32,
                   16}; // Minimum size constraints (only width/height used)
  UIRect m_maxBounds{0, 0, 800,
                   600};    // Maximum size constraints (only width/height used)
  int m_contentPadding{8};    // Padding around content for size calculations
  bool m_autoWidth{true};     // Auto-size width based on content
  bool m_autoHeight{true};    // Auto-size height based on content
  bool m_sizeToContent{true}; // Size exactly to fit content (vs. expand to fill)

  // Component-specific data
  std::string m_text{};
  std::string m_textureID{};
  float m_value{0.0f};
  float m_minValue{0.0f};
  float m_maxValue{1.0f};
  bool m_checked{false};
  std::vector<std::string> m_listItems{};
  int m_selectedIndex{-1};
  std::string m_placeholder{};
  int m_maxLength{256};

  // Callbacks
  std::function<void()> m_onClick{};
  std::function<void(float)> m_onValueChanged{};
  std::function<void(const std::string &)> m_onTextChanged{};
  std::function<void()> m_onHover{};
  std::function<void()> m_onFocus{};
  std::function<void()>
      m_onContentChanged{}; // Called when content changes and resize is needed

  virtual ~UIComponent() = default;
};

// Layout Container
struct UILayout {
  std::string m_id{};
  UILayoutType m_type{UILayoutType::ABSOLUTE};
  UIRect m_bounds{};
  std::vector<std::string> m_childComponents{};

  // Layout-specific properties
  int m_spacing{4};
  int m_columns{1};
  int m_rows{1};
  UIAlignment m_alignment{UIAlignment::TOP_LEFT};
  bool m_autoSize{false};
};

// UI Theme
struct UITheme {
  std::string m_name{"default"};
  std::unordered_map<UIComponentType, UIStyle> m_componentStyles{};

  UIStyle getStyle(UIComponentType type) const {
    auto it = m_componentStyles.find(type);
    return (it != m_componentStyles.end()) ? it->second : UIStyle{};
  }
};

// Animation data
struct UIAnimation {
  std::string m_componentID{};
  float m_duration{0.0f};
  float m_elapsed{0.0f};
  bool m_active{false};

  UIRect m_startBounds{};
  UIRect m_targetBounds{};
  SDL_Color m_startColor{};
  SDL_Color m_targetColor{};

  std::function<void()> m_onComplete{};
};

// Event log state for auto-updating
struct EventLogState {
  float m_timer{0.0f};
  int m_messageIndex{0};
  float m_updateInterval{2.0f};
  bool m_autoUpdate{false};
};

class UIManager {
public:
  ~UIManager() {
    if (!m_isShutdown) {
      clean();
    }
  }

  static UIManager &Instance() {
    static UIManager instance;
    return instance;
  }

  // Core system methods
  bool init();
  void update(float deltaTime);
  void render(SDL_Renderer *renderer);
  void render(); // Overloaded version using cached renderer
  void clean();
  bool isShutdown() const { return m_isShutdown; }

  // Renderer management
  void setRenderer(SDL_Renderer *renderer) { m_cachedRenderer = renderer; }
  SDL_Renderer *getRenderer() const { return m_cachedRenderer; }

  // UI Component creation methods
  void createButton(const std::string &id, const UIRect &bounds,
                    const std::string &text = "");
  void createButtonDanger(const std::string &id, const UIRect &bounds,
                          const std::string &text = "");
  void createButtonSuccess(const std::string &id, const UIRect &bounds,
                           const std::string &text = "");
  void createButtonWarning(const std::string &id, const UIRect &bounds,
                           const std::string &text = "");
  void createLabel(const std::string &id, const UIRect &bounds,
                   const std::string &text = "");
  void createTitle(const std::string &id, const UIRect &bounds,
                   const std::string &text);
  void createPanel(const std::string &id, const UIRect &bounds);
  void createProgressBar(const std::string &id, const UIRect &bounds,
                         float minVal = 0.0f, float maxVal = 1.0f);
  void createInputField(const std::string &id, const UIRect &bounds,
                        const std::string &placeholder = "");
  void createImage(const std::string &id, const UIRect &bounds,
                   const std::string &textureID = "");
  void createSlider(const std::string &id, const UIRect &bounds,
                    float minVal = 0.0f, float maxVal = 1.0f);
  void createCheckbox(const std::string &id, const UIRect &bounds,
                      const std::string &text = "");
  void createList(const std::string &id, const UIRect &bounds);
  void createTooltip(const std::string &id, const std::string &text = "");
  void createEventLog(const std::string &id, const UIRect &bounds,
                      int maxEntries = 5);
  void createDialog(const std::string &id, const UIRect &bounds);

  // Modal creation helper - combines theme + overlay + dialog
  void createModal(const std::string &dialogId, const UIRect &bounds,
                   const std::string &theme, int windowWidth, int windowHeight);

  // Theme management
  void refreshAllComponentThemes();

  // Component manipulation
  void removeComponent(const std::string &id);
  void clearAllComponents();
  bool hasComponent(const std::string &id) const;
  void setComponentVisible(const std::string &id, bool visible);
  void setComponentEnabled(const std::string &id, bool enabled);
  void setComponentBounds(const std::string &id, const UIRect &bounds);
  void setComponentZOrder(const std::string &id, int zOrder);

  // Component property setters
  void setText(const std::string &id, const std::string &text);
  void setTexture(const std::string &id, const std::string &textureID);
  void setValue(const std::string &id, float value);
  void setChecked(const std::string &id, bool checked);
  void setStyle(const std::string &id, const UIStyle &style);

  // Component property getters
  std::string getText(const std::string &id) const;
  float getValue(const std::string &id) const;
  bool getChecked(const std::string &id) const;
  UIRect getBounds(const std::string &id) const;
  UIState getComponentState(const std::string &id) const;

  // Event handling
  bool isButtonClicked(const std::string &id) const;
  bool isButtonPressed(const std::string &id) const;
  bool isButtonHovered(const std::string &id) const;
  bool isComponentFocused(const std::string &id) const;

  // Callback setters
  void setOnClick(const std::string &id, std::function<void()> callback);
  void setOnValueChanged(const std::string &id,
                         std::function<void(float)> callback);
  void setOnTextChanged(const std::string &id,
                        std::function<void(const std::string &)> callback);
  void setOnHover(const std::string &id, std::function<void()> callback);
  void setOnFocus(const std::string &id, std::function<void()> callback);

  // Layout management
  void createLayout(const std::string &id, UILayoutType type,
                    const UIRect &bounds);
  void addComponentToLayout(const std::string &layoutID,
                            const std::string &componentID);
  void removeComponentFromLayout(const std::string &layoutID,
                                 const std::string &componentID);
  void updateLayout(const std::string &layoutID);
  void setLayoutSpacing(const std::string &layoutID, int spacing);
  void setLayoutColumns(const std::string &layoutID, int columns);
  void setLayoutAlignment(const std::string &layoutID, UIAlignment alignment);

  // Progress bar specific methods
  void updateProgressBar(const std::string &id, float value);
  void setProgressBarRange(const std::string &id, float minVal, float maxVal);

  // List specific methods
  void addListItem(const std::string &listID, const std::string &item);
  void removeListItem(const std::string &listID, int index);
  void clearList(const std::string &listID);
  int getSelectedListItem(const std::string &listID) const;
  void setSelectedListItem(const std::string &listID, int index);

  // Enhanced list methods for auto-scrolling and management
  void setListMaxItems(const std::string &listID, int maxItems);
  void addListItemWithAutoScroll(const std::string &listID,
                                 const std::string &item);
  void clearListItems(const std::string &listID);

  // Event log specific methods
  // Event log management
  void addEventLogEntry(const std::string &logID, const std::string &entry);
  void clearEventLog(const std::string &logID);
  void setEventLogMaxEntries(const std::string &logID, int maxEntries);
  void setupDemoEventLog(const std::string &logID);
  void enableEventLogAutoUpdate(const std::string &logID,
                                float interval = 2.0f);
  void disableEventLogAutoUpdate(const std::string &logID);

  // Title specific methods
  void setTitleAlignment(const std::string &titleID, UIAlignment alignment);
  void
  centerTitleInContainer(const std::string &titleID, int containerX,
                         int containerWidth); // Center title after auto-sizing

  // Input field specific methods
  void setInputFieldPlaceholder(const std::string &id,
                                const std::string &placeholder);
  void setInputFieldMaxLength(const std::string &id, int maxLength);
  bool isInputFieldFocused(const std::string &id) const;

  // Animation system
  void animateMove(const std::string &id, const UIRect &targetBounds,
                   float duration, std::function<void()> onComplete = nullptr);
  void animateColor(const std::string &id, const SDL_Color &targetColor,
                    float duration, std::function<void()> onComplete = nullptr);
  void stopAnimation(const std::string &id);
  bool isAnimating(const std::string &id) const;

  // Theme management
  void loadTheme(const UITheme &theme);
  void setDefaultTheme();
  void setLightTheme();
  void setDarkTheme();
  void setThemeMode(const std::string &mode);
  const std::string &getCurrentThemeMode() const;
  void applyThemeToComponent(const std::string &id, UIComponentType type);
  void setGlobalStyle(const UIStyle &style);

  // Overlay management - creates/removes semi-transparent background overlays
  void
  createOverlay(int windowWidth,
                int windowHeight); // Creates overlay using specified dimensions
  void
  createOverlay(); // Creates overlay using auto-detected logical dimensions
  void removeOverlay(); // Removes the overlay background

  // Text background methods (for labels and titles readability)
  void enableTextBackground(const std::string &id, bool enable = true);
  void setTextBackgroundColor(const std::string &id, SDL_Color color);
  void setTextBackgroundPadding(const std::string &id, int padding);

  // Component cleanup utilities
  void removeComponentsWithPrefix(const std::string &prefix);
  void resetToDefaultTheme();
  void cleanupForStateTransition();

  // Simplified state transition method
  void prepareForStateTransition();

  // Auto-sizing core methods
  void calculateOptimalSize(
      const std::string &id); // Calculate and apply optimal size for component
  void calculateOptimalSize(
      std::shared_ptr<UIComponent>
          component); // Calculate and apply optimal size for component
  bool measureComponentContent(const std::shared_ptr<UIComponent> &component,
                               int *width,
                               int *height); // Measure content dimensions
  void invalidateLayout(
      const std::string &layoutID); // Mark layout for recalculation
  void recalculateLayout(
      const std::string
          &layoutID); // Recalculate layout with new component sizes
  void enableAutoSizing(
      const std::string &id,
      bool enable = true); // Enable/disable auto-sizing for component
  void
  setAutoSizingConstraints(const std::string &id, const UIRect &minBounds,
                           const UIRect &maxBounds); // Set size constraints

  // Auto-detection and convenience methods
  int getLogicalWidth() const;  // Auto-detect logical width from GameEngine
  int getLogicalHeight() const; // Auto-detect logical height from GameEngine
  void createTitleAtTop(const std::string &id, const std::string &text,
                        int height = 40);
  void createButtonAtBottom(const std::string &id, const std::string &text,
                            int width = 120, int height = 40);
  void createCenteredDialog(const std::string &id, int width, int height,
                            const std::string &theme = "dark");

  // Utility methods
  void setGlobalFont(const std::string &fontID);
  void setGlobalScale(float scale);
  float getGlobalScale() const { return m_globalScale; }
  void enableTooltips(bool enable) { m_tooltipsEnabled = enable; }
  void setTooltipDelay(float delay) { m_tooltipDelay = delay; }

  // Debug methods
  void setDebugMode(bool enable) { m_debugMode = enable; }
  void drawDebugBounds(bool enable) { m_drawDebugBounds = enable; }

private:
  // Core data
  std::unordered_map<std::string, std::shared_ptr<UIComponent>> m_components{};
  std::vector<std::shared_ptr<UIComponent>> m_sortedComponents{};
  bool m_sortIsDirty{true};
  std::unordered_map<std::string, std::shared_ptr<UILayout>> m_layouts{};
  std::vector<std::shared_ptr<UIAnimation>> m_animations{};

  // State tracking
  std::vector<std::string> m_clickedButtons{};
  std::vector<std::string> m_hoveredComponents{};
  std::string m_focusedComponent{};
  std::string m_hoveredTooltip{};
  float m_tooltipTimer{0.0f};

  // Theme and styling
  UITheme m_currentTheme{};
  UIStyle m_globalStyle{};
  std::string m_globalFontID{"default"};
  std::string m_titleFontID{"fonts_Arial"};
  std::string m_uiFontID{"fonts_UI_Arial"};
  float m_globalScale{1.0f};
  std::string m_currentThemeMode{"light"};

  // Settings
  bool m_tooltipsEnabled{true};
  float m_tooltipDelay{1.0f};
  bool m_debugMode{false};
  bool m_drawDebugBounds{false};

  // Event log state tracking
  std::unordered_map<std::string, EventLogState> m_eventLogStates{};
  bool m_isShutdown{false};

  // Input state
  Vector2D m_lastMousePosition{};
  bool m_mousePressed{false};
  bool m_mouseReleased{false};

  // Private helper methods
  std::shared_ptr<UIComponent> getComponent(const std::string &id);
  std::shared_ptr<const UIComponent> getComponent(const std::string &id) const;
  std::shared_ptr<UILayout> getLayout(const std::string &id);

  void handleInput();
  void updateAnimations(float deltaTime);
  void updateTooltips(float deltaTime);
  void updateEventLogs(float deltaTime);
  void renderComponent(SDL_Renderer *renderer,
                       const std::shared_ptr<UIComponent> &component);
  void renderTooltip(SDL_Renderer *renderer);
  void sortComponentsByZOrder();

  // Component-specific rendering
  void renderButton(SDL_Renderer *renderer,
                    const std::shared_ptr<UIComponent> &component);
  void renderLabel(SDL_Renderer *renderer,
                   const std::shared_ptr<UIComponent> &component);
  void renderPanel(SDL_Renderer *renderer,
                   const std::shared_ptr<UIComponent> &component);
  void renderProgressBar(SDL_Renderer *renderer,
                         const std::shared_ptr<UIComponent> &component);
  void renderInputField(SDL_Renderer *renderer,
                        const std::shared_ptr<UIComponent> &component);
  void renderImage(SDL_Renderer *renderer,
                   const std::shared_ptr<UIComponent> &component);
  void renderSlider(SDL_Renderer *renderer,
                    const std::shared_ptr<UIComponent> &component);
  void renderCheckbox(SDL_Renderer *renderer,
                      const std::shared_ptr<UIComponent> &component);
  void renderList(SDL_Renderer *renderer,
                  const std::shared_ptr<UIComponent> &component);
  void renderEventLog(SDL_Renderer *renderer,
                      const std::shared_ptr<UIComponent> &component);

  // Layout helpers
  void applyAbsoluteLayout(const std::shared_ptr<UILayout> &layout);
  void applyFlowLayout(const std::shared_ptr<UILayout> &layout);
  void applyGridLayout(const std::shared_ptr<UILayout> &layout);
  void applyStackLayout(const std::shared_ptr<UILayout> &layout);
  void applyAnchorLayout(const std::shared_ptr<UILayout> &layout);

  // Utility helpers
  void drawRect(SDL_Renderer *renderer, const UIRect &rect,
                const SDL_Color &color, bool filled = true);
  void drawBorder(SDL_Renderer *renderer, const UIRect &rect,
                  const SDL_Color &color, int width = 1);
  void drawTextWithBackground(const std::string &text,
                              const std::string &fontID, int x, int y,
                              SDL_Color textColor, SDL_Renderer *renderer,
                              int alignment, bool useBackground,
                              SDL_Color backgroundColor, int padding);
  UIRect calculateTextBounds(const std::string &text, const std::string &fontID,
                             const UIRect &container, UIAlignment alignment);
  SDL_Color interpolateColor(const SDL_Color &start, const SDL_Color &end,
                             float t);
  UIRect interpolateRect(const UIRect &start, const UIRect &end, float t);

  // Cached renderer for performance
  SDL_Renderer *m_cachedRenderer{nullptr};

  // Text cache for performance optimization
  std::unordered_map<std::string, std::string> m_textCache{};

  // Delete copy constructor and assignment operator
  UIManager(const UIManager &) = delete;
  UIManager &operator=(const UIManager &) = delete;

  UIManager() = default;
};

#endif // UI_MANAGER_HPP