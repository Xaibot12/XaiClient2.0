#pragma once
#include "../network.h"
#include "../Module.h"
#include "../TextureManager.h"
#include "../MathUtils.h"
#include <map>
#include <vector>
#include <string>
#include <filesystem>
#include <algorithm>
#include <tuple>
#include <thread>
#include <mutex>
#include <shared_mutex>
#include <condition_variable>
#include <queue>
#include <memory>
#include <atomic>

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

struct ChunkMesh {
    std::vector<std::pair<ImU32, Edge>> edges;
    std::vector<std::pair<ImU32, Face>> faces;
};

struct CachedChunk {
    // Changed from std::string to uint16_t (Palette ID) to save memory
    // 0 is reserved for "Air/Unknown"
    std::map<int, uint16_t> blocks; // Local Index (0-4095) -> PaletteID
    
    std::shared_ptr<ChunkMesh> mesh;
    mutable std::mutex meshMutex; // Protects access to mesh pointer
    mutable std::mutex blockMutex; // Protects access to blocks map
    
    CachedChunk() : mesh(std::make_shared<ChunkMesh>()) {}
    CachedChunk(const CachedChunk& other) {
        // Copy constructor needed for std::map insertion
        blocks = other.blocks;
        mesh = other.mesh;
        // mutex cannot be copied, so we initialize a new one
    }
};

class BlockESP : public Module {
public:
    std::map<std::string, BlockConfig> blocks;
    std::map<std::string, std::string> blockIcons; // Maps clean BlockID -> Texture Filename
    std::vector<std::string> availableBlocks;
    
    // Palette System
    std::vector<std::string> globalPalette; // ID -> Name. Index 0 is "air"
    std::map<std::string, uint16_t> globalPaletteMap; // Name -> ID
    std::mutex paletteMutex;
    
    // World State
    std::map<std::tuple<int, int, int>, CachedChunk> chunkMap;
    std::shared_mutex chunkMapMutex;

    NetworkClient* net;
    
    // UI State
    char searchFilter[128] = "";
    bool onlyShowSelected = false;
    std::string editingBlock = ""; // Which block is currently being color picked
    bool showColorPicker = false;
    int renderRange = 64;

    // Worker Thread
    std::thread workerThread;
    std::atomic<bool> shouldStop{false};
    std::mutex queueMutex;
    std::condition_variable queueCV;
    std::queue<std::vector<BlockUpdate>> updateQueue;
    std::queue<std::pair<int, int>> unloadQueue;
    std::atomic<bool> clearCacheRequested{false};

    BlockESP(NetworkClient* netInstance);
    ~BlockESP();
    
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
    
    // Internal helpers
    void UpdateChunk(std::tuple<int, int, int> chunkPos);
    std::tuple<int, int, int> GetChunkPos(int x, int y, int z);
    
    // Palette Helpers
    uint16_t GetBlockID(const std::string& name);
    std::string GetBlockName(uint16_t id);
    uint16_t GetBlock(int x, int y, int z); // Returns ID instead of string

    void WorkerLoop();
};
