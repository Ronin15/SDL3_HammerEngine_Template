/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "managers/SaveGameManager.hpp"
#include "core/Logger.hpp"
#include "entities/Player.hpp"
#include "utils/BinarySerializer.hpp"
#include "utils/Vector2D.hpp"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>

// File signature constant
constexpr char HAMMER_SAVE_SIGNATURE[9] = {'F', 'O', 'R', 'G', 'E',
                                           'S', 'A', 'V', 'E'};
constexpr size_t HAMMER_SAVE_SIGNATURE_SIZE = sizeof(HAMMER_SAVE_SIGNATURE);

bool SaveGameManager::save(const std::string &saveFileName,
                           const Player &player) {
  // No need for null check with references - they're always valid

  // Make sure base directory exists
  if (!std::filesystem::exists(m_saveDirectory)) {
    SAVEGAME_WARN("Base directory doesn't exist: '" + m_saveDirectory + "'");
    try {
      if (std::filesystem::create_directories(m_saveDirectory)) {
        SAVEGAME_INFO("Created base directory: " + m_saveDirectory);
      } else {
        SAVEGAME_ERROR("Failed to create base directory");
        return false;
      }
    } catch (const std::exception &e) {
      SAVEGAME_ERROR("Error creating base directory: " + std::string(e.what()));
      return false;
    }
  }

  // Ensure the save directory exists
  if (!ensureSaveDirectoryExists()) {
    SAVEGAME_ERROR("Failed to ensure save directory exists!");
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
      SAVEGAME_ERROR("Could not open file " + fullPath + " for writing!");
      SAVEGAME_DEBUG("Parent directory " + parentPath.string() + " exists: " +
                     (std::filesystem::exists(parentPath) ? "yes" : "no"));
      return false;
    }
    SAVEGAME_DEBUG("Opened file for writing: " + fullPath);

    // We'll write the header at the end once we know the data size
    // For now, just skip past where the header will be
    file.seekp(sizeof(SaveGameHeader));

    // Start of data section - track position to calculate data size
    std::streampos dataStart = file.tellp();

    // Write player data using BinarySerializer
    auto writer =
        std::make_unique<BinarySerial::Writer>(std::shared_ptr<std::ostream>(
            &file, [](std::ostream *) { /* no-op deleter */ }));

    // Write position
    if (!writer->writeSerializable(player.getPosition())) {
      SAVEGAME_ERROR("Failed to write player position");
      file.close();
      return false;
    }

    // Write textureID
    if (!writer->writeString(player.getTextureID())) {
      SAVEGAME_ERROR("Failed to write player textureID");
      file.close();
      return false;
    }

    // Write current state
    if (!writer->writeString(player.getCurrentStateName())) {
      SAVEGAME_ERROR("Failed to write player state");
      file.close();
      return false;
    }

    // Write current level (this would come from your game state)
    if (!writer->writeString(
            "current_level_id")) { // Replace with actual level ID
      SAVEGAME_ERROR("Failed to write level ID");
      file.close();
      return false;
    }

    // End of data section - calculate data size
    std::streampos dataEnd = file.tellp();
    uint32_t dataSize = static_cast<uint32_t>(dataEnd - dataStart);

    // Go back and write the header
    file.seekp(0);
    writeHeader(file, dataSize);

    file.close();

    SAVEGAME_INFO("Save successful: " + saveFileName);
    return true;
  } catch (const std::exception &e) {
    SAVEGAME_ERROR("Error saving game: " + std::string(e.what()));
    return false;
  }
}

bool SaveGameManager::saveToSlot(int slotNumber, const Player &player) {
  if (slotNumber < 1) {
    SAVEGAME_ERROR("Invalid slot number: " + std::to_string(slotNumber));
    return false;
  }

  std::string fileName = getSlotFileName(slotNumber);
  return save(fileName, player);
}

bool SaveGameManager::load(const std::string &saveFileName,
                           Player &player) const {
  // No need for null check with references - they're always valid

  // Check if the file exists
  std::string fullPath = getFullSavePath(saveFileName);
  if (!std::filesystem::exists(fullPath)) {
    SAVEGAME_ERROR("Save file does not exist: " + saveFileName);
    return false;
  }

  try {
    // Open binary file for reading
    std::ifstream file(fullPath, std::ios::binary | std::ios::in);
    if (!file.is_open()) {
      SAVEGAME_ERROR("Could not open file for reading: " + fullPath);
      return false;
    }

    // Read and validate header
    SaveGameHeader header;
    if (!readHeader(file, header)) {
      SAVEGAME_ERROR("Invalid save file format");
      file.close();
      return false;
    }

    // Read player data using BinarySerializer
    auto reader = std::make_unique<BinarySerial::Reader>(
        std::shared_ptr<std::istream>(&file, [](std::istream *) {}));

    // Read position
    Vector2D position(0.0f, 0.0f);
    if (!reader->readSerializable(position)) {
      SAVEGAME_ERROR("Error reading player position");
      file.close();
      return false;
    }

    // Apply position to player
    player.setVelocity(Vector2D(0, 0)); // Reset velocity
    player.setPosition(position);

    // Read textureID
    std::string textureID;
    if (!reader->readString(textureID)) {
      SAVEGAME_ERROR("Error reading player textureID!");
      file.close();
      return false;
    }

    // Read state
    std::string state;
    if (!reader->readString(state)) {
      SAVEGAME_ERROR("Error reading player state!");
      file.close();
      return false;
    }

    // Apply state to player
    player.changeState(state);

    // Read level ID (not using it yet, but reading for future use)
    std::string levelID;
    if (!reader->readString(levelID)) {
      SAVEGAME_ERROR("Error reading level ID!");
      file.close();
      return false;
    }

    file.close();

    SAVEGAME_INFO("Game loaded: " + saveFileName);
    return true;
  } catch (const std::exception &e) {
    SAVEGAME_ERROR("Error loading game: " + std::string(e.what()));
    return false;
  }
}

