/**
 * @file 03_tcp_sender.c
 * @brief Standalone Edge Client that streams compressed logs over TCP socket.
 *
 * Framing Protocol:
 * [2 Bytes Magic: "LD" (0x4C 0x44)] [4 Bytes Payload Length (Big Endian)] [Payload Bytes]
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

static void send_all(SOCKET sock, const uint8_t* data, size_t len) {
    size_t total = 0;
    while (total < len) {
        int sent = send(sock, (const char*)data + total, (int)(len - total), 0);
        if (sent <= 0) break;
        total += sent;
    }
}

int main(int argc, char** argv) {
    const char* server_ip = (argc > 1) ? argv[1] : "127.0.0.1";
    int server_port       = (argc > 2) ? atoi(argv[2]) : 9876;
    const char* log_file  = (argc > 3) ? argv[3] : NULL;

#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2,2), &wsa);
#endif

    printf("Connecting to LDS Log Ingestion Server at %s:%d...\n", server_ip, server_port);
    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) {
        fprintf(stderr, "Socket creation failed\n");
        return 1;
    }

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port   = htons(server_port);
    serv_addr.sin_addr.s_addr = inet_addr(server_ip);

    if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == SOCKET_ERROR) {
        fprintf(stderr, "Connection to server failed. Make sure receiver is running first!\n");
        closesocket(sock);
        return 1;
    }

    printf("Connected! Starting real-time compressed streaming...\n");

    LDSEncoder* encoder = lds_encoder_create();
    FILE* fp = log_file ? fopen(log_file, "r") : stdin;
    if (!fp) {
        perror("fopen");
        closesocket(sock);
        return 1;
    }

    char line_buf[4096];
    size_t line_num = 0;
    size_t total_raw = 0;
    size_t total_sent = 0;

    while (fgets(line_buf, sizeof(line_buf), fp)) {
        // Strip newline
        size_t l = strlen(line_buf);
        while (l > 0 && (line_buf[l-1] == '\r' || line_buf[l-1] == '\n')) {
            line_buf[--l] = '\0';
        }
        if (l == 0) continue;

        total_raw += l;
        line_num++;

        // Compress single line
        size_t enc_len = 0;
        uint8_t* payload = lds_encode_line(encoder, line_buf, &enc_len);

        // Frame header: [2B Magic: 0x4C, 0x44] [4B Length]
        uint8_t frame[6];
        frame[0] = 0x4C; // 'L'
        frame[1] = 0x44; // 'D'
        frame[2] = (uint8_t)((enc_len >> 24) & 0xFF);
        frame[3] = (uint8_t)((enc_len >> 16) & 0xFF);
        frame[4] = (uint8_t)((enc_len >> 8)  & 0xFF);
        frame[5] = (uint8_t)(enc_len & 0xFF);

        send_all(sock, frame, 6);
        send_all(sock, payload, enc_len);

        total_sent += (6 + enc_len);
        free(payload);

        if (line_num % 1000 == 0) {
            printf("Streamed %zu lines | Raw: %.2f KB -> Sent: %.2f KB (%.1f%% bandwidth saved)\n",
                   line_num, total_raw / 1024.0, total_sent / 1024.0,
                   (1.0 - (double)total_sent / total_raw) * 100.0);
        }
    }

    printf("\nFinished! Total streamed: %zu lines | Raw: %.2f KB -> Wire: %.2f KB (%.2fx compression)\n",
           line_num, total_raw / 1024.0, total_sent / 1024.0,
           (double)total_raw / total_sent);

    if (fp != stdin) fclose(fp);
    lds_encoder_free(encoder);
    closesocket(sock);
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
