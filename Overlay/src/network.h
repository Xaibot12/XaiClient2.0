#pragma once
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>
#include <vector>
#include <iostream>
#include <algorithm>
#include <intrin.h>
#include "Module.h"

#pragma comment(lib, "ws2_32.lib")

struct Enchantment {
    std::string abbr;
    int level;
};

struct Item {
    std::string id;
    int count;
    int maxDamage;
    int damage;
    std::vector<Enchantment> enchants;
};

struct Entity {
    int id;
    float x, y, z; // Relative to camera
    float w, h;
    std::string name;
    int ping;
    float health, maxHealth, absorption;
    std::vector<Item> items;
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
    bool shouldClearBlocks = false;
    std::vector<Entity> entities;
    std::vector<BlockUpdate> blockUpdates;
    std::vector<std::string> blocksToDelete;
};

class NetworkClient {
    SOCKET sock;
    bool connected = false;

public:
    NetworkClient() : sock(INVALID_SOCKET) {}

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

                int count;
                readInt(count);
                if (!readError && (count < 0 || count > 100000)) { // Sanity
                    std::cout << "[Network] Error: Entity count " << count << " unreasonable." << std::endl;
                    readError = true; 
                }

                if (!readError) {
                    data.entities.clear();
                    for (int i = 0; i < count; i++) {
                        if (readError) break;
                        Entity e;
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
                        }
                        data.entities.push_back(e);
                    }
                }
                
                if (!readError) gotFrameUpdate = true;

                } 
                else if (header == 0xBE0C4D0) { // Block Updates
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
