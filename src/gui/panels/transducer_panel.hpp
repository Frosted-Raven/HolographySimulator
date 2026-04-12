#pragma once

#include "gui/theme.hpp"
#include "transducer/transducer.hpp"
#include "transducer/transducer_array.hpp"
#include "world.hpp"

#include <imgui.h>
#include <immer/flex_vector.hpp>
#include <functional>
#include <string>

namespace gui::panels {

inline void render_transducer_panel(
    const immer::flex_vector<transducer::tran_array::tran_array_model>& arrays,
    const std::function<void(world::actions)>& dispatch)
{
    theme::section_header("TRANSDUCERS");

    if (theme::accent_button("+ ARRAY", {-1.0f, 0.0f})) {
        dispatch(world::actions{world::action::add_array{}});
    }

    ImGui::Spacing();

    if (arrays.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, theme::color::text_dim);
        ImGui::Text("  no arrays");
        ImGui::PopStyleColor();
        return;
    }

    size_t ai = 0;
    for (const auto& arr : arrays) {
        ImGui::PushID(static_cast<int>(ai));

        std::string arr_label = arr.name.has_value()
            ? arr.name.value()
            : ("ARRAY_" + std::to_string(ai));

        // Header row with delete button
        bool open = ImGui::CollapsingHeader(arr_label.c_str());

        ImGui::SameLine();
        float del_w = 52.0f;
        ImGui::SetCursorPosX(ImGui::GetWindowWidth() - del_w - ImGui::GetStyle().WindowPadding.x);
        if (theme::danger_button("DEL", {del_w, 0.0f})) {
            dispatch(world::actions{world::action::remove_array{ai}});
            ImGui::PopID(); // ai
            break; // collection modified, stop iteration
        }

        if (open) {
            ImGui::Indent(8.0f);

            // Array info
            ImGui::PushStyleColor(ImGuiCol_Text, theme::color::text_dim);
            ImGui::Text("%zu transducers", arr.tran_array.size());
            ImGui::PopStyleColor();

            ImGui::Spacing();

            // Add / remove
            if (theme::accent_button("+ TRANSDUCER", {-1.0f, 0.0f})) {
                dispatch(world::actions{world::action::mod_array{
                    ai,
                    transducer::tran_array::actions{
                        transducer::tran_array::action::add_tran{}}}});
            }
            if (!arr.tran_array.empty()) {
                if (theme::danger_button("- LAST", {-1.0f, 0.0f})) {
                    dispatch(world::actions{world::action::mod_array{
                        ai,
                        transducer::tran_array::actions{
                            transducer::tran_array::action::remove_tran{}}}});
                }
            }

            ImGui::Spacing();

            // Group adjust controls — persistent per-array statics
            theme::label("GROUP ADJUST");

            // Use unique IDs per array for statics
            static float s_phase[64] = {};
            static float s_amp[64]   = {1.0f};
            static float s_freq[64]  = {40000.0f};

            size_t idx = ai < 64 ? ai : size_t{0};

            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 58.0f);
            ImGui::DragFloat("##gph", &s_phase[idx], 0.01f,
                             -3.14159f, 3.14159f, "PH  %.3f rad");
            ImGui::SameLine();
            if (theme::accent_button("SET##ph", {50.0f, 0.0f})) {
                dispatch(world::actions{world::action::mod_array{
                    ai,
                    transducer::tran_array::actions{
                        transducer::tran_array::action::group_adjust{
                            transducer::single::actions{
                                transducer::single::action::new_phase{
                                    static_cast<double>(s_phase[idx])}}}}}}); }

            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 58.0f);
            ImGui::DragFloat("##gam", &s_amp[idx], 0.01f,
                             0.0f, 10.0f, "AMP %.3f");
            ImGui::SameLine();
            if (theme::accent_button("SET##am", {50.0f, 0.0f})) {
                dispatch(world::actions{world::action::mod_array{
                    ai,
                    transducer::tran_array::actions{
                        transducer::tran_array::action::group_adjust{
                            transducer::single::actions{
                                transducer::single::action::new_amplitude{
                                    static_cast<double>(s_amp[idx])}}}}}}); }

            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 58.0f);
            ImGui::DragFloat("##gfr", &s_freq[idx], 100.0f,
                             20.0f, 200000.0f, "HZ  %.0f");
            ImGui::SameLine();
            if (theme::accent_button("SET##fr", {50.0f, 0.0f})) {
                dispatch(world::actions{world::action::mod_array{
                    ai,
                    transducer::tran_array::actions{
                        transducer::tran_array::action::group_adjust{
                            transducer::single::actions{
                                transducer::single::action::new_frequency{
                                    static_cast<double>(s_freq[idx])}}}}}}); }

            // Individual transducer list
            if (!arr.tran_array.empty()) {
                ImGui::Spacing();
                theme::label("INDIVIDUAL");

                int ti = 0;
                for (const auto& t : arr.tran_array) {
                    ImGui::PushID(ti); // ti is already int — fine

                    // Active indicator
                    ImGui::PushStyleColor(ImGuiCol_Text,
                        t.is_active ? theme::color::cyan_bright : theme::color::text_dim);
                    ImGui::Text("T%03d", ti);
                    ImGui::PopStyleColor();

                    ImGui::SameLine();
                    ImGui::PushStyleColor(ImGuiCol_Text, theme::color::text_mid);
                    ImGui::Text("ph:%.2f  amp:%.2f  f:%.0f",
                                t.phase, t.amplitude, t.frequency);
                    ImGui::PopStyleColor();

                    ImGui::SameLine();
                    if (ImGui::SmallButton(t.is_active ? "ON " : "OFF")) {
                        dispatch(world::actions{world::action::mod_array{
                            ai,
                            transducer::tran_array::actions{
                                transducer::tran_array::action::single_adjust{
                                    ti,
                                    transducer::single::actions{
                                        transducer::single::action::toggle_active{}}}}}});
                    }

                    ImGui::PopID();
                    ti++;
                }
            }

            ImGui::Unindent(8.0f);
        }

        ImGui::PopID();
        ai++;
    }
}

} // namespace gui::panels
