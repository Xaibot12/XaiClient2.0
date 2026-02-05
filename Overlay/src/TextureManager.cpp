#include "TextureManager.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include <filesystem>

TextureManager& TextureManager::Instance() {
    static TextureManager instance;
    return instance;
}

void TextureManager::Initialize(ID3D11Device* device) {
    this->device = device;
}

ID3D11ShaderResourceView* TextureManager::GetTexture(const std::string& itemId) {
    if (textureCache.find(itemId) != textureCache.end()) {
        return textureCache[itemId];
    }

    // Base path to textures
    // Using the path provided by user, but adapting to potential relative path if needed.
    // Assuming the user runs the overlay from the project root or similar.
    // For reliability in development, using the absolute path provided.
    std::string basePath = "C:/Users/Tobi/Documents/GitHub/XaiClient2.0.0/Overlay/assets/MinecraftTexturePack/assets/minecraft/textures/item/";
    std::string fullPath = basePath + itemId + ".png";

    // Check if file exists to avoid unnecessary load attempts
    // std::filesystem::exists requires C++17
    
    ID3D11ShaderResourceView* srv = LoadTextureFromFile(fullPath);
    // Cache even if null to avoid retrying failed loads? 
    // Maybe better to cache nulls too.
    textureCache[itemId] = srv;
    
    return srv;
}

ID3D11ShaderResourceView* TextureManager::GetBlockTexture(const std::string& blockId) {
    if (blockTextureCache.find(blockId) != blockTextureCache.end()) {
        return blockTextureCache[blockId];
    }

    std::string basePath = "C:/Users/Tobi/Documents/GitHub/XaiClient2.0.0/Overlay/assets/MinecraftTexturePack/assets/minecraft/textures/block/";
    std::string fullPath = basePath + blockId + ".png";

    ID3D11ShaderResourceView* srv = LoadTextureFromFile(fullPath);
    blockTextureCache[blockId] = srv;
    
    return srv;
}

ID3D11ShaderResourceView* TextureManager::LoadTextureFromFile(const std::string& path) {
    if (!device) return nullptr;

    int width, height, channels;
    unsigned char* data = stbi_load(path.c_str(), &width, &height, &channels, 4); // Force RGBA
    if (!data) return nullptr;

    // Create Texture
    D3D11_TEXTURE2D_DESC desc;
    ZeroMemory(&desc, sizeof(desc));
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = 0;
    desc.MiscFlags = 0;

    D3D11_SUBRESOURCE_DATA initData;
    ZeroMemory(&initData, sizeof(initData));
    initData.pSysMem = data;
    initData.SysMemPitch = width * 4;

    ID3D11Texture2D* texture = nullptr;
    HRESULT hr = device->CreateTexture2D(&desc, &initData, &texture);
    stbi_image_free(data);

    if (FAILED(hr)) return nullptr;

    // Create SRV
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
    ZeroMemory(&srvDesc, sizeof(srvDesc));
    srvDesc.Format = desc.Format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = 1;

    ID3D11ShaderResourceView* srv = nullptr;
    hr = device->CreateShaderResourceView(texture, &srvDesc, &srv);
    texture->Release();

    if (FAILED(hr)) return nullptr;

    return srv;
}
