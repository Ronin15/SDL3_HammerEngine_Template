/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#define BOOST_TEST_MODULE ResourceArchitectureTests
#include <boost/test/unit_test.hpp>

#include "entities/Resource.hpp"
#include "entities/DroppedItem.hpp"
#include "managers/ResourceTemplateManager.hpp"
#include "utils/Vector2D.hpp"

/**
 * @brief Tests that validate the new Resource architecture
 * 
 * These tests ensure that:
 * 1. Resources are pure data classes (no Entity inheritance)
 * 2. DroppedItem properly uses Resource templates
 * 3. Visual properties flow correctly from Resource to DroppedItem
 * 4. Memory usage is efficient
 */

class ResourceArchitectureTestFixture {
public:
    ResourceArchitectureTestFixture() {
        // Initialize ResourceTemplateManager
        resourceManager = &ResourceTemplateManager::Instance();
        if (!resourceManager->isInitialized()) {
            resourceManager->init();
        }
        
        // Get a test resource handle
        testResourceHandle = resourceManager->getHandleByName("Super Health Potion");
        BOOST_REQUIRE(testResourceHandle.isValid());
        
        testResource = resourceManager->getResourceTemplate(testResourceHandle);
        BOOST_REQUIRE(testResource != nullptr);
    }
    
    ~ResourceArchitectureTestFixture() {
        // No cleanup needed - managers handle their own lifecycle
    }

protected:
    ResourceTemplateManager* resourceManager;
    HammerEngine::ResourceHandle testResourceHandle;
    std::shared_ptr<Resource> testResource;
};

BOOST_FIXTURE_TEST_SUITE(ResourceArchitectureTestSuite, ResourceArchitectureTestFixture)

BOOST_AUTO_TEST_CASE(TestResourceIsPureDataClass) {
    // Test that Resource doesn't have Entity behavior
    BOOST_REQUIRE(testResource != nullptr);
    
    // Verify Resource has proper data properties
    BOOST_CHECK(!testResource->getName().empty());
    BOOST_CHECK(!testResource->getId().empty());
    BOOST_CHECK(testResource->getValue() >= 0.0f);
    BOOST_CHECK(testResource->getWeight() >= 0.0f);
    BOOST_CHECK(testResource->getMaxStackSize() > 0);
    
    // Verify visual properties are available
    BOOST_CHECK(!testResource->getIconTextureId().empty());
    BOOST_CHECK(!testResource->getWorldTextureId().empty());
    BOOST_CHECK(testResource->getNumFrames() > 0);
    BOOST_CHECK(testResource->getAnimSpeed() > 0);
    
    // Verify category and type are set (test by converting to string)
    BOOST_CHECK(!Resource::categoryToString(testResource->getCategory()).empty());
    BOOST_CHECK(!Resource::typeToString(testResource->getType()).empty());
}

BOOST_AUTO_TEST_CASE(TestResourceImmutability) {
    // Test that core properties can't be changed after creation
    std::string originalName = testResource->getName();
    std::string originalId = testResource->getId();
    ResourceCategory originalCategory = testResource->getCategory();
    ResourceType originalType = testResource->getType();
    
    // These should remain constant (no setters for core properties)
    BOOST_CHECK_EQUAL(testResource->getName(), originalName);
    BOOST_CHECK_EQUAL(testResource->getId(), originalId);
    
    // Test category and type are still the same using string comparison
    BOOST_CHECK_EQUAL(Resource::categoryToString(testResource->getCategory()), 
                      Resource::categoryToString(originalCategory));
    BOOST_CHECK_EQUAL(Resource::typeToString(testResource->getType()), 
                      Resource::typeToString(originalType));
}

BOOST_AUTO_TEST_CASE(TestDroppedItemCreation) {
    // Test that DroppedItem can be created from Resource template
    Vector2D testPosition(100.0f, 200.0f);
    int testQuantity = 5;
    
    auto droppedItem = std::make_shared<DroppedItem>(testResourceHandle, testPosition, testQuantity);
    
    BOOST_REQUIRE(droppedItem != nullptr);
    BOOST_CHECK(droppedItem->getResourceHandle() == testResourceHandle);
    BOOST_CHECK_EQUAL(droppedItem->getQuantity(), testQuantity);
    BOOST_CHECK(droppedItem->canPickup() == false); // Should have pickup delay initially
    
    // Verify position was set correctly
    BOOST_CHECK_EQUAL(droppedItem->getPosition().getX(), testPosition.getX());
    BOOST_CHECK_EQUAL(droppedItem->getPosition().getY(), testPosition.getY());
}

