#pragma once
#include "../Module.h"

class Nametags : public Module {
public:
    Nametags() : Module("Nametags", CategoryType::Render) {}

    void RenderSettings() override {
        ImGui::Text("No settings for Nametags yet.");
    }
};
