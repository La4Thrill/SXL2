#include "web_server.h"
#include "global_vars.h"
#include "simulator.h"
#include "db_manager.h"
#include "cjson.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#endif

#define SERVER_PORT 8080
#define HTTP_BUFFER_SIZE 8192

static int g_server_socket = -1;

static FILE* open_text_with_fallback(const char* path) {
    FILE* fp = fopen(path, "rb");
    if (fp) {
        return fp;
    }

    char candidate[320];
    snprintf(candidate, sizeof(candidate), "..\\%s", path);
    fp = fopen(candidate, "rb");
    if (fp) {
        return fp;
    }

    snprintf(candidate, sizeof(candidate), "..\\..\\%s", path);
    fp = fopen(candidate, "rb");
    if (fp) {
        return fp;
    }

    return NULL;
}

static void close_socket_safe(int sock) {
#ifdef _WIN32
    closesocket(sock);
#else
    close(sock);
#endif
}

static void send_text_response(int client_sock, const char* status, const char* content_type, const char* body) {
    if (!body) {
        body = "";
    }

    char header[512];
    int len = (int)strlen(body);

    snprintf(header, sizeof(header),
        "HTTP/1.1 %s\r\n"
        "Content-Type: %s\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n\r\n",
        status, content_type, len);

    send(client_sock, header, (int)strlen(header), 0);
    if (len > 0) {
        send(client_sock, body, len, 0);
    }
}

static void send_json_response(int client_sock, const char* status, cJSON* root) {
    char* json = cJSON_PrintUnformatted(root);
    send_text_response(client_sock, status, "application/json", json ? json : "{}");
    if (json) {
        free(json);
    }
}

static void serve_index_html(int client_sock) {
    FILE* fp = open_text_with_fallback("index.html");
    if (!fp) {
        send_text_response(client_sock, "404 Not Found", "text/plain", "index.html not found");
        return;
    }

    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (size < 0 || size > 2 * 1024 * 1024) {
        fclose(fp);
        send_text_response(client_sock, "500 Internal Server Error", "text/plain", "invalid file size");
        return;
    }

    char* content = (char*)malloc((size_t)size + 1);
    if (!content) {
        fclose(fp);
        send_text_response(client_sock, "500 Internal Server Error", "text/plain", "malloc failed");
        return;
    }

    fread(content, 1, (size_t)size, fp);
    content[size] = '\0';
    fclose(fp);

    send_text_response(client_sock, "200 OK", "text/html; charset=utf-8", content);
    free(content);
}

static void handle_get(int client_sock, const char* path) {
    if (strcmp(path, "/") == 0 || strcmp(path, "/index.html") == 0) {
        serve_index_html(client_sock);
        return;
    }

    if (strncmp(path, "/api/status", 11) == 0) {
        cJSON* root = cJSON_CreateObject();

        pthread_mutex_lock(&g_users[0].mutex);
        cJSON_AddStringToObject(root, "user", g_users[0].username);
        cJSON_AddStringToObject(root, "device_id", g_users[0].device_id);
        cJSON_AddNumberToObject(root, "current_floor", g_users[0].current_floor);
        cJSON_AddNumberToObject(root, "climbed", g_users[0].total_steps);
        cJSON_AddNumberToObject(root, "speed", g_users[0].speed_per_minute);
        cJSON_AddNumberToObject(root, "file_lines", g_users[0].sent_lines);
        pthread_mutex_unlock(&g_users[0].mutex);

        cJSON_AddBoolToObject(root, "running", sim_is_running());
        cJSON_AddNumberToObject(root, "max_floors", g_max_floors);

        send_json_response(client_sock, "200 OK", root);
        cJSON_Delete(root);
        return;
    }

    if (strncmp(path, "/api/history", 12) == 0) {
        char* json = get_recent_history_json(10);
        send_text_response(client_sock, "200 OK", "application/json", json ? json : "[]");
        if (json) {
            free(json);
        }
        return;
    }

    if (strcmp(path, "/api/reset") == 0) {
        g_reset_requested = true;
        cJSON* root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "status", "reset_requested");
        send_json_response(client_sock, "200 OK", root);
        cJSON_Delete(root);
        return;
    }

    send_text_response(client_sock, "404 Not Found", "application/json", "{\"error\":\"not found\"}");
}

