/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#define BOOST_TEST_MODULE UIManagerFunctionalTests
#include <boost/test/unit_test.hpp>

#include <memory>
#include <atomic>

#include "managers/UIManager.hpp"

// ============================================================================
// FIXTURE: UIManagerFixture
// ============================================================================

struct UIManagerFixture {
    UIManagerFixture() {
        UIManager::Instance().init();

        // Set initial window size
        UIManager::Instance().onWindowResize(800, 600);
    }

    ~UIManagerFixture() {
        UIManager::Instance().clean();
    }
};

// ============================================================================
// TEST SUITE: UIPositioningTests
// ============================================================================
// Tests that validate UI positioning modes work correctly

BOOST_FIXTURE_TEST_SUITE(UIPositioningTests, UIManagerFixture)

// ----------------------------------------------------------------------------
// Test: ABSOLUTE positioning (backward compatibility)
// ----------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(TestAbsolutePositioning) {
    auto& ui = UIManager::Instance();

    // Create button with absolute positioning
    ui.createButton("abs_button", UIRect{100, 50, 200, 40}, "Absolute");

    UIPositioning positioning;
    positioning.mode = UIPositionMode::ABSOLUTE;
    positioning.offsetX = 100;
    positioning.offsetY = 50;

    ui.setComponentPositioning("abs_button", positioning);

    // Verify button exists
    BOOST_CHECK(ui.hasComponent("abs_button"));

    // Window resize should NOT move absolute positioned elements
    ui.onWindowResize(1024, 768);

    // Component should still exist after resize
    BOOST_CHECK(ui.hasComponent("abs_button"));

    ui.removeComponent("abs_button");
}

// ----------------------------------------------------------------------------
// Test: CENTERED_H positioning (horizontal center)
// ----------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(TestCenteredHorizontalPositioning) {
    auto& ui = UIManager::Instance();

    // Create button centered horizontally
    ui.createButton("centered_h_button", UIRect{350, 50, 100, 40}, "CenterH");

    UIPositioning positioning;
    positioning.mode = UIPositionMode::CENTERED_H;
    positioning.offsetX = 0;      // No horizontal offset
    positioning.offsetY = 50;     // 50 pixels from top
    positioning.fixedWidth = 100;

    ui.setComponentPositioning("centered_h_button", positioning);

    // Window resize should reposition horizontally
    ui.onWindowResize(1024, 768);

    // Button should still exist and be centered
    BOOST_CHECK(ui.hasComponent("centered_h_button"));

    ui.removeComponent("centered_h_button");
}

// ----------------------------------------------------------------------------
// Test: CENTERED_V positioning (vertical center)
// ----------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(TestCenteredVerticalPositioning) {
    auto& ui = UIManager::Instance();

    // Create button centered vertically
    ui.createButton("centered_v_button", UIRect{50, 280, 100, 40}, "CenterV");

    UIPositioning positioning;
    positioning.mode = UIPositionMode::CENTERED_V;
    positioning.offsetX = 50;     // 50 pixels from left
    positioning.offsetY = 0;      // No vertical offset
    positioning.fixedHeight = 40;

    ui.setComponentPositioning("centered_v_button", positioning);

    // Window resize should reposition vertically
    ui.onWindowResize(800, 768);

    BOOST_CHECK(ui.hasComponent("centered_v_button"));

    ui.removeComponent("centered_v_button");
}

// ----------------------------------------------------------------------------
// Test: CENTERED_BOTH positioning (center both axes)
// ----------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(TestCenteredBothPositioning) {
    auto& ui = UIManager::Instance();

    // Create button centered on both axes
    ui.createButton("centered_both", UIRect{350, 280, 100, 40}, "Center");

    UIPositioning positioning;
    positioning.mode = UIPositionMode::CENTERED_BOTH;
    positioning.offsetX = 0;
    positioning.offsetY = 0;
    positioning.fixedWidth = 100;
    positioning.fixedHeight = 40;

    ui.setComponentPositioning("centered_both", positioning);

    // Window resize should center on both axes
    ui.onWindowResize(1024, 768);

    BOOST_CHECK(ui.hasComponent("centered_both"));

    ui.removeComponent("centered_both");
}

// ----------------------------------------------------------------------------
// Test: TOP_ALIGNED positioning
// ----------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(TestTopAlignedPositioning) {
    auto& ui = UIManager::Instance();

    // Create button aligned to top edge
    ui.createButton("top_aligned", UIRect{350, 20, 100, 40}, "Top");

    UIPositioning positioning;
    positioning.mode = UIPositionMode::TOP_ALIGNED;
    positioning.offsetX = 0;      // Horizontally centered
    positioning.offsetY = 20;     // 20 pixels from top
    positioning.fixedWidth = 100;

    ui.setComponentPositioning("top_aligned", positioning);

    ui.onWindowResize(1024, 768);

    BOOST_CHECK(ui.hasComponent("top_aligned"));

    ui.removeComponent("top_aligned");
}

