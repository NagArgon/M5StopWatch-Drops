/*
 * SPDX-FileCopyrightText: 2026 NagArgon
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <smooth_lvgl.hpp>
#include <uitk/short_namespace.hpp>
#include <lvgl.h>
#include <M5GFX.h>
#include <atomic>
#include <memory>
#include <vector>

namespace view {

// 玉の合成はLVGLを使わず、M5GFXのスプライト回転合成 + 全画面キャンバスのDMA転送で行う。
// LVGLはタッチ/入力と画面遷移のために黒背景パネルだけ残す。
class DropsView {
public:
    // スプライトはこのサイズで焼き、表示はpushRotateZoomの倍率で縮小する
    static constexpr int sprite_base_diameter = 126;

    ~DropsView();

    // Badge画像の走査とスプライト焼き込み (開いたとき一度だけ)
    void init(lv_obj_t* parent);

    // 玉の数とレンダリング径 [px] を設定
    void configure(int count, int renderDiameter);

    // 選択肢: [ミックス(Badge2枚以上のとき)] + [各Badge画像] + [色玉ミックス]
    int spriteOptionCount() const;
    void applySpriteOption(int option);

    // 中心座標 [px] と回転角 [deg] を保存 (描画はrenderFrameでまとめて行う)
    void setBall(int index, float x, float y, float angleDeg);

    // 全玉をキャンバスに合成してパネルへ転送 (変化がなければスキップ)
    void renderFrame();

    // タッチ操作 (読み取ると同時にクリアされる)
    bool takeTap();
    bool takeLongPress();

private:
    struct BallState {
        float x     = 0.0f;
        float y     = 0.0f;
        float angle = 0.0f;
        int sprite  = 0;  // _sprites のインデックス
        int32_t drawn_x   = INT32_MIN;
        int32_t drawn_y   = INT32_MIN;
        int32_t drawn_rot = INT32_MIN;  // 量子化済み [0.1deg]
    };

    std::unique_ptr<uitk::lvgl_cpp::Container> _panel;
    std::unique_ptr<LGFX_Sprite> _frame;  // 全画面キャンバス
    std::vector<std::unique_ptr<LGFX_Sprite>> _sprites;  // Badge画像 → 色玉 の順
    int _badge_sprite_count = 0;
    std::vector<BallState> _balls;
    float _zoom            = 1.0f;
    int _current_option    = 0;
    bool _force_render     = true;
    uint32_t _last_push_ms = 0;

    std::atomic<bool> _tap_pending{false};
    std::atomic<bool> _long_press_pending{false};

    bool bakeBadgeSprite(const char* lvglPath);
    void bakeColorSprite(uint32_t colorHex, int suit);
    // RGB565A8ワークバッファからマゼンタ透過キーのLGFXスプライトを作る
    bool addSpriteFromRgb565a8(const uint8_t* buffer);
    void applyCircleMask(uint8_t* buffer);
    int mixOptionOffset() const;

    static void onPanelEvent(lv_event_t* e);
};

}  // namespace view
