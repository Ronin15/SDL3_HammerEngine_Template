/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#ifndef SOUND_MANAGER_HPP
#define SOUND_MANAGER_HPP

#include <SDL3/SDL.h>
#include <SDL3_mixer/SDL_mixer.h>
#include <boost/container/flat_map.hpp>
#include <atomic>
#include <string>

class SoundManager {
 public:
  ~SoundManager();

  static SoundManager& Instance() {
    static SoundManager instance;
    return instance;
  }

  // Initialize the SoundManager
  bool init();

  // Loads a sound effect from a file or all sound effects from a directory
  // If dirPath is a directory, it loads all supported sound files from that directory
  // Returns true if at least one sound was loaded successfully
  // When loading a directory, soundID is used as a prefix for filenames
  bool loadSFX(const std::string& fileName,
                     const std::string& soundID);

  // Loads a music file
  bool loadMusic(const std::string& fileName,
                 const std::string& musicID);

  // Play a sound effect
  void playSFX(const std::string& soundID,
               int loops = 0,
               int volume = 100);

  // Play music
  void playMusic(const std::string& musicID,
                 int loops = -1,
                 int volume = 100);

  // Pause music
  void pauseMusic();

  // Resume music
  void resumeMusic();

  // Stop music
  void stopMusic();

  // Check if music is playing
  bool isMusicPlaying() const;

  // Set music volume (0-128)
  void setMusicVolume(int volume);

  // Set sound effect volume (0-128)
  void setSFXVolume(int volume);

  // Clean up resources
  void clean();

  // Remove a sound effect from the map
  void clearSFX(const std::string& soundID);

  // Remove music from the map
  void clearMusic(const std::string& musicID);

  // Check if a sound is loaded
  bool isSFXLoaded(const std::string& soundID) const;

  // Check if music is loaded
  bool isMusicLoaded(const std::string& musicID) const;
  
  // Get current music volume
  int getMusicVolume() const { return m_musicVolume; }
  
  // Get current SFX volume
  int getSFXVolume() const { return m_sfxVolume; }

 private:
  boost::container::flat_map<std::string, Mix_Chunk*> m_sfxMap{};
  boost::container::flat_map<std::string, Mix_Music*> m_musicMap{};
  SDL_AudioDeviceID m_deviceId{0};
  bool m_initialized{false};
  std::atomic<bool> m_isShutdown{false};
  int m_musicVolume{100};
  int m_sfxVolume{100};

  // Delete copy constructor and assignment operator
  SoundManager(const SoundManager&) = delete; // Prevent copying
  SoundManager& operator=(const SoundManager&) = delete; // Prevent assignment

  SoundManager();
};

#endif // SOUND_MANAGER_HPP
