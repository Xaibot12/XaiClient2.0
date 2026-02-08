#include "ESP.h"
#include "../network.h"
#include "../utils/IconLoader.h"
#include "../utils/DataLists.h"
#include "../imgui.h"
#include <sstream>
#include <cstring>

ESP::ESP(NetworkClient* netInstance) : Module("ESP", CategoryType::Render), net(netInstance) {
    enabled = true;
}

// Helper to format name for display (snake_case -> Title Case)
// Duplicate from BlockESP.cpp, could be moved to utils
std::string FormatEntityName(std::string name) {
    std::string out = "";
    bool capitalize = true;
    for (char c : name) {
        if (c == '_') {
            out += ' ';
            capitalize = true;
        } else {
            if (capitalize) {
                out += toupper(c);
                capitalize = false;
            } else {
                out += c;
            }
        }
    }
    return out;
}

void ESP::RenderSettings() {
    bool changed = false;
    if (ImGui::Checkbox("Mobs", &showGeneric)) changed = true;
    ImGui::SameLine();
    ImGui::ColorEdit3("##GenericColor", genericColor, ImGuiColorEditFlags_NoInputs);
    
    if (ImGui::Checkbox("Show All Entities (Items, etc.)", &showAllEntities)) changed = true;

    ImGui::Separator();
    ImGui::Text("Specific Mobs");
    
    // Search & Filter
    ImGui::InputText("Search", searchFilter, IM_ARRAYSIZE(searchFilter));
    ImGui::SameLine();
    ImGui::Checkbox("Show Selected", &onlyShowSelected);

    // Initialize cache if needed
    if (!cacheInitialized) {
        availableEntities.clear();
        entityIcons.clear();
        for (const auto& id : DataLists::Entities) {
            // Check for spawn egg
            std::string egg = id + "_spawn_egg";
            if (IconLoader::Get().HasIcon(egg)) {
                availableEntities.push_back(id);
                entityIcons[id] = "item:" + egg;
            } else {
                // Check if it's an item directly or use barrier
                // For now, always include but use barrier if no egg?
                // User said: "if there is none, just display a barrier item"
                availableEntities.push_back(id);
                entityIcons[id] = "item:barrier"; 
            }
        }
        std::sort(availableEntities.begin(), availableEntities.end());
        cacheInitialized = true;
    }

    float windowVisibleX2 = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
    ImGuiStyle& style = ImGui::GetStyle();
    
    bool openColorPicker = false;

    for (const auto& entityId : availableEntities) {
        // Format Name (e.g. "zombie" -> "Zombie")
        std::string displayName = FormatEntityName(entityId);
        
        bool isEnabled = specificMobs.count(displayName) > 0;
        
        // Filter: Show Selected
        if (onlyShowSelected && !isEnabled) continue;

        // Filter: Search
        if (strlen(searchFilter) > 0) {
            std::string s = searchFilter;
            std::string b = displayName;
            std::transform(s.begin(), s.end(), s.begin(), ::tolower);
            std::transform(b.begin(), b.end(), b.begin(), ::tolower);
            if (b.find(s) == std::string::npos) continue;
        }

        ImGui::PushID(entityId.c_str());
        
        // Icon
        std::string iconVal = entityIcons[entityId];
        std::string cleanId = iconVal.substr(5);
        ID3D11ShaderResourceView* tex = IconLoader::Get().GetTexture(cleanId);
        
        if (isEnabled) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.8f, 0.2f, 0.5f));
        }

        if (ImGui::ImageButton("##btn", tex, ImVec2(32, 32))) {
            // Toggle
            if (isEnabled) {
                specificMobs.erase(displayName);
            } else {
                // Add with default color (Green? Or Yellow?)
                specificMobs[displayName] = { 0.0f, 1.0f, 0.0f };
            }
            changed = true;
        }
        
        if (isEnabled) {
            ImGui::PopStyleColor();
        }

        // Right Click: Color Picker
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            if (isEnabled) { // Only pick color if enabled
                editingMob = displayName;
                openColorPicker = true;
            }
        }

        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(displayName.c_str());
        }

        float last_button_x2 = ImGui::GetItemRectMax().x;
        float next_button_x2 = last_button_x2 + style.ItemSpacing.x + 32 + style.FramePadding.x * 2;
        if (next_button_x2 < windowVisibleX2)
            ImGui::SameLine();
            
        ImGui::PopID();
    }
    
    if (openColorPicker) {
        ImGui::OpenPopup("ColorPickerPopup");
    }

    if (ImGui::BeginPopup("ColorPickerPopup")) {
        ImGui::Text("Color for %s", editingMob.c_str());
        // Need a temp array for color picker since map vector is float vector
        if (specificMobs.count(editingMob)) {
            float* col = specificMobs[editingMob].data();
            if (ImGui::ColorPicker3("Color", col)) {
                changed = true;
            }
        }
        ImGui::EndPopup();
    }
    
    if (changed) SendUpdate();
}

void ESP::OnToggle() {
    SendUpdate();
}

void ESP::SendUpdate() {
    if (net) {
        net->SendESPSettings(showGeneric, showAllEntities, specificMobs);
    }
}

float* ESP::GetColor(const std::string& name) {
    if (!enabled) return nullptr;
    if (specificMobs.count(name)) return specificMobs[name].data();
    return (showGeneric || showAllEntities) ? genericColor : nullptr;
}

void ESP::SaveConfig(std::ostream& stream) {
    Module::SaveConfig(stream);
    stream << "ShowGeneric=" << showGeneric << "\n";
    stream << "ShowAllEntities=" << showAllEntities << "\n";
    stream << "GenericColor=" << genericColor[0] << " " << genericColor[1] << " " << genericColor[2] << "\n";
    
    stream << "SpecificCount=" << specificMobs.size() << "\n";
    int i = 0;
    for (const auto& kv : specificMobs) {
        stream << "Specific_Name_" << i << "=" << kv.first << "\n";
        stream << "Specific_Color_" << i << "=" << kv.second[0] << " " << kv.second[1] << " " << kv.second[2] << "\n";
        i++;
    }
}

void ESP::LoadConfig(const std::map<std::string, std::string>& config) {
    Module::LoadConfig(config);
    if (config.count("ShowGeneric")) showGeneric = config.at("ShowGeneric") == "1";
    if (config.count("ShowAllEntities")) showAllEntities = config.at("ShowAllEntities") == "1";
    if (config.count("GenericColor")) {
        std::stringstream ss(config.at("GenericColor"));
        ss >> genericColor[0] >> genericColor[1] >> genericColor[2];
    }
    
    if (config.count("SpecificCount")) {
        int count = std::stoi(config.at("SpecificCount"));
        specificMobs.clear();
        for (int i = 0; i < count; i++) {
            std::string nameKey = "Specific_Name_" + std::to_string(i);
            std::string colorKey = "Specific_Color_" + std::to_string(i);
            if (config.count(nameKey) && config.count(colorKey)) {
                std::string name = config.at(nameKey);
                std::stringstream ss(config.at(colorKey));
                float r, g, b;
                ss >> r >> g >> b;
                specificMobs[name] = { r, g, b };
            }
        }
    }
}
