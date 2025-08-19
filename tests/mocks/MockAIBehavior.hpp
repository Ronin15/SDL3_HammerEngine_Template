/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#ifndef MOCK_AI_BEHAVIOR_HPP
#define MOCK_AI_BEHAVIOR_HPP

#include "ai/AIBehavior.hpp"

class MockAIBehavior : public AIBehavior {
public:
    MockAIBehavior() = default;
    ~MockAIBehavior() override = default;

    void executeLogic(EntityPtr entity) override;
    void init(EntityPtr entity) override;
    void clean(EntityPtr entity) override;
    std::string getName() const override;
    std::shared_ptr<AIBehavior> clone() const override;

    // Mock control
    int executeLogicCallCount = 0;
    int initCallCount = 0;
    int cleanCallCount = 0;
    std::string name = "MockBehavior";
};

#endif // MOCK_AI_BEHAVIOR_HPP
