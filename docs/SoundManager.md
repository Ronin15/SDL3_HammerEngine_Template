# SoundManager Documentation

## Overview

The SoundManager provides a centralized audio system for the Forge Game Engine, handling both sound effects and music playback. It supports multiple audio formats with directory-based batch loading, volume control, and efficient audio resource management with automatic cleanup.

## Key Features

- **Multi-Format Support**: WAV, MP3, and OGG audio files
- **Directory Loading**: Batch load all audio files from a directory
- **Sound Effects**: Short audio clips with volume and loop control
- **Music Playback**: Background music with seamless looping
- **Volume Management**: Independent volume control for SFX and music
- **Memory Management**: Automatic audio resource cleanup
- **Thread Safety**: Safe audio operations across threads

## Initialization

```cpp
// Initialize the audio system
if (!SoundManager::Instance().init()) {
    std::cerr << "Failed to initialize SoundManager" << std::endl;
    return false;
}
```

## Loading Audio

### Sound Effects Loading

#### Single Sound Effect

```cpp
// Load a single sound effect file
bool success = SoundManager::Instance().loadSFX(
    "assets/sounds/jump.wav",      // Audio file path
    "jump"                         // Sound ID for reference
);
```

#### Directory Loading

```cpp
// Load all audio files from a directory
bool success = SoundManager::Instance().loadSFX(
    "assets/sounds/effects/",      // Directory path
    "sfx"                          // ID prefix for all sounds
);

// Creates sound IDs like: "sfx_explosion", "sfx_pickup", etc.
```

### Music Loading

```cpp
// Load background music
bool success = SoundManager::Instance().loadMusic(
    "assets/music/background.ogg", // Music file path
    "bg_music"                     // Music ID for reference
);
```

## Audio Playback

### Sound Effects

```cpp
// Play a sound effect once
SoundManager::Instance().playSFX("jump");

// Play with custom volume (0-100)
SoundManager::Instance().playSFX("explosion", 0, 80);

// Play with looping (-1 = infinite, 0 = once, 1+ = specific count)
SoundManager::Instance().playSFX("engine_loop", 5, 60); // Loop 5 times
```

### Music Control

```cpp
// Play background music (loops by default)
SoundManager::Instance().playMusic("bg_music");

// Play music with custom volume
SoundManager::Instance().playMusic("bg_music", -1, 70);

// Pause current music
SoundManager::Instance().pauseMusic();

// Resume paused music
SoundManager::Instance().resumeMusic();

// Stop music completely
SoundManager::Instance().stopMusic();
```

## Volume Management

### Global Volume Control

```cpp
// Set master volume (0-100)
SoundManager::Instance().setMasterVolume(80);

// Set SFX volume (0-100)
SoundManager::Instance().setSFXVolume(90);

// Set music volume (0-100)
SoundManager::Instance().setMusicVolume(60);

// Get current volumes
int masterVol = SoundManager::Instance().getMasterVolume();
int sfxVol = SoundManager::Instance().getSFXVolume();
int musicVol = SoundManager::Instance().getMusicVolume();
```

### Muting

```cpp
// Mute/unmute all audio
SoundManager::Instance().setMuted(true);
bool isMuted = SoundManager::Instance().isMuted();

// Mute/unmute just music
SoundManager::Instance().setMusicMuted(true);
bool musicMuted = SoundManager::Instance().isMusicMuted();
```

## Audio Resource Management

### Check Audio Availability

```cpp
// Check if sound effect is loaded
if (SoundManager::Instance().hasSFX("jump")) {
    // Sound is available for playback
}

// Check if music is loaded
if (SoundManager::Instance().hasMusic("bg_music")) {
    // Music is available for playback
}
```

### Resource Cleanup

```cpp
// Remove specific sound effect
SoundManager::Instance().clearSFX("old_sound");

// Remove specific music
SoundManager::Instance().clearMusic("old_music");

// Clear all audio resources
SoundManager::Instance().clearAll();
```

## Usage Examples

### Game Audio Controller

