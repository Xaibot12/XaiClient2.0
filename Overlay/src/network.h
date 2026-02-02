#pragma once
#include <winsock2.h>
#include <vector>
#include <iostream>
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

struct GameData {
    float camYaw, camPitch;
    float fov;
    bool isScreenOpen;
    std::vector<Entity> entities;
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
            return true;
        }

        closesocket(sock);
        WSACleanup();
        return false;
    }

    bool ReadPacket(GameData& data) {
        if (!connected) return false;

        // Drain the buffer: Read ALL available packets, keep only the last full one.
        // This prevents latency buildup if the game renders faster than the overlay.
        bool hasNewData = false;
        
        while (true) {
            // Check if data is available without blocking
            unsigned long bytesAvailable = 0;
            ioctlsocket(sock, FIONREAD, &bytesAvailable);
            
            // If no data available, but we haven't read anything yet, block for one packet
            if (bytesAvailable == 0 && !hasNewData) {
                 // Wait for at least one packet
            } else if (bytesAvailable == 0) {
                // We have the latest packet
                break;
            }

            int header;
            int bytes = recv(sock, (char*)&header, 4, 0);
            if (bytes <= 0) {
                connected = false;
                closesocket(sock);
                sock = INVALID_SOCKET;
                return false;
            }

            header = ntohl(header);
            if (header != 0xCAFEBABE) {
                // Desync!
                return false; 
            }

            // Read the rest of the packet
            auto readFloat = [&](float& f) {
                int i;
                recv(sock, (char*)&i, 4, 0);
                i = ntohl(i);
                memcpy(&f, &i, 4);
            };
            
            auto readInt = [&](int& i) {
                recv(sock, (char*)&i, 4, 0);
                i = ntohl(i);
            };

            auto readString = [&](std::string& s) {
                int len;
                readInt(len);
                if (len > 0) {
                    s.resize(len);
                    int total = 0;
                    while (total < len) {
                        int r = recv(sock, &s[total], len - total, 0);
                        if (r <= 0) break; // Should handle error
                        total += r;
                    }
                } else {
                    s = "";
                }
            };

            readFloat(data.camYaw);
            readFloat(data.camPitch);
            readFloat(data.fov);

            char screenStatus;
            recv(sock, &screenStatus, 1, 0);
            data.isScreenOpen = (screenStatus != 0);

            int count;
            readInt(count);

            data.entities.clear();
            for (int i = 0; i < count; i++) {
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
                    Item item;
                    readString(item.id);
                    if (!item.id.empty()) {
                        readInt(item.count);
                        
                        // Durability
                        readInt(item.maxDamage);
                        readInt(item.damage);

                        int enchCount;
                        readInt(enchCount);
                        for (int k = 0; k < enchCount; k++) {
                            Enchantment ench;
                            readString(ench.abbr);
                            readInt(ench.level);
                            item.enchants.push_back(ench);
                        }
                    }
                    e.items.push_back(item);
                }

                data.entities.push_back(e);
            }
            hasNewData = true;
        }

        return hasNewData;
    }

    bool SendState(const std::vector<Module*>& modules) {
        if (!connected) return false;

        // Simple binary protocol
        // 1. Packet ID/Header (0xDEADBEEF)
        int header = htonl(0xDEADBEEF);
        if (send(sock, (char*)&header, 4, 0) == SOCKET_ERROR) return false;

        // 2. Count
        int count = htonl(modules.size());
        if (send(sock, (char*)&count, 4, 0) == SOCKET_ERROR) return false;

        for (auto mod : modules) {
            // Name Length
            int nameLen = htonl(mod->name.length());
            if (send(sock, (char*)&nameLen, 4, 0) == SOCKET_ERROR) return false;
            
            // Name
            if (send(sock, mod->name.c_str(), mod->name.length(), 0) == SOCKET_ERROR) return false;

            // Enabled
            char enabled = mod->enabled ? 1 : 0;
            if (mod->name == "Disable") {
                // If this is the Disable module, we pack the "Fully" setting into the byte
                // 0: Disabled (not active)
                // 1: Active (Disable requested)
                // 2: Active + Fully (Disable + Fully requested)
                // Actually, let's keep it simple. If it's enabled, we check the 'fully' flag.
                // Since Module base class doesn't have 'fully', we might need to cast or send extra data.
                // But the mod expects a generic format.
                // Let's stick to the generic format for now.
                // However, the user wants "Fully" to be sent.
                // So, if "Disable" module is enabled, we need to communicate "Fully".
                // Hack: Append "Fully" to name? No.
                // Hack: Use the enabled byte. 1 = Enabled, 2 = Enabled + Fully.
                // The Mod needs to interpret this.
                // Wait, I can't cast here easily without including Disable.h which might cause circular deps or just be ugly.
                // Let's check main.cpp logic instead. 
                // We will handle the "Disable" packet logic inside main.cpp before sending, 
                // OR we update Module to have a generic integer state? No.
                // Let's use the 'enabled' byte as a bitfield for Disable module.
                // But we need to access 'fully'. 
                // Let's leave this standard for now and handle special Disable packet in main.cpp
            }
            if (send(sock, &enabled, 1, 0) == SOCKET_ERROR) return false;
        }
        return true;
    }

    bool SendDisable(bool fully) {
        if (!connected) return false;
        
        // Custom packet for Shutdown
        // Header: 0xBADF00D
        int header = htonl(0xBADF00D);
        if (send(sock, (char*)&header, 4, 0) == SOCKET_ERROR) return false;

        // Payload: Fully (1 byte)
        char f = fully ? 1 : 0;
        if (send(sock, &f, 1, 0) == SOCKET_ERROR) return false;

        return true;
    }
};
