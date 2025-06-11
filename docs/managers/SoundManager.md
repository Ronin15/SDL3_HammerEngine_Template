# SoundManager Documentation

## Overview

The SoundManager provides a centralized audio system for the Forge Game Engine, handling both sound effects and music playback. It supports multiple audio formats with volume control, efficient resource management, and seamless integration with game states.

## Key Features

- **Multi-Format Support**: WAV, MP3, and OGG audio files
- **Sound Effects**: Short audio clips with volume and loop control
- **Music Playback**: Background music with seamless looping and crossfading
- **Volume Management**: Independent volume control for SFX and music
- **Directory Loading**: Batch load audio files from directories
- **Memory Management**: Automatic audio resource cleanup
- **Thread Safety**: Safe audio operations across threads

## Quick Start

### Initialization

```cpp
#include "managers/SoundManager.hpp"

// Initialize the audio system
if (!SoundManager::Instance().init()) {
    std::cerr << "Failed to initialize SoundManager" << std::endl;
    return false;
}
```

### Basic Sound Effects

```cpp
// Load a single sound effect
bool success = SoundManager::Instance().loadSFX(
    "res/sounds/jump.wav",      // Audio file path
    "jump"                      // Sound ID for reference
);

// Play the sound effect
SoundManager::Instance().playSFX("jump");

// Play with custom volume (0.0 to 1.0)
SoundManager::Instance().playSFX("jump", 0.7f);
```

### Background Music

```cpp
// Load and play background music
SoundManager::Instance().loadMusic(
    "res/music/background.ogg", // Music file path
    "bg_music"                  // Music ID
);

// Play music with looping
SoundManager::Instance().playMusic("bg_music", true);

// Set music volume
SoundManager::Instance().setMusicVolume(0.6f);
```

## Sound Effects Management

### Single File Loading

```cpp
// Load individual sound files
SoundManager& soundMgr = SoundManager::Instance();

soundMgr.loadSFX("res/sounds/explosion.wav", "explosion");
soundMgr.loadSFX("res/sounds/pickup.ogg", "pickup");
soundMgr.loadSFX("res/sounds/laser.mp3", "laser");
```

### Directory Loading

```cpp
// Load all audio files from a directory
bool success = soundMgr.loadSFXFromDirectory(
    "res/sounds/effects/",      // Directory path
    "sfx"                       // ID prefix for all sounds
);

// Creates sound IDs like: "sfx_explosion", "sfx_pickup", "sfx_laser", etc.
// File extensions are automatically stripped from IDs
```

### Playing Sound Effects

```cpp
// Basic playback
soundMgr.playSFX("explosion");

// With custom volume
soundMgr.playSFX("pickup", 0.8f);

// With looping (useful for ambient sounds)
soundMgr.playSFX("engine_idle", 0.5f, true);

// Stop specific sound effect
soundMgr.stopSFX("engine_idle");

// Stop all sound effects
soundMgr.stopAllSFX();
```

### Volume Control

```cpp
// Set global SFX volume (0.0 = silent, 1.0 = full volume)
soundMgr.setSFXVolume(0.7f);

// Get current SFX volume
float currentVolume = soundMgr.getSFXVolume();

// Mute/unmute sound effects
soundMgr.muteSFX(true);   // Mute
soundMgr.muteSFX(false);  // Unmute
```

## Music Management

### Loading Music

```cpp
// Load background music tracks
soundMgr.loadMusic("res/music/menu.ogg", "menu_music");
soundMgr.loadMusic("res/music/gameplay.mp3", "game_music");
soundMgr.loadMusic("res/music/battle.wav", "battle_music");
```

### Music Playback

```cpp
// Play music with looping
soundMgr.playMusic("menu_music", true);

// Play music once (no loop)
soundMgr.playMusic("victory_fanfare", false);

// Fade in music over 2 seconds
soundMgr.fadeInMusic("game_music", 2000, true);

// Crossfade between music tracks
soundMgr.crossfadeMusic("menu_music", "game_music", 1500);
```

### Music Control

```cpp
// Pause/resume music
soundMgr.pauseMusic();
soundMgr.resumeMusic();

// Stop music
soundMgr.stopMusic();

// Fade out music over 1 second
soundMgr.fadeOutMusic(1000);

// Check if music is playing
bool isPlaying = soundMgr.isMusicPlaying();
```

### Music Volume

```cpp
// Set music volume (independent of SFX volume)
soundMgr.setMusicVolume(0.4f);

// Get current music volume
float musicVol = soundMgr.getMusicVolume();

// Mute/unmute music
soundMgr.muteMusic(true);
```

## Game Integration

### Game State Integration

