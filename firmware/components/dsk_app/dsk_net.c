#include <string.h>
#include <errno.h>
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "arpa/inet.h"
#include "sdkconfig.h"
#include "dsk_net.h"
#include "dsk_store.h"

static const char *TAG = "dsk_net";
#define RX_MAX 4096

static dsk_line_cb_t s_on_line;
static dsk_link_cb_t s_on_link;
static volatile bool s_wifi_up, s_tcp_up, s_force_reconnect;
static int s_sock = -1;
static SemaphoreHandle_t s_tx_lock;
static char s_ssid[33], s_pass[65], s_host[48];
static bool s_have_creds;

static void load_creds(void)
{
    s_ssid[0] = s_pass[0] = s_host[0] = 0;
    dsk_store_get("ssid", s_ssid, sizeof s_ssid);
    dsk_store_get("pass", s_pass, sizeof s_pass);
    dsk_store_get("host", s_host, sizeof s_host);
    if (!s_ssid[0] && CONFIG_DSK_DEFAULT_SSID[0]) {
        strlcpy(s_ssid, CONFIG_DSK_DEFAULT_SSID, sizeof s_ssid);
        strlcpy(s_pass, CONFIG_DSK_DEFAULT_PASS, sizeof s_pass);
    }
    s_have_creds = s_ssid[0] != 0;
}

static void wifi_connect(void)
{
    if (!s_have_creds) return;
    wifi_config_t wc = {0};
    strlcpy((char *)wc.sta.ssid, s_ssid, sizeof wc.sta.ssid);
    strlcpy((char *)wc.sta.password, s_pass, sizeof wc.sta.password);
    wc.sta.threshold.authmode = s_pass[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    wc.sta.pmf_cfg.capable = true;
    esp_wifi_disconnect();
    esp_wifi_set_config(WIFI_IF_STA, &wc);
    esp_wifi_connect();
}

static void wifi_evt(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) { wifi_connect(); }
    else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_wifi_up = false;
        if (s_have_creds) { vTaskDelay(pdMS_TO_TICKS(1500)); esp_wifi_connect(); }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        s_wifi_up = true;
        ip_event_got_ip_t *e = data;
        ESP_LOGI(TAG, "Wi-Fi up, ip " IPSTR, IP2STR(&e->ip_info.ip));
    }
}

bool dsk_net_wifi_up(void) { return s_wifi_up; }
bool dsk_net_tcp_up(void) { return s_tcp_up; }
void dsk_net_force_reconnect(void) { s_force_reconnect = true; }
void dsk_net_reconfigure(void) { load_creds(); wifi_connect(); s_force_reconnect = true; }

bool dsk_net_send_line(const char *line)
{
    if (!s_tcp_up || s_sock < 0) return false;
    bool ok = true;
    xSemaphoreTake(s_tx_lock, portMAX_DELAY);
    size_t n = strlen(line);
    const char nl = '\n';
    for (size_t off = 0; off < n && ok;) {
        int w = send(s_sock, line + off, n - off, 0);
        if (w < 0) { if (errno == EAGAIN || errno == EWOULDBLOCK) { vTaskDelay(pdMS_TO_TICKS(5)); continue; } ok = false; }
        else off += w;
    }
    if (ok && send(s_sock, &nl, 1, 0) < 0) ok = false;
    xSemaphoreGive(s_tx_lock);
    return ok;
}

