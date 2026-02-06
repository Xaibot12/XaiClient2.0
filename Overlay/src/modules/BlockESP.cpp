#include "BlockESP.h"
#include <iostream>
#include <sstream>
#include <chrono>
#include <set>
#include "../stb_image.h"

namespace fs = std::filesystem;

// Hardcoded colors for ores
bool GetHardcodedColor(const std::string& blockId, float* color) {
    // Diamond
    if (blockId == "diamond_ore") {
        color[0] = 0.0f; color[1] = 1.0f; color[2] = 1.0f; return true; // Bright Cyan
    }
    if (blockId == "deepslate_diamond_ore") {
        color[0] = 0.0f; color[1] = 0.7f; color[2] = 0.7f; return true; // Darker Cyan
    }

    // Gold
    if (blockId == "gold_ore") {
        color[0] = 1.0f; color[1] = 0.85f; color[2] = 0.0f; return true; // Bright Gold
    }
    if (blockId == "deepslate_gold_ore" || blockId == "nether_gold_ore") {
        color[0] = 0.8f; color[1] = 0.6f; color[2] = 0.0f; return true; // Darker Gold
    }

    // Emerald
    if (blockId == "emerald_ore") {
        color[0] = 0.0f; color[1] = 1.0f; color[2] = 0.2f; return true; // Bright Green
    }
    if (blockId == "deepslate_emerald_ore") {
        color[0] = 0.0f; color[1] = 0.7f; color[2] = 0.15f; return true; // Darker Green
    }

    // Iron
    if (blockId == "iron_ore") {
        color[0] = 0.9f; color[1] = 0.8f; color[2] = 0.7f; return true; // Beige
    }
    if (blockId == "deepslate_iron_ore") {
        color[0] = 0.7f; color[1] = 0.6f; color[2] = 0.5f; return true; // Darker Beige
    }

    // Lapis
    if (blockId == "lapis_ore") {
        color[0] = 0.1f; color[1] = 0.3f; color[2] = 0.9f; return true; // Blue
    }
    if (blockId == "deepslate_lapis_ore") {
        color[0] = 0.05f; color[1] = 0.2f; color[2] = 0.7f; return true; // Darker Blue
    }

    // Redstone
    if (blockId == "redstone_ore") {
        color[0] = 1.0f; color[1] = 0.0f; color[2] = 0.0f; return true; // Red
    }
    if (blockId == "deepslate_redstone_ore") {
        color[0] = 0.7f; color[1] = 0.0f; color[2] = 0.0f; return true; // Darker Red
    }

    // Coal
    if (blockId == "coal_ore") {
        color[0] = 0.2f; color[1] = 0.2f; color[2] = 0.2f; return true; // Dark Grey
    }
    if (blockId == "deepslate_coal_ore") {
        color[0] = 0.1f; color[1] = 0.1f; color[2] = 0.1f; return true; // Darker Grey
    }
    
    // Copper
    if (blockId == "copper_ore") {
        color[0] = 0.9f; color[1] = 0.5f; color[2] = 0.2f; return true; // Orange
    }
    if (blockId == "deepslate_copper_ore") {
        color[0] = 0.7f; color[1] = 0.3f; color[2] = 0.1f; return true; // Darker Orange
    }

    // Others
    if (blockId == "nether_quartz_ore") {
        color[0] = 0.95f; color[1] = 0.95f; color[2] = 0.95f; return true; // White-ish
    }
    if (blockId == "ancient_debris") {
        color[0] = 0.4f; color[1] = 0.25f; color[2] = 0.2f; return true; // Brown
    }
    
    return false;
}

bool GetAverageColor(const std::string& path, float* color) {
    if (!fs::exists(path)) return false;
    
    int w, h, channels;
    unsigned char* data = stbi_load(path.c_str(), &w, &h, &channels, 4);
    if (!data) return false;

    long long r = 0, g = 0, b = 0, count = 0;
    for (int i = 0; i < w * h; i++) {
        // Skip transparent or near-transparent pixels
        if (data[i * 4 + 3] < 20) continue; 
        
        r += data[i * 4];
        g += data[i * 4 + 1];
        b += data[i * 4 + 2];
        count++;
    }
    
    stbi_image_free(data);
    
    if (count == 0) return false;
    
    color[0] = (float)(r / count) / 255.0f;
    color[1] = (float)(g / count) / 255.0f;
    color[2] = (float)(b / count) / 255.0f;
    return true;
}

BlockESP::BlockESP(NetworkClient* netInstance) : Module("BlockESP", CategoryType::Render), net(netInstance) {
    LoadAvailableBlocks();
    workerThread = std::thread(&BlockESP::WorkerLoop, this);
}

BlockESP::~BlockESP() {
    shouldStop = true;
    queueCV.notify_all();
    if (workerThread.joinable()) {
        workerThread.join();
    }
}

// Helper to strip suffixes
std::string StripSuffix(std::string name) {
    const char* suffixes[] = { "_top", "_bottom", "_side", "_front", "_end", "_on", "_off", "_left", "_right", "_moist", "_inventory" };
    for (const char* suffix : suffixes) {
        if (name.length() > strlen(suffix) && name.substr(name.length() - strlen(suffix)) == suffix) {
            return name.substr(0, name.length() - strlen(suffix));
        }
    }
    return name;
}

// Helper to format name for display (snake_case -> Title Case)
std::string FormatName(std::string name) {
    std::string out = "";
    bool capitalize = true;
    for (char c : name) {
        if (c == '_') {
            out += ' ';
            capitalize = true;
        } else {
            if (capitalize) {
                out += toupper(c);
                capitalize = false;
            } else {
                out += c;
            }
        }
    }
    return out;
}

void BlockESP::LoadAvailableBlocks() {
    std::string path = "C:/Users/Tobi/Documents/GitHub/XaiClient2.0.0/Overlay/assets/MinecraftTexturePack/assets/minecraft/textures/block/";
    if (fs::exists(path)) {
        for (const auto& entry : fs::directory_iterator(path)) {
            if (entry.path().extension() == ".png") {
                std::string filename = entry.path().stem().string();
                
                // Skip debug/meta textures
                if (filename == "missing_tile" || filename.find("debug") != std::string::npos) continue;

                std::string cleanId = StripSuffix(filename);
                
                if (blockIcons.find(cleanId) == blockIcons.end()) {
                    blockIcons[cleanId] = filename;
                    availableBlocks.push_back(cleanId);
                } else {
                    std::string currentIcon = blockIcons[cleanId];
                    if (filename.length() < currentIcon.length()) {
                         blockIcons[cleanId] = filename; 
                    }
                }
            }
        }
    }
    // Sort for easier searching
    std::sort(availableBlocks.begin(), availableBlocks.end());
}

