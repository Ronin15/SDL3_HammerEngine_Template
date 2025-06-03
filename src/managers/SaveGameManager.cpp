/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include "managers/SaveGameManager.hpp"
#include "entities/Player.hpp"
#include "utils/Vector2D.hpp"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <ctime>
#include <cstring>
#include <chrono>

// File signature constant
constexpr char FORGE_SAVE_SIGNATURE[9] = {'F', 'O', 'R', 'G', 'E', 'S', 'A', 'V', 'E'};
constexpr size_t FORGE_SAVE_SIGNATURE_SIZE = sizeof(FORGE_SAVE_SIGNATURE);

// Initialize the static variable
bool SaveGameManager::initialized = false;

bool SaveGameManager::save(const std::string& saveFileName, const Player& player) {
    // No need for null check with references - they're always valid

    // Make sure base directory exists
    if (!std::filesystem::exists(m_saveDirectory)) {
        std::cerr << "Forge Game Engine - Save Game Manager: Base directory doesn't exist: '" << m_saveDirectory << "'\n";
        try {
            if (std::filesystem::create_directories(m_saveDirectory)) {
                // Base directory created successfully
            } else {
                std::cerr << "Forge Game Engine - Save Game Manager: Failed to create base directory\n";
                return false;
            }
        } catch (const std::exception& e) {
            std::cerr << "Forge Game Engine - Save Game Manager: Error creating base directory: " << e.what() << "\n";
            return false;
        }
    }

    // Ensure the save directory exists
    if (!ensureSaveDirectoryExists()) {
        std::cerr << "Forge Game Engine - Save Game Manager: Failed to ensure save directory exists!" << std::endl;
        return false;
    }

    try {
        // Create full path for the save file
        std::string fullPath = getFullSavePath(saveFileName);
        
        // Make sure parent directory exists once more
        std::filesystem::path filePath(fullPath);
        std::filesystem::path parentPath = filePath.parent_path();
        if (!std::filesystem::exists(parentPath)) {
            // Create parent directory if it doesn't exist
            std::filesystem::create_directories(parentPath);
        }

        // Open binary file for writing
        std::ofstream file(fullPath, std::ios::binary | std::ios::out);
        if (!file.is_open()) {
            std::cerr << "Forge Game Engine - Save Game Manager: Could not open file " << fullPath << " for writing!" << std::endl;
            // Check if parent directory exists and is writable
            std::cerr << "Forge Game Engine - Save Game Manager: Parent directory "
                     << parentPath.string() << " exists: "
                     << (std::filesystem::exists(parentPath) ? "yes" : "no") << std::endl;
            return false;
        }
        // File opened successfully

        // We'll write the header at the end once we know the data size
        // For now, just skip past where the header will be
        file.seekp(sizeof(SaveGameHeader));

        // Start of data section - track position to calculate data size
        std::streampos dataStart = file.tellp();

        // Write player data

        // Write position
        writeVector2D(file, player.getPosition());

        // Write textureID
        writeString(file, player.getTextureID());

        // Write current state
        writeString(file, player.getCurrentStateName());

        // Write current level (this would come from your game state)
        writeString(file, "current_level_id"); // Replace with actual level ID

        // End of data section - calculate data size
        std::streampos dataEnd = file.tellp();
        uint32_t dataSize = static_cast<uint32_t>(dataEnd - dataStart);

        // Go back and write the header
        file.seekp(0);
        writeHeader(file, dataSize);

        file.close();

        std::cout << "Forge Game Engine - Save successful: " << saveFileName << "\n";
        return true;
    }
    catch (const std::exception& e) {
        std::cerr << "Forge Game Engine - Save Game Manager: Error saving game: " << e.what() << std::endl;
        return false;
    }
}

bool SaveGameManager::saveToSlot(int slotNumber, const Player& player) {
    if (slotNumber < 1) {
        std::cerr << "Forge Game Engine - Save Game Manager: Invalid slot number: " << slotNumber << std::endl;
        return false;
    }

    std::string fileName = getSlotFileName(slotNumber);
    return save(fileName, player);
}