static void handle_post(int client_sock, const char* path) {
    cJSON* root = cJSON_CreateObject();

    if (strcmp(path, "/start") == 0) {
        sim_start();
        cJSON_AddStringToObject(root, "status", "started");
        send_json_response(client_sock, "200 OK", root);
    } else if (strcmp(path, "/stop") == 0) {
        sim_stop();
        cJSON_AddStringToObject(root, "status", "stopped");
        send_json_response(client_sock, "200 OK", root);
    } else if (strcmp(path, "/reset") == 0) {
        g_reset_requested = true;
        cJSON_AddStringToObject(root, "status", "reset_requested");
        send_json_response(client_sock, "200 OK", root);
    } else {
        cJSON_AddStringToObject(root, "error", "not found");
        send_json_response(client_sock, "404 Not Found", root);
    }

    cJSON_Delete(root);
}

static void* handle_client(void* arg) {
    int client_sock = *(int*)arg;
    free(arg);

    char buffer[HTTP_BUFFER_SIZE];
    memset(buffer, 0, sizeof(buffer));

    int bytes = recv(client_sock, buffer, sizeof(buffer) - 1, 0);
    if (bytes <= 0) {
        close_socket_safe(client_sock);
        return NULL;
    }

    char method[8] = {0};
    char path[256] = {0};
    sscanf(buffer, "%7s %255s", method, path);

    char* q = strchr(path, '?');
    if (q) {
        *q = '\0';
    }

    if (strcmp(method, "GET") == 0) {
        handle_get(client_sock, path);
    } else if (strcmp(method, "POST") == 0) {
        handle_post(client_sock, path);
    } else if (strcmp(method, "OPTIONS") == 0) {
        send_text_response(client_sock, "200 OK", "text/plain", "");
    } else {
        send_text_response(client_sock, "405 Method Not Allowed", "application/json", "{\"error\":\"method not allowed\"}");
    }

    close_socket_safe(client_sock);
    return NULL;
}

void start_web_server(void) {
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        fprintf(stderr, "[WEB] WSAStartup failed\n");
        return;
    }
#endif

    g_server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (g_server_socket < 0) {
        fprintf(stderr, "[WEB] socket create failed\n");
#ifdef _WIN32
        WSACleanup();
#endif
        return;
    }

    int opt = 1;
    setsockopt(g_server_socket, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(SERVER_PORT);

    if (bind(g_server_socket, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "[WEB] bind failed\n");
        close_socket_safe(g_server_socket);
        g_server_socket = -1;
#ifdef _WIN32
        WSACleanup();
#endif
        return;
    }

    if (listen(g_server_socket, 16) < 0) {
        fprintf(stderr, "[WEB] listen failed\n");
        close_socket_safe(g_server_socket);
        g_server_socket = -1;
#ifdef _WIN32
        WSACleanup();
#endif
        return;
    }

    printf("[WEB] listening on :%d\n", SERVER_PORT);

    while (g_system_running) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_sock = accept(g_server_socket, (struct sockaddr*)&client_addr, &client_len);
        if (client_sock < 0) {
            if (!g_system_running) {
                break;
            }
            continue;
        }

        int* sock_arg = (int*)malloc(sizeof(int));
        if (!sock_arg) {
            close_socket_safe(client_sock);
            continue;
        }
        *sock_arg = client_sock;

        pthread_t tid;
        if (pthread_create(&tid, NULL, handle_client, sock_arg) == 0) {
            pthread_detach(tid);
        } else {
            free(sock_arg);
            close_socket_safe(client_sock);
        }
    }

    stop_web_server();
}

void stop_web_server(void) {
    if (g_server_socket >= 0) {
        close_socket_safe(g_server_socket);
        g_server_socket = -1;
    }
#ifdef _WIN32
    WSACleanup();
#endif
}
