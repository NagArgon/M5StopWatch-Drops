/*
 * SPDX-FileCopyrightText: 2026 NagArgon
 *
 * SPDX-License-Identifier: MIT
 */
#include "view.h"
#include <lvgl_private.h>  // lv_image_decoder_dsc_t の完全定義
#include <hal/hal.h>
#include <esp_heap_caps.h>
#include <mooncake_log.h>
#include <dirent.h>
#include <unistd.h>
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace view;
using namespace uitk;
using namespace uitk::lvgl_cpp;

namespace {

constexpr int _panel_size = 466;

constexpr uint32_t _bg_color = 0x000000;

// マゼンタを透過キーにする (実画像がこの値になったら1bitずらして回避)
constexpr uint16_t _transp_key = 0xF81F;

// 回転はこの刻みでだけ更新して再合成を減らす [0.1deg]
constexpr int32_t _rotation_step = 30;

// 静止時でもこの間隔では再転送する (LVGL側の再描画で消された場合の保険) [ms]
constexpr uint32_t _idle_push_interval_ms = 500;

// Badge画像がないときのフォールバック色 (パステル)
constexpr uint32_t _fallback_colors[] = {0xFFB3BA, 0xFFDFBA, 0xFFFFBA, 0xBAFFC9, 0xBAE1FF, 0xE3BAFF};

static const std::string_view _tag = "DropsView";

// RGB565A8ワークバッファ (色プレーン w*h*2 + アルファプレーン w*h*1)
constexpr size_t sprite_buffer_size(int d)
{
    return static_cast<size_t>(d) * d * 3;
}

void sprite_write_pixel(uint8_t* buffer, int d, int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    reinterpret_cast<uint16_t*>(buffer)[y * d + x] =
        static_cast<uint16_t>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
    buffer[static_cast<size_t>(d) * d * 2 + y * d + x] = a;
}

// デコード済みバッファから1ピクセル読んで BGRA で返す (対応外フォーマットは false)
bool read_pixel(const uint8_t* data, uint32_t stride, lv_color_format_t cf, int x, int y, uint8_t out[4])
{
    switch (cf) {
        case LV_COLOR_FORMAT_RGB565: {
            uint16_t v = reinterpret_cast<const uint16_t*>(data + y * stride)[x];
            out[0]     = (v & 0x1F) << 3;
            out[1]     = ((v >> 5) & 0x3F) << 2;
            out[2]     = ((v >> 11) & 0x1F) << 3;
            out[3]     = 0xFF;
            return true;
        }
        case LV_COLOR_FORMAT_RGB888: {
            const uint8_t* px = data + y * stride + x * 3;
            out[0]            = px[0];
            out[1]            = px[1];
            out[2]            = px[2];
            out[3]            = 0xFF;
            return true;
        }
        case LV_COLOR_FORMAT_XRGB8888:
        case LV_COLOR_FORMAT_ARGB8888: {
            const uint8_t* px = data + y * stride + x * 4;
            out[0]            = px[0];
            out[1]            = px[1];
            out[2]            = px[2];
            out[3]            = (cf == LV_COLOR_FORMAT_ARGB8888) ? px[3] : 0xFF;
            return true;
        }
        default:
            return false;
    }
}

// トランプのスート形状 (u,v は中心原点・v上向き、マークが概ね |u|,|v|<=1 に収まるスケール)
bool heart_contains(float u, float v)
{
    // ハート曲線: (u^2 + v^2 - 1)^3 - u^2 * v^3 <= 0
    float q = u * u + v * v - 1.0f;
    return q * q * q - u * u * v * v * v <= 0.0f;
}

bool suit_contains(int suit, float u, float v)
{
    switch (suit) {
        case 0:  // ハート
            return heart_contains(u * 1.1f, v * 1.1f + 0.1f);
        case 1: {  // スペード: 逆さハート + 軸
            if (heart_contains(u * 1.1f, -v * 1.1f + 0.1f)) {
                return true;
            }
            return std::fabs(u) <= 0.35f * (0.2f - v) && v <= -0.2f && v >= -1.0f;
        }
        case 2:  // ダイヤ
            return std::fabs(u) * 1.4f + std::fabs(v) <= 1.1f;
        default: {  // クラブ: 円3つ + 軸
            float r_sq     = 0.45f * 0.45f;
            auto in_circle = [&](float cx, float cy) {
                return (u - cx) * (u - cx) + (v - cy) * (v - cy) <= r_sq;
            };
            if (in_circle(0.0f, 0.5f) || in_circle(-0.45f, -0.15f) || in_circle(0.45f, -0.15f)) {
                return true;
            }
            return std::fabs(u) <= 0.35f * (0.2f - v) && v <= -0.2f && v >= -1.0f;
        }
    }
}

}  // namespace

