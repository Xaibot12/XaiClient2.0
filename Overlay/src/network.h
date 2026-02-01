#pragma once
#include <winsock2.h>
#include <vector>
#include <iostream>

#pragma comment(lib, "ws2_32.lib")

struct Entity {
    int id;
    float x, y, z; // Relative to camera
    float w, h;
};

struct GameData {
    float camYaw, camPitch;
    std::vector<Entity> entities;
};

class NetworkClient {
    SOCKET sock;
    bool connected = false;

public:
    NetworkClient() : sock(INVALID_SOCKET) {}

    bool Connect() {
        if (connected) return true;

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

            readFloat(data.camYaw);
            readFloat(data.camPitch);

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
                data.entities.push_back(e);
            }
            hasNewData = true;
        }

        return hasNewData;
    }
};
