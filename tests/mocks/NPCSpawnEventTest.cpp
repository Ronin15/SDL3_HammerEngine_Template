/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include "events/NPCSpawnEvent.hpp"
#include "utils/Vector2D.hpp"
#include "MockNPC.hpp"
#include "MockGameEngine.hpp"
#include <iostream>
#include <random>
#include <chrono>
#include <algorithm>
#include <cmath>

// Helper function to map NPC types to texture IDs
static std::string getTextureForNPCType(const std::string& npcType) {
    if (npcType == "Guard") return "guard";
    if (npcType == "Villager") return "villager";
    if (npcType == "Merchant") return "npc";
    if (npcType == "Warrior") return "npc";
    return "npc"; // Default fallback
}

// Helper function to get player position for tests
static Vector2D getPlayerPosition() {
    // Return center of mock screen for testing
    return Vector2D(GameEngine::Instance().getWindowWidth() / 2.0f, 
                   GameEngine::Instance().getWindowHeight() / 2.0f);
}

// Random number generator for testing
static std::mt19937 gen(std::chrono::steady_clock::now().time_since_epoch().count());

NPCSpawnEvent::NPCSpawnEvent(const std::string& name, const std::string& npcType)
    : m_name(name) {
    m_spawnParams.npcType = npcType;
    m_spawnParams.count = 1;
    m_spawnParams.spawnRadius = 0.0f;
}

NPCSpawnEvent::NPCSpawnEvent(const std::string& name, const SpawnParameters& params)
    : m_name(name), m_spawnParams(params) {
}

void NPCSpawnEvent::update() {
    if (!isActive()) return;

    // Update respawn timer
    if (m_canRespawn && m_respawnTimer > 0.0f) {
        m_respawnTimer -= 0.016f; // Assume 60 FPS for testing
    }

    // Clean up dead entities
    cleanDeadEntities();

    // Check if we should trigger
    if (checkConditions()) {
        execute();
    }

    // Check for auto-reset conditions
    if (m_canRespawn && canRespawn() && areAllEntitiesDead()) {
        reset();
    }
}

void NPCSpawnEvent::execute() {
    if (!isActive()) return;

    // Check spawn limits
    if (m_maxSpawnCount > 0 && m_currentSpawnCount >= m_maxSpawnCount) {
        std::cout << "NPCSpawnEvent: Spawn limit reached (" << m_maxSpawnCount << ")" << std::endl;
        return;
    }

    std::cout << "Executing NPCSpawnEvent: " << m_name << std::endl;

    try {
        for (int i = 0; i < m_spawnParams.count; ++i) {
            if (m_maxSpawnCount > 0 && m_currentSpawnCount >= m_maxSpawnCount) {
                break;
            }

            Vector2D spawnPos = getRandomSpawnPosition();
            EntityPtr npc = spawnSingleNPC(spawnPos);
            
            if (npc) {
                m_spawnedEntities.push_back(npc);
                m_currentSpawnCount++;
                m_totalSpawned++;
                std::cout << "Spawned " << m_spawnParams.npcType << " at (" 
                         << spawnPos.getX() << ", " << spawnPos.getY() << ")" << std::endl;
            }
        }

        // Set respawn timer if enabled
        if (m_canRespawn) {
            m_respawnTimer = m_respawnTime;
        }

        // Mark as executed
        m_hasTriggered = true;

    } catch (const std::exception& e) {
        std::cerr << "Exception during NPCSpawnEvent execution: " << e.what() << std::endl;
    }
}

void NPCSpawnEvent::reset() {
    m_hasTriggered = false;
    setActive(true);
    
    // Don't reset spawn counts or spawned entities for testing
    // This allows for proper testing of limits and tracking
    
    m_respawnTimer = 0.0f;
    
    std::cout << "NPCSpawnEvent reset: " << m_name << std::endl;
}

void NPCSpawnEvent::clean() {
    clearSpawnedEntities();
    m_conditions.clear();
}

