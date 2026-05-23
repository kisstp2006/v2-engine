#include "engine.h"
#include "editor_style.h"

namespace editor {

namespace {

constexpr ImVec4 kAccent       = ImVec4(0.29f, 0.62f, 1.00f, 1.00f);  // #4a9eff
constexpr ImVec4 kAccentSoft   = ImVec4(0.29f, 0.62f, 1.00f, 0.40f);
constexpr ImVec4 kAccentMedium = ImVec4(0.29f, 0.62f, 1.00f, 0.65f);

constexpr ImVec4 kBgWindow     = ImVec4(0.118f, 0.118f, 0.118f, 1.00f);  // #1e1e1e
constexpr ImVec4 kBgPanel      = ImVec4(0.145f, 0.145f, 0.149f, 1.00f);  // #252526
constexpr ImVec4 kBgPanelAlt   = ImVec4(0.180f, 0.180f, 0.180f, 1.00f);  // #2e2e2e
constexpr ImVec4 kBgPopup      = ImVec4(0.118f, 0.118f, 0.118f, 0.96f);

constexpr ImVec4 kText         = ImVec4(0.86f, 0.86f, 0.86f, 1.00f);
constexpr ImVec4 kTextDim      = ImVec4(0.55f, 0.55f, 0.55f, 1.00f);
constexpr ImVec4 kBorder       = ImVec4(0.27f, 0.27f, 0.27f, 1.00f);
constexpr ImVec4 kBorderShadow = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);

}  // namespace

void applyStyle() {
    ImGuiStyle& s = ImGui::GetStyle();

    // ---- Density: kompakt (Unity Inspector) ----
    s.WindowPadding     = ImVec2(6, 6);
    s.FramePadding      = ImVec2(4, 2);
    s.CellPadding       = ImVec2(4, 2);
    s.ItemSpacing       = ImVec2(4, 2);
    s.ItemInnerSpacing  = ImVec2(4, 2);
    s.IndentSpacing     = 16.0f;
    s.ScrollbarSize     = 12.0f;
    s.GrabMinSize       = 8.0f;

    // ---- Sarkok: élesek (0 px) ----
    s.WindowRounding    = 0.0f;
    s.ChildRounding     = 0.0f;
    s.FrameRounding     = 0.0f;
    s.PopupRounding     = 0.0f;
    s.ScrollbarRounding = 0.0f;
    s.GrabRounding      = 0.0f;
    s.TabRounding       = 0.0f;

    // ---- Szegélyek ----
    s.WindowBorderSize  = 1.0f;
    s.ChildBorderSize   = 1.0f;
    s.PopupBorderSize   = 1.0f;
    s.FrameBorderSize   = 0.0f;
    s.TabBorderSize     = 0.0f;

    // ---- Színpaletta (Unity / VS Code dark + #4a9eff accent) ----
    ImVec4* c = s.Colors;

    c[ImGuiCol_Text]                 = kText;
    c[ImGuiCol_TextDisabled]         = kTextDim;
    c[ImGuiCol_TextSelectedBg]       = kAccentSoft;

    c[ImGuiCol_WindowBg]             = kBgWindow;
    c[ImGuiCol_ChildBg]              = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg]              = kBgPopup;
    c[ImGuiCol_MenuBarBg]            = kBgPanel;

    c[ImGuiCol_Border]               = kBorder;
    c[ImGuiCol_BorderShadow]         = kBorderShadow;

    c[ImGuiCol_FrameBg]              = ImVec4(0.18f, 0.18f, 0.18f, 1.00f);
    c[ImGuiCol_FrameBgHovered]       = ImVec4(0.24f, 0.24f, 0.24f, 1.00f);
    c[ImGuiCol_FrameBgActive]        = kAccentSoft;

    c[ImGuiCol_TitleBg]              = kBgPanel;
    c[ImGuiCol_TitleBgActive]        = kBgPanelAlt;
    c[ImGuiCol_TitleBgCollapsed]     = ImVec4(0.05f, 0.05f, 0.05f, 0.95f);

    c[ImGuiCol_ScrollbarBg]          = ImVec4(0.08f, 0.08f, 0.08f, 0.53f);
    c[ImGuiCol_ScrollbarGrab]        = ImVec4(0.31f, 0.31f, 0.31f, 1.00f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.41f, 0.41f, 0.41f, 1.00f);
    c[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.51f, 0.51f, 0.51f, 1.00f);

    c[ImGuiCol_CheckMark]            = kAccent;
    c[ImGuiCol_SliderGrab]           = kAccent;
    c[ImGuiCol_SliderGrabActive]     = ImVec4(0.41f, 0.71f, 1.00f, 1.00f);

    c[ImGuiCol_Button]               = ImVec4(0.21f, 0.21f, 0.22f, 1.00f);
    c[ImGuiCol_ButtonHovered]        = kAccentSoft;
    c[ImGuiCol_ButtonActive]         = kAccentMedium;

    c[ImGuiCol_Header]               = ImVec4(0.18f, 0.18f, 0.18f, 1.00f);
    c[ImGuiCol_HeaderHovered]        = kAccentSoft;
    c[ImGuiCol_HeaderActive]         = kAccentMedium;

    c[ImGuiCol_Separator]            = kBorder;
    c[ImGuiCol_SeparatorHovered]     = kAccentSoft;
    c[ImGuiCol_SeparatorActive]      = kAccentMedium;

    c[ImGuiCol_ResizeGrip]           = ImVec4(0.29f, 0.62f, 1.00f, 0.18f);
    c[ImGuiCol_ResizeGripHovered]    = kAccentSoft;
    c[ImGuiCol_ResizeGripActive]     = kAccentMedium;

    c[ImGuiCol_Tab]                  = kBgPanel;
    c[ImGuiCol_TabHovered]           = kAccentSoft;
    c[ImGuiCol_TabActive]            = kBgWindow;
    c[ImGuiCol_TabUnfocused]         = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
    c[ImGuiCol_TabUnfocusedActive]   = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);

    c[ImGuiCol_DockingPreview]       = kAccentMedium;
    c[ImGuiCol_DockingEmptyBg]       = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);

    c[ImGuiCol_PlotLines]            = kAccent;
    c[ImGuiCol_PlotLinesHovered]     = ImVec4(0.41f, 0.71f, 1.00f, 1.00f);
    c[ImGuiCol_PlotHistogram]        = kAccent;
    c[ImGuiCol_PlotHistogramHovered] = ImVec4(0.41f, 0.71f, 1.00f, 1.00f);

    c[ImGuiCol_TableHeaderBg]        = kBgPanel;
    c[ImGuiCol_TableBorderStrong]    = kBorder;
    c[ImGuiCol_TableBorderLight]     = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
    c[ImGuiCol_TableRowBg]           = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TableRowBgAlt]        = ImVec4(1.00f, 1.00f, 1.00f, 0.04f);

    c[ImGuiCol_DragDropTarget]       = kAccent;
    c[ImGuiCol_NavHighlight]         = kAccent;
    c[ImGuiCol_NavWindowingHighlight]= ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
    c[ImGuiCol_NavWindowingDimBg]    = ImVec4(0.00f, 0.00f, 0.00f, 0.40f);
    c[ImGuiCol_ModalWindowDimBg]     = ImVec4(0.00f, 0.00f, 0.00f, 0.55f);
}

}  // namespace editor
