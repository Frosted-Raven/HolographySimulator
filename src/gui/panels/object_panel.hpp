#pragma once

#include "gui/theme.hpp"
#include "space/grid.hpp"
#include "space/medium.hpp"
#include "space/object/object.hpp"
#include "space/object/object_descriptor.hpp"
#include "world.hpp"

#include <imgui.h>
#include <cstdio>
#include <cstring>
#include <functional>
#include <optional>
#include <string>
#include <variant>

namespace gui::panels {

inline void render_object_panel(const space::grid::grid_model& model,
                                 const std::function<void(world::actions)>& dispatch)
{
    theme::section_header("OBJECTS");

    if (theme::accent_button("+ SPHERE", {-1.0f, 0.0f}))
        dispatch(space::grid::actions{space::grid::action::new_sphere{}});
    if (theme::accent_button("+ CUBE",   {-1.0f, 0.0f}))
        dispatch(space::grid::actions{space::grid::action::new_cube{}});

    ImGui::Spacing();

    if (model.objects.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, theme::color::text_dim);
        ImGui::Text("  no objects");
        ImGui::PopStyleColor();
        return;
    }

    // Dispatch helpers — keep call sites concise
    auto dispatch_obj = [&](int idx, space::object::actions a) {
        dispatch(space::grid::actions{
            space::grid::action::object_action{idx, std::move(a)}});
    };
    auto dispatch_shape = [&](int idx, space::object::shapes::actions a) {
        dispatch_obj(idx, space::object::actions{std::move(a)});
    };
    auto dispatch_medium = [&](int idx, space::medium::actions a) {
        dispatch_obj(idx, space::object::actions{std::move(a)});
    };

    int i = 0;
    for (const auto& obj : model.objects) {
        ImGui::PushID(i);

        const bool is_sphere = std::holds_alternative<
            space::object::shapes::sphere_model>(obj.shape);

        // ── Row header ────────────────────────────────────────────────────
        ImGui::PushStyleColor(ImGuiCol_Text, theme::color::cyan_bright);
        ImGui::Text("%s", is_sphere ? "[SPH]" : "[CUB]");
        ImGui::PopStyleColor();

        ImGui::SameLine();
        std::string display = obj.name.empty()
            ? ("OBJ_" + std::to_string(i)) : obj.name;
        ImGui::PushStyleColor(ImGuiCol_Text, theme::color::text_bright);
        ImGui::Text("%s", display.c_str());
        ImGui::PopStyleColor();

        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, theme::color::text_dim);
        ImGui::Text("p%d", obj.object_prio);
        ImGui::PopStyleColor();

        // ── Collapsible detail ────────────────────────────────────────────
        const std::string node_id = "##obj" + std::to_string(i);
        if (ImGui::TreeNode(node_id.c_str(), "  details")) {
            ImGui::Indent(8.0f);

            // ── SHAPE ─────────────────────────────────────────────────────
            theme::label("SHAPE");

            // World position (read from variant)
            const auto wp = std::visit(
                [](const auto& s) { return s.world_position; }, obj.shape);

            float pos[3] = { float(wp.x), float(wp.y), float(wp.z) };
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::DragFloat3("##wp", pos, 0.001f, -10.0f, 10.0f, "POS  %.3f m"))
                dispatch_shape(i, space::object::shapes::action::edit_position{
                    {double(pos[0]), double(pos[1]), double(pos[2])}});

