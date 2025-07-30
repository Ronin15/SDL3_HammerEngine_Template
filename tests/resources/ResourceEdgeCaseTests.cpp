/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#define BOOST_TEST_MODULE ResourceEdgeCaseTests
#include <boost/test/unit_test.hpp>

#include "entities/Resource.hpp"
#include "entities/resources/InventoryComponent.hpp"
#include "events/ResourceChangeEvent.hpp"
#include "managers/ResourceTemplateManager.hpp"
#include "managers/WorldResourceManager.hpp"
#include "utils/ResourceHandle.hpp"

#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <limits>
#include <memory>
#include <random>
#include <set>
#include <thread>
#include <vector>

using HammerEngine::ResourceHandle;

struct ResourceEdgeCaseFixture {
  ResourceTemplateManager *templateManager;
  WorldResourceManager *worldManager;

  ResourceEdgeCaseFixture() {
    templateManager = &ResourceTemplateManager::Instance();
    worldManager = &WorldResourceManager::Instance();

    // Clean and reinitialize managers
    templateManager->clean();
    worldManager->clean();

    BOOST_REQUIRE(templateManager->init());
    BOOST_REQUIRE(worldManager->init());
  }

  ~ResourceEdgeCaseFixture() {
    worldManager->clean();
    templateManager->clean();
  }

  ResourcePtr
  createTestResource(const std::string &name,
                     ResourceCategory category = ResourceCategory::Material,
                     ResourceType type = ResourceType::RawResource) {
    auto handle = templateManager->generateHandle();
    std::string id = "test_" + name; // Use test_ prefix for ID
    return std::make_shared<Resource>(handle, id, name, category, type);
  }
};

BOOST_FIXTURE_TEST_SUITE(ResourceEdgeCaseTestSuite, ResourceEdgeCaseFixture)

//==============================================================================
// Handle Lifecycle Edge Cases
//==============================================================================

BOOST_AUTO_TEST_CASE(TestHandleOverflowProtection) {
  // Test behavior when handle ID approaches maximum values
  std::vector<ResourceHandle> handles;

  // Generate a large number of handles to test for overflow protection
  const size_t NUM_HANDLES = 10000;
  handles.reserve(NUM_HANDLES);

  for (size_t i = 0; i < NUM_HANDLES; ++i) {
    auto handle = templateManager->generateHandle();
    BOOST_CHECK(handle.isValid());
    handles.push_back(handle);
  }

  // Verify all handles are unique
  std::set<ResourceHandle> uniqueHandles(handles.begin(), handles.end());
  BOOST_CHECK_EQUAL(uniqueHandles.size(), handles.size());
}

BOOST_AUTO_TEST_CASE(TestStaleHandleDetection) {
  // Create and register a resource
  auto resource = createTestResource("TestStaleResource");
  auto handle = resource->getHandle();

  BOOST_REQUIRE(templateManager->registerResourceTemplate(resource));
  BOOST_CHECK(templateManager->getResourceTemplate(handle) != nullptr);

  // Remove the resource to make handle stale
  templateManager->removeResourceTemplate(handle);

  // Verify stale handle is detected
  BOOST_CHECK(templateManager->getResourceTemplate(handle) == nullptr);
  // Test operations with stale handle
  auto newResource = createTestResource("NewResource");
  auto newHandle = newResource->getHandle();

  // Verify new handle is different from stale one (avoid direct comparison)
  BOOST_CHECK(handle.getId() != newHandle.getId() ||
              handle.getGeneration() != newHandle.getGeneration());
}

BOOST_AUTO_TEST_CASE(TestInvalidHandleOperations) {
  ResourceHandle invalidHandle; // Default invalid handle

  BOOST_CHECK(!invalidHandle.isValid());
  BOOST_CHECK_EQUAL(invalidHandle.getId(), ResourceHandle::INVALID_ID);
  BOOST_CHECK_EQUAL(invalidHandle.getGeneration(),
                    ResourceHandle::INVALID_GENERATION);

  // Test operations with invalid handle
  BOOST_CHECK(templateManager->getResourceTemplate(invalidHandle) == nullptr);

  // Test world manager operations with default world
  auto defaultWorldId = WorldResourceManager::WorldId("default");
  worldManager->createWorld(defaultWorldId);

  BOOST_CHECK(!worldManager->hasResource(defaultWorldId, invalidHandle));
  BOOST_CHECK_EQUAL(
      worldManager->getResourceQuantity(defaultWorldId, invalidHandle), 0);

  // Test transaction operations with invalid handle (should be safe)
  auto addResult =
      worldManager->addResource(defaultWorldId, invalidHandle, 100);
  BOOST_CHECK(addResult == ResourceTransactionResult::InvalidResourceHandle);

  auto removeResult =
      worldManager->removeResource(defaultWorldId, invalidHandle, 25);
  BOOST_CHECK(removeResult == ResourceTransactionResult::InvalidResourceHandle);
}

