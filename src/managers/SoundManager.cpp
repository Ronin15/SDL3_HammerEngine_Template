/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "managers/SoundManager.hpp"
#include "core/Logger.hpp"
#include <algorithm>
#include <filesystem>

SoundManager::SoundManager() {
  // Member variables are already initialized in the header with brace
  // initialization
}

SoundManager::~SoundManager() {
  // Only clean up if not already shut down
  if (!m_isShutdown) {
    clean();
  }
}

bool SoundManager::init() {
  if (m_initialized) {
    SOUND_WARN("Already initialized");
    return true;
  }

  // Initialize SDL Audio if not already initialized
  if (!SDL_WasInit(SDL_INIT_AUDIO)) {
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
      SOUND_ERROR("Failed to initialize SDL audio subsystem");
      return false;
    }
  }

  // Initialize SDL3_mixer library
  if (!MIX_Init()) {
    SOUND_ERROR("Failed to initialize SDL3_mixer library");
    return false;
  }

  // Create SDL3_mixer instance for audio device output
  m_mixer = MIX_CreateMixerDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr);
  if (!m_mixer) {
    SOUND_ERROR("Failed to create SDL3_mixer instance");
    MIX_Quit();
    return false;
  }

  // Create separate groups for SFX and music
  m_sfxGroup = MIX_CreateGroup(m_mixer);
  if (!m_sfxGroup) {
    SOUND_ERROR("Failed to create SFX group");
    MIX_DestroyMixer(m_mixer);
    MIX_Quit();
    m_mixer = nullptr;
    return false;
  }

  m_musicGroup = MIX_CreateGroup(m_mixer);
  if (!m_musicGroup) {
    SOUND_ERROR("Failed to create music group");
    MIX_DestroyGroup(m_sfxGroup);
    MIX_DestroyMixer(m_mixer);
    MIX_Quit();
    m_sfxGroup = nullptr;
    m_mixer = nullptr;
    return false;
  }

  m_initialized = true;
  SOUND_INFO(
      "Successfully initialized SDL3_mixer with separate SFX and music groups");
  return true;
}

std::vector<std::string> SoundManager::getSupportedExtensions() const {
  return {".wav", ".mp3", ".ogg", ".flac", ".aiff", ".voc"};
}

bool SoundManager::loadSFX(const std::string &filePath,
                           const std::string &soundID) {
  if (!m_initialized) {
    SOUND_ERROR("Not initialized. Call init() first.");
    return false;
  }

  namespace fs = std::filesystem;

  try {
    if (fs::is_directory(filePath)) {
      // Load all supported audio files from directory
      bool loadedAny = false;
      int fileCount = 0;
      const auto supportedExts = getSupportedExtensions();

      for (const auto &entry : fs::directory_iterator(filePath)) {
        if (!entry.is_regular_file())
          continue;

        const std::string extension = entry.path().extension().string();
        if (std::find(supportedExts.begin(), supportedExts.end(), extension) !=
            supportedExts.end()) {
          const std::string fileName = entry.path().stem().string();
          const std::string fullSoundID = soundID + "_" + fileName;

          MIX_Audio *audio =
              MIX_LoadAudio(m_mixer, entry.path().string().c_str(), false);
          if (!audio) {
            SOUND_WARN("Failed to load SFX from " + entry.path().string());
            continue;
          }

          // Free existing audio if it exists
          auto it = m_audioMap.find(fullSoundID);
          if (it != m_audioMap.end()) {
            MIX_DestroyAudio(it->second);
          }

          m_audioMap[fullSoundID] = audio;
          fileCount++;
          loadedAny = true;
        }
      }

      if (loadedAny) {
        SOUND_INFO("Loaded " + std::to_string(fileCount) +
                   " sound effects from directory: " + filePath);
      } else {
        SOUND_WARN("No supported audio files found in directory: " + filePath);
      }

      return loadedAny;
    } else {
      // Load single file
      MIX_Audio *audio = MIX_LoadAudio(m_mixer, filePath.c_str(), false);
      if (!audio) {
        SOUND_ERROR("Failed to load SFX: " + filePath);
        return false;
      }

      // Free existing audio if it exists
      auto it = m_audioMap.find(soundID);
      if (it != m_audioMap.end()) {
        MIX_DestroyAudio(it->second);
      }

      m_audioMap[soundID] = audio;
      SOUND_INFO("Loaded sound effect: " + soundID);
      return true;
    }
  } catch (const fs::filesystem_error &e) {
    SOUND_ERROR("Filesystem error loading SFX: " + filePath);
    return false;
  }
}

