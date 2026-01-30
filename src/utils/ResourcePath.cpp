/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "utils/ResourcePath.hpp"
#include "core/Logger.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <filesystem>
#include <format>

namespace HammerEngine {

namespace fs = std::filesystem;

// Static member definitions
std::vector<ResourcePath::SearchPath> ResourcePath::s_searchPaths;
bool ResourcePath::s_initialized = false;
bool ResourcePath::s_isBundle = false;

void ResourcePath::init() {
    if (s_initialized) {
        return;
    }

    detectExecutionContext();
    s_initialized = true;

    if (s_isBundle) {
        RESOURCEPATH_INFO("Running from macOS app bundle");
    } else {
        RESOURCEPATH_INFO("Running from direct execution");
    }

    RESOURCEPATH_INFO(std::format("Base path = {}", getBasePath()));
}

void ResourcePath::detectExecutionContext() {
    std::string basePath = getExecutablePath();

    // Normalize to generic format for cross-platform string matching
    fs::path exePath(basePath);
    std::string exePathStr = exePath.generic_string();

    // Check for macOS bundle structure: .app/Contents in path
    // SDL_GetBasePath may return either MacOS/ or Contents/ depending on version
    s_isBundle = (exePathStr.find(".app/Contents") != std::string::npos);

    if (s_isBundle) {
        // Find the Contents directory and add Resources as search path
        fs::path currentPath(basePath);

        // Navigate to Contents if we're in MacOS subdirectory
        while (!currentPath.empty() && currentPath.filename() != "Contents") {
            currentPath = currentPath.parent_path();
        }

        if (!currentPath.empty()) {
            fs::path resourcesPath = currentPath / "Resources";
            addSearchPath(resourcesPath.string(), 0);
        }
    } else {
        // Running directly - SDL_GetBasePath gives us the executable's directory
        // For development, we want the project root (parent of bin/)
        fs::path binPath(basePath);

        // Check if we're in a bin/debug or bin/release structure
        std::string binPathStr = binPath.generic_string();
        if (binPathStr.find("/bin/debug/") != std::string::npos ||
            binPathStr.find("/bin/release/") != std::string::npos ||
            binPathStr.find("/bin/profile/") != std::string::npos) {
            // Go up two levels: bin/debug -> bin -> project root
            fs::path projectRoot = binPath.parent_path().parent_path();
            addSearchPath(projectRoot.string(), 0);
        } else {
            // Use the executable's directory as-is
            addSearchPath(basePath, 0);
        }
    }
}

std::string ResourcePath::getExecutablePath() {
    // SDL_GetBasePath returns the directory containing the executable
    // SDL3 returns const char* (static storage, no need to free)
    const char* basePath = SDL_GetBasePath();
    if (basePath && basePath[0] != '\0') {
        return std::string(basePath);
    }

    // Fallback to current working directory
    return fs::current_path().string();
}

std::string ResourcePath::resolve(const std::string& relativePath) {
    if (!s_initialized) {
        // Fall back to relative path if not initialized
        return relativePath;
    }

    // Check each search path in priority order (highest first)
    for (const auto& searchPath : s_searchPaths) {
        fs::path fullPath = fs::path(searchPath.path) / relativePath;
        if (fs::exists(fullPath)) {
            return fullPath.string();
        }
    }

    // Not found in any search path - return relative path as fallback
    // This allows the caller's error handling to report the missing resource
    return relativePath;
}

bool ResourcePath::exists(const std::string& relativePath) {
    if (!s_initialized) {
        return fs::exists(relativePath);
    }

    for (const auto& searchPath : s_searchPaths) {
        fs::path fullPath = fs::path(searchPath.path) / relativePath;
        if (fs::exists(fullPath)) {
            return true;
        }
    }

    return false;
}

void ResourcePath::addSearchPath(const std::string& path, int priority) {
    // Check if path already exists
    auto it = std::find_if(s_searchPaths.begin(), s_searchPaths.end(),
        [&path](const SearchPath& sp) { return sp.path == path; });

    if (it != s_searchPaths.end()) {
        // Update priority if already exists
        it->priority = priority;
    } else {
        // Add new path
        s_searchPaths.push_back({path, priority});
    }

    // Sort by priority (highest first)
    std::sort(s_searchPaths.begin(), s_searchPaths.end(),
        [](const SearchPath& a, const SearchPath& b) {
            return a.priority > b.priority;
        });
}

void ResourcePath::removeSearchPath(const std::string& path) {
    s_searchPaths.erase(
        std::remove_if(s_searchPaths.begin(), s_searchPaths.end(),
            [&path](const SearchPath& sp) { return sp.path == path; }),
        s_searchPaths.end());
}

std::string ResourcePath::getBasePath() {
    if (s_searchPaths.empty()) {
        return "";
    }
    return s_searchPaths.front().path;
}

bool ResourcePath::isRunningFromBundle() {
    return s_isBundle;
}

} // namespace HammerEngine
