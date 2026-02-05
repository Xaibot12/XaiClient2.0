#pragma once
#include <d3d11.h>
#include <string>
#include <map>

class TextureManager {
public:
    static TextureManager& Instance();
    void Initialize(ID3D11Device* device);
    ID3D11ShaderResourceView* GetTexture(const std::string& itemId);
    ID3D11ShaderResourceView* GetBlockTexture(const std::string& blockId);

private:
    ID3D11Device* device = nullptr;
    std::map<std::string, ID3D11ShaderResourceView*> textureCache;
    std::map<std::string, ID3D11ShaderResourceView*> blockTextureCache;
    ID3D11ShaderResourceView* LoadTextureFromFile(const std::string& path);
};
