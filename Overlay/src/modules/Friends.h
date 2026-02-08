#pragma once
#include "../Module.h"
#include "../network.h"
#include <unordered_set>
#include <vector>
#include <string>
#include <algorithm>
#include <sstream>
#include <Windows.h>

class Friends : public Module {
public:
    std::unordered_set<std::string> friendList;
    bool middleClickFriends = false;
    char inputBuf[64] = "";
    bool wasMiddleDown = false;

    Friends() : Module("Friends", CategoryType::Settings) {}

    void RenderSettings() override {
        ImGui::Checkbox("Middle-Click Friends", &middleClickFriends);
        ImGui::Separator();
        
        ImGui::Text("Add Friend:");
        ImGui::InputText("##friendinput", inputBuf, sizeof(inputBuf));
        ImGui::SameLine();
        if (ImGui::Button("Add") && strlen(inputBuf) > 0) {
            std::string name = inputBuf;
            // Trim whitespace
            name.erase(0, name.find_first_not_of(" \t\n\r\f\v"));
            name.erase(name.find_last_not_of(" \t\n\r\f\v") + 1);
            
            if (!name.empty()) {
                friendList.insert(name);
                memset(inputBuf, 0, sizeof(inputBuf));
            }
        }
        
        ImGui::Separator();
        ImGui::Text("Friend List:");
        for (auto it = friendList.begin(); it != friendList.end(); ) {
            ImGui::Text("%s", it->c_str());
            ImGui::SameLine();
            std::string btnLabel = "Delete##" + *it;
            if (ImGui::Button(btnLabel.c_str())) {
                it = friendList.erase(it);
            } else {
                ++it;
            }
        }
    }

    void Update(const GameData* data) override {
        if (!middleClickFriends) return;
        
        bool isMiddleDown = (GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0;
        
        if (isMiddleDown && !wasMiddleDown) {
            // Clicked
            if (data && data->targetedEntityId != -1) {
                for (const auto& e : data->entities) {
                    if (e.id == data->targetedEntityId) {
                        ToggleFriend(e.name);
                        break;
                    }
                }
            }
        }
        
        wasMiddleDown = isMiddleDown;
    }

    void ToggleFriend(const std::string& name) {
        if (friendList.count(name)) {
            friendList.erase(name);
        } else {
            friendList.insert(name);
        }
    }

    void SaveConfig(std::ostream& stream) override {
        Module::SaveConfig(stream);
        stream << "MiddleClickFriends=" << (middleClickFriends ? "1" : "0") << "\n";
        
        stream << "FriendsList=";
        int i = 0;
        for (const auto& name : friendList) {
            stream << name;
            if (i < friendList.size() - 1) stream << ",";
            i++;
        }
        stream << "\n";
    }

    void LoadConfig(const std::map<std::string, std::string>& config) override {
        Module::LoadConfig(config);
        if (config.count("MiddleClickFriends")) middleClickFriends = config.at("MiddleClickFriends") == "1";
        
        if (config.count("FriendsList")) {
            friendList.clear();
            std::string line = config.at("FriendsList");
            std::stringstream ss(line);
            std::string name;
            while (std::getline(ss, name, ',')) {
                if (!name.empty()) friendList.insert(name);
            }
        }
    }
};
