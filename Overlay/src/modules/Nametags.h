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

    void Render(const Entity& entity, float screenX, float screenY) {
        if (!enabled) return;

        // Calculate Distance
        float dist = sqrt(entity.x * entity.x + entity.y * entity.y + entity.z * entity.z);

        // Calculate Scale
        // Goal: 50% size at 300 blocks.
        // Linear drop: 1.0 at 0, 0.5 at 300.
        // Formula: Scale = 1.0 - (dist / 600.0)
        // Clamp to minimum 0.5 (or maybe 0.4 to allow further scaling)
        float distScale = 1.0f - (dist / 600.0f);
        if (distScale < 0.5f) distScale = 0.5f;

        // Apply Base Size Setting
        float finalScale = (baseSize / 100.0f) * distScale;

        // Apply Scale to ImGui
        // Note: ImGui font scaling is global or per-window. 
        // We can use ImDrawList::AddText with font size, but default font doesn't support arbitrary sizes well without rebuilding atlas.
        // Better approach: Use SetWindowFontScale if we were in a window, but we are using background draw list.
        // We can just scale the coordinates and sizes we use for drawing.
        
        // Actually, let's just use ImGui::GetFont()->Scale for the draw list? 
        // No, AddText doesn't take a scale parameter unless we use the complex overload.
        // Let's use the complex overload of AddText: AddText(font, font_size, ...)
        
        ImDrawList* draw = ImGui::GetBackgroundDrawList();
        float fontSize = 16.0f * finalScale; // Default approx 16px?
        // Actually, ImGui::GetFontSize() returns current font size.
        float currentFontSize = ImGui::GetFontSize();
        float scaledFontSize = currentFontSize * finalScale;

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
        draw->AddRectFilled(
            ImVec2(screenX - textSize.x / 2 - padding, finalY - textSize.y - padding),
            ImVec2(screenX + textSize.x / 2 + padding, finalY + padding),
            IM_COL32(0, 0, 0, 180) // Slightly more opaque
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
                
                startX += itemSize + spacing;
            }
        }
    }
};
