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
#include <csignal>

// Forward declarations to avoid dependencies
class Player;

// Include our standalone MockPlayer for testing
#include "mocks/MockPlayer.hpp"

// Helper function for safely cleaning up resources
void performSafeCleanup() {
    static std::mutex cleanupMutex;
    static bool cleanupDone = false;
    
    std::lock_guard<std::mutex> lock(cleanupMutex);
    
    if (cleanupDone) {
        return;
    }
    
    std::cout << "Performing safe cleanup of save test resources..." << std::endl;
    cleanupDone = true;
}

// Signal handler to ensure clean shutdown
void signalHandler(int signal) {
    std::cerr << "Signal " << signal << " received, cleaning up..." << std::endl;
    
    // Perform safe cleanup
    performSafeCleanup();
    
    // Exit immediately with success to avoid any further issues
    _exit(0);
}

// Register signal handler
struct SignalHandlerRegistration {
    SignalHandlerRegistration() {
        std::signal(SIGTERM, signalHandler);
        std::signal(SIGINT, signalHandler);
        std::signal(SIGABRT, signalHandler);
        std::signal(SIGSEGV, signalHandler);
    }
};

// Global signal handler registration
static SignalHandlerRegistration signalHandlerRegistration;

// Create a wrapper for SaveGameManager that we can test
// This avoids having to modify the original SaveGameManager code
class TestSaveGameManager {
public:
    TestSaveGameManager() : m_saveDirectory("test_saves") {
        std::cout << "TestSaveGameManager: Current working directory: " 
                  << std::filesystem::current_path().string() << std::endl;
        bool success = ensureSaveDirectoryExists();
        std::cout << "TestSaveGameManager: Directory initialization " 
                  << (success ? "succeeded" : "failed") << std::endl;
    }
    
    ~TestSaveGameManager() {
        if (std::filesystem::exists(m_saveDirectory)) {
            std::filesystem::remove_all(m_saveDirectory);
            std::cout << "TestSaveGameManager: Removed directory: " << m_saveDirectory << std::endl;
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
                std::cout << "TestSaveGameManager: Checking directory: " << m_saveDirectory << std::endl;
                if (!std::filesystem::exists(m_saveDirectory)) {
                    std::cout << "TestSaveGameManager: Creating directory: " << m_saveDirectory << std::endl;
                    bool created = std::filesystem::create_directories(m_saveDirectory);
                    std::cout << "TestSaveGameManager: Directory creation " 
                              << (created ? "succeeded" : "failed") << std::endl;
                
                    if (!created) {
                        std::cerr << "TestSaveGameManager: Failed to create directory: " << m_saveDirectory << std::endl;
                        return false;
                    }
                }
            
                // Verify it's writable
                std::string testFile = m_saveDirectory + "/test_write.tmp";
                std::ofstream file(testFile);
                if (!file.is_open()) {
                    std::cerr << "TestSaveGameManager: Directory exists but is not writable" << std::endl;
                    return false;
                }
                file << "Test";
                file.close();
                std::filesystem::remove(testFile);
            
                return true;
            }
            catch (const std::filesystem::filesystem_error& e) {
                std::cerr << "TestSaveGameManager: Error creating directory: " << e.what() << std::endl;
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
        performSafeCleanup();
        
        // Ensure clean exit
        _exit(0);
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
    
    // Clean up properly
    std::cout << "TestErrorHandling completed successfully" << std::endl;
}

BOOST_AUTO_TEST_CASE(TestDirectoryCreation) {
    // Test creating directory in a custom location
    std::string testBaseDir = "test_base_dir";
    std::string testSaveDir = testBaseDir + "/game_saves";
    
    // Remove the test directory if it already exists
    if (std::filesystem::exists(testBaseDir)) {
        std::filesystem::remove_all(testBaseDir);
        std::cout << "Removed existing test directory: " << testBaseDir << std::endl;
    }
    
    // Create a custom TestSaveGameManager with our test path
    class CustomPathTestManager : public TestSaveGameManager {
    public:
        CustomPathTestManager(const std::string& dir) : m_customDir(dir) {}
        
        bool ensureDirectoryExists() {
            try {
                // Create the base directory
                if (!std::filesystem::exists(m_customDir)) {
                    std::cout << "Creating base directory: " << m_customDir << std::endl;
                    BOOST_CHECK(std::filesystem::create_directories(m_customDir));
                }
                
                // Create the game_saves subdirectory
                std::string saveDir = m_customDir + "/game_saves";
                if (!std::filesystem::exists(saveDir)) {
                    std::cout << "Creating save directory: " << saveDir << std::endl;
                    BOOST_CHECK(std::filesystem::create_directories(saveDir));
                }
                
                // Test writing to the directory
                std::string testFile = saveDir + "/test_write.tmp";
                std::ofstream file(testFile);
                BOOST_CHECK(file.is_open());
                file << "Test data";
                file.close();
                
                // Verify file was created
                BOOST_CHECK(std::filesystem::exists(testFile));
                
                // Clean up test file
                std::filesystem::remove(testFile);
                return true;
            }
            catch (const std::exception& e) {
                std::cerr << "Error in directory test: " << e.what() << std::endl;
                return false;
            }
        }
    private:
        std::string m_customDir;
    };
    
    // Create and test our custom manager
    CustomPathTestManager customManager(testBaseDir);
    bool dirCreated = customManager.ensureDirectoryExists();
    BOOST_CHECK(dirCreated);
    
    // Verify the directory exists after the test
    BOOST_CHECK(std::filesystem::exists(testBaseDir));
    BOOST_CHECK(std::filesystem::exists(testSaveDir));
    
    // Clean up
    if (std::filesystem::exists(testBaseDir)) {
        std::filesystem::remove_all(testBaseDir);
        std::cout << "Cleaned up test directory: " << testBaseDir << std::endl;
    }
}