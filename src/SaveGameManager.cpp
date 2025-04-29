/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include "SaveGameManager.hpp"
#include "Player.hpp"
#include "Vector2D.hpp"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <vector>
#include <algorithm>
#include <ctime>
#include <cstring>


// Initialize the static variable
bool SaveGameManager::initialized = false;

bool SaveGameManager::save(const std::string& saveFileName, const Player* player) {
    if (player == nullptr) {
        std::cerr << "Forge Game Engine - SaveGameManager: Cannot save null player\n";
        return false;
    }

    // Ensure the save directory exists
    if (!ensureSaveDirectoryExists()) {
        return false;
    }

    // Create full path for the save file
    std::string fullPath = getFullSavePath(saveFileName);

    try {
        // Open binary file for writing
        std::ofstream file(fullPath, std::ios::binary | std::ios::out);
        if (!file.is_open()) {
            std::cerr << "Forge Game Engine - SaveGameManager: Could not open file " << fullPath << " for writing\n";
            return false;
        }

        // We'll write the header at the end once we know the data size
        // For now, just skip past where the header will be
        file.seekp(sizeof(SaveGameHeader));

        // Start of data section - track position to calculate data size
        std::streampos dataStart = file.tellp();

        // Write player data

        // Write position
        writeVector2D(file, player->getPosition());

        // Write textureID
        writeString(file, player->getTextureID());

        // Write current state
        writeString(file, player->getCurrentStateName());

        // Write current level (this would come from your game state)
        writeString(file, "current_level_id"); // Replace with actual level ID

        // End of data section - calculate data size
        std::streampos dataEnd = file.tellp();
        uint32_t dataSize = static_cast<uint32_t>(dataEnd - dataStart);

        // Go back and write the header
        file.seekp(0);
        writeHeader(file, dataSize);

        file.close();

        std::cout << "Forge Game Engine - SaveGameManager: Game saved to " << fullPath << "\n";
        return true;
    }
    catch (const std::exception& e) {
        std::cerr << "Forge Game Engine - SaveGameManager: Error saving game: " << e.what() << std::endl;
        return false;
    }
}

bool SaveGameManager::saveToSlot(int slotNumber, const Player* player) {
    if (slotNumber < 1) {
        std::cerr << "Forge Game Engine - SaveGameManager: Invalid slot number: " << slotNumber << "\n";
        return false;
    }

    std::string fileName = getSlotFileName(slotNumber);
    return save(fileName, player);
}


bool SaveGameManager::load(const std::string& saveFileName, Player* player) {
    if (player == nullptr) {
        std::cerr << "Forge Game Engine - SaveGameManager: Cannot load to null player\n";
        return false;
    }

    // Create full path for the save file
    std::string fullPath = getFullSavePath(saveFileName);

    // Check if the file exists
    if (!std::filesystem::exists(fullPath)) {
        std::cerr << "Forge Game Engine - SaveGameManager: Save file does not exist: " << fullPath << "\n";
        return false;
    }

    try {
        // Open binary file for reading
        std::ifstream file(fullPath, std::ios::binary | std::ios::in);
        if (!file.is_open()) {
            std::cerr << "Forge Game Engine - SaveGameManager: Could not open file " << fullPath << " for reading\n";
            return false;
        }

        // Read and validate header
        SaveGameHeader header;
        if (!readHeader(file, header)) {
            std::cerr << "Forge Game Engine - SaveGameManager: Invalid save file format\n";
            file.close();
            return false;
        }

        // Read player data

        Vector2D position;
        ;
        if (!readVector2D(file, position)) {
            std::cerr << "Forge Game Engine - SaveGameManager: Error reading player position\n";
            file.close();
            return false;
        }

        // Apply position to player
        player->setVelocity(Vector2D(0, 0)); // Reset velocity
        player->setPosition(position);

        // Read textureID
        std::string textureID;
        if (!readString(file, textureID)) {
            std::cerr << "Forge Game Engine - SaveGameManager: Error reading player textureID\n";
            file.close();
            return false;
        }

        // Read state
        std::string state;
        if (!readString(file, state)) {
            std::cerr << "Forge Game Engine - SaveGameManager: Error reading player state\n";
            file.close();
            return false;
        }

        // Apply state to player
        player->changeState(state);

        // Read level ID (not using it yet, but reading for future use)
        std::string levelID;
        if (!readString(file, levelID)) {
            std::cerr << "Forge Game Engine - SaveGameManager: Error reading level ID\n";
            file.close();
            return false;
        }

        file.close();

        std::cout << "Forge Game Engine - SaveGameManager: Game loaded from " << fullPath << "\n";
        return true;
    }
    catch (const std::exception& e) {
        std::cerr << "Forge Game Engine - SaveGameManager: Error loading game: " << e.what() << std::endl;
        return false;
    }
}


bool SaveGameManager::loadFromSlot(int slotNumber, Player* player) {
    if (slotNumber < 1) {
        std::cerr << "Forge Game Engine - SaveGameManager: Invalid slot number: " << slotNumber << "\n";
        return false;
    }

    std::string fileName = getSlotFileName(slotNumber);
    return load(fileName, player);
}

bool SaveGameManager::deleteSave(const std::string& saveFileName) {
    std::string fullPath = getFullSavePath(saveFileName);

    try {
        if (std::filesystem::exists(fullPath)) {
            std::filesystem::remove(fullPath);
            std::cout << "Forge Game Engine - SaveGameManager: Deleted save file: " << fullPath << "\n";
            return true;
        }
        else {
            std::cerr << "Forge Game Engine - SaveGameManager: Save file does not exist: " << fullPath << "\n";
            return false;
        }
    }
    catch (const std::filesystem::filesystem_error& e) {
        std::cerr << "Forge Game Engine - SaveGameManager: Error deleting save file: " << e.what() << std::endl;
        return false;
    }
}

