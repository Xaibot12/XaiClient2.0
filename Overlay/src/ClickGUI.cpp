#include "ClickGUI.h"
#include "imgui.h"

ClickGUI::ClickGUI() {}

ClickGUI::~ClickGUI() {
    for (auto mod : modules) {
        delete mod;
    }
}

void ClickGUI::RegisterModule(Module* module) {
    modules.push_back(module);
}

std::string ClickGUI::GetCategoryName(CategoryType type) {
    switch (type) {
        case CategoryType::Combat: return "Combat";
        case CategoryType::Render: return "Render";
        case CategoryType::Movement: return "Movement";
        case CategoryType::Settings: return "Settings";
        default: return "Unknown";
    }
}

bool ClickGUI::Render() {
    if (!open) return false;

    bool changed = false;

    CategoryType categories[] = {
        CategoryType::Combat,
        CategoryType::Render,
        CategoryType::Movement,
        CategoryType::Settings
    };

    for (auto cat : categories) {
        std::string catName = GetCategoryName(cat);
        
        // Use ImGui::Begin to create a window for each category
        // This makes them drag-and-droppable by default in ImGui
        ImGui::SetNextWindowSize(ImVec2(150, 0), ImGuiCond_FirstUseEver);
        
        // Create the window
        if (ImGui::Begin(catName.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            
            for (auto mod : modules) {
                if (mod->category == cat) {
                    // Render Module as a Selectable (like a list item)
                    // It highlights when 'enabled' is true
                    if (ImGui::Selectable(mod->name.c_str(), mod->enabled)) {
                        mod->Toggle();
                        changed = true;
                    }
                    
                    // Right click to expand/collapse settings
                    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                        mod->expanded = !mod->expanded;
                    }

                    // Render Settings if expanded
                    if (mod->expanded) {
                        ImGui::Indent();
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.8f, 0.8f, 1.0f));
                        mod->RenderSettings();
                        ImGui::PopStyleColor();
                        ImGui::Unindent();
                        // Add a small separator after settings for visual clarity
                        ImGui::Separator();
                    }
                }
            }
        }
        ImGui::End();
    }
    return changed;
}
