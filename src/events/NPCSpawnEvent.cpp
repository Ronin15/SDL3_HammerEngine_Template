/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include "events/NPCSpawnEvent.hpp"
#include "utils/Vector2D.hpp"
#include "entities/NPC.hpp"
#include "core/GameEngine.hpp"
#include <iostream>
#include <random>
#include <chrono>
#include <algorithm>

// Helper function to map NPC types to texture IDs
static std::string getTextureForNPCType(const std::string& npcType) {
    if (npcType == "Guard") return "guard";
    if (npcType == "Villager") return "villager";
    if (npcType == "Merchant") return "merchant";
    if (npcType == "Warrior") return "warrior";
    return "npc"; // Default fallback
}

// Helper function to get player position
static Vector2D getPlayerPosition() {
    // Try to get player position from current game state
    // For now, return center of screen as placeholder
    return Vector2D(GameEngine::Instance().getWindowWidth() / 2.0f,
                   GameEngine::Instance().getWindowHeight() / 2.0f);
}

// Random number generation
static std::random_device rd;
static std::mt19937 gen(rd());

NPCSpawnEvent::NPCSpawnEvent(const std::string& name, const std::string& npcType)
    : m_name(name) {

    // Initialize with basic parameters
    m_spawnParams.npcType = npcType;
    m_spawnParams.count = 1;
    m_spawnParams.spawnRadius = 0.0f;
}

NPCSpawnEvent::NPCSpawnEvent(const std::string& name, const SpawnParameters& params)
    : m_name(name), m_spawnParams(params) {
}

void NPCSpawnEvent::update() {
    // Skip update if not active or on cooldown
    if (!m_active || m_onCooldown) {
        return;
    }

    // Clean up any dead entities from the tracked list
    cleanDeadEntities();

    // Check respawn conditions if applicable
    if (m_canRespawn && areAllEntitiesDead()) {
        m_respawnTimer += 0.016f; // Assume ~60fps for now
    }

    // Update frame counter for frequency control
    m_frameCounter++;
    if (m_updateFrequency > 1 && m_frameCounter % m_updateFrequency != 0) {
        return;
    }

    // Reset frame counter to prevent overflow
    if (m_frameCounter >= 10000) {
        m_frameCounter = 0;
    }
}

void NPCSpawnEvent::execute() {
    // Check spawn count limits
    if (m_maxSpawnCount >= 0 && m_currentSpawnCount >= m_maxSpawnCount) {
        std::cout << "NPCSpawnEvent: " << m_name << " - Max spawn count reached ("
                  << m_maxSpawnCount << ")" << std::endl;
        return;
    }

    // Mark as triggered
    m_hasTriggered = true;

    // Start cooldown if set
    if (m_cooldownTime > 0.0f) {
        m_onCooldown = true;
        m_cooldownTimer = 0.0f;
    }

    // Spawn the specified number of NPCs
    std::cout << "Spawning " << m_spawnParams.count << " NPCs of type: "
              << m_spawnParams.npcType << std::endl;

    // Clear previous spawned entities if we don't want to track them
    if (!m_canRespawn && !m_oneTimeEvent) {
        clearSpawnedEntities();
    }

    // Perform the actual spawning
    for (int i = 0; i < m_spawnParams.count; ++i) {
        // Get a spawn position based on configured area/points
        Vector2D spawnPos = getRandomSpawnPosition();

        // Spawn the NPC
        EntityPtr entity = spawnSingleNPC(spawnPos);

        // Track the entity if spawned successfully
        if (entity) {
            m_spawnedEntities.push_back(entity);
            m_currentSpawnCount++;
            m_totalSpawned++;

            std::cout << "  - NPC " << i+1 << " spawned at position ("
                              << spawnPos.getX() << ", " << spawnPos.getY() << ")" << std::endl;
        }
    }

    // Reset respawn timer
    m_respawnTimer = 0.0f;
}

void NPCSpawnEvent::reset() {
    m_onCooldown = false;
    m_cooldownTimer = 0.0f;
    m_hasTriggered = false;
    m_respawnTimer = 0.0f;

    // Clear spawned entities
    clearSpawnedEntities();

    // Reset spawn counters
    m_currentSpawnCount = 0;
    m_totalSpawned = 0;
}

void NPCSpawnEvent::clean() {
    // Clean up any resources specific to this spawn event
    m_conditions.clear();
    clearSpawnedEntities();
}

