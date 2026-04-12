#pragma once

#include "gui/theme.hpp"
#include "space/grid.hpp"

#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <unordered_set>
#include <vector>

namespace gui::panels {

namespace detail {

inline ImU32 shade(ImU32 col, float f) {
    int r = int(std::min(255.0f, float( col        & 0xFF) * f));
    int g = int(std::min(255.0f, float((col >>  8) & 0xFF) * f));
    int b = int(std::min(255.0f, float((col >> 16) & 0xFF) * f));
    int a = (col >> 24) & 0xFF;
    return IM_COL32(r, g, b, a);
}

} // namespace detail

inline void render_field_panel(const space::grid::grid_model& grid) {

    // ── Persistent camera state ──────────────────────────────────────────
    static float cam_yaw   =  0.785f;
    static float cam_pitch =  0.524f;
    static float cam_zoom  =  1.0f;
    static bool  dragging  =  false;

    ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::color::bg_void);
    ImGui::BeginChild("##field_view", {0.0f, 0.0f}, false,
                       ImGuiWindowFlags_NoScrollbar |
                       ImGuiWindowFlags_NoScrollWithMouse);

    // ── Input ────────────────────────────────────────────────────────────
    if (ImGui::IsWindowHovered()) {
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) dragging = true;
        if (float w = ImGui::GetIO().MouseWheel; w != 0.0f) {
            cam_zoom *= std::pow(1.12f, w);
            cam_zoom  = std::clamp(cam_zoom, 0.1f, 20.0f);
        }
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) dragging = false;
    if (dragging) {
        const ImVec2 d = ImGui::GetIO().MouseDelta;
        cam_yaw   -= d.x * 0.008f;
        cam_pitch += d.y * 0.005f;
        cam_pitch  = std::clamp(cam_pitch, 0.05f, 1.55f);
    }

    // ── Geometry setup ───────────────────────────────────────────────────
    ImDrawList* dl  = ImGui::GetWindowDrawList();
    const ImVec2 p  = ImGui::GetWindowPos();
    const ImVec2 sz = ImGui::GetWindowSize();

    dl->AddRectFilled(p, {p.x + sz.x, p.y + sz.y}, IM_COL32(4, 4, 14, 255));

    // Screen-space Tron grid — brighter than before
    {
        const float  step = 40.0f;
        const ImU32  mn   = IM_COL32(0,  60,  95, 90);
        const ImU32  mj   = IM_COL32(0, 120, 180, 65);
        int xi = 0;
        for (float x = 0.0f; x < sz.x; x += step, ++xi) {
            bool major = xi % 5 == 0;
            dl->AddLine({p.x+x, p.y}, {p.x+x, p.y+sz.y}, major?mj:mn, major?0.9f:0.45f);
        }
        int yi = 0;
        for (float y = 0.0f; y < sz.y; y += step, ++yi) {
            bool major = yi % 5 == 0;
            dl->AddLine({p.x, p.y+y}, {p.x+sz.x, p.y+y}, major?mj:mn, major?0.9f:0.45f);
        }
    }

    const float nx = float(grid.grid.x > 0 ? grid.grid.x : 1);
    const float ny = float(grid.grid.y > 0 ? grid.grid.y : 1);
    const float nz = float(grid.grid.z > 0 ? grid.grid.z : 1);

    const float margin    = 28.0f;
    const float hud_h     = 58.0f;
    const float pw        = sz.x - margin * 2.0f;
    const float ph        = sz.y - margin * 2.0f - hud_h;
    const float half_diag = 0.5f * std::sqrt(nx*nx + ny*ny + nz*nz);
    const float base_s    = (pw > 0.0f && ph > 0.0f && half_diag > 0.0f)
                          ? std::min(pw, ph) * 0.42f / half_diag : 4.0f;
    const float s         = base_s * cam_zoom;

    const float panel_cx = p.x + sz.x * 0.5f;
    const float panel_cy = p.y + (sz.y - hud_h) * 0.5f;

    const float sin_yaw   = std::sin(cam_yaw);
    const float cos_yaw   = std::cos(cam_yaw);
    const float sin_pitch = std::sin(cam_pitch);
    const float cos_pitch = std::cos(cam_pitch);

    auto project = [&](float xi, float yi, float zi) -> ImVec2 {
        float gx = xi - nx * 0.5f;
        float gy = yi - ny * 0.5f;
        float gz = zi - nz * 0.5f;
        float rx  =  gx * cos_yaw + gz * sin_yaw;
        float ry  =  gy;
        float rz  = -gx * sin_yaw + gz * cos_yaw;
        float rx2 = rx;
        float ry2 = ry * cos_pitch - rz * sin_pitch;
        return { panel_cx + rx2 * s,
                 panel_cy - ry2 * s };
    };

    const float cam_dx = sin_yaw * cos_pitch;
    const float cam_dy = sin_pitch;
    const float cam_dz = cos_yaw * cos_pitch;

    // ── Bounding-box wireframe — brighter than before ────────────────────
    {
        const ImU32 dim    = IM_COL32(0, 130, 190, 100);
        const ImU32 bright = IM_COL32(0, 210, 255, 170);
        auto edge = [&](float x0,float y0,float z0,float x1,float y1,float z1,ImU32 c){
            dl->AddLine(project(x0,y0,z0), project(x1,y1,z1), c, 1.0f);
        };
        edge(0, 0,  0,  nx, 0,  0,  dim);    edge(nx,0,  0,  nx, 0,  nz, dim);
        edge(nx,0,  nz, 0,  0,  nz, dim);    edge(0, 0,  nz, 0,  0,  0,  dim);
        edge(0, ny, 0,  nx, ny, 0,  bright);  edge(nx,ny, 0,  nx, ny, nz, bright);
        edge(nx,ny, nz, 0,  ny, nz, bright);  edge(0, ny, nz, 0,  ny, 0,  bright);
        edge(0, 0,  0,  0,  ny, 0,  dim);    edge(nx,0,  0,  nx, ny, 0,  dim);
        edge(nx,0,  nz, nx, ny, nz, dim);    edge(0, 0,  nz, 0,  ny, nz, dim);
    }

    // ── Floor grid at Y = 0 ──────────────────────────────────────────────
    // Drawn before voxels so objects sit visibly on top of it.
    {
        // Subtle fill to define the floor plane
        {
            ImVec2 q[4] = { project(0,0,0), project(nx,0,0),
                            project(nx,0,nz), project(0,0,nz) };
            dl->AddConvexPolyFilled(q, 4, IM_COL32(0, 50, 90, 22));
        }

        // Grid lines — step chosen to give ~8 divisions across the shorter axis
        const float raw_step = std::min(nx, nz) / 8.0f;
        const float step = raw_step < 1.0f ? 1.0f : std::floor(raw_step);

        const ImU32 minor_col = IM_COL32(0, 110, 170, 65);
        const ImU32 major_col = IM_COL32(0, 180, 240, 100);

        int xi = 0;
        for (float x = 0.0f; x <= nx + 0.001f; x += step, ++xi) {
            bool major = (xi % 4 == 0);
            dl->AddLine(project(x, 0, 0), project(x, 0, nz),
                        major ? major_col : minor_col, major ? 1.0f : 0.55f);
        }
        int zi = 0;
        for (float z = 0.0f; z <= nz + 0.001f; z += step, ++zi) {
            bool major = (zi % 4 == 0);
            dl->AddLine(project(0, 0, z), project(nx, 0, z),
                        major ? major_col : minor_col, major ? 1.0f : 0.55f);
        }

        // Bright perimeter outline
        const ImU32 rim = IM_COL32(0, 200, 255, 130);
        dl->AddLine(project(0,  0, 0),  project(nx, 0, 0),  rim, 1.2f);
        dl->AddLine(project(nx, 0, 0),  project(nx, 0, nz), rim, 1.2f);
        dl->AddLine(project(nx, 0, nz), project(0,  0, nz), rim, 1.2f);
        dl->AddLine(project(0,  0, nz), project(0,  0, 0),  rim, 1.2f);
    }

    // ── Axis indicators ───────────────────────────────────────────────────
    {
        const float al  = std::max(std::min({nx, ny, nz}) * 0.18f, 2.5f);
        ImVec2 org = project(0.0f, 0.0f, 0.0f);
        auto axis = [&](float dx, float dy, float dz, ImU32 col, const char* lbl) {
            ImVec2 tip = project(dx*al, dy*al, dz*al);
            dl->AddLine(org, tip, col, 1.5f);
            dl->AddCircleFilled(tip, 2.5f, col);
            float ox = tip.x > org.x ? 4.0f : (tip.x < org.x ? -10.0f : -4.0f);
            float oy = tip.y < org.y ? -14.0f : 4.0f;
            dl->AddText({tip.x+ox, tip.y+oy}, col, lbl);
        };
        axis(1,0,0, IM_COL32(  0, 220, 255, 220), "X");
        axis(0,1,0, IM_COL32(255, 160,   0, 220), "Y");
        axis(0,0,1, IM_COL32(180,   0, 255, 220), "Z");
    }

    // ── Voxel rendering (face-culled, depth-sorted) ───────────────────────
    {
        // Half-Lambert shading: brt = base + range * (dot + 1) / 2
        // Maps dot∈[-1,1] to brt∈[base, base+range], so no face ever goes black.
        // face order: 0=+X 1=-X 2=+Y 3=-Y 4=+Z 5=-Z
        constexpr float lx = 0.486f, ly = 0.811f, lz = 0.325f; // normalize(0.6,1.0,0.4)
        constexpr float base = 0.20f, range = 0.80f;
        auto hl = [&](float dot) { return base + range * (dot + 1.0f) * 0.5f; };
        const float brt[6] = { hl( lx), hl(-lx), hl( ly), hl(-ly), hl( lz), hl(-lz) };
        // typical values: +Y≈0.92, +X≈0.79, +Z≈0.73, -Z≈0.47, -X≈0.41, -Y≈0.28

        const int dx_vis = cam_dx >= 0.0f ? +1 : -1;
        const int dy_vis = cam_dy >= 0.0f ? +1 : -1;
        const int dz_vis = cam_dz >= 0.0f ? +1 : -1;

        const float xf_off = dx_vis > 0 ? 1.0f : 0.0f;
        const float yf_off = dy_vis > 0 ? 1.0f : 0.0f;
        const float zf_off = dz_vis > 0 ? 1.0f : 0.0f;

        const int bx_idx = dx_vis > 0 ? 0 : 1;
        const int by_idx = dy_vis > 0 ? 2 : 3;
        const int bz_idx = dz_vis > 0 ? 4 : 5;

        struct Voxel {
            float xi, yi, zi, depth;
            ImU32 col;
            bool sx, sy, sz;
        };

        static std::vector<Voxel> voxels;
        voxels.clear();

        static const ImU32 palette[] = {
            IM_COL32(  0, 220, 255, 230),
            IM_COL32(255, 120,   0, 230),
            IM_COL32(180,   0, 255, 230),
            IM_COL32(  0, 255, 140, 230),
            IM_COL32(255, 220,   0, 230),
            IM_COL32(255,   0, 140, 230),
        };

        int oi = 0;
        for (const auto& obj : grid.objects) {
            const ImU32 col = palette[oi % 6];

            // Only in-bounds voxels enter the neighbour set.
            // Out-of-bounds voxels must not suppress the exposed face at the grid edge.
            std::unordered_set<space::utility::cell_point> vol;
            vol.reserve(obj.volume.size());
            for (const auto& kv : obj.volume) {
                const auto& cp = kv.first;
                if (cp.x < grid.grid.x && cp.y < grid.grid.y && cp.z < grid.grid.z)
                    vol.insert(cp);
            }

            auto filled = [&](int x, int y, int z) -> bool {
                if (x < 0 || y < 0 || z < 0) return false;
                return vol.count({uint16_t(x), uint16_t(y), uint16_t(z)}) > 0;
            };

            for (const auto& kv : obj.volume) {
                const auto& cp = kv.first;
                const int ix = cp.x, iy = cp.y, iz = cp.z;

                // Skip voxels outside the grid
                if (ix >= grid.grid.x || iy >= grid.grid.y || iz >= grid.grid.z)
                    continue;

                const bool sx = !filled(ix + dx_vis, iy, iz);
                const bool sy = !filled(ix, iy + dy_vis, iz);
                const bool sz = !filled(ix, iy, iz + dz_vis);
                if (!sx && !sy && !sz) continue;

                const float depth = (ix+0.5f)*cam_dx + (iy+0.5f)*cam_dy + (iz+0.5f)*cam_dz;
                voxels.push_back({ float(ix), float(iy), float(iz), depth, col, sx, sy, sz });
            }
            ++oi;
        }

        std::sort(voxels.begin(), voxels.end(),
                  [](const Voxel& a, const Voxel& b){ return a.depth < b.depth; });

        for (const auto& v : voxels) {
            const float x0=v.xi, x1=v.xi+1.0f;
            const float y0=v.yi, y1=v.yi+1.0f;
            const float z0=v.zi, z1=v.zi+1.0f;
            const float xf=v.xi+xf_off, yf=v.yi+yf_off, zf=v.zi+zf_off;

            // Sort the three faces darkest-first within each voxel
            const float fb[3] = {
                v.sx ? brt[bx_idx] : 2.0f,
                v.sz ? brt[bz_idx] : 2.0f,
                v.sy ? brt[by_idx] : 2.0f,
            };
            int ord[3] = {0,1,2};
            if (fb[ord[0]]>fb[ord[1]]) std::swap(ord[0],ord[1]);
            if (fb[ord[1]]>fb[ord[2]]) std::swap(ord[1],ord[2]);
            if (fb[ord[0]]>fb[ord[1]]) std::swap(ord[0],ord[1]);

            for (int fi = 0; fi < 3; ++fi) {
                switch (ord[fi]) {
                case 0: if (v.sx) {
                    ImVec2 f[4]={ project(xf,y0,z0),project(xf,y1,z0),
                                  project(xf,y1,z1),project(xf,y0,z1) };
                    dl->AddConvexPolyFilled(f,4,detail::shade(v.col,brt[bx_idx]));
                } break;
                case 1: if (v.sz) {
                    ImVec2 f[4]={ project(x0,y0,zf),project(x1,y0,zf),
                                  project(x1,y1,zf),project(x0,y1,zf) };
                    dl->AddConvexPolyFilled(f,4,detail::shade(v.col,brt[bz_idx]));
                } break;
                case 2: if (v.sy) {
                    ImVec2 f[4]={ project(x0,yf,z0),project(x1,yf,z0),
                                  project(x1,yf,z1),project(x0,yf,z1) };
                    dl->AddConvexPolyFilled(f,4,detail::shade(v.col,brt[by_idx]));
                } break;
                }
            }
        }

        if (grid.objects.empty()) {
            const ImVec2 ctr = {p.x+sz.x*0.5f, p.y+sz.y*0.5f};
            auto centred = [&](const char* txt, ImU32 col, float dy) {
                ImVec2 ts = ImGui::CalcTextSize(txt);
                dl->AddText({ctr.x-ts.x*0.5f, ctr.y+dy}, col, txt);
            };
            centred("ADD OBJECTS IN THE SIDEBAR", IM_COL32(0,110,160,90), -8.0f);
        }
    }

    // ── HUD ──────────────────────────────────────────────────────────────
    {
        char buf[128];
        const float lx = p.x + sz.x * 0.5f;
        const float ly = p.y + sz.y - hud_h + 6.0f;

        std::snprintf(buf, sizeof(buf), "GRID  %d \xc3\x97 %d \xc3\x97 %d   %.1f mm/cell",
                      grid.grid.x, grid.grid.y, grid.grid.z, grid.cell_size * 1000.0);
        ImVec2 ts = ImGui::CalcTextSize(buf);
        dl->AddText({lx-ts.x*0.5f, ly},       IM_COL32(0,140,190,160), buf);

        std::snprintf(buf, sizeof(buf), "%zu objects  \xc2\xb7  %zu stamped voxels",
                      grid.objects.size(), grid.voxels.size());
        ts = ImGui::CalcTextSize(buf);
        dl->AddText({lx-ts.x*0.5f, ly+18.0f}, IM_COL32(0,80,110,120), buf);

        const char* hint = "drag to orbit  \xc2\xb7  scroll to zoom";
        ts = ImGui::CalcTextSize(hint);
        dl->AddText({lx-ts.x*0.5f, ly+36.0f}, IM_COL32(0,55,80,70), hint);
    }

    dl->AddRect(p, {p.x+sz.x, p.y+sz.y}, IM_COL32(0,140,200,60), 0.0f, 0, 1.5f);

    ImGui::EndChild();
    ImGui::PopStyleColor();
}

} // namespace gui::panels
