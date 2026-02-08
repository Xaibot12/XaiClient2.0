#include "IconLoader.h"
#include "../stb_image.h"
#include <algorithm>
#include <iostream>

namespace fs = std::filesystem;

IconLoader& IconLoader::Get() {
    static IconLoader instance;
    return instance;
}

void IconLoader::Initialize(ID3D11Device* dev) {
    this->device = dev;
    availableIcons.clear();
    
    // Path to item textures
    std::string itemPath = "C:/Users/Tobi/Documents/GitHub/XaiClient2.0.0/Overlay/assets/MinecraftTexturePack/assets/minecraft/textures/item";
    std::string blockPath = "C:/Users/Tobi/Documents/GitHub/XaiClient2.0.0/Overlay/assets/MinecraftTexturePack/assets/minecraft/textures/block";

    if (fs::exists(itemPath)) {
        for (const auto& entry : fs::directory_iterator(itemPath)) {
            if (entry.path().extension() == ".png") {
                availableIcons.push_back(entry.path().stem().string());
            }
        }
    } else {
         printf("[IconLoader] Error: Item texture path not found: %s\n", itemPath.c_str());
    }

    // Also scan block textures
    if (fs::exists(blockPath)) {
        for (const auto& entry : fs::directory_iterator(blockPath)) {
            if (entry.path().extension() == ".png") {
                std::string name = entry.path().stem().string();
                // Avoid duplicates (if item has same name as block, item is already added)
                // Since availableIcons is not sorted yet, we use linear search or just sort later and unique
                availableIcons.push_back(name);
            }
        }
    }

    // Sort and Remove duplicates
    std::sort(availableIcons.begin(), availableIcons.end());
    availableIcons.erase(std::unique(availableIcons.begin(), availableIcons.end()), availableIcons.end());
    
    filteredIcons = availableIcons; // Initial filter
}

ID3D11ShaderResourceView* IconLoader::GetTexture(const std::string& name) {
    if (textureCache.count(name)) return textureCache[name];

    // Try loading from Item folder first
    std::string itemPath = "C:/Users/Tobi/Documents/GitHub/XaiClient2.0.0/Overlay/assets/MinecraftTexturePack/assets/minecraft/textures/item/" + name + ".png";
    std::string blockPath = "C:/Users/Tobi/Documents/GitHub/XaiClient2.0.0/Overlay/assets/MinecraftTexturePack/assets/minecraft/textures/block/" + name + ".png";
    
    ID3D11ShaderResourceView* tex = nullptr;
    
    if (fs::exists(itemPath)) {
        tex = LoadTextureFromFile(itemPath);
    }
    
    // Fallback to Block folder with heuristics
    if (!tex) {
         std::vector<std::string> suffixes = { "", "_front", "_side", "_top", "_bottom", "_on", "_off" };
         for (const auto& suffix : suffixes) {
             std::string tryPath = "C:/Users/Tobi/Documents/GitHub/XaiClient2.0.0/Overlay/assets/MinecraftTexturePack/assets/minecraft/textures/block/" + name + suffix + ".png";
             if (fs::exists(tryPath)) {
                 tex = LoadTextureFromFile(tryPath);
                 if (tex) break;
             }
         }
    }
    
    if (tex) {
        textureCache[name] = tex;
    }
    return tex;
}

bool IconLoader::HasIcon(const std::string& name) {
    return std::binary_search(availableIcons.begin(), availableIcons.end(), name);
}

ID3D11ShaderResourceView* IconLoader::LoadTextureFromFile(const std::string& path) {
    if (!device) return nullptr;

    int w, h, channels;
    unsigned char* data = stbi_load(path.c_str(), &w, &h, &channels, 4);
    if (!data) return nullptr;

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = w;
    desc.Height = h;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = 0;

    D3D11_SUBRESOURCE_DATA subResource = {};
    subResource.pSysMem = data;
    subResource.SysMemPitch = w * 4;

    ID3D11Texture2D* pTexture = nullptr;
    device->CreateTexture2D(&desc, &subResource, &pTexture);
    
    stbi_image_free(data);

    if (!pTexture) return nullptr;

    ID3D11ShaderResourceView* pSRV = nullptr;
    device->CreateShaderResourceView(pTexture, nullptr, &pSRV);
    pTexture->Release();

    return pSRV;
}

bool IconLoader::ItemSelector(const char* label, std::string& currentItem, const std::vector<std::string>* items, bool isEntityList) {
    bool changed = false;
    
    ImGui::Text("%s", label);
    ImGui::InputText("Search", searchBuffer, sizeof(searchBuffer));
    
    // Determine source list
    const std::vector<std::string>& sourceList = items ? *items : availableIcons;

    // Filter
    std::vector<std::string> localFiltered;
    std::string query = searchBuffer;
    std::transform(query.begin(), query.end(), query.begin(), ::tolower);
    
    if (query.empty()) {
        localFiltered = sourceList;
    } else {
        for (const auto& name : sourceList) {
            std::string lowerName = name;
            std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
            if (lowerName.find(query) != std::string::npos) {
                localFiltered.push_back(name);
            }
        }
    }

    // Grid View
    float iconSize = 32.0f;
    float padding = 4.0f;
    float windowVisibleX = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
    
    ImGui::BeginChild((std::string(label) + "_Grid").c_str(), ImVec2(0, 200), true);
    
    for (size_t i = 0; i < localFiltered.size(); i++) {
        const std::string& itemName = localFiltered[i];
        std::string textureName = itemName;

        if (isEntityList) {
            std::string egg = itemName + "_spawn_egg";
            if (std::binary_search(availableIcons.begin(), availableIcons.end(), egg)) {
                textureName = egg;
            } else {
                textureName = "barrier";
            }
        }
        
        ImGui::PushID((int)i);
        
        ID3D11ShaderResourceView* tex = GetTexture(textureName);
        
        // If texture is null (e.g. block has no item texture), skip or show text?
        // User said: "Make sure that only items that are actually blocks are shown"
        // If we are iterating DataLists::Blocks, and one doesn't have a texture, we probably shouldn't show it or show placeholder.
        // Let's show text if no texture.

        if (tex) {
            if (ImGui::ImageButton(itemName.c_str(), (void*)tex, ImVec2(iconSize, iconSize))) {
                currentItem = itemName;
                changed = true;
            }
        } else {
            // Fallback for missing textures
            if (ImGui::Button(itemName.c_str(), ImVec2(iconSize, iconSize))) {
                 currentItem = itemName;
                 changed = true;
            }
        }
        
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", itemName.c_str());
        }

        float lastButtonX = ImGui::GetItemRectMax().x;
        float nextButtonX = lastButtonX + padding + iconSize;
        if (i + 1 < localFiltered.size() && nextButtonX < windowVisibleX) {
            ImGui::SameLine();
        }
        
        ImGui::PopID();
    }
    
    ImGui::EndChild();
    
    if (!currentItem.empty()) {
        ImGui::Text("Selected: %s", currentItem.c_str());
        
        std::string textureName = currentItem;
        if (isEntityList) {
            std::string egg = currentItem + "_spawn_egg";
            if (std::binary_search(availableIcons.begin(), availableIcons.end(), egg)) {
                textureName = egg;
            } else {
                textureName = "barrier";
            }
        }
        
        ID3D11ShaderResourceView* tex = GetTexture(textureName);
        if (tex) {
            ImGui::SameLine();
            ImGui::Image((void*)tex, ImVec2(16, 16));
        }
    }
    
    return changed;
}
