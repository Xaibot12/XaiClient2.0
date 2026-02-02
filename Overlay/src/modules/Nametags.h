#pragma once
#include "../Module.h"
#include "../network.h"
#include "../TextureManager.h"
#include <string>

class Nametags : public Module {
public:
    bool showName = true;
    bool showPing = true;
    bool showHealth = true;
    bool showMaxHealth = true;
    bool showAbsorption = true;
    bool showItems = true;
    bool showEnchants = true;
    bool showDistance = true;
    
    int yOffset = -20;
    int baseSize = 100; // Percent

    Nametags() : Module("Nametags", CategoryType::Render) {}

    void RenderSettings() override {
        ImGui::Checkbox("Name", &showName);
        ImGui::Checkbox("Ping", &showPing);
        ImGui::Checkbox("Health", &showHealth);
        ImGui::Checkbox("Max Health", &showMaxHealth);
        ImGui::Checkbox("Absorption", &showAbsorption);
        ImGui::Checkbox("Items", &showItems);
        ImGui::Checkbox("Enchants", &showEnchants);
        ImGui::Checkbox("Distance", &showDistance);
        
        ImGui::SliderInt("Y Offset", &yOffset, -100, 100);
        ImGui::SliderInt("Size (%)", &baseSize, 50, 200);
    }

    void SaveConfig(std::ostream& stream) override {
        Module::SaveConfig(stream);
        stream << "ShowName=" << showName << "\n";
        stream << "ShowPing=" << showPing << "\n";
        stream << "ShowHealth=" << showHealth << "\n";
        stream << "ShowMaxHealth=" << showMaxHealth << "\n";
        stream << "ShowAbsorption=" << showAbsorption << "\n";
        stream << "ShowItems=" << showItems << "\n";
        stream << "ShowEnchants=" << showEnchants << "\n";
        stream << "ShowDistance=" << showDistance << "\n";
        stream << "YOffset=" << yOffset << "\n";
        stream << "BaseSize=" << baseSize << "\n";
    }

    void LoadConfig(const std::map<std::string, std::string>& config) override {
        Module::LoadConfig(config);
        if (config.count("ShowName")) showName = config.at("ShowName") == "1";
        if (config.count("ShowPing")) showPing = config.at("ShowPing") == "1";
        if (config.count("ShowHealth")) showHealth = config.at("ShowHealth") == "1";
        if (config.count("ShowMaxHealth")) showMaxHealth = config.at("ShowMaxHealth") == "1";
        if (config.count("ShowAbsorption")) showAbsorption = config.at("ShowAbsorption") == "1";
        if (config.count("ShowItems")) showItems = config.at("ShowItems") == "1";
        if (config.count("ShowEnchants")) showEnchants = config.at("ShowEnchants") == "1";
        if (config.count("ShowDistance")) showDistance = config.at("ShowDistance") == "1";
        if (config.count("YOffset")) yOffset = std::stoi(config.at("YOffset"));
        if (config.count("BaseSize")) baseSize = std::stoi(config.at("BaseSize"));
    }

