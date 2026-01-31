/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#define BOOST_TEST_MODULE NPCMemoryTests
#include <boost/test/unit_test.hpp>

#include "managers/EntityDataManager.hpp"
#include "entities/EntityHandle.hpp"
#include "utils/Vector2D.hpp"
#include <cmath>
#include <vector>

// Test tolerance for floating-point comparisons
constexpr float EPSILON = 0.001f;

bool approxEqual(float a, float b, float epsilon = EPSILON) {
    return std::abs(a - b) < epsilon;
}

// ============================================================================
// Test Fixture
// ============================================================================

class NPCMemoryTestFixture {
public:
    NPCMemoryTestFixture() {
        edm = &EntityDataManager::Instance();
        edm->init();
    }

    ~NPCMemoryTestFixture() {
        edm->clean();
    }

protected:
    EntityDataManager* edm;

    // Helper to create a test NPC and get its EDM index
    std::pair<EntityHandle, size_t> createTestNPC(float x = 100.0f, float y = 100.0f) {
        EntityHandle handle = edm->createNPCWithRaceClass(Vector2D(x, y), "Human", "Guard");
        if (!handle.isValid()) {
            // Fallback if race/class not loaded
            handle = edm->registerPlayer(1, Vector2D(x, y), 16.0f, 16.0f);
        }
        size_t index = edm->getIndex(handle);
        return {handle, index};
    }
};

// ============================================================================
// MEMORY DATA STRUCTURE TESTS
// ============================================================================

BOOST_FIXTURE_TEST_SUITE(MemoryStructureTests, NPCMemoryTestFixture)

BOOST_AUTO_TEST_CASE(TestMemoryEntrySize) {
    // MemoryEntry should be compact (under 40 bytes)
    BOOST_CHECK(sizeof(MemoryEntry) <= 40);
}

BOOST_AUTO_TEST_CASE(TestEmotionalStateSize) {
    // EmotionalState should be exactly 16 bytes
    BOOST_CHECK_EQUAL(sizeof(EmotionalState), 16);
}

BOOST_AUTO_TEST_CASE(TestNPCMemoryDataSize) {
    // NPCMemoryData should be under 512 bytes
    BOOST_CHECK(sizeof(NPCMemoryData) <= 512);
}

BOOST_AUTO_TEST_CASE(TestMemoryEntryClearing) {
    MemoryEntry entry;
    entry.subject = EntityHandle{123, EntityKind::NPC, 1};
    entry.location = Vector2D{100.0f, 200.0f};
    entry.timestamp = 10.0f;
    entry.value = 50.0f;
    entry.type = MemoryType::DamageReceived;
    entry.importance = 100;
    entry.flags = MemoryEntry::FLAG_VALID;

    entry.clear();

    BOOST_CHECK(!entry.isValid());
    BOOST_CHECK_EQUAL(entry.timestamp, 0.0f);
    BOOST_CHECK_EQUAL(entry.value, 0.0f);
    BOOST_CHECK_EQUAL(entry.importance, 0);
}

