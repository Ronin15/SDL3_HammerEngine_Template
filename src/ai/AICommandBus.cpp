/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "ai/AICommandBus.hpp"
#include <algorithm>

namespace HammerEngine {

void AICommandBus::enqueueBehaviorMessage(EntityHandle targetHandle, size_t targetEdmIndex,
                                          uint8_t messageId, uint8_t param) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_pendingMessages.push_back({targetHandle, targetEdmIndex, messageId, param});
}

void AICommandBus::enqueueBehaviorTransition(EntityHandle targetHandle, size_t targetEdmIndex,
                                             const BehaviorConfigData& config) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_pendingTransitions.push_back({targetHandle, targetEdmIndex, config});
}

void AICommandBus::clearBehaviorMessages(EntityHandle targetHandle, size_t targetEdmIndex) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::erase_if(m_pendingMessages, [targetHandle, targetEdmIndex](const BehaviorMessageCommand& cmd) {
        return cmd.targetEdmIndex == targetEdmIndex && cmd.targetHandle == targetHandle;
    });
}

void AICommandBus::clearAll() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_pendingMessages.clear();
    m_pendingTransitions.clear();
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
