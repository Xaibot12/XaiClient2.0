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
        chunkMap.clear();
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
    chunkMap.clear(); 
    SendUpdate();
}

void BlockESP::RemoveBlocks(const std::string& blockId) {
    if (globalPaletteMap.find(blockId) == globalPaletteMap.end()) return;
    uint16_t id = globalPaletteMap[blockId];

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
    if (id == 0 || id > globalPalette.size()) return "";
    return globalPalette[id - 1];
}

uint16_t BlockESP::GetBlock(int x, int y, int z) {
    auto chunkPos = GetChunkPos(x, y, z);
    if (chunkMap.find(chunkPos) == chunkMap.end()) return 0;
    
    // Local coords (0-15)
    int lx = x % 16; if (lx < 0) lx += 16;
    int ly = y % 16; if (ly < 0) ly += 16;
    int lz = z % 16; if (lz < 0) lz += 16;
    
    int index = lx + (ly * 16) + (lz * 256);
    
    auto& blocks = chunkMap[chunkPos].blocks;
    auto it = blocks.find(index);
    if (it != blocks.end()) {
        return it->second;
    }
    return 0;
}

void BlockESP::RebuildAllChunks() {
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
    chunkMap.clear();
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

    for (const auto& u : updates) {
        auto chunkPos = GetChunkPos(u.x, u.y, u.z);
        
        // Local coords
        int lx = u.x % 16; if (lx < 0) lx += 16;
        int ly = u.y % 16; if (ly < 0) ly += 16;
        int lz = u.z % 16; if (lz < 0) lz += 16;
        int index = lx + (ly * 16) + (lz * 256);

        if (u.remove) {
            if (chunkMap.count(chunkPos)) {
                chunkMap[chunkPos].blocks.erase(index);
                // If chunk is empty, we could remove it, but maybe keep it for now
                if (chunkMap[chunkPos].blocks.empty()) {
                    chunkMap.erase(chunkPos);
                }
            }
        } else {
            // Convert string to ID
            chunkMap[chunkPos].blocks[index] = GetBlockID(u.id);
        }

        // Mark this chunk dirty
        dirtyChunks.insert(chunkPos);

        // Check neighbors for cross-chunk updates
        int neighbors[6][3] = {
            {1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}, {0,0,1}, {0,0,-1}
        };
        for(int i=0; i<6; i++) {
             dirtyChunks.insert(GetChunkPos(u.x + neighbors[i][0], u.y + neighbors[i][1], u.z + neighbors[i][2]));
        }
    }

    auto endUpdate = std::chrono::high_resolution_clock::now();
    outUpdateTime = std::chrono::duration_cast<std::chrono::microseconds>(endUpdate - startUpdate).count();

    auto startRebuild = std::chrono::high_resolution_clock::now();
    for (const auto& chunkPos : dirtyChunks) {
        UpdateChunk(chunkPos);
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
    if (chunkMap.find(chunkPos) == chunkMap.end()) return;
    auto& chunk = chunkMap[chunkPos];
    chunk.edges.clear();
    chunk.faces.clear();
    
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
    std::vector<BlockInfo> blockInfos(globalPalette.size() + 1);
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
                    if (dAxis == 0) { lx = d; lz = u; ly = v; }
                    else if (dAxis == 1) { ly = d; lx = u; lz = v; }
                    else { lz = d; lx = u; ly = v; }
                    
                    uint16_t id = data[lx][ly][lz];
                    if (id == 0) continue;

                    // Check neighbor
                    int nx = lx + faceDirs[dir][0];
                    int ny = ly + faceDirs[dir][1];
                    int nz = lz + faceDirs[dir][2];

                    uint16_t neighborId = 0;
                    if (nx >= 0 && nx < 16 && ny >= 0 && ny < 16 && nz >= 0 && nz < 16) {
                        neighborId = data[nx][ny][nz];
                    } else {
                        // Global lookup
                        neighborId = GetBlock(startX + nx, startY + ny, startZ + nz);
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

                    // Generate Face
                    Face f;
                    float dPos = (float)d;
                    if (dir == 1 || dir == 2 || dir == 4) dPos += 1.0f;
                    
                    float uPos = (float)u;
                    float vPos = (float)v;
                    float uSize = (float)w;
                    float vSize = (float)h;

                    // Helper to map (u, v, d) -> (x, y, z)
                    auto mapCoords = [&](float u, float v, float d) {
                         float res[3];
                         if (dAxis == 0) { res[0] = d; res[2] = u; res[1] = v; }
                         else if (dAxis == 1) { res[1] = d; res[0] = u; res[2] = v; }
                         else { res[2] = d; res[0] = u; res[1] = v; }
                         res[0] += startX; res[1] += startY; res[2] += startZ;
                         return std::make_tuple(res[0], res[1], res[2]);
                    };

                    float p[4][3];
                    auto setP = [&](int idx, float u, float v) {
                        auto t = mapCoords(u, v, dPos);
                        p[idx][0] = std::get<0>(t);
                        p[idx][1] = std::get<1>(t);
                        p[idx][2] = std::get<2>(t);
                    };
                    
                    setP(0, uPos, vPos);
                    setP(1, uPos + uSize, vPos);
                    setP(2, uPos + uSize, vPos + vSize);
                    setP(3, uPos, vPos + vSize);

                    f.x1 = p[0][0]; f.y1 = p[0][1]; f.z1 = p[0][2];
                    f.x2 = p[1][0]; f.y2 = p[1][1]; f.z2 = p[1][2];
                    f.x3 = p[2][0]; f.y3 = p[2][1]; f.z3 = p[2][2];
                    f.x4 = p[3][0]; f.y4 = p[3][1]; f.z4 = p[3][2];
                    
                    chunk.faces.push_back({blockInfos[id].faceColor, f});
                }
            }
            
            // Generate Edges from Mask Boundaries
            // This prevents internal edges between adjacent faces of the same type
            float dPos = (float)d;
            if (dir == 1 || dir == 2 || dir == 4) dPos += 1.0f;
            
            int worldUAxis = (dAxis == 0) ? 2 : 0; 
            int worldVAxis = (dAxis == 1) ? 2 : 1; 

            auto mapCoords = [&](float u, float v, float d) {
                 float res[3];
                 if (dAxis == 0) { res[0] = d; res[2] = u; res[1] = v; }
                 else if (dAxis == 1) { res[1] = d; res[0] = u; res[2] = v; }
                 else { res[2] = d; res[0] = u; res[1] = v; }
                 res[0] += startX; res[1] += startY; res[2] += startZ;
                 return std::make_tuple(res[0], res[1], res[2]);
            };

            auto addEdge = [&](float u1, float v1, float u2, float v2, ImU32 col, int axis) {
                TempEdge e;
                e.color = col;
                e.axis = axis;
                auto t1 = mapCoords(u1, v1, dPos);
                auto t2 = mapCoords(u2, v2, dPos);
                
                // Ensure consistent ordering for merger
                float val1 = (axis == 0) ? std::get<0>(t1) : ((axis == 1) ? std::get<1>(t1) : std::get<2>(t1));
                float val2 = (axis == 0) ? std::get<0>(t2) : ((axis == 1) ? std::get<1>(t2) : std::get<2>(t2));
                
                if (val1 < val2) {
                    e.x1 = std::get<0>(t1); e.y1 = std::get<1>(t1); e.z1 = std::get<2>(t1);
                    e.x2 = std::get<0>(t2); e.y2 = std::get<1>(t2); e.z2 = std::get<2>(t2);
                } else {
                    e.x1 = std::get<0>(t2); e.y1 = std::get<1>(t2); e.z1 = std::get<2>(t2);
                    e.x2 = std::get<0>(t1); e.y2 = std::get<1>(t1); e.z2 = std::get<2>(t1);
                }
                tempEdges.push_back(e);
            };

            auto GetMask = [&](int u, int v) -> uint16_t {
                if (u < 0 || u >= 16 || v < 0 || v >= 16) return 0;
                return mask[v][u];
            };

            // Vertical Edges (along V axis, varying V)
            for (int u = 0; u <= 16; u++) {
                for (int v = 0; v < 16; v++) {
                    uint16_t id1 = GetMask(u - 1, v);
                    uint16_t id2 = GetMask(u, v);
                    
                    if (id1 != id2) {
                        if (id1 != 0) addEdge((float)u, (float)v, (float)u, (float)v + 1.0f, blockInfos[id1].color, worldVAxis);
                        if (id2 != 0) addEdge((float)u, (float)v, (float)u, (float)v + 1.0f, blockInfos[id2].color, worldVAxis);
                    }
                }
            }

            // Horizontal Edges (along U axis, varying U)
            for (int v = 0; v <= 16; v++) {
                for (int u = 0; u < 16; u++) {
                    uint16_t id1 = GetMask(u, v - 1);
                    uint16_t id2 = GetMask(u, v);
                    
                    if (id1 != id2) {
                         if (id1 != 0) addEdge((float)u, (float)v, (float)u + 1.0f, (float)v, blockInfos[id1].color, worldUAxis);
                         if (id2 != 0) addEdge((float)u, (float)v, (float)u + 1.0f, (float)v, blockInfos[id2].color, worldUAxis);
                    }
                }
            }

        }
    }

    // 4. Merge Edges
    if (!tempEdges.empty()) {
        std::sort(tempEdges.begin(), tempEdges.end());
        
        TempEdge current = tempEdges[0];
        for (size_t i = 1; i < tempEdges.size(); i++) {
            TempEdge& next = tempEdges[i];
            
            bool sameLine = (current.color == next.color) && (current.axis == next.axis);
            if (sameLine) {
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
                
                if (currEnd >= nextStart - 0.001f) {
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
            chunk.edges.push_back({current.color, e});
            
            current = next;
        }
        Edge e;
        e.x1 = current.x1; e.y1 = current.y1; e.z1 = current.z1;
        e.x2 = current.x2; e.y2 = current.y2; e.z2 = current.z2;
        chunk.edges.push_back({current.color, e});
    }

    chunk.edges.shrink_to_fit();
    chunk.faces.shrink_to_fit();
}

void BlockESP::Render(const GameData& data, float screenW, float screenH, ImDrawList* draw) {
    if (!enabled) return;

    // clear data if not connected
    if (!net->IsConnected()) {
        chunkMap.clear();
        return;
    }

    if (data.shouldClearBlocks) {
        chunkMap.clear();
    }

    if (!data.blocksToDelete.empty()) {
        for (const auto& id : data.blocksToDelete) {
            RemoveBlocks(id);
        }
        RebuildAllChunks();
    }

    auto startCalc = std::chrono::high_resolution_clock::now();
    // Process network updates
    long long updateTime = 0;
    long long rebuildTime = 0;
    size_t updateCount = data.blockUpdates.size();
    ProcessUpdates(data.blockUpdates, updateTime, rebuildTime);
    auto endCalc = std::chrono::high_resolution_clock::now();
    long long calcTime = std::chrono::duration_cast<std::chrono::microseconds>(endCalc - startCalc).count();

    auto startRender = std::chrono::high_resolution_clock::now();

    // Precompute ViewState
    ViewState viewState = PrecomputeViewState(data.camYaw, data.camPitch, data.fov, screenW, screenH);

    // Render loop
    int drawnEdges = 0;
    float rangeSq = (float)(renderRange * renderRange);
    
    for (const auto& [key, chunk] : chunkMap) {
        // Distance Check (Chunk Center)
        float cx = (std::get<0>(key) * 16) + 8.0f;
        float cy = (std::get<1>(key) * 16) + 8.0f;
        float cz = (std::get<2>(key) * 16) + 8.0f;
        
        float distSq = (cx - data.camX) * (cx - data.camX) + 
                       (cy - data.camY) * (cy - data.camY) + 
                       (cz - data.camZ) * (cz - data.camZ);
                       
        // Check if chunk is completely out of range (approximate)
        // Add some buffer (chunk radius ~14)
        if (distSq > (rangeSq + 5000.0f)) continue;
        
        for (const auto& facePair : chunk.faces) {
             ImU32 col = facePair.first;
             const Face& f = facePair.second;
             
             // Project (Relative to camera)
             float rX1 = f.x1 - data.camX; float rY1 = f.y1 - data.camY; float rZ1 = f.z1 - data.camZ;
             float rX2 = f.x2 - data.camX; float rY2 = f.y2 - data.camY; float rZ2 = f.z2 - data.camZ;
             float rX3 = f.x3 - data.camX; float rY3 = f.y3 - data.camY; float rZ3 = f.z3 - data.camZ;
             float rX4 = f.x4 - data.camX; float rY4 = f.y4 - data.camY; float rZ4 = f.z4 - data.camZ;

             Vec2 p1 = WorldToScreen(rX1, rY1, rZ1, viewState);
             Vec2 p2 = WorldToScreen(rX2, rY2, rZ2, viewState);
             Vec2 p3 = WorldToScreen(rX3, rY3, rZ3, viewState);
             Vec2 p4 = WorldToScreen(rX4, rY4, rZ4, viewState);
             
             if (p1.x > -10000 && p2.x > -10000 && p3.x > -10000 && p4.x > -10000) {
                 draw->AddQuadFilled(ImVec2(p1.x, p1.y), ImVec2(p2.x, p2.y), ImVec2(p3.x, p3.y), ImVec2(p4.x, p4.y), col);
             }
        }

        for (const auto& edgePair : chunk.edges) {
             ImU32 col = edgePair.first;
             const Edge& e = edgePair.second;
             
             // Project (Relative to camera)
             float rX1 = e.x1 - data.camX; float rY1 = e.y1 - data.camY; float rZ1 = e.z1 - data.camZ;
             float rX2 = e.x2 - data.camX; float rY2 = e.y2 - data.camY; float rZ2 = e.z2 - data.camZ;

             Vec2 p1 = WorldToScreen(rX1, rY1, rZ1, viewState);
             Vec2 p2 = WorldToScreen(rX2, rY2, rZ2, viewState);
             
             if (p1.x > -10000 && p2.x > -10000) {
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
            totalBlocks += chunk.blocks.size();
            // Map node overhead: 3 ptrs + color + key + value + padding ~ 48 bytes
            blockMem += chunk.blocks.size() * 48; 
            edgeMem += chunk.edges.capacity() * sizeof(std::pair<ImU32, Edge>);
            faceMem += chunk.faces.capacity() * sizeof(std::pair<ImU32, Face>);
        }
        
        // Convert to MB
        double blockMemMB = blockMem / (1024.0 * 1024.0);
        double edgeMemMB = edgeMem / (1024.0 * 1024.0);
        double faceMemMB = faceMem / (1024.0 * 1024.0);
        double totalMemMB = blockMemMB + edgeMemMB + faceMemMB;

        std::cout << "[BlockESP] Calc: " << calcTime << "us (Update: " << updateTime << "us [" << updateCount << "] | Rebuild: " << rebuildTime << "us) | Render: " << renderTime << "us | Edges: " << drawnEdges 
                  << " | Chunks: " << chunkMap.size() 
                  << " | Blocks: " << totalBlocks
                  << " | ESP Data: " << totalMemMB << "MB (Blocks: " << blockMemMB << "MB | Edges: " << edgeMemMB << "MB | Faces: " << faceMemMB << "MB)" << std::endl;
        lastPrint = now;
    }
}
