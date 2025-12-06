/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#define BOOST_TEST_MODULE CameraTests
#include <boost/test/unit_test.hpp>

#include <memory>
#include <cmath>
#include <limits>

#include "utils/Camera.hpp"
#include "utils/Vector2D.hpp"

using namespace HammerEngine;

// Test tolerance for floating-point comparisons
constexpr float EPSILON = 0.001f;

// Helper to check if two floats are approximately equal
bool approxEqual(float a, float b, float epsilon = EPSILON) {
    return std::abs(a - b) < epsilon;
}

// Helper to check if a float is finite (not NaN or infinity)
bool isFinite(float value) {
    return std::isfinite(value);
}

// ============================================================================
// COORDINATE TRANSFORM TESTS
// Critical: "All rendering depends on correct camera math" - agent report
// ============================================================================

BOOST_AUTO_TEST_SUITE(CoordinateTransformTests)

BOOST_AUTO_TEST_CASE(TestWorldToScreenBasicTransform) {
    // Camera at origin, 800x600 viewport, 1.0 zoom
    Camera camera(0.0f, 0.0f, 800.0f, 600.0f);

    float screenX, screenY;

    // World point at camera center (0,0) should map to screen center (400, 300)
    camera.worldToScreen(0.0f, 0.0f, screenX, screenY);
    BOOST_CHECK(approxEqual(screenX, 400.0f));
    BOOST_CHECK(approxEqual(screenY, 300.0f));

    // World point 100 units right of camera center
    camera.worldToScreen(100.0f, 0.0f, screenX, screenY);
    BOOST_CHECK(approxEqual(screenX, 500.0f));
    BOOST_CHECK(approxEqual(screenY, 300.0f));

    // World point 50 units down from camera center
    camera.worldToScreen(0.0f, 50.0f, screenX, screenY);
    BOOST_CHECK(approxEqual(screenX, 400.0f));
    BOOST_CHECK(approxEqual(screenY, 350.0f));
}

BOOST_AUTO_TEST_CASE(TestScreenToWorldBasicTransform) {
    // Camera at origin, 800x600 viewport, 1.0 zoom
    Camera camera(0.0f, 0.0f, 800.0f, 600.0f);

    float worldX, worldY;

    // Screen center (400, 300) should map to world camera center (0, 0)
    camera.screenToWorld(400.0f, 300.0f, worldX, worldY);
    BOOST_CHECK(approxEqual(worldX, 0.0f));
    BOOST_CHECK(approxEqual(worldY, 0.0f));

    // Top-left screen corner should map to world space
    camera.screenToWorld(0.0f, 0.0f, worldX, worldY);
    BOOST_CHECK(approxEqual(worldX, -400.0f));
    BOOST_CHECK(approxEqual(worldY, -300.0f));

    // Bottom-right screen corner
    camera.screenToWorld(800.0f, 600.0f, worldX, worldY);
    BOOST_CHECK(approxEqual(worldX, 400.0f));
    BOOST_CHECK(approxEqual(worldY, 300.0f));
}

BOOST_AUTO_TEST_CASE(TestRoundTripTransformAccuracy) {
    // Test that world→screen→world preserves original coordinates
    Camera camera(500.0f, 500.0f, 1920.0f, 1080.0f);

    // Test multiple world points
    std::vector<Vector2D> testPoints = {
        Vector2D(500.0f, 500.0f),   // Camera center
        Vector2D(0.0f, 0.0f),       // Origin
        Vector2D(1000.0f, 1000.0f), // Far point
        Vector2D(-500.0f, -500.0f), // Negative coordinates
        Vector2D(123.456f, 789.012f) // Arbitrary point
    };

    for (const auto& worldPoint : testPoints) {
        float screenX, screenY;
        camera.worldToScreen(worldPoint.getX(), worldPoint.getY(), screenX, screenY);

        float worldX, worldY;
        camera.screenToWorld(screenX, screenY, worldX, worldY);

        // Should get back original world coordinates
        BOOST_CHECK(approxEqual(worldX, worldPoint.getX()));
        BOOST_CHECK(approxEqual(worldY, worldPoint.getY()));
    }
}