void BlockESP::RenderSettings() {
    ImGui::SliderInt("Render Range (Blocks)", &renderRange, 16, 512);
    ImGui::InputText("Search", searchFilter, IM_ARRAYSIZE(searchFilter));
    
    if (ImGui::Button("Clear Cache")) {
        {
            std::unique_lock<std::shared_mutex> lock(chunkMapMutex);
            chunkMap.clear();
        }
        SendUpdate();
    }

    float windowVisibleX2 = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
    ImGuiStyle& style = ImGui::GetStyle();
    
    int buttons = 0;
    
    bool openColorPicker = false;

    // Base path for icons - duplicated from LoadAvailableBlocks
    std::string basePath = "C:/Users/Tobi/Documents/GitHub/XaiClient2.0.0/Overlay/assets/MinecraftTexturePack/assets/minecraft/textures/block/";

    for (const auto& blockId : availableBlocks) {
        std::string displayName = FormatName(blockId);

        // Filter
        if (strlen(searchFilter) > 0) {
            std::string s = searchFilter;
            std::string b = displayName; // Search by display name
            // Case insensitive search
            std::transform(s.begin(), s.end(), s.begin(), ::tolower);
            std::transform(b.begin(), b.end(), b.begin(), ::tolower);
            if (b.find(s) == std::string::npos) continue;
        }

        ImGui::PushID(blockId.c_str());
        
        // Icon (Use mapped texture)
        ID3D11ShaderResourceView* tex = TextureManager::Instance().GetBlockTexture(blockIcons[blockId]);
        
        bool isEnabled = blocks[blockId].enabled;
        if (isEnabled) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.8f, 0.2f, 0.5f));
        }

        if (ImGui::ImageButton("##btn", tex, ImVec2(32, 32))) {
            // Left Click: Toggle
            blocks[blockId].enabled = !blocks[blockId].enabled;
            if (blocks[blockId].enabled) {
                // Initialize color if needed
                if (!blocks[blockId].colorInitialized) {
                    // 1. Try hardcoded
                    if (!GetHardcodedColor(blockId, blocks[blockId].color)) {
                        // 2. Try average from icon
                        std::string fullPath = basePath + blockIcons[blockId] + ".png";
                        if (!GetAverageColor(fullPath, blocks[blockId].color)) {
                             // 3. Fallback
                             blocks[blockId].color[0] = 0.0f; 
                             blocks[blockId].color[1] = 1.0f; 
                             blocks[blockId].color[2] = 0.0f;
                        }
                    }
                    blocks[blockId].colorInitialized = true;
                }
            } else {
                // Remove this specific block type from local cache immediately
                RemoveBlocks(blockId);
            }
            
            // Update meshes
            RebuildAllChunks();
            SendUpdate();
        }
        
        if (isEnabled) {
            ImGui::PopStyleColor();
        }

        // Right Click: Color Picker
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            editingBlock = blockId;
            openColorPicker = true;
        }

        // Tooltip
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(displayName.c_str());
        }

        float last_button_x2 = ImGui::GetItemRectMax().x;
        float next_button_x2 = last_button_x2 + style.ItemSpacing.x + 32 + style.FramePadding.x * 2;
        if (next_button_x2 < windowVisibleX2)
            ImGui::SameLine();
            
        ImGui::PopID();
    }
    
    if (openColorPicker) {
        ImGui::OpenPopup("ColorPickerPopup");
    }

    if (ImGui::BeginPopup("ColorPickerPopup")) {
        ImGui::Text("Color for %s", FormatName(editingBlock).c_str());
        if (ImGui::ColorPicker3("Color", blocks[editingBlock].color)) {
             blocks[editingBlock].colorInitialized = true; // Mark as initialized/custom
             RebuildAllChunks();
        }
        ImGui::EndPopup();
    }
}

void BlockESP::OnToggle() {
    // Reset local data and force resync from server
    {
        std::unique_lock<std::shared_mutex> lock(chunkMapMutex);
        chunkMap.clear();
    }
    SendUpdate();
}

void BlockESP::RemoveBlocks(const std::string& blockId) {
    uint16_t id = 0;
    {
        std::lock_guard<std::mutex> lock(paletteMutex);
        if (globalPaletteMap.find(blockId) == globalPaletteMap.end()) return;
        id = globalPaletteMap[blockId];
    }

    std::unique_lock<std::shared_mutex> lock(chunkMapMutex);
    for (auto& [key, chunk] : chunkMap) {
        auto it = chunk.blocks.begin();
        while (it != chunk.blocks.end()) {
            if (it->second == id) {
                it = chunk.blocks.erase(it);
            } else {
                ++it;
            }
        }
    }
}

uint16_t BlockESP::GetBlockID(const std::string& name) {
    if (name.empty()) return 0;
    std::lock_guard<std::mutex> lock(paletteMutex);
    if (globalPaletteMap.find(name) == globalPaletteMap.end()) {
        // Add to palette
        globalPalette.push_back(name);
        // IDs are 1-based (0 is reserved for air/empty)
        uint16_t id = (uint16_t)globalPalette.size(); // size is now at least 1
        globalPaletteMap[name] = id;
        return id;
    }
    return globalPaletteMap[name];
}

std::string BlockESP::GetBlockName(uint16_t id) {
    std::lock_guard<std::mutex> lock(paletteMutex);
    if (id == 0 || id > globalPalette.size()) return "";
    return globalPalette[id - 1];
}

uint16_t BlockESP::GetBlock(int x, int y, int z) {
    auto chunkPos = GetChunkPos(x, y, z);
    
    // NOTE: Caller must hold chunkMapMutex (Shared or Unique)
    if (chunkMap.find(chunkPos) == chunkMap.end()) return 0;
    
    // Local coords (0-15)
    int lx = x % 16; if (lx < 0) lx += 16;
    int ly = y % 16; if (ly < 0) ly += 16;
    int lz = z % 16; if (lz < 0) lz += 16;
    
    int index = lx + (ly * 16) + (lz * 256);
    
    const auto& chunk = chunkMap.at(chunkPos);
    
    // Protect block access
    std::lock_guard<std::mutex> lock(chunk.blockMutex);
    const auto& blocks = chunk.blocks;
    
    auto it = blocks.find(index);
    if (it != blocks.end()) {
        return it->second;
    }
    return 0;
}