void NPCSpawnEvent::onMessage(const std::string& message) {
    std::cout << "NPCSpawnEvent received message: " << message << std::endl;
    
    if (message == "spawn") {
        execute();
    } else if (message == "reset") {
        reset();
    } else if (message == "clear") {
        clearSpawnedEntities();
    } else if (message.find("spawn_at:") == 0) {
        // Parse spawn_at:x,y message
        std::string coords = message.substr(9);
        std::size_t comma = coords.find(',');
        if (comma != std::string::npos) {
            try {
                float x = std::stof(coords.substr(0, comma));
                float y = std::stof(coords.substr(comma + 1));
                
                Vector2D position(x, y);
                EntityPtr npc = spawnSingleNPC(position);
                if (npc) {
                    m_spawnedEntities.push_back(npc);
                    m_currentSpawnCount++;
                    m_totalSpawned++;
                }
            } catch (const std::exception& e) {
                std::cerr << "Failed to parse spawn coordinates: " << e.what() << std::endl;
            }
        }
    }
}

void NPCSpawnEvent::addSpawnPoint(float x, float y) {
    addSpawnPoint(Vector2D(x, y));
}

void NPCSpawnEvent::addSpawnPoint(const Vector2D& point) {
    m_areaType = SpawnAreaType::Points;
    m_spawnPoints.push_back(point);
}

void NPCSpawnEvent::clearSpawnPoints() {
    m_spawnPoints.clear();
    m_areaType = SpawnAreaType::Points;
    
    // Reset area parameters
    m_areaX1 = m_areaY1 = m_areaX2 = m_areaY2 = 0.0f;
    m_areaCenter = Vector2D(0.0f, 0.0f);
    m_areaRadius = 0.0f;
}

void NPCSpawnEvent::setSpawnArea(float x1, float y1, float x2, float y2) {
    m_areaType = SpawnAreaType::Rectangle;
    m_areaX1 = x1;
    m_areaY1 = y1;
    m_areaX2 = x2;
    m_areaY2 = y2;
}

void NPCSpawnEvent::setSpawnArea(float centerX, float centerY, float radius) {
    m_areaType = SpawnAreaType::Circle;
    m_areaCenter = Vector2D(centerX, centerY);
    m_areaRadius = radius;
}

bool NPCSpawnEvent::checkConditions() {
    // For demo events, only allow triggering through explicit spawn requests
    // This prevents auto-triggering while still allowing manual triggers
    if (m_name.find("demo_") == 0) {
        return false; // Demo events should only respond to onMessage, not auto-trigger
    }

    // If this is a one-time event that has already triggered, return false
    if (m_oneTimeEvent && m_hasTriggered) {
        return false;
    }

    // If respawn is enabled, check if all entities are dead and respawn timer elapsed
    if (m_canRespawn) {
        if (!areAllEntitiesDead() || !checkRespawnCondition()) {
            return false;
        }
    }

    // Check max spawn count
    if (m_maxSpawnCount >= 0 && m_currentSpawnCount >= m_maxSpawnCount) {
        return false;
    }

    // Check all custom conditions using STL algorithm
    if (!std::all_of(m_conditions.begin(), m_conditions.end(), 
                     [](const auto& condition) { return condition(); })) {
        return false;
    }

    // Check proximity if enabled
    if (m_useProximityTrigger && !checkProximityCondition()) {
        return false;
    }

    // Check time of day if enabled
    if (m_useTimeOfDay && !checkTimeCondition()) {
        return false;
    }

    return true;
}

void NPCSpawnEvent::addCondition(std::function<bool()> condition) {
    m_conditions.push_back(condition);
}

void NPCSpawnEvent::setProximityTrigger(float distance) {
    m_useProximityTrigger = true;
    m_proximityDistance = distance;
}

bool NPCSpawnEvent::isPlayerInProximity() const {
    return checkProximityCondition();
}

void NPCSpawnEvent::setTimeOfDayTrigger(float startHour, float endHour) {
    m_useTimeOfDay = true;
    m_startHour = startHour;
    m_endHour = endHour;
}

void NPCSpawnEvent::setRespawnTime(float seconds) {
    m_canRespawn = true;
    m_respawnTime = seconds;
    m_respawnTimer = 0.0f;
}

bool NPCSpawnEvent::canRespawn() const {
    return m_canRespawn && areAllEntitiesDead() && m_respawnTimer >= m_respawnTime;
}

void NPCSpawnEvent::clearSpawnedEntities() {
    m_spawnedEntities.clear();
    m_currentSpawnCount = 0;
}