DropsView::~DropsView()
{
    _sprites.clear();
    _frame.reset();
    _panel.reset();
}

void DropsView::init(lv_obj_t* parent)
{
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
    _panel->addFlag(LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(_panel->get(), onPanelEvent, LV_EVENT_SHORT_CLICKED, this);
    lv_obj_add_event_cb(_panel->get(), onPanelEvent, LV_EVENT_LONG_PRESSED, this);

    // 全画面合成キャンバス
    _frame = std::make_unique<LGFX_Sprite>();
    _frame->setColorDepth(16);
    _frame->setPsram(true);
    if (_frame->createSprite(_panel_size, _panel_size) == nullptr) {
        mclog::tagError(_tag, "frame canvas alloc failed");
        _frame.reset();
    }

    // 診断用: badgeディレクトリの中身をログに出す
    DIR* dir = opendir("/spiflash/badge");
    if (dir != nullptr) {
        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            mclog::tagInfo(_tag, "badge dir entry: {}", entry->d_name);
        }
        closedir(dir);
    } else {
        mclog::tagWarn(_tag, "badge dir not found");
    }

    // Badge画像スロットを走査してスプライト化
    char fs_path[64]   = {};
    char lvgl_path[68] = {};
    for (int slot = 0; slot < 6; slot++) {
        for (const auto* ext : {"jpg", "jpeg", "png"}) {
            std::snprintf(fs_path, sizeof(fs_path), "/spiflash/badge/slot_%d.%s", slot, ext);
            if (access(fs_path, R_OK) != 0) {
                continue;
            }
            std::snprintf(lvgl_path, sizeof(lvgl_path), "A:%s", fs_path);
            if (bakeBadgeSprite(lvgl_path)) {
                _badge_sprite_count++;
                mclog::tagInfo(_tag, "baked sprite from {}", fs_path);
            }
            break;
        }
    }

    int suit = 0;
    for (auto color : _fallback_colors) {
        bakeColorSprite(color, suit++ % 4);
    }
}

void DropsView::configure(int count, int renderDiameter)
{
    _zoom = static_cast<float>(renderDiameter) / sprite_base_diameter;
    _balls.resize(count);
    _force_render = true;
    applySpriteOption(_current_option);
}

int DropsView::mixOptionOffset() const
{
    // Badge画像が2枚以上あるときだけ先頭に「ミックス」枠を置く
    return _badge_sprite_count >= 2 ? 1 : 0;
}

int DropsView::spriteOptionCount() const
{
    return mixOptionOffset() + _badge_sprite_count + 1;
}

void DropsView::applySpriteOption(int option)
{
    _current_option  = option;
    const int offset = mixOptionOffset();
    const int colors = static_cast<int>(_sprites.size()) - _badge_sprite_count;

    for (std::size_t i = 0; i < _balls.size(); i++) {
        if (offset == 1 && option == 0) {
            // ミックス: ドロップ毎に違うBadge画像
            _balls[i].sprite = i % _badge_sprite_count;
        } else if (option - offset < _badge_sprite_count) {
            _balls[i].sprite = option - offset;
        } else {
            _balls[i].sprite = _badge_sprite_count + (i % colors);
        }
    }
    _force_render = true;
}

void DropsView::setBall(int index, float x, float y, float angleDeg)
{
    if (index < 0 || index >= static_cast<int>(_balls.size())) {
        return;
    }
    _balls[index].x     = x;
    _balls[index].y     = y;
    _balls[index].angle = angleDeg;
}