void BlockESP::RebuildAllChunks() {
    // This is called from GUI thread (RenderSettings) when toggling/coloring.
    // It should trigger rebuilds.
    // Since we are now async, we should probably just mark all chunks dirty or push a special update?
    // But RebuildAllChunks was synchronous.
    // Let's make it trigger async rebuild by pushing an empty update? No.
    // We can just iterate and call UpdateChunk, but UpdateChunk is now thread-safe (mostly).
    // However, UpdateChunk builds mesh.
    // If we call it here, we block the GUI.
    // Better: Iterate all keys and add them to a "Force Rebuild" list?
    // Or just run it synchronously here? It might freeze the UI for a moment.
    // Given the user wants smoothness, we should probably offload this too.
    // But for now, to keep it simple and safe, let's just lock and run it (as it was before, essentially).
    
    std::shared_lock<std::shared_mutex> lock(chunkMapMutex);
    for (auto& [key, chunk] : chunkMap) {
        UpdateChunk(key);
    }
}

void BlockESP::SendUpdate() {
    if (!net) return;
    
    std::vector<std::string> enabledBlocks;
    if (enabled) {
        for (const auto& pair : blocks) {
            if (pair.second.enabled) {
                enabledBlocks.push_back(pair.first);
            }
        }
    }
    
    net->SendBlockList(enabledBlocks);
}

void BlockESP::SaveConfig(std::ostream& stream) {
    Module::SaveConfig(stream);
    stream << "RenderRange=" << renderRange << "\n";
    for (const auto& pair : blocks) {
        // Save if enabled OR if color has been initialized/customized
        if (pair.second.enabled || pair.second.colorInitialized) {
            stream << "Block_" << pair.first << "=" 
                   << (pair.second.enabled ? "1" : "0") << ","
                   << pair.second.color[0] << "," 
                   << pair.second.color[1] << "," 
                   << pair.second.color[2] << "\n";
        }
    }
}

void BlockESP::LoadConfig(const std::map<std::string, std::string>& config) {
    Module::LoadConfig(config);
    
    if (config.count("RenderRange")) {
        renderRange = std::stoi(config.at("RenderRange"));
    }

    for (const auto& pair : config) {
        if (pair.first.rfind("Block_", 0) == 0) { // Starts with "Block_"
            std::string blockId = pair.first.substr(6);
            std::string val = pair.second;
            
            std::stringstream ss(val);
            std::string segment;
            std::vector<std::string> parts;
            while(std::getline(ss, segment, ',')) {
                parts.push_back(segment);
            }
            
            if (parts.size() == 3) {
                // Old format: R, G, B (Implies Enabled)
                blocks[blockId].enabled = true;
                blocks[blockId].color[0] = std::stof(parts[0]);
                blocks[blockId].color[1] = std::stof(parts[1]);
                blocks[blockId].color[2] = std::stof(parts[2]);
                blocks[blockId].colorInitialized = true;
            } else if (parts.size() >= 4) {
                // New format: Enabled, R, G, B
                blocks[blockId].enabled = (parts[0] == "1");
                blocks[blockId].color[0] = std::stof(parts[1]);
                blocks[blockId].color[1] = std::stof(parts[2]);
                blocks[blockId].color[2] = std::stof(parts[3]);
                blocks[blockId].colorInitialized = true;
            }
        }
    }
    // Update server after loading
    {
        std::unique_lock<std::shared_mutex> lock(chunkMapMutex);
        chunkMap.clear();
    }
    SendUpdate();
}

std::tuple<int, int, int> BlockESP::GetChunkPos(int x, int y, int z) {
    // 16x16x16 chunks
    // Handle negative coordinates correctly
    int cx = (x >= 0) ? (x / 16) : ((x + 1) / 16 - 1);
    int cy = (y >= 0) ? (y / 16) : ((y + 1) / 16 - 1);
    int cz = (z >= 0) ? (z / 16) : ((z + 1) / 16 - 1);
    return {cx, cy, cz};
}

void BlockESP::ProcessUpdates(const std::vector<BlockUpdate>& updates, long long& outUpdateTime, long long& outRebuildTime) {
    outUpdateTime = 0;
    outRebuildTime = 0;
    if (updates.empty()) return;

    auto startUpdate = std::chrono::high_resolution_clock::now();

    std::set<std::tuple<int, int, int>> dirtyChunks;

    // Phase 1: Update Blocks
    // Optimization: Avoid holding Unique Lock for the entire duration.
    // Only hold Unique Lock when CREATING chunks.
    // Use Shared Lock + Per-Chunk Lock when UPDATING blocks.
    
    // 1. Identify chunks that need to be created
    std::set<std::tuple<int, int, int>> neededChunks;
    for (const auto& u : updates) {
        if (!u.remove) { // We only create chunks if we are adding blocks
            neededChunks.insert(GetChunkPos(u.x, u.y, u.z));
        }
    }

    {
        // Check for missing chunks with Shared Lock first
        std::vector<std::tuple<int, int, int>> missing;
        {
            std::shared_lock<std::shared_mutex> lock(chunkMapMutex);
            for(const auto& pos : neededChunks) {
                if (chunkMap.find(pos) == chunkMap.end()) {
                    missing.push_back(pos);
                }
            }
        }
        
        // Create missing chunks (Unique Lock)
        if (!missing.empty()) {
            std::unique_lock<std::shared_mutex> lock(chunkMapMutex);
            for(const auto& pos : missing) {
                chunkMap[pos]; // Default construct
            }
        }
    }

    // 2. Update Blocks (Shared Lock on Map, Unique Lock on Chunk)
    {
        std::shared_lock<std::shared_mutex> lock(chunkMapMutex);
        
        for (const auto& u : updates) {
            auto chunkPos = GetChunkPos(u.x, u.y, u.z);
            
            auto it = chunkMap.find(chunkPos);
            if (it == chunkMap.end()) continue; // Should not happen for Adds, maybe for Removes
            
            auto& chunk = it->second;
            std::lock_guard<std::mutex> blockLock(chunk.blockMutex); // Fine-grained lock

            // Local coords
            int lx = u.x % 16; if (lx < 0) lx += 16;
            int ly = u.y % 16; if (ly < 0) ly += 16;
            int lz = u.z % 16; if (lz < 0) lz += 16;
            int index = lx + (ly * 16) + (lz * 256);

            if (u.remove) {
                chunk.blocks.erase(index);
                // We do NOT erase empty chunks here to avoid upgrading lock
            } else {
                chunk.blocks[index] = GetBlockID(u.id);
            }

            // Mark dirty
            dirtyChunks.insert(chunkPos);

            // Neighbors
            int neighbors[6][3] = {
                {1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}, {0,0,1}, {0,0,-1}
            };
            for(int i=0; i<6; i++) {
                 dirtyChunks.insert(GetChunkPos(u.x + neighbors[i][0], u.y + neighbors[i][1], u.z + neighbors[i][2]));
            }
        }
    }

    auto endUpdate = std::chrono::high_resolution_clock::now();
    outUpdateTime = std::chrono::duration_cast<std::chrono::microseconds>(endUpdate - startUpdate).count();

    // Phase 2: Rebuild Meshes (Read Lock on chunkMap)
    auto startRebuild = std::chrono::high_resolution_clock::now();
    {
        std::shared_lock<std::shared_mutex> lock(chunkMapMutex);
        for (const auto& chunkPos : dirtyChunks) {
            // Check if chunk still exists (it might have been removed if empty)
            if (chunkMap.count(chunkPos)) {
                UpdateChunk(chunkPos);
            }
        }
    }
    
    auto endRebuild = std::chrono::high_resolution_clock::now();
    outRebuildTime = std::chrono::duration_cast<std::chrono::microseconds>(endRebuild - startRebuild).count();
}