```cpp
class MenuState : public GameState {
public:
    bool enter() override {
        auto& sound = SoundManager::Instance();
        
        // Load state-specific audio
        sound.loadMusic("res/music/menu.ogg", "menu_bg");
        sound.loadSFX("res/sounds/ui/", "ui");  // Load all UI sounds
        
        // Start background music
        sound.playMusic("menu_bg", true);
        
        return true;
    }
    
    void handleButtonClick() {
        // Play UI sound effect
        SoundManager::Instance().playSFX("ui_button_click");
    }
    
    bool exit() override {
        auto& sound = SoundManager::Instance();
        
        // Fade out music when leaving state
        sound.fadeOutMusic(500);
        
        // Clean up state-specific sounds
        sound.unloadMusic("menu_bg");
        sound.unloadSFXGroup("ui");
        
        return true;
    }
};
```

### Dynamic Audio Events

```cpp
class GamePlayState : public GameState {
public:
    void onPlayerJump() {
        SoundManager::Instance().playSFX("jump", 0.8f);
    }
    
    void onEnemyDeath() {
        SoundManager::Instance().playSFX("enemy_death", 1.0f);
    }
    
    void onLevelComplete() {
        auto& sound = SoundManager::Instance();
        
        // Fade out background music
        sound.fadeOutMusic(1000);
        
        // Play victory sound
        sound.playSFX("victory", 1.0f);
        
        // Start victory music after delay
        // Note: You might implement a timer or delayed callback system
    }
    
    void onHealthLow() {
        // Play heartbeat sound on loop
        SoundManager::Instance().playSFX("heartbeat", 0.6f, true);
    }
    
    void onHealthRecovered() {
        // Stop heartbeat sound
        SoundManager::Instance().stopSFX("heartbeat");
    }
};
```

### Audio Settings Menu

```cpp
class AudioSettingsMenu {
private:
    float m_sfxVolume = 1.0f;
    float m_musicVolume = 1.0f;
    
public:
    void updateSFXVolume(float volume) {
        m_sfxVolume = volume;
        SoundManager::Instance().setSFXVolume(volume);
        
        // Play test sound to demonstrate volume
        SoundManager::Instance().playSFX("ui_slider", volume);
    }
    
    void updateMusicVolume(float volume) {
        m_musicVolume = volume;
        SoundManager::Instance().setMusicVolume(volume);
    }
    
    void toggleSFXMute() {
        static bool isMuted = false;
        isMuted = !isMuted;
        SoundManager::Instance().muteSFX(isMuted);
    }
    
    void toggleMusicMute() {
        static bool isMuted = false;
        isMuted = !isMuted;
        SoundManager::Instance().muteMusic(isMuted);
    }
};
```

## Audio Resource Management

### Loading Strategies

```cpp
// Load all game audio at startup (small games)
void loadAllAudio() {
    auto& sound = SoundManager::Instance();
    
    sound.loadSFXFromDirectory("res/sounds/ui/", "ui");
    sound.loadSFXFromDirectory("res/sounds/gameplay/", "game");
    sound.loadMusic("res/music/menu.ogg", "menu_music");
    sound.loadMusic("res/music/game.ogg", "game_music");
}

// Load audio per game state (larger games)
void loadStateAudio(const std::string& stateName) {
    auto& sound = SoundManager::Instance();
    
    std::string soundPath = "res/sounds/" + stateName + "/";
    std::string musicPath = "res/music/" + stateName + ".ogg";
    
    sound.loadSFXFromDirectory(soundPath, stateName);
    sound.loadMusic(musicPath, stateName + "_music");
}
```

### Cleanup and Unloading

```cpp
// Check if audio is loaded
bool hasSFX = soundMgr.hasSFX("jump");
bool hasMusic = soundMgr.hasMusic("bg_music");

// Unload specific audio
soundMgr.unloadSFX("unused_sound");
soundMgr.unloadMusic("old_music");

// Unload groups of sounds
soundMgr.unloadSFXGroup("ui");  // Unloads all sounds with "ui" prefix

// Clean up all audio (called automatically on shutdown)
soundMgr.clean();
```

## Performance Considerations

### Efficient Audio Loading

```cpp
// ✅ GOOD: Load audio during state transitions or loading screens
void LoadingState::loadAudio() {
    SoundManager::Instance().loadSFXFromDirectory("res/sounds/", "game");
}

// ✅ GOOD: Use appropriate file formats
// - WAV for short sound effects (uncompressed, fast loading)
// - OGG for music and longer sounds (compressed, smaller file size)
// - MP3 for compatibility (more widely supported)

// ❌ BAD: Don't load audio during gameplay
void GameplayState::update() {
    SoundManager::Instance().loadSFX("sound.wav", "new_sound");  // Causes frame drops
}
```

### Memory Management

```cpp
// ✅ GOOD: Unload unused audio between states
void GameState::exit() {
    SoundManager::Instance().unloadSFXGroup("state_specific");
}

// ✅ GOOD: Use volume control instead of stopping/starting music frequently
SoundManager::Instance().setMusicVolume(0.0f);  // Effectively mute
// Rather than:
SoundManager::Instance().stopMusic();
```

