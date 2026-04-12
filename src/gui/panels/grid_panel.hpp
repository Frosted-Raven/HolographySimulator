#pragma once

#include "gui/theme.hpp"
#include "space/grid.hpp"
#include "world.hpp"

#include <imgui.h>
#include <functional>

namespace gui::panels {

inline void render_grid_panel(const space::grid::grid_model& model,
                               const std::function<void(world::actions)>& dispatch) {
    theme::section_header("GRID");

    // --- Cell size ---
    theme::label("CELL SIZE");
    float cs = static_cast<float>(model.cell_size);
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::DragFloat("##cs", &cs, 0.0001f, 0.0001f, 1.0f, "%.5f m")) {
        dispatch(space::grid::actions{
            space::grid::action::cell_size{static_cast<double>(cs)}});
    }

    ImGui::Spacing();

    // --- Dimensions ---
    theme::label("DIMENSIONS  (cells)");

    int gx = model.grid.x;
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::DragInt("##gx", &gx, 1.0f, 1, 4096, "X  %d")) {
        dispatch(space::grid::actions{
            space::grid::action::grid_dimensions{
                static_cast<uint16_t>(gx), std::nullopt, std::nullopt}});
    }

    int gy = model.grid.y;
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::DragInt("##gy", &gy, 1.0f, 1, 4096, "Y  %d")) {
        dispatch(space::grid::actions{
            space::grid::action::grid_dimensions{
                std::nullopt, static_cast<uint16_t>(gy), std::nullopt}});
    }

    int gz = model.grid.z;
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::DragInt("##gz", &gz, 1.0f, 1, 4096, "Z  %d")) {
        dispatch(space::grid::actions{
            space::grid::action::grid_dimensions{
                std::nullopt, std::nullopt, static_cast<uint16_t>(gz)}});
    }

    ImGui::Spacing();

    // --- Actions ---
    if (theme::accent_button("SORT OBJECTS", {-1.0f, 0.0f})) {
        dispatch(space::grid::actions{space::grid::action::sort_objects{}});
    }
    if (theme::accent_button("STAMP GRID", {-1.0f, 0.0f})) {
        dispatch(space::grid::actions{space::grid::action::update_grid{}});
    }

    // --- Info readout ---
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Separator, theme::color::border_dim);
    ImGui::Separator();
    ImGui::PopStyleColor();
    ImGui::Spacing();

    ImGui::PushStyleColor(ImGuiCol_Text, theme::color::text_dim);
    double px = model.grid.x * model.cell_size;
    double py = model.grid.y * model.cell_size;
    double pz = model.grid.z * model.cell_size;
    ImGui::Text("PHYSICAL   %.3f x %.3f x %.3f m", px, py, pz);
    ImGui::Text("VOXELS     %zu stamped", model.voxels.size());
    ImGui::Text("OBJECTS    %zu", model.objects.size());
    ImGui::PopStyleColor();
}

} // namespace gui::panels
