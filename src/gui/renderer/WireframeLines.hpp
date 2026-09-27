#pragma once

#include <imgui_internal.h>
#include <span>

namespace WireframeLines {
struct Line { ImVec2 a, b; ImU32 color; };

// Batch independent segments using ImGui's baked AA-line texture. Preserve the
// normal AddLine path for fractional widths, non-textured AA, or scaled fringe.
inline void Draw(ImDrawList* draw, std::span<const Line> lines, float thickness = 1.f) {
    const int width = static_cast<int>(thickness);
    const bool textured = (draw->Flags & ImDrawListFlags_AntiAliasedLines) &&
        (draw->Flags & ImDrawListFlags_AntiAliasedLinesUseTex) && draw->_FringeScale == 1.f &&
        width >= 1 && width < IM_DRAWLIST_TEX_LINES_WIDTH_MAX && thickness-width <= .00001f;
    if (!textured) {
        for (const auto& line : lines) draw->AddLine(line.a,line.b,line.color,thickness);
        return;
    }
    const auto uv = draw->_Data->TexUvLines[width];
    const ImVec2 uv0{uv.x,uv.y}, uv1{uv.z,uv.w};
    const float half = thickness*.5f+1.f;
    // Small reservations also respect ImGui's 16-bit index / VtxOffset split.
    for (size_t first=0; first<lines.size();) {
        const size_t count=std::min(size_t(1024),lines.size()-first);
        draw->PrimReserve(static_cast<int>(count*6),static_cast<int>(count*4));
        for (const auto& line : lines.subspan(first,count)) {
            const float dx=line.b.x-line.a.x, dy=line.b.y-line.a.y;
            const float length=std::sqrt(dx*dx+dy*dy);
            const float scale=length>0 ? half/length : 0;
            const ImVec2 normal{dy*scale,-dx*scale};
            // AddLine's half-pixel alignment is retained.
            const ImVec2 a{line.a.x+.5f,line.a.y+.5f}, b{line.b.x+.5f,line.b.y+.5f};
            draw->PrimQuadUV(a+normal,b+normal,b-normal,a-normal,uv0,uv0,uv1,uv1,line.color);
        }
        first+=count;
    }
}
}
