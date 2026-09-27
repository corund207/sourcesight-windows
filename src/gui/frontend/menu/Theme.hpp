#pragma once

#include <imgui.h>

// SourceSight Graphite Studio palette: charcoal surfaces and sage-green accents.
// Shared by the menu and lightweight overlay panels.
namespace theme {

inline constexpr ImVec4 kSurfaceBase = ImVec4(.22f,.24f,.25f,1);
inline constexpr ImVec4 kSurfaceElev1 = ImVec4(.17f,.18f,.19f,1);
inline constexpr ImVec4 kSurfaceElev2 = ImVec4(.21f,.23f,.24f,1);
inline constexpr ImVec4 kSurfaceDeep = ImVec4(.12f,.13f,.14f,1);

inline constexpr ImVec4 kBorderBase = ImVec4(.27f,.29f,.30f,1);
inline constexpr ImVec4 kBorderStrong = ImVec4(.42f,.46f,.44f,1);

// Graphite Studio sage-green accents, with pale green for primary actions.
inline constexpr ImVec4 kAccent = ImVec4(.48f,.66f,.45f,1);
inline constexpr ImVec4 kAccentBright = ImVec4(.37f,.56f,.38f,1);
inline constexpr ImVec4 kAccentDim = ImVec4(.64f,.76f,.60f,.66f);
inline constexpr ImVec4 kAccentSoft = ImVec4(.77f,.88f,.70f,1);
inline constexpr ImVec4 kAccentStrong = ImVec4(.18f,.30f,.21f,1);
inline constexpr ImVec4 kAccentGlow = ImVec4(.48f,.66f,.45f,.16f);

inline constexpr ImVec4 kTextPrimary = ImVec4(.91f,.93f,.92f,1);
inline constexpr ImVec4 kTextSecondary = ImVec4(.73f,.77f,.74f,1);
inline constexpr ImVec4 kTextMuted = ImVec4(.64f,.68f,.66f,1);
inline constexpr ImVec4 kTextDisabled = ImVec4(.64f,.68f,.66f,1);
inline constexpr ImVec4 kSignalOK = ImVec4(0.310f, 0.800f, 0.600f, 1.0f);
inline constexpr ImVec4 kSignalWarn = ImVec4(0.950f, 0.660f, 0.260f, 1.0f);
inline constexpr ImVec4 kSignalErr = ImVec4(0.960f, 0.420f, 0.440f, 1.0f);
// Kept for the existing notice overlay; it is informational, not a second UI accent.
inline constexpr ImVec4 kGold = ImVec4(0.930f, 0.790f, 0.450f, 1.0f);

inline constexpr float kFontBody = 14.0f;
inline constexpr float kFontHeading = 25.0f;
inline constexpr float kWindowRounding = 24.0f;
inline constexpr float kChildRounding = 18.0f;
inline constexpr float kFrameRounding = 10.0f;
inline constexpr float kPopupRounding = 12.0f;
inline constexpr float kWindowPad = 20.0f;
inline constexpr float kCardPad = 20.0f;
inline constexpr float kItemGap = 12.0f;

inline ImU32 Pack(const ImVec4& c) {
    return IM_COL32(static_cast<int>(c.x * 255.0f), static_cast<int>(c.y * 255.0f),
                    static_cast<int>(c.z * 255.0f), static_cast<int>(c.w * 255.0f));
}

inline ImVec4 WithAlpha(const ImVec4& c, float a) {
    return ImVec4(c.x, c.y, c.z, c.w * a);
}

// Shared with overlay consumers. This is a restrained graphite surface, not
// an aurora/glass treatment; the helper remains for source compatibility.
inline void DrawBackdrop(ImDrawList* d, const ImVec2& p, const ImVec2& s, float r) {
    const ImVec2 end = p + s;
    d->AddRectFilled(p, end, Pack(kSurfaceDeep), r);
    d->AddRect(p, end, Pack(WithAlpha(kBorderBase, 0.70f)), r, 0, 1.0f);
}

inline void DrawGlass(ImDrawList* d, const ImVec2& p, const ImVec2& s, float r,
                      const ImVec4& fill, float alpha = 1.0f, bool border = true) {
    const ImVec2 end = p + s;
    d->AddRectFilled(p, end, Pack(WithAlpha(fill, alpha)), r);
    d->AddRectFilled(ImVec2(p.x + 1.0f, p.y), ImVec2(end.x - 1.0f, p.y + 1.0f),
                     Pack(WithAlpha(kAccentDim, 0.24f * alpha)), r * 0.35f);
    if (border)
        d->AddRect(p, end, Pack(WithAlpha(kBorderBase, alpha)), r, 0, 1.0f);
}

} // namespace theme