bool SoundManager::loadMusic(const std::string &filePath,
                             const std::string &musicID) {
  // Use the same loading mechanism as SFX but with a music prefix
  return loadSFX(filePath, "music_" + musicID);
}

MIX_Track *SoundManager::createAndConfigureTrack(MIX_Group *group,
                                                 const std::string &tag) {
  MIX_Track *track = MIX_CreateTrack(m_mixer);
  if (!track) {
    return nullptr;
  }

  // Set the track's group
  if (!MIX_SetTrackGroup(track, group)) {
    MIX_DestroyTrack(track);
    return nullptr;
  }

  // Tag the track for easier management
  if (!MIX_TagTrack(track, tag.c_str())) {
    MIX_DestroyTrack(track);
    return nullptr;
  }

  return track;
}

void SoundManager::cleanupStoppedTracks() {
  // Clean up stopped SFX tracks
  for (auto it = m_activeSfxTracks.begin(); it != m_activeSfxTracks.end();) {
    auto &tracks = it->second;
    tracks.erase(std::remove_if(tracks.begin(), tracks.end(),
                                [this](MIX_Track *track) {
                                  if (!MIX_TrackPlaying(track)) {
                                    m_trackToAudioMap.erase(track);
                                    MIX_UntagTrack(track, "sfx");
                                    MIX_DestroyTrack(track);
                                    return true;
                                  }
                                  return false;
                                }),
                 tracks.end());

    if (tracks.empty()) {
      it = m_activeSfxTracks.erase(it);
    } else {
      ++it;
    }
  }

  // Clean up stopped music tracks
  m_activeMusicTracks.erase(std::remove_if(m_activeMusicTracks.begin(),
                                           m_activeMusicTracks.end(),
                                           [this](MIX_Track *track) {
                                             if (!MIX_TrackPlaying(track)) {
                                               m_trackToAudioMap.erase(track);
                                               MIX_UntagTrack(track, "music");
                                               MIX_DestroyTrack(track);
                                               return true;
                                             }
                                             return false;
                                           }),
                            m_activeMusicTracks.end());
}

void SoundManager::playSFX(const std::string &soundID, int loops,
                           float volume) {
  if (!m_initialized) {
    SOUND_WARN("Not initialized. Call init() first.");
    return;
  }

  auto it = m_audioMap.find(soundID);
  if (it == m_audioMap.end()) {
    SOUND_WARN("Sound effect not found: " + soundID);
    return;
  }

  cleanupStoppedTracks();

  // Create a new track for this SFX
  MIX_Track *track = createAndConfigureTrack(m_sfxGroup, "sfx");
  if (!track) {
    SOUND_ERROR("Failed to create track for SFX: " + soundID);
    return;
  }

  // Set track audio
  if (!MIX_SetTrackAudio(track, it->second)) {
    SOUND_ERROR("Failed to set track audio for SFX: " + soundID);
    MIX_UntagTrack(track, "sfx");
    MIX_DestroyTrack(track);
    return;
  }

  // Set track volume (combine with global SFX volume)
  float finalVolume = std::clamp(volume * m_sfxVolume, 0.0f, 10.0f);
  if (!MIX_SetTrackGain(track, finalVolume)) {
    SOUND_WARN("Failed to set track volume for SFX: " + soundID);
  }

  // Configure playback properties
  SDL_PropertiesID props = SDL_CreateProperties();
  if (loops > 0) {
    SDL_SetNumberProperty(props, MIX_PROP_PLAY_LOOPS_NUMBER, loops);
  }

  // Start playback
  if (!MIX_PlayTrack(track, props)) {
    SOUND_ERROR("Failed to play SFX: " + soundID);
    MIX_UntagTrack(track, "sfx");
    MIX_DestroyTrack(track);
    SDL_DestroyProperties(props);
    return;
  }

  SDL_DestroyProperties(props);

  // Track the active sound
  m_activeSfxTracks[soundID].push_back(track);
  m_trackToAudioMap[track] = soundID;
}

