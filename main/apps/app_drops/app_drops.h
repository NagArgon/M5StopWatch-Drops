/*
 * SPDX-FileCopyrightText: 2026 NagArgon
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <apps/common/key_manager/key_manager.h>
#include <mooncake.h>
#include <memory>
#include "physics.h"
#include "view/view.h"

/**
 * @brief Badge画像をドロップ (玉) にして、IMUの傾きで転がす物理演算App
 */
class AppDrops : public mooncake::AppAbility {
public:
    AppDrops();

    void onCreate() override;
    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    std::unique_ptr<input::KeyManager> _key_manager;
    std::unique_ptr<view::DropsView> _view;
    std::unique_ptr<drops::World> _world;

    uint32_t _last_tick     = 0;
    uint32_t _last_imu_read = 0;
    float _filtered_ax  = 0.0f;
    float _filtered_ay  = 0.0f;
    int _sprite_option  = 0;
    int _ball_count     = 0;
};
