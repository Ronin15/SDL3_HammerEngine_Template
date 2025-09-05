#include "EventManagerTestAccess.hpp"

#include "managers/EventManager.hpp"

void EventManagerTestAccess::reset() {
    // Ensure any in-flight work is ended and internal state is consistent
    EventManager::Instance().prepareForStateTransition();
    // Clear handlers explicitly to avoid cross-test interference
    EventManager::Instance().clearAllHandlers();
    // Full teardown and re-init
    EventManager::Instance().clean();
    EventManager::Instance().init();
}