void SoundManager::playMusic(const std::string &musicID, int loops,
                             float volume) {
  if (!m_initialized) {
    SOUND_WARN("Not initialized. Call init() first.");
    return;
  }

  const std::string fullMusicID = "music_" + musicID;
  auto it = m_audioMap.find(fullMusicID);
  if (it == m_audioMap.end()) {
    SOUND_WARN("Music track not found: " + musicID);
    return;
  }

  cleanupStoppedTracks();

  // Stop any currently playing music
  stopMusic();

  // Create a new track for this music
  MIX_Track *track = createAndConfigureTrack(m_musicGroup, "music");
  if (!track) {
    SOUND_ERROR("Failed to create track for music: " + musicID);
    return;
  }

  // Set track audio
  if (!MIX_SetTrackAudio(track, it->second)) {
    SOUND_ERROR("Failed to set track audio for music: " + musicID);
    MIX_UntagTrack(track, "music");
    MIX_DestroyTrack(track);
    return;
  }

  // Set track volume (combine with global music volume)
  float finalVolume = std::clamp(volume * m_musicVolume, 0.0f, 10.0f);
  if (!MIX_SetTrackGain(track, finalVolume)) {
    SOUND_WARN("Failed to set track volume for music: " + musicID);
  }

  // Configure playback properties
  SDL_PropertiesID props = SDL_CreateProperties();
  if (loops != 0) {
    SDL_SetNumberProperty(props, MIX_PROP_PLAY_LOOPS_NUMBER, loops);
  }

  // Start playback
  if (!MIX_PlayTrack(track, props)) {
    SOUND_ERROR("Failed to play music: " + musicID);
    MIX_UntagTrack(track, "music");
    MIX_DestroyTrack(track);
    SDL_DestroyProperties(props);
    return;
  }

  SDL_DestroyProperties(props);

  // Track the active music
  m_activeMusicTracks.push_back(track);
  m_trackToAudioMap[track] = fullMusicID;

  SOUND_INFO("Started playing music: " + musicID);
}

void SoundManager::pauseMusic() {
  if (!m_initialized) {
    return;
  }

  cleanupStoppedTracks();

  // Pause all music tracks
  for (MIX_Track *track : m_activeMusicTracks) {
    if (MIX_TrackPlaying(track) && !MIX_TrackPaused(track)) {
      MIX_PauseTrack(track);
    }
  }
}

void SoundManager::resumeMusic() {
  if (!m_initialized) {
    return;
  }

  cleanupStoppedTracks();

  // Resume all paused music tracks
  for (MIX_Track *track : m_activeMusicTracks) {
    if (MIX_TrackPaused(track)) {
      MIX_ResumeTrack(track);
    }
  }
}

void SoundManager::stopMusic() {
  if (!m_initialized) {
    return;
  }

  cleanupStoppedTracks();

  // Stop all music tracks
  for (MIX_Track *track : m_activeMusicTracks) {
    if (MIX_TrackPlaying(track)) {
      MIX_StopTrack(track, 0); // Stop immediately
    }
  }

  // Clean up stopped tracks
  cleanupStoppedTracks();
}

bool SoundManager::isMusicPlaying() const {
  if (!m_initialized) {
    return false;
  }

  // Check if any music track is currently playing
  for (MIX_Track *track : m_activeMusicTracks) {
    if (MIX_TrackPlaying(track) && !MIX_TrackPaused(track)) {
      return true;
    }
  }
  return false;
}

void SoundManager::setMusicVolume(float volume) {
  volume = std::clamp(volume, 0.0f, 10.0f);
  m_musicVolume = volume;

  if (m_initialized) {
    // Update volume for all active music tracks
    cleanupStoppedTracks();
    for (MIX_Track *track : m_activeMusicTracks) {
      MIX_SetTrackGain(track, volume);
    }
  }
}