```cpp
class GameAudio {
private:
    SDL_Renderer* m_renderer;
    bool m_soundEnabled = true;
    
public:
    void initializeAudio() {
        if (!SoundManager::Instance().init()) {
            std::cerr << "Audio initialization failed" << std::endl;
            return;
        }
        
        // Load game audio
        loadSoundEffects();
        loadMusic();
        
        // Set initial volumes
        SoundManager::Instance().setMasterVolume(80);
        SoundManager::Instance().setSFXVolume(90);
        SoundManager::Instance().setMusicVolume(60);
    }
    
    void loadSoundEffects() {
        SoundManager& audio = SoundManager::Instance();
        
        // Load UI sounds
        audio.loadSFX("assets/audio/ui/", "ui");
        
        // Load gameplay sounds
        audio.loadSFX("assets/audio/player/jump.wav", "player_jump");
        audio.loadSFX("assets/audio/player/land.wav", "player_land");
        audio.loadSFX("assets/audio/weapons/", "weapon");
        audio.loadSFX("assets/audio/enemies/", "enemy");
    }
    
    void loadMusic() {
        SoundManager& audio = SoundManager::Instance();
        
        audio.loadMusic("assets/music/menu.ogg", "menu_music");
        audio.loadMusic("assets/music/level1.ogg", "level1_music");
        audio.loadMusic("assets/music/boss.ogg", "boss_music");
    }
    
    void playPlayerJump() {
        if (m_soundEnabled) {
            SoundManager::Instance().playSFX("player_jump", 0, 85);
        }
    }
    
    void startLevelMusic(int level) {
        std::string musicID = "level" + std::to_string(level) + "_music";
        SoundManager::Instance().playMusic(musicID, -1, 70);
    }
};
```

### Dynamic Audio System

```cpp
class DynamicAudioSystem {
private:
    std::string m_currentMusic;
    float m_musicVolume = 70.0f;
    
public:
    void updateAudio(GameState state, float deltaTime) {
        // Fade music based on game state
        switch (state) {
            case GameState::MENU:
                fadeToMusic("menu_music", deltaTime);
                break;
            case GameState::GAMEPLAY:
                fadeToMusic("level_music", deltaTime);
                break;
            case GameState::BOSS_FIGHT:
                fadeToMusic("boss_music", deltaTime);
                break;
        }
    }
    
private:
    void fadeToMusic(const std::string& newMusic, float deltaTime) {
        if (m_currentMusic != newMusic) {
            // Fade out current music
            m_musicVolume -= 100.0f * deltaTime; // 1 second fade
            
            if (m_musicVolume <= 0.0f) {
                // Switch to new music
                SoundManager::Instance().stopMusic();
                SoundManager::Instance().playMusic(newMusic, -1, 0);
                m_currentMusic = newMusic;
                m_musicVolume = 0.0f;
            } else {
                SoundManager::Instance().setMusicVolume(static_cast<int>(m_musicVolume));
            }
        } else if (m_musicVolume < 70.0f) {
            // Fade in new music
            m_musicVolume += 100.0f * deltaTime;
            if (m_musicVolume > 70.0f) m_musicVolume = 70.0f;
            SoundManager::Instance().setMusicVolume(static_cast<int>(m_musicVolume));
        }
    }
};
```

### UI Sound Effects

```cpp
class UIAudioManager {
public:
    void initializeUI() {
        SoundManager& audio = SoundManager::Instance();
        
        // Load UI sounds
        audio.loadSFX("assets/ui/sounds/click.wav", "ui_click");
        audio.loadSFX("assets/ui/sounds/hover.wav", "ui_hover");
        audio.loadSFX("assets/ui/sounds/error.wav", "ui_error");
        audio.loadSFX("assets/ui/sounds/success.wav", "ui_success");
    }
    
    void onButtonClick() {
        SoundManager::Instance().playSFX("ui_click", 0, 75);
    }
    
    void onButtonHover() {
        SoundManager::Instance().playSFX("ui_hover", 0, 50);
    }
    
    void onError() {
        SoundManager::Instance().playSFX("ui_error", 0, 80);
    }
    
    void onSuccess() {
        SoundManager::Instance().playSFX("ui_success", 0, 85);
    }
};
```

### Ambient Audio System

