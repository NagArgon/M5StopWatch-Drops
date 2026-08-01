/*
 * SPDX-FileCopyrightText: 2026 NagArgon
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <cmath>
#include <cstddef>
#include <vector>

namespace drops {

// 円形アリーナ内を転がる円形ボディの簡易2D物理エンジン
class World {
public:
    struct Ball {
        float x = 0.0f, y = 0.0f;    // 中心座標 [px]
        float vx = 0.0f, vy = 0.0f;  // 速度 [px/s]
        float angle = 0.0f;          // 回転角 [deg]
        float omega = 0.0f;          // 角速度 [deg/s]
        float radius = 36.0f;
    };

    void setup(float centerX, float centerY, float arenaRadius)
    {
        _cx = centerX;
        _cy = centerY;
        _arena_radius = arenaRadius;
        balls.clear();
    }

    void addBall(float x, float y, float radius)
    {
        Ball ball;
        ball.x      = x;
        ball.y      = y;
        ball.radius = radius;
        balls.push_back(ball);
    }

    // gravityX/Y: 画面座標系の重力加速度 [px/s^2]
    void step(float dt, float gravityX, float gravityY)
    {
        constexpr int substeps = 2;
        float h                = dt / substeps;

        for (int s = 0; s < substeps; s++) {
            for (auto& ball : balls) {
                ball.vx += gravityX * h;
                ball.vy += gravityY * h;
                // 空気抵抗的な減衰
                float damping = 1.0f - _linear_damping * h;
                ball.vx *= damping;
                ball.vy *= damping;
                ball.omega *= damping;
                ball.x += ball.vx * h;
                ball.y += ball.vy * h;
                ball.angle += ball.omega * h;
                if (ball.angle > 360.0f) ball.angle -= 360.0f;
                if (ball.angle < 0.0f) ball.angle += 360.0f;
            }

            for (std::size_t i = 0; i < balls.size(); i++) {
                for (std::size_t j = i + 1; j < balls.size(); j++) {
                    resolveBallPair(balls[i], balls[j]);
                }
                resolveWall(balls[i]);
            }
        }
    }

    std::vector<Ball> balls;

private:
    float _cx = 0.0f, _cy = 0.0f;
    float _arena_radius = 0.0f;

    static constexpr float _restitution      = 0.15f;  // 低反発: 弾まず積み重なる
    static constexpr float _wall_restitution = 0.25f;
    static constexpr float _linear_damping   = 0.4f;   // [1/s]
    static constexpr float _roll_blend       = 0.25f;  // 接触時に転がり回転へ寄せる割合
    static constexpr float _rad_to_deg       = 57.29578f;

    void resolveBallPair(Ball& a, Ball& b)
    {
        float dx        = b.x - a.x;
        float dy        = b.y - a.y;
        float dist_sq   = dx * dx + dy * dy;
        float min_dist  = a.radius + b.radius;
        if (dist_sq >= min_dist * min_dist || dist_sq < 1e-9f) {
            return;
        }

        float dist = std::sqrt(dist_sq);
        float nx   = dx / dist;
        float ny   = dy / dist;

        // 位置補正: めり込みを半分ずつ押し戻す
        float overlap = min_dist - dist;
        a.x -= nx * overlap * 0.5f;
        a.y -= ny * overlap * 0.5f;
        b.x += nx * overlap * 0.5f;
        b.y += ny * overlap * 0.5f;

        // 法線方向の相対速度に反発インパルス (等質量)
        float rel_vn = (b.vx - a.vx) * nx + (b.vy - a.vy) * ny;
        if (rel_vn < 0.0f) {
            float impulse = -(1.0f + _restitution) * rel_vn * 0.5f;
            a.vx -= impulse * nx;
            a.vy -= impulse * ny;
            b.vx += impulse * nx;
            b.vy += impulse * ny;
        }

        // 接線方向の相対速度から転がり回転を付ける
        float rel_vt = (b.vx - a.vx) * -ny + (b.vy - a.vy) * nx;
        blendRoll(a, rel_vt);
        blendRoll(b, rel_vt);
    }

    void resolveWall(Ball& ball)
    {
        float dx      = ball.x - _cx;
        float dy      = ball.y - _cy;
        float dist_sq = dx * dx + dy * dy;
        float limit   = _arena_radius - ball.radius;
        if (dist_sq <= limit * limit) {
            return;
        }

        float dist = std::sqrt(dist_sq);
        if (dist < 1e-6f) {
            return;
        }
        // 壁の内向き法線
        float nx = -dx / dist;
        float ny = -dy / dist;

        ball.x = _cx - nx * limit;
        ball.y = _cy - ny * limit;

        float vn = ball.vx * nx + ball.vy * ny;
        if (vn < 0.0f) {
            ball.vx -= (1.0f + _wall_restitution) * vn * nx;
            ball.vy -= (1.0f + _wall_restitution) * vn * ny;
        }

        // 壁に沿った速度で転がす (接線 = 法線を90度回転)
        float vt           = ball.vx * -ny + ball.vy * nx;
        float omega_target = vt / ball.radius * _rad_to_deg;
        ball.omega += _roll_blend * (omega_target - ball.omega);
    }

    void blendRoll(Ball& ball, float tangential_velocity)
    {
        float omega_target = tangential_velocity / ball.radius * _rad_to_deg;
        ball.omega += _roll_blend * 0.5f * (omega_target - ball.omega);
    }
};

}  // namespace drops