bool NPCSpawnEvent::areAllEntitiesDead() const {
    for (const auto& weakPtr : m_spawnedEntities) {
        if (!weakPtr.expired()) {
            return false; // At least one entity is still alive
        }
    }
    return true; // All entities are dead/expired
}

EntityPtr NPCSpawnEvent::forceSpawnNPC(const std::string& npcType, float x, float y) {
    std::cout << "Forcing spawn of NPC type: " << npcType
              << " at position (" << x << ", " << y << ")" << std::endl;

    try {
        // Get the texture ID for this NPC type
        std::string textureID = getTextureForNPCType(npcType);
        
        // Create the NPC using mock
        Vector2D position(x, y);
        auto npc = MockNPC::create(textureID, position, 64, 64);
        
        if (npc) {
            // Set basic wander area around spawn point
            npc->setWanderArea(x - 50.0f, y - 50.0f, x + 50.0f, y + 50.0f);
            npc->setBoundsCheckEnabled(true);
            
            std::cout << "Force-spawned " << npcType << " at (" << x << ", " << y << ")" << std::endl;
            return std::static_pointer_cast<Entity>(npc);
        }
        
        return nullptr;
        
    } catch (const std::exception& e) {
        std::cerr << "Exception while force-spawning NPC: " << e.what() << std::endl;
        return nullptr;
    }
}

std::vector<EntityPtr> NPCSpawnEvent::forceSpawnNPCs(const SpawnParameters& params, float x, float y) {
    std::vector<EntityPtr> spawnedNPCs;
    
    for (int i = 0; i < params.count; ++i) {
        Vector2D spawnPos(x, y);
        
        // Apply spawn radius if specified
        if (params.spawnRadius > 0.0f) {
            std::uniform_real_distribution<float> angleDist(0.0f, 2.0f * 3.14159f);
            std::uniform_real_distribution<float> radiusDist(0.0f, params.spawnRadius);
            
            float angle = angleDist(gen);
            float radius = radiusDist(gen);
            
            spawnPos.setX(spawnPos.getX() + radius * cos(angle));
            spawnPos.setY(spawnPos.getY() + radius * sin(angle));
        }
        
        // Apply minimum distance apart
        if (params.minDistanceApart > 0.0f && !spawnedNPCs.empty()) {
            bool tooClose = true;
            int attempts = 0;
            
            while (tooClose && attempts < 10) {
                tooClose = false;
                for (const auto& existingNPC : spawnedNPCs) {
                    Vector2D existingPos = existingNPC->getPosition();
                    float distance = sqrt(pow(spawnPos.getX() - existingPos.getX(), 2) + pow(spawnPos.getY() - existingPos.getY(), 2));
                    if (distance < params.minDistanceApart) {
                        tooClose = true;
                        break;
                    }
                }
                
                if (tooClose) {
                    spawnPos.setX(spawnPos.getX() + (std::rand() % 20 - 10));
                    spawnPos.setY(spawnPos.getY() + (std::rand() % 20 - 10));
                    attempts++;
                }
            }
        }
        
        EntityPtr npc = forceSpawnNPC(params.npcType, spawnPos.getX(), spawnPos.getY());
        if (npc) {
            spawnedNPCs.push_back(npc);
        }
    }
    
    return spawnedNPCs;
}

bool NPCSpawnEvent::checkProximityCondition() const {
    if (!m_useProximityTrigger) return true;

    Vector2D playerPos = getPlayerPosition();
    
    // Check distance to spawn points or areas
    switch (m_areaType) {
        case SpawnAreaType::Points:
            for (const auto& point : m_spawnPoints) {
                float distance = sqrt(pow(playerPos.getX() - point.getX(), 2) + pow(playerPos.getY() - point.getY(), 2));
                if (distance <= m_proximityDistance) {
                    return true;
                }
            }
            return false;
            
        case SpawnAreaType::Rectangle: {
            // Distance to rectangle
            float centerX = (m_areaX1 + m_areaX2) / 2.0f;
            float centerY = (m_areaY1 + m_areaY2) / 2.0f;
            float distance = sqrt(pow(playerPos.getX() - centerX, 2) + pow(playerPos.getY() - centerY, 2));
            return distance <= m_proximityDistance;
        }
        
        case SpawnAreaType::Circle: {
            float distance = sqrt(pow(playerPos.getX() - m_areaCenter.getX(), 2) + pow(playerPos.getY() - m_areaCenter.getY(), 2));
            return distance <= m_proximityDistance;
        }
    }
    
    return false;
}