void SoundManager::setSFXVolume(float volume) {
  volume = std::clamp(volume, 0.0f, 10.0f);
  m_sfxVolume = volume;

  if (m_initialized) {
    // Update volume for all active SFX tracks
    cleanupStoppedTracks();
    for (auto &pair : m_activeSfxTracks) {
      for (MIX_Track *track : pair.second) {
        MIX_SetTrackGain(track, volume);
      }
    }
  }
}

void SoundManager::clean() {
  if (m_isShutdown)
    return;

  // Stop and destroy all active tracks
  for (auto &pair : m_activeSfxTracks) {
    for (MIX_Track *track : pair.second) {
      if (MIX_TrackPlaying(track)) {
        MIX_StopTrack(track, 0);
      }
      MIX_UntagTrack(track, "sfx");
      MIX_DestroyTrack(track);
    }
  }
  m_activeSfxTracks.clear();

  for (MIX_Track *track : m_activeMusicTracks) {
    if (MIX_TrackPlaying(track)) {
      MIX_StopTrack(track, 0);
    }
    MIX_UntagTrack(track, "music");
    MIX_DestroyTrack(track);
  }
  m_activeMusicTracks.clear();
  m_trackToAudioMap.clear();

  // Free all audio
  for (auto &pair : m_audioMap) {
    if (pair.second) {
      MIX_DestroyAudio(pair.second);
    }
  }
  m_audioMap.clear();

  // Destroy groups
  if (m_sfxGroup) {
    MIX_DestroyGroup(m_sfxGroup);
    m_sfxGroup = nullptr;
  }
  if (m_musicGroup) {
    MIX_DestroyGroup(m_musicGroup);
    m_musicGroup = nullptr;
  }

  // Destroy mixer
  if (m_mixer) {
    MIX_DestroyMixer(m_mixer);
    m_mixer = nullptr;
  }

  // Quit SDL3_mixer library
  if (m_initialized) {
    MIX_Quit();
  }

  m_initialized = false;
  m_isShutdown = true;
  SOUND_INFO("SoundManager cleaned up");
}

void SoundManager::clearSFX(const std::string &soundID) {
  // Stop any playing instances of this SFX
  auto trackIt = m_activeSfxTracks.find(soundID);
  if (trackIt != m_activeSfxTracks.end()) {
    for (MIX_Track *track : trackIt->second) {
      if (MIX_TrackPlaying(track)) {
        MIX_StopTrack(track, 0);
      }
      m_trackToAudioMap.erase(track);
      MIX_UntagTrack(track, "sfx");
      MIX_DestroyTrack(track);
    }
    m_activeSfxTracks.erase(trackIt);
  }

  // Remove the audio from memory
  auto audioIt = m_audioMap.find(soundID);
  if (audioIt != m_audioMap.end()) {
    MIX_DestroyAudio(audioIt->second);
    m_audioMap.erase(audioIt);
    SOUND_INFO("Cleared sound effect: " + soundID);
  }
}

void SoundManager::clearMusic(const std::string &musicID) {
  const std::string fullMusicID = "music_" + musicID;

  // Stop any playing instances of this music
  auto newEnd = std::remove_if(
      m_activeMusicTracks.begin(), m_activeMusicTracks.end(),
      [this, &fullMusicID](MIX_Track *track) {
        auto it = m_trackToAudioMap.find(track);
        if (it != m_trackToAudioMap.end() && it->second == fullMusicID) {
          if (MIX_TrackPlaying(track)) {
            MIX_StopTrack(track, 0);
          }
          MIX_UntagTrack(track, "music");
          MIX_DestroyTrack(track);
          m_trackToAudioMap.erase(it);
          return true;
        }
        return false;
      });
  m_activeMusicTracks.erase(newEnd, m_activeMusicTracks.end());

  // Remove the audio from memory
  auto audioIt = m_audioMap.find(fullMusicID);
  if (audioIt != m_audioMap.end()) {
    MIX_DestroyAudio(audioIt->second);
    m_audioMap.erase(audioIt);
    SOUND_INFO("Cleared music track: " + musicID);
  }
}

bool SoundManager::isSFXLoaded(const std::string &soundID) const {
  return m_audioMap.find(soundID) != m_audioMap.end();
}

bool SoundManager::isMusicLoaded(const std::string &musicID) const {
  return m_audioMap.find("music_" + musicID) != m_audioMap.end();
}