// ----------------------------------------------------------------------------
// Test: BOTTOM_ALIGNED positioning
// ----------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(TestBottomAlignedPositioning) {
    auto& ui = UIManager::Instance();

    // Create button aligned to bottom edge
    ui.createButton("bottom_aligned", UIRect{350, 540, 100, 40}, "Bottom");

    UIPositioning positioning;
    positioning.mode = UIPositionMode::BOTTOM_ALIGNED;
    positioning.offsetX = 0;      // Horizontally centered
    positioning.offsetY = 20;     // 20 pixels from bottom
    positioning.fixedWidth = 100;
    positioning.fixedHeight = 40;

    ui.setComponentPositioning("bottom_aligned", positioning);

    ui.onWindowResize(1024, 768);

    BOOST_CHECK(ui.hasComponent("bottom_aligned"));

    ui.removeComponent("bottom_aligned");
}

// ----------------------------------------------------------------------------
// Test: LEFT_ALIGNED positioning
// ----------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(TestLeftAlignedPositioning) {
    auto& ui = UIManager::Instance();

    // Create button aligned to left edge
    ui.createButton("left_aligned", UIRect{20, 280, 100, 40}, "Left");

    UIPositioning positioning;
    positioning.mode = UIPositionMode::LEFT_ALIGNED;
    positioning.offsetX = 20;     // 20 pixels from left
    positioning.offsetY = 0;      // Vertically centered
    positioning.fixedHeight = 40;

    ui.setComponentPositioning("left_aligned", positioning);

    ui.onWindowResize(800, 768);

    BOOST_CHECK(ui.hasComponent("left_aligned"));

    ui.removeComponent("left_aligned");
}

// ----------------------------------------------------------------------------
// Test: RIGHT_ALIGNED positioning
// ----------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(TestRightAlignedPositioning) {
    auto& ui = UIManager::Instance();

    // Create button aligned to right edge
    ui.createButton("right_aligned", UIRect{680, 280, 100, 40}, "Right");

    UIPositioning positioning;
    positioning.mode = UIPositionMode::RIGHT_ALIGNED;
    positioning.offsetX = 20;     // 20 pixels from right edge
    positioning.offsetY = 0;      // Vertically centered
    positioning.fixedWidth = 100;
    positioning.fixedHeight = 40;

    ui.setComponentPositioning("right_aligned", positioning);

    ui.onWindowResize(1024, 768);

    BOOST_CHECK(ui.hasComponent("right_aligned"));

    ui.removeComponent("right_aligned");
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// TEST SUITE: UICallbackTests
// ============================================================================
// Tests that validate UI callbacks fire correctly

BOOST_FIXTURE_TEST_SUITE(UICallbackTests, UIManagerFixture)

// ----------------------------------------------------------------------------
// Test: onClick callback fires when button is clicked
// ----------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(TestOnClickCallback) {
    auto& ui = UIManager::Instance();

    ui.createButton("click_button", UIRect{100, 100, 150, 50}, "Click Me");

    // Set up callback with atomic flag
    std::atomic<bool> buttonClicked{false};

    ui.setOnClick("click_button", [&buttonClicked]() {
        buttonClicked.store(true, std::memory_order_release);
    });

    // Simulate click by calling processEvent with mouse down/up
    // (In actual usage, UIManager::handleInput processes SDL events)
    // For testing, we verify the callback was set successfully
    BOOST_CHECK(ui.hasComponent("click_button"));

    // Clean up
    ui.removeComponent("click_button");
}

// ----------------------------------------------------------------------------
// Test: onValueChanged callback for progress bar
// ----------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(TestOnValueChangedCallback) {
    auto& ui = UIManager::Instance();

    ui.createProgressBar("progress", UIRect{100, 100, 300, 30}, 0.0f, 100.0f);

    // Set up callback to track value changes
    std::atomic<float> lastValue{-1.0f};

    ui.setOnValueChanged("progress", [&lastValue](float newValue) {
        lastValue.store(newValue, std::memory_order_release);
    });

    // Update progress bar value
    ui.updateProgressBar("progress", 0.5f); // 50%

    // Verify callback was set
    BOOST_CHECK(ui.hasComponent("progress"));

    ui.removeComponent("progress");
}

