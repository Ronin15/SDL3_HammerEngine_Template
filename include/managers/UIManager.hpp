/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#ifndef UI_MANAGER_HPP
#define UI_MANAGER_HPP

#include <SDL3/SDL.h>
#include <boost/container/flat_map.hpp>
#include <boost/container/small_vector.hpp>
#include <memory>
#include <string>
#include <functional>
#include "utils/Vector2D.hpp"

// Forward declarations
class FontManager;
class InputManager;
class TextureManager;

// UI Component Types
enum class UIComponentType {
    BUTTON,
    LABEL,
    PANEL,
    PROGRESS_BAR,
    INPUT_FIELD,
    IMAGE,
    SLIDER,
    CHECKBOX,
    LIST,
    TOOLTIP
};

// Layout Types
enum class UILayoutType {
    ABSOLUTE,
    FLOW,
    GRID,
    STACK,
    ANCHOR
};

// UI States
enum class UIState {
    NORMAL,
    HOVERED,
    PRESSED,
    DISABLED,
    FOCUSED
};

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
    UIRect(int x_, int y_, int w_, int h_) : x(x_), y(y_), width(w_), height(h_) {}
    
    bool contains(int px, int py) const {
        return px >= x && px < x + width && py >= y && py < y + height;
    }
    
    SDL_Rect toSDLRect() const {
        return {x, y, width, height};
    }
};

// UI Style structure
struct UIStyle {
    SDL_Color backgroundColor{50, 50, 50, 255};
    SDL_Color borderColor{100, 100, 100, 255};
    SDL_Color textColor{255, 255, 255, 255};
    SDL_Color hoverColor{70, 70, 70, 255};
    SDL_Color pressedColor{30, 30, 30, 255};
    SDL_Color disabledColor{80, 80, 80, 128};
    
    int borderWidth{1};
    int padding{8};
    int margin{4};
    
    std::string fontID{"fonts_UI_Arial"};
    int fontSize{16};
    
    UIAlignment textAlign{UIAlignment::CENTER_CENTER};
};

// Base UI Component
struct UIComponent {
    std::string id{};
    UIComponentType type{};
    UIRect bounds{};
    UIState state{UIState::NORMAL};
    UIStyle style{};
    bool visible{true};
    bool enabled{true};
    int zOrder{0};
    
    // Component-specific data
    std::string text{};
    std::string textureID{};
    float value{0.0f};
    float minValue{0.0f};
    float maxValue{1.0f};
    bool checked{false};
    boost::container::small_vector<std::string, 16> listItems{};
    int selectedIndex{-1};
    std::string placeholder{};
    int maxLength{256};
    
    // Callbacks
    std::function<void()> onClick{};
    std::function<void(float)> onValueChanged{};
    std::function<void(const std::string&)> onTextChanged{};
    std::function<void()> onHover{};
    std::function<void()> onFocus{};
    
    virtual ~UIComponent() = default;
};

// Layout Container
struct UILayout {
    std::string id{};
    UILayoutType type{UILayoutType::ABSOLUTE};
    UIRect bounds{};
    boost::container::small_vector<std::string, 16> childComponents{};
    
    // Layout-specific properties
    int spacing{4};
    int columns{1};
    int rows{1};
    UIAlignment alignment{UIAlignment::TOP_LEFT};
    bool autoSize{false};
};

// UI Theme
struct UITheme {
    std::string name{"default"};
    boost::container::flat_map<UIComponentType, UIStyle> componentStyles{};
    
    UIStyle getStyle(UIComponentType type) const {
        auto it = componentStyles.find(type);
        return (it != componentStyles.end()) ? it->second : UIStyle{};
    }
};

// Animation data
struct UIAnimation {
    std::string componentID{};
    float duration{0.0f};
    float elapsed{0.0f};
    bool active{false};
    
    UIRect startBounds{};
    UIRect targetBounds{};
    SDL_Color startColor{};
    SDL_Color targetColor{};
    
    std::function<void()> onComplete{};
};

class UIManager {
public:
    ~UIManager() {
        if (!m_isShutdown) {
            clean();
        }
    }

    static UIManager& Instance() {
        static UIManager instance;
        return instance;
    }

    // Core system methods
    bool init();
    void update(float deltaTime);
    void render(SDL_Renderer* renderer);
    void clean();
    bool isShutdown() const { return m_isShutdown; }