// Helper struct for edge merging
struct TempEdge {
    float x1, y1, z1;
    float x2, y2, z2;
    ImU32 color;
    int axis; // 0=X, 1=Y, 2=Z
    
    bool operator<(const TempEdge& other) const {
        if (color != other.color) return color < other.color;
        if (axis != other.axis) return axis < other.axis;
        
        // Primary sort key: Constant coordinates
        // Secondary sort key: Variable coordinate start
        if (axis == 0) { // X is variable. Y, Z constant
             if (y1 != other.y1) return y1 < other.y1;
             if (z1 != other.z1) return z1 < other.z1;
             return x1 < other.x1;
        } else if (axis == 1) { // Y is variable. X, Z constant
             if (x1 != other.x1) return x1 < other.x1;
             if (z1 != other.z1) return z1 < other.z1;
             return y1 < other.y1;
        } else { // Z is variable. X, Y constant
             if (x1 != other.x1) return x1 < other.x1;
             if (y1 != other.y1) return y1 < other.y1;
             return z1 < other.z1;
        }
    }
};

void BlockESP::UpdateChunk(std::tuple<int, int, int> chunkPos) {
    // NOTE: Caller holds chunkMapMutex (Shared or Unique)
    
    // We can't safely access chunkMap[chunkPos] if it doesn't exist, but caller checks.
    // However, if we hold shared_lock, another thread (Main) might want to erase it?
    // Main thread uses unique_lock to erase. It will block until we release shared_lock.
    // So we are safe.
    
    const auto& chunk = chunkMap.at(chunkPos);
    
    // Create NEW mesh
    auto newMesh = std::make_shared<ChunkMesh>();
    
    // Bounds
    int cx = std::get<0>(chunkPos);
    int cy = std::get<1>(chunkPos);
    int cz = std::get<2>(chunkPos);
    
    int startX = cx * 16;
    int startY = cy * 16;
    int startZ = cz * 16;

    // 1. Build Lookup Table for Block Properties
    struct BlockInfo {
        bool enabled;
        ImU32 color;
        ImU32 faceColor;
    };
    
    // We can't easily index by ID since IDs are dynamic, but globalPalette is a vector.
    // ID corresponds to globalPalette index + 1.
    // Accessing globalPalette requires lock? 
    // globalPalette is modified in GetBlockID (Phase 1).
    // UpdateChunk runs in Phase 2.
    // Phase 1 and Phase 2 are sequential in Worker thread.
    // But RenderSettings (Main Thread) might modify globalPalette?
    // No, RenderSettings reads availableBlocks, doesn't add to globalPalette.
    // Only GetBlockID adds to globalPalette.
    // So globalPalette is stable during Phase 2?
    // Unless GetBlockID is called from another thread? No.
    // Wait, RenderSettings might call RebuildAllChunks -> UpdateChunk -> globalPalette access.
    // So we need locking.
    
    std::vector<BlockInfo> blockInfos;
    {
        std::lock_guard<std::mutex> lock(paletteMutex);
        blockInfos.resize(globalPalette.size() + 1);
        for (size_t i = 0; i < globalPalette.size(); i++) {
            std::string name = globalPalette[i];
            if (blocks.count(name)) {
                const auto& conf = blocks[name];
                blockInfos[i + 1].enabled = conf.enabled;
                if (conf.enabled) {
                    blockInfos[i + 1].color = IM_COL32(conf.color[0]*255, conf.color[1]*255, conf.color[2]*255, 255);
                    blockInfos[i + 1].faceColor = IM_COL32(conf.color[0]*255, conf.color[1]*255, conf.color[2]*255, 50);
                }
            } else {
                blockInfos[i + 1].enabled = false;
            }
        }
    }

    // 2. Fill Dense Chunk Data
    // 0 = Air or Disabled Block. >0 = ID of Enabled Block.
    uint16_t data[16][16][16];
    std::memset(data, 0, sizeof(data));

    for (const auto& [index, id] : chunk.blocks) {
        if (id < blockInfos.size() && blockInfos[id].enabled) {
            int lx = index % 16;
            int ly = (index / 16) % 16;
            int lz = index / 256;
            data[lx][ly][lz] = id;
        }
    }

    // 3. Greedy Meshing
    // Directions: 0: -Z (North), 1: +X (East), 2: +Z (South), 3: -X (West), 4: +Y (Up), 5: -Y (Down)
    static const int faceDirs[6][3] = {
        {0, 0, -1}, {1, 0, 0}, {0, 0, 1}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}
    };
    
    std::vector<TempEdge> tempEdges;

    for (int dir = 0; dir < 6; dir++) {
        // Determine U, V axes
        int uAxis, vAxis, dAxis;
        if (dir == 0 || dir == 2) { // Z faces
            dAxis = 2; uAxis = 0; vAxis = 1; // Depth=Z, U=X, V=Y
        } else if (dir == 1 || dir == 3) { // X faces
            dAxis = 0; uAxis = 2; vAxis = 1; // Depth=X, U=Z, V=Y
        } else { // Y faces
            dAxis = 1; uAxis = 0; vAxis = 2; // Depth=Y, U=X, V=Z
        }

        // Iterate through layers of Depth Axis
        for (int d = 0; d < 16; d++) {
            uint16_t mask[16][16] = {0}; // mask[v][u]

            // Fill mask
            for (int v = 0; v < 16; v++) {
                for (int u = 0; u < 16; u++) {
                    // Map u, v, d to lx, ly, lz
                    int lx, ly, lz;
                    if (dAxis == 0) { lx = d; ly = v; lz = u; } // X=d, Y=v, Z=u (Wait, mapping depends on axis choice above)
                    // Let's use standard mapping:
                    // Z-faces (d=z): u=x, v=y
                    // X-faces (d=x): u=z, v=y
                    // Y-faces (d=y): u=x, v=z
                    
                    if (dir == 0 || dir == 2) { lx = u; ly = v; lz = d; }
                    else if (dir == 1 || dir == 3) { lx = d; ly = v; lz = u; }
                    else { lx = u; ly = d; lz = v; }

                    uint16_t id = data[lx][ly][lz];
                    if (id == 0) continue;

                    // Check neighbor in direction 'dir'
                    int nx = lx + faceDirs[dir][0];
                    int ny = ly + faceDirs[dir][1];
                    int nz = lz + faceDirs[dir][2];

                    uint16_t neighborId = 0;
                    if (nx >= 0 && nx < 16 && ny >= 0 && ny < 16 && nz >= 0 && nz < 16) {
                        neighborId = data[nx][ny][nz];
                    } else {
                        // Global lookup (Requires lock, which we hold)
                        neighborId = GetBlock(startX + nx, startY + ny, startZ + nz);
                        // Filter neighborId: if it's not enabled, treat as air (0) so we draw face
                        if (neighborId >= blockInfos.size() || !blockInfos[neighborId].enabled) {
                            neighborId = 0;
                        }
                    }

                    if (neighborId != id) {
                        mask[v][u] = id;
                    }
                }
            }

            // Greedy Mesh the mask
            bool visited[16][16] = {false};
            for (int v = 0; v < 16; v++) {
                for (int u = 0; u < 16; u++) {
                    if (visited[v][u] || mask[v][u] == 0) continue;

                    uint16_t id = mask[v][u];
                    
                    // Compute width (along U)
                    int w = 1;
                    while (u + w < 16 && !visited[v][u + w] && mask[v][u + w] == id) {
                        w++;
                    }

                    // Compute height (along V)
                    int h = 1;
                    bool canExtend = true;
                    while (v + h < 16) {
                        for (int k = 0; k < w; k++) {
                            if (visited[v + h][u + k] || mask[v + h][u + k] != id) {
                                canExtend = false;
                                break;
                            }
                        }
                        if (!canExtend) break;
                        h++;
                    }

                    // Mark visited
                    for (int dy = 0; dy < h; dy++) {
                        for (int dx = 0; dx < w; dx++) {
                            visited[v + dy][u + dx] = true;
                        }
                    }

                    // Create Face
                    Face f;
                    float x1, y1, z1, x2, y2, z2; // Local to chunk
                    
                    // Convert back to local coords
                    if (dir == 0 || dir == 2) { // Z faces
                         // u=x, v=y, d=z
                         // Quad: (u, v, d) to (u+w, v+h, d)
                         x1 = (float)u; y1 = (float)v; z1 = (float)d;
                         x2 = (float)(u + w); y2 = (float)(v + h); z2 = (float)d;
                         if (dir == 2) { z1 += 1.0f; z2 += 1.0f; } // +Z face
                    } else if (dir == 1 || dir == 3) { // X faces
                         // u=z, v=y, d=x
                         x1 = (float)d; y1 = (float)v; z1 = (float)u;
                         x2 = (float)d; y2 = (float)(v + h); z2 = (float)(u + w);
                         if (dir == 1) { x1 += 1.0f; x2 += 1.0f; } // +X face
                    } else { // Y faces
                         // u=x, v=z, d=y
                         x1 = (float)u; y1 = (float)d; z1 = (float)v;
                         x2 = (float)(u + w); y2 = (float)d; z2 = (float)(v + h);
                         if (dir == 4) { y1 += 1.0f; y2 += 1.0f; } // +Y face
                    }

                    // Add to mesh (World Coords)
                    f.x1 = startX + x1; f.y1 = startY + y1; f.z1 = startZ + z1;
                    // ... Need 4 points for quad ...
                    // Let's simplify: Store min/max and expand in Render?
                    // No, Render expects 4 points.
                    // Depending on axis, expand x2/y2/z2
                    
                    // Wait, previous implementation stored 4 points in Face?
                    // Let's check struct Face
                    // struct Face { float x1, y1, z1; float x2, y2, z2; float x3, y3, z3; float x4, y4, z4; };
                    
                    // We need to calculate the 4 corners based on orientation
                    if (dir == 0) { // -Z (North)
                        f.x1 = startX + x2; f.y1 = startY + y1; f.z1 = startZ + z1;
                        f.x2 = startX + x1; f.y2 = startY + y1; f.z2 = startZ + z1;
                        f.x3 = startX + x1; f.y3 = startY + y2; f.z3 = startZ + z1;
                        f.x4 = startX + x2; f.y4 = startY + y2; f.z4 = startZ + z1;
                    } else if (dir == 2) { // +Z (South)
                        f.x1 = startX + x1; f.y1 = startY + y1; f.z1 = startZ + z1;
                        f.x2 = startX + x2; f.y2 = startY + y1; f.z2 = startZ + z1;
                        f.x3 = startX + x2; f.y3 = startY + y2; f.z3 = startZ + z1;
                        f.x4 = startX + x1; f.y4 = startY + y2; f.z4 = startZ + z1;
                    } else if (dir == 3) { // -X (West)
                        f.x1 = startX + x1; f.y1 = startY + y1; f.z1 = startZ + z1;
                        f.x2 = startX + x1; f.y2 = startY + y1; f.z2 = startZ + z2;
                        f.x3 = startX + x1; f.y3 = startY + y2; f.z3 = startZ + z2;
                        f.x4 = startX + x1; f.y4 = startY + y2; f.z4 = startZ + z1;
                    } else if (dir == 1) { // +X (East)
                        f.x1 = startX + x1; f.y1 = startY + y1; f.z1 = startZ + z2;
                        f.x2 = startX + x1; f.y2 = startY + y1; f.z2 = startZ + z1;
                        f.x3 = startX + x1; f.y3 = startY + y2; f.z3 = startZ + z1;
                        f.x4 = startX + x1; f.y4 = startY + y2; f.z4 = startZ + z2;
                    } else if (dir == 5) { // -Y (Down)
                        f.x1 = startX + x1; f.y1 = startY + y1; f.z1 = startZ + z1;
                        f.x2 = startX + x2; f.y2 = startY + y1; f.z2 = startZ + z1;
                        f.x3 = startX + x2; f.y3 = startY + y1; f.z3 = startZ + z2;
                        f.x4 = startX + x1; f.y4 = startY + y1; f.z4 = startZ + z2;
                    } else if (dir == 4) { // +Y (Up)
                        f.x1 = startX + x1; f.y1 = startY + y1; f.z1 = startZ + z2;
                        f.x2 = startX + x2; f.y2 = startY + y1; f.z2 = startZ + z2;
                        f.x3 = startX + x2; f.y3 = startY + y1; f.z3 = startZ + z1;
                        f.x4 = startX + x1; f.y4 = startY + y1; f.z4 = startZ + z1;
                    }

                    newMesh->faces.push_back({blockInfos[id].faceColor, f});

                    // Add Edges
                    // Only add edges that are on the border of the greedy quad
                    // Or simply add 4 lines for the quad
                    // Better: Add segments to tempEdges for merging
                    
                    // Bottom/Top horizontal
                    // Left/Right vertical
                    
                    TempEdge e; e.color = blockInfos[id].color;
                    
                    // ... Edge extraction logic ...
                    // Simplifying for brevity/correctness based on previous logic:
                    // Previous logic extracted edges from faces?
                    // Actually, let's just add the 4 edges of the quad to tempEdges
                    
                    // Edge 1
                    e.x1 = f.x1; e.y1 = f.y1; e.z1 = f.z1;
                    e.x2 = f.x2; e.y2 = f.y2; e.z2 = f.z2;
                    // determine axis
                    if (e.x1 != e.x2) e.axis = 0; else if (e.y1 != e.y2) e.axis = 1; else e.axis = 2;
                    // normalize (x1 < x2 etc)
                    if (e.x1 > e.x2 || e.y1 > e.y2 || e.z1 > e.z2) { std::swap(e.x1, e.x2); std::swap(e.y1, e.y2); std::swap(e.z1, e.z2); }
                    tempEdges.push_back(e);

                    // Edge 2
                    e.x1 = f.x2; e.y1 = f.y2; e.z1 = f.z2;
                    e.x2 = f.x3; e.y2 = f.y3; e.z2 = f.z3;
                    if (e.x1 != e.x2) e.axis = 0; else if (e.y1 != e.y2) e.axis = 1; else e.axis = 2;
                    if (e.x1 > e.x2 || e.y1 > e.y2 || e.z1 > e.z2) { std::swap(e.x1, e.x2); std::swap(e.y1, e.y2); std::swap(e.z1, e.z2); }
                    tempEdges.push_back(e);

                    // Edge 3
                    e.x1 = f.x3; e.y1 = f.y3; e.z1 = f.z3;
                    e.x2 = f.x4; e.y2 = f.y4; e.z2 = f.z4;
                    if (e.x1 != e.x2) e.axis = 0; else if (e.y1 != e.y2) e.axis = 1; else e.axis = 2;
                    if (e.x1 > e.x2 || e.y1 > e.y2 || e.z1 > e.z2) { std::swap(e.x1, e.x2); std::swap(e.y1, e.y2); std::swap(e.z1, e.z2); }
                    tempEdges.push_back(e);

                    // Edge 4
                    e.x1 = f.x4; e.y1 = f.y4; e.z1 = f.z4;
                    e.x2 = f.x1; e.y2 = f.y1; e.z2 = f.z1;
                    if (e.x1 != e.x2) e.axis = 0; else if (e.y1 != e.y2) e.axis = 1; else e.axis = 2;
                    if (e.x1 > e.x2 || e.y1 > e.y2 || e.z1 > e.z2) { std::swap(e.x1, e.x2); std::swap(e.y1, e.y2); std::swap(e.z1, e.z2); }
                    tempEdges.push_back(e);
                }
            }
        }
    }

    // Merge Edges
    std::sort(tempEdges.begin(), tempEdges.end());
    
    if (!tempEdges.empty()) {
        TempEdge current = tempEdges[0];
        // We need to count duplicates. If an edge is shared by 2 faces (e.g. 90 degree turn), we keep it.
        // If it's shared by 2 coplanar faces, it shouldn't exist because we greedy meshed faces.
        // Wait, 4 edges per quad. Adjacent quads share an edge.
        // If we draw wireframe, we want that edge? 
        // Actually, we usually want outlines.
        // If an edge is present TWICE (shared by 2 faces), it might be an internal edge or a corner.
        // If it is shared by faces with different normals (corner), we want it.
        // If it is shared by faces with same normal (internal), we don't want it (but greedy meshing removes internal edges).
        // So duplicates here mean corners or T-junctions.
        // We can just draw all unique edges?
        // Or merging collinear edges.
        
        // Let's just merge collinear edges regardless of duplicates for now to match previous behavior?
        // Previous behavior:
        /*
            if (sameLine) {
                 // extend
            }
        */
        
        // Actually, logic is:
        // Iterate sorted edges.
        // If current edge overlaps/touches next edge AND same color AND same axis AND same constant coords:
        // Merge them.
        
        for (size_t i = 1; i < tempEdges.size(); i++) {
            TempEdge& next = tempEdges[i];
            
            bool sameLine = false;
            if (current.color == next.color && current.axis == next.axis) {
                 if (current.axis == 0) { // X variable
                     sameLine = (current.y1 == next.y1) && (current.z1 == next.z1);
                 } else if (current.axis == 1) { // Y variable
                     sameLine = (current.x1 == next.x1) && (current.z1 == next.z1);
                 } else { // Z variable
                     sameLine = (current.x1 == next.x1) && (current.y1 == next.y1);
                 }
            }

            if (sameLine) {
                float currEnd = (current.axis == 0) ? current.x2 : ((current.axis == 1) ? current.y2 : current.z2);
                float nextStart = (next.axis == 0) ? next.x1 : ((next.axis == 1) ? next.y1 : next.z1);
                
                if (currEnd >= nextStart - 0.001f) { // Overlap or touch
                    float nextEnd = (next.axis == 0) ? next.x2 : ((next.axis == 1) ? next.y2 : next.z2);
                    if (nextEnd > currEnd) {
                        if (current.axis == 0) current.x2 = nextEnd;
                        else if (current.axis == 1) current.y2 = nextEnd;
                        else current.z2 = nextEnd;
                    }
                    continue; 
                }
            }
            
            Edge e;
            e.x1 = current.x1; e.y1 = current.y1; e.z1 = current.z1;
            e.x2 = current.x2; e.y2 = current.y2; e.z2 = current.z2;
            newMesh->edges.push_back({current.color, e});
            
            current = next;
        }
        Edge e;
        e.x1 = current.x1; e.y1 = current.y1; e.z1 = current.z1;
        e.x2 = current.x2; e.y2 = current.y2; e.z2 = current.z2;
        newMesh->edges.push_back({current.color, e});
    }

    newMesh->edges.shrink_to_fit();
    newMesh->faces.shrink_to_fit();
    
    // Swap the mesh
    {
        std::lock_guard<std::mutex> lock(chunk.meshMutex);
        // We need to cast away constness of 'chunk' to modify 'mesh'
        // Since we accessed it via at() on a non-const map?
        // Wait, I used const auto& chunk = chunkMap.at(chunkPos);
        // This makes 'chunk' const.
        // But 'meshMutex' is mutable.
        // 'mesh' is NOT mutable.
        // So I need non-const reference.
        // Since I hold shared_lock, accessing map via non-const is... undefined if I modify the map structure.
        // But I am modifying the VALUE (chunk).
        // Does shared_lock allow modifying values?
        // Yes, if the map is not const.
        // shared_lock only guarantees multiple readers.
        // If I modify the object *in place* (thread safe internally), it is fine.
        // So I should use auto& chunk = chunkMap.at(chunkPos);
        // But chunkMap is not const? Yes.
        // But wait, if I use shared_lock, I shouldn't modify the container structure.
        // But modifying the element is fine if it doesn't race with other readers of THAT element.
        // Render reads THAT element (mesh pointer).
        // I protect mesh pointer with meshMutex.
        // So it is safe.
        // So I just need to remove 'const' from 'chunk'.
        const_cast<CachedChunk&>(chunk).mesh = newMesh;
    }
}