void DropsView::renderFrame()
{
    if (_frame == nullptr) {
        return;
    }

    // 変化チェック (位置1px、回転3度単位)
    bool moved = _force_render;
    for (auto& ball : _balls) {
        int32_t px  = static_cast<int32_t>(ball.x + 0.5f);
        int32_t py  = static_cast<int32_t>(ball.y + 0.5f);
        int32_t rot = (static_cast<int32_t>(ball.angle * 10.0f) / _rotation_step) * _rotation_step;
        if (px != ball.drawn_x || py != ball.drawn_y || rot != ball.drawn_rot) {
            moved = true;
        }
        ball.drawn_x   = px;
        ball.drawn_y   = py;
        ball.drawn_rot = rot;
    }

    uint32_t now = GetHAL().millis();
    if (!moved && now - _last_push_ms < _idle_push_interval_ms) {
        return;
    }
    _force_render = false;
    _last_push_ms = now;

    _frame->fillSprite(TFT_BLACK);
    for (auto& ball : _balls) {
        if (ball.sprite < 0 || ball.sprite >= static_cast<int>(_sprites.size())) {
            continue;
        }
        _sprites[ball.sprite]->pushRotateZoom(_frame.get(), static_cast<float>(ball.drawn_x),
                                              static_cast<float>(ball.drawn_y), ball.drawn_rot / 10.0f, _zoom, _zoom,
                                              _transp_key);
    }

    auto& display = GetHAL().getDisplay();
    _frame->pushSprite(&display, 0, 0);
}

bool DropsView::takeTap()
{
    return _tap_pending.exchange(false);
}

bool DropsView::takeLongPress()
{
    return _long_press_pending.exchange(false);
}

void DropsView::onPanelEvent(lv_event_t* e)
{
    auto* self = static_cast<DropsView*>(lv_event_get_user_data(e));
    if (self == nullptr) {
        return;
    }
    if (lv_event_get_code(e) == LV_EVENT_SHORT_CLICKED) {
        self->_tap_pending.store(true);
    } else if (lv_event_get_code(e) == LV_EVENT_LONG_PRESSED) {
        self->_long_press_pending.store(true);
    }
}

bool DropsView::addSpriteFromRgb565a8(const uint8_t* buffer)
{
    const int d = sprite_base_diameter;

    auto sprite = std::make_unique<LGFX_Sprite>();
    sprite->setColorDepth(16);
    sprite->setPsram(true);
    if (sprite->createSprite(d, d) == nullptr) {
        mclog::tagError(_tag, "ball sprite alloc failed");
        return false;
    }
    sprite->setPivot(d / 2.0f - 0.5f, d / 2.0f - 0.5f);

    const uint16_t* color_plane = reinterpret_cast<const uint16_t*>(buffer);
    const uint8_t* alpha_plane  = buffer + static_cast<size_t>(d) * d * 2;
    for (int y = 0; y < d; y++) {
        for (int x = 0; x < d; x++) {
            uint16_t color;
            if (alpha_plane[y * d + x] < 128) {
                color = _transp_key;
            } else {
                color = color_plane[y * d + x];
                if (color == _transp_key) {
                    color ^= 0x0001;  // 実画像が透過キーと衝突したら1bitずらす
                }
            }
            sprite->drawPixel(x, y, color);
        }
    }

    _sprites.push_back(std::move(sprite));
    return true;
}

