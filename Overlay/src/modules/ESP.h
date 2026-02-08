#pragma once
#include "../Module.h"
#include <map>
#include <vector>
#include <string>

class NetworkClient;

class ESP : public Module { // MobESP
public:
    bool showGeneric = true;
    bool showAllEntities = false;
    float genericColor[3] = { 1.0f, 0.0f, 0.0f }; // Red default for mobs

    // Specific Mobs (e.g. "Zombie", "Creeper")
    std::map<std::string, std::vector<float>> specificMobs;
    
    // UI State
    char searchFilter[64] = "";
    bool onlyShowSelected = false;
    std::string editingMob = "";
    bool showColorPicker = false;
    
    // Cache
    std::vector<std::string> availableEntities;
    std::map<std::string, std::string> entityIcons; // Maps ID -> Icon Path
    bool cacheInitialized = false;

    char inputName[64] = "";
    float inputColor[3] = { 1.0f, 1.0f, 0.0f };
    std::string selectedIcon = "";

    NetworkClient* net;

    ESP(NetworkClient* netInstance = nullptr);

    void RenderSettings() override;
    void OnToggle() override;
    float* GetColor(const std::string& name);
    void SaveConfig(std::ostream& stream) override;
    void LoadConfig(const std::map<std::string, std::string>& config) override;

    void SendUpdate();
};