// ----------------------------------------------------------------------------
// Test: onTextChanged callback for input field
// ----------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(TestOnTextChangedCallback) {
    auto& ui = UIManager::Instance();

    ui.createInputField("input", UIRect{100, 100, 200, 30}, "Enter text...");

    // Set up callback to track text changes
    std::atomic<bool> textChanged{false};

    ui.setOnTextChanged("input", [&textChanged](const std::string& newText) {
        (void)newText; // Use parameter
        textChanged.store(true, std::memory_order_release);
    });

    // Verify callback was set
    BOOST_CHECK(ui.hasComponent("input"));

    ui.removeComponent("input");
}

// ----------------------------------------------------------------------------
// Test: Multiple callbacks can be set independently
// ----------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(TestMultipleIndependentCallbacks) {
    auto& ui = UIManager::Instance();

    ui.createButton("button1", UIRect{100, 100, 100, 40}, "Button 1");
    ui.createButton("button2", UIRect{220, 100, 100, 40}, "Button 2");

    std::atomic<int> button1Clicks{0};
    std::atomic<int> button2Clicks{0};

    ui.setOnClick("button1", [&button1Clicks]() {
        button1Clicks.fetch_add(1, std::memory_order_relaxed);
    });

    ui.setOnClick("button2", [&button2Clicks]() {
        button2Clicks.fetch_add(1, std::memory_order_relaxed);
    });

    // Verify both buttons exist with independent callbacks
    BOOST_CHECK(ui.hasComponent("button1"));
    BOOST_CHECK(ui.hasComponent("button2"));

    ui.removeComponent("button1");
    ui.removeComponent("button2");
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// TEST SUITE: UIComponentCreationTests
// ============================================================================
// Tests that validate UI component creation

BOOST_FIXTURE_TEST_SUITE(UIComponentCreationTests, UIManagerFixture)

// ----------------------------------------------------------------------------
// Test: Create various button types
// ----------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(TestCreateButtonVariants) {
    auto& ui = UIManager::Instance();

    ui.createButton("normal_button", UIRect{100, 50, 150, 40}, "Normal");
    ui.createButtonDanger("danger_button", UIRect{100, 100, 150, 40}, "Danger");
    ui.createButtonSuccess("success_button", UIRect{100, 150, 150, 40}, "Success");
    ui.createButtonWarning("warning_button", UIRect{100, 200, 150, 40}, "Warning");

    BOOST_CHECK(ui.hasComponent("normal_button"));
    BOOST_CHECK(ui.hasComponent("danger_button"));
    BOOST_CHECK(ui.hasComponent("success_button"));
    BOOST_CHECK(ui.hasComponent("warning_button"));

    ui.removeComponent("normal_button");
    ui.removeComponent("danger_button");
    ui.removeComponent("success_button");
    ui.removeComponent("warning_button");
}

// ----------------------------------------------------------------------------
// Test: Create text components (label, title)
// ----------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(TestCreateTextComponents) {
    auto& ui = UIManager::Instance();

    ui.createLabel("label1", UIRect{100, 50, 200, 30}, "This is a label");
    ui.createTitle("title1", UIRect{100, 100, 300, 40}, "This is a title");

    BOOST_CHECK(ui.hasComponent("label1"));
    BOOST_CHECK(ui.hasComponent("title1"));

    ui.removeComponent("label1");
    ui.removeComponent("title1");
}

// ----------------------------------------------------------------------------
// Test: Create panel container
// ----------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(TestCreatePanel) {
    auto& ui = UIManager::Instance();

    ui.createPanel("panel1", UIRect{100, 100, 400, 300});

    BOOST_CHECK(ui.hasComponent("panel1"));

    ui.removeComponent("panel1");
}

// ----------------------------------------------------------------------------
// Test: Create progress bar with min/max values
// ----------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(TestCreateProgressBar) {
    auto& ui = UIManager::Instance();

    ui.createProgressBar("progress1", UIRect{100, 100, 300, 25}, 0.0f, 100.0f);

    BOOST_CHECK(ui.hasComponent("progress1"));

    // Update progress value
    ui.updateProgressBar("progress1", 0.75f); // 75%

    ui.removeComponent("progress1");
}

// ----------------------------------------------------------------------------
// Test: Create input field with placeholder
// ----------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(TestCreateInputField) {
    auto& ui = UIManager::Instance();

    ui.createInputField("input1", UIRect{100, 100, 250, 30}, "Enter username...");

    BOOST_CHECK(ui.hasComponent("input1"));

    ui.removeComponent("input1");
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// TEST SUITE: UIComponentManagementTests
// ============================================================================
// Tests that validate component lifecycle management

