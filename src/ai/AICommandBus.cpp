/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "ai/AICommandBus.hpp"
#include <algorithm>

namespace HammerEngine {

void AICommandBus::enqueueBehaviorMessage(size_t targetEdmIndex, uint8_t messageId, uint8_t param) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_pendingMessages.push_back({targetEdmIndex, messageId, param});
}

void AICommandBus::enqueueBehaviorTransition(size_t targetEdmIndex, const BehaviorConfigData& config) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_pendingTransitions.push_back({targetEdmIndex, config});
}

void AICommandBus::clearBehaviorMessages(size_t targetEdmIndex) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::erase_if(m_pendingMessages, [targetEdmIndex](const BehaviorMessageCommand& cmd) {
        return cmd.targetEdmIndex == targetEdmIndex;
    });
}

void AICommandBus::drainBehaviorMessages(std::vector<BehaviorMessageCommand>& out) {
    std::lock_guard<std::mutex> lock(m_mutex);
    out.clear();
    out.swap(m_pendingMessages);
}

void AICommandBus::drainBehaviorTransitions(std::vector<BehaviorTransitionCommand>& out) {
    std::lock_guard<std::mutex> lock(m_mutex);
    out.clear();
    out.swap(m_pendingTransitions);
}

} // namespace HammerEngine