    void Render(const Entity& entity, float screenX, float screenY, float fov) {
        if (!enabled) return;

        // Calculate Distance
        float dist = sqrt(entity.x * entity.x + entity.y * entity.y + entity.z * entity.z);

        // Scaling Logic
        // 1. Distance Projection: "project it to 1/3 of the distance"
        // We use a modified distance for the calculation to make nametags appear closer/larger than they really are.
        // Cap the distance at 6m for scaling purposes (so < 6m acts like 6m)
        float scaleDist = dist;
        if (scaleDist < 6.0f) scaleDist = 6.0f;
        
        float effectiveDist = scaleDist / 3.0f;
        if (effectiveDist < 1.0f) effectiveDist = 1.0f; // Prevent division by zero or huge scales

        // 2. FOV Dependency
        float fovRad = fov * (3.14159f / 180.0f);
        float fovFactor = 1.0f / tan(fovRad / 2.0f);
        
        // Reference FOV (70 degrees) for clamping logic
        // This allows us to apply min/max size constraints based on "Standard Distance" 
        // while allowing Zoom (FOV change) to scale the text BEYOND those limits.
        float refFovRad = 70.0f * (3.14159f / 180.0f);
        float refFovFactor = 1.0f / tan(refFovRad / 2.0f);

        // 3. Calculate Base Scale (at Standard FOV)
        float scaleConstant = 4.0f;
        float baseScale = (scaleConstant * refFovFactor) / effectiveDist;
        
        // Apply User Base Size
        baseScale *= (baseSize / 100.0f);

        // 4. Clamp the Base Scale
        // Ensure readable minimum size and reasonable maximum size at standard FOV
        if (baseScale < 0.5f) baseScale = 0.5f;
        if (baseScale > 3.0f) baseScale = 3.0f;

        // 5. Apply Zoom Factor
        // Calculate how much we are zoomed in relative to standard 70 FOV
        float zoomRatio = fovFactor / refFovFactor;
        
        // Final scale respects the clamps for distance, but reacts fully to zoom
        float finalScale = baseScale * zoomRatio;

        // Apply Scale to ImGui
        ImDrawList* draw = ImGui::GetBackgroundDrawList();
        float scaledFontSize = 16.0f * finalScale; // Default approx 16px

        // Apply Y-Offset (Scaled?) - User probably wants constant screen offset or scaled offset?
        // Usually offset scales with distance to stay "above" head correctly, but fixed offset is easier to tune.
        // Let's stick to fixed offset for now, or maybe scaled offset?
        // If the player is far away, the head is smaller, so a fixed -20 offset might look too high.
        // Let's scale the offset too.
        float scaledOffset = yOffset * finalScale;
        float finalY = screenY + scaledOffset;

        // Build the text
        std::string text = "";
        if (showName) text += entity.name + " ";
        if (showPing) text += std::to_string(entity.ping) + "ms ";
        if (showHealth) {
            text += std::to_string((int)entity.health);
            if (showMaxHealth) text += "/" + std::to_string((int)entity.maxHealth);
            text += " HP";
        }
        if (showAbsorption && entity.absorption > 0) {
            text += " +" + std::to_string((int)entity.absorption) + " Abs";
        }
        if (showDistance) {
            text += " " + std::to_string((int)dist) + "m";
        }

        // Measure text with specific font size
        ImVec2 textSize = ImGui::GetFont()->CalcTextSizeA(scaledFontSize, FLT_MAX, 0.0f, text.c_str());
        
        // Draw Background
        float padding = 4.0f * finalScale;
        // Use (10,10,10) instead of (0,0,0) to avoid ColorKey transparency
        draw->AddRectFilled(
            ImVec2(screenX - textSize.x / 2 - padding, finalY - textSize.y - padding),
            ImVec2(screenX + textSize.x / 2 + padding, finalY + padding),
            IM_COL32(10, 10, 10, 220) 
        );
        // Add Border
        draw->AddRect(
            ImVec2(screenX - textSize.x / 2 - padding, finalY - textSize.y - padding),
            ImVec2(screenX + textSize.x / 2 + padding, finalY + padding),
            IM_COL32(10, 10, 10, 255),
            0.0f,
            0,
            1.0f // Thickness
        );

        // Draw Text
        draw->AddText(
            ImGui::GetFont(),
            scaledFontSize,
            ImVec2(screenX - textSize.x / 2, finalY - textSize.y),
            IM_COL32(255, 255, 255, 255),
            text.c_str()
        );

        // Render Items
        if (showItems) {
            float itemSize = 32.0f * finalScale; 
            float spacing = 2.0f * finalScale;
            
            // Count valid items
            int validItems = 0;
            for (const auto& item : entity.items) if (!item.id.empty()) validItems++;
            if (validItems == 0) return;

            float totalWidth = validItems * itemSize + (validItems - 1) * spacing;
            float startX = screenX - totalWidth / 2;
            // Position items BELOW or ABOVE? 
            // Original code: y - textSize.y - padding - itemSize - 5; (Above text)
            // User said: "items above the nametag"
            // Wait, original code had items *above*?
            // "itemY = y - textSize.y - padding - itemSize - 5;" -> This is ABOVE the text (since Y is up? No, Y is down in ImGui).
            // ImGui: (0,0) is Top-Left. Increasing Y goes DOWN.
            // screenY is the Head position.
            // Text is drawn at (screenX - w/2, screenY - h). So text is ABOVE the head point.
            // ItemY calculation: screenY - textH - padding - itemH - 5. This is ABOVE the text.
            // So Items are on TOP.
            
            float itemY = finalY - textSize.y - padding - itemSize - (5 * finalScale);

            for (const auto& item : entity.items) {
                if (item.id.empty()) continue;

                // Placeholder Box
                draw->AddRectFilled(
                    ImVec2(startX, itemY),
                    ImVec2(startX + itemSize, itemY + itemSize),
                    IM_COL32(50, 50, 50, 200)
                );
                
                // Render Icon from Texture
                ID3D11ShaderResourceView* texture = TextureManager::Instance().GetTexture(item.id);
                if (texture) {
                    draw->AddImage((void*)texture, 
                        ImVec2(startX, itemY), 
                        ImVec2(startX + itemSize, itemY + itemSize));
                } else {
                    // Fallback border
                    draw->AddRect(
                        ImVec2(startX, itemY),
                        ImVec2(startX + itemSize, itemY + itemSize),
                        IM_COL32(200, 200, 200, 255)
                    );
                }

                // Render Enchants
                if (showEnchants) {
                    float enchY = itemY;
                    float enchScale = finalScale * 0.7f; // Smaller than main text
                    float enchFontSize = 16.0f * enchScale; // Approx
                    
                    for (const auto& ench : item.enchants) {
                        std::string enchText = ench.abbr + std::to_string(ench.level);
                        draw->AddText(ImGui::GetFont(), enchFontSize, ImVec2(startX + 1, enchY - enchFontSize), IM_COL32(255, 255, 0, 255), enchText.c_str());
                        enchY -= enchFontSize;
                    }
                }

                // Render Stack Size (Top Right)
                if (item.count > 1) {
                    std::string countText = "x" + std::to_string(item.count);
                    float countScale = finalScale * 0.8f;
                    float countFontSize = 16.0f * countScale;
                    ImVec2 countSize = ImGui::GetFont()->CalcTextSizeA(countFontSize, FLT_MAX, 0.0f, countText.c_str());
                    
                    draw->AddText(
                        ImGui::GetFont(), 
                        countFontSize, 
                        ImVec2(startX + itemSize - countSize.x, itemY - countSize.y/2), 
                        IM_COL32(255, 255, 255, 255), 
                        countText.c_str()
                    );
                }

                // Render Durability (Bottom Center)
                if (item.maxDamage > 0) {
                    int durability = item.maxDamage - item.damage;
                    float durabilityPercent = (float)durability / (float)item.maxDamage;
                    
                    ImU32 duraColor = IM_COL32(255, 255, 255, 255);
                    if (durabilityPercent < 0.2f) duraColor = IM_COL32(255, 50, 50, 255);      // Red
                    else if (durabilityPercent < 0.5f) duraColor = IM_COL32(255, 255, 50, 255); // Yellow
                    else duraColor = IM_COL32(50, 255, 50, 255);                                // Green

                    std::string duraText = std::to_string(durability);
                    float duraScale = finalScale * 0.7f;
                    float duraFontSize = 16.0f * duraScale;
                    ImVec2 duraSize = ImGui::GetFont()->CalcTextSizeA(duraFontSize, FLT_MAX, 0.0f, duraText.c_str());

                    draw->AddText(
                        ImGui::GetFont(),
                        duraFontSize,
                        ImVec2(startX + (itemSize - duraSize.x) / 2, itemY + itemSize - duraSize.y/4),
                        duraColor,
                        duraText.c_str()
                    );
                }
                
                startX += itemSize + spacing;
            }
        }
    }
};
