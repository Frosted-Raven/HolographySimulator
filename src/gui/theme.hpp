#pragma once

#include <imgui.h>

namespace gui::theme {

// ---------------------------------------------------------------------------
// Tron: Legacy colour palette
// ---------------------------------------------------------------------------
namespace color {
    // Backgrounds — deep blue-black
    constexpr ImVec4 bg_void    = {0.02f, 0.02f, 0.06f, 1.00f};
    constexpr ImVec4 bg_dark    = {0.04f, 0.05f, 0.10f, 1.00f};
    constexpr ImVec4 bg_panel   = {0.05f, 0.07f, 0.13f, 1.00f};
    constexpr ImVec4 bg_widget  = {0.07f, 0.10f, 0.17f, 1.00f};

    // Cyan — primary interactive / hero colour
    constexpr ImVec4 cyan_bright = {0.00f, 0.90f, 1.00f, 1.00f};
    constexpr ImVec4 cyan_mid    = {0.00f, 0.62f, 0.80f, 1.00f};
    constexpr ImVec4 cyan_dim    = {0.00f, 0.32f, 0.48f, 1.00f};

    // Orange — antagonist accent, use sparingly (warnings / destructive)
    constexpr ImVec4 orange      = {1.00f, 0.42f, 0.00f, 1.00f};
    constexpr ImVec4 orange_dim  = {0.55f, 0.20f, 0.00f, 1.00f};

    // Text
    constexpr ImVec4 text_bright = {0.85f, 0.97f, 1.00f, 1.00f};
    constexpr ImVec4 text_mid    = {0.48f, 0.70f, 0.80f, 1.00f};
    constexpr ImVec4 text_dim    = {0.25f, 0.38f, 0.48f, 1.00f};