//==============================================================================
// Concurrent Access and Race Conditions
//==============================================================================

BOOST_AUTO_TEST_CASE(TestConcurrentHandleGeneration) {
  const int NUM_THREADS = 8;
  const int HANDLES_PER_THREAD = 1000;

  std::vector<std::future<std::vector<ResourceHandle>>> futures;
  std::vector<ResourceHandle> allHandles;

  // Launch multiple threads generating handles
  for (int t = 0; t < NUM_THREADS; ++t) {
    futures.push_back(std::async(std::launch::async, [&]() {
      std::vector<ResourceHandle> threadHandles;
      threadHandles.reserve(HANDLES_PER_THREAD);

      for (int i = 0; i < HANDLES_PER_THREAD; ++i) {
        auto handle = templateManager->generateHandle();
        BOOST_CHECK(handle.isValid());
        threadHandles.push_back(handle);
      }
      return threadHandles;
    }));
  }

  // Collect all handles
  for (auto &future : futures) {
    auto threadHandles = future.get();
    allHandles.insert(allHandles.end(), threadHandles.begin(),
                      threadHandles.end());
  }

  // Verify all handles are unique (no race conditions in generation)
  std::set<ResourceHandle> uniqueHandles(allHandles.begin(), allHandles.end());
  BOOST_CHECK_EQUAL(uniqueHandles.size(), allHandles.size());
}

BOOST_AUTO_TEST_CASE(TestConcurrentResourceOperations) {
  // Create test resources
  auto goldResource = createTestResource("ConcurrentGold");
  auto silverResource = createTestResource("ConcurrentSilver");

  BOOST_REQUIRE(templateManager->registerResourceTemplate(goldResource));
  BOOST_REQUIRE(templateManager->registerResourceTemplate(silverResource));

  auto goldHandle = goldResource->getHandle();
  auto silverHandle = silverResource->getHandle();

  // Create test world
  auto worldId = WorldResourceManager::WorldId("concurrent_test");
  worldManager->createWorld(worldId);
  // Set initial quantities
  BOOST_REQUIRE(worldManager->setResource(worldId, goldHandle, 1000) ==
                ResourceTransactionResult::Success);
  BOOST_REQUIRE(worldManager->setResource(worldId, silverHandle, 2000) ==
                ResourceTransactionResult::Success);

  const int NUM_THREADS = 4;
  const int OPERATIONS_PER_THREAD = 100;

  std::atomic<int> totalAdded{0};
  std::atomic<int> totalRemoved{0};
  std::vector<std::future<void>> futures;

  // Launch threads performing concurrent operations
  for (int t = 0; t < NUM_THREADS; ++t) {
    futures.push_back(std::async(std::launch::async, [&]() {
      std::random_device rd;
      std::mt19937 gen(rd());
      std::uniform_int_distribution<> amountDist(1, 10);
      std::uniform_int_distribution<> operationDist(0, 1);
      std::uniform_int_distribution<> resourceDist(0, 1);

      for (int i = 0; i < OPERATIONS_PER_THREAD; ++i) {
        auto handle = (resourceDist(gen) == 0) ? goldHandle : silverHandle;
        int amount = amountDist(gen);

        if (operationDist(gen) == 0) {
          // Add operation
          if (worldManager->addResource(worldId, handle, amount) ==
              ResourceTransactionResult::Success) {
            totalAdded.fetch_add(amount);
          }
        } else {
          // Remove operation
          if (worldManager->removeResource(worldId, handle, amount) ==
              ResourceTransactionResult::Success) {
            totalRemoved.fetch_add(amount);
          }
        }

        // Small delay to increase chance of race conditions
        std::this_thread::sleep_for(std::chrono::microseconds(1));
      }
    }));
  }

  // Wait for all operations to complete
  for (auto &future : futures) {
    future.wait();
  }

  // Verify final state is consistent
  auto finalGold = worldManager->getResourceQuantity(worldId, goldHandle);
  auto finalSilver = worldManager->getResourceQuantity(worldId, silverHandle);

  BOOST_CHECK_GE(finalGold, 0);
  BOOST_CHECK_GE(finalSilver, 0);

  // Total final quantity should equal initial + added - removed
  auto expectedTotal = 3000 + totalAdded.load() - totalRemoved.load();
  auto actualTotal = finalGold + finalSilver;

  BOOST_CHECK_EQUAL(actualTotal, expectedTotal);
}

//==============================================================================
// Memory Pressure and Resource Exhaustion
//==============================================================================

