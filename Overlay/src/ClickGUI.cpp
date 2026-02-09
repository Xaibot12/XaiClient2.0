#include "ClickGUI.h"
#include "imgui.h"
#include <fstream>
#include <sstream>
#include <Windows.h>

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

    // Main ClickGUI Window
    ImGui::SetNextWindowSize(ImVec2(400, 350), ImGuiCond_FirstUseEver);
    
    // Pass nullptr to second argument to remove close button (handled by keybind)
    if (ImGui::Begin("XaiClient", nullptr, ImGuiWindowFlags_NoCollapse)) {
        
        if (ImGui::BeginTabBar("Categories")) {
            
            CategoryType categories[] = {
                CategoryType::Combat,
                CategoryType::Render,
                CategoryType::Movement,
                CategoryType::Settings
            };

            for (auto cat : categories) {
                if (ImGui::BeginTabItem(GetCategoryName(cat).c_str())) {
                    
                    ImGui::Spacing();
                    
                    // Filter modules for this category
                    for (auto mod : modules) {
                        if (mod->category == cat) {
                            ImGui::PushID(mod->name.c_str()); // Unique ID scope

                            // Render Module Checkbox
                            bool enabled = mod->enabled;
                            if (ImGui::Checkbox(mod->name.c_str(), &enabled)) {
                                mod->Toggle();
                                changed = true;
                            }
                            
                            ImGui::SameLine();
                            // Use ArrowButton for settings
                            if (ImGui::ArrowButton("##settings", mod->expanded ? ImGuiDir_Down : ImGuiDir_Right)) {
                                mod->expanded = !mod->expanded;
                            }
                            
                            ImGui::SameLine();
                            
                            // Bind Button
                            std::string bindText = "Bind";
                            if (mod->isBinding) {
                                bindText = "...";
                            } else if (mod->keybind > 0) {
                                char nameBuffer[128];
                                UINT scanCode = MapVirtualKey(mod->keybind, MAPVK_VK_TO_VSC);
                                if (GetKeyNameTextA(scanCode << 16, nameBuffer, 128)) {
                                    bindText = std::string(nameBuffer);
                                } else {
                                    bindText = "Key " + std::to_string(mod->keybind);
                                }
                            }
                            
                            if (ImGui::Button(bindText.c_str())) {
                                mod->isBinding = !mod->isBinding;
                            }
                            
                            if (mod->isBinding) {
                                ImGui::Text("Press any key (Esc to clear)");
                                for (int i = 0x01; i < 0xFE; i++) {
                                    if (i == VK_LBUTTON || i == VK_RBUTTON || i == VK_MBUTTON) continue; // Ignore mouse clicks for safety
                                    
                                    if (GetAsyncKeyState(i) & 0x8000) {
                                        if (i == VK_ESCAPE) {
                                            mod->keybind = 0;
                                            mod->isBinding = false;
                                            changed = true;
                                        } else {
                                            mod->keybind = i;
                                            mod->isBinding = false;
                                            changed = true;
                                        }
                                        break; // Only handle one key
                                    }
                                }
                            }

                            if (mod->expanded) {
                                ImGui::Indent();
                                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.8f, 0.8f, 1.0f));
                                mod->RenderSettings();
                                ImGui::PopStyleColor();
                                ImGui::Unindent();
                                ImGui::Separator();
                            }
                            
                            ImGui::PopID();
                        }
                    }
                    
                    // Settings Category Extras
                    if (cat == CategoryType::Settings) {
                        ImGui::Separator();
                        ImGui::TextDisabled("Configuration");
                        if (ImGui::Button("Save Config", ImVec2(120, 0))) {
                            SaveConfig("config.ini");
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("Load Config", ImVec2(120, 0))) {
                            LoadConfig("config.ini");
                        }
                    }
                    
                    ImGui::EndTabItem();
                }
            }
            ImGui::EndTabBar();
        }
    }
    ImGui::End();

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