bool SaveGameManager::loadFromSlot(int slotNumber, Player &player) {
  if (slotNumber < 1) {
    SAVEGAME_ERROR("Invalid slot number: " + std::to_string(slotNumber));
    return false;
  }

  std::string fileName = getSlotFileName(slotNumber);
  return load(fileName, player);
}

bool SaveGameManager::deleteSave(const std::string &saveFileName) const {
  try {
    std::string fullPath = getFullSavePath(saveFileName);
    if (std::filesystem::exists(fullPath)) {
      std::filesystem::remove(fullPath);
      SAVEGAME_INFO("Save successful: " + saveFileName);
      return true;
    } else {
      SAVEGAME_ERROR("Save file does not exist: " + fullPath);
      return false;
    }
  } catch (const std::filesystem::filesystem_error &e) {
    SAVEGAME_ERROR("Error deleting save file: " + std::string(e.what()));
    return false;
  }
}

bool SaveGameManager::deleteSlot(int slotNumber) {
  if (slotNumber < 1) {
    SAVEGAME_ERROR("Invalid slot number: " + std::to_string(slotNumber));
    return false;
  }

  std::string fileName = getSlotFileName(slotNumber);
  return deleteSave(fileName);
}

std::vector<std::string> SaveGameManager::getSaveFiles() const {
  std::vector<std::string> saveFiles;
  saveFiles.reserve(10); // Reserve capacity for typical number of save files
  std::string savePath = m_saveDirectory + "/game_saves";

  // Check if the directory exists
  if (!std::filesystem::exists(savePath) ||
      !std::filesystem::is_directory(savePath)) {
    return saveFiles; // Return empty vector
  }

  try {
    // Iterate through all files in the directory
    for (const auto &entry : std::filesystem::directory_iterator(savePath)) {
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
  } catch (const std::filesystem::filesystem_error &e) {
    SAVEGAME_ERROR("Error listing save files: " + std::string(e.what()));
  }

  return saveFiles;
}

SaveGameData
SaveGameManager::getSaveInfo(const std::string &saveFileName) const {
  return extractSaveInfo(saveFileName);
}

std::vector<SaveGameData> SaveGameManager::getAllSaveInfo() const {
  std::vector<SaveGameData> saveInfoList;
  saveInfoList.reserve(10); // Reserve capacity for typical number of saves
  std::vector<std::string> files = getSaveFiles();

  for (const auto &file : files) {
    SaveGameData info = extractSaveInfo(file);
    saveInfoList.push_back(info);
  }

  return saveInfoList;
}

bool SaveGameManager::saveExists(const std::string &saveFileName) const {
  return std::filesystem::exists(getFullSavePath(saveFileName));
}

bool SaveGameManager::slotExists(int slotNumber) const {
  if (slotNumber < 1) {
    return false;
  }

  std::string fileName = getSlotFileName(slotNumber);
  return saveExists(fileName);
}

bool SaveGameManager::isValidSaveFile(const std::string &saveFileName) const {
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
    if (std::memcmp(header.signature, HAMMER_SAVE_SIGNATURE,
                    HAMMER_SAVE_SIGNATURE_SIZE) != 0) {
      file.close();
      return false;
    }

    file.close();
    return true;
  } catch (const std::exception &e) {
    return false;
  }
}

void SaveGameManager::setSaveDirectory(const std::string &directory) {
  // Check if directory exists
  if (!std::filesystem::exists(directory)) {
    // Try to create it
    try {
      if (!std::filesystem::create_directories(directory)) {
        SAVEGAME_ERROR("Failed to create directory: " + directory);
      }
    } catch (const std::exception &e) {
      SAVEGAME_ERROR("Error creating directory: " + std::string(e.what()));
    }
  }

  m_saveDirectory = directory;

  // Ensure the game_saves subdirectory exists right away
  ensureSaveDirectoryExists();
}