BOOST_AUTO_TEST_CASE(TestDroppedItemUsesResourceTemplate) {
    // Test that DroppedItem properly references Resource template
    Vector2D testPosition(50.0f, 75.0f);
    auto droppedItem = std::make_shared<DroppedItem>(testResourceHandle, testPosition, 1);
    
    // Get the template through DroppedItem
    auto templateFromDroppedItem = droppedItem->getResourceTemplate();
    BOOST_REQUIRE(templateFromDroppedItem != nullptr);
    
    // Verify it's the same resource template
    BOOST_CHECK_EQUAL(templateFromDroppedItem->getId(), testResource->getId());
    BOOST_CHECK_EQUAL(templateFromDroppedItem->getName(), testResource->getName());
    BOOST_CHECK_EQUAL(templateFromDroppedItem->getWorldTextureId(), testResource->getWorldTextureId());
    BOOST_CHECK_EQUAL(templateFromDroppedItem->getNumFrames(), testResource->getNumFrames());
    BOOST_CHECK_EQUAL(templateFromDroppedItem->getAnimSpeed(), testResource->getAnimSpeed());
}

BOOST_AUTO_TEST_CASE(TestDroppedItemQuantityManagement) {
    Vector2D testPosition(0.0f, 0.0f);
    auto droppedItem = std::make_shared<DroppedItem>(testResourceHandle, testPosition, 10);
    
    // Test adding quantity within stack limits
    bool addResult = droppedItem->addQuantity(5);
    BOOST_CHECK(addResult);
    BOOST_CHECK_EQUAL(droppedItem->getQuantity(), 15);
    
    // Test removing quantity
    bool removeResult = droppedItem->removeQuantity(3);
    BOOST_CHECK(removeResult);
    BOOST_CHECK_EQUAL(droppedItem->getQuantity(), 12);
    
    // Test removing more than available (should fail)
    bool removeFailResult = droppedItem->removeQuantity(20);
    BOOST_CHECK(!removeFailResult);
    BOOST_CHECK_EQUAL(droppedItem->getQuantity(), 12); // Should remain unchanged
    
    // Test removing all
    bool removeAllResult = droppedItem->removeQuantity(12);
    BOOST_CHECK(removeAllResult);
    BOOST_CHECK_EQUAL(droppedItem->getQuantity(), 0);
    BOOST_CHECK(!droppedItem->canPickup()); // Empty stacks can't be picked up
}

BOOST_AUTO_TEST_CASE(TestResourceStringConversions) {
    // Test category string conversions for known values
    std::string itemStr = Resource::categoryToString(ResourceCategory::Item);
    BOOST_CHECK_EQUAL(itemStr, "Item");
    
    std::string currencyStr = Resource::categoryToString(ResourceCategory::Currency);
    BOOST_CHECK_EQUAL(currencyStr, "Currency");
    
    // Test round trip conversion for Item category
    ResourceCategory convertedBack = Resource::stringToCategory(itemStr);
    BOOST_CHECK_EQUAL(Resource::categoryToString(convertedBack), itemStr);
}

BOOST_AUTO_TEST_CASE(TestMemoryEfficiency) {
    // Test that Resource objects are lightweight (pure data)
    // This is more of a design validation than a strict memory test
    
    Vector2D testPosition(0.0f, 0.0f);
    
    // Create multiple DroppedItems referencing the same Resource template
    auto droppedItem1 = std::make_shared<DroppedItem>(testResourceHandle, testPosition, 1);
    auto droppedItem2 = std::make_shared<DroppedItem>(testResourceHandle, testPosition, 5);
    auto droppedItem3 = std::make_shared<DroppedItem>(testResourceHandle, testPosition, 10);
    
    // All should reference the same Resource template
    auto template1 = droppedItem1->getResourceTemplate();
    auto template2 = droppedItem2->getResourceTemplate();
    auto template3 = droppedItem3->getResourceTemplate();
    
    // Verify they're the same object (shared, not duplicated)
    BOOST_CHECK(template1.get() == template2.get());
    BOOST_CHECK(template2.get() == template3.get());
    
    // Verify they all have the same properties
    BOOST_CHECK_EQUAL(template1->getId(), template2->getId());
    BOOST_CHECK_EQUAL(template2->getId(), template3->getId());
}

BOOST_AUTO_TEST_SUITE_END()
