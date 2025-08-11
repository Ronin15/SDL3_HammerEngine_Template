/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#define BOOST_TEST_MODULE GameStateManagerTests
#include <boost/test/unit_test.hpp>
#include "managers/GameStateManager.hpp"
#include "gameStates/GameState.hpp"
#include <memory>
#include <string>

// Mock GameState for testing
class MockGameState : public GameState {
public:
    explicit MockGameState(const std::string& name) 
        : m_name(name), m_enterCalled(false), m_exitCalled(false), 
          m_updateCalled(false), m_renderCalled(false), m_handleInputCalled(false),
          m_pauseCalled(false), m_resumeCalled(false) {}

    bool enter() override { 
        m_enterCalled = true; 
        return true; 
    }
    
    void update(float deltaTime) override { 
        m_updateCalled = true; 
        m_lastDeltaTime = deltaTime;
    }
    
    void render() override { 
        m_renderCalled = true; 
    }
    
    void handleInput() override { 
        m_handleInputCalled = true; 
    }
    
    bool exit() override { 
        m_exitCalled = true; 
        return true; 
    }
    
    void pause() override { 
        m_pauseCalled = true; 
    }
    
    void resume() override { 
        m_resumeCalled = true; 
    }
    
    std::string getName() const override { 
        return m_name; 
    }

    // Test helper methods
    bool wasEnterCalled() const { return m_enterCalled; }
    bool wasExitCalled() const { return m_exitCalled; }
    bool wasUpdateCalled() const { return m_updateCalled; }
    bool wasRenderCalled() const { return m_renderCalled; }
    bool wasHandleInputCalled() const { return m_handleInputCalled; }
    bool wasPauseCalled() const { return m_pauseCalled; }
    bool wasResumeCalled() const { return m_resumeCalled; }
    float getLastDeltaTime() const { return m_lastDeltaTime; }

    void resetFlags() {
        m_enterCalled = m_exitCalled = m_updateCalled = m_renderCalled = 
        m_handleInputCalled = m_pauseCalled = m_resumeCalled = false;
    }

private:
    std::string m_name;
    bool m_enterCalled, m_exitCalled, m_updateCalled, m_renderCalled, 
         m_handleInputCalled, m_pauseCalled, m_resumeCalled;
    float m_lastDeltaTime{0.0f};
};

struct GameStateManagerFixture {
    GameStateManager manager;
    
    GameStateManagerFixture() = default;
    ~GameStateManagerFixture() = default;
};

BOOST_FIXTURE_TEST_SUITE(GameStateManagerTestSuite, GameStateManagerFixture)

BOOST_AUTO_TEST_CASE(TestInitialState) {
    // Manager should start empty
    BOOST_CHECK(!manager.hasState("nonexistent"));
    BOOST_CHECK(manager.getState("nonexistent") == nullptr);
}

BOOST_AUTO_TEST_CASE(TestAddState) {
    auto mockState = std::make_unique<MockGameState>("TestState");
    
    // Add state
    manager.addState(std::move(mockState));
    
    // State should be registered but not active
    BOOST_CHECK(manager.hasState("TestState"));
    BOOST_CHECK(manager.getState("TestState") != nullptr);
    BOOST_CHECK_EQUAL(manager.getState("TestState")->getName(), "TestState");
}

