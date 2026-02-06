#pragma once
#include <cmath>

struct Vec2 { float x, y; };
struct Vec3 { float x, y, z; };

struct ViewState {
    float cosYaw, sinYaw;
    float cosPitch, sinPitch;
    float tanHalfFov;
    float aspectRatio;
    float screenW, screenH;
};

inline ViewState PrecomputeViewState(float yaw, float pitch, float fov, float screenW, float screenH) {
    float yawRad = -yaw * (3.14159f / 180.0f);
    float pitchRad = -pitch * (3.14159f / 180.0f);
    float fovRad = fov * (3.14159f / 180.0f);

    return {
        cos(yawRad), sin(yawRad),
        cos(pitchRad), sin(pitchRad),
        tan(fovRad / 2.0f),
        screenW / screenH,
        screenW, screenH
    };
}

inline Vec3 WorldToCamera(float x, float y, float z, const ViewState& vs) {
    // 1. Yaw Rotation (Y-axis)
    float x1 = -(x * vs.cosYaw - z * vs.sinYaw);
    float z1 = x * vs.sinYaw + z * vs.cosYaw;
    float y1 = y;

    // 2. Pitch Rotation (X-axis)
    float y2 = y1 * vs.cosPitch - z1 * vs.sinPitch;
    float z2 = y1 * vs.sinPitch + z1 * vs.cosPitch;
    float x2 = x1;
    
    return {x2, y2, z2};
}

inline Vec2 CameraToScreen(const Vec3& p, const ViewState& vs) {
    // 3. Projection
    // Use a smaller epsilon because clipping sets Z exactly to 0.1f
    if (p.z < 0.05f) return { -10000, -10000 };

    float x_screen = (p.x / (p.z * vs.tanHalfFov * vs.aspectRatio)) * 0.5f + 0.5f;
    float y_screen = 0.5f - (p.y / (p.z * vs.tanHalfFov)) * 0.5f;

    return { x_screen * vs.screenW, y_screen * vs.screenH };
}

inline Vec2 WorldToScreen(float x, float y, float z, const ViewState& vs) {
    Vec3 p = WorldToCamera(x, y, z, vs);
    return CameraToScreen(p, vs);
}

// Keep legacy overload for compatibility if needed (optional)
inline Vec2 WorldToScreen(float x, float y, float z, float yaw, float pitch, float fov, float screenW, float screenH) {
    auto vs = PrecomputeViewState(yaw, pitch, fov, screenW, screenH);
    return WorldToScreen(x, y, z, vs);
}
