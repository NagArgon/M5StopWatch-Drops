/*
 * SPDX-FileCopyrightText: 2026 NagArgon
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <apps/common/key_manager/key_manager.h>
#include <mooncake.h>
#include <memory>
#include "view/view.h"

/**
 * @brief 重力方向を検知して、画面上で常に「上」を指し続ける矢印を表示するApp
 */
class AppUpIndicator : public mooncake::AppAbility {
public:
    AppUpIndicator();

    void onCreate() override;
    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    std::unique_ptr<input::KeyManager> _key_manager;
    std::unique_ptr<view::UpIndicatorView> _view;

    // 画面平面に射影した重力ベクトルのローパスフィルタ値 [g]
    float _filtered_x = 0.0f;
    float _filtered_y = 0.0f;
    bool _is_flat     = true;
};