    // Component creation methods
    void createButton(const std::string& id, const UIRect& bounds, const std::string& text = "");
    void createLabel(const std::string& id, const UIRect& bounds, const std::string& text = "");
    void createPanel(const std::string& id, const UIRect& bounds);
    void createProgressBar(const std::string& id, const UIRect& bounds, float minVal = 0.0f, float maxVal = 1.0f);
    void createInputField(const std::string& id, const UIRect& bounds, const std::string& placeholder = "");
    void createImage(const std::string& id, const UIRect& bounds, const std::string& textureID = "");
    void createSlider(const std::string& id, const UIRect& bounds, float minVal = 0.0f, float maxVal = 1.0f);
    void createCheckbox(const std::string& id, const UIRect& bounds, const std::string& text = "");
    void createList(const std::string& id, const UIRect& bounds);
    void createTooltip(const std::string& id, const std::string& text = "");

    // Component manipulation
    void removeComponent(const std::string& id);
    void clearAllComponents();
    bool hasComponent(const std::string& id) const;
    void setComponentVisible(const std::string& id, bool visible);
    void setComponentEnabled(const std::string& id, bool enabled);
    void setComponentBounds(const std::string& id, const UIRect& bounds);
    void setComponentZOrder(const std::string& id, int zOrder);

    // Component property setters
    void setText(const std::string& id, const std::string& text);
    void setTexture(const std::string& id, const std::string& textureID);
    void setValue(const std::string& id, float value);
    void setChecked(const std::string& id, bool checked);
    void setStyle(const std::string& id, const UIStyle& style);

    // Component property getters
    std::string getText(const std::string& id) const;
    float getValue(const std::string& id) const;
    bool getChecked(const std::string& id) const;
    UIRect getBounds(const std::string& id) const;
    UIState getComponentState(const std::string& id) const;

    // Event handling
    bool isButtonClicked(const std::string& id) const;
    bool isButtonPressed(const std::string& id) const;
    bool isButtonHovered(const std::string& id) const;
    bool isComponentFocused(const std::string& id) const;

    // Callback setters
    void setOnClick(const std::string& id, std::function<void()> callback);
    void setOnValueChanged(const std::string& id, std::function<void(float)> callback);
    void setOnTextChanged(const std::string& id, std::function<void(const std::string&)> callback);
    void setOnHover(const std::string& id, std::function<void()> callback);
    void setOnFocus(const std::string& id, std::function<void()> callback);

    // Layout management
    void createLayout(const std::string& id, UILayoutType type, const UIRect& bounds);
    void addComponentToLayout(const std::string& layoutID, const std::string& componentID);
    void removeComponentFromLayout(const std::string& layoutID, const std::string& componentID);
    void updateLayout(const std::string& layoutID);
    void setLayoutSpacing(const std::string& layoutID, int spacing);
    void setLayoutColumns(const std::string& layoutID, int columns);
    void setLayoutAlignment(const std::string& layoutID, UIAlignment alignment);

    // Progress bar specific methods
    void updateProgressBar(const std::string& id, float value);
    void setProgressBarRange(const std::string& id, float minVal, float maxVal);

    // List specific methods
    void addListItem(const std::string& listID, const std::string& item);
    void removeListItem(const std::string& listID, int index);
    void clearList(const std::string& listID);
    int getSelectedListItem(const std::string& listID) const;
    void setSelectedListItem(const std::string& listID, int index);

    // Input field specific methods
    void setInputFieldPlaceholder(const std::string& id, const std::string& placeholder);
    void setInputFieldMaxLength(const std::string& id, int maxLength);
    bool isInputFieldFocused(const std::string& id) const;

    // Animation system
    void animateMove(const std::string& id, const UIRect& targetBounds, float duration, std::function<void()> onComplete = nullptr);
    void animateColor(const std::string& id, const SDL_Color& targetColor, float duration, std::function<void()> onComplete = nullptr);
    void stopAnimation(const std::string& id);
    bool isAnimating(const std::string& id) const;

    // Theme management
    void loadTheme(const UITheme& theme);
    void setDefaultTheme();
    void applyThemeToComponent(const std::string& id, UIComponentType type);
    void setGlobalStyle(const UIStyle& style);

