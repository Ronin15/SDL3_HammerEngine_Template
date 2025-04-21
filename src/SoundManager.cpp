#include "SoundManager.hpp"
#include <iostream>
#include <filesystem>
#include <algorithm>

SoundManager* SoundManager::sp_Instance{nullptr};

SoundManager::SoundManager() : m_deviceId(0), m_initialized(false) {
}

SoundManager::~SoundManager() {
  clean();
}

bool SoundManager::init() {
  // Initialize SDL_mixer
  if (!SDL_Init(SDL_INIT_AUDIO)) {
    std::cerr << "Error initializing SDL Audio: " << SDL_GetError() << std::endl;
    return false;
  }

  // Initialize SDL3_mixer with default settings
  MIX_InitFlags initFlags = Mix_Init(MIX_INIT_MP3 | MIX_INIT_OGG);
  if (initFlags == 0) {
    std::cerr << "Error initializing SDL_mixer: " << SDL_GetError() << std::endl;
    SDL_Quit();
    return false;
  }

  // Open audio device
  SDL_AudioSpec desired_spec;
  SDL_zero(desired_spec);
  desired_spec.freq = 44100;
  desired_spec.format = SDL_AUDIO_F32;
  desired_spec.channels = 2;

  m_deviceId = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &desired_spec);
  if (!m_deviceId) {
    std::cerr << "Error opening audio device: " << SDL_GetError() << std::endl;
    Mix_Quit();
    SDL_Quit();
    return false;
  }

  // Initialize SDL_mixer with the opened audio device
  if (!Mix_OpenAudio(m_deviceId, &desired_spec)) {
    std::cerr << "Error initializing SDL_mixer: " << SDL_GetError() << std::endl;
    SDL_CloseAudioDevice(m_deviceId);
    Mix_Quit();
    SDL_Quit();
    return false;
  }

  std::cout << "Forge Game Engine - Sound system initialized successfully" << std::endl;
  m_initialized = true;
  return true;
}

bool SoundManager::loadSFX(std::string filePath, std::string soundID) {
  // Check if the filePath is a directory
  if (std::filesystem::exists(filePath) && std::filesystem::is_directory(filePath)) {
    std::cout << "Forge Game Engine - Loading sound effects from directory: " << filePath << "\n";

    bool loadedAny = false;
    int soundsLoaded = 0;

    try {
      // Iterate through all files in the directory
      for (const auto& entry : std::filesystem::directory_iterator(filePath)) {
        if (!entry.is_regular_file()) {
          continue; // Skip directories and special files
        }

        // Get file path and extension
        std::filesystem::path path = entry.path();
        std::string extension = path.extension().string();

        // Convert extension to lowercase for case-insensitive comparison
        std::transform(extension.begin(), extension.end(), extension.begin(),
                      [](unsigned char c) { return std::tolower(c); });

        // Check if the file has a supported audio extension
        if (extension == ".wav" || extension == ".mp3" || extension == ".ogg") {
          std::string fullPath = path.string();
          std::string filename = path.stem().string(); // Get filename without extension

          // Create sound ID by combining the provided prefix and filename
          std::string combinedID = soundID.empty() ? filename : soundID + "_" + filename;
          //std::cout << "Forge Game Engine - Loading sound ID: " << combinedID << "!\n";
          // Load the individual file as a sound
          Mix_Chunk* p_chunk = Mix_LoadWAV(fullPath.c_str());

          std::cout << "Forge Game Engine - Loading sound effect: " << fullPath << "!\n";

          if (p_chunk == nullptr) {
            std::cout << "Forge Game Engine - Could not load sound effect: " << SDL_GetError() << "\n";
            continue;
          }

          m_sfxMap[combinedID] = p_chunk;
          loadedAny = true;
          soundsLoaded++;
        }
      }
    } catch (const std::filesystem::filesystem_error& e) {
      std::cout << "Forge Game Engine - Filesystem error: " << e.what() << "\n";
    } catch (const std::exception& e) {
      std::cout << "Forge Game Engine - Error while loading sound effects: " << e.what() << "\n";
    }

    std::cout << "Forge Game Engine - Loaded " << soundsLoaded << " sound effects from directory: " << filePath << "\n";
    return loadedAny; // Return true if at least one sound was loaded successfully
  }

  // Standard single file loading code
  Mix_Chunk* p_chunk = Mix_LoadWAV(filePath.c_str());

  std::cout << "Forge Game Engine - Loading sound effect: " << filePath << "! ID: " << soundID << "\n";

  if (p_chunk == nullptr) {
    std::cout << "Forge Game Engine - Could not load sound effect: " << SDL_GetError() << "\n";
    return false;
  }

  m_sfxMap[soundID] = p_chunk;
  return true;
}