void SaveGameManager::clean() {
  if (m_isShutdown) {
    return;
  }

  // Set shutdown flag
  m_isShutdown = true;

  SAVEGAME_INFO("Save Game Manager resources cleaned!");
}

// Private helper methods
std::string SaveGameManager::getSlotFileName(int slotNumber) const {
  return "save_slot_" + std::to_string(slotNumber) + ".dat";
}

std::string
SaveGameManager::getFullSavePath(const std::string &saveFileName) const {
  return m_saveDirectory + "/game_saves/" + saveFileName;
}

bool SaveGameManager::ensureSaveDirectoryExists() const {
  try {
    // First ensure the base directory exists (which should be "res")
    if (!std::filesystem::exists(m_saveDirectory)) {
      // Try to create the base directory
      if (!std::filesystem::create_directories(m_saveDirectory)) {
        SAVEGAME_ERROR("Failed to create base directory");
        return false;
      }
    }

    // Create the game_saves directory inside the base directory
    std::string savePath = m_saveDirectory + "/game_saves";

    if (!std::filesystem::exists(savePath)) {
      // Create the directory and all parent directories
      if (!std::filesystem::create_directories(savePath)) {
        SAVEGAME_ERROR("Failed to create save directory");
        return false;
      }
    }

    // Verify directory exists after creation attempt
    if (!std::filesystem::exists(savePath)) {
      SAVEGAME_ERROR("Directory still doesn't exist after creation attempt");
      return false;
    }

    // Verify directory is writable by attempting to create a test file
    {
      std::string testFilePath = savePath + "/test_write.tmp";
      std::ofstream testFile(testFilePath);
      if (!testFile.is_open()) {
        SAVEGAME_ERROR("Directory exists but is not writable");
        return false;
      }
      testFile << "Test"; // Actually write something
      testFile.close();
      if (std::filesystem::exists(testFilePath)) {
        std::filesystem::remove(testFilePath);
      } else {
        SAVEGAME_ERROR("Test file was not created properly");
        return false;
      }
    }

    return true;
  } catch (const std::exception &e) {
    SAVEGAME_ERROR("Error creating save directory: " + std::string(e.what()));
    return false;
  }
}

SaveGameData
SaveGameManager::extractSaveInfo(const std::string &saveFileName) const {
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
      SAVEGAME_ERROR("Invalid save file format when extracting info!");
      file.close();
      return info;
    }

    // Set timestamp from header using thread-safe localtime
    std::tm timeinfo;
#ifdef _WIN32
    localtime_s(&timeinfo, &header.timestamp);
#else
    localtime_r(&header.timestamp, &timeinfo);
#endif
    char buffer[80];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeinfo);
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
  } catch (const std::exception &e) {
    SAVEGAME_ERROR("Error extracting save info: " + std::string(e.what()));
  }

  return info;
}

// Add the missing binary file operations
bool SaveGameManager::writeHeader(std::ofstream &file,
                                  uint32_t dataSize) const {
  SaveGameHeader header;
  // Initialize signature using the same constant
  std::memcpy(header.signature, HAMMER_SAVE_SIGNATURE,
              HAMMER_SAVE_SIGNATURE_SIZE);
  header.timestamp =
      std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  header.dataSize = dataSize;

  file.write(reinterpret_cast<const char *>(&header), sizeof(SaveGameHeader));
  return file.good();
}

bool SaveGameManager::readHeader(std::ifstream &file,
                                 SaveGameHeader &header) const {
  file.read(reinterpret_cast<char *>(&header), sizeof(SaveGameHeader));

  // Verify header signature using constexpr array
  if (std::memcmp(header.signature, HAMMER_SAVE_SIGNATURE,
                  HAMMER_SAVE_SIGNATURE_SIZE) != 0) {
    return false;
  }

  return file.good();
}

// These methods are now replaced by BinarySerializer - kept for compatibility
// if needed elsewhere
bool SaveGameManager::writeString(std::ofstream &file,
                                  const std::string &str) const {
  auto writer = std::make_unique<BinarySerial::Writer>(
      std::shared_ptr<std::ostream>(&file, [](std::ostream *) {}));
  return writer->writeString(str);
}

bool SaveGameManager::readString(std::ifstream &file, std::string &str) const {
  auto reader = std::make_unique<BinarySerial::Reader>(
      std::shared_ptr<std::istream>(&file, [](std::istream *) {}));
  return reader->readString(str);
}

bool SaveGameManager::writeVector2D(std::ofstream &file,
                                    const Vector2D &vec) const {
  auto writer = std::make_unique<BinarySerial::Writer>(
      std::shared_ptr<std::ostream>(&file, [](std::ostream *) {}));
  return writer->writeSerializable(vec);
}

bool SaveGameManager::readVector2D(std::ifstream &file, Vector2D &vec) const {
  auto reader = std::make_unique<BinarySerial::Reader>(
      std::shared_ptr<std::istream>(&file, [](std::istream *) {}));
  return reader->readSerializable(vec);
}