bool SaveGameManager::load(const std::string& saveFileName, Player& player) const {
    // No need for null check with references - they're always valid

    // Check if the file exists
    std::string fullPath = getFullSavePath(saveFileName);
    if (!std::filesystem::exists(fullPath)) {
        std::cerr << "Forge Game Engine - Save Game Manager: Save file does not exist: " << saveFileName << std::endl;
        return false;
    }

    try {
        // Open binary file for reading
        std::ifstream file(fullPath, std::ios::binary | std::ios::in);
        if (!file.is_open()) {
            std::cerr << "Forge Game Engine - Save Game Manager: Could not open file for reading!" << std::endl;
            return false;
        }

        // Read and validate header
        SaveGameHeader header;
        if (!readHeader(file, header)) {
            std::cerr << "Forge Game Engine - Save Game Manager: Invalid save file format" << std::endl;
            file.close();
            return false;
        }

        // Read player data
        Vector2D position(0.0f, 0.0f);
        if (!readVector2D(file, position)) {
            std::cerr << "Forge Game Engine - Save Game Manager: Error reading player position" << std::endl;
            file.close();
            return false;
        }

        // Apply position to player
        player.setVelocity(Vector2D(0, 0)); // Reset velocity
        player.setPosition(position);

        // Read textureID
        std::string textureID;
        if (!readString(file, textureID)) {
            std::cerr << "Forge Game Engine - Save Game Manager: Error reading player textureID!" << std::endl;
            file.close();
            return false;
        }

        // Read state
        std::string state;
        if (!readString(file, state)) {
            std::cerr << "Forge Game Engine - Save Game Manager: Error reading player state!" << std::endl;
            file.close();
            return false;
        }

        // Apply state to player
        player.changeState(state);

        // Read level ID (not using it yet, but reading for future use)
        std::string levelID;
        if (!readString(file, levelID)) {
            std::cerr << "Forge Game Engine - Save Game Manager: Error reading level ID!" << std::endl;
            file.close();
            return false;
        }

        file.close();

        std::cout << "Forge Game Engine - Game loaded: " << saveFileName << "\n";
        return true;
    }
    catch (const std::exception& e) {
        std::cerr << "Forge Game Engine - Save Game Manager: Error loading game: " << e.what() << std::endl;
        return false;
    }
}


bool SaveGameManager::loadFromSlot(int slotNumber, Player& player) {
    if (slotNumber < 1) {
        std::cerr << "Forge Game Engine - Save Game Manager: Invalid slot number: " << slotNumber << std::endl;
        return false;
    }

    std::string fileName = getSlotFileName(slotNumber);
    return load(fileName, player);
}

bool SaveGameManager::deleteSave(const std::string& saveFileName) const {
    try {
        std::string fullPath = getFullSavePath(saveFileName);
        if (std::filesystem::exists(fullPath)) {
            std::filesystem::remove(fullPath);
            std::cout << "Forge Game Engine - Save successful: " << saveFileName << "\n";
            return true;
        }
        else {
            std::cerr << "Forge Game Engine - Save Game Manager: Save file does not exist: " << fullPath << std::endl;
            return false;
        }
    }
    catch (const std::filesystem::filesystem_error& e) {
        std::cerr << "Forge Game Engine - Save Game Manager: Error deleting save file: " << e.what() << std::endl;
        return false;
    }
}

bool SaveGameManager::deleteSlot(int slotNumber) {
    if (slotNumber < 1) {
        std::cerr << "Forge Game Engine - Save Game Manager: Invalid slot number: " << slotNumber << std::endl;
        return false;
    }

    std::string fileName = getSlotFileName(slotNumber);
    return deleteSave(fileName);
}

boost::container::small_vector<std::string, 10> SaveGameManager::getSaveFiles() const {
    boost::container::small_vector<std::string, 10> saveFiles;
    std::string savePath = m_saveDirectory + "/game_saves";

    // Check if the directory exists
    if (!std::filesystem::exists(savePath) || !std::filesystem::is_directory(savePath)) {
        return saveFiles; // Return empty vector
    }

    try {
        // Iterate through all files in the directory
        for (const auto& entry : std::filesystem::directory_iterator(savePath)) {
            if (entry.is_regular_file()) {
                // Get file path and extension
                std::filesystem::path filePath = entry.path();
                std::string extension = filePath.extension().string();

                // Convert extension to lowercase for case-insensitive comparison
                std::transform(extension.begin(), extension.end(), extension.begin(),
                              [](unsigned char c) { return std::tolower(c); });

                // Check if the file is a save file (.dat extension)
                if (extension == ".dat") {
                    // Verify it's a valid save file by checking the header
                    if (isValidSaveFile(filePath.filename().string())) {
                        saveFiles.push_back(filePath.filename().string());
                    }
                }
            }
        }
    }
    catch (const std::filesystem::filesystem_error& e) {
        std::cerr << "Forge Game Engine - Save Game Manager: Error listing save files: " << e.what() << std::endl;
    }

    return saveFiles;
}

