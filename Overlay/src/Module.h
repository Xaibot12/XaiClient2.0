#pragma once
#include <string>
#include <vector>
#include <iostream>
#include <map>
#include "imgui.h"

enum class CategoryType {
    Combat,
    Render,
    Movement,
    Settings
};

struct GameData;

class Module {
public:
    std::string name;
    CategoryType category;
    bool enabled;
    bool expanded; // For settings
    int keybind = 0;
    bool isBinding = false;

    Module(std::string name, CategoryType category) : name(name), category(category), enabled(false), expanded(false) {}
    virtual ~Module() {}

    void Toggle() { 
        enabled = !enabled; 
        OnToggle(); 
    }
    
    virtual void OnToggle() {}

    virtual void Update(const GameData* data) {}
    
    // Draw settings in the expanded view
    virtual void RenderSettings() {
        ImGui::Text("No settings available.");
    }

    virtual void SaveConfig(std::ostream& stream) {
        stream << "Enabled=" << (enabled ? "1" : "0") << "\n";
        stream << "Keybind=" << keybind << "\n";
    }

    virtual void LoadConfig(const std::map<std::string, std::string>& config) {
        if (config.count("Enabled")) {
            enabled = config.at("Enabled") == "1";
        }
        if (config.count("Keybind")) {
            try {
                keybind = std::stoi(config.at("Keybind"));
            } catch (...) { keybind = 0; }
        }
    }
};