## Audio Quality Settings

### Format Recommendations

- **Sound Effects**: WAV format for best quality and fast loading
- **Background Music**: OGG format for good quality with compression
- **Voice/Dialog**: MP3 or OGG depending on length and quality needs

### Volume Guidelines

```cpp
// Recommended volume ranges
const float UI_SFX_VOLUME = 0.7f;      // UI sounds should be clear but not overpowering
const float GAMEPLAY_SFX_VOLUME = 0.8f; // Gameplay sounds can be more prominent
const float AMBIENT_SFX_VOLUME = 0.4f;  // Ambient sounds should be subtle
const float MUSIC_VOLUME = 0.5f;        // Music should complement, not overpower
```

## Error Handling

### Safe Audio Operations

```cpp
// Always check if audio loading succeeded
if (!soundMgr.loadSFX("important.wav", "important_sound")) {
    std::cerr << "Failed to load critical sound effect" << std::endl;
    // Handle gracefully - game should continue without audio
}

// Verify audio exists before playing
if (soundMgr.hasSFX("jump")) {
    soundMgr.playSFX("jump");
} else {
    // Fallback or skip audio
}

// Handle audio system initialization failure
if (!SoundManager::Instance().init()) {
    std::cerr << "Audio system unavailable - running in silent mode" << std::endl;
    // Game continues without audio
}
```

## API Reference

### Core Methods

```cpp
// Initialization and cleanup
bool init();
void clean();

// Sound effects
bool loadSFX(const std::string& filePath, const std::string& soundID);
bool loadSFXFromDirectory(const std::string& directory, const std::string& prefix);
void playSFX(const std::string& soundID, float volume = 1.0f, bool loop = false);
void stopSFX(const std::string& soundID);
void stopAllSFX();
bool hasSFX(const std::string& soundID) const;
void unloadSFX(const std::string& soundID);
void unloadSFXGroup(const std::string& prefix);

// Music
bool loadMusic(const std::string& filePath, const std::string& musicID);
void playMusic(const std::string& musicID, bool loop = true);
void fadeInMusic(const std::string& musicID, int fadeTime, bool loop = true);
void crossfadeMusic(const std::string& fromID, const std::string& toID, int fadeTime);
void pauseMusic();
void resumeMusic();
void stopMusic();
void fadeOutMusic(int fadeTime);
bool isMusicPlaying() const;
bool hasMusic(const std::string& musicID) const;
void unloadMusic(const std::string& musicID);

// Volume control
void setSFXVolume(float volume);        // 0.0 to 1.0
float getSFXVolume() const;
void setMusicVolume(float volume);      // 0.0 to 1.0
float getMusicVolume() const;
void muteSFX(bool mute);
void muteMusic(bool mute);
```

## Best Practices

### Audio Loading

```cpp
// ✅ GOOD: Organize audio by category
soundMgr.loadSFXFromDirectory("res/sounds/ui/", "ui");
soundMgr.loadSFXFromDirectory("res/sounds/weapons/", "weapon");
soundMgr.loadSFXFromDirectory("res/sounds/ambient/", "ambient");

// ✅ GOOD: Use descriptive sound IDs
soundMgr.loadSFX("res/sounds/sword_clash.wav", "sword_clash");
soundMgr.loadSFX("res/sounds/door_open.wav", "door_open");

// ✅ GOOD: Load audio at appropriate times
void GameState::enter() {
    loadStateSpecificAudio();
}
```

### Volume Management

```cpp
// ✅ GOOD: Provide player control over audio
void AudioSettings::applySoundSettings() {
    soundMgr.setSFXVolume(m_sfxVolume);
    soundMgr.setMusicVolume(m_musicVolume);
}

// ✅ GOOD: Use context-appropriate volumes
soundMgr.playSFX("footstep", 0.3f);     // Subtle
soundMgr.playSFX("explosion", 0.9f);    // Prominent
soundMgr.playSFX("ui_click", 0.6f);     // Clear but not intrusive
```

### Resource Management

```cpp
// ✅ GOOD: Clean up unused audio
void GameState::exit() {
    soundMgr.unloadSFXGroup("state_" + getStateName());
    soundMgr.fadeOutMusic(500);
}

// ✅ GOOD: Check audio availability
if (soundMgr.hasSFX("special_effect")) {
    soundMgr.playSFX("special_effect");
}
```

## Integration with Other Systems

The SoundManager works seamlessly with:
- **[EventManager](../events/EventManager.md)**: Audio can be triggered by game events through EventManager
- **[GameEngine](../core/GameEngine.md)**: Integrated into main game loop
- **GameStates**: State-specific audio loading and management

The SoundManager provides essential audio capabilities that enhance the game experience while maintaining efficient resource usage and easy integration with game logic.