SaveGameData SaveGameManager::getSaveInfo(const std::string& saveFileName) const {
    return extractSaveInfo(saveFileName);
}

boost::container::small_vector<SaveGameData, 10> SaveGameManager::getAllSaveInfo() const {
    boost::container::small_vector<SaveGameData, 10> saveInfoList;
    boost::container::small_vector<std::string, 10> files = getSaveFiles();

    for (const auto& file : files) {
        SaveGameData info = extractSaveInfo(file);
        saveInfoList.push_back(info);
    }

    return saveInfoList;
}

bool SaveGameManager::saveExists(const std::string& saveFileName) const {
    return std::filesystem::exists(getFullSavePath(saveFileName));
}

bool SaveGameManager::slotExists(int slotNumber) const {
    if (slotNumber < 1) {
        return false;
    }

    std::string fileName = getSlotFileName(slotNumber);
    return saveExists(fileName);
}

bool SaveGameManager::isValidSaveFile(const std::string& saveFileName) const {
    // Check if file exists
    std::string fullPath = getFullSavePath(saveFileName);
    if (!std::filesystem::exists(fullPath)) {
        return false;
    }

    try {
        // Open file and check header
        std::ifstream file(fullPath, std::ios::binary | std::ios::in);
        if (!file.is_open()) {
            return false;
        }

        // Read header
        SaveGameHeader header;
        if (!readHeader(file, header)) {
            file.close();
            return false;
        }

        // Check signature using constexpr array
        if (std::memcmp(header.signature, FORGE_SAVE_SIGNATURE, FORGE_SAVE_SIGNATURE_SIZE) != 0) {
            file.close();
            return false;
        }

        file.close();
        return true;
    }
    catch (const std::exception& e) {
        return false;
    }
}

void SaveGameManager::setSaveDirectory(const std::string& directory) {
    // Check if directory exists
    if (!std::filesystem::exists(directory)) {
        // Try to create it
        try {
            if (!std::filesystem::create_directories(directory)) {
                std::cerr << "Forge Game Engine - Save Game Manager: Failed to create directory: " << directory << "\n";
            }
        } catch (const std::exception& e) {
            std::cerr << "Forge Game Engine - Save Game Manager: Error creating directory: " << e.what() << "\n";
        }
    }

    m_saveDirectory = directory;

    // Ensure the game_saves subdirectory exists right away
    ensureSaveDirectoryExists();
}

void SaveGameManager::clean() {
    std::cout << "Forge Game Engine - Save Game Manager resources cleaned!\n";
}

// Private helper methods
std::string SaveGameManager::getSlotFileName(int slotNumber) const {
    return "save_slot_" + std::to_string(slotNumber) + ".dat";
}

std::string SaveGameManager::getFullSavePath(const std::string& saveFileName) const {
    return m_saveDirectory + "/game_saves/" + saveFileName;
}

bool SaveGameManager::ensureSaveDirectoryExists() const {
    try {
        // First ensure the base directory exists (which should be "res")
        if (!std::filesystem::exists(m_saveDirectory)) {
            // Try to create the base directory
            if (!std::filesystem::create_directories(m_saveDirectory)) {
                std::cerr << "Forge Game Engine - Save Game Manager: Failed to create base directory\n";
                return false;
            }
        }

        // Create the game_saves directory inside the base directory
        std::string savePath = m_saveDirectory + "/game_saves";

        if (!std::filesystem::exists(savePath)) {
            // Create the directory and all parent directories
            if (!std::filesystem::create_directories(savePath)) {
                std::cerr << "Forge Game Engine - Save Game Manager: Failed to create save directory\n";
                return false;
            }
        }

        // Verify directory exists after creation attempt
        if (!std::filesystem::exists(savePath)) {
            std::cerr << "Forge Game Engine - Save Game Manager: Directory still doesn't exist after creation attempt\n";
            return false;
        }

        // Verify directory is writable by attempting to create a test file
        {
            std::string testFilePath = savePath + "/test_write.tmp";
            std::ofstream testFile(testFilePath);
            if (!testFile.is_open()) {
                std::cerr << "Forge Game Engine - Save Game Manager: Directory exists but is not writable\n";
                return false;
            }
            testFile << "Test"; // Actually write something
            testFile.close();
            if (std::filesystem::exists(testFilePath)) {
                std::filesystem::remove(testFilePath);
            } else {
                std::cerr << "Forge Game Engine - Save Game Manager: Test file was not created properly\n";
                return false;
            }
        }

        return true;
    }
    catch (const std::exception& e) {
        std::cerr << "Forge Game Engine - Save Game Manager: Error creating save directory: " << e.what() << std::endl;
        return false;
    }
}

