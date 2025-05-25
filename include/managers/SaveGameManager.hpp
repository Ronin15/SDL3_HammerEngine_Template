/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#ifndef SAVE_GAME_MANAGER_HPP
#define SAVE_GAME_MANAGER_HPP

#include <string>
#include <boost/container/small_vector.hpp>
#include <boost/serialization/serialization.hpp>
#include <boost/archive/binary_oarchive.hpp>
#include <boost/archive/binary_iarchive.hpp>
#include "utils/Vector2D.hpp"
#include <ctime>

// Forward declarations
class Player;
class Vector2D;

// SaveGame header structure - used at the beginning of save files
struct SaveGameHeader {
    char signature[9]{'F', 'O', 'R', 'G', 'E', 'S', 'A', 'V', 'E'}; // File signature "FORGESAVE"
    uint32_t version{1};                                          // Save format version
    time_t timestamp{0};                                          // Save timestamp
    uint32_t dataSize{0};                                         // Size of data section
};

// SaveGame data structure for metadata access
struct SaveGameData {
    std::string saveName{};
    std::string timestamp{};
    int playerLevel{0};
    float playerHealth{100.0f};
    float playerXPos{0.0f};   // Added player X position
    float playerYPos{0.0f};   // Added player Y position
    std::string currentLevel{};
    // Add more fields as needed
};

class SaveGameManager {
public:
    ~SaveGameManager() = default;

    static SaveGameManager& Instance() {
        static SaveGameManager instance;
        initialized = true;
        return instance;
    }

    static bool Exists() { return initialized; }

    // Save game data to a file
    // Returns true if save was successful
    bool save(const std::string& saveFileName, const Player* player);

    // Save game data to a slot (creates a file with a standard naming convention)
    // Returns true if save was successful
    bool saveToSlot(int slotNumber, const Player* player);

    // Load game data from a file
    // Returns true if load was successful
    bool load(const std::string& saveFileName, Player* player);

    // Load game data from a slot
    // Returns true if load was successful
    bool loadFromSlot(int slotNumber, Player* player);

    // Delete a save file
    // Returns true if deletion was successful
    bool deleteSave(const std::string& saveFileName);

    // Delete a save slot
    // Returns true if deletion was successful
    bool deleteSlot(int slotNumber);

    // Get a list of all save files in the save directory
    boost::container::small_vector<std::string, 10> getSaveFiles() const;

    // Get information about a specific save file
    SaveGameData getSaveInfo(const std::string& saveFileName) const;

    // Get information about all save slots
    boost::container::small_vector<SaveGameData, 10> getAllSaveInfo() const;

    // Check if a save file exists
    bool saveExists(const std::string& saveFileName) const;

    // Check if a save slot is in use
    bool slotExists(int slotNumber) const;

    // Validate if a file is a valid save file
    bool isValidSaveFile(const std::string& saveFileName) const;

    // Set the base directory for save files
    void setSaveDirectory(const std::string& directory);



    // Clean up resources
    void clean();

private:
    std::string m_saveDirectory{"res"};  // Default save directory
    static bool initialized;

    // Helper methods
    std::string getSlotFileName(int slotNumber) const;
    std::string getFullSavePath(const std::string& saveFileName) const;
    bool ensureSaveDirectoryExists() const;
    SaveGameData extractSaveInfo(const std::string& saveFileName) const;

    // Binary file operations
    bool writeHeader(std::ofstream& file, uint32_t dataSize) const;
    bool readHeader(std::ifstream& file, SaveGameHeader& header) const;
    bool writeString(std::ofstream& file, const std::string& str) const;
    bool readString(std::ifstream& file, std::string& str) const;
    // Binary serialization using Boost
    bool writeVector2D(std::ofstream& file, const Vector2D& vec) const;
    bool readVector2D(std::ifstream& file, Vector2D& vec) const;

    // Delete copy constructor and assignment operator
    SaveGameManager(const SaveGameManager&) = delete;  // prevent copy construction
    SaveGameManager& operator=(const SaveGameManager&) = delete;  // prevent assignment

    SaveGameManager() = default;
};

#endif  // SAVE_GAME_MANAGER_HPP