```cpp
class AmbientAudioSystem {
private:
    std::vector<std::string> m_ambientSounds;
    int m_currentAmbient = 0;
    uint32_t m_ambientTimer = 0;
    uint32_t m_ambientDelay = 5000; // 5 seconds
    
public:
    void loadAmbientSounds() {
        SoundManager& audio = SoundManager::Instance();
        
        // Load ambient sounds
        audio.loadSFX("assets/ambient/birds.ogg", "ambient_birds");
        audio.loadSFX("assets/ambient/wind.ogg", "ambient_wind");
        audio.loadSFX("assets/ambient/water.ogg", "ambient_water");
        
        m_ambientSounds = {"ambient_birds", "ambient_wind", "ambient_water"};
    }
    
    void update(uint32_t deltaTime) {
        m_ambientTimer += deltaTime;
        
        if (m_ambientTimer >= m_ambientDelay) {
            playRandomAmbient();
            m_ambientTimer = 0;
            m_ambientDelay = 3000 + (rand() % 7000); // 3-10 seconds
        }
    }
    
private:
    void playRandomAmbient() {
        if (!m_ambientSounds.empty()) {
            int index = rand() % m_ambientSounds.size();
            SoundManager::Instance().playSFX(m_ambientSounds[index], 0, 30);
        }
    }
};
```

## Best Practices

### Audio Loading Strategy

```cpp
void loadGameAudio() {
    SoundManager& audio = SoundManager::Instance();
    
    // Load audio at startup for best performance
    audio.loadSFX("assets/audio/ui/", "ui");
    audio.loadSFX("assets/audio/gameplay/", "game");
    audio.loadSFX("assets/audio/effects/", "fx");
    
    // Load music files
    audio.loadMusic("assets/music/menu.ogg", "menu");
    audio.loadMusic("assets/music/gameplay.ogg", "game");
}
```

### Volume Management

```cpp
class AudioSettings {
public:
    void applySettings(float masterVol, float sfxVol, float musicVol) {
        SoundManager& audio = SoundManager::Instance();
        
        audio.setMasterVolume(static_cast<int>(masterVol * 100));
        audio.setSFXVolume(static_cast<int>(sfxVol * 100));
        audio.setMusicVolume(static_cast<int>(musicVol * 100));
    }
    
    void saveSettings() {
        SoundManager& audio = SoundManager::Instance();
        
        // Save to config file
        m_config["audio"]["master"] = audio.getMasterVolume();
        m_config["audio"]["sfx"] = audio.getSFXVolume();
        m_config["audio"]["music"] = audio.getMusicVolume();
    }
};
```

### Error Handling

```cpp
bool initializeAudioSystem() {
    if (!SoundManager::Instance().init()) {
        std::cerr << "SoundManager initialization failed" << std::endl;
        return false;
    }
    
    if (!SoundManager::Instance().loadSFX("assets/audio/required.wav", "required")) {
        std::cerr << "Failed to load required audio" << std::endl;
        return false;
    }
    
    return true;
}
```

## Performance Considerations

- **Pre-load audio**: Load all audio files at startup rather than during gameplay
- **Audio format**: Use OGG for music (smaller file size) and WAV for short SFX
- **Memory usage**: Monitor audio memory usage, especially for long music tracks
- **Streaming**: Consider streaming for very long audio files
- **Compression**: Use compressed formats for storage efficiency

## File Format Support

- **WAV**: Uncompressed audio (best for short sound effects)
- **MP3**: Compressed audio (good compatibility)
- **OGG**: Open-source compressed audio (recommended for music)

## Thread Safety

SoundManager provides thread-safe access for audio operations:

```cpp
// Safe to call from multiple threads
std::thread audioThread([&]() {
    SoundManager::Instance().playSFX("explosion", 0, 80);
});
```

## Integration with Game States

```cpp
class GameState {
public:
    virtual bool enter() {
        // Load state-specific audio
        return SoundManager::Instance().loadMusic("assets/state_music.ogg", "state_music");
    }
    
    virtual bool exit() {
        // Clean up state-specific audio if needed
        SoundManager::Instance().clearMusic("state_music");
        return true;
    }
    
    virtual void update() {
        // Play state music if not already playing
        if (!SoundManager::Instance().isMusicPlaying()) {
            SoundManager::Instance().playMusic("state_music");
        }
    }
};
```

## See Also

- `include/managers/SoundManager.hpp` - Complete API reference
- `docs/FontManager.md` - Font management documentation
- `docs/TextureManager.md` - Texture management documentation
- `docs/README.md` - General manager system overview