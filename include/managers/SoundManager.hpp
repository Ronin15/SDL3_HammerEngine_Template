/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#ifndef SOUND_MANAGER_HPP
#define SOUND_MANAGER_HPP

#include <SDL3/SDL.h>
#include <SDL3_mixer/SDL_mixer.h>
#include <unordered_map>
#include <atomic>
#include <string>

class SoundManager {
 public:
  ~SoundManager();

  static SoundManager& Instance() {
    static SoundManager instance;
    return instance;
  }

  /**
   * @brief Initializes the SoundManager and SDL audio subsystem
   * @return true if initialization successful, false otherwise
   */
  bool init();

  /**
   * @brief Loads a sound effect from a file or all sound effects from a directory
   * @param filePath Path to sound file or directory containing supported audio files
   * @param soundID Unique identifier for the sound(s). Used as prefix when loading directory
   * @return true if at least one sound was loaded successfully, false otherwise
   */
  bool loadSFX(const std::string& filePath,
                     const std::string& soundID);

  /**
   * @brief Loads a music file for background music playback
   * @param filePath Path to music file or directory containing music files
   * @param musicID Unique identifier for the music track(s)
   * @return true if music was loaded successfully, false otherwise
   */
  bool loadMusic(const std::string& filePath,
                 const std::string& musicID);

  /**
   * @brief Plays a loaded sound effect
   * @param soundID Unique identifier of the sound effect to play
   * @param loops Number of additional loops to play (0 = play once, default: 0)
   * @param volume Volume level from 0-128 (default: 100)
   */
  void playSFX(const std::string& soundID,
               int loops = 0,
               int volume = 100);

  /**
   * @brief Plays a loaded music track
   * @param musicID Unique identifier of the music track to play
   * @param loops Number of loops (-1 = infinite loop, default: -1)
   * @param volume Volume level from 0-128 (default: 100)
   */
  void playMusic(const std::string& musicID,
                 int loops = -1,
                 int volume = 100);

  /**
   * @brief Pauses currently playing music
   */
  void pauseMusic();

  /**
   * @brief Resumes paused music playback
   */
  void resumeMusic();

  /**
   * @brief Stops currently playing music
   */
  void stopMusic();

  /**
   * @brief Checks if music is currently playing
   * @return true if music is playing, false otherwise
   */
  bool isMusicPlaying() const;

  /**
   * @brief Sets the global music volume level
   * @param volume Volume level from 0-128
   */
  void setMusicVolume(int volume);

  /**
   * @brief Sets the global sound effects volume level
   * @param volume Volume level from 0-128
   */
  void setSFXVolume(int volume);

  /**
   * @brief Cleans up all audio resources and shuts down SDL audio subsystem
   */
  void clean();

  /**
   * @brief Removes a sound effect from memory
   * @param soundID Unique identifier of the sound effect to remove
   */
  void clearSFX(const std::string& soundID);

  /**
   * @brief Removes a music track from memory
   * @param musicID Unique identifier of the music track to remove
   */
  void clearMusic(const std::string& musicID);

  /**
   * @brief Checks if a sound effect is loaded in memory
   * @param soundID Unique identifier of the sound effect to check
   * @return true if sound effect is loaded, false otherwise
   */
  bool isSFXLoaded(const std::string& soundID) const;

  /**
   * @brief Checks if a music track is loaded in memory
   * @param musicID Unique identifier of the music track to check
   * @return true if music track is loaded, false otherwise
   */
  bool isMusicLoaded(const std::string& musicID) const;
  
  /**
   * @brief Gets the current music volume level
   * @return Current music volume (0-128)
   */
  int getMusicVolume() const { return m_musicVolume; }
  
  /**
   * @brief Gets the current sound effects volume level
   * @return Current SFX volume (0-128)
   */
  int getSFXVolume() const { return m_sfxVolume; }
  
  /**
   * @brief Checks if SoundManager has been shut down
   * @return true if manager is shut down, false otherwise
   */
  bool isShutdown() const { return m_isShutdown.load(); }

 private:
  std::unordered_map<std::string, Mix_Chunk*> m_sfxMap{};
  std::unordered_map<std::string, Mix_Music*> m_musicMap{};
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