    // Borders
    constexpr ImVec4 border_bright = {0.00f, 0.72f, 1.00f, 0.80f};
    constexpr ImVec4 border_dim    = {0.00f, 0.35f, 0.55f, 0.40f};
}

// ---------------------------------------------------------------------------
// Apply Tron theme to the current ImGui context
// ---------------------------------------------------------------------------
inline void apply() {
    ImGuiStyle& s = ImGui::GetStyle();

    // Geometry — sharp, angular, minimal rounding
    s.WindowRounding    = 0.0f;
    s.ChildRounding     = 0.0f;
    s.FrameRounding     = 2.0f;
    s.ScrollbarRounding = 2.0f;
    s.GrabRounding      = 2.0f;
    s.TabRounding       = 0.0f;
    s.PopupRounding     = 0.0f;

    s.WindowBorderSize  = 1.0f;
    s.ChildBorderSize   = 1.0f;
    s.FrameBorderSize   = 1.0f;
    s.PopupBorderSize   = 1.0f;
    s.TabBorderSize     = 1.0f;

    s.WindowPadding    = {10.0f, 10.0f};
    s.FramePadding     = {6.0f,  4.0f};
    s.ItemSpacing      = {8.0f,  6.0f};
    s.ItemInnerSpacing = {5.0f,  4.0f};
    s.IndentSpacing    = 14.0f;
    s.ScrollbarSize    = 10.0f;
    s.GrabMinSize      = 8.0f;

    ImVec4* c = s.Colors;
    using namespace color;

    c[ImGuiCol_WindowBg]              = bg_dark;
    c[ImGuiCol_ChildBg]               = bg_panel;
    c[ImGuiCol_PopupBg]               = bg_dark;

    c[ImGuiCol_Border]                = border_dim;
    c[ImGuiCol_BorderShadow]          = {0,0,0,0};

    c[ImGuiCol_FrameBg]               = bg_widget;
    c[ImGuiCol_FrameBgHovered]        = {0.04f, 0.14f, 0.24f, 1.0f};
    c[ImGuiCol_FrameBgActive]         = {0.00f, 0.30f, 0.46f, 1.0f};

    c[ImGuiCol_TitleBg]               = bg_void;
    c[ImGuiCol_TitleBgActive]         = bg_void;
    c[ImGuiCol_TitleBgCollapsed]      = bg_void;
    c[ImGuiCol_MenuBarBg]             = bg_void;

    c[ImGuiCol_ScrollbarBg]           = bg_dark;
    c[ImGuiCol_ScrollbarGrab]         = cyan_dim;
    c[ImGuiCol_ScrollbarGrabHovered]  = cyan_mid;
    c[ImGuiCol_ScrollbarGrabActive]   = cyan_bright;

    c[ImGuiCol_CheckMark]             = cyan_bright;
    c[ImGuiCol_SliderGrab]            = cyan_mid;
    c[ImGuiCol_SliderGrabActive]      = cyan_bright;

    c[ImGuiCol_Button]                = {0.00f, 0.32f, 0.48f, 0.60f};
    c[ImGuiCol_ButtonHovered]         = {0.00f, 0.52f, 0.72f, 0.80f};
    c[ImGuiCol_ButtonActive]          = cyan_mid;

    c[ImGuiCol_Header]                = {0.00f, 0.45f, 0.65f, 0.35f};
    c[ImGuiCol_HeaderHovered]         = {0.00f, 0.60f, 0.82f, 0.45f};
    c[ImGuiCol_HeaderActive]          = {0.00f, 0.72f, 0.92f, 0.55f};

    c[ImGuiCol_Separator]             = border_dim;
    c[ImGuiCol_SeparatorHovered]      = border_bright;
    c[ImGuiCol_SeparatorActive]       = cyan_bright;

    c[ImGuiCol_ResizeGrip]            = {0.00f, 0.72f, 1.00f, 0.20f};
    c[ImGuiCol_ResizeGripHovered]     = {0.00f, 0.72f, 1.00f, 0.45f};
    c[ImGuiCol_ResizeGripActive]      = cyan_bright;

    c[ImGuiCol_Tab]                   = {0.03f, 0.18f, 0.28f, 1.00f};
    c[ImGuiCol_TabHovered]            = {0.00f, 0.52f, 0.72f, 0.80f};
    c[ImGuiCol_TabActive]             = {0.00f, 0.42f, 0.62f, 1.00f};
    c[ImGuiCol_TabUnfocused]          = bg_dark;
    c[ImGuiCol_TabUnfocusedActive]    = {0.03f, 0.22f, 0.35f, 1.00f};

    c[ImGuiCol_PlotLines]             = cyan_mid;
    c[ImGuiCol_PlotLinesHovered]      = cyan_bright;
    c[ImGuiCol_PlotHistogram]         = cyan_mid;
    c[ImGuiCol_PlotHistogramHovered]  = cyan_bright;

    c[ImGuiCol_Text]                  = text_bright;
    c[ImGuiCol_TextDisabled]          = text_dim;
    c[ImGuiCol_TextSelectedBg]        = {0.00f, 0.55f, 0.78f, 0.35f};

    c[ImGuiCol_DragDropTarget]        = cyan_bright;
    c[ImGuiCol_NavHighlight]          = cyan_bright;
    c[ImGuiCol_NavWindowingHighlight] = {1,1,1,0.70f};
    c[ImGuiCol_NavWindowingDimBg]     = {0.8f,0.8f,0.8f,0.20f};
    c[ImGuiCol_ModalWindowDimBg]      = {0.02f,0.02f,0.06f,0.60f};
}

// ---------------------------------------------------------------------------
// Reusable styled widgets
// ---------------------------------------------------------------------------

// Cyan section header with divider line
inline void section_header(const char* label) {
    ImGui::PushStyleColor(ImGuiCol_Text, color::cyan_bright);
    ImGui::Text("// %s", label);
    ImGui::PopStyleColor();
    ImGui::PushStyleColor(ImGuiCol_Separator, color::cyan_dim);
    ImGui::Separator();
    ImGui::PopStyleColor();
    ImGui::Spacing();
}

// Primary cyan button
inline bool accent_button(const char* label, ImVec2 size = {0, 0}) {
    ImGui::PushStyleColor(ImGuiCol_Button,        {0.00f, 0.40f, 0.60f, 0.65f});
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.00f, 0.65f, 0.88f, 0.85f});
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  {0.00f, 0.88f, 1.00f, 1.00f});
    ImGui::PushStyleColor(ImGuiCol_Border,        {0.00f, 0.72f, 1.00f, 0.90f});
    bool r = ImGui::Button(label, size);
    ImGui::PopStyleColor(4);
    return r;
}

// Orange destructive button
inline bool danger_button(const char* label, ImVec2 size = {0, 0}) {
    ImGui::PushStyleColor(ImGuiCol_Button,        {0.48f, 0.14f, 0.00f, 0.65f});
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.78f, 0.24f, 0.00f, 0.85f});
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  {1.00f, 0.42f, 0.00f, 1.00f});
    ImGui::PushStyleColor(ImGuiCol_Border,        {1.00f, 0.42f, 0.00f, 0.80f});
    bool r = ImGui::Button(label, size);
    ImGui::PopStyleColor(4);
    return r;
}

// Dim label in all-caps style
inline void label(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, color::text_mid);
    ImGui::Text("%s", text);
    ImGui::PopStyleColor();
}

} // namespace gui::theme
