/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#define BOOST_TEST_MODULE ManagerRuntimeTests
#include <boost/test/unit_test.hpp>

#define private public
#include "managers/FontManager.hpp"
#include "managers/SoundManager.hpp"
#include "managers/TextureManager.hpp"
#undef private

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

using namespace HammerEngine;

namespace {

namespace fs = std::filesystem;

void writeU16(std::ofstream& stream, uint16_t value)
{
    const std::array<unsigned char, 2> bytes{
        static_cast<unsigned char>(value & 0xffu),
        static_cast<unsigned char>((value >> 8u) & 0xffu),
    };
    stream.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

void writeU32(std::ofstream& stream, uint32_t value)
{
    const std::array<unsigned char, 4> bytes{
        static_cast<unsigned char>(value & 0xffu),
        static_cast<unsigned char>((value >> 8u) & 0xffu),
        static_cast<unsigned char>((value >> 16u) & 0xffu),
        static_cast<unsigned char>((value >> 24u) & 0xffu),
    };
    stream.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

void writeTestWavFile(const fs::path& filePath)
{
    std::ofstream stream(filePath, std::ios::binary);
    BOOST_REQUIRE_MESSAGE(stream.is_open(), "Failed to create test WAV file");

    constexpr uint16_t channels = 1;
    constexpr uint32_t sampleRate = 8000;
    constexpr uint16_t bitsPerSample = 16;
    constexpr uint16_t blockAlign = channels * (bitsPerSample / 8u);
    constexpr uint32_t byteRate = sampleRate * blockAlign;
    constexpr uint16_t sampleCount = 4;
    constexpr uint32_t dataSize = sampleCount * blockAlign;
    constexpr uint32_t riffSize = 36u + dataSize;

    stream.write("RIFF", 4);
    writeU32(stream, riffSize);
    stream.write("WAVE", 4);
    stream.write("fmt ", 4);
    writeU32(stream, 16u);
    writeU16(stream, 1u);
    writeU16(stream, channels);
    writeU32(stream, sampleRate);
    writeU32(stream, byteRate);
    writeU16(stream, blockAlign);
    writeU16(stream, bitsPerSample);
    stream.write("data", 4);
    writeU32(stream, dataSize);

    const int16_t sample = 0;
    for (uint16_t i = 0; i < sampleCount; ++i) {
        stream.write(reinterpret_cast<const char*>(&sample), sizeof(sample));
    }
}

void resetSoundManager(SoundManager& manager)
{
    if (manager.m_initialized) {
        manager.clean();
    }

    manager.m_initialized = false;
    manager.m_sfxLoaded.store(false, std::memory_order_release);
    manager.m_musicLoaded.store(false, std::memory_order_release);
    manager.m_isShutdown.store(false, std::memory_order_release);
    manager.m_musicVolume = 1.0f;
    manager.m_sfxVolume = 1.0f;
    manager.m_mixer = nullptr;
    manager.m_sfxGroup = nullptr;
    manager.m_musicGroup = nullptr;
    manager.m_audioMap.clear();
    manager.m_activeSfxTracks.clear();
    manager.m_activeMusicTracks.clear();
    manager.m_trackToAudioMap.clear();
}

void resetFontManager(FontManager& manager)
{
    manager.destroyGPUTextObjects();
    manager.m_fontMap.clear();
    manager.m_fontsLoaded.store(false, std::memory_order_release);
    manager.m_isShutdown = false;
    manager.m_fontFilePaths.clear();
    manager.m_lastWindowWidth = 0;
    manager.m_lastWindowHeight = 0;
    manager.m_lastFontPath.clear();
}

void resetTextureManager(TextureManager& manager)
{
    if (!manager.m_isShutdown) {
        manager.clean();
    }

    manager.m_isShutdown = false;
    manager.m_gpuTextureMap.clear();
    manager.m_pendingUploads.clear();
}

struct SoundManagerFixture {
    SoundManagerFixture()
    {
#if defined(_WIN32)
        _putenv_s("SDL_AUDIODRIVER", "dummy");
#else
        setenv("SDL_AUDIODRIVER", "dummy", 1);
#endif
        resetSoundManager(SoundManager::Instance());

        m_tempRoot = fs::temp_directory_path() / "hammer_sound_manager_tests";
        std::error_code ec;
        fs::remove_all(m_tempRoot, ec);
        fs::create_directories(m_tempRoot);
        m_sfxDir = m_tempRoot / "sfx";
        fs::create_directories(m_sfxDir);

        m_sfxFile = m_sfxDir / "tone.wav";
        writeTestWavFile(m_sfxFile);

        BOOST_REQUIRE_MESSAGE(SoundManager::Instance().init(),
                              "SoundManager init failed; dummy audio backend may be unavailable");
    }

    ~SoundManagerFixture()
    {
        resetSoundManager(SoundManager::Instance());

        std::error_code ec;
        fs::remove_all(m_tempRoot, ec);
    }

    fs::path m_tempRoot;
    fs::path m_sfxDir;
    fs::path m_sfxFile;
};

struct FontManagerFixture {
    FontManagerFixture()
    {
        resetFontManager(FontManager::Instance());
        BOOST_REQUIRE(FontManager::Instance().init());
    }

    ~FontManagerFixture()
    {
        resetFontManager(FontManager::Instance());
        if (!FontManager::Instance().isShutdown()) {
            FontManager::Instance().clean();
        }
    }
};

struct TextureManagerFixture {
    TextureManagerFixture()
    {
        resetTextureManager(TextureManager::Instance());
    }

    ~TextureManagerFixture()
    {
        resetTextureManager(TextureManager::Instance());
    }
};

} // namespace

BOOST_FIXTURE_TEST_SUITE(SoundManagerRuntimeTests, SoundManagerFixture)

BOOST_AUTO_TEST_CASE(TestInitialStateAndVolumeClamping)
{
    auto& manager = SoundManager::Instance();

    BOOST_CHECK(!manager.isShutdown());
    BOOST_CHECK(!manager.isMusicPlaying());
    BOOST_CHECK(!manager.isSFXLoaded("missing"));
    BOOST_CHECK(!manager.isMusicLoaded("missing"));

    manager.setMusicVolume(-2.0f);
    manager.setSFXVolume(50.0f);

    BOOST_CHECK_EQUAL(manager.getMusicVolume(), 0.0f);
    BOOST_CHECK_EQUAL(manager.getSFXVolume(), 10.0f);
}

BOOST_AUTO_TEST_CASE(TestLoadAndClearSFX)
{
    auto& manager = SoundManager::Instance();

    BOOST_REQUIRE(manager.loadSFX(m_sfxDir.string(), "fx"));
    BOOST_CHECK(manager.isSFXLoaded("fx_tone"));

    manager.clearSFX("fx_tone");
    BOOST_CHECK(!manager.isSFXLoaded("fx_tone"));
}

BOOST_AUTO_TEST_CASE(TestLoadAndClearMusic)
{
    auto& manager = SoundManager::Instance();

    BOOST_REQUIRE(manager.loadMusic(m_sfxFile.string(), "theme"));
    BOOST_CHECK(manager.isMusicLoaded("theme"));

    manager.clearMusic("theme");
    BOOST_CHECK(!manager.isMusicLoaded("theme"));
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_FIXTURE_TEST_SUITE(FontManagerRuntimeTests, FontManagerFixture)

BOOST_AUTO_TEST_CASE(TestLoadFontsForDisplayAndMeasureText)
{
    auto& manager = FontManager::Instance();
    const fs::path fontsDir = fs::path("res") / "fonts";

    BOOST_REQUIRE(manager.loadFontsForDisplay(fontsDir.string(), 1920, 1080, 1.0f));
    BOOST_CHECK(manager.areFontsLoaded());
    BOOST_CHECK_EQUAL(manager.m_fontMap.size(), 8u);
    BOOST_CHECK_EQUAL(manager.m_lastWindowWidth, 1920);
    BOOST_CHECK_EQUAL(manager.m_lastWindowHeight, 1080);
    BOOST_CHECK_EQUAL(manager.m_lastFontPath, fontsDir.string());
    BOOST_CHECK(manager.isFontLoaded("fonts_Arial"));
    BOOST_CHECK(manager.isFontLoaded("fonts_UI_Arial"));

    int width = 0;
    int height = 0;
    BOOST_REQUIRE(manager.measureText("Hello", "fonts_Arial", &width, &height));
    BOOST_CHECK_GT(width, 0);
    BOOST_CHECK_GT(height, 0);

    int lineHeight = 0;
    int ascent = 0;
    int descent = 0;
    BOOST_REQUIRE(manager.getFontMetrics("fonts_Arial", &lineHeight, &ascent, &descent));
    BOOST_CHECK_GT(lineHeight, 0);

    int multilineWidth = 0;
    int multilineHeight = 0;
    BOOST_REQUIRE(manager.measureMultilineText("Hello\nWorld", "fonts_Arial", 0,
                                               &multilineWidth, &multilineHeight));
    BOOST_CHECK_GE(multilineHeight, lineHeight * 2);

    const auto wrapped = manager.wrapTextToLines("hello world", "fonts_Arial", 1);
    BOOST_CHECK_GE(wrapped.size(), 2u);
}

BOOST_AUTO_TEST_CASE(TestReloadFontsForDisplayRefreshesState)
{
    auto& manager = FontManager::Instance();
    const fs::path fontsDir = fs::path("res") / "fonts";

    BOOST_REQUIRE(manager.loadFontsForDisplay(fontsDir.string(), 1280, 720, 1.0f));
    BOOST_REQUIRE(manager.reloadFontsForDisplay(fontsDir.string(), 1600, 900, 1.5f));

    BOOST_CHECK(manager.areFontsLoaded());
    BOOST_CHECK_EQUAL(manager.m_fontMap.size(), 8u);
    BOOST_CHECK_EQUAL(manager.m_lastWindowWidth, 1600);
    BOOST_CHECK_EQUAL(manager.m_lastWindowHeight, 900);
    BOOST_CHECK_EQUAL(manager.m_lastFontPath, fontsDir.string());
}

BOOST_AUTO_TEST_CASE(TestClearFontLeavesOtherCachedEntries)
{
    auto& manager = FontManager::Instance();
    const fs::path fontsDir = fs::path("res") / "fonts";

    BOOST_REQUIRE(manager.loadFontsForDisplay(fontsDir.string(), 1920, 1080, 1.0f));
    manager.clearFont("fonts_Arial");

    BOOST_CHECK(!manager.isFontLoaded("fonts_Arial"));
    BOOST_CHECK(manager.isFontLoaded("fonts_UI_Arial"));
    BOOST_CHECK_EQUAL(manager.m_fontMap.size(), 7u);
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_FIXTURE_TEST_SUITE(TextureManagerRuntimeTests, TextureManagerFixture)

BOOST_AUTO_TEST_CASE(TestLoadGPUTextureFailsWithoutInitializedDevice)
{
    auto& manager = TextureManager::Instance();
    const fs::path atlasPath = fs::path("res") / "img" / "atlas.png";

    BOOST_CHECK(!manager.loadGPU(atlasPath.string(), "atlas"));
    BOOST_CHECK(!manager.hasGPUTextures());
    BOOST_CHECK(!manager.hasPendingUploads());
    BOOST_CHECK(!manager.getGPUTextureData("atlas").has_value());
}

BOOST_AUTO_TEST_CASE(TestCleanIsSafeAfterFailedLoad)
{
    auto& manager = TextureManager::Instance();
    const fs::path atlasPath = fs::path("res") / "img" / "atlas.png";

    BOOST_CHECK(!manager.loadGPU(atlasPath.string(), "atlas"));
    manager.clean();

    BOOST_CHECK(manager.isShutdown());
    BOOST_CHECK(!manager.hasGPUTextures());
    BOOST_CHECK(!manager.hasPendingUploads());
}

BOOST_AUTO_TEST_SUITE_END()
