/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#ifndef GAMEPLAY_HUD_CONTROLLER_HPP
#define GAMEPLAY_HUD_CONTROLLER_HPP

/**
 * @file GameplayHUDController.hpp
 * @brief State-scoped event bridge for transient gameplay HUD state
 *
 * GameplayHUDController listens to committed gameplay events and exposes
 * read-only state for HUD rendering. It does not mutate UI components
 * directly; active states query this controller when updating the HUD.
 */

#include "controllers/ControllerBase.hpp"
#include "controllers/IUpdatable.hpp"
#include "entities/EntityHandle.hpp"
#include <string>

class GameplayHUDController : public ControllerBase, public IUpdatable
{
public:
    static constexpr float TARGET_DISPLAY_DURATION{3.0f};

    explicit GameplayHUDController(EntityHandle playerHandle)
        : m_playerHandle(playerHandle)
    {
    }

    ~GameplayHUDController() override = default;

    GameplayHUDController(GameplayHUDController&&) noexcept = default;
    GameplayHUDController& operator=(GameplayHUDController&&) noexcept = default;

    void subscribe() override;
    [[nodiscard]] std::string_view getName() const override { return "GameplayHUDController"; }
    void update(float deltaTime) override;

    [[nodiscard]] bool hasActiveTarget() const;
    [[nodiscard]] float getTargetHealth() const;
    [[nodiscard]] const std::string& getTargetLabel() const { return m_targetLabel; }

private:
    void onCombatEvent(const EventData& data);
    void clearTarget();

    EntityHandle m_playerHandle{};
    EntityHandle m_targetedHandle{};
    EntityHandle m_lastLabeledHandle{};
    float m_targetDisplayTimer{0.0f};
    float m_cachedTargetHealth{0.0f};
    float m_cachedTargetMaxHealth{100.0f};
    std::string m_targetLabel{"Target"};
};

#endif // GAMEPLAY_HUD_CONTROLLER_HPP
