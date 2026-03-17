#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <stdbool.h>
#include <pthread.h>
#include "web_server.h"
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

// ✅ 核心修复：引入统一的全局变量头文件，替代所有手写的 extern 声明
#include "global_vars.h"

#include "cjson.h"
#include "db_manager.h"
#include "audio_hint.h"

#define PORT 8080
#define BUFFER_SIZE 4096

// 全局套接字描述符 (用于优雅退出)
int server_socket = -1;

// 函数声明
void* handle_client(void* arg);
void send_response(int client_sock, const char* status, const char* message, const char* data_json);
void parse_request(int client_sock, char* buffer);

// 启动 Web 服务器
void start_web_server() {
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len;
    pthread_t thread_id;

    // 初始化 Winsock (Windows 特有)
    #ifdef _WIN32
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            printf("[ERROR] WSAStartup failed.\n");
            return;
        }
    #endif

    // 创建套接字
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == -1) {
        perror("Socket creation failed");
        return;
    }

    // 设置地址重用 (防止重启时报 Address already in use)
    int opt = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

    // 配置服务器地址
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    // 绑定
    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        #ifdef _WIN32
            closesocket(server_socket);
            WSACleanup();
        #else
            close(server_socket);
        #endif
        return;
    }

    printf("[INFO] Server listening on port %d...\n", PORT);

    // 监听
    if (listen(server_socket, 10) < 0) {
        perror("Listen failed");
        return;
    }

    // 主循环
    while (g_system_running) { // 使用全局变量控制循环
        client_len = sizeof(client_addr);
        int client_sock = accept(server_socket, (struct sockaddr*)&client_addr, &client_len);

        if (client_sock < 0) {
            if (g_reset_requested) break; // 如果收到重置信号则退出
            perror("Accept failed");
            continue;
        }

        printf("[INFO] New connection from %s\n", inet_ntoa(client_addr.sin_addr));

        // 创建线程处理客户端
        if (pthread_create(&thread_id, NULL, handle_client, (void*)&client_sock) != 0) {
            perror("Could not create thread");
            #ifdef _WIN32
                closesocket(client_sock);
            #else
                close(client_sock);
            #endif
        } else {
            pthread_detach(thread_id); // 分离线程，自动释放资源
        }
    }

    // 清理
    #ifdef _WIN32
        closesocket(server_socket);
        WSACleanup();
    #else
        close(server_socket);
    #endif
    printf("[INFO] Web Server stopped.\n");
}

// 客户端处理线程
void* handle_client(void* arg) {
    int client_sock = *(int*)arg;
    char buffer[BUFFER_SIZE] = {0};
    char response[BUFFER_SIZE] = {0};

    // 读取请求
    int valread = recv(client_sock, buffer, BUFFER_SIZE, 0);
    if (valread <= 0) {
        #ifdef _WIN32
            closesocket(client_sock);
        #else
            close(client_sock);
        #endif
        return NULL;
    }

    // 简单解析 HTTP 请求 (只处理 GET)
    if (strncmp(buffer, "GET", 3) == 0) {
        parse_request(client_sock, buffer);
    } else {
        send_response(client_sock, "405 Method Not Allowed", "Only GET supported", NULL);
    }

    // 关闭连接
    #ifdef _WIN32
        closesocket(client_sock);
    #else
        close(client_sock);
    #endif

    return NULL;
}

// 解析请求路径并响应
void parse_request(int client_sock, char* buffer) {
    char path[256] = {0};
    // 提取路径 (例如: GET /api/status HTTP/1.1)
    sscanf(buffer, "GET %255s", path);

    // 去除末尾可能存在的 HTTP 版本或参数
    char* space = strchr(path, ' ');
    if (space) *space = '\0';

    cJSON* root = cJSON_CreateObject();
    char* json_str = NULL;

    // 路由处理
    if (strcmp(path, "/api/status") == 0) {
        // 获取实时数据 (注意加锁保护)
        pthread_mutex_lock(&g_users[0].mutex);
        cJSON_AddNumberToObject(root, "floor", g_users[0].current_floor);
        cJSON_AddNumberToObject(root, "steps", g_users[0].total_steps);
        cJSON_AddNumberToObject(root, "speed", g_users[0].speed_per_minute);
        pthread_mutex_unlock(&g_users[0].mutex);

        cJSON_AddBoolToObject(root, "running", g_system_state.simulation_running);
        cJSON_AddNumberToObject(root, "max_floors", g_max_floors);

        json_str = cJSON_PrintUnformatted(root);
        send_response(client_sock, "200 OK", "Success", json_str);
        free(json_str);

    } else if (strcmp(path, "/api/reset") == 0) {
        // 触发重置
        g_reset_requested = true;
        reset_simulation(); // 调用全局重置函数

        cJSON_AddStringToObject(root, "status", "reset_triggered");
        json_str = cJSON_PrintUnformatted(root);
        send_response(client_sock, "200 OK", "Reset Command Received", json_str);
        free(json_str);

    } else if (strcmp(path, "/") == 0 || strcmp(path, "/index.html") == 0) {
        // 返回简单的 HTML 页面 (实际项目中应读取 index.html 文件)
        const char* html = "<html><body><h1>Smart Stair Climbing Sim</h1><p>Status: Running</p><script>setTimeout(()=>location.reload(), 2000);</script></body></html>";

        char http_header[512];
        sprintf(http_header,
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/html\r\n"
            "Content-Length: %zu\r\n"
            "Connection: close\r\n\r\n", strlen(html));

        send(client_sock, http_header, strlen(http_header), 0);
        send(client_sock, html, strlen(html), 0);

        cJSON_Delete(root);
        return;

    } else {
        cJSON_AddStringToObject(root, "error", "404 Not Found");
        json_str = cJSON_PrintUnformatted(root);
        send_response(client_sock, "404 Not Found", "Resource not found", json_str);
        free(json_str);
    }

    cJSON_Delete(root);
}

// 发送标准 HTTP 响应
void send_response(int client_sock, const char* status, const char* message, const char* data_json) {
    char header[1024];
    int content_length = data_json ? strlen(data_json) : 0;

    sprintf(header,
        "HTTP/1.1 %s\r\n"
        "Content-Type: application/json\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n\r\n",
        status, content_length);

    send(client_sock, header, strlen(header), 0);
    if (data_json) {
        send(client_sock, data_json, content_length, 0);
    }
}