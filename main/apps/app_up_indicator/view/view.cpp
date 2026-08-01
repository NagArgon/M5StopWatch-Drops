/*
 * SPDX-FileCopyrightText: 2026 NagArgon
 *
 * SPDX-License-Identifier: MIT
 */
#include "view.h"
#include <cmath>
#include <cstdio>

using namespace view;
using namespace uitk;
using namespace uitk::lvgl_cpp;

namespace {

constexpr int _panel_size       = 466;
constexpr int _center           = 233;
constexpr int _dial_size        = 420;
constexpr int _dial_border      = 3;
constexpr int _top_marker_size  = 14;
constexpr int _arrow_tip_radius = 170;
constexpr int _arrow_tail_radius = 130;
constexpr int _arrow_head_length = 52;
constexpr int _arrow_line_width  = 10;
constexpr int _angle_label_y     = 160;

constexpr uint32_t _bg_color     = 0x000000;
constexpr uint32_t _dial_color   = 0x3A3A3A;
constexpr uint32_t _marker_color = 0xCBFFB1;
constexpr uint32_t _arrow_color  = 0xFF5A5A;
constexpr uint32_t _label_color  = 0xCBFFB1;

constexpr float _pi = 3.14159265358979323846f;

float unwrap_angle(float previous_unwrapped_angle, float next_angle)
{
    float previous_angle = std::fmod(previous_unwrapped_angle, 360.0f);
    if (previous_angle < 0.0f) {
        previous_angle += 360.0f;
    }

    float delta = std::fmod(next_angle - previous_angle, 360.0f);
    if (delta < -180.0f) {
        delta += 360.0f;
    } else if (delta > 180.0f) {
        delta -= 360.0f;
    }

    return previous_unwrapped_angle + delta;
}

}  // namespace

void UpIndicatorView::init(lv_obj_t* parent)
{
    _angle_initialized = false;
    _angle_unwrapped   = 0.0f;
    _last_drawn_angle  = -1000.0f;
    _is_flat           = true;

    _anim_angle.pause();
    _anim_angle.easingOptions().duration       = 0.15;
    _anim_angle.easingOptions().easingFunction = ease::linear;
    _anim_angle.teleport(0.0f);
    _anim_angle.play();

    _panel = std::make_unique<Container>(parent);
    _panel->setSize(_panel_size, _panel_size);
    _panel->setAlign(LV_ALIGN_CENTER);
    _panel->setBgColor(lv_color_hex(_bg_color));
    _panel->setBgOpa(LV_OPA_COVER);
    _panel->setBorderWidth(0);
    _panel->setOutlineWidth(0);
    _panel->setShadowWidth(0);
    _panel->setPaddingAll(0);
    _panel->setRadius(0);
    _panel->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

    _dial = std::make_unique<Container>(_panel->get());
    _dial->setSize(_dial_size, _dial_size);
    _dial->setAlign(LV_ALIGN_CENTER);
    _dial->setBgOpa(LV_OPA_TRANSP);
    _dial->setBorderWidth(_dial_border);
    _dial->setBorderColor(lv_color_hex(_dial_color));
    _dial->setOutlineWidth(0);
    _dial->setShadowWidth(0);
    _dial->setPaddingAll(0);
    _dial->setRadius(LV_RADIUS_CIRCLE);
    _dial->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

    // 端末画面の「真上」に置く固定マーカー (矢印がここを指せば端末が正立)
    _top_marker = std::make_unique<Container>(_panel->get());
    _top_marker->setSize(_top_marker_size, _top_marker_size);
    _top_marker->align(LV_ALIGN_CENTER, 0, -(_dial_size / 2));
    _top_marker->setBgColor(lv_color_hex(_marker_color));
    _top_marker->setBgOpa(LV_OPA_COVER);
    _top_marker->setBorderWidth(0);
    _top_marker->setOutlineWidth(0);
    _top_marker->setShadowWidth(0);
    _top_marker->setPaddingAll(0);
    _top_marker->setRadius(LV_RADIUS_CIRCLE);
    _top_marker->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

    _arrow = lv_line_create(_panel->get());
    lv_obj_set_style_line_width(_arrow, _arrow_line_width, 0);
    lv_obj_set_style_line_color(_arrow, lv_color_hex(_arrow_color), 0);
    lv_obj_set_style_line_rounded(_arrow, true, 0);
    lv_obj_remove_flag(_arrow, LV_OBJ_FLAG_SCROLLABLE);

    _angle_label = std::make_unique<Label>(_panel->get());
    _angle_label->align(LV_ALIGN_CENTER, 0, _angle_label_y);
    _angle_label->setTextFont(&lv_font_montserrat_16);
    _angle_label->setTextColor(lv_color_hex(_label_color));
    _angle_label->setBgOpa(LV_OPA_TRANSP);
    _angle_label->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
    _angle_label->setText("---");

    _flat_label = std::make_unique<Label>(_panel->get());
    _flat_label->setAlign(LV_ALIGN_CENTER);
    _flat_label->setTextFont(&lv_font_montserrat_16);
    _flat_label->setTextColor(lv_color_hex(_dial_color));
    _flat_label->setBgOpa(LV_OPA_TRANSP);
    _flat_label->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
    _flat_label->setText("FLAT");

    applyFlatState();
    applyArrowState();
}

