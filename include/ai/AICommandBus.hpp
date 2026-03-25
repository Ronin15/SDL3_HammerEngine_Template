/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#ifndef AI_COMMAND_BUS_HPP
#define AI_COMMAND_BUS_HPP

#include "ai/BehaviorConfig.hpp"
#include "entities/EntityHandle.hpp"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

namespace HammerEngine {

class AICommandBus {
public:
    struct BehaviorMessageCommand {
        EntityHandle targetHandle{};
        size_t targetEdmIndex{SIZE_MAX};
        uint8_t messageId{0};
        uint8_t param{0};
        uint64_t sequence{0}; // Monotonic enqueue sequence for deterministic arbitration
    };

    struct BehaviorTransitionCommand {
        EntityHandle targetHandle{};
        size_t targetEdmIndex{SIZE_MAX};
        BehaviorConfigData config{};
        uint64_t sequence{0}; // Monotonic enqueue sequence for deterministic arbitration
    };

    struct FactionChangeCommand {
        EntityHandle targetHandle{};
        size_t targetEdmIndex{SIZE_MAX};
        uint8_t oldFaction{0};
        uint8_t newFaction{0};
    };

    static AICommandBus& Instance() {
        static AICommandBus instance;
        return instance;
    }

    void enqueueBehaviorMessage(EntityHandle targetHandle, size_t targetEdmIndex,
                                uint8_t messageId, uint8_t param = 0);
    void enqueueBehaviorTransition(EntityHandle targetHandle, size_t targetEdmIndex,
                                   const BehaviorConfigData& config);
    void enqueueFactionChange(EntityHandle targetHandle, size_t targetEdmIndex,
                              uint8_t oldFaction, uint8_t newFaction);
    void clearBehaviorMessages(EntityHandle targetHandle, size_t targetEdmIndex);
    void clearAll();

    void drainBehaviorMessages(std::vector<BehaviorMessageCommand>& out);
    void drainBehaviorTransitions(std::vector<BehaviorTransitionCommand>& out);
    void drainFactionChanges(std::vector<FactionChangeCommand>& out);

private:
    AICommandBus() = default;
    AICommandBus(const AICommandBus&) = delete;
    AICommandBus& operator=(const AICommandBus&) = delete;

    std::mutex m_mutex;
    std::vector<BehaviorMessageCommand> m_pendingMessages;
    std::vector<BehaviorTransitionCommand> m_pendingTransitions;
    std::vector<FactionChangeCommand> m_pendingFactionChanges;
    std::atomic<uint64_t> m_nextMessageSequence{1};
    std::atomic<uint64_t> m_nextTransitionSequence{1};
};

} // namespace HammerEngine

#endif // AI_COMMAND_BUS_HPP
