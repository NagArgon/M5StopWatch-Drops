/*
 * SPDX-FileCopyrightText: 2026 NagArgon
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_drops.h"
#include <hal/hal.h>
#include <mooncake.h>
#include <mooncake_log.h>
#include <assets/assets.h>
#include <smooth_lvgl.hpp>
#include <cmath>

namespace {

constexpr float _pi = 3.14159265358979323846f;

constexpr int _ball_count_min       = 1;
constexpr int _ball_count_min_color = 6;  // 色玉モードは6色セットなので6個から
constexpr int _ball_count_max       = 20;
constexpr int _ball_count_default   = 8;
constexpr float _arena_cx         = 233.0f;
constexpr float _arena_cy         = 233.0f;
constexpr float _arena_r          = 220.0f;
// アリーナ面積のこの割合をドロップで埋めるようにサイズを決める
constexpr float _fill_ratio = 0.5f;

// 1g あたりの重力加速度 [px/s^2]
constexpr float _gravity_px = 2800.0f;
// 加速度ベクトルの平滑化係数
constexpr float _filter_alpha = 0.3f;

int diameter_for_count(int count)
{
    // 6個未満に減らしてもサイズは6個時より大きくしない
    if (count < _ball_count_min_color) {
        count = _ball_count_min_color;
    }
    float radius = _arena_r * std::sqrt(_fill_ratio / count);
    int d        = static_cast<int>(radius * 2.0f);
    if (d > view::DropsView::sprite_base_diameter) {
        d = view::DropsView::sprite_base_diameter;
    }
    return d & ~1;  // 偶数に丸める
}

}  // namespace

AppDrops::AppDrops()
{
    setAppInfo().name = "DROPS";
    setAppInfo().icon = (void*)&icon_badge;
    _ball_count       = _ball_count_default;
}

void AppDrops::onCreate()
{
    mclog::tagInfo(getAppInfo().name, "on create");
}

void AppDrops::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");

    _key_manager = std::make_unique<input::KeyManager>();
    _filtered_ax = 0.0f;
    _filtered_ay = 0.0f;
    _last_tick   = GetHAL().millis();

    const int d        = diameter_for_count(_ball_count);
    const float radius = d / 2.0f;

    _world = std::make_unique<drops::World>();
    _world->setup(_arena_cx, _arena_cy, _arena_r);

    // 中央1個 + リングで初期配置 (多少の重なりは物理の位置補正で解消される)
    _world->addBall(_arena_cx, _arena_cy, radius);
    for (int i = 1; i < _ball_count; i++) {
        float ring_r = (_arena_r - radius) * 0.6f;
        float rad    = i * (360.0f / (_ball_count - 1)) * _pi / 180.0f;
        _world->addBall(_arena_cx + ring_r * std::cos(rad), _arena_cy + ring_r * std::sin(rad), radius);
    }

    // 無重力で数ステップ回して重なりをほぐしておく
    for (int i = 0; i < 30; i++) {
        _world->step(0.016f, 0.0f, 0.0f);
    }

    LvglLockGuard lock;

    _view = std::make_unique<view::DropsView>();
    _view->init(lv_screen_active());

    _view->configure(_ball_count, d);

    // 前回開いたときの選択を維持 (画像が減っていた場合に備えてクランプ)
    _sprite_option = _sprite_option % _view->spriteOptionCount();
    _view->applySpriteOption(_sprite_option);

    for (int i = 0; i < _ball_count; i++) {
        const auto& ball = _world->balls[i];
        _view->setBall(i, ball.x, ball.y, ball.angle);
    }
}

void AppDrops::onRunning()
{
    const auto key_event = _key_manager->update();
    if (key_event == input::KeyEvent::GoHome) {
        close();
        return;
    }
    if (_view && _world) {
        bool count_changed  = false;
        bool option_changed = false;

        if (key_event == input::KeyEvent::GoNext || key_event == input::KeyEvent::GoPrevious) {
            const int option_count = _view->spriteOptionCount();
            const int step         = (key_event == input::KeyEvent::GoNext) ? 1 : -1;
            _sprite_option         = (_sprite_option + step + option_count) % option_count;
            option_changed         = true;
        }

        // 色玉モードは6色セットなので、6個未満の状態で切り替えたら6個まで補充
        const bool is_color_option = (_sprite_option == _view->spriteOptionCount() - 1);
        if (is_color_option && _ball_count < _ball_count_min_color) {
            while (_ball_count < _ball_count_min_color) {
                _ball_count++;
                _world->addBall(_arena_cx, _arena_cy, 1.0f);  // 半径は直後にまとめて更新
            }
            count_changed = true;
        }

        // タップで追加、長押しで削減 (サイズも半分埋まる基準で追従)
        if (_view->takeTap() && _ball_count < _ball_count_max) {
            _ball_count++;
            _world->addBall(_arena_cx, _arena_cy, 1.0f);
            count_changed = true;
        }
        const int count_min = is_color_option ? _ball_count_min_color : _ball_count_min;
        if (_view->takeLongPress() && _ball_count > count_min) {
            _ball_count--;
            _world->balls.pop_back();
            count_changed = true;
        }

        if (count_changed || option_changed) {
            LvglLockGuard lock;
            if (option_changed) {
                _view->applySpriteOption(_sprite_option);
            }
            if (count_changed) {
                const int d        = diameter_for_count(_ball_count);
                const float radius = d / 2.0f;
                for (auto& ball : _world->balls) {
                    ball.radius = radius;
                }
                _view->configure(_ball_count, d);
            }
        }
    }

    uint32_t now = GetHAL().millis();
    float dt     = static_cast<float>(now - _last_tick) / 1000.0f;
    _last_tick   = now;
    if (dt <= 0.0f) {
        return;
    }
    if (dt > 0.05f) {
        dt = 0.05f;
    }

    // I2C読み取りは1回9ms前後かかるので20ms間隔に制限
    if (now - _last_imu_read >= 20) {
        _last_imu_read = now;
        GetHAL().updateImuData();
        const auto& imu_data = GetHAL().getImuData();
        _filtered_ax += _filter_alpha * (imu_data.accelX - _filtered_ax);
        _filtered_ay += _filter_alpha * (imu_data.accelY - _filtered_ay);
    }

    // 画面座標系の「上」は +(ax, ay) 方向 (app_up_indicator で実機確認済み)
    // なので重力はその逆方向
    float gravity_x = -_filtered_ax * _gravity_px;
    float gravity_y = -_filtered_ay * _gravity_px;

    _world->step(dt, gravity_x, gravity_y);

    {
        // M5GFXでの直描き中にLVGL側のflushと重ならないようロックを取る
        LvglLockGuard lock;
        if (_view) {
            for (int i = 0; i < _ball_count && i < static_cast<int>(_world->balls.size()); i++) {
                const auto& ball = _world->balls[i];
                _view->setBall(i, ball.x, ball.y, ball.angle);
            }
            _view->renderFrame();
        }
    }
}

void AppDrops::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");

    _key_manager.reset();

    LvglLockGuard lock;

    _view.reset();
    _world.reset();
}