    // Utility methods
    void setGlobalFont(const std::string& fontID);
    void setGlobalScale(float scale);
    float getGlobalScale() const { return m_globalScale; }
    void enableTooltips(bool enable) { m_tooltipsEnabled = enable; }
    void setTooltipDelay(float delay) { m_tooltipDelay = delay; }

    // Debug methods
    void setDebugMode(bool enable) { m_debugMode = enable; }
    void drawDebugBounds(bool enable) { m_drawDebugBounds = enable; }

private:
    // Core data
    boost::container::flat_map<std::string, std::shared_ptr<UIComponent>> m_components{};
    boost::container::flat_map<std::string, std::shared_ptr<UILayout>> m_layouts{};
    boost::container::small_vector<std::shared_ptr<UIAnimation>, 16> m_animations{};
    
    // State tracking
    boost::container::small_vector<std::string, 8> m_clickedButtons{};
    boost::container::small_vector<std::string, 8> m_hoveredComponents{};
    std::string m_focusedComponent{};
    std::string m_hoveredTooltip{};
    float m_tooltipTimer{0.0f};
    
    // Theme and styling
    UITheme m_currentTheme{};
    UIStyle m_globalStyle{};
    std::string m_globalFontID{"default"};
    float m_globalScale{1.0f};
    
    // Settings
    bool m_tooltipsEnabled{true};
    float m_tooltipDelay{1.0f};
    bool m_debugMode{false};
    bool m_drawDebugBounds{false};
    bool m_isShutdown{false};
    
    // Input state
    Vector2D m_lastMousePosition{};
    bool m_mousePressed{false};
    bool m_mouseReleased{false};
    
    // Private helper methods
    std::shared_ptr<UIComponent> getComponent(const std::string& id);
    std::shared_ptr<const UIComponent> getComponent(const std::string& id) const;
    std::shared_ptr<UILayout> getLayout(const std::string& id);
    void handleInput();
    void updateAnimations(float deltaTime);
    void updateTooltips(float deltaTime);
    void renderComponent(SDL_Renderer* renderer, const std::shared_ptr<UIComponent>& component);
    void renderTooltip(SDL_Renderer* renderer);
    void sortComponentsByZOrder();
    
    // Component-specific rendering
    void renderButton(SDL_Renderer* renderer, const std::shared_ptr<UIComponent>& component);
    void renderLabel(SDL_Renderer* renderer, const std::shared_ptr<UIComponent>& component);
    void renderPanel(SDL_Renderer* renderer, const std::shared_ptr<UIComponent>& component);
    void renderProgressBar(SDL_Renderer* renderer, const std::shared_ptr<UIComponent>& component);
    void renderInputField(SDL_Renderer* renderer, const std::shared_ptr<UIComponent>& component);
    void renderImage(SDL_Renderer* renderer, const std::shared_ptr<UIComponent>& component);
    void renderSlider(SDL_Renderer* renderer, const std::shared_ptr<UIComponent>& component);
    void renderCheckbox(SDL_Renderer* renderer, const std::shared_ptr<UIComponent>& component);
    void renderList(SDL_Renderer* renderer, const std::shared_ptr<UIComponent>& component);
    
    // Layout helpers
    void applyAbsoluteLayout(const std::shared_ptr<UILayout>& layout);
    void applyFlowLayout(const std::shared_ptr<UILayout>& layout);
    void applyGridLayout(const std::shared_ptr<UILayout>& layout);
    void applyStackLayout(const std::shared_ptr<UILayout>& layout);
    void applyAnchorLayout(const std::shared_ptr<UILayout>& layout);
    
    // Utility helpers
    void drawRect(SDL_Renderer* renderer, const UIRect& rect, const SDL_Color& color, bool filled = true);
    void drawBorder(SDL_Renderer* renderer, const UIRect& rect, const SDL_Color& color, int width = 1);
    UIRect calculateTextBounds(const std::string& text, const std::string& fontID, const UIRect& container, UIAlignment alignment);
    SDL_Color interpolateColor(const SDL_Color& start, const SDL_Color& end, float t);
    UIRect interpolateRect(const UIRect& start, const UIRect& end, float t);
    
    // Delete copy constructor and assignment operator
    UIManager(const UIManager&) = delete;
    UIManager& operator=(const UIManager&) = delete;
    
    UIManager() = default;
};

#endif // UI_MANAGER_HPP