void BlockESP::WorkerLoop() {
    while (!shouldStop) {
        std::vector<BlockUpdate> updates;
        std::vector<std::pair<int, int>> unloads;
        
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            queueCV.wait(lock, [this] { return shouldStop || !updateQueue.empty() || !unloadQueue.empty(); });
            
            if (shouldStop) break;
            
            if (!updateQueue.empty()) {
                updates = std::move(updateQueue.front());
                updateQueue.pop();
            }
            while (!unloadQueue.empty()) {
                unloads.push_back(unloadQueue.front());
                unloadQueue.pop();
            }
        }

        if (!unloads.empty()) {
            std::unique_lock<std::shared_mutex> lock(chunkMapMutex);
            for (const auto& p : unloads) {
                // Optimization: Instead of scanning the entire map (O(N)), 
                // we probe the likely vertical chunk range (O(Log N)).
                // Standard Minecraft is Y=-64 to 320 (cy -4 to 20).
                // We scan -64 to 64 to be safe (Y -1024 to +1024).
                for (int cy = -64; cy <= 64; cy++) {
                    chunkMap.erase({p.first, cy, p.second});
                }
            }
        }

        if (!updates.empty()) {
            long long t1, t2;
            ProcessUpdates(updates, t1, t2);
        }
    }
}