BOOST_AUTO_TEST_CASE(TestEmotionalStateDecay) {
    EmotionalState emotions;
    emotions.aggression = 1.0f;
    emotions.fear = 0.8f;
    emotions.curiosity = 0.5f;
    emotions.suspicion = 0.3f;

    // Decay at 10% per second for 1 second
    emotions.decay(0.1f, 1.0f);

    BOOST_CHECK(approxEqual(emotions.aggression, 0.9f));
    BOOST_CHECK(approxEqual(emotions.fear, 0.72f));
    BOOST_CHECK(approxEqual(emotions.curiosity, 0.45f));
    BOOST_CHECK(approxEqual(emotions.suspicion, 0.27f));
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// MEMORY INITIALIZATION TESTS
// ============================================================================

BOOST_FIXTURE_TEST_SUITE(MemoryInitTests, NPCMemoryTestFixture)

BOOST_AUTO_TEST_CASE(TestMemoryDataPreallocated) {
    auto [handle, index] = createTestNPC();

    // Memory data should exist for the entity (pre-allocated with entity)
    BOOST_CHECK(index < 1000000);  // Valid index

    // Get memory data - should not crash
    auto& memData = edm->getMemoryData(index);
    (void)memData;  // Suppress unused warning
}

BOOST_AUTO_TEST_CASE(TestInitMemoryData) {
    auto [handle, index] = createTestNPC();

    edm->initMemoryData(index);

    BOOST_CHECK(edm->hasMemoryData(index));

    auto& memData = edm->getMemoryData(index);
    BOOST_CHECK(memData.isValid());
    BOOST_CHECK_EQUAL(memData.memoryCount, 0);
    BOOST_CHECK_EQUAL(memData.combatEncounters, 0);
    BOOST_CHECK(!memData.hasOverflow());
}

BOOST_AUTO_TEST_CASE(TestClearMemoryData) {
    auto [handle, index] = createTestNPC();

    edm->initMemoryData(index);

    // Add some data
    MemoryEntry entry;
    entry.type = MemoryType::ThreatSpotted;
    entry.flags = MemoryEntry::FLAG_VALID;
    edm->addMemory(index, entry);

    auto& memData = edm->getMemoryData(index);
    BOOST_CHECK(memData.memoryCount > 0);

    edm->clearMemoryData(index);

    // Memory should be cleared but structure still accessible
    auto& cleared = edm->getMemoryData(index);
    BOOST_CHECK(!cleared.isValid());
    BOOST_CHECK_EQUAL(cleared.memoryCount, 0);
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// ADD MEMORY TESTS
// ============================================================================

BOOST_FIXTURE_TEST_SUITE(AddMemoryTests, NPCMemoryTestFixture)

BOOST_AUTO_TEST_CASE(TestAddSingleMemory) {
    auto [handle, index] = createTestNPC();
    edm->initMemoryData(index);

    MemoryEntry entry;
    entry.subject = EntityHandle{999, EntityKind::Player, 1};
    entry.location = Vector2D{50.0f, 75.0f};
    entry.timestamp = 5.0f;
    entry.value = 25.0f;
    entry.type = MemoryType::AttackedBy;
    entry.importance = 200;
    entry.flags = MemoryEntry::FLAG_VALID;

    edm->addMemory(index, entry);

    auto& memData = edm->getMemoryData(index);
    BOOST_CHECK_EQUAL(memData.memoryCount, 1);
}

BOOST_AUTO_TEST_CASE(TestAddMultipleMemories) {
    auto [handle, index] = createTestNPC();
    edm->initMemoryData(index);

    for (int i = 0; i < 5; ++i) {
        MemoryEntry entry;
        entry.type = MemoryType::LocationVisited;
        entry.timestamp = static_cast<float>(i);
        entry.flags = MemoryEntry::FLAG_VALID;
        edm->addMemory(index, entry);
    }

    auto& memData = edm->getMemoryData(index);
    BOOST_CHECK_EQUAL(memData.memoryCount, 5);
}

BOOST_AUTO_TEST_CASE(TestInlineMemoryCircularBuffer) {
    auto [handle, index] = createTestNPC();
    edm->initMemoryData(index);

    // Add more than INLINE_MEMORY_COUNT memories without overflow
    for (int i = 0; i < 10; ++i) {
        MemoryEntry entry;
        entry.type = MemoryType::ThreatSpotted;
        entry.timestamp = static_cast<float>(i);
        entry.flags = MemoryEntry::FLAG_VALID;
        edm->addMemory(index, entry, false);  // No overflow
    }

    auto& memData = edm->getMemoryData(index);
    // Should cap at INLINE_MEMORY_COUNT (circular buffer overwrites oldest)
    BOOST_CHECK_EQUAL(memData.memoryCount, NPCMemoryData::INLINE_MEMORY_COUNT);
    BOOST_CHECK(!memData.hasOverflow());
}

BOOST_AUTO_TEST_CASE(TestMemoryOverflow) {
    auto [handle, index] = createTestNPC();
    edm->initMemoryData(index);

    // Add more than INLINE_MEMORY_COUNT memories with overflow enabled
    for (int i = 0; i < 10; ++i) {
        MemoryEntry entry;
        entry.type = MemoryType::WitnessedCombat;
        entry.timestamp = static_cast<float>(i);
        entry.flags = MemoryEntry::FLAG_VALID;
        edm->addMemory(index, entry, true);  // Use overflow
    }

    auto& memData = edm->getMemoryData(index);
    BOOST_CHECK_EQUAL(memData.memoryCount, 10);
    BOOST_CHECK(memData.hasOverflow());

    // Verify overflow exists
    auto* overflow = edm->getMemoryOverflow(memData.overflowId);
    BOOST_CHECK(overflow != nullptr);
    BOOST_CHECK_EQUAL(overflow->extraMemories.size(), 10 - NPCMemoryData::INLINE_MEMORY_COUNT);
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// FIND MEMORY TESTS
// ============================================================================

BOOST_FIXTURE_TEST_SUITE(FindMemoryTests, NPCMemoryTestFixture)

BOOST_AUTO_TEST_CASE(TestFindMemoriesByType) {
    auto [handle, index] = createTestNPC();
    edm->initMemoryData(index);

    // Add mixed memory types
    for (int i = 0; i < 3; ++i) {
        MemoryEntry entry;
        entry.type = MemoryType::DamageReceived;
        entry.value = static_cast<float>(i * 10);
        entry.flags = MemoryEntry::FLAG_VALID;
        edm->addMemory(index, entry);
    }

    MemoryEntry otherEntry;
    otherEntry.type = MemoryType::Interaction;
    otherEntry.flags = MemoryEntry::FLAG_VALID;
    edm->addMemory(index, otherEntry);

    std::vector<const MemoryEntry*> results;
    edm->findMemoriesByType(index, MemoryType::DamageReceived, results);

    BOOST_CHECK_EQUAL(results.size(), 3);
    for (const auto* mem : results) {
        BOOST_CHECK_EQUAL(static_cast<int>(mem->type), static_cast<int>(MemoryType::DamageReceived));
    }
}

BOOST_AUTO_TEST_CASE(TestFindMemoriesByTypeWithLimit) {
    auto [handle, index] = createTestNPC();
    edm->initMemoryData(index);

    for (int i = 0; i < 5; ++i) {
        MemoryEntry entry;
        entry.type = MemoryType::ThreatSpotted;
        entry.flags = MemoryEntry::FLAG_VALID;
        edm->addMemory(index, entry);
    }

    std::vector<const MemoryEntry*> results;
    edm->findMemoriesByType(index, MemoryType::ThreatSpotted, results, 2);

    BOOST_CHECK_EQUAL(results.size(), 2);
}

BOOST_AUTO_TEST_CASE(TestFindMemoriesOfEntity) {
    auto [handle, index] = createTestNPC();
    auto [targetHandle, targetIndex] = createTestNPC(200.0f, 200.0f);
    edm->initMemoryData(index);

    // Add memories about the target
    MemoryEntry entry1;
    entry1.subject = targetHandle;
    entry1.type = MemoryType::AttackedBy;
    entry1.flags = MemoryEntry::FLAG_VALID;
    edm->addMemory(index, entry1);

    MemoryEntry entry2;
    entry2.subject = targetHandle;
    entry2.type = MemoryType::DamageReceived;
    entry2.flags = MemoryEntry::FLAG_VALID;
    edm->addMemory(index, entry2);

    // Add memory about different entity
    MemoryEntry entry3;
    entry3.subject = EntityHandle{9999, EntityKind::NPC, 1};
    entry3.type = MemoryType::AllySpotted;
    entry3.flags = MemoryEntry::FLAG_VALID;
    edm->addMemory(index, entry3);

    std::vector<const MemoryEntry*> results;
    edm->findMemoriesOfEntity(index, targetHandle, results);

    BOOST_CHECK_EQUAL(results.size(), 2);
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// EMOTIONAL STATE TESTS
// ============================================================================

BOOST_FIXTURE_TEST_SUITE(EmotionalStateTests, NPCMemoryTestFixture)

BOOST_AUTO_TEST_CASE(TestModifyEmotions) {
    auto [handle, index] = createTestNPC();
    edm->initMemoryData(index);

    edm->modifyEmotions(index, 0.5f, 0.3f, 0.2f, 0.1f);

    auto& memData = edm->getMemoryData(index);
    BOOST_CHECK(approxEqual(memData.emotions.aggression, 0.5f));
    BOOST_CHECK(approxEqual(memData.emotions.fear, 0.3f));
    BOOST_CHECK(approxEqual(memData.emotions.curiosity, 0.2f));
    BOOST_CHECK(approxEqual(memData.emotions.suspicion, 0.1f));
}

BOOST_AUTO_TEST_CASE(TestEmotionsClamping) {
    auto [handle, index] = createTestNPC();
    edm->initMemoryData(index);

    // Try to set emotions above 1.0
    edm->modifyEmotions(index, 2.0f, 2.0f, 2.0f, 2.0f);

    auto& memData = edm->getMemoryData(index);
    BOOST_CHECK(approxEqual(memData.emotions.aggression, 1.0f));
    BOOST_CHECK(approxEqual(memData.emotions.fear, 1.0f));

    // Try to set emotions below 0.0
    edm->modifyEmotions(index, -3.0f, -3.0f, -3.0f, -3.0f);

    BOOST_CHECK(approxEqual(memData.emotions.aggression, 0.0f));
    BOOST_CHECK(approxEqual(memData.emotions.fear, 0.0f));
}

BOOST_AUTO_TEST_CASE(TestUpdateEmotionalDecay) {
    auto [handle, index] = createTestNPC();
    edm->initMemoryData(index);

    edm->modifyEmotions(index, 1.0f, 1.0f, 1.0f, 1.0f);

    // Decay over 2 seconds at 5% per second
    edm->updateEmotionalDecay(index, 2.0f, 0.05f);

    auto& memData = edm->getMemoryData(index);
    // 1.0 * (1 - 0.05 * 2) = 0.9
    BOOST_CHECK(approxEqual(memData.emotions.aggression, 0.9f));
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// COMBAT EVENT TESTS
// ============================================================================

BOOST_FIXTURE_TEST_SUITE(CombatEventTests, NPCMemoryTestFixture)

BOOST_AUTO_TEST_CASE(TestRecordCombatEventReceived) {
    auto [handle, index] = createTestNPC();
    EntityHandle attacker{999, EntityKind::Player, 1};

    edm->recordCombatEvent(index, attacker, handle, 25.0f, true, 10.0f);

    auto& memData = edm->getMemoryData(index);
    BOOST_CHECK(memData.isValid());
    BOOST_CHECK(memData.lastAttacker == attacker);
    BOOST_CHECK(approxEqual(memData.totalDamageReceived, 25.0f));
    BOOST_CHECK_EQUAL(memData.combatEncounters, 1);
    BOOST_CHECK(memData.isInCombat());
    BOOST_CHECK(memData.emotions.fear > 0.0f);  // Fear increased from damage
}

BOOST_AUTO_TEST_CASE(TestRecordCombatEventDealt) {
    auto [handle, index] = createTestNPC();
    EntityHandle target{888, EntityKind::NPC, 1};

    edm->recordCombatEvent(index, handle, target, 30.0f, false, 15.0f);

    auto& memData = edm->getMemoryData(index);
    BOOST_CHECK(memData.lastTarget == target);
    BOOST_CHECK(approxEqual(memData.totalDamageDealt, 30.0f));
    BOOST_CHECK(memData.emotions.aggression > 0.0f);  // Aggression increased from combat
}

BOOST_AUTO_TEST_CASE(TestMultipleCombatEvents) {
    auto [handle, index] = createTestNPC();
    EntityHandle attacker{999, EntityKind::Player, 1};

    edm->recordCombatEvent(index, attacker, handle, 10.0f, true, 1.0f);
    edm->recordCombatEvent(index, attacker, handle, 15.0f, true, 2.0f);
    edm->recordCombatEvent(index, attacker, handle, 20.0f, true, 3.0f);

    auto& memData = edm->getMemoryData(index);
    BOOST_CHECK(approxEqual(memData.totalDamageReceived, 45.0f));
    BOOST_CHECK_EQUAL(memData.combatEncounters, 3);
    BOOST_CHECK(approxEqual(memData.lastCombatTime, 3.0f));
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// LOCATION HISTORY TESTS
// ============================================================================

BOOST_FIXTURE_TEST_SUITE(LocationHistoryTests, NPCMemoryTestFixture)

BOOST_AUTO_TEST_CASE(TestAddLocationToHistory) {
    auto [handle, index] = createTestNPC();
    edm->initMemoryData(index);

    edm->addLocationToHistory(index, Vector2D{100.0f, 100.0f});
    edm->addLocationToHistory(index, Vector2D{200.0f, 200.0f});

    auto& memData = edm->getMemoryData(index);
    BOOST_CHECK_EQUAL(memData.locationCount, 2);
}

BOOST_AUTO_TEST_CASE(TestLocationHistoryCircular) {
    auto [handle, index] = createTestNPC();
    edm->initMemoryData(index);

    // Add more locations than INLINE_LOCATION_COUNT
    for (int i = 0; i < 10; ++i) {
        edm->addLocationToHistory(index, Vector2D{static_cast<float>(i * 100), 0.0f});
    }

    auto& memData = edm->getMemoryData(index);
    // Should cap at INLINE_LOCATION_COUNT
    BOOST_CHECK_EQUAL(memData.locationCount, NPCMemoryData::INLINE_LOCATION_COUNT);
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// ENTITY DESTRUCTION CLEANUP TESTS
// ============================================================================

BOOST_FIXTURE_TEST_SUITE(CleanupTests, NPCMemoryTestFixture)

BOOST_AUTO_TEST_CASE(TestMemoryClearedOnEntityDestruction) {
    auto [handle, index] = createTestNPC();
    edm->initMemoryData(index);

    // Add some memories with overflow
    for (int i = 0; i < 15; ++i) {
        MemoryEntry entry;
        entry.type = MemoryType::WitnessedCombat;
        entry.flags = MemoryEntry::FLAG_VALID;
        edm->addMemory(index, entry, true);
    }

    auto& memData = edm->getMemoryData(index);
    uint32_t overflowId = memData.overflowId;
    BOOST_CHECK(memData.hasOverflow());

    // Destroy the entity
    edm->destroyEntity(handle);
    edm->processDestructionQueue();

    // Overflow should be cleaned up
    auto* overflow = edm->getMemoryOverflow(overflowId);
    BOOST_CHECK(overflow == nullptr);
}

BOOST_AUTO_TEST_SUITE_END()
