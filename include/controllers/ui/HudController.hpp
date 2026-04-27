/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#ifndef HUD_CONTROLLER_HPP
#define HUD_CONTROLLER_HPP

/**
 * @file HudController.hpp
 * @brief State-scoped event bridge for transient gameplay HUD state
 *
 * HudController listens to committed gameplay events and exposes
 * read-only state for HUD rendering. It does not mutate UI components
 * directly; active states query this controller when updating the HUD.
 */

#include "controllers/ControllerBase.hpp"
#include "controllers/IUpdatable.hpp"
#include "entities/EntityHandle.hpp"
#include <string>

class HudController : public ControllerBase, public IUpdatable
{
public:
    static constexpr float TARGET_DISPLAY_DURATION{3.0f};

    explicit HudController(EntityHandle playerHandle)
        : m_playerHandle(playerHandle)
    {
    }

    ~HudController() override = default;

    HudController(HudController&&) noexcept = default;
    HudController& operator=(HudController&&) noexcept = default;

    void subscribe() override;
    [[nodiscard]] std::string_view getName() const override { return "HudController"; }
    void update(float deltaTime) override;

    [[nodiscard]] bool hasActiveTarget() const;
    [[nodiscard]] float getTargetHealth() const;
    [[nodiscard]] const std::string& getTargetLabel() const { return m_targetLabel; }

    void initializeHotbarUI();
    void setHotbarSelectedIndex(size_t i);
    void setHotbarVisible(bool visible);
    [[nodiscard]] size_t getHotbarSelectedIndex() const { return m_hotbarSelectedIndex; }

    static constexpr size_t HOTBAR_SLOT_COUNT = 9;
    static constexpr const char* HOTBAR_PANEL_ID = "gameplay_hotbar_panel";

private:
    void onCombatEvent(const EventData& data);
    void clearTarget();
    void pollHotbarInput();
    void applyHotbarSelectionStyling();
    static std::string hotbarSlotId(size_t i);
    static std::string hotbarKeyLabelId(size_t i);

    EntityHandle m_playerHandle{};
    EntityHandle m_targetedHandle{};
    EntityHandle m_lastLabeledHandle{};
    float m_targetDisplayTimer{0.0f};
    float m_cachedTargetHealth{0.0f};
    std::string m_targetLabel{"Target"};

    size_t m_hotbarSelectedIndex{0};
    bool m_hotbarUICreated{false};
};

#endif // HUD_CONTROLLER_HPP
