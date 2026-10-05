/**
 * @file 04_tcp_receiver.c
 * @brief Standalone Ingestion Server that receives compressed frames over TCP
 *        and reconstructs exact original log streams in real-time.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../include/lds.h"

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "ws2_32.lib")
  typedef int socklen_t;
#else
  #include <unistd.h>
  #include <sys/types.h>
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #define closesocket close
  #define SOCKET int
  #define INVALID_SOCKET -1
  #define SOCKET_ERROR -1
#endif

static bool recv_exact(SOCKET sock, uint8_t* buf, size_t len) {
    size_t total = 0;
    while (total < len) {
        int r = recv(sock, (char*)buf + total, (int)(len - total), 0);
        if (r <= 0) return false;
        total += r;
    }
    return true;
}

int main(int argc, char** argv) {
    int port = (argc > 1) ? atoi(argv[1]) : 9876;

#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2,2), &wsa);
#endif

    SOCKET server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock == INVALID_SOCKET) {
        fprintf(stderr, "Failed to create socket\n");
        return 1;
    }

    int opt = 1;
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family      = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port        = htons(port);

    if (bind(server_sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == SOCKET_ERROR) {
        fprintf(stderr, "Bind failed on port %d\n", port);
        closesocket(server_sock);
        return 1;
    }

    if (listen(server_sock, 5) == SOCKET_ERROR) {
        fprintf(stderr, "Listen failed\n");
        closesocket(server_sock);
        return 1;
    }

    printf("====================================================\n");
    printf("  LDS Stream Ingestion Receiver Listening on Port %d\n", port);
    printf("====================================================\n");
    printf("Waiting for edge client connections...\n");

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        SOCKET client_sock = accept(server_sock, (struct sockaddr*)&client_addr, &client_len);
        if (client_sock == INVALID_SOCKET) break;

        char* client_ip = inet_ntoa(client_addr.sin_addr);
        printf("\n[CONNECTED] Edge client connected from %s:%d\n", client_ip, ntohs(client_addr.sin_port));

        // Create a new decoder instance for this client stream
        LDSDecoder* decoder = lds_decoder_create();
        size_t lines_received = 0;
        size_t total_payload_bytes = 0;
        size_t total_restored_bytes = 0;

        while (1) {
            // Read frame header: [2B Magic] [4B Length]
            uint8_t header[6];
            if (!recv_exact(client_sock, header, 6)) break;

            if (header[0] != 0x4C || header[1] != 0x44) {
                fprintf(stderr, "[ERROR] Bad frame magic bytes! Disconnecting client.\n");
                break;
            }

            uint32_t payload_len = ((uint32_t)header[2] << 24) |
                                   ((uint32_t)header[3] << 16) |
                                   ((uint32_t)header[4] << 8)  |
                                   ((uint32_t)header[5]);

            if (payload_len == 0 || payload_len > 10 * 1024 * 1024) {
                fprintf(stderr, "[ERROR] Invalid payload length %u\n", payload_len);
                break;
            }

            uint8_t* payload = malloc(payload_len);
            if (!recv_exact(client_sock, payload, payload_len)) {
                free(payload);
                break;
            }

            total_payload_bytes += payload_len;

            // Decompress log line
            char* log_line = lds_decode_line(decoder, payload, payload_len);
            free(payload);

            if (log_line) {
                lines_received++;
                total_restored_bytes += strlen(log_line);

                // Print first 5 and then periodic samples
                if (lines_received <= 5 || lines_received % 1000 == 0) {
                    printf("[%zu] %s\n", lines_received, log_line);
                }
                free(log_line);
            }
        }

        printf("[DISCONNECTED] Client %s finished. Received %zu lines (Compressed Wire: %.2f KB -> Restored: %.2f KB)\n",
               client_ip, lines_received, total_payload_bytes / 1024.0, total_restored_bytes / 1024.0);

        lds_decoder_free(decoder);
        closesocket(client_sock);
    }

    closesocket(server_sock);
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
