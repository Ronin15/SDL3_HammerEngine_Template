/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#ifndef CONTROLLER_BASE_HPP
#define CONTROLLER_BASE_HPP

/**
 * @file ControllerBase.hpp
 * @brief Base class for lightweight event-bridge controllers
 *
 * Controllers are state-scoped helpers that monitor events and dispatch
 * other events or trigger manager actions. They do NOT own data and
 * should NOT contain UI logic.
 *
 * Key characteristics:
 * - Owned by GameState (not singletons)
 * - Auto-unsubscribe on destruction
 * - Minimal state (subscription tokens only)
 *
 * Use a Controller when:
 * - Bridging one event type to another
 * - Triggering manager actions on specific events
 * - Logic is only relevant while in certain game states
 *
 * Promote to Manager when:
 * - Significant data ownership required
 * - Complex simulation logic accumulates
 * - Multiple systems depend on it globally
 */

#include "managers/EventManager.hpp"
#include <vector>

class ControllerBase
{
public:
    /**
     * @brief Virtual destructor auto-unsubscribes from all events
     */
    virtual ~ControllerBase() { unsubscribe(); }

    // Non-copyable (event handlers capture 'this')
    ControllerBase(const ControllerBase&) = delete;
    ControllerBase& operator=(const ControllerBase&) = delete;

    // Movable
    ControllerBase(ControllerBase&& other) noexcept
        : m_subscribed(other.m_subscribed)
        , m_handlerTokens(std::move(other.m_handlerTokens))
    {
        other.m_subscribed = false;
    }

    ControllerBase& operator=(ControllerBase&& other) noexcept
    {
        if (this != &other) {
            unsubscribe();  // Clean up current subscriptions
            m_subscribed = other.m_subscribed;
            m_handlerTokens = std::move(other.m_handlerTokens);
            other.m_subscribed = false;
        }
        return *this;
    }

    /**
     * @brief Unsubscribe from all registered event handlers
     * @note Safe to call multiple times
     */
    void unsubscribe()
    {
        if (!m_subscribed) {
            return;
        }

        auto& eventMgr = EventManager::Instance();
        for (const auto& token : m_handlerTokens) {
            eventMgr.removeHandler(token);
        }
        m_handlerTokens.clear();
        m_subscribed = false;
    }

    /**
     * @brief Check if currently subscribed to events
     * @return True if subscribed, false otherwise
     */
    [[nodiscard]] bool isSubscribed() const { return m_subscribed; }

protected:
    ControllerBase() = default;

    /**
     * @brief Register a handler token for automatic cleanup
     * @param token The handler token from EventManager::registerHandlerWithToken
     */
    void addHandlerToken(const EventManager::HandlerToken& token)
    {
        m_handlerTokens.push_back(token);
    }

    /**
     * @brief Mark controller as subscribed
     * @param subscribed True when subscribed, false otherwise
     */
    void setSubscribed(bool subscribed) { m_subscribed = subscribed; }

    /**
     * @brief Check if already subscribed (for idempotent subscribe)
     * @return True if already subscribed
     */
    [[nodiscard]] bool checkAlreadySubscribed() const { return m_subscribed; }

private:
    bool m_subscribed{false};
    std::vector<EventManager::HandlerToken> m_handlerTokens;
};

#endif // CONTROLLER_BASE_HPP
