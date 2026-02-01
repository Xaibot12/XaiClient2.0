#pragma once
#include "../Module.h"

class Disable : public Module {
public:
    bool fully = false;

    Disable() : Module("Disable", CategoryType::Settings) {}

    void RenderSettings() override {
        ImGui::Checkbox("Fully", &fully);
    }
};