bool SoundManager::loadMusic(std::string filePath, std::string musicID) {
  Mix_Music* p_music = Mix_LoadMUS(filePath.c_str());

  std::cout << "Forge Game Engine - Loading music: " << filePath << "!\n";

  if (p_music == nullptr) {
    std::cout << "Forge Game Engine - Could not load music: " << SDL_GetError() << "\n";
    return false;
  }

  m_musicMap[musicID] = p_music;
  return true;
}

void SoundManager::playSFX(std::string soundID, int loops, int volume) {
  if (!m_initialized) {
    std::cout << "Forge Game Engine - Sound system not initialized!\n";
    return;
  }

  if (m_sfxMap.find(soundID) == m_sfxMap.end()) {
    std::cout << "Forge Game Engine - Sound effect not found: " << soundID << "\n";
    return;
  }

  // Set volume (0-128)
  Mix_VolumeChunk(m_sfxMap[soundID], volume);

  // Play the sound effect (-1 means first available channel)
  if (Mix_PlayChannel(-1, m_sfxMap[soundID], loops) == -1) {
    std::cout << "Forge Game Engine - Could not play sound effect: " << SDL_GetError() << "\n";
  }
}

void SoundManager::playMusic(std::string musicID, int loops, int volume) {
  if (!m_initialized) {
    std::cout << "Forge Game Engine - Sound system not initialized!\n";
    return;
  }

  if (m_musicMap.find(musicID) == m_musicMap.end()) {
    std::cout << "Forge Game Engine - Music not found: " << musicID << "\n";
    return;
  }

  // Set volume (0-128)
  Mix_VolumeMusic(volume);

  // Play the music
  if (Mix_PlayMusic(m_musicMap[musicID], loops) != 0) {
    std::cout << "Forge Game Engine - Could not play music: " << SDL_GetError() << "\n";
  }
}

void SoundManager::pauseMusic() {
  if (Mix_PlayingMusic()) {
    Mix_PauseMusic();
  }
}

void SoundManager::resumeMusic() {
  if (Mix_PausedMusic()) {
    Mix_ResumeMusic();
  }
}

void SoundManager::stopMusic() {
  Mix_HaltMusic();
}

bool SoundManager::isMusicPlaying() {
  return Mix_PlayingMusic() != 0;
}

void SoundManager::setMusicVolume(int volume) {
  // Ensure volume is in the valid range (0-128)
  volume = (volume < 0) ? 0 : (volume > 128) ? 128 : volume;
  Mix_VolumeMusic(volume);
}

void SoundManager::setSFXVolume(int volume) {
  // Ensure volume is in the valid range (0-128)
  volume = (volume < 0) ? 0 : (volume > 128) ? 128 : volume;
  Mix_MasterVolume(volume);
}

void SoundManager::clean() {
  // Free all loaded sound effects
  for (auto& sfxPair : m_sfxMap) {
    Mix_FreeChunk(sfxPair.second);
  }
  m_sfxMap.clear();

  // Free all loaded music
  for (auto& musicPair : m_musicMap) {
    Mix_FreeMusic(musicPair.second);
  }
  m_musicMap.clear();

  // Close SDL_mixer and SDL audio
  if (m_initialized) {
    Mix_CloseAudio();
    Mix_Quit();
    if (m_deviceId) {
      SDL_CloseAudioDevice(m_deviceId);
    }
    m_initialized = false;
  }
}

void SoundManager::clearSFX(std::string soundID) {
  if (m_sfxMap.find(soundID) != m_sfxMap.end()) {
    Mix_FreeChunk(m_sfxMap[soundID]);
    m_sfxMap.erase(soundID);
    std::cout << "Forge Game Engine - Cleared : " << soundID << " sound effect" << std::endl;
  }
}

void SoundManager::clearMusic(std::string musicID) {
  if (m_musicMap.find(musicID) != m_musicMap.end()) {
    Mix_FreeMusic(m_musicMap[musicID]);
    m_musicMap.erase(musicID);
    std::cout << "Forge Game Engine - Cleared : " << musicID << " music" << std::endl;
  }
}

bool SoundManager::isSFXLoaded(std::string soundID) {
  return m_sfxMap.find(soundID) != m_sfxMap.end();
}

bool SoundManager::isMusicLoaded(std::string musicID) {
  return m_musicMap.find(musicID) != m_musicMap.end();
}
