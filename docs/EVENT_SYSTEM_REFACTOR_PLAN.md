# Event System Refactoring Plan

This document outlines the plan to refactor the event system to use world coordinates for all world-related actions, while ensuring the UI system continues to operate exclusively in screen coordinates.

## Core Principle

A mouse click will be processed in one of two ways:
1.  If the click is on a UI element, it's a **UI event** and will be handled by the `UIManager`.
2.  If the click is *not* on a UI element, it's a **World event**, and its screen coordinates will be converted to world coordinates before being processed.

---

## Phase 1: Investigation and Discovery (Read-Only)

Before any code is modified, a comprehensive analysis of the existing engine and SDL3 capabilities will be performed.

- [ ] **Analyze Existing Codebase for Coordinate Utilities:**
    - [ ] Read `src/utils/Camera.cpp` and `include/utils/Camera.hpp` to check for any existing screen-to-world or world-to-screen conversion methods.
    - [ ] Read `src/managers/UIManager.cpp` and `include/managers/UIManager.hpp` to identify how it currently handles input and if it has a function to check if a click is within a UI element's bounds.
    - [ ] Search the entire codebase for patterns like `screenToWorld`, `worldPos`, `GetMouse`, and `SDL_PointInRect` to find any other relevant code.

- [ ] **Analyze Existing Architectural Flow:**
    - [ ] Review the `handleInput()` methods in the primary game states (`src/gameStates/GamePlayState.cpp`, `src/gameStates/UIDemoState.cpp`, etc.) to understand the exact sequence of input processing.

- [ ] **Consult SDL3 Documentation for Helper Functions:**
    - [ ] Investigate SDL3's capabilities for coordinate transformation, specifically looking for functions like `SDL_RenderCoordinatesFromWindow` to simplify screen-to-world calculations.

---

## Phase 2: Proposed Implementation

Based on the findings from Phase 1, the following implementation will be carried out.

- [ ] **Enhance or Create the Coordinate Conversion Utility:**
    - [ ] **If a conversion utility exists in `Camera.cpp`:** Enhance it to ensure it's accurate and uses any relevant SDL3 helper functions.
    - [ ] **If no utility exists:** Create a new `screenToWorld(Vector2D screenCoords)` method in the `Camera` class.

- [ ] **Enhance or Create the UI Click Detection Utility:**
    - [ ] **If `UIManager.cpp` already has a way to check for clicks:** Ensure it's robust and use it as the primary gatekeeper.
    - [ ] **If not:** Add a new `bool isClickOnUI(Vector2D screenCoords)` method to the `UIManager`.

- [ ] **Refactor Game State Input Handling:**
    - [ ] In the `handleInput()` method of the active game states, implement the following logic for mouse clicks:
        1.  Get mouse coordinates from `InputManager`.
        2.  Call `UIManager::isClickOnUI()`.
        3.  If `true`, let the UI Manager handle it.
        4.  If `false`, call `Camera::screenToWorld()` to get world coordinates.
        5.  Dispatch world-related events using the new world coordinates.
