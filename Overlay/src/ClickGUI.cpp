#include "ClickGUI.h"
#include "imgui.h"
#include <fstream>
#include <sstream>

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

void ClickGUI::SaveConfig(const std::string& path) {
    std::ofstream file(path);
    if (!file.is_open()) return;

    for (auto mod : modules) {
        file << "[" << mod->name << "]" << std::endl;
        mod->SaveConfig(file);
        file << std::endl;
    }
    file.close();
}

void ClickGUI::LoadConfig(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return;

    std::string line;
    std::string currentSection = "";
    std::map<std::string, std::string> currentConfig;

    auto ProcessSection = [&]() {
        if (!currentSection.empty()) {
            for (auto mod : modules) {
                if (mod->name == currentSection) {
                    mod->LoadConfig(currentConfig);
                    break;
                }
            }
        }
        currentConfig.clear();
    };

    while (std::getline(file, line)) {
        if (line.empty()) continue;

        // Trim
        size_t first = line.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) continue;
        size_t last = line.find_last_not_of(" \t\r\n");
        line = line.substr(first, (last - first + 1));

        if (line.front() == '[' && line.back() == ']') {
            ProcessSection();
            currentSection = line.substr(1, line.size() - 2);
        } else {
            size_t eq = line.find('=');
            if (eq != std::string::npos) {
                std::string key = line.substr(0, eq);
                std::string val = line.substr(eq + 1);
                currentConfig[key] = val;
            }
        }
    }
    ProcessSection(); // Process last section
    file.close();
}
