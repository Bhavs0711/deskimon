/**
 * HTTP Control Server for Deskimon Simulator
 * Simple REST API for external control
 */

#include "http_control.h"
#include "hal_sim.h"
#include "dsk_emotion.h"
#include "dsk_overlay.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

/* External functions from main.c */
extern void set_emotion_external(dsk_emotion_t emotion);
extern void show_overlay_external(dsk_overlay_kind_t kind, const char *title, const char *subtitle);
extern dsk_emotion_t get_current_emotion(void);

static int server_socket = -1;
static pthread_t server_thread;
static bool server_running = false;

/* Parse simple JSON (basic key-value extraction) */
static bool get_json_value(const char *json, const char *key, char *out, size_t out_len)
{
    char search[64];
    snprintf(search, sizeof(search), "\"%s\"", key);
    
    const char *start = strstr(json, search);
    if (!start) return false;
    
    start = strchr(start + strlen(search), ':');
    if (!start) return false;
    
    /* Skip whitespace and quotes */
    while (*start && (*start == ' ' || *start == ':' || *start == '"')) start++;
    
    const char *end = start;
    while (*end && *end != '"' && *end != ',' && *end != '}') end++;
    
    size_t len = end - start;
    if (len == 0 || len >= out_len) return false;
    strncpy(out, start, len);
    out[len] = '\0';
    
    return true;
}

/* Handle HTTP request */
static void handle_request(int client_socket, const char *request)
{
    char response[1024];
    const char *body_start = strstr(request, "\r\n\r\n");
    if (!body_start) body_start = strstr(request, "\n\n");
    if (body_start) body_start += 4;
    
    /* POST /emotion - Set emotion by name */
    if (strstr(request, "POST /emotion") && !strstr(request, "/emotion/id")) {
        if (body_start) {
            char emotion_name[256];
            if (get_json_value(body_start, "emotion", emotion_name, sizeof(emotion_name))) {
                dsk_emotion_t emotion;
                if (dsk_emotion_from_name(emotion_name, &emotion)) {
                    set_emotion_external(emotion);
                    snprintf(response, sizeof(response),
                            "HTTP/1.1 200 OK\r\n"
                            "Content-Type: application/json\r\n"
                            "Access-Control-Allow-Origin: *\r\n\r\n"
                            "{\"status\":\"ok\",\"emotion\":\"%s\"}\n", emotion_name);
                } else {
                    snprintf(response, sizeof(response),
                            "HTTP/1.1 400 Bad Request\r\n"
                            "Content-Type: application/json\r\n\r\n"
                            "{\"status\":\"error\",\"message\":\"Unknown emotion\"}\n");
                }
            } else {
                snprintf(response, sizeof(response),
                        "HTTP/1.1 400 Bad Request\r\n"
                        "Content-Type: application/json\r\n\r\n"
                        "{\"status\":\"error\",\"message\":\"Missing emotion field\"}\n");
            }
        } else {
            snprintf(response, sizeof(response),
                    "HTTP/1.1 400 Bad Request\r\n"
                    "Content-Type: application/json\r\n\r\n"
                    "{\"status\":\"error\",\"message\":\"Missing body\"}\n");
        }
    }
    /* POST /emotion/id - Set emotion by ID */
    else if (strstr(request, "POST /emotion/id")) {
        if (body_start) {
            char id_str[32];
            if (get_json_value(body_start, "id", id_str, sizeof(id_str))) {
                int id = atoi(id_str);
                if (id >= 0 && id < DSK_EMO_COUNT) {
                    set_emotion_external((dsk_emotion_t)id);
                    snprintf(response, sizeof(response),
                            "HTTP/1.1 200 OK\r\n"
                            "Content-Type: application/json\r\n"
                            "Access-Control-Allow-Origin: *\r\n\r\n"
                            "{\"status\":\"ok\",\"emotion_id\":%d}\n", id);
                } else {
                    snprintf(response, sizeof(response),
                            "HTTP/1.1 400 Bad Request\r\n"
                            "Content-Type: application/json\r\n\r\n"
                            "{\"status\":\"error\",\"message\":\"Invalid emotion ID\"}\n");
                }
            } else {
                snprintf(response, sizeof(response),
                        "HTTP/1.1 400 Bad Request\r\n"
                        "Content-Type: application/json\r\n\r\n"
                        "{\"status\":\"error\",\"message\":\"Missing id field\"}\n");
            }
        } else {
            snprintf(response, sizeof(response),
                    "HTTP/1.1 400 Bad Request\r\n"
                    "Content-Type: application/json\r\n\r\n"
                    "{\"status\":\"error\",\"message\":\"Missing body\"}\n");
        }
    }
    /* POST /overlay - Show overlay */
    else if (strstr(request, "POST /overlay")) {
        if (body_start) {
            char title[256];
            char subtitle[256];
            char kind_str[64];
            
            if (get_json_value(body_start, "title", title, sizeof(title)) &&
                get_json_value(body_start, "subtitle", subtitle, sizeof(subtitle))) {
                dsk_overlay_kind_t kind = DSK_OVL_INFO;
                if (get_json_value(body_start, "kind", kind_str, sizeof(kind_str))) {
                    if (strcmp(kind_str, "meeting") == 0) kind = DSK_OVL_MEETING;
                    else if (strcmp(kind_str, "reminder") == 0) kind = DSK_OVL_REMINDER;
                    else if (strcmp(kind_str, "alarm") == 0) kind = DSK_OVL_ALARM;
                    else if (strcmp(kind_str, "email") == 0) kind = DSK_OVL_EMAIL;
                    else if (strcmp(kind_str, "status") == 0) kind = DSK_OVL_STATUS;
                    else if (strcmp(kind_str, "processing") == 0) kind = DSK_OVL_PROCESSING;
                }
                
                show_overlay_external(kind, title, subtitle);
                snprintf(response, sizeof(response),
                        "HTTP/1.1 200 OK\r\n"
                        "Content-Type: application/json\r\n"
                        "Access-Control-Allow-Origin: *\r\n\r\n"
                        "{\"status\":\"ok\",\"overlay\":\"shown\"}\n");
            } else {
                snprintf(response, sizeof(response),
                        "HTTP/1.1 400 Bad Request\r\n"
                        "Content-Type: application/json\r\n\r\n"
                        "{\"status\":\"error\",\"message\":\"Missing title or subtitle\"}\n");
            }
        } else {
            snprintf(response, sizeof(response),
                    "HTTP/1.1 400 Bad Request\r\n"
                    "Content-Type: application/json\r\n\r\n"
                    "{\"status\":\"error\",\"message\":\"Missing body\"}\n");
        }
    }
    /* GET /status - Get current status */
    else if (strstr(request, "GET /status")) {
        dsk_emotion_t emotion = get_current_emotion();
        snprintf(response, sizeof(response),
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: application/json\r\n"
                "Access-Control-Allow-Origin: *\r\n\r\n"
                "{\"emotion_id\":%d,\"emotion_name\":\"%s\"}\n",
                emotion, dsk_emotion_name(emotion));
    }
    /* OPTIONS - CORS preflight */
    else if (strstr(request, "OPTIONS ")) {
        snprintf(response, sizeof(response),
                "HTTP/1.1 204 No Content\r\n"
                "Access-Control-Allow-Origin: *\r\n"
                "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
                "Access-Control-Allow-Headers: Content-Type\r\n\r\n");
    }
    /* 404 Not Found */
    else {
        snprintf(response, sizeof(response),
                "HTTP/1.1 404 Not Found\r\n"
                "Content-Type: application/json\r\n\r\n"
                "{\"status\":\"error\",\"message\":\"Endpoint not found\"}\n");
    }
    
    send(client_socket, response, strlen(response), 0);
}

