/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include "managers/SettingsManager.hpp"
#include "core/Logger.hpp"
#include "utils/JsonReader.hpp"
#include <algorithm>
#include <fstream>
#include <sstream>

namespace HammerEngine {

bool SettingsManager::loadFromFile(const std::string& filepath) {
    JsonReader reader;
    if (!reader.loadFromFile(filepath)) {
        SETTINGS_ERROR("Failed to load settings from file: " + filepath + " - " + reader.getLastError());
        return false;
    }

    const JsonValue& root = reader.getRoot();
    if (!root.isObject()) {
        SETTINGS_ERROR("Settings file root is not a JSON object: " + filepath);
        return false;
    }

    std::unique_lock<std::shared_mutex> lock(m_settingsMutex);

    // Parse each category
    const JsonObject& rootObj = root.asObject();
    for (const auto& [categoryName, categoryValue] : rootObj) {
        if (!categoryValue.isObject()) {
            SETTINGS_WARNING("Category '" + categoryName + "' is not an object, skipping");
            continue;
        }

        const JsonObject& categoryObj = categoryValue.asObject();
        for (const auto& [key, value] : categoryObj) {
            SettingValue settingValue;

            // Convert JSON value to SettingValue variant
            if (value.isBool()) {
                settingValue = value.asBool();
            } else if (value.isNumber()) {
                double numValue = value.asNumber();
                // Check if it's an integer value
                if (numValue == static_cast<int>(numValue)) {
                    settingValue = static_cast<int>(numValue);
                } else {
                    settingValue = static_cast<float>(numValue);
                }
            } else if (value.isString()) {
                settingValue = value.asString();
            } else {
                SETTINGS_WARNING("Unsupported value type for setting '" + categoryName + "." + key + "', skipping");
                continue;
            }

            m_settings[categoryName][key] = settingValue;
        }
    }

    SETTINGS_INFO("Loaded settings from file: " + filepath);
    return true;
}

bool SettingsManager::saveToFile(const std::string& filepath) {
    std::shared_lock<std::shared_mutex> lock(m_settingsMutex);

    std::ofstream file(filepath);
    if (!file.is_open()) {
        SETTINGS_ERROR("Failed to open settings file for writing: " + filepath);
        return false;
    }

    // Build JSON manually for formatting control
    file << "{\n";

    size_t categoryIndex = 0;
    for (const auto& [categoryName, categorySettings] : m_settings) {
        file << "  \"" << categoryName << "\": {\n";

        size_t keyIndex = 0;
        for (const auto& [key, value] : categorySettings) {
            file << "    \"" << key << "\": ";

            // Write value based on type
            std::visit([&file](auto&& arg) {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, bool>) {
                    file << (arg ? "true" : "false");
                } else if constexpr (std::is_same_v<T, int>) {
                    file << arg;
                } else if constexpr (std::is_same_v<T, float>) {
                    file << arg;
                } else if constexpr (std::is_same_v<T, std::string>) {
                    file << "\"" << arg << "\"";
                }
            }, value);

            if (keyIndex < categorySettings.size() - 1) {
                file << ",";
            }
            file << "\n";
            keyIndex++;
        }

        file << "  }";
        if (categoryIndex < m_settings.size() - 1) {
            file << ",";
        }
        file << "\n";
        categoryIndex++;
    }

    file << "}\n";
    file.close();

    SETTINGS_INFO("Saved settings to file: " + filepath);
    return true;
}

bool SettingsManager::has(const std::string& category, const std::string& key) const {
    std::shared_lock<std::shared_mutex> lock(m_settingsMutex);

    auto categoryIt = m_settings.find(category);
    if (categoryIt == m_settings.end()) {
        return false;
    }

    return categoryIt->second.find(key) != categoryIt->second.end();
}

bool SettingsManager::remove(const std::string& category, const std::string& key) {
    std::unique_lock<std::shared_mutex> lock(m_settingsMutex);

    auto categoryIt = m_settings.find(category);
    if (categoryIt == m_settings.end()) {
        return false;
    }

    auto keyIt = categoryIt->second.find(key);
    if (keyIt == categoryIt->second.end()) {
        return false;
    }

    categoryIt->second.erase(keyIt);

    // Remove category if empty
    if (categoryIt->second.empty()) {
        m_settings.erase(categoryIt);
    }

    return true;
}

bool SettingsManager::clearCategory(const std::string& category) {
    std::unique_lock<std::shared_mutex> lock(m_settingsMutex);

    auto categoryIt = m_settings.find(category);
    if (categoryIt == m_settings.end()) {
        return false;
    }

    m_settings.erase(categoryIt);
    return true;
}

void SettingsManager::clearAll() {
    std::unique_lock<std::shared_mutex> lock(m_settingsMutex);
    m_settings.clear();
}

size_t SettingsManager::registerChangeListener(const std::string& category, ChangeCallback callback) {
    std::lock_guard<std::mutex> lock(m_listenersMutex);

    size_t id = m_nextCallbackId++;
    m_listeners.push_back({id, category, std::move(callback)});

    return id;
}

void SettingsManager::unregisterChangeListener(size_t callbackId) {
    std::lock_guard<std::mutex> lock(m_listenersMutex);

    m_listeners.erase(
        std::remove_if(m_listeners.begin(), m_listeners.end(),
            [callbackId](const ListenerInfo& info) {
                return info.id == callbackId;
            }),
        m_listeners.end()
    );
}

std::vector<std::string> SettingsManager::getCategories() const {
    std::shared_lock<std::shared_mutex> lock(m_settingsMutex);

    std::vector<std::string> categories;
    categories.reserve(m_settings.size());

    for (const auto& [category, _] : m_settings) {
        categories.push_back(category);
    }

    return categories;
}

std::vector<std::string> SettingsManager::getKeys(const std::string& category) const {
    std::shared_lock<std::shared_mutex> lock(m_settingsMutex);

    auto categoryIt = m_settings.find(category);
    if (categoryIt == m_settings.end()) {
        return {};
    }

    std::vector<std::string> keys;
    keys.reserve(categoryIt->second.size());

    for (const auto& [key, _] : categoryIt->second) {
        keys.push_back(key);
    }

    return keys;
}

void SettingsManager::notifyListeners(const std::string& category, const std::string& key, const SettingValue& newValue) {
    std::lock_guard<std::mutex> lock(m_listenersMutex);

    for (const auto& listener : m_listeners) {
        // Call listener if it's watching all categories or this specific category
        if (listener.category.empty() || listener.category == category) {
            listener.callback(category, key, newValue);
        }
    }
}

std::string SettingsManager::variantToString(const SettingValue& value) const {
    return std::visit([](auto&& arg) -> std::string {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, bool>) {
            return arg ? "true" : "false";
        } else if constexpr (std::is_same_v<T, int>) {
            return std::to_string(arg);
        } else if constexpr (std::is_same_v<T, float>) {
            return std::to_string(arg);
        } else if constexpr (std::is_same_v<T, std::string>) {
            return arg;
        }
        return "";
    }, value);
}

} // namespace HammerEngine