            if (is_sphere) {
                const auto& sph = std::get<space::object::shapes::sphere_model>(obj.shape);
                float r = float(sph.scale);
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::DragFloat("##sr", &r, 0.001f, 0.001f, 1.0f, "R  %.4f m"))
                    dispatch_shape(i, space::object::shapes::action::edit_sphere{double(r)});
            } else {
                const auto& cub = std::get<space::object::shapes::cube_model>(obj.shape);

                float sc[3] = { float(cub.scale.i), float(cub.scale.j), float(cub.scale.k) };
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::DragFloat3("##csc", sc, 0.001f, 0.001f, 2.0f, "SZ  %.3f m"))
                    dispatch_shape(i, space::object::shapes::action::edit_cube{
                        space::utility::vector3{double(sc[0]), double(sc[1]), double(sc[2])},
                        std::nullopt});

                float rot[3] = { float(cub.rotation.i), float(cub.rotation.j), float(cub.rotation.k) };
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::DragFloat3("##crot", rot, 0.01f, -3.14159f, 3.14159f, "ROT  %.2f rad"))
                    dispatch_shape(i, space::object::shapes::action::edit_cube{
                        std::nullopt,
                        space::utility::vector3{double(rot[0]), double(rot[1]), double(rot[2])}});
            }

            // ── GRID PLACEMENT ────────────────────────────────────────────
            ImGui::Spacing();
            theme::label("GRID OFFSET  (cells)");

            int tx = obj.transform_mod.x;
            int ty = obj.transform_mod.y;
            int tz = obj.transform_mod.z;
            bool offset_dirty = false;

            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::DragInt("##tx", &tx, 1.0f, 0, 4095, "X  %d")) offset_dirty = true;
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::DragInt("##ty", &ty, 1.0f, 0, 4095, "Y  %d")) offset_dirty = true;
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::DragInt("##tz", &tz, 1.0f, 0, 4095, "Z  %d")) offset_dirty = true;

            if (offset_dirty)
                dispatch_obj(i, space::object::actions{
                    space::object::action::transform_mod{
                        space::utility::cell_point{
                            static_cast<uint16_t>(tx),
                            static_cast<uint16_t>(ty),
                            static_cast<uint16_t>(tz)}}});

            ImGui::Spacing();
            if (theme::accent_button("REGEN SDF + VOL", {-1.0f, 0.0f}))
                dispatch(space::grid::actions{space::grid::action::update_object_sdf{i}});
            if (theme::accent_button("REGEN VOL ONLY",  {-1.0f, 0.0f}))
                dispatch(space::grid::actions{space::grid::action::update_object_volume{i}});

            // ── MEDIUM ────────────────────────────────────────────────────
            ImGui::Spacing();
            theme::label("MEDIUM");

            const auto& med = obj.medium;

            // Name
            char name_buf[64] = {};
            std::strncpy(name_buf, med.name.c_str(), sizeof(name_buf) - 1);
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputText("##mname", name_buf, sizeof(name_buf));
            if (ImGui::IsItemDeactivatedAfterEdit())
                dispatch_medium(i, space::medium::action::name{std::string(name_buf)});

            // Scalar properties
            auto med_drag = [&](const char* id, const char* fmt,
                                 float cur, float speed, float lo, float hi,
                                 auto make_action) {
                float fv = cur;
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::DragFloat(id, &fv, speed, lo, hi, fmt))
                    dispatch_medium(i, make_action(double(fv)));
            };

            med_drag("##mden", "DENS  %.2f kg/m\xc2\xb3",
                     float(med.density),    0.5f,   0.0f, 20000.0f,
                     [](double v){ return space::medium::action::density{v}; });

            med_drag("##mabs", "ABS  %.4f",
                     float(med.absorption), 0.001f, 0.0f,     1.0f,
                     [](double v){ return space::medium::action::absorption{v}; });

            med_drag("##mtmp", "TEMP  %.1f \xc2\xb0" "C",
                     float(med.temperature), 0.1f, -100.0f, 2000.0f,
                     [](double v){ return space::medium::action::temperature{v}; });

            med_drag("##mstf", "STIF  %.0f Pa",
                     float(med.stiffness),  500.0f, 0.0f, 1.0e9f,
                     [](double v){ return space::medium::action::stiffness{v}; });

            // Computed read-only
            ImGui::PushStyleColor(ImGuiCol_Text, theme::color::text_dim);
            ImGui::Text("  speed  %.1f m/s   Z  %.0f Pa\xc2\xb7s/m",
                        med.sound_speed, med.acoustic_impedance);
            ImGui::PopStyleColor();

            bool rigid = med.is_rigid;
            if (ImGui::Checkbox("RIGID BODY", &rigid))
                dispatch_medium(i, space::medium::action::rigid{rigid});

            // ── STATS ─────────────────────────────────────────────────────
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text, theme::color::text_dim);
            ImGui::Text("sdf %zu  \xc2\xb7  vol %zu voxels",
                        obj.sdf_data.distance.size(), obj.volume.size());
            ImGui::PopStyleColor();

            ImGui::Unindent(8.0f);
            ImGui::TreePop();
        }

        ImGui::PushStyleColor(ImGuiCol_Separator, theme::color::border_dim);
        ImGui::Separator();
        ImGui::PopStyleColor();

        ImGui::PopID();
        ++i;
    }
}

} // namespace gui::panels