/* Find the Mac: configured host, else UDP broadcast discovery. Reply: {"type":"agent","port":N} from the Mac's IP. */
static bool discover(struct sockaddr_in *out)
{
    if (s_host[0]) {
        struct addrinfo hints = {.ai_family = AF_INET, .ai_socktype = SOCK_STREAM}, *res = NULL;
        if (getaddrinfo(s_host, NULL, &hints, &res) == 0 && res) {
            memcpy(out, res->ai_addr, sizeof *out); out->sin_port = htons(CONFIG_DSK_TCP_PORT);
            freeaddrinfo(res);
            ESP_LOGI(TAG, "discovered Mac at configured host %s", s_host);
            return true;
        }
        ESP_LOGW(TAG, "configured host %s lookup failed", s_host);
    }
    
    int u = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (u < 0) { ESP_LOGW(TAG, "discovery: socket creation failed"); return false; }
    
    int yes = 1; setsockopt(u, SOL_SOCKET, SO_BROADCAST, &yes, sizeof yes);
    struct timeval tv = {.tv_sec = 2}; setsockopt(u, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    struct sockaddr_in b = {.sin_family = AF_INET, .sin_port = htons(CONFIG_DSK_DISCOVERY_PORT), .sin_addr.s_addr = htonl(INADDR_BROADCAST)};
    const char *hello = "{\"type\":\"discover\",\"device\":\"deskimon\"}";
    
    ESP_LOGD(TAG, "discovery: sending broadcast on UDP :%d", CONFIG_DSK_DISCOVERY_PORT);
    int sent = sendto(u, hello, strlen(hello), 0, (struct sockaddr *)&b, sizeof b);
    if (sent < 0) { ESP_LOGW(TAG, "discovery: sendto failed errno=%d", errno); close(u); return false; }
    
    char buf[128]; struct sockaddr_in from; socklen_t fl = sizeof from;
    int n = recvfrom(u, buf, sizeof buf - 1, 0, (struct sockaddr *)&from, &fl);
    close(u);
    
    if (n <= 0) { ESP_LOGD(TAG, "discovery: timeout waiting for response (errno=%d)", errno); return false; }
    if (!strstr(buf, "\"agent\"")) { ESP_LOGW(TAG, "discovery: invalid response: %.*s", n, buf); return false; }
    
    *out = from; out->sin_port = htons(CONFIG_DSK_TCP_PORT);
    char *p = strstr(buf, "\"port\":");
    if (p) { int port = atoi(p + 7); if (port > 0 && port < 65536) out->sin_port = htons(port); }
    
    char ip_str[INET_ADDRSTRLEN];
    inet_ntoa_r(from.sin_addr, ip_str, sizeof ip_str);
    ESP_LOGI(TAG, "discovered Mac at %s:%u", ip_str, ntohs(out->sin_port));
    return true;
}

static void net_task(void *arg)
{
    static char rx[RX_MAX];
    int backoff = 1000;
    for (;;) {
        if (!s_wifi_up) { vTaskDelay(pdMS_TO_TICKS(500)); continue; }
        
        struct sockaddr_in addr;
        if (!discover(&addr)) { 
            ESP_LOGW(TAG, "discovery failed, backoff %d ms", backoff);
            vTaskDelay(pdMS_TO_TICKS(backoff)); 
            if (backoff < 8000) backoff *= 2; 
            continue; 
        }
        
        int s = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
        if (s < 0) { 
            ESP_LOGW(TAG, "socket creation failed: errno %d", errno);
            vTaskDelay(pdMS_TO_TICKS(backoff)); 
            continue; 
        }
        
        struct timeval tv = {.tv_sec = 5}; 
        setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
        int nd = 1; 
        setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &nd, sizeof nd);
        
        char ip_str[INET_ADDRSTRLEN];
        inet_ntoa_r(addr.sin_addr, ip_str, sizeof ip_str);
        ESP_LOGI(TAG, "TCP: connecting to %s:%u", ip_str, ntohs(addr.sin_port));
        
        if (connect(s, (struct sockaddr *)&addr, sizeof addr) != 0) {
            ESP_LOGW(TAG, "connect failed: errno %d, backoff %d ms", errno, backoff);
            close(s);
            vTaskDelay(pdMS_TO_TICKS(backoff)); 
            if (backoff < 8000) backoff *= 2; 
            continue;
        }
        
        backoff = 1000;
        tv.tv_sec = 1; 
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
        s_sock = s; 
        s_tcp_up = true; 
        s_force_reconnect = false;
        ESP_LOGI(TAG, "TCP connected");
        if (s_on_link) s_on_link(true);

        size_t len = 0;
        while (s_tcp_up && !s_force_reconnect && s_wifi_up) {
            int n = recv(s, rx + len, RX_MAX - len - 1, 0);
            if (n == 0) { 
                ESP_LOGD(TAG, "TCP recv returned 0 (peer closed)"); 
                break; 
            }
            if (n < 0) { 
                if (errno == EAGAIN || errno == EWOULDBLOCK || errno == ETIMEDOUT) continue; 
                ESP_LOGD(TAG, "TCP recv error: errno %d", errno);
                break; 
            }
            len += n; 
            rx[len] = 0;
            char *start = rx, *nl;
            while ((nl = memchr(start, '\n', rx + len - start))) {
                *nl = 0;
                if (nl > start && s_on_line) s_on_line(start);
                start = nl + 1;
            }
            len = rx + len - start;
            memmove(rx, start, len);
            if (len >= RX_MAX - 1) { 
                ESP_LOGW(TAG, "oversized line dropped");
                len = 0; 
            }
        }
        
        s_tcp_up = false;
        xSemaphoreTake(s_tx_lock, portMAX_DELAY); 
        close(s); 
        s_sock = -1; 
        xSemaphoreGive(s_tx_lock);
        ESP_LOGI(TAG, "TCP disconnected");
        if (s_on_link) s_on_link(false);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

esp_err_t dsk_net_start(dsk_line_cb_t on_line, dsk_link_cb_t on_link)
{
    s_on_line = on_line; s_on_link = on_link;
    s_tx_lock = xSemaphoreCreateMutex();
    load_creds();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_evt, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_evt, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_MIN_MODEM));
    ESP_ERROR_CHECK(esp_wifi_start());
    xTaskCreate(net_task, "dsk_net", 6144, NULL, 5, NULL);
    return ESP_OK;
}
