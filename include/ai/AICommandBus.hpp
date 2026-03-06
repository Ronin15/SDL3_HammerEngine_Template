/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#ifndef AI_COMMAND_BUS_HPP
#define AI_COMMAND_BUS_HPP

#include "ai/BehaviorConfig.hpp"
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

namespace HammerEngine {

class AICommandBus {
public:
    struct BehaviorMessageCommand {
        size_t targetEdmIndex{SIZE_MAX};
        uint8_t messageId{0};
        uint8_t param{0};
    };

    struct BehaviorTransitionCommand {
        size_t targetEdmIndex{SIZE_MAX};
        BehaviorConfigData config{};
    };

    static AICommandBus& Instance() {
        static AICommandBus instance;
        return instance;
    }

    void enqueueBehaviorMessage(size_t targetEdmIndex, uint8_t messageId, uint8_t param = 0);
    void enqueueBehaviorTransition(size_t targetEdmIndex, const BehaviorConfigData& config);
    void clearBehaviorMessages(size_t targetEdmIndex);

    void drainBehaviorMessages(std::vector<BehaviorMessageCommand>& out);
    void drainBehaviorTransitions(std::vector<BehaviorTransitionCommand>& out);

private:
    AICommandBus() = default;
    AICommandBus(const AICommandBus&) = delete;
    AICommandBus& operator=(const AICommandBus&) = delete;

    std::mutex m_mutex;
    std::vector<BehaviorMessageCommand> m_pendingMessages;
    std::vector<BehaviorTransitionCommand> m_pendingTransitions;
};

} // namespace HammerEngine

#endif // AI_COMMAND_BUS_HPP