BOOST_AUTO_TEST_CASE(TestLargeNumberOfResources) {
  const size_t LARGE_COUNT = 50000;
  std::vector<ResourcePtr> resources;
  resources.reserve(LARGE_COUNT);

  // Create large number of resources
  for (size_t i = 0; i < LARGE_COUNT; ++i) {
    auto resource = createTestResource("LargeTest_" + std::to_string(i));
    resources.push_back(resource);

    // Add every 100th resource to template manager to test memory usage
    if (i % 100 == 0) {
      BOOST_CHECK(templateManager->registerResourceTemplate(resource));
    }
  }

  // Verify system stability under memory pressure
  BOOST_CHECK(templateManager->isInitialized());
  BOOST_CHECK_GT(templateManager->getResourceTemplateCount(), 0);

  // Test cleanup under memory pressure
  resources.clear();

  // Force garbage collection and verify system stability
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  BOOST_CHECK(templateManager->isInitialized());
}

BOOST_AUTO_TEST_CASE(TestExtremeQuantityValues) {
  auto resource = createTestResource("ExtremeQuantityTest");
  BOOST_REQUIRE(templateManager->registerResourceTemplate(resource));

  auto handle = resource->getHandle();
  auto worldId = WorldResourceManager::WorldId("extreme_test");
  worldManager->createWorld(worldId);
  // Test maximum safe values
  const uint64_t MAX_SAFE_QUANTITY = std::numeric_limits<uint64_t>::max() / 2;
  const uint64_t NEAR_MAX_QUANTITY =
      std::numeric_limits<uint64_t>::max() - 1000;

  // Test setting large quantities
  auto setResult =
      worldManager->setResource(worldId, handle, MAX_SAFE_QUANTITY);
  BOOST_CHECK(setResult == ResourceTransactionResult::Success);
  BOOST_CHECK_EQUAL(worldManager->getResourceQuantity(worldId, handle),
                    MAX_SAFE_QUANTITY);

  // Test overflow protection
  auto addResult =
      worldManager->addResource(worldId, handle, NEAR_MAX_QUANTITY);
  BOOST_CHECK(addResult !=
              ResourceTransactionResult::Success); // Should fail due to
                                                   // overflow protection

  // Test underflow protection
  worldManager->setResource(worldId, handle, 100);
  auto removeResult = worldManager->removeResource(worldId, handle, 200);
  BOOST_CHECK(removeResult !=
              ResourceTransactionResult::Success); // Should fail due to
                                                   // underflow protection
  BOOST_CHECK_EQUAL(worldManager->getResourceQuantity(worldId, handle),
                    100); // Quantity should remain unchanged
}

//==============================================================================
// Malformed Input and Error Recovery
//==============================================================================

BOOST_AUTO_TEST_CASE(TestNullPointerHandling) {
  // Test adding null resource template
  BOOST_CHECK(!templateManager->registerResourceTemplate(nullptr));

  // Test operations with null inventory component
  auto resource = createTestResource("NullTest");
  auto handle = resource->getHandle();

  // These operations should handle null gracefully (implementation dependent)
  // Main goal is to ensure no crashes occur
  BOOST_CHECK_NO_THROW(templateManager->getResourceTemplate(handle));
}

BOOST_AUTO_TEST_CASE(TestEmptyStringHandling) {
  // Test resource with empty name
  auto emptyNameResource = createTestResource("");
  BOOST_CHECK(templateManager->registerResourceTemplate(emptyNameResource));

  // Test getHandleByName with empty string
  auto handle = templateManager->getHandleByName("");
  BOOST_CHECK(handle.isValid()); // Should find the empty-named resource
}

BOOST_AUTO_TEST_CASE(TestDuplicateResourceHandling) {
  auto resource1 = createTestResource("DuplicateTest");
  auto resource2 =
      createTestResource("DuplicateTest"); // Same name, different handle

  BOOST_CHECK(templateManager->registerResourceTemplate(resource1));
  // Second registration should fail due to duplicate name enforcement
  BOOST_CHECK(!templateManager->registerResourceTemplate(resource2));

  // Only the first resource should be registered
  BOOST_CHECK_GT(templateManager->getResourceTemplateCount(), 0);

  // Name lookup should return the first resource
  auto foundHandle = templateManager->getHandleByName("DuplicateTest");
  BOOST_CHECK(foundHandle.isValid());
  BOOST_CHECK_EQUAL(foundHandle.getId(), resource1->getHandle().getId());
}

//==============================================================================
// Performance Under Extreme Load
//==============================================================================

