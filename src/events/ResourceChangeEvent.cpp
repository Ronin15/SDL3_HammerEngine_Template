/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "events/ResourceChangeEvent.hpp"

const std::string ResourceChangeEvent::EVENT_TYPE = "ResourceChangeEvent";

ResourceChangeEvent::ResourceChangeEvent(
    EntityPtr owner, HammerEngine::ResourceHandle resourceHandle,
    int oldQuantity, int newQuantity, const std::string &changeReason)
    : m_owner(owner ? EntityWeakPtr(owner) : EntityWeakPtr()), 
      m_resourceHandle(resourceHandle),
      m_oldQuantity(oldQuantity), m_newQuantity(newQuantity),
      m_changeReason(changeReason) {}