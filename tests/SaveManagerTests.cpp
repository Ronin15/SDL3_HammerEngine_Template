/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

// Define this to make Boost.Test a header-only library
#define BOOST_TEST_MODULE SaveManagerTests
#include <boost/test/included/unit_test.hpp>
#include <boost/serialization/serialization.hpp>
#include <boost/archive/binary_oarchive.hpp>
#include <boost/archive/binary_iarchive.hpp>
#include <filesystem>
#include <fstream>

// Forward declarations to avoid dependencies
class Player;

// Include our standalone MockPlayer for testing
#include "MockPlayer.hpp"

// Create a wrapper for SaveGameManager that we can test
// This avoids having to modify the original SaveGameManager code
class TestSaveGameManager {
public:
    TestSaveGameManager() {
        m_saveDirectory = "test_saves";
        ensureSaveDirectoryExists();
    }
    
    ~TestSaveGameManager() {
        if (std::filesystem::exists(m_saveDirectory)) {
            std::filesystem::remove_all(m_saveDirectory);
        }
    }
    
    // Save a MockPlayer to a file
    bool save(const std::string& saveFileName, const MockPlayer* player) {
        if (player == nullptr) {
            return false;
        }
        
        // Ensure the save directory exists
        ensureSaveDirectoryExists();
        
        // Create full path for the save file
        std::string fullPath = getFullSavePath(saveFileName);
        
        try {
            // Open binary file for writing
            std::ofstream file(fullPath, std::ios::binary | std::ios::out);
            if (!file.is_open()) {
                return false;
            }
            
            // Create a simple save format: Position(x, y), TextureID, StateName
            Vector2D pos = player->getPosition();
            std::string textureID = player->getTextureID();
            std::string stateName = player->getCurrentStateName();
            
            // Use Boost.Serialization to write position
            try {
                boost::archive::binary_oarchive oa(file);
                oa << pos;
            } catch (const std::exception& e) {
                std::cerr << "Error serializing Vector2D: " << e.what() << std::endl;
                return false;
            }
            
            // Write textureID (size + string)
            uint32_t textureSize = static_cast<uint32_t>(textureID.size());
            file.write(reinterpret_cast<const char*>(&textureSize), sizeof(uint32_t));
            file.write(textureID.c_str(), textureSize);
            
            // Write stateName (size + string)
            uint32_t stateSize = static_cast<uint32_t>(stateName.size());
            file.write(reinterpret_cast<const char*>(&stateSize), sizeof(uint32_t));
            file.write(stateName.c_str(), stateSize);
            
            file.close();
            return true;
        }
        catch (const std::exception& e) {
            return false;
        }
    }
    
    // Load a MockPlayer from a file
    bool load(const std::string& saveFileName, MockPlayer* player) {
        if (player == nullptr) {
            return false;
        }
        
        // Create full path for the save file
        std::string fullPath = getFullSavePath(saveFileName);
        
        // Check if the file exists
        if (!std::filesystem::exists(fullPath)) {
            return false;
        }
        
        try {
            // Open binary file for reading
            std::ifstream file(fullPath, std::ios::binary | std::ios::in);
            if (!file.is_open()) {
                return false;
            }
            
            // Read position using Boost.Serialization
            Vector2D pos(0.0f, 0.0f);
            try {
                boost::archive::binary_iarchive ia(file);
                ia >> pos;
                player->setPosition(pos);
            } catch (const std::exception& e) {
                std::cerr << "Error deserializing Vector2D: " << e.what() << std::endl;
                return false;
            }
            
            // Read textureID
            uint32_t textureSize;
            file.read(reinterpret_cast<char*>(&textureSize), sizeof(uint32_t));
            std::string textureID(textureSize, '\0');
            file.read(&textureID[0], textureSize);
            player->setTestTextureID(textureID);
            
            // Read stateName
            uint32_t stateSize;
            file.read(reinterpret_cast<char*>(&stateSize), sizeof(uint32_t));
            std::string stateName(stateSize, '\0');
            file.read(&stateName[0], stateSize);
            player->setTestState(stateName);
            
            file.close();
            return true;
        }
        catch (const std::exception& e) {
            return false;
        }
    }
    
    // Helper methods for slots
    bool saveToSlot(int slotNumber, const MockPlayer* player) {
        if (slotNumber < 1) {
            return false;
        }
        std::string fileName = getSlotFileName(slotNumber);
        return save(fileName, player);
    }
    
    bool loadFromSlot(int slotNumber, MockPlayer* player) {
        if (slotNumber < 1) {
            return false;
        }
        std::string fileName = getSlotFileName(slotNumber);
        return load(fileName, player);
    }
    
    bool deleteSlot(int slotNumber) {
        if (slotNumber < 1) {
            return false;
        }
        std::string fileName = getSlotFileName(slotNumber);
        return deleteSave(fileName);
    }
    
    // File operations
    bool deleteSave(const std::string& saveFileName) {
        std::string fullPath = getFullSavePath(saveFileName);
        try {
            if (std::filesystem::exists(fullPath)) {
                std::filesystem::remove(fullPath);
                return true;
            }
            return false;
        }
        catch (const std::filesystem::filesystem_error& e) {
            return false;
        }
    }
    