SaveGameData SaveGameManager::extractSaveInfo(const std::string& saveFileName) const {
    SaveGameData info;
    info.saveName = saveFileName;

    // Check if the file exists
    std::string fullPath = getFullSavePath(saveFileName);
    if (!std::filesystem::exists(fullPath)) {
        return info; // Return empty info
    }

    try {
        // Open binary file for reading
        std::ifstream file(fullPath, std::ios::binary | std::ios::in);
        if (!file.is_open()) {
            return info; // Return empty info
        }

        // Read and validate header
        SaveGameHeader header;
        if (!readHeader(file, header)) {
            std::cerr << "Forge Game Engine - Save Game Manager: Invalid save file format when extracting info!" << std::endl;
            file.close();
            return info;
        }

        // Set timestamp from header
        const std::tm* timeinfo = std::localtime(&header.timestamp);
        char buffer[80];
        std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", timeinfo);
        info.timestamp = buffer;

        // Read position (skip the actual data, we just want to read ahead)
        Vector2D position(0.0f, 0.0f);
        if (!readVector2D(file, position)) {
            file.close();
            return info;
        }

        // Store position in info
        info.playerXPos = position.getX();
        info.playerYPos = position.getY();

        // Skip textureID (read but don't store)
        std::string textureID;
        if (!readString(file, textureID)) {
            file.close();
            return info;
        }

        // Skip state (read but don't store)
        std::string state;
        if (!readString(file, state)) {
            file.close();
            return info;
        }

        // Read level ID
        std::string levelID;
        if (!readString(file, levelID)) {
            file.close();
            return info;
        }
        info.currentLevel = levelID;

        file.close();
    }
    catch (const std::exception& e) {
        std::cerr << "Forge Game Engine - Save Game Manager: Error extracting save info: " << e.what() << std::endl;
    }

    return info;
}

// Add the missing binary file operations
bool SaveGameManager::writeHeader(std::ofstream& file, uint32_t dataSize) const {
    SaveGameHeader header;
    // Initialize signature using the same constant
    std::memcpy(header.signature, FORGE_SAVE_SIGNATURE, FORGE_SAVE_SIGNATURE_SIZE);
    header.timestamp = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    header.dataSize = dataSize;

    file.write(reinterpret_cast<const char*>(&header), sizeof(SaveGameHeader));
    return file.good();
}

bool SaveGameManager::readHeader(std::ifstream& file, SaveGameHeader& header) const {
    file.read(reinterpret_cast<char*>(&header), sizeof(SaveGameHeader));

    // Verify header signature using constexpr array
    if (std::memcmp(header.signature, FORGE_SAVE_SIGNATURE, FORGE_SAVE_SIGNATURE_SIZE) != 0) {
        return false;
    }

    return file.good();
}

bool SaveGameManager::writeString(std::ofstream& file, const std::string& str) const {
    uint32_t length = static_cast<uint32_t>(str.length());
    file.write(reinterpret_cast<const char*>(&length), sizeof(uint32_t));
    file.write(str.c_str(), length);
    return file.good();
}

bool SaveGameManager::readString(std::ifstream& file, std::string& str) const {
    uint32_t length;
    file.read(reinterpret_cast<char*>(&length), sizeof(uint32_t));

    if (!file.good() || length > 1000000) { // Safety check for unreasonable string length
        return false;
    }

    str.resize(length);
    file.read(&str[0], length);
    return file.good();
}

bool SaveGameManager::writeVector2D(std::ofstream& file, const Vector2D& vec) const {
    try {
        boost::archive::binary_oarchive oa(file);
        oa << vec;
        return file.good();
    } catch (const std::exception& e) {
        std::cerr << "Forge Game Engine - Save Game Manager: Error serializing Vector2D: " << e.what() << std::endl;
        return false;
    }
}

bool SaveGameManager::readVector2D(std::ifstream& file, Vector2D& vec) const {
    try {
        boost::archive::binary_iarchive ia(file);
        ia >> vec;
        return file.good();
    } catch (const std::exception& e) {
        std::cerr << "Forge Game Engine - Save Game Manager: Error deserializing Vector2D: " << e.what() << std::endl;
        return false;
    }
}
