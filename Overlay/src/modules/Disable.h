#pragma once
#include "../Module.h"

class Disable : public Module {
public:
    bool fully = false;

    Disable() : Module("Disable", CategoryType::Settings) {}

    void RenderSettings() override {
        ImGui::Checkbox("Fully", &fully);
    }

    void SaveConfig(std::ostream& stream) override {
        // Force Enabled=0 so we don't self-destruct on startup
        stream << "Enabled=0\n";
        stream << "Fully=" << (fully ? "1" : "0") << "\n";
    }

    void LoadConfig(const std::map<std::string, std::string>& config) override {
        if (config.count("Fully")) {
            fully = config.at("Fully") == "1";
        }
    }
};