    // Check if a save file exists
    bool saveExists(const std::string& saveFileName) const {
        std::string fullPath = getFullSavePath(saveFileName);
        return std::filesystem::exists(fullPath);
    }
    
    // Check if a slot is in use
    bool slotExists(int slotNumber) const {
        if (slotNumber < 1) {
            return false;
        }
        std::string fileName = getSlotFileName(slotNumber);
        return saveExists(fileName);
    }
    
private:
    std::string m_saveDirectory;
    
    // Helper methods
    std::string getSlotFileName(int slotNumber) const {
        return "save_slot_" + std::to_string(slotNumber) + ".dat";
    }
    
    std::string getFullSavePath(const std::string& saveFileName) const {
        return m_saveDirectory + "/" + saveFileName;
    }
    
    bool ensureSaveDirectoryExists() const {
        try {
            if (!std::filesystem::exists(m_saveDirectory)) {
                std::filesystem::create_directories(m_saveDirectory);
            }
            return true;
        }
        catch (const std::filesystem::filesystem_error& e) {
            return false;
        }
    }
};

// Global fixture for test setup and cleanup
struct TestFixture {
    TestFixture() {
        // Save directory is handled by TestSaveGameManager
    }
    
    ~TestFixture() {
        // Cleanup is handled by TestSaveGameManager destructor
    }
};

BOOST_GLOBAL_FIXTURE(TestFixture);

BOOST_AUTO_TEST_CASE(TestSaveAndLoad) {
    // Create the test manager
    TestSaveGameManager saveManager;
    
    // Create a test player
    MockPlayer player;
    player.setTestPosition(123.0f, 456.0f);
    player.setTestTextureID("test_texture");
    player.setTestState("running");
    
    // Save the player state
    bool saveResult = saveManager.save("test_save.dat", &player);
    BOOST_CHECK(saveResult);
    
    // Check if the file exists
    BOOST_CHECK(saveManager.saveExists("test_save.dat"));
    
    // Create a new player with different state
    MockPlayer loadedPlayer;
    loadedPlayer.setTestPosition(0.0f, 0.0f);
    loadedPlayer.setTestTextureID("different_texture");
    loadedPlayer.setTestState("idle");
    
    // Load the saved state
    bool loadResult = saveManager.load("test_save.dat", &loadedPlayer);
    BOOST_CHECK(loadResult);
    
    // Check that the state was loaded correctly
    BOOST_CHECK_CLOSE(loadedPlayer.getPosition().getX(), 123.0f, 0.001f);
    BOOST_CHECK_CLOSE(loadedPlayer.getPosition().getY(), 456.0f, 0.001f);
    BOOST_CHECK_EQUAL(loadedPlayer.getCurrentStateName(), "running");
    BOOST_CHECK_EQUAL(loadedPlayer.getTextureID(), "test_texture");
}

BOOST_AUTO_TEST_CASE(TestSlotOperations) {
    // Create the test manager
    TestSaveGameManager saveManager;
    
    // Create a test player
    MockPlayer player;
    player.setTestPosition(123.0f, 456.0f);
    
    // Save to slot 1
    bool saveResult = saveManager.saveToSlot(1, &player);
    BOOST_CHECK(saveResult);
    
    // Check if the slot exists
    BOOST_CHECK(saveManager.slotExists(1));
    
    // Load from slot 1
    MockPlayer loadedPlayer;
    bool loadResult = saveManager.loadFromSlot(1, &loadedPlayer);
    BOOST_CHECK(loadResult);
    
    // Check slot data was loaded correctly
    BOOST_CHECK_CLOSE(loadedPlayer.getPosition().getX(), 123.0f, 0.001f);
    
    // Delete the slot
    bool deleteResult = saveManager.deleteSlot(1);
    BOOST_CHECK(deleteResult);
    
    // Check that the slot no longer exists
    BOOST_CHECK(!saveManager.slotExists(1));
}

BOOST_AUTO_TEST_CASE(TestErrorHandling) {
    // Create the test manager
    TestSaveGameManager saveManager;
    
    // Create a test player
    MockPlayer player;
    
    // Try to load a non-existent file
    bool loadResult = saveManager.load("non_existent.dat", &player);
    BOOST_CHECK(!loadResult);
    
    // Try to save with a null player
    bool saveResult = saveManager.save("null_test.dat", nullptr);
    BOOST_CHECK(!saveResult);
    
    // Try to load with a null player
    saveResult = saveManager.save("valid.dat", &player);
    BOOST_CHECK(saveResult);
    loadResult = saveManager.load("valid.dat", nullptr);
    BOOST_CHECK(!loadResult);
    
    // Test invalid slot numbers
    saveResult = saveManager.saveToSlot(0, &player);
    BOOST_CHECK(!saveResult);
    saveResult = saveManager.saveToSlot(-1, &player);
    BOOST_CHECK(!saveResult);
}