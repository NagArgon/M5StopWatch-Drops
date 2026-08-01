/*
 * SPDX-FileCopyrightText: 2026 NagArgon
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_up_indicator.h"
#include <hal/hal.h>
#include <mooncake.h>
#include <mooncake_log.h>
#include <assets/assets.h>
#include <smooth_lvgl.hpp>
#include <cmath>

static constexpr float _pi = 3.14159265358979323846f;
// 加速度ベクトルの平滑化係数 (1フレームあたり)
static constexpr float _filter_alpha = 0.25f;
// 端末がほぼ水平 (画面が天井/床向き) とみなすヒステリシス閾値 [g]
static constexpr float _flat_enter_threshold = 0.18f;
static constexpr float _flat_exit_threshold  = 0.25f;

AppUpIndicator::AppUpIndicator()
{
    setAppInfo().name = "UP";
    setAppInfo().icon = (void*)&icon_imu;
}

void AppUpIndicator::onCreate()
{
    mclog::tagInfo(getAppInfo().name, "on create");
}

void AppUpIndicator::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");

    _key_manager = std::make_unique<input::KeyManager>();

    _filtered_x = 0.0f;
    _filtered_y = 0.0f;
    _is_flat    = true;

    LvglLockGuard lock;

    _view = std::make_unique<view::UpIndicatorView>();
    _view->init(lv_screen_active());
}

void AppUpIndicator::onRunning()
{
    if (_key_manager->update() == input::KeyEvent::GoHome) {
        close();
        return;
    }

    LvglLockGuard lock;

    if (!_view) {
        return;
    }

    GetHAL().updateImuData();
    const auto& imu_data = GetHAL().getImuData();

    // 画面座標系 (+x: 右, +y: 下) に射影された重力ベクトルを平滑化
    _filtered_x += _filter_alpha * (imu_data.accelX - _filtered_x);
    _filtered_y += _filter_alpha * (imu_data.accelY - _filtered_y);

    float magnitude = std::sqrt(_filtered_x * _filtered_x + _filtered_y * _filtered_y);

    // ほぼ水平のときは「上」が定まらないのでヒステリシス付きで判定
    if (_is_flat) {
        if (magnitude > _flat_exit_threshold) {
            _is_flat = false;
        }
    } else {
        if (magnitude < _flat_enter_threshold) {
            _is_flat = true;
        }
    }

    if (!_is_flat && magnitude > 1e-6f) {
        // 「上」= 重力の逆方向 (画面座標系)
        // 実機ではセンサー値の符号が画面座標と一致していたため、そのまま使う
        float up_x = _filtered_x / magnitude;
        float up_y = _filtered_y / magnitude;

        // 画面の真上 (0, -1) から時計回りの角度 [deg]
        float angle_deg = std::atan2(up_x, -up_y) * 180.0f / _pi;
        if (angle_deg < 0.0f) {
            angle_deg += 360.0f;
        }

        _view->setDirection(up_x, up_y);
        _view->setAngleDeg(angle_deg);
        _view->setFlat(false);
    } else {
        _view->setFlat(true);
    }

    _view->update();
}

void AppUpIndicator::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");

    _key_manager.reset();

    LvglLockGuard lock;

    _view.reset();
}