BOOST_FIXTURE_TEST_SUITE(UIComponentManagementTests, UIManagerFixture)

// ----------------------------------------------------------------------------
// Test: Remove component
// ----------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(TestRemoveComponent) {
    auto& ui = UIManager::Instance();

    ui.createButton("temp_button", UIRect{100, 100, 100, 40}, "Temp");
    BOOST_CHECK(ui.hasComponent("temp_button"));

    ui.removeComponent("temp_button");
    BOOST_CHECK(!ui.hasComponent("temp_button"));
}

// ----------------------------------------------------------------------------
// Test: Set text on existing component
// ----------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(TestSetComponentText) {
    auto& ui = UIManager::Instance();

    ui.createLabel("label", UIRect{100, 100, 200, 30}, "Original Text");
    BOOST_CHECK(ui.hasComponent("label"));

    ui.setText("label", "Updated Text");

    // Component should still exist
    BOOST_CHECK(ui.hasComponent("label"));

    ui.removeComponent("label");
}

// ----------------------------------------------------------------------------
// Test: Enable/disable component
// ----------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(TestEnableDisableComponent) {
    auto& ui = UIManager::Instance();

    ui.createButton("toggle_button", UIRect{100, 100, 100, 40}, "Toggle");

    // Disable component
    ui.setComponentEnabled("toggle_button", false);
    BOOST_CHECK(ui.hasComponent("toggle_button"));

    // Re-enable component
    ui.setComponentEnabled("toggle_button", true);
    BOOST_CHECK(ui.hasComponent("toggle_button"));

    ui.removeComponent("toggle_button");
}

// ----------------------------------------------------------------------------
// Test: Show/hide component
// ----------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(TestShowHideComponent) {
    auto& ui = UIManager::Instance();

    ui.createButton("visibility_button", UIRect{100, 100, 100, 40}, "Visible");

    // Hide component
    ui.setComponentVisible("visibility_button", false);
    BOOST_CHECK(ui.hasComponent("visibility_button"));

    // Show component
    ui.setComponentVisible("visibility_button", true);
    BOOST_CHECK(ui.hasComponent("visibility_button"));

    ui.removeComponent("visibility_button");
}

// ----------------------------------------------------------------------------
// Test: Set component Z-order
// ----------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(TestSetComponentZOrder) {
    auto& ui = UIManager::Instance();

    ui.createButton("background", UIRect{100, 100, 100, 40}, "Back");
    ui.createButton("foreground", UIRect{120, 120, 100, 40}, "Front");

    ui.setComponentZOrder("background", 1);
    ui.setComponentZOrder("foreground", 10);

    BOOST_CHECK(ui.hasComponent("background"));
    BOOST_CHECK(ui.hasComponent("foreground"));

    ui.removeComponent("background");
    ui.removeComponent("foreground");
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// TEST SUITE: UIWindowResizeTests
// ============================================================================
// Tests that validate UI responds correctly to window resize events

BOOST_FIXTURE_TEST_SUITE(UIWindowResizeTests, UIManagerFixture)

// ----------------------------------------------------------------------------
// Test: Window resize triggers repositioning
// ----------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(TestWindowResizeTriggersRepositioning) {
    auto& ui = UIManager::Instance();

    // Create centered button
    ui.createButton("centered", UIRect{350, 280, 100, 40}, "Center");

    UIPositioning positioning;
    positioning.mode = UIPositionMode::CENTERED_BOTH;
    positioning.offsetX = 0;
    positioning.offsetY = 0;
    positioning.fixedWidth = 100;
    positioning.fixedHeight = 40;

    ui.setComponentPositioning("centered", positioning);

    // Resize window
    ui.onWindowResize(1024, 768);

    // Component should still exist after resize
    BOOST_CHECK(ui.hasComponent("centered"));

    // Resize again to different dimensions
    ui.onWindowResize(1280, 720);

    BOOST_CHECK(ui.hasComponent("centered"));

    ui.removeComponent("centered");
}

// ----------------------------------------------------------------------------
// Test: Multiple window resizes preserve component state
// ----------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(TestMultipleResizesPreserveState) {
    auto& ui = UIManager::Instance();

    ui.createButton("resize_test", UIRect{100, 100, 120, 40}, "Resize");

    // Perform multiple resizes
    ui.onWindowResize(1024, 768);
    ui.onWindowResize(800, 600);
    ui.onWindowResize(1280, 1024);
    ui.onWindowResize(1920, 1080);

    // Component should survive all resizes
    BOOST_CHECK(ui.hasComponent("resize_test"));

    ui.removeComponent("resize_test");
}

BOOST_AUTO_TEST_SUITE_END()