BOOST_AUTO_TEST_CASE(TestTransformsWithDifferentCameraPositions) {
    // Test that camera position affects transforms correctly

    // Camera at (100, 200)
    Camera camera1(100.0f, 200.0f, 800.0f, 600.0f);
    float screenX, screenY;

    // World point at camera center should map to screen center
    camera1.worldToScreen(100.0f, 200.0f, screenX, screenY);
    BOOST_CHECK(approxEqual(screenX, 400.0f));
    BOOST_CHECK(approxEqual(screenY, 300.0f));

    // Camera at (-500, -500)
    Camera camera2(-500.0f, -500.0f, 800.0f, 600.0f);
    camera2.worldToScreen(-500.0f, -500.0f, screenX, screenY);
    BOOST_CHECK(approxEqual(screenX, 400.0f));
    BOOST_CHECK(approxEqual(screenY, 300.0f));
}

BOOST_AUTO_TEST_CASE(TestVector2DTransformMethods) {
    // Test the Vector2D overloads
    Camera camera(0.0f, 0.0f, 800.0f, 600.0f);

    Vector2D worldPoint(100.0f, 50.0f);
    Vector2D screenPoint = camera.worldToScreen(worldPoint);

    // Verify screen coordinates
    BOOST_CHECK(approxEqual(screenPoint.getX(), 500.0f));
    BOOST_CHECK(approxEqual(screenPoint.getY(), 350.0f));

    // Convert back to world
    Vector2D worldAgain = camera.screenToWorld(screenPoint);
    BOOST_CHECK(approxEqual(worldAgain.getX(), worldPoint.getX()));
    BOOST_CHECK(approxEqual(worldAgain.getY(), worldPoint.getY()));
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// ZOOM TESTS
// Critical: Must not produce NaN or infinity
// ============================================================================

BOOST_AUTO_TEST_SUITE(ZoomTests)

BOOST_AUTO_TEST_CASE(TestZoomInBounds) {
    // Default config has zoom levels: {1.0f, 1.5f, 2.0f}
    Camera camera;

    BOOST_CHECK_EQUAL(camera.getZoomLevel(), 0);
    BOOST_CHECK(approxEqual(camera.getZoom(), 1.0f));

    // Zoom in to level 1
    camera.zoomIn();
    BOOST_CHECK_EQUAL(camera.getZoomLevel(), 1);
    BOOST_CHECK(approxEqual(camera.getZoom(), 1.5f));

    // Zoom in to level 2 (max)
    camera.zoomIn();
    BOOST_CHECK_EQUAL(camera.getZoomLevel(), 2);
    BOOST_CHECK(approxEqual(camera.getZoom(), 2.0f));

    // Attempt to zoom beyond max - should stay at max
    camera.zoomIn();
    BOOST_CHECK_EQUAL(camera.getZoomLevel(), 2);
    BOOST_CHECK(approxEqual(camera.getZoom(), 2.0f));
}

BOOST_AUTO_TEST_CASE(TestZoomOutBounds) {
    Camera camera;

    // Zoom to max first
    camera.zoomIn();
    camera.zoomIn();
    BOOST_CHECK_EQUAL(camera.getZoomLevel(), 2);

    // Zoom out to level 1
    camera.zoomOut();
    BOOST_CHECK_EQUAL(camera.getZoomLevel(), 1);
    BOOST_CHECK(approxEqual(camera.getZoom(), 1.5f));

    // Zoom out to level 0 (min)
    camera.zoomOut();
    BOOST_CHECK_EQUAL(camera.getZoomLevel(), 0);
    BOOST_CHECK(approxEqual(camera.getZoom(), 1.0f));

    // Attempt to zoom below min - should stay at min
    camera.zoomOut();
    BOOST_CHECK_EQUAL(camera.getZoomLevel(), 0);
    BOOST_CHECK(approxEqual(camera.getZoom(), 1.0f));
}

BOOST_AUTO_TEST_CASE(TestSetZoomLevelValid) {
    Camera camera;

    // Set to level 2
    BOOST_CHECK(camera.setZoomLevel(2));
    BOOST_CHECK_EQUAL(camera.getZoomLevel(), 2);
    BOOST_CHECK(approxEqual(camera.getZoom(), 2.0f));

    // Set to level 0
    BOOST_CHECK(camera.setZoomLevel(0));
    BOOST_CHECK_EQUAL(camera.getZoomLevel(), 0);
    BOOST_CHECK(approxEqual(camera.getZoom(), 1.0f));

    // Set to level 1
    BOOST_CHECK(camera.setZoomLevel(1));
    BOOST_CHECK_EQUAL(camera.getZoomLevel(), 1);
    BOOST_CHECK(approxEqual(camera.getZoom(), 1.5f));
}

BOOST_AUTO_TEST_CASE(TestSetZoomLevelInvalid) {
    Camera camera;

    // Attempt to set invalid levels
    BOOST_CHECK(!camera.setZoomLevel(-1));
    BOOST_CHECK(!camera.setZoomLevel(3));
    BOOST_CHECK(!camera.setZoomLevel(100));

    // Camera should remain at default level 0
    BOOST_CHECK_EQUAL(camera.getZoomLevel(), 0);
    BOOST_CHECK(approxEqual(camera.getZoom(), 1.0f));
}

BOOST_AUTO_TEST_CASE(TestZoomEffectOnViewRect) {
    Camera camera(0.0f, 0.0f, 800.0f, 600.0f);

    // At 1.0x zoom, view rect should be full viewport
    auto viewRect1x = camera.getViewRect();
    BOOST_CHECK(approxEqual(viewRect1x.width, 800.0f));
    BOOST_CHECK(approxEqual(viewRect1x.height, 600.0f));

    // At 2.0x zoom, view rect should be half size (see less world)
    camera.setZoomLevel(2); // 2.0x zoom
    auto viewRect2x = camera.getViewRect();
    BOOST_CHECK(approxEqual(viewRect2x.width, 400.0f));
    BOOST_CHECK(approxEqual(viewRect2x.height, 300.0f));

    // View rect center should still be at camera position
    BOOST_CHECK(approxEqual(viewRect2x.centerX(), 0.0f));
    BOOST_CHECK(approxEqual(viewRect2x.centerY(), 0.0f));
}

BOOST_AUTO_TEST_CASE(TestZoomNoNaNOrInfinity) {
    Camera camera(0.0f, 0.0f, 800.0f, 600.0f);

    // Test all zoom levels for finite values
    for (int i = 0; i < camera.getNumZoomLevels(); ++i) {
        camera.setZoomLevel(i);

        float zoom = camera.getZoom();
        BOOST_CHECK(isFinite(zoom));
        BOOST_CHECK_GT(zoom, 0.0f);

        auto viewRect = camera.getViewRect();
        BOOST_CHECK(isFinite(viewRect.x));
        BOOST_CHECK(isFinite(viewRect.y));
        BOOST_CHECK(isFinite(viewRect.width));
        BOOST_CHECK(isFinite(viewRect.height));
        BOOST_CHECK_GT(viewRect.width, 0.0f);
        BOOST_CHECK_GT(viewRect.height, 0.0f);
    }
}

BOOST_AUTO_TEST_CASE(TestZoomEffectOnCoordinateTransforms) {
    Camera camera(0.0f, 0.0f, 800.0f, 600.0f);

    // At 1.0x zoom
    float screenX1, screenY1;
    camera.worldToScreen(100.0f, 100.0f, screenX1, screenY1);

    // At 2.0x zoom
    camera.setZoomLevel(2);
    float screenX2, screenY2;
    camera.worldToScreen(100.0f, 100.0f, screenX2, screenY2);

    // Zoom affects the render offset calculation (viewport size in world coords changes)
    // At higher zoom, the visible world area is smaller, changing the camera offset
    BOOST_CHECK_NE(screenX1, screenX2);
    BOOST_CHECK_NE(screenY1, screenY2);

    // All values should be finite
    BOOST_CHECK(isFinite(screenX1));
    BOOST_CHECK(isFinite(screenY1));
    BOOST_CHECK(isFinite(screenX2));
    BOOST_CHECK(isFinite(screenY2));
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// BOUNDS CLAMPING TESTS
// Critical: Prevents out-of-bounds spatial queries
// ============================================================================

BOOST_AUTO_TEST_SUITE(BoundsClampingTests)

BOOST_AUTO_TEST_CASE(TestCameraStaysWithinWorldBounds) {
    Camera camera(0.0f, 0.0f, 800.0f, 600.0f);

    // Set world bounds: 0,0 to 2000,2000
    camera.setWorldBounds(0.0f, 0.0f, 2000.0f, 2000.0f);

    // Try to move camera beyond max bounds
    camera.setPosition(3000.0f, 3000.0f);
    camera.update(0.016f); // Trigger clamping

    // Camera should be clamped (accounting for half viewport)
    // Viewport is 800x600, so halfWidth=400, halfHeight=300
    // Max X position = worldMaxX - halfViewportWidth = 2000 - 400 = 1600
    // Max Y position = worldMaxY - halfViewportHeight = 2000 - 300 = 1700
    BOOST_CHECK_LE(camera.getX(), 1600.0f + EPSILON);
    BOOST_CHECK_LE(camera.getY(), 1700.0f + EPSILON);

    // Try to move camera before min bounds
    camera.setPosition(-1000.0f, -1000.0f);
    camera.update(0.016f);

    // Camera should be clamped
    // Min X position = worldMinX + halfViewportWidth = 0 + 400 = 400
    // Min Y position = worldMinY + halfViewportHeight = 0 + 300 = 300
    BOOST_CHECK_GE(camera.getX(), 400.0f - EPSILON);
    BOOST_CHECK_GE(camera.getY(), 300.0f - EPSILON);
}

BOOST_AUTO_TEST_CASE(TestClampingWithZoom) {
    Camera camera(0.0f, 0.0f, 800.0f, 600.0f);
    camera.setWorldBounds(0.0f, 0.0f, 2000.0f, 2000.0f);

    // At 2.0x zoom, viewport is effectively smaller (400x300)
    camera.setZoomLevel(2);

    // Try to move beyond bounds
    camera.setPosition(3000.0f, 3000.0f);
    camera.update(0.016f);

    // At 2x zoom, effective halfViewport is (width/zoom)/2 and (height/zoom)/2
    // halfViewportWidth = (800/2.0)/2 = 200
    // halfViewportHeight = (600/2.0)/2 = 150
    // Max X position = 2000 - 200 = 1800
    // Max Y position = 2000 - 150 = 1850
    BOOST_CHECK_LE(camera.getX(), 1800.0f + EPSILON);
    BOOST_CHECK_LE(camera.getY(), 1850.0f + EPSILON);

    // Try to move before min
    camera.setPosition(-1000.0f, -1000.0f);
    camera.update(0.016f);

    // Min X position = 0 + 200 = 200
    // Min Y position = 0 + 150 = 150
    BOOST_CHECK_GE(camera.getX(), 200.0f - EPSILON);
    BOOST_CHECK_GE(camera.getY(), 150.0f - EPSILON);
}

BOOST_AUTO_TEST_CASE(TestClampingWhenWorldSmallerThanViewport) {
    Camera camera(0.0f, 0.0f, 800.0f, 600.0f);

    // World smaller than viewport: 0,0 to 400,300
    camera.setWorldBounds(0.0f, 0.0f, 400.0f, 300.0f);

    // Try to move camera
    camera.setPosition(1000.0f, 1000.0f);
    camera.update(0.016f);

    // Camera should be centered on the small world
    // Center X = (0 + 400) / 2 = 200
    // Center Y = (0 + 300) / 2 = 150
    BOOST_CHECK(approxEqual(camera.getX(), 200.0f));
    BOOST_CHECK(approxEqual(camera.getY(), 150.0f));
}

BOOST_AUTO_TEST_CASE(TestClampingDisabled) {
    Camera::Config config;
    config.clampToWorldBounds = false;

    Camera camera(config);
    camera.setViewport(800.0f, 600.0f);
    camera.setWorldBounds(0.0f, 0.0f, 2000.0f, 2000.0f);

    // Try to move beyond bounds
    camera.setPosition(5000.0f, 5000.0f);
    camera.update(0.016f);

    // With clamping disabled, position should not be constrained
    BOOST_CHECK(approxEqual(camera.getX(), 5000.0f));
    BOOST_CHECK(approxEqual(camera.getY(), 5000.0f));
}

BOOST_AUTO_TEST_CASE(TestBoundsValidation) {
    Camera camera;

    // Set valid bounds
    camera.setWorldBounds(0.0f, 0.0f, 1000.0f, 1000.0f);
    auto bounds = camera.getWorldBounds();
    BOOST_CHECK(approxEqual(bounds.minX, 0.0f));
    BOOST_CHECK(approxEqual(bounds.maxX, 1000.0f));

    // Invalid bounds should be rejected (max <= min)
    camera.setWorldBounds(1000.0f, 1000.0f, 0.0f, 0.0f);
    bounds = camera.getWorldBounds();
    // Bounds should remain unchanged
    BOOST_CHECK(approxEqual(bounds.minX, 0.0f));
    BOOST_CHECK(approxEqual(bounds.maxX, 1000.0f));
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// VIEWPORT TESTS
// ============================================================================

BOOST_AUTO_TEST_SUITE(ViewportTests)

BOOST_AUTO_TEST_CASE(TestViewportUpdate) {
    Camera camera(0.0f, 0.0f, 800.0f, 600.0f);

    auto viewport = camera.getViewport();
    BOOST_CHECK(approxEqual(viewport.width, 800.0f));
    BOOST_CHECK(approxEqual(viewport.height, 600.0f));

    // Update viewport
    camera.setViewport(1920.0f, 1080.0f);
    viewport = camera.getViewport();
    BOOST_CHECK(approxEqual(viewport.width, 1920.0f));
    BOOST_CHECK(approxEqual(viewport.height, 1080.0f));
}

BOOST_AUTO_TEST_CASE(TestViewportValidation) {
    Camera camera(0.0f, 0.0f, 800.0f, 600.0f);

    // Try to set invalid viewports (negative or zero dimensions)
    camera.setViewport(-100.0f, 600.0f);
    auto viewport = camera.getViewport();
    // Should remain unchanged
    BOOST_CHECK(approxEqual(viewport.width, 800.0f));
    BOOST_CHECK(approxEqual(viewport.height, 600.0f));

    camera.setViewport(800.0f, 0.0f);
    viewport = camera.getViewport();
    // Should remain unchanged
    BOOST_CHECK(approxEqual(viewport.width, 800.0f));
    BOOST_CHECK(approxEqual(viewport.height, 600.0f));
}

BOOST_AUTO_TEST_CASE(TestGetViewRectDifferentViewports) {
    Camera camera(0.0f, 0.0f, 800.0f, 600.0f);

    auto viewRect1 = camera.getViewRect();
    BOOST_CHECK(approxEqual(viewRect1.width, 800.0f));
    BOOST_CHECK(approxEqual(viewRect1.height, 600.0f));

    // Change viewport
    camera.setViewport(1920.0f, 1080.0f);
    auto viewRect2 = camera.getViewRect();
    BOOST_CHECK(approxEqual(viewRect2.width, 1920.0f));
    BOOST_CHECK(approxEqual(viewRect2.height, 1080.0f));

    // View rect should be centered on camera position
    BOOST_CHECK(approxEqual(viewRect2.centerX(), 0.0f));
    BOOST_CHECK(approxEqual(viewRect2.centerY(), 0.0f));
}

BOOST_AUTO_TEST_CASE(TestViewportHelperMethods) {
    Camera::Viewport viewport{1920.0f, 1080.0f};

    BOOST_CHECK(approxEqual(viewport.halfWidth(), 960.0f));
    BOOST_CHECK(approxEqual(viewport.halfHeight(), 540.0f));
    BOOST_CHECK(viewport.isValid());

    Camera::Viewport invalidViewport{-100.0f, 600.0f};
    BOOST_CHECK(!invalidViewport.isValid());
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// CONFIG VALIDATION TESTS
// ============================================================================

BOOST_AUTO_TEST_SUITE(ConfigValidationTests)

BOOST_AUTO_TEST_CASE(TestValidConfigAccepted) {
    Camera::Config config;
    config.smoothTime = 0.15f;
    config.deadZoneRadius = 32.0f;
    config.maxSpeed = 1000.0f;
    config.clampToWorldBounds = true;
    config.zoomLevels = {1.0f, 1.5f, 2.0f};
    config.defaultZoomLevel = 0;

    BOOST_CHECK(config.isValid());

    Camera camera(config);
    auto retrievedConfig = camera.getConfig();
    BOOST_CHECK(approxEqual(retrievedConfig.smoothTime, 0.15f));
    BOOST_CHECK(approxEqual(retrievedConfig.deadZoneRadius, 32.0f));
}

BOOST_AUTO_TEST_CASE(TestInvalidConfigRejected) {
    Camera camera;

    // Negative smooth time
    Camera::Config config1;
    config1.smoothTime = -1.0f;
    BOOST_CHECK(!config1.isValid());
    BOOST_CHECK(!camera.setConfig(config1));

    // Negative max speed
    Camera::Config config2;
    config2.maxSpeed = -100.0f;
    BOOST_CHECK(!config2.isValid());
    BOOST_CHECK(!camera.setConfig(config2));

    // Empty zoom levels
    Camera::Config config3;
    config3.zoomLevels.clear();
    BOOST_CHECK(!config3.isValid());
    BOOST_CHECK(!camera.setConfig(config3));

    // Negative zoom level
    Camera::Config config4;
    config4.zoomLevels = {-1.0f, 1.0f};
    BOOST_CHECK(!config4.isValid());
    BOOST_CHECK(!camera.setConfig(config4));

    // Invalid default zoom level index
    Camera::Config config5;
    config5.zoomLevels = {1.0f, 1.5f};
    config5.defaultZoomLevel = 5; // Out of range
    BOOST_CHECK(!config5.isValid());
    BOOST_CHECK(!camera.setConfig(config5));
}

BOOST_AUTO_TEST_CASE(TestBoundsConfigValidation) {
    Camera::Bounds validBounds{0.0f, 0.0f, 1000.0f, 1000.0f};
    BOOST_CHECK(validBounds.isValid());

    Camera::Bounds invalidBounds1{1000.0f, 0.0f, 0.0f, 1000.0f}; // maxX <= minX
    BOOST_CHECK(!invalidBounds1.isValid());

    Camera::Bounds invalidBounds2{0.0f, 1000.0f, 1000.0f, 0.0f}; // maxY <= minY
    BOOST_CHECK(!invalidBounds2.isValid());
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// VISIBILITY TESTS
// ============================================================================

BOOST_AUTO_TEST_SUITE(VisibilityTests)

BOOST_AUTO_TEST_CASE(TestPointVisibility) {
    Camera camera(0.0f, 0.0f, 800.0f, 600.0f);

    // Point at camera center should be visible
    BOOST_CHECK(camera.isPointVisible(0.0f, 0.0f));

    // Points within viewport should be visible
    BOOST_CHECK(camera.isPointVisible(100.0f, 100.0f));
    BOOST_CHECK(camera.isPointVisible(-100.0f, -100.0f));

    // Points outside viewport should not be visible
    // View rect extends from -400 to 400 in X, -300 to 300 in Y
    BOOST_CHECK(!camera.isPointVisible(500.0f, 0.0f));
    BOOST_CHECK(!camera.isPointVisible(0.0f, 400.0f));
    BOOST_CHECK(!camera.isPointVisible(-500.0f, 0.0f));
    BOOST_CHECK(!camera.isPointVisible(0.0f, -400.0f));
}

BOOST_AUTO_TEST_CASE(TestPointVisibilityVector2D) {
    Camera camera(0.0f, 0.0f, 800.0f, 600.0f);

    Vector2D visiblePoint(50.0f, 50.0f);
    BOOST_CHECK(camera.isPointVisible(visiblePoint));

    Vector2D invisiblePoint(1000.0f, 1000.0f);
    BOOST_CHECK(!camera.isPointVisible(invisiblePoint));
}

BOOST_AUTO_TEST_CASE(TestRectVisibility) {
    Camera camera(0.0f, 0.0f, 800.0f, 600.0f);

    // Rect fully inside viewport
    BOOST_CHECK(camera.isRectVisible(0.0f, 0.0f, 50.0f, 50.0f));

    // Rect partially overlapping viewport
    BOOST_CHECK(camera.isRectVisible(350.0f, 0.0f, 100.0f, 100.0f));

    // Rect completely outside viewport
    BOOST_CHECK(!camera.isRectVisible(1000.0f, 1000.0f, 50.0f, 50.0f));
}

BOOST_AUTO_TEST_CASE(TestVisibilityWithZoom) {
    Camera camera(0.0f, 0.0f, 800.0f, 600.0f);

    // At 1.0x zoom, point at (450, 0) is outside viewport (extends to 400)
    BOOST_CHECK(!camera.isPointVisible(450.0f, 0.0f));

    // At 0.5x zoom would show more, but we only have 1.0, 1.5, 2.0
    // At 2.0x zoom, viewport is smaller, so same point is still outside
    camera.setZoomLevel(2);
    BOOST_CHECK(!camera.isPointVisible(450.0f, 0.0f));
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// VIEW RECT TESTS
// ============================================================================

BOOST_AUTO_TEST_SUITE(ViewRectTests)

BOOST_AUTO_TEST_CASE(TestViewRectCalculation) {
    Camera camera(100.0f, 200.0f, 800.0f, 600.0f);

    auto viewRect = camera.getViewRect();

    // View rect should be centered on camera position
    BOOST_CHECK(approxEqual(viewRect.centerX(), 100.0f));
    BOOST_CHECK(approxEqual(viewRect.centerY(), 200.0f));

    // At 1.0x zoom, dimensions should match viewport
    BOOST_CHECK(approxEqual(viewRect.width, 800.0f));
    BOOST_CHECK(approxEqual(viewRect.height, 600.0f));

    // Top-left corner
    BOOST_CHECK(approxEqual(viewRect.left(), 100.0f - 400.0f));
    BOOST_CHECK(approxEqual(viewRect.top(), 200.0f - 300.0f));

    // Bottom-right corner
    BOOST_CHECK(approxEqual(viewRect.right(), 100.0f + 400.0f));
    BOOST_CHECK(approxEqual(viewRect.bottom(), 200.0f + 300.0f));
}

BOOST_AUTO_TEST_CASE(TestViewRectHelperMethods) {
    Camera camera(0.0f, 0.0f, 800.0f, 600.0f);
    auto viewRect = camera.getViewRect();

    // Test helper methods
    BOOST_CHECK(approxEqual(viewRect.left(), -400.0f));
    BOOST_CHECK(approxEqual(viewRect.right(), 400.0f));
    BOOST_CHECK(approxEqual(viewRect.top(), -300.0f));
    BOOST_CHECK(approxEqual(viewRect.bottom(), 300.0f));
    BOOST_CHECK(approxEqual(viewRect.centerX(), 0.0f));
    BOOST_CHECK(approxEqual(viewRect.centerY(), 0.0f));
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// CAMERA MODE TESTS
// ============================================================================

BOOST_AUTO_TEST_SUITE(CameraModeTests)

BOOST_AUTO_TEST_CASE(TestModeChanges) {
    Camera camera;

    // Default mode is Free
    BOOST_CHECK(camera.getMode() == Camera::Mode::Free);

    // Change to Follow mode
    camera.setMode(Camera::Mode::Follow);
    BOOST_CHECK(camera.getMode() == Camera::Mode::Follow);

    // Change to Fixed mode
    camera.setMode(Camera::Mode::Fixed);
    BOOST_CHECK(camera.getMode() == Camera::Mode::Fixed);

    // Change back to Free
    camera.setMode(Camera::Mode::Free);
    BOOST_CHECK(camera.getMode() == Camera::Mode::Free);
}

BOOST_AUTO_TEST_CASE(TestFreeModeBehavior) {
    Camera camera;
    camera.setMode(Camera::Mode::Free);

    // In Free mode, camera should not move on update
    Vector2D initialPos = camera.getPosition();
    camera.update(0.016f);

    Vector2D afterUpdate = camera.getPosition();
    BOOST_CHECK(approxEqual(afterUpdate.getX(), initialPos.getX()));
    BOOST_CHECK(approxEqual(afterUpdate.getY(), initialPos.getY()));
}

BOOST_AUTO_TEST_SUITE_END()
