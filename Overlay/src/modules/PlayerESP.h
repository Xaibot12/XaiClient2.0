#pragma once
#include "../Module.h"
#include "Friends.h"
#include "../utils/IconLoader.h"
#include <unordered_map>
#include <string>
#include <vector>
#include <algorithm>

class PlayerESP : public Module {
public:
    Friends* friendsModule;

    bool showGeneric = true;
    float genericColor[3] = { 1.0f, 0.0f, 0.0f }; // Red

    bool showFriends = true;
    float friendColor[3] = { 0.0f, 1.0f, 0.0f }; // Green

    // Specific players
    std::unordered_map<std::string, std::vector<float>> specificPlayers; 
    
    char searchFilter[64] = "";
    std::string editingPlayer = "";
    bool showColorPicker = false;

    PlayerESP(Friends* friends) : Module("PlayerESP", CategoryType::Render), friendsModule(friends) {}

    void RenderSettings() override {
        ImGui::Checkbox("Generic", &showGeneric);
        ImGui::SameLine();
        ImGui::ColorEdit3("##GenericColor", genericColor, ImGuiColorEditFlags_NoInputs);
        
        ImGui::Checkbox("Friends", &showFriends);
        ImGui::SameLine();
        ImGui::ColorEdit3("##FriendColor", friendColor, ImGuiColorEditFlags_NoInputs);

        ImGui::Separator();
        ImGui::Text("Specific Players");
        
        ImGui::InputText("Search/Add", searchFilter, sizeof(searchFilter));

        float windowVisibleX2 = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
        ImGuiStyle& style = ImGui::GetStyle();
        bool openColorPicker = false;

        // 1. Suggestion from Search
        std::string s = searchFilter;
        // Trim
        s.erase(0, s.find_first_not_of(" \t\n\r\f\v"));
        s.erase(s.find_last_not_of(" \t\n\r\f\v") + 1);

        if (!s.empty() && specificPlayers.find(s) == specificPlayers.end()) {
                 // Show "Add [Name]" item
                 // Use name_tag icon as player_head is missing in some packs
                 ID3D11ShaderResourceView* tex = IconLoader::Get().GetTexture("name_tag"); 
                 
                 ImGui::PushID("AddBtn");
                 if (ImGui::ImageButton("##add", tex, ImVec2(32, 32))) {
                 specificPlayers[s] = { 1.0f, 1.0f, 0.0f }; // Default Yellow
                 memset(searchFilter, 0, sizeof(searchFilter)); 
             }
             if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add %s", s.c_str());
             
             float last_button_x2 = ImGui::GetItemRectMax().x;
             float next_button_x2 = last_button_x2 + style.ItemSpacing.x + 32 + style.FramePadding.x * 2;
             if (next_button_x2 < windowVisibleX2) ImGui::SameLine();
             ImGui::PopID();
        }

        // 2. Existing Players
        for (auto it = specificPlayers.begin(); it != specificPlayers.end(); ) {
            std::string name = it->first;
            
            // Search Filter
            bool match = true;
            if (!s.empty()) {
                std::string b = name;
                std::string s_lower = s;
                std::transform(s_lower.begin(), s_lower.end(), s_lower.begin(), ::tolower);
                std::transform(b.begin(), b.end(), b.begin(), ::tolower);
                if (b.find(s_lower) == std::string::npos) match = false;
            }

            if (!match) {
                ++it;
                continue;
            }
 
             ImGui::PushID(name.c_str());
             ID3D11ShaderResourceView* tex = IconLoader::Get().GetTexture("name_tag");
 
             // Selected style (Always selected in this list)
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.8f, 0.2f, 0.5f));
            
            if (ImGui::ImageButton("##btn", tex, ImVec2(32, 32))) {
                // Remove
                it = specificPlayers.erase(it);
                ImGui::PopStyleColor();
                ImGui::PopID();
                continue; // Next iteration
            }
            ImGui::PopStyleColor();

            // Right Click Color
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                editingPlayer = name;
                openColorPicker = true;
            }
            
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", name.c_str());

            float last_button_x2 = ImGui::GetItemRectMax().x;
            float next_button_x2 = last_button_x2 + style.ItemSpacing.x + 32 + style.FramePadding.x * 2;
            if (next_button_x2 < windowVisibleX2)
                ImGui::SameLine();
            
            ImGui::PopID();
            ++it;
        }

        if (openColorPicker) ImGui::OpenPopup("PlayerColorPicker");
        if (ImGui::BeginPopup("PlayerColorPicker")) {
             ImGui::Text("Color for %s", editingPlayer.c_str());
             if (specificPlayers.count(editingPlayer)) {
                 ImGui::ColorPicker3("Color", specificPlayers[editingPlayer].data());
             }
             ImGui::EndPopup();
        }
    }

    float* GetColor(const std::string& name) {
        if (!enabled) return nullptr;

        // Check Specific
        auto it = specificPlayers.find(name);
        if (it != specificPlayers.end()) {
            return it->second.data();
        }
        // Check Friends
        if (friendsModule && friendsModule->friendList.count(name)) {
            return showFriends ? friendColor : nullptr;
        }
        // Generic
        return showGeneric ? genericColor : nullptr;
    }

    void SaveConfig(std::ostream& stream) override {
        Module::SaveConfig(stream);
        stream << "ShowGeneric=" << showGeneric << "\n";
        stream << "GenericColor=" << genericColor[0] << " " << genericColor[1] << " " << genericColor[2] << "\n";
        stream << "ShowFriends=" << showFriends << "\n";
        stream << "FriendColor=" << friendColor[0] << " " << friendColor[1] << " " << friendColor[2] << "\n";
        
        stream << "SpecificCount=" << specificPlayers.size() << "\n";
        int i = 0;
        for (const auto& kv : specificPlayers) {
            stream << "Specific_Name_" << i << "=" << kv.first << "\n";
            stream << "Specific_Color_" << i << "=" << kv.second[0] << " " << kv.second[1] << " " << kv.second[2] << "\n";
            i++;
        }
    }

    void LoadConfig(const std::map<std::string, std::string>& config) override {
        Module::LoadConfig(config);
        if (config.count("ShowGeneric")) showGeneric = config.at("ShowGeneric") == "1";
        if (config.count("GenericColor")) {
            std::stringstream ss(config.at("GenericColor"));
            ss >> genericColor[0] >> genericColor[1] >> genericColor[2];
        }
        if (config.count("ShowFriends")) showFriends = config.at("ShowFriends") == "1";
        if (config.count("FriendColor")) {
            std::stringstream ss(config.at("FriendColor"));
            ss >> friendColor[0] >> friendColor[1] >> friendColor[2];
        }
        
        if (config.count("SpecificCount")) {
            int count = std::stoi(config.at("SpecificCount"));
            specificPlayers.clear();
            for (int i = 0; i < count; i++) {
                std::string nameKey = "Specific_Name_" + std::to_string(i);
                std::string colorKey = "Specific_Color_" + std::to_string(i);
                if (config.count(nameKey) && config.count(colorKey)) {
                    std::string name = config.at(nameKey);
                    std::stringstream ss(config.at(colorKey));
                    float r, g, b;
                    ss >> r >> g >> b;
                    specificPlayers[name] = { r, g, b };
                }
            }
        }
    }
};