void NPCSpawnEvent::onMessage(const std::string& message) {
    // Handle spawn requests from EventManager
    // Note: We handle spawn requests even when inactive to allow manual triggering
    if (message.find("SPAWN_REQUEST:") == 0) {
        // Check if we've reached the maximum NPC limit
        if (m_totalSpawned >= 50) { // Limit to 50 total NPCs to prevent memory issues
            std::cout << "NPC spawn limit reached (50), ignoring spawn request" << std::endl;
            return;
        }

        // Parse the message to extract NPC type and position
        // Format: "SPAWN_REQUEST:NPCType:x:y"
        // Parse spawn request message

        size_t firstColon = message.find(':', 14); // Skip "SPAWN_REQUEST:"
        size_t secondColon = message.find(':', firstColon + 1);

        if (firstColon != std::string::npos && secondColon != std::string::npos) {
            std::string requestedNpcType = message.substr(14, firstColon - 14);
            std::string xStr = message.substr(firstColon + 1, secondColon - firstColon - 1);
            std::string yStr = message.substr(secondColon + 1);

            float x = std::stof(xStr);
            float y = std::stof(yStr);

            // Parsed spawn request successfully

            // Only respond if this event is configured for the requested NPC type
            if (requestedNpcType != m_spawnParams.npcType) {
                return; // This event doesn't handle this NPC type
            }

            // Create the NPC directly
            try {
                std::string textureID = getTextureForNPCType(requestedNpcType);
                Vector2D position(x, y);
                auto npc = NPC::create(textureID, position, 64, 64);

                if (npc) {
                    // Set wander area around spawn point
                    npc->setWanderArea(x - 100.0f, y - 100.0f, x + 100.0f, y + 100.0f);
                    npc->setBoundsCheckEnabled(true);

                    // Track the spawned NPC
                    m_spawnedEntities.push_back(std::static_pointer_cast<Entity>(npc));
                    m_currentSpawnCount++;
                    m_totalSpawned++;

                    // NPC created successfully
                } else {
                    std::cerr << "Failed to create NPC: " << requestedNpcType << std::endl;
                }
            } catch (const std::exception& e) {
                std::cerr << "Exception spawning NPC: " << e.what() << std::endl;
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

    // Default to circle around origin if no points defined
    if (m_areaType == SpawnAreaType::Points) {
        m_areaType = SpawnAreaType::Circle;
        m_areaCenter = Vector2D(0, 0);
        m_areaRadius = 10.0f;
    }
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
    // If no entities were spawned, consider them "all dead"
    if (m_spawnedEntities.empty()) {
        return true;
    }

    // Check if all spawned entities are gone using STL algorithm
    return std::none_of(m_spawnedEntities.begin(), m_spawnedEntities.end(),
                      [](const auto& weakEntity) {
                          return weakEntity.lock() != nullptr;
                      });
}

EntityPtr NPCSpawnEvent::forceSpawnNPC(const std::string& npcType, float x, float y) {
    std::cout << "Forcing spawn of NPC type: " << npcType
              << " at position (" << x << ", " << y << ")" << std::endl;

    try {
        // Get the texture ID for this NPC type
        std::string textureID = getTextureForNPCType(npcType);

        // Create the NPC
        Vector2D position(x, y);
        auto npc = NPC::create(textureID, position, 64, 64);

        if (npc) {
            // Set basic wander area around spawn point
            npc->setWanderArea(x - 50.0f, y - 50.0f, x + 50.0f, y + 50.0f);
            npc->setBoundsCheckEnabled(true);

            std::cout << "Force-spawned " << npcType << " at (" << x << ", " << y << ")" << std::endl;
            return std::static_pointer_cast<Entity>(npc);
        }

        // Return nullptr if NPC creation failed
        return nullptr;

    } catch (const std::exception& e) {
        std::cerr << "Exception while force-spawning NPC: " << e.what() << std::endl;
        return nullptr;
    }
}

std::vector<EntityPtr> NPCSpawnEvent::forceSpawnNPCs(const SpawnParameters& params, float x, float y) {
    std::cout << "Forcing spawn of " << params.count << " NPCs of type: " << params.npcType
              << " at position (" << x << ", " << y << ")" << std::endl;

    std::vector<EntityPtr> spawnedNPCs;

    try {
        std::string textureID = getTextureForNPCType(params.npcType);

        for (int i = 0; i < params.count; ++i) {
            // Calculate spawn position with some random offset
            std::uniform_real_distribution<float> offsetDist(-params.spawnRadius, params.spawnRadius);
            float offsetX = params.spawnRadius > 0 ? offsetDist(gen) : 0.0f;
            float offsetY = params.spawnRadius > 0 ? offsetDist(gen) : 0.0f;

            Vector2D spawnPos(x + offsetX, y + offsetY);
            auto npc = NPC::create(textureID, spawnPos, 64, 64);

            if (npc) {
                // Configure wander area
                float wanderRadius = params.spawnRadius > 0 ? params.spawnRadius : 50.0f;
                npc->setWanderArea(
                    x - wanderRadius, y - wanderRadius,
                    x + wanderRadius, y + wanderRadius
                );
                npc->setBoundsCheckEnabled(true);

                spawnedNPCs.push_back(std::static_pointer_cast<Entity>(npc));
                std::cout << "  - NPC " << (i+1) << " spawned successfully" << std::endl;
            }
        }

    } catch (const std::exception& e) {
        std::cerr << "Exception while force-spawning NPCs: " << e.what() << std::endl;
    }

    return spawnedNPCs;
}

bool NPCSpawnEvent::checkProximityCondition() const {
    if (!m_useProximityTrigger) {
        return true; // No proximity check required
    }

    Vector2D playerPos = getPlayerPosition();
    Vector2D centerPos;

    // Determine center position based on spawn area type
    switch (m_areaType) {
        case SpawnAreaType::Points:
            if (m_spawnPoints.empty()) {
                centerPos = Vector2D(0.0f, 0.0f);
            } else {
                // Use the first point as reference
                centerPos = m_spawnPoints[0];
            }
            break;

        case SpawnAreaType::Rectangle:
            // Use center of rectangle
            centerPos.setX((m_areaX1 + m_areaX2) / 2.0f);
            centerPos.setY((m_areaY1 + m_areaY2) / 2.0f);
            break;

        case SpawnAreaType::Circle:
            centerPos = m_areaCenter;
            break;
    }

    // Calculate distance from player to center
    float dx = playerPos.getX() - centerPos.getX();
    float dy = playerPos.getY() - centerPos.getY();
    float distSquared = dx * dx + dy * dy;

    return distSquared <= (m_proximityDistance * m_proximityDistance);
}

bool NPCSpawnEvent::checkTimeCondition() const {
    if (!m_useTimeOfDay) {
        return true; // No time restriction
    }

    // This would get the current game time from a time system
    // For now, using a placeholder value (real-world hour 0-23)
    auto now = std::chrono::system_clock::now();
    auto timeT = std::chrono::system_clock::to_time_t(now);
    std::tm localTime = *std::localtime(&timeT);
    float currentHour = static_cast<float>(localTime.tm_hour) +
                        static_cast<float>(localTime.tm_min) / 60.0f;

    if (m_startHour <= m_endHour) {
        // Simple case: start time is before end time
        return currentHour >= m_startHour && currentHour <= m_endHour;
    } else {
        // Wrapping case: start time is after end time (spans midnight)
        return currentHour >= m_startHour || currentHour <= m_endHour;
    }
}

bool NPCSpawnEvent::checkRespawnCondition() const {
    if (!m_canRespawn) {
        return false; // Respawn not enabled
    }

    return m_respawnTimer >= m_respawnTime;
}

Vector2D NPCSpawnEvent::getRandomSpawnPosition() const {
    switch (m_areaType) {
        case SpawnAreaType::Points:
            if (m_spawnPoints.empty()) {
                // Fallback to origin if no points defined
                return Vector2D(0.0f, 0.0f);
            } else {
                // Pick a random point from the list
                std::uniform_int_distribution<size_t> dist(0, m_spawnPoints.size() - 1);
                size_t index = dist(gen);

                // If spawn radius is set, randomize around the point
                if (m_spawnParams.spawnRadius > 0.0f) {
                    return getRandomPointAroundPoint(m_spawnPoints[index], m_spawnParams.spawnRadius);
                } else {
                    return m_spawnPoints[index];
                }
            }

        case SpawnAreaType::Rectangle:
            return getRandomPointInRectangle();

        case SpawnAreaType::Circle:
            return getRandomPointInCircle();
    }

    // Fallback
    return Vector2D(0, 0);
}

Vector2D NPCSpawnEvent::getRandomPointInRectangle() const {
    std::uniform_real_distribution<float> distX(m_areaX1, m_areaX2);
    std::uniform_real_distribution<float> distY(m_areaY1, m_areaY2);

    return Vector2D(distX(gen), distY(gen));
}

Vector2D NPCSpawnEvent::getRandomPointInCircle() const {
    // Generate random angle and distance from center
    std::uniform_real_distribution<float> distAngle(0.0f, 2.0f * 3.14159f);
    std::uniform_real_distribution<float> distRadius(0.0f, m_areaRadius);

    float angle = distAngle(gen);
    float radius = distRadius(gen);

    float x = m_areaCenter.getX() + radius * std::cos(angle);
    float y = m_areaCenter.getY() + radius * std::sin(angle);

    return Vector2D(x, y);
}

Vector2D NPCSpawnEvent::getRandomPointAroundPoint(const Vector2D& center, float radius) const {
    // Generate random angle and distance from center
    std::uniform_real_distribution<float> distAngle(0.0f, 2.0f * 3.14159f);
    std::uniform_real_distribution<float> distRadius(0.0f, radius);

    float angle = distAngle(gen);
    float dist = distRadius(gen);

    float x = center.getX() + dist * std::cos(angle);
    float y = center.getY() + dist * std::sin(angle);

    return Vector2D(x, y);
}

Vector2D NPCSpawnEvent::getPlayerPosition() const {
    // This would typically get the player position from the game state
    // For now, use the helper function
    return ::getPlayerPosition();
}

EntityPtr NPCSpawnEvent::spawnSingleNPC(const Vector2D& position) {
    std::cout << "Spawning NPC of type " << m_spawnParams.npcType
              << " at position (" << position.getX() << ", " << position.getY() << ")" << std::endl;

    try {
        // Get the texture ID for this NPC type
        std::string textureID = getTextureForNPCType(m_spawnParams.npcType);

        // Create the NPC with appropriate frame dimensions
        // Most NPC sprites are 64x64 pixels per frame
        auto npc = NPC::create(textureID, position, 64, 64);

        if (!npc) {
            std::cerr << "Failed to create NPC of type: " << m_spawnParams.npcType << std::endl;
            return nullptr;
        }

        // Configure NPC properties
        if (m_spawnParams.facingPlayer) {
            Vector2D playerPos = getPlayerPosition();
            if (playerPos.getX() < position.getX()) {
                npc->setFlip(SDL_FLIP_HORIZONTAL);
            }
        }

        // Set wander area if NPC should stay in bounds
        float wanderRadius = m_spawnParams.spawnRadius > 0 ? m_spawnParams.spawnRadius : 100.0f;
        npc->setWanderArea(
            position.getX() - wanderRadius, position.getY() - wanderRadius,
            position.getX() + wanderRadius, position.getY() + wanderRadius
        );
        npc->setBoundsCheckEnabled(true);

        // Additional spawn options
        if (m_spawnParams.fadeIn) {
            std::cout << "  - With fade-in effect over " << m_spawnParams.fadeTime << "s" << std::endl;
        }

        if (m_spawnParams.playSpawnEffect && !m_spawnParams.spawnEffectID.empty()) {
            std::cout << "  - With spawn effect: " << m_spawnParams.spawnEffectID << std::endl;
        }

        if (!m_spawnParams.spawnSoundID.empty()) {
            std::cout << "  - With spawn sound: " << m_spawnParams.spawnSoundID << std::endl;
        }

        if (!m_spawnParams.aiBehavior.empty()) {
            std::cout << "  - With AI behavior: " << m_spawnParams.aiBehavior << std::endl;
            // TODO: Assign AI behavior to NPC
            // AIManager::Instance().assignBehaviorToEntity(npc, m_spawnParams.aiBehavior);
        }

        std::cout << "Successfully created NPC with texture: " << textureID << std::endl;
        return std::static_pointer_cast<Entity>(npc);
    } catch (const std::exception& e) {
        std::cerr << "Exception creating NPC: " << e.what() << std::endl;
        return nullptr;
    }
}

void NPCSpawnEvent::cleanDeadEntities() {
    // Remove any expired weak pointers from the list
    m_spawnedEntities.erase(
        std::remove_if(
            m_spawnedEntities.begin(),
            m_spawnedEntities.end(),
            [](const EntityWeakPtr& weakPtr) { return weakPtr.expired(); }
        ),
        m_spawnedEntities.end()
    );

    // Update current spawn count
    m_currentSpawnCount = static_cast<int>(m_spawnedEntities.size());
}
