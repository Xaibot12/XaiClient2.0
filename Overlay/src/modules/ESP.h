#pragma once
#include "../Module.h"

#include <sstream>

class ESP : public Module {
public:
    float color[3] = { 1.0f, 0.0f, 0.0f };

    ESP() : Module("ESP", CategoryType::Render) {
        enabled = true;
    }

    void RenderSettings() override {
        ImGui::ColorEdit3("Color", color);
    }

    void SaveConfig(std::ostream& stream) override {
        Module::SaveConfig(stream);
        stream << "Color=" << color[0] << " " << color[1] << " " << color[2] << "\n";
    }

    void LoadConfig(const std::map<std::string, std::string>& config) override {
        Module::LoadConfig(config);
        if (config.count("Color")) {
            std::stringstream ss(config.at("Color"));
            ss >> color[0] >> color[1] >> color[2];
        }
    }
};
