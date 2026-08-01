/*
 * SPDX-FileCopyrightText: 2026 NagArgon
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <smooth_lvgl.hpp>
#include <uitk/short_namespace.hpp>
#include <memory>

namespace view {

class UpIndicatorView {
public:
    void init(lv_obj_t* parent);
    void update();

    // 「上」方向の単位ベクトル (画面座標系: +x 右, +y 下)
    void setDirection(float upX, float upY);
    void setAngleDeg(float angleDeg);
    void setFlat(bool isFlat);

private:
    std::unique_ptr<uitk::lvgl_cpp::Container> _panel;
    std::unique_ptr<uitk::lvgl_cpp::Container> _dial;
    std::unique_ptr<uitk::lvgl_cpp::Container> _top_marker;
    std::unique_ptr<uitk::lvgl_cpp::Label> _angle_label;
    std::unique_ptr<uitk::lvgl_cpp::Label> _flat_label;
    lv_obj_t* _arrow                     = nullptr;
    lv_point_precise_t _arrow_points[5]  = {};

    uitk::AnimateValue _anim_angle;
    bool _angle_initialized = false;
    float _angle_unwrapped  = 0.0f;
    float _last_drawn_angle = -1000.0f;
    bool _is_flat           = true;

    void applyArrowState();
    void applyFlatState();
};

}  // namespace view