bool DropsView::bakeBadgeSprite(const char* lvglPath)
{
    lv_image_decoder_dsc_t dec;
    lv_result_t res = lv_image_decoder_open(&dec, lvglPath, nullptr);
    if (res != LV_RESULT_OK) {
        mclog::tagWarn(_tag, "decoder open failed: {}", lvglPath);
        return false;
    }

    const int src_w = dec.header.w;
    const int src_h = dec.header.h;
    if (src_w <= 0 || src_h <= 0) {
        lv_image_decoder_close(&dec);
        return false;
    }

    const int d              = sprite_base_diameter;
    const size_t buffer_size = sprite_buffer_size(d);
    uint8_t* buffer = static_cast<uint8_t*>(heap_caps_malloc(buffer_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (buffer == nullptr) {
        mclog::tagError(_tag, "sprite buffer alloc failed");
        lv_image_decoder_close(&dec);
        return false;
    }
    std::memset(buffer, 0, buffer_size);

    // 短辺で中央クロップして最近傍サンプリング
    const int min_side = src_w < src_h ? src_w : src_h;
    const int x0       = (src_w - min_side) / 2;
    const int y0       = (src_h - min_side) / 2;

    bool ok       = true;
    int rows_done = 0;

    // デコード済み領域 (帯) から、そこに含まれるソース行に対応する出力行を埋める
    auto sample_strip = [&](const lv_draw_buf_t* strip, const lv_area_t* area) {
        const uint32_t stride      = strip->header.stride;
        const lv_color_format_t cf = static_cast<lv_color_format_t>(strip->header.cf);
        const uint8_t* data        = strip->data;
        for (int y = 0; y < d && ok; y++) {
            int sy = y0 + y * min_side / d;
            if (sy < area->y1 || sy > area->y2) {
                continue;
            }
            for (int x = 0; x < d; x++) {
                int sx = x0 + x * min_side / d;
                if (sx < area->x1 || sx > area->x2) {
                    continue;
                }
                uint8_t bgra[4];
                if (!read_pixel(data, stride, cf, sx - area->x1, sy - area->y1, bgra)) {
                    mclog::tagWarn(_tag, "unsupported color format: {}", static_cast<int>(cf));
                    ok = false;
                    break;
                }
                sprite_write_pixel(buffer, d, x, y, bgra[2], bgra[1], bgra[0], bgra[3]);
            }
            rows_done++;
        }
    };

    if (dec.decoded != nullptr) {
        // 一括デコードできるデコーダ (PNG等)
        lv_area_t whole = {0, 0, src_w - 1, src_h - 1};
        sample_strip(dec.decoded, &whole);
    } else {
        // ストリーミングデコーダ (TJPGD等): 帯単位で受け取る
        lv_area_t full_area = {0, 0, src_w - 1, src_h - 1};
        lv_area_t strip_area;
        strip_area.x1 = LV_COORD_MIN;
        strip_area.y1 = LV_COORD_MIN;
        strip_area.x2 = LV_COORD_MIN;
        strip_area.y2 = LV_COORD_MIN;
        while (ok && lv_image_decoder_get_area(&dec, &full_area, &strip_area) == LV_RESULT_OK) {
            if (dec.decoded != nullptr) {
                sample_strip(dec.decoded, &strip_area);
            }
        }
    }
    lv_image_decoder_close(&dec);

    if (!ok || rows_done < d) {
        mclog::tagWarn(_tag, "bake incomplete: {} (rows={}/{})", lvglPath, rows_done, d);
        heap_caps_free(buffer);
        return false;
    }

    applyCircleMask(buffer);
    bool added = addSpriteFromRgb565a8(buffer);
    heap_caps_free(buffer);
    return added;
}

void DropsView::bakeColorSprite(uint32_t colorHex, int suit)
{
    const int d              = sprite_base_diameter;
    const size_t buffer_size = sprite_buffer_size(d);
    uint8_t* buffer = static_cast<uint8_t*>(heap_caps_malloc(buffer_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (buffer == nullptr) {
        mclog::tagError(_tag, "sprite buffer alloc failed");
        return;
    }

    const uint8_t r = (colorHex >> 16) & 0xFF;
    const uint8_t g = (colorHex >> 8) & 0xFF;
    const uint8_t b = colorHex & 0xFF;

    // スートの色: ハート/ダイヤは赤、スペード/クラブは黒に近いグレー
    const bool is_red    = (suit == 0 || suit == 2);
    const uint8_t mark_r = is_red ? 0xD0 : 0x3A;
    const uint8_t mark_g = is_red ? 0x45 : 0x3A;
    const uint8_t mark_b = is_red ? 0x5C : 0x3A;

    const float c = (d - 1) / 2.0f;
    for (int y = 0; y < d; y++) {
        for (int x = 0; x < d; x++) {
            // マークは玉の6割サイズ、v は上向きが正の数学系座標
            float u = (x - c) / (d * 0.30f);
            float v = -(y - c) / (d * 0.30f);
            bool in = suit_contains(suit, u, v);
            sprite_write_pixel(buffer, d, x, y, in ? mark_r : r, in ? mark_g : g, in ? mark_b : b, 0xFF);
        }
    }

    applyCircleMask(buffer);
    addSpriteFromRgb565a8(buffer);
    heap_caps_free(buffer);
}

void DropsView::applyCircleMask(uint8_t* buffer)
{
    const int d    = sprite_base_diameter;
    const float c  = (d - 1) / 2.0f;
    const float rr = d / 2.0f;
    uint8_t* alpha = buffer + static_cast<size_t>(d) * d * 2;

    for (int y = 0; y < d; y++) {
        for (int x = 0; x < d; x++) {
            float dist    = std::sqrt((x - c) * (x - c) + (y - c) * (y - c));
            float falloff = rr - dist;
            uint8_t* a    = &alpha[y * d + x];
            if (falloff <= 0.0f) {
                *a = 0;
            } else if (falloff < 1.0f) {
                *a = static_cast<uint8_t>(*a * falloff);
            }
        }
    }
}