void UpIndicatorView::update()
{
    if (!_anim_angle.done()) {
        applyArrowState();
    }
}

void UpIndicatorView::setDirection(float upX, float upY)
{
    // 描画は角度ベースで行うので、ベクトルから画面真上基準の時計回り角度に変換
    float angle = std::atan2(upX, -upY) * 180.0f / _pi;
    if (angle < 0.0f) {
        angle += 360.0f;
    }

    if (!_angle_initialized) {
        _angle_unwrapped   = angle;
        _angle_initialized = true;
        _anim_angle.teleport(angle);
    } else {
        _angle_unwrapped = unwrap_angle(_angle_unwrapped, angle);
    }

    _anim_angle = _angle_unwrapped;
}

void UpIndicatorView::setAngleDeg(float angleDeg)
{
    if (_angle_label) {
        char buffer[16] = {};
        std::snprintf(buffer, sizeof(buffer), "%d deg", static_cast<int>(angleDeg + 0.5f) % 360);
        _angle_label->setText(buffer);
    }
}

void UpIndicatorView::setFlat(bool isFlat)
{
    if (_is_flat == isFlat) {
        return;
    }
    _is_flat = isFlat;
    applyFlatState();
}

void UpIndicatorView::applyArrowState()
{
    if (_arrow == nullptr) {
        return;
    }

    float angle = _anim_angle;
    if (angle == _last_drawn_angle) {
        return;
    }
    _last_drawn_angle = angle;

    float rad = angle * _pi / 180.0f;
    // 画面真上基準・時計回り角度の単位ベクトル (画面座標系: +y 下)
    float ux = std::sin(rad);
    float uy = -std::cos(rad);

    float tip_x  = _center + _arrow_tip_radius * ux;
    float tip_y  = _center + _arrow_tip_radius * uy;
    float tail_x = _center - _arrow_tail_radius * ux;
    float tail_y = _center - _arrow_tail_radius * uy;

    // 矢じり: 先端から後方へ ±30 度開いた 2 本
    constexpr float head_open = 30.0f * _pi / 180.0f;
    float back_rad_l          = rad + _pi - head_open;
    float back_rad_r          = rad + _pi + head_open;
    float head_l_x            = tip_x + _arrow_head_length * std::sin(back_rad_l);
    float head_l_y            = tip_y - _arrow_head_length * std::cos(back_rad_l);
    float head_r_x            = tip_x + _arrow_head_length * std::sin(back_rad_r);
    float head_r_y            = tip_y - _arrow_head_length * std::cos(back_rad_r);

    _arrow_points[0] = {static_cast<lv_value_precise_t>(tail_x), static_cast<lv_value_precise_t>(tail_y)};
    _arrow_points[1] = {static_cast<lv_value_precise_t>(tip_x), static_cast<lv_value_precise_t>(tip_y)};
    _arrow_points[2] = {static_cast<lv_value_precise_t>(head_l_x), static_cast<lv_value_precise_t>(head_l_y)};
    _arrow_points[3] = {static_cast<lv_value_precise_t>(tip_x), static_cast<lv_value_precise_t>(tip_y)};
    _arrow_points[4] = {static_cast<lv_value_precise_t>(head_r_x), static_cast<lv_value_precise_t>(head_r_y)};

    lv_line_set_points(_arrow, _arrow_points, 5);
}

void UpIndicatorView::applyFlatState()
{
    if (_arrow != nullptr) {
        lv_obj_set_style_line_opa(_arrow, _is_flat ? LV_OPA_20 : LV_OPA_COVER, 0);
    }
    if (_flat_label) {
        if (_is_flat) {
            _flat_label->removeFlag(LV_OBJ_FLAG_HIDDEN);
        } else {
            _flat_label->addFlag(LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (_angle_label) {
        if (_is_flat) {
            _angle_label->setText("---");
        }
    }
}