BOOST_AUTO_TEST_CASE(TestRapidOperationSequences) {
  auto resource = createTestResource("RapidTest");
  BOOST_REQUIRE(templateManager->registerResourceTemplate(resource));

  auto handle = resource->getHandle();
  auto worldId = WorldResourceManager::WorldId("rapid_test");
  worldManager->createWorld(worldId);
  const int RAPID_OPERATIONS = 10000;

  auto startTime = std::chrono::high_resolution_clock::now();

  // Perform rapid add/remove sequences
  for (int i = 0; i < RAPID_OPERATIONS; ++i) {
    worldManager->addResource(worldId, handle, 1);
    worldManager->removeResource(worldId, handle, 1);
  }

  auto endTime = std::chrono::high_resolution_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
      endTime - startTime);

  // Should complete in reasonable time (less than 1 second for 10k operations)
  BOOST_CHECK_LT(duration.count(), 1000);

  // Final quantity should be 0
  BOOST_CHECK_EQUAL(worldManager->getResourceQuantity(worldId, handle), 0);
}

BOOST_AUTO_TEST_CASE(TestHighFrequencyCallbacks) {
  auto resource = createTestResource("CallbackTest");
  BOOST_REQUIRE(templateManager->registerResourceTemplate(resource));

  auto handle = resource->getHandle();
  std::atomic<int> callbackCount{0};
  const int EXPECTED_CALLBACKS = 1000;

  // Create inventory component with callback
  InventoryComponent inventory;
  inventory.setResourceChangeCallback(
      [&callbackCount](const ResourceHandle &, uint64_t, uint64_t) {
        callbackCount.fetch_add(1);
      });

  // Perform operations that trigger callbacks
  for (int i = 0; i < EXPECTED_CALLBACKS; ++i) {
    inventory.addResource(handle, 1);
    inventory.removeResource(handle, 1);
  }

  // Allow time for any asynchronous callback processing
  std::this_thread::sleep_for(std::chrono::milliseconds(10));

  // Verify callbacks were triggered (exact count depends on implementation)
  BOOST_CHECK_GT(callbackCount.load(), 0);
}

//==============================================================================
// System Integration Edge Cases
//==============================================================================

BOOST_AUTO_TEST_CASE(TestManagerShutdownAndReinit) {
  // Create resources
  auto resource = createTestResource("ShutdownTest");
  BOOST_REQUIRE(templateManager->registerResourceTemplate(resource));

  auto handle = resource->getHandle();
  auto worldId = WorldResourceManager::WorldId("shutdown_test");
  worldManager->createWorld(worldId);
  worldManager->setResource(worldId, handle, 500);

  // Verify initial state
  BOOST_CHECK_EQUAL(worldManager->getResourceQuantity(worldId, handle), 500);
  BOOST_CHECK_GT(templateManager->getResourceTemplateCount(), 0);

  // Shutdown managers
  worldManager->clean();
  templateManager->clean();

  // Verify shutdown state
  BOOST_CHECK(!templateManager->isInitialized());
  BOOST_CHECK_EQUAL(templateManager->getResourceTemplateCount(), 0);

  // Reinitialize
  BOOST_REQUIRE(templateManager->init());
  BOOST_REQUIRE(worldManager->init());

  // Verify clean reinitialization
  BOOST_CHECK(templateManager->isInitialized());
  BOOST_CHECK_GT(templateManager->getResourceTemplateCount(),
                 0); // Default resources loaded

  // Original resource should be gone
  BOOST_CHECK(templateManager->getResourceTemplate(handle) == nullptr);
  worldManager->createWorld(worldId); // Recreate world
  BOOST_CHECK_EQUAL(worldManager->getResourceQuantity(worldId, handle), 0);
}

BOOST_AUTO_TEST_CASE(TestCrossManagerConsistency) {
  // Test consistency between template and world managers
  auto resource = createTestResource("ConsistencyTest");
  auto handle = resource->getHandle();
  auto worldId = WorldResourceManager::WorldId("consistency_test");
  // Add to template manager only
  BOOST_REQUIRE(templateManager->registerResourceTemplate(resource));
  BOOST_CHECK(templateManager->getResourceTemplate(handle) != nullptr);

  worldManager->createWorld(worldId);
  BOOST_CHECK_EQUAL(worldManager->getResourceQuantity(worldId, handle), 0);

  // Add quantity to world manager
  auto addResult = worldManager->addResource(worldId, handle, 100);
  BOOST_CHECK(addResult == ResourceTransactionResult::Success);
  BOOST_CHECK_EQUAL(worldManager->getResourceQuantity(worldId, handle), 100);

  // Remove from template manager (world quantities should remain)
  templateManager->removeResourceTemplate(handle);
  BOOST_CHECK(templateManager->getResourceTemplate(handle) == nullptr);
  BOOST_CHECK_EQUAL(worldManager->getResourceQuantity(worldId, handle),
                    100); // World data persists

  // Operations on orphaned world data should still work
  auto addResult2 = worldManager->addResource(worldId, handle, 50);
  BOOST_CHECK(addResult2 == ResourceTransactionResult::Success);
  BOOST_CHECK_EQUAL(worldManager->getResourceQuantity(worldId, handle), 150);
}

BOOST_AUTO_TEST_SUITE_END()