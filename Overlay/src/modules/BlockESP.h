#pragma once
#include "../Module.h"
#include "../TextureManager.h"
#include "../network.h"
#include "../MathUtils.h"
#include <map>
#include <vector>
#include <string>
#include <filesystem>
#include <algorithm>

#include <tuple>

struct BlockConfig {
    bool enabled = false;
    float color[3] = { 0.0f, 0.0f, 0.0f }; // Default Black, will be set on first toggle
    bool colorInitialized = false;
};

struct Edge {
    float x1, y1, z1;
    float x2, y2, z2;
};

struct Face {
    float x1, y1, z1;
    float x2, y2, z2;
    float x3, y3, z3;
    float x4, y4, z4;
};

struct CachedChunk {
    // Changed from std::string to uint16_t (Palette ID) to save memory
    // 0 is reserved for "Air/Unknown"
    std::map<int, uint16_t> blocks; // Local Index (0-4095) -> PaletteID
    std::vector<std::pair<ImU32, Edge>> edges;
    std::vector<std::pair<ImU32, Face>> faces;
};

class BlockESP : public Module {
public:
    std::map<std::string, BlockConfig> blocks;
    std::map<std::string, std::string> blockIcons; // Maps clean BlockID -> Texture Filename
    std::vector<std::string> availableBlocks;
    
    // Palette System
    std::vector<std::string> globalPalette; // ID -> Name. Index 0 is "air"
    std::map<std::string, uint16_t> globalPaletteMap; // Name -> ID
    
    // World State
    std::map<std::tuple<int, int, int>, CachedChunk> chunkMap;

    NetworkClient* net;
    
    // UI State
    char searchFilter[128] = "";
    std::string editingBlock = ""; // Which block is currently being color picked
    bool showColorPicker = false;
    int renderRange = 64;

    BlockESP(NetworkClient* netInstance);
    
    void RenderSettings() override;
    void OnToggle() override;
    void SaveConfig(std::ostream& stream) override;
    void LoadConfig(const std::map<std::string, std::string>& config) override;
    
    void SendUpdate(); // Made public for main.cpp to call on connect

    void RemoveBlocks(const std::string& blockId); // Helper to remove specific blocks

    void Render(const GameData& data, float screenW, float screenH, ImDrawList* draw);

    
private:
    void LoadAvailableBlocks();
    void RebuildAllChunks();
    // void SendUpdate(); // Moved to public
    void ProcessUpdates(const std::vector<BlockUpdate>& updates, long long& outUpdateTime, long long& outRebuildTime);
    void UpdateChunk(std::tuple<int, int, int> chunkPos);
    std::tuple<int, int, int> GetChunkPos(int x, int y, int z);
    
    // Palette Helpers
    uint16_t GetBlockID(const std::string& name);
    std::string GetBlockName(uint16_t id);
    uint16_t GetBlock(int x, int y, int z); // Returns ID instead of string
};