bool NPCSpawnEvent::checkTimeCondition() const {
    if (!m_useTimeOfDay) return true;
    
    // For testing, assume it's always the right time
    // In a real implementation, this would check game time
    return true;
}

bool NPCSpawnEvent::checkRespawnCondition() const {
    if (!m_canRespawn) return true;
    
    return m_respawnTimer >= m_respawnTime;
}

Vector2D NPCSpawnEvent::getRandomSpawnPosition() const {
    switch (m_areaType) {
        case SpawnAreaType::Points:
            if (!m_spawnPoints.empty()) {
                std::uniform_int_distribution<size_t> dist(0, m_spawnPoints.size() - 1);
                Vector2D basePoint = m_spawnPoints[dist(gen)];
                return getRandomPointAroundPoint(basePoint, m_spawnParams.spawnRadius);
            }
            // Fallback to center of screen
            return Vector2D(GameEngine::Instance().getWindowWidth() / 2.0f,
                           GameEngine::Instance().getWindowHeight() / 2.0f);
            
        case SpawnAreaType::Rectangle:
            return getRandomPointInRectangle();
            
        case SpawnAreaType::Circle:
            return getRandomPointInCircle();
    }
    
    // Default fallback
    return Vector2D(0.0f, 0.0f);
}

Vector2D NPCSpawnEvent::getRandomPointInRectangle() const {
    std::uniform_real_distribution<float> distX(m_areaX1, m_areaX2);
    std::uniform_real_distribution<float> distY(m_areaY1, m_areaY2);
    
    return Vector2D(distX(gen), distY(gen));
}

Vector2D NPCSpawnEvent::getRandomPointInCircle() const {
    std::uniform_real_distribution<float> angleDist(0.0f, 2.0f * 3.14159f);
    std::uniform_real_distribution<float> radiusDist(0.0f, m_areaRadius);
    
    float angle = angleDist(gen);
    float radius = radiusDist(gen);
    
    return Vector2D(
        m_areaCenter.getX() + radius * cos(angle),
        m_areaCenter.getY() + radius * sin(angle)
    );
}

Vector2D NPCSpawnEvent::getRandomPointAroundPoint(const Vector2D& center, float radius) const {
    if (radius <= 0.0f) return center;
    
    std::uniform_real_distribution<float> angleDist(0.0f, 2.0f * 3.14159f);
    std::uniform_real_distribution<float> radiusDist(0.0f, radius);
    
    float angle = angleDist(gen);
    float r = radiusDist(gen);
    
    return Vector2D(
        center.getX() + r * cos(angle),
        center.getY() + r * sin(angle)
    );
}

Vector2D NPCSpawnEvent::getPlayerPosition() const {
    return ::getPlayerPosition();
}

EntityPtr NPCSpawnEvent::spawnSingleNPC(const Vector2D& position) {
    try {
        std::string textureID = getTextureForNPCType(m_spawnParams.npcType);
        
        auto npc = MockNPC::create(textureID, position, 64, 64);
        
        if (npc) {
            // Set wander area
            float wanderSize = 50.0f;
            npc->setWanderArea(
                position.getX() - wanderSize, position.getY() - wanderSize,
                position.getX() + wanderSize, position.getY() + wanderSize
            );
            npc->setBoundsCheckEnabled(true);
            
            return std::static_pointer_cast<Entity>(npc);
        }
        
        return nullptr;
        
    } catch (const std::exception& e) {
        std::cerr << "Exception while spawning NPC: " << e.what() << std::endl;
        return nullptr;
    }
}

void NPCSpawnEvent::cleanDeadEntities() {
    m_spawnedEntities.erase(
        std::remove_if(m_spawnedEntities.begin(), m_spawnedEntities.end(),
                      [](const EntityWeakPtr& ptr) { return ptr.expired(); }),
        m_spawnedEntities.end()
    );
    
    // Update current spawn count
    m_currentSpawnCount = static_cast<int>(m_spawnedEntities.size());
}