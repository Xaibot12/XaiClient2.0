#pragma once
#include <d3d11.h>
#include <string>
#include <vector>
#include <map>
#include <filesystem>
#include "../imgui.h"

namespace fs = std::filesystem;

class IconLoader {
public:
    static IconLoader& Get();   void Initialize(ID3D11Device* device);
    ID3D11ShaderResourceView* GetTexture(const std::string& name);
    bool HasIcon(const std::string& name);
    
    // Renders an item selector widget. Returns true if selection changed.
    // If items list is provided, it uses that instead of all available icons.
    // isEntityList: if true, appends "_spawn_egg" to find texture, falls back to "barrier".
    bool ItemSelector(const char* label, std::string& currentItem, const std::vector<std::string>* items = nullptr, bool isEntityList = false);

private:
    ID3D11Device* device = nullptr;
    std::vector<std::string> availableIcons;
    std::vector<std::string> filteredIcons;
    std::map<std::string, ID3D11ShaderResourceView*> textureCache;
    char searchBuffer[64] = "";
    
    // Helper to load texture from file
    ID3D11ShaderResourceView* LoadTextureFromFile(const std::string& path);
};
