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

class Module {
public:
    std::string name;
    CategoryType category;
    bool enabled;
    bool expanded; // For settings

    Module(std::string name, CategoryType category) : name(name), category(category), enabled(false), expanded(false) {}
    virtual ~Module() {}

    void Toggle() { 
        enabled = !enabled; 
        OnToggle(); 
    }
    
    virtual void OnToggle() {}
    
    // Draw settings in the expanded view
    virtual void RenderSettings() {
        ImGui::Text("No settings available.");
    }

    virtual void SaveConfig(std::ostream& stream) {
        stream << "Enabled=" << (enabled ? "1" : "0") << "\n";
    }

    virtual void LoadConfig(const std::map<std::string, std::string>& config) {
        if (config.count("Enabled")) {
            enabled = config.at("Enabled") == "1";
        }
    }
};