BOOST_AUTO_TEST_CASE(TestAddDuplicateState) {
    auto mockState1 = std::make_unique<MockGameState>("TestState");
    auto mockState2 = std::make_unique<MockGameState>("TestState");
    
    // Add first state
    manager.addState(std::move(mockState1));
    
    // Adding duplicate should throw
    BOOST_CHECK_THROW(manager.addState(std::move(mockState2)), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(TestPushState) {
    auto mockState = std::make_unique<MockGameState>("TestState");
    MockGameState* statePtr = mockState.get();
    
    manager.addState(std::move(mockState));
    
    // Push state should call enter()
    manager.pushState("TestState");
    BOOST_CHECK(statePtr->wasEnterCalled());
}

BOOST_AUTO_TEST_CASE(TestPushNonexistentState) {
    // Pushing nonexistent state should not crash (logs error)
    BOOST_CHECK_NO_THROW(manager.pushState("NonexistentState"));
}

BOOST_AUTO_TEST_CASE(TestPopState) {
    auto mockState = std::make_unique<MockGameState>("TestState");
    MockGameState* statePtr = mockState.get();
    
    manager.addState(std::move(mockState));
    manager.pushState("TestState");
    
    statePtr->resetFlags();
    
    // Pop state should call exit()
    manager.popState();
    BOOST_CHECK(statePtr->wasExitCalled());
}

BOOST_AUTO_TEST_CASE(TestPopEmptyStack) {
    // Popping empty stack should not crash
    BOOST_CHECK_NO_THROW(manager.popState());
}

BOOST_AUTO_TEST_CASE(TestChangeState) {
    auto mockState1 = std::make_unique<MockGameState>("State1");
    auto mockState2 = std::make_unique<MockGameState>("State2");
    MockGameState* state1Ptr = mockState1.get();
    MockGameState* state2Ptr = mockState2.get();
    
    manager.addState(std::move(mockState1));
    manager.addState(std::move(mockState2));
    
    // Push first state
    manager.pushState("State1");
    BOOST_CHECK(state1Ptr->wasEnterCalled());
    
    state1Ptr->resetFlags();
    state2Ptr->resetFlags();
    
    // Change to second state
    manager.changeState("State2");
    
    // First state should exit, second should enter
    BOOST_CHECK(state1Ptr->wasExitCalled());
    BOOST_CHECK(state2Ptr->wasEnterCalled());
}

BOOST_AUTO_TEST_CASE(TestRequestStateChange) {
    auto mockState1 = std::make_unique<MockGameState>("State1");
    auto mockState2 = std::make_unique<MockGameState>("State2");
    MockGameState* state1Ptr = mockState1.get();
    MockGameState* state2Ptr = mockState2.get();
    
    manager.addState(std::move(mockState1));
    manager.addState(std::move(mockState2));
    
    manager.pushState("State1");
    state1Ptr->resetFlags();
    state2Ptr->resetFlags();
    
    // Request deferred state change
    manager.requestStateChange("State2");
    
    // Change should not happen immediately
    BOOST_CHECK(!state1Ptr->wasExitCalled());
    BOOST_CHECK(!state2Ptr->wasEnterCalled());
    
    // Update should process the deferred change
    manager.update(0.016f);
    
    BOOST_CHECK(state1Ptr->wasExitCalled());
    BOOST_CHECK(state2Ptr->wasEnterCalled());
}

BOOST_AUTO_TEST_CASE(TestUpdate) {
    auto mockState = std::make_unique<MockGameState>("TestState");
    MockGameState* statePtr = mockState.get();
    
    manager.addState(std::move(mockState));
    manager.pushState("TestState");
    
    statePtr->resetFlags();
    
    // Update should call update on active state
    const float deltaTime = 0.016f;
    manager.update(deltaTime);
    
    BOOST_CHECK(statePtr->wasUpdateCalled());
    BOOST_CHECK_EQUAL(statePtr->getLastDeltaTime(), deltaTime);
}

BOOST_AUTO_TEST_CASE(TestUpdateEmptyStack) {
    // Update with no active states should not crash
    BOOST_CHECK_NO_THROW(manager.update(0.016f));
}

BOOST_AUTO_TEST_CASE(TestRender) {
    auto mockState1 = std::make_unique<MockGameState>("State1");
    auto mockState2 = std::make_unique<MockGameState>("State2");
    MockGameState* state1Ptr = mockState1.get();
    MockGameState* state2Ptr = mockState2.get();
    
    manager.addState(std::move(mockState1));
    manager.addState(std::move(mockState2));
    
    // Push both states to create a stack
    manager.pushState("State1");
    manager.pushState("State2");
    
    state1Ptr->resetFlags();
    state2Ptr->resetFlags();
    
    // Render should only call render on the top (current) active state
    manager.render();
    
    BOOST_CHECK(!state1Ptr->wasRenderCalled()); // State1 is paused, should not render
    BOOST_CHECK(state2Ptr->wasRenderCalled());  // State2 is active, should render
}

BOOST_AUTO_TEST_CASE(TestRenderEmptyStack) {
    // Render with no active states should not crash
    BOOST_CHECK_NO_THROW(manager.render());
}

BOOST_AUTO_TEST_CASE(TestHandleInput) {
    auto mockState1 = std::make_unique<MockGameState>("State1");
    auto mockState2 = std::make_unique<MockGameState>("State2");
    MockGameState* state1Ptr = mockState1.get();
    MockGameState* state2Ptr = mockState2.get();
    
    manager.addState(std::move(mockState1));
    manager.addState(std::move(mockState2));
    
    // Push both states
    manager.pushState("State1");
    manager.pushState("State2");
    
    state1Ptr->resetFlags();
    state2Ptr->resetFlags();
    
    // HandleInput should only call handleInput on top state
    manager.handleInput();
    
    BOOST_CHECK(!state1Ptr->wasHandleInputCalled()); // Bottom state should not handle input
    BOOST_CHECK(state2Ptr->wasHandleInputCalled());  // Top state should handle input
}

BOOST_AUTO_TEST_CASE(TestHandleInputEmptyStack) {
    // HandleInput with no active states should not crash
    BOOST_CHECK_NO_THROW(manager.handleInput());
}

BOOST_AUTO_TEST_CASE(TestPauseResume) {
    auto mockState1 = std::make_unique<MockGameState>("State1");
    auto mockState2 = std::make_unique<MockGameState>("State2");
    MockGameState* state1Ptr = mockState1.get();
    MockGameState* state2Ptr = mockState2.get();
    
    manager.addState(std::move(mockState1));
    manager.addState(std::move(mockState2));
    
    // Push first state
    manager.pushState("State1");
    state1Ptr->resetFlags();
    
    // Push second state should pause first
    manager.pushState("State2");
    BOOST_CHECK(state1Ptr->wasPauseCalled());
    BOOST_CHECK(state2Ptr->wasEnterCalled());
    
    state1Ptr->resetFlags();
    state2Ptr->resetFlags();
    
    // Pop second state should resume first
    manager.popState();
    BOOST_CHECK(state2Ptr->wasExitCalled());
    BOOST_CHECK(state1Ptr->wasResumeCalled());
}

BOOST_AUTO_TEST_CASE(TestRemoveState) {
    auto mockState1 = std::make_unique<MockGameState>("State1");
    auto mockState2 = std::make_unique<MockGameState>("State2");
    
    manager.addState(std::move(mockState1));
    manager.addState(std::move(mockState2));
    
    // Push both states
    manager.pushState("State1");
    manager.pushState("State2");
    
    // Get the shared pointers from the manager and cast them to MockGameState
    auto state1Shared = std::dynamic_pointer_cast<MockGameState>(manager.getState("State1"));
    auto state2Shared = std::dynamic_pointer_cast<MockGameState>(manager.getState("State2"));
    
    BOOST_REQUIRE(state1Shared != nullptr);
    BOOST_REQUIRE(state2Shared != nullptr);
    
    state1Shared->resetFlags();
    state2Shared->resetFlags();
    
    // Remove active state
    manager.removeState("State2");
    
    // State2 should exit, State1 should resume
    BOOST_CHECK(state2Shared->wasExitCalled());
    BOOST_CHECK(state1Shared->wasResumeCalled());
    
    // State should no longer be registered
    BOOST_CHECK(!manager.hasState("State2"));
    BOOST_CHECK(manager.getState("State2") == nullptr);
}

BOOST_AUTO_TEST_CASE(TestRemoveNonexistentState) {
    // Removing nonexistent state should not crash
    BOOST_CHECK_NO_THROW(manager.removeState("NonexistentState"));
}

BOOST_AUTO_TEST_CASE(TestClearAllStates) {
    auto mockState1 = std::make_unique<MockGameState>("State1");
    auto mockState2 = std::make_unique<MockGameState>("State2");
    
    manager.addState(std::move(mockState1));
    manager.addState(std::move(mockState2));
    
    // Push both states
    manager.pushState("State1");
    manager.pushState("State2");
    
    // Get the shared pointers from the manager and cast them to MockGameState
    auto state1Shared = std::dynamic_pointer_cast<MockGameState>(manager.getState("State1"));
    auto state2Shared = std::dynamic_pointer_cast<MockGameState>(manager.getState("State2"));
    
    BOOST_REQUIRE(state1Shared != nullptr);
    BOOST_REQUIRE(state2Shared != nullptr);
    
    state1Shared->resetFlags();
    state2Shared->resetFlags();
    
    // Clear all states
    manager.clearAllStates();
    
    // Both states should exit
    BOOST_CHECK(state1Shared->wasExitCalled());
    BOOST_CHECK(state2Shared->wasExitCalled());
    
    // States should no longer be registered
    BOOST_CHECK(!manager.hasState("State1"));
    BOOST_CHECK(!manager.hasState("State2"));
}

BOOST_AUTO_TEST_CASE(TestStateStackBehavior) {
    auto mockState1 = std::make_unique<MockGameState>("State1");
    auto mockState2 = std::make_unique<MockGameState>("State2");
    auto mockState3 = std::make_unique<MockGameState>("State3");
    MockGameState* state1Ptr = mockState1.get();
    MockGameState* state2Ptr = mockState2.get();
    MockGameState* state3Ptr = mockState3.get();
    
    manager.addState(std::move(mockState1));
    manager.addState(std::move(mockState2));
    manager.addState(std::move(mockState3));
    
    // Push states to create a stack: State1 -> State2 -> State3
    manager.pushState("State1");
    manager.pushState("State2");
    manager.pushState("State3");
    
    // Reset flags
    state1Ptr->resetFlags();
    state2Ptr->resetFlags();
    state3Ptr->resetFlags();
    
    // Only top state (State3) should receive update and input
    manager.update(0.016f);
    manager.handleInput();
    
    BOOST_CHECK(!state1Ptr->wasUpdateCalled());
    BOOST_CHECK(!state2Ptr->wasUpdateCalled());
    BOOST_CHECK(state3Ptr->wasUpdateCalled());
    
    BOOST_CHECK(!state1Ptr->wasHandleInputCalled());
    BOOST_CHECK(!state2Ptr->wasHandleInputCalled());
    BOOST_CHECK(state3Ptr->wasHandleInputCalled());
    
    // Only the top state should render (correct behavior for game state management)
    state1Ptr->resetFlags();
    state2Ptr->resetFlags();
    state3Ptr->resetFlags();
    
    manager.render();
    
    BOOST_CHECK(!state1Ptr->wasRenderCalled()); // State1 is paused, should not render
    BOOST_CHECK(!state2Ptr->wasRenderCalled()); // State2 is paused, should not render  
    BOOST_CHECK(state3Ptr->wasRenderCalled());  // State3 is active, should render
}

BOOST_AUTO_TEST_CASE(TestComplexStateTransitions) {
    auto mockState1 = std::make_unique<MockGameState>("Menu");
    auto mockState2 = std::make_unique<MockGameState>("Game");
    auto mockState3 = std::make_unique<MockGameState>("Pause");
    MockGameState* menuPtr = mockState1.get();
    MockGameState* gamePtr = mockState2.get();
    MockGameState* pausePtr = mockState3.get();
    
    manager.addState(std::move(mockState1));
    manager.addState(std::move(mockState2));
    manager.addState(std::move(mockState3));
    
    // Start with menu
    manager.pushState("Menu");
    BOOST_CHECK(menuPtr->wasEnterCalled());
    
    // Change to game (menu exits, game enters)
    menuPtr->resetFlags();
    gamePtr->resetFlags();
    manager.changeState("Game");
    BOOST_CHECK(menuPtr->wasExitCalled());
    BOOST_CHECK(gamePtr->wasEnterCalled());
    
    // Push pause (game pauses, pause enters)
    gamePtr->resetFlags();
    pausePtr->resetFlags();
    manager.pushState("Pause");
    BOOST_CHECK(gamePtr->wasPauseCalled());
    BOOST_CHECK(pausePtr->wasEnterCalled());
    
    // Pop pause (pause exits, game resumes)
    gamePtr->resetFlags();
    pausePtr->resetFlags();
    manager.popState();
    BOOST_CHECK(pausePtr->wasExitCalled());
    BOOST_CHECK(gamePtr->wasResumeCalled());
}

BOOST_AUTO_TEST_SUITE_END()