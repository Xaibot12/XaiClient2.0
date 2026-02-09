#pragma once
#define NOMINMAX
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>
#include <ws2tcpip.h>
#include <vector>
#include <map>
#include <iostream>
#include <algorithm>
#include <chrono>
#include <intrin.h>
#include "Module.h"

#pragma comment(lib, "ws2_32.lib")

struct Enchantment {
    std::string abbr;
    int level;
};

struct Item {
    std::string id;
    int count = 0;
    int maxDamage = 0;
    int damage = 0;
    std::vector<Enchantment> enchants;
};

struct Entity {
    int id;
    bool isPlayer;
    float x, y, z; // Relative to camera
    float w, h;
    std::string name;
    int ping;
    float health, maxHealth, absorption;
    std::vector<Item> items;

    // Cache for optimization
    bool shouldRender = false;
    bool shouldRenderNametag = false;
    unsigned int cachedColor = 0xFFFFFFFF;
    int lastFrameSeen = 0;
};

struct BlockPos {
    std::string id;
    int x, y, z; // Absolute Coordinates
};

struct BlockUpdate {
    bool remove;
    std::string id;
    int x, y, z;
};

struct GameData {
    float camYaw, camPitch;
    double camX, camY, camZ; // Absolute Camera Position
    float fov;
    bool isScreenOpen;
    int targetedEntityId = -1;
    bool shouldClearBlocks = false;
    std::vector<Entity> entities;
    std::vector<BlockUpdate> blockUpdates;
    std::vector<std::string> blocksToDelete;
    std::vector<std::pair<int, int>> chunksToUnload;
    std::vector<int> hotkeysPressed;
};

#include "modules/ESP.h"
#include "modules/PlayerESP.h"
#include "modules/Nametags.h"

class NetworkClient {
    SOCKET sock;
    bool connected = false;
    
    // Modules for Pre-Calculation
    ESP* espModule = nullptr;
    PlayerESP* playerEspModule = nullptr;
    Nametags* nametagsModule = nullptr;

    std::map<int, Entity> entityCache;
    int currentFrame = 0;
    
    // Debugging
    long long totalParseTime = 0;
    int parseFrames = 0;
    std::chrono::steady_clock::time_point lastDebugTime;

public:
    NetworkClient() : sock(INVALID_SOCKET) {
        lastDebugTime = std::chrono::steady_clock::now();
    }
    
    void SetModules(ESP* esp, PlayerESP* playerEsp, Nametags* nametags) {
        espModule = esp;
        playerEspModule = playerEsp;
        nametagsModule = nametags;
    }

    bool IsConnected() const { return connected; }

    bool Connect() {
        if (connected) return true;

        if (sock != INVALID_SOCKET) {
            closesocket(sock);
            sock = INVALID_SOCKET;
        }

        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) return false;

        sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock == INVALID_SOCKET) {
            WSACleanup();
            return false;
        }

        // Set NoDelay
        int flag = 1;
        setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (char*)&flag, sizeof(int));

        sockaddr_in server;
        server.sin_family = AF_INET;
        server.sin_port = htons(25566);
        server.sin_addr.s_addr = inet_addr("127.0.0.1");

        if (connect(sock, (sockaddr*)&server, sizeof(server)) == 0) {
            connected = true;
            // Clear previous state on new connection
            return true;
        }

        closesocket(sock);
        WSACleanup();
        return false;
    }

    bool SendDisable(bool fully) {
        if (!connected) return false;

        int header = htonl(0xBADF00D);
        if (send(sock, (char*)&header, 4, 0) == SOCKET_ERROR) return false;

        char b = fully ? 1 : 0;
        if (send(sock, &b, 1, 0) == SOCKET_ERROR) return false;
        
        return true;
    }

    bool SendState(const std::vector<Module*>& modules) {
        if (!connected) return false;

        int header = htonl(0xDEADBEEF);
        if (send(sock, (char*)&header, 4, 0) == SOCKET_ERROR) return false;

        int count = htonl(modules.size());
        if (send(sock, (char*)&count, 4, 0) == SOCKET_ERROR) return false;

        for (Module* mod : modules) {
            std::string name = mod->name;
            int nameLen = htonl(name.length());
            if (send(sock, (char*)&nameLen, 4, 0) == SOCKET_ERROR) return false;
            if (send(sock, name.c_str(), name.length(), 0) == SOCKET_ERROR) return false;
            
            char enabled = mod->enabled ? 1 : 0;
            if (send(sock, &enabled, 1, 0) == SOCKET_ERROR) return false;
        }
        return true;
    }

    bool SendBlockList(const std::vector<std::string>& blocks) {
        if (!connected) return false;
        std::cout << "[Overlay] Sending Block List Request. Count: " << blocks.size() << std::endl;
        // Packet Header: 0xB10C0
        int header = htonl(0xB10C0);
        if (send(sock, (char*)&header, 4, 0) == SOCKET_ERROR) return false;

        int count = htonl(blocks.size());
        if (send(sock, (char*)&count, 4, 0) == SOCKET_ERROR) return false;

        for (const auto& block : blocks) {
            int len = htonl(block.length());
            if (send(sock, (char*)&len, 4, 0) == SOCKET_ERROR) return false;
            if (send(sock, block.c_str(), block.length(), 0) == SOCKET_ERROR) return false;
        }
        return true;
    }

    bool SendESPSettings(bool showGeneric, bool showAll, const std::map<std::string, std::vector<float>>& specificMobs) {
        if (!connected) return false;
        
        int header = htonl(0xE581);
        if (send(sock, (char*)&header, 4, 0) == SOCKET_ERROR) return false;

        char flags[2] = { (char)(showGeneric ? 1 : 0), (char)(showAll ? 1 : 0) };
        if (send(sock, flags, 2, 0) == SOCKET_ERROR) return false;

        int count = htonl(specificMobs.size());
        if (send(sock, (char*)&count, 4, 0) == SOCKET_ERROR) return false;

        for (const auto& kv : specificMobs) {
            std::string name = kv.first;
            int len = htonl(name.length());
            if (send(sock, (char*)&len, 4, 0) == SOCKET_ERROR) return false;
            if (send(sock, name.c_str(), name.length(), 0) == SOCKET_ERROR) return false;
        }
        return true;
    }

    int VKToGLFW(int vk) {
        if (vk >= '0' && vk <= '9') return vk;
        if (vk >= 'A' && vk <= 'Z') return vk;
        switch (vk) {
            case VK_SPACE: return 32;
            case VK_OEM_7: return 39; // '
            case VK_OEM_COMMA: return 44; // ,
            case VK_OEM_MINUS: return 45; // -
            case VK_OEM_PERIOD: return 46; // .
            case VK_OEM_2: return 47; // /
            case VK_OEM_1: return 59; // ;
            case VK_OEM_PLUS: return 61; // =
            case VK_OEM_4: return 91; // [
            case VK_OEM_5: return 92; // \ (backslash)
            case VK_OEM_6: return 93; // ]
            case VK_OEM_3: return 96; // `
            case VK_ESCAPE: return 256;
            case VK_RETURN: return 257;
            case VK_TAB: return 258;
            case VK_BACK: return 259;
            case VK_INSERT: return 260;
            case VK_DELETE: return 261;
            case VK_RIGHT: return 262;
            case VK_LEFT: return 263;
            case VK_DOWN: return 264;
            case VK_UP: return 265;
            case VK_PRIOR: return 266; // PgUp
            case VK_NEXT: return 267; // PgDn
            case VK_HOME: return 268;
            case VK_END: return 269;
            case VK_CAPITAL: return 280;
            case VK_SCROLL: return 281;
            case VK_NUMLOCK: return 282;
            case VK_PRINT: return 283;
            case VK_PAUSE: return 284;
            case VK_F1: return 290;
            case VK_F2: return 291;
            case VK_F3: return 292;
            case VK_F4: return 293;
            case VK_F5: return 294;
            case VK_F6: return 295;
            case VK_F7: return 296;
            case VK_F8: return 297;
            case VK_F9: return 298;
            case VK_F10: return 299;
            case VK_F11: return 300;
            case VK_F12: return 301;
            case VK_F13: return 302;
            case VK_F14: return 303;
            case VK_F15: return 304;
            case VK_F16: return 305;
            case VK_F17: return 306;
            case VK_F18: return 307;
            case VK_F19: return 308;
            case VK_F20: return 309;
            case VK_F21: return 310;
            case VK_F22: return 311;
            case VK_F23: return 312;
            case VK_F24: return 313;
            case VK_LSHIFT: return 340;
            case VK_LCONTROL: return 341;
            case VK_LMENU: return 342; // LAlt
            case VK_LWIN: return 343;
            case VK_RSHIFT: return 344;
            case VK_RCONTROL: return 345;
            case VK_RMENU: return 346; // RAlt
            case VK_RWIN: return 347;
            case VK_APPS: return 348;
            default: return 0;
        }
    }

    bool SendHotkeys(const std::vector<int>& vkKeys) {
        if (!connected) return false;
        
        int header = htonl(0xB14D0);
        if (send(sock, (char*)&header, 4, 0) == SOCKET_ERROR) return false;

        std::vector<int> glfwKeys;
        for (int vk : vkKeys) {
            int glfw = VKToGLFW(vk);
            if (glfw != 0) glfwKeys.push_back(glfw);
        }

        int count = htonl(glfwKeys.size());
        if (send(sock, (char*)&count, 4, 0) == SOCKET_ERROR) return false;

        for (int k : glfwKeys) {
            int kn = htonl(k);
            if (send(sock, (char*)&kn, 4, 0) == SOCKET_ERROR) return false;
        }
        return true;
    }

    bool ReadPacket(GameData& data) {
        if (!connected) return false;

        bool gotFrameUpdate = false;

        // Loop to process all pending packets
        while (true) {
            unsigned long bytesAvailable = 0;
            ioctlsocket(sock, FIONREAD, &bytesAvailable);
            
            // If no data and we haven't updated yet, wait a bit? 
            // No, ReadPacket is called per frame. We check what's there.
            if (bytesAvailable < 4) break;

            int header;
            int r = recv(sock, (char*)&header, 4, 0);
            if (r <= 0) {
                connected = false;
                closesocket(sock);
                sock = INVALID_SOCKET;
                return false;
            }

            header = ntohl(header);

            // Helpers with error checking
            bool readError = false;
            auto tStart = std::chrono::high_resolution_clock::now();
            auto readFloat = [&](float& f) {
                if (readError) return;
                int i; 
                int rr = recv(sock, (char*)&i, 4, 0);
                if (rr != 4) { readError = true; return; }
                i = ntohl(i); memcpy(&f, &i, 4);
            };
            auto readDouble = [&](double& d) {
                if (readError) return;
                long long l; 
                int rr = recv(sock, (char*)&l, 8, 0);
                if (rr != 8) { readError = true; return; }
                l = _byteswap_uint64(l);
                memcpy(&d, &l, 8);
            };
            auto readInt = [&](int& i) {
                if (readError) return;
                int rr = recv(sock, (char*)&i, 4, 0);
                if (rr != 4) { readError = true; return; }
                i = ntohl(i);
            };
            auto readByte = [&](char& b) {
                if (readError) return;
                int rr = recv(sock, &b, 1, 0);
                if (rr != 1) { readError = true; return; }
            };
            auto readString = [&](std::string& s) {
                if (readError) return;
                int len; readInt(len);
                if (readError) return;
                
                if (len > 32767 || len < 0) { // Sanity check
                    std::cout << "[Network] Error: String length " << len << " out of bounds." << std::endl;
                    readError = true;
                    return;
                }

                if (len > 0) {
                    s.resize(len);
                    int total = 0;
                    while (total < len) {
                        int rr = recv(sock, &s[total], len - total, 0);
                        if (rr <= 0) { readError = true; break; }
                        total += rr;
                    }
                } else s = "";
            };

            if (header == 0xCAFEBABE) { // Frame Data
                readFloat(data.camYaw);
                readFloat(data.camPitch);
                readDouble(data.camX);
                readDouble(data.camY);
                readDouble(data.camZ);
                readFloat(data.fov);

                char screenStatus;
                readByte(screenStatus);
                data.isScreenOpen = (screenStatus != 0);

                readInt(data.targetedEntityId);

                int count;
                readInt(count);
                if (!readError && (count < 0 || count > 100000)) { // Sanity
                    std::cout << "[Network] Error: Entity count " << count << " unreasonable." << std::endl;
                    readError = true; 
                }

                if (!readError) {
                    data.entities.clear();
                    data.entities.reserve(count); // Phase 2: Reserve Space
                    currentFrame++;

                    for (int i = 0; i < count; i++) {
                        if (readError) break;
                        
                        char type;
                        readByte(type);
                        
                        Entity* ePtr = nullptr;

                        if (type == 0 || type == 1) { // Full Update (0=Player, 1=Mob)
                            Entity e;
                            e.isPlayer = (type == 0);

                            readInt(e.id);
                            readFloat(e.x);
                            readFloat(e.y);
                            readFloat(e.z);
                            readFloat(e.w);
                            readFloat(e.h);

                            readString(e.name);
                            readInt(e.ping);
                            readFloat(e.health);
                            readFloat(e.maxHealth);
                            readFloat(e.absorption);

                            for (int j = 0; j < 6; j++) {
                                if (readError) break;
                                Item item;
                                readString(item.id);
                                if (!readError && !item.id.empty()) {
                                    readInt(item.count);
                                    readInt(item.maxDamage);
                                    readInt(item.damage);
                                    int enchCount;
                                    readInt(enchCount);
                                    if (!readError && (enchCount < 0 || enchCount > 100)) { readError = true; } // Sanity
                                    
                                    for (int k = 0; k < enchCount; k++) {
                                        if (readError) break;
                                        Enchantment ench;
                                        readString(ench.abbr);
                                        readInt(ench.level);
                                        item.enchants.push_back(ench);
                                    }
                                }
                                e.items.push_back(item);
                            }
                            
                            // Update Cache
                            entityCache[e.id] = e;
                            ePtr = &entityCache[e.id];

                        } else if (type == 2) { // Pos Only
                            int id;
                            readInt(id);
                            float x, y, z;
                            readFloat(x); readFloat(y); readFloat(z);

                            if (entityCache.count(id)) {
                                ePtr = &entityCache[id];
                                ePtr->x = x; ePtr->y = y; ePtr->z = z;
                            }
                        }

                        if (ePtr) {
                            Entity& e = *ePtr;
                            e.lastFrameSeen = currentFrame;

                            // Phase 1: Pre-Calculation of Colors & Status
                            e.shouldRender = false;
                            e.shouldRenderNametag = false;
                            e.cachedColor = 0xFFFFFFFF;

                            if (e.isPlayer) {
                                if (playerEspModule && playerEspModule->enabled) {
                                    float* col = playerEspModule->GetColor(e.name);
                                    if (col) {
                                        e.shouldRender = true;
                                        e.cachedColor = IM_COL32((int)(col[0]*255), (int)(col[1]*255), (int)(col[2]*255), 255);
                                    }
                                }
                                if (nametagsModule && nametagsModule->enabled) {
                                    e.shouldRenderNametag = true;
                                }
                            } else {
                                if (espModule && espModule->enabled) {
                                    float* col = espModule->GetColor(e.name);
                                    if (col) {
                                        e.shouldRender = true;
                                        e.cachedColor = IM_COL32((int)(col[0]*255), (int)(col[1]*255), (int)(col[2]*255), 255);
                                    }
                                }
                            }

                            data.entities.push_back(e);
                        }
                    }
                    
                    // GC: Remove old entities every 600 frames (~10s at 60fps)
                    if (currentFrame % 600 == 0) {
                        for (auto it = entityCache.begin(); it != entityCache.end(); ) {
                            if (it->second.lastFrameSeen < currentFrame - 600) {
                                it = entityCache.erase(it);
                            } else {
                                ++it;
                            }
                        }
                    }

                    auto tEnd = std::chrono::high_resolution_clock::now();
                    totalParseTime += std::chrono::duration_cast<std::chrono::microseconds>(tEnd - tStart).count();
                    parseFrames++;

                    auto now = std::chrono::steady_clock::now();
                    if (std::chrono::duration_cast<std::chrono::seconds>(now - lastDebugTime).count() >= 1) {
                        double avgParse = (totalParseTime / (double)parseFrames) / 1000.0;
                        printf("[Perf] C++: Parse=%.2fms, Entities=%d\n", avgParse, (int)data.entities.size());
                        lastDebugTime = now;
                        totalParseTime = 0;
                        parseFrames = 0;
                    }
                }
                
                if (!readError) gotFrameUpdate = true;

            } else if (header == 0xBE0C4D0) { // Block Updates
                    int count;
                    readInt(count);
                    
                    if (!readError && (count < 0 || count > 1000000)) { // Sanity (up to 1M updates is theoretically possible but risky, let's say 100k)
                         std::cout << "[Network] Error: Block update count " << count << " unreasonable." << std::endl;
                         readError = true;
                    }

                    if (!readError) {
                        for(int i=0; i<count; i++) {
                            if (readError) break;
                            char type;
                            readByte(type);
                            int x, y, z;
                            readInt(x); readInt(y); readInt(z);
                            
                            BlockUpdate bu;
                            bu.x = x; bu.y = y; bu.z = z;
                            if (type == 0) { // Add
                                bu.remove = false;
                                readString(bu.id);
                            } else { // Remove
                                bu.remove = true;
                            }
                            data.blockUpdates.push_back(bu);
                        }
                    }
                }
                else if (header == 0x0C1EA400) { // CLEAR ALL
                    data.shouldClearBlocks = true;
                }
                else if (header == 0xB10CDE1) { // Delete Block Type
                    std::string blockId;
                    readString(blockId);
                    if (!readError) {
                        data.blocksToDelete.push_back(blockId);
                    }
                }
                else if (header == 0xC400000) { // Chunk Unload
                    int cx, cz;
                    readInt(cx);
                    readInt(cz);
                    if (!readError) {
                        data.chunksToUnload.push_back({cx, cz});
                    }
                }
                else if (header == 0xCB14D) { // Hotkey Pressed
                    int key;
                    readInt(key);
                    if (!readError) {
                        data.hotkeysPressed.push_back(key);
                    }
                }
                else {
                    // Desync
                    std::cout << "[Network] Error: Unknown Packet Header 0x" << std::hex << header << std::dec << std::endl;
                    connected = false;
                    closesocket(sock);
                    sock = INVALID_SOCKET;
                    return false;
                }

                if (readError) {
                    std::cout << "[Network] Error: Read failure during packet body." << std::endl;
                    connected = false;
                    closesocket(sock);
                    sock = INVALID_SOCKET;
                    return false;
                }
            }
            return gotFrameUpdate;
        }
};