/* Server thread */
static void* server_thread_func(void *arg)
{
    int port = *(int*)arg;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        printf("[HTTP] Failed to create socket\n");
        return NULL;
    }
    
    int opt = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);
    
    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        printf("[HTTP] Failed to bind to port %d\n", port);
        close(server_socket);
        return NULL;
    }
    
    if (listen(server_socket, 5) < 0) {
        printf("[HTTP] Failed to listen\n");
        close(server_socket);
        return NULL;
    }
    
    printf("[HTTP] Control server listening on http://localhost:%d\n", port);
    printf("[HTTP] Endpoints:\n");
    printf("[HTTP]   POST /emotion {\"emotion\": \"happy\"}\n");
    printf("[HTTP]   POST /overlay {\"title\": \"Task Done\", \"subtitle\": \"Completed\", \"kind\": \"status\"}\n");
    printf("[HTTP]   GET  /status\n\n");
    fflush(stdout);
    
    while (server_running) {
        int client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &client_len);
        if (client_socket < 0) {
            if (server_running) {
                printf("[HTTP] Accept failed\n");
            }
            continue;
        }
        
        char buffer[2048];
        ssize_t bytes_read = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
        if (bytes_read > 0) {
            buffer[bytes_read] = '\0';
            handle_request(client_socket, buffer);
        }
        
        close(client_socket);
    }
    
    close(server_socket);
    return NULL;
}

bool http_control_start(int port)
{
    if (server_running) return false;
    
    server_running = true;
    static int port_arg;
    port_arg = port;
    
    if (pthread_create(&server_thread, NULL, server_thread_func, &port_arg) != 0) {
        server_running = false;
        return false;
    }
    
    return true;
}

void http_control_stop(void)
{
    if (!server_running) return;
    
    server_running = false;
    if (server_socket >= 0) {
        shutdown(server_socket, SHUT_RDWR);
        close(server_socket);
    }
    pthread_join(server_thread, NULL);
}