bool SaveGameManager::deleteSlot(int slotNumber) {
    if (slotNumber < 1) {
        std::cerr << "Forge Game Engine - SaveGameManager: Invalid slot number: " << slotNumber << "\n";
        return false;
    }

    std::string fileName = getSlotFileName(slotNumber);
    return deleteSave(fileName);
}

std::vector<std::string> SaveGameManager::getSaveFiles() const {
    std::vector<std::string> saveFiles;

    // Check if the directory exists
    if (!std::filesystem::exists(m_saveDirectory) || !std::filesystem::is_directory(m_saveDirectory)) {
        return saveFiles; // Return empty vector
    }

    try {
        // Iterate through all files in the directory
        for (const auto& entry : std::filesystem::directory_iterator(m_saveDirectory)) {
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
        std::cerr << "Forge Game Engine - SaveGameManager: Error listing save files: " << e.what() << std::endl;
    }

    return saveFiles;
}

SaveGameData SaveGameManager::getSaveInfo(const std::string& saveFileName) const {
    return extractSaveInfo(saveFileName);
}

std::vector<SaveGameData> SaveGameManager::getAllSaveInfo() const {
    std::vector<SaveGameData> saveInfoList;
    std::vector<std::string> files = getSaveFiles();

    for (const auto& file : files) {
        SaveGameData info = extractSaveInfo(file);
        saveInfoList.push_back(info);
    }

    return saveInfoList;
}

bool SaveGameManager::saveExists(const std::string& saveFileName) const {
    std::string fullPath = getFullSavePath(saveFileName);
    return std::filesystem::exists(fullPath);
}

bool SaveGameManager::slotExists(int slotNumber) const {
    if (slotNumber < 1) {
        return false;
    }

    std::string fileName = getSlotFileName(slotNumber);
    return saveExists(fileName);
}

bool SaveGameManager::isValidSaveFile(const std::string& saveFileName) const {
    // Implementation for isValidSaveFile was missing!
    std::string fullPath = getFullSavePath(saveFileName);

    // Check if file exists
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

        // Check signature
        const char* expectedSignature = "FORGESAV";
        if (std::memcmp(header.signature, expectedSignature, 8) != 0) {
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
    m_saveDirectory = directory;
    std::cout << "Forge Game Engine - SaveGameManager: Save directory set to " << m_saveDirectory << "\n";
}

void SaveGameManager::clean() {
    std::cout << "Forge Game Engine - SaveGameManager resources cleaned!\n";
}

// Private helper methods
std::string SaveGameManager::getSlotFileName(int slotNumber) const {
    return "save_slot_" + std::to_string(slotNumber) + ".dat";
}

std::string SaveGameManager::getFullSavePath(const std::string& saveFileName) const {
    return m_saveDirectory + "/" + saveFileName;
}

bool SaveGameManager::ensureSaveDirectoryExists() const {
    try {
        if (!std::filesystem::exists(m_saveDirectory)) {
            std::filesystem::create_directories(m_saveDirectory);
            std::cout << "Forge Game Engine - SaveGameManager: Created save directory: " << m_saveDirectory << "\n";
        }
        return true;
    }
    catch (const std::filesystem::filesystem_error& e) {
        std::cerr << "Forge Game Engine - SaveGameManager: Error creating save directory: " << e.what() << std::endl;
        return false;
    }
}

SaveGameData SaveGameManager::extractSaveInfo(const std::string& saveFileName) const {
    SaveGameData info;
    info.saveName = saveFileName;

    std::string fullPath = getFullSavePath(saveFileName);

    // Check if the file exists
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
            std::cerr << "Forge Game Engine - SaveGameManager: Invalid save file format when extracting info\n";
            file.close();
            return info;
        }

        // Set timestamp from header
        std::tm* timeinfo = std::localtime(&header.timestamp);
        char buffer[80];
        std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", timeinfo);
        info.timestamp = buffer;

        // Read position (skip the actual data, we just want to read ahead)
        Vector2D position;
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
        std::cerr << "Forge Game Engine - SaveGameManager: Error extracting save info: " << e.what() << std::endl;
    }

    return info;
}

// Add the missing binary file operations
bool SaveGameManager::writeHeader(std::ofstream& file, uint32_t dataSize) const {
    SaveGameHeader header;
    header.timestamp = std::time(nullptr);
    header.dataSize = dataSize;

    file.write(reinterpret_cast<const char*>(&header), sizeof(SaveGameHeader));
    return file.good();
}

bool SaveGameManager::readHeader(std::ifstream& file, SaveGameHeader& header) const {
    file.read(reinterpret_cast<char*>(&header), sizeof(SaveGameHeader));

    // Verify header signature
    const char* expectedSignature = "FORGESAV";
    if (std::memcmp(header.signature, expectedSignature, 8) != 0) {
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
    float x = vec.getX();
    float y = vec.getY();
    file.write(reinterpret_cast<const char*>(&x), sizeof(float));
    file.write(reinterpret_cast<const char*>(&y), sizeof(float));
    return file.good();
}

bool SaveGameManager::readVector2D(std::ifstream& file, Vector2D& vec) const {
    float x, y;
    file.read(reinterpret_cast<char*>(&x), sizeof(float));
    file.read(reinterpret_cast<char*>(&y), sizeof(float));

    if (file.good()) {
        vec = Vector2D(x, y);
        return true;
    }
    return false;
}