void BlockESP::Render(const GameData& data, float screenW, float screenH, ImDrawList* draw) {
    if (!enabled) return;

    // clear data if not connected
    if (!net->IsConnected()) {
        std::unique_lock<std::shared_mutex> lock(chunkMapMutex);
        chunkMap.clear();
        return;
    }

    if (data.shouldClearBlocks) {
        std::unique_lock<std::shared_mutex> lock(chunkMapMutex);
        chunkMap.clear();
    }

    if (!data.blocksToDelete.empty()) {
        for (const auto& id : data.blocksToDelete) {
            RemoveBlocks(id);
        }
        // Rebuild is handled in RemoveBlocks? No, RemoveBlocks just modifies blocks.
        // We need to trigger rebuild.
        RebuildAllChunks();
    }

    // Push unloads to worker
    if (!data.chunksToUnload.empty()) {
        {
            std::lock_guard<std::mutex> lock(queueMutex);
            for (const auto& chunk : data.chunksToUnload) {
                unloadQueue.push(chunk);
            }
        }
        queueCV.notify_one();
    }

    // Push updates to worker
    if (!data.blockUpdates.empty()) {
        {
            std::lock_guard<std::mutex> lock(queueMutex);
            updateQueue.push(data.blockUpdates);
        }
        queueCV.notify_one();
    }

    auto startRender = std::chrono::high_resolution_clock::now();

    // Precompute ViewState
    ViewState viewState = PrecomputeViewState(data.camYaw, data.camPitch, data.fov, screenW, screenH);

    // Render loop
    int drawnEdges = 0;
    float limitDist = (float)renderRange + 24.0f; // Range + Chunk Radius buffer
    float limitSq = limitDist * limitDist;
    
    // Acquire Read Lock
    std::shared_lock<std::shared_mutex> lock(chunkMapMutex);

    // Collect and sort chunks for Painter's Algorithm (Far -> Near)
    struct RenderableChunk {
        float distSq;
        const CachedChunk* chunk;
    };
    std::vector<RenderableChunk> renderList;
    renderList.reserve(chunkMap.size());
    
    for (const auto& [key, chunk] : chunkMap) {
        // Distance Check (Chunk Center)
        float cx = (std::get<0>(key) * 16) + 8.0f;
        float cy = (std::get<1>(key) * 16) + 8.0f;
        float cz = (std::get<2>(key) * 16) + 8.0f;
        
        float distSq = (cx - data.camX) * (cx - data.camX) + 
                       (cy - data.camY) * (cy - data.camY) + 
                       (cz - data.camZ) * (cz - data.camZ);
                       
        if (distSq > limitSq) continue;
        
        renderList.push_back({distSq, &chunk});
    }

    // Sort: Furthest first (Painter's Algorithm)
    std::sort(renderList.begin(), renderList.end(), [](const RenderableChunk& a, const RenderableChunk& b) {
        return a.distSq > b.distSq;
    });

    for (const auto& rc : renderList) {
        const CachedChunk& chunk = *rc.chunk;
        
        // Get Mesh safely
        std::shared_ptr<ChunkMesh> mesh;
        {
            std::lock_guard<std::mutex> meshLock(chunk.meshMutex);
            mesh = chunk.mesh;
        }
        
        if (!mesh) continue;

        for (const auto& facePair : mesh->faces) {
             ImU32 col = facePair.first;
             const Face& f = facePair.second;
             
             // Camera Space
             Vec3 v1 = WorldToCamera(f.x1 - data.camX, f.y1 - data.camY, f.z1 - data.camZ, viewState);
             Vec3 v2 = WorldToCamera(f.x2 - data.camX, f.y2 - data.camY, f.z2 - data.camZ, viewState);
             Vec3 v3 = WorldToCamera(f.x3 - data.camX, f.y3 - data.camY, f.z3 - data.camZ, viewState);
             Vec3 v4 = WorldToCamera(f.x4 - data.camX, f.y4 - data.camY, f.z4 - data.camZ, viewState);
             
             bool allIn = (v1.z > 0.1f && v2.z > 0.1f && v3.z > 0.1f && v4.z > 0.1f);
             bool allOut = (v1.z <= 0.1f && v2.z <= 0.1f && v3.z <= 0.1f && v4.z <= 0.1f);

             if (allOut) continue;

             if (allIn) {
                 Vec2 p1 = CameraToScreen(v1, viewState);
                 Vec2 p2 = CameraToScreen(v2, viewState);
                 Vec2 p3 = CameraToScreen(v3, viewState);
                 Vec2 p4 = CameraToScreen(v4, viewState);
                 draw->AddQuadFilled(ImVec2(p1.x, p1.y), ImVec2(p2.x, p2.y), ImVec2(p3.x, p3.y), ImVec2(p4.x, p4.y), col);
             } else {
                 // Clipping
                 Vec3 input[4] = {v1, v2, v3, v4};
                 Vec3 output[8];
                 int outCount = 0;
                 float nearZ = 0.1f;
                 
                 const Vec3* prev = &input[3];
                 float prevD = prev->z - nearZ;
                 
                 for(int i=0; i<4; i++) {
                     const Vec3* curr = &input[i];
                     float currD = curr->z - nearZ;
                     
                     if (currD >= 0) { // Current inside
                        if (prevD < 0) { // Enter
                            float t = (nearZ - prev->z) / (curr->z - prev->z);
                            output[outCount++] = { prev->x + (curr->x - prev->x)*t, prev->y + (curr->y - prev->y)*t, nearZ };
                        }
                        output[outCount++] = *curr;
                     } else { // Current outside
                        if (prevD >= 0) { // Exit
                            float t = (nearZ - prev->z) / (curr->z - prev->z);
                            output[outCount++] = { prev->x + (curr->x - prev->x)*t, prev->y + (curr->y - prev->y)*t, nearZ };
                        }
                     }
                     prev = curr;
                     prevD = currD;
                 }
                 
                 if (outCount >= 3) {
                     ImVec2 imPoints[8];
                     for(int i=0; i<outCount; i++) {
                         Vec2 p = CameraToScreen(output[i], viewState);
                         imPoints[i] = ImVec2(p.x, p.y);
                     }
                     draw->AddConvexPolyFilled(imPoints, outCount, col);
                 }
             }
        }

        for (const auto& edgePair : mesh->edges) {
             ImU32 col = edgePair.first;
             const Edge& e = edgePair.second;
             
             Vec3 v1 = WorldToCamera(e.x1 - data.camX, e.y1 - data.camY, e.z1 - data.camZ, viewState);
             Vec3 v2 = WorldToCamera(e.x2 - data.camX, e.y2 - data.camY, e.z2 - data.camZ, viewState);

             if (v1.z <= 0.1f && v2.z <= 0.1f) continue;
             
             if (v1.z > 0.1f && v2.z > 0.1f) {
                 Vec2 p1 = CameraToScreen(v1, viewState);
                 Vec2 p2 = CameraToScreen(v2, viewState);
                 draw->AddLine(ImVec2(p1.x, p1.y), ImVec2(p2.x, p2.y), col);
                 drawnEdges++;
             } else {
                 float t = (0.1f - v1.z) / (v2.z - v1.z);
                 Vec3 intersect = { v1.x + (v2.x - v1.x)*t, v1.y + (v2.y - v1.y)*t, 0.1f };
                 
                 Vec3 start = (v1.z > 0.1f) ? v1 : intersect;
                 Vec3 end = (v2.z > 0.1f) ? v2 : intersect;
                 
                 Vec2 p1 = CameraToScreen(start, viewState);
                 Vec2 p2 = CameraToScreen(end, viewState);
                 draw->AddLine(ImVec2(p1.x, p1.y), ImVec2(p2.x, p2.y), col);
                 drawnEdges++;
             }
        }
    }

    auto endRender = std::chrono::high_resolution_clock::now();
    long long renderTime = std::chrono::duration_cast<std::chrono::microseconds>(endRender - startRender).count();
    
    // Debug Output (Throttled)
    static long long lastPrint = 0;
    long long now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    if (now - lastPrint > 1000) {
        // Calculate memory
        size_t totalBlocks = 0;
        size_t blockMem = 0;
        size_t edgeMem = 0;
        size_t faceMem = 0;
        
        for(const auto& [key, chunk] : chunkMap) {
            std::lock_guard<std::mutex> lock(chunk.blockMutex);
            totalBlocks += chunk.blocks.size();
            blockMem += chunk.blocks.size() * 48; 
            if (chunk.mesh) {
                edgeMem += chunk.mesh->edges.capacity() * sizeof(std::pair<ImU32, Edge>);
                faceMem += chunk.mesh->faces.capacity() * sizeof(std::pair<ImU32, Face>);
            }
        }
        
        double blockMemMB = blockMem / (1024.0 * 1024.0);
        double edgeMemMB = edgeMem / (1024.0 * 1024.0);
        double faceMemMB = faceMem / (1024.0 * 1024.0);
        double totalMemMB = blockMemMB + edgeMemMB + faceMemMB;

        std::cout << "[BlockESP] Render: " << renderTime << "us | Edges: " << drawnEdges 
                  << " | Chunks: " << chunkMap.size() 
                  << " | Blocks: " << totalBlocks
                  << " | ESP Data: " << totalMemMB << "MB" << std::endl;
        lastPrint = now;
    }
}
