#pragma once
#include "../Module.h"

class ESP : public Module {
public:
    float color[3] = { 1.0f, 0.0f, 0.0f };

    ESP() : Module("ESP", CategoryType::Render) {
        enabled = true;
    }

    void RenderSettings() override {
        ImGui::ColorEdit3("Color", color);
    }
};
