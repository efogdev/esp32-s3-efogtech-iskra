#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_system.h>
#include <nvs_flash.h>
#include <sys/param.h>
#include "esp_netif.h"
#include "esp_eth.h"
#include "protocol_examples_common.h"
#include <esp_http_server.h>

static const char *WS_TAG = "WS";
static int voltage = 0;
static int _temperature = 0;
//static const int voltageOkThreshold = 1700;

static void toggleHeating();

struct async_resp_arg {
    httpd_handle_t hd;
    int fd;
};

static void ws_update_temperature(int new_temperature) {
    _temperature = new_temperature;
}

static void ws_update_voltage(int new_voltage) {
    voltage = new_voltage;
}

static void ws_async_send(void *arg)
{
    static const char * data = "Async data";
    struct async_resp_arg *resp_arg = arg;
    httpd_handle_t hd = resp_arg->hd;
    int fd = resp_arg->fd;
    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.payload = (uint8_t*)data;
    ws_pkt.len = strlen(data);
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;

    httpd_ws_send_frame_async(hd, fd, &ws_pkt);
    free(resp_arg);
}

static esp_err_t trigger_async_send(httpd_handle_t handle, httpd_req_t *req)
{
    struct async_resp_arg *resp_arg = malloc(sizeof(struct async_resp_arg));
    resp_arg->hd = req->handle;
    resp_arg->fd = httpd_req_to_sockfd(req);
    return httpd_queue_work(handle, ws_async_send, resp_arg);
}

static esp_err_t ws_send_frame_to_all_clients(httpd_ws_frame_t *ws_pkt, httpd_handle_t server_handle) {
    static const size_t max_clients = CONFIG_LWIP_MAX_LISTENING_TCP;
    size_t fds = max_clients;
    int client_fds[max_clients];

    esp_err_t ret = httpd_get_client_list(server_handle, &fds, client_fds);

    if (ret != ESP_OK) {
        return ret;
    }

    for (int i = 0; i < fds; i++) {
        int client_info = httpd_ws_get_fd_info(server_handle, client_fds[i]);
        if (client_info == HTTPD_WS_CLIENT_WEBSOCKET) {
            httpd_ws_send_frame_async(server_handle, client_fds[i], ws_pkt);
        }
    }

    return ESP_OK;
}

static esp_err_t echo_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        return ESP_OK;
    }
    httpd_ws_frame_t ws_pkt;
    uint8_t *buf = NULL;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;
    /* Set max_len = 0 to get the frame len */
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(WS_TAG, "httpd_ws_recv_frame failed to get frame len with %d", ret);
        return ret;
    }
    if (ws_pkt.len) {
        /* ws_pkt.len + 1 is for NULL termination as we are expecting a string */
        buf = calloc(1, ws_pkt.len + 1);
        if (buf == NULL) {
            ESP_LOGE(WS_TAG, "Failed to calloc memory for buf");
            return ESP_ERR_NO_MEM;
        }
        ws_pkt.payload = buf;
        /* Set max_len = ws_pkt.len to get the frame payload */
        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        if (ret != ESP_OK) {
            ESP_LOGE(WS_TAG, "httpd_ws_recv_frame failed with %d", ret);
            free(buf);
            return ret;
        }

        ESP_LOGI(WS_TAG, "Got packet with message: %s", ws_pkt.payload);

        ret = ESP_OK;

        if (strcmp((const char *) ws_pkt.payload, "on") == 0) {
            ESP_LOGI(WS_TAG, "WiFi connection established!");

//            if (voltage < voltageOkThreshold) {
//                const char *buf = "{\"type\":\"show\",\"content\":\"Your power adapter is too weak, not enough power for the heater. \\n\\n Please, find a one with PD (Power Deliver) 18W or more.\"}";
//                httpd_ws_frame_t pkt;
//                memset(&pkt, 0, sizeof(httpd_ws_frame_t));
//                pkt.payload = (uint8_t *) buf;
//                pkt.type = HTTPD_WS_TYPE_TEXT;
//                pkt.len = strlen(buf);
//
//                ret = httpd_ws_send_frame(req, &pkt);
//            }
        }

        if (strcmp((const char *) ws_pkt.payload, "heat") == 0) {
            ESP_LOGI(WS_TAG, "Heat toggle request.");

            toggleHeating();
        }
    }

    if (ws_pkt.type == HTTPD_WS_TYPE_TEXT &&
        strcmp((char*)ws_pkt.payload,"Trigger async") == 0) {
        free(buf);
        return trigger_async_send(req->handle, req);
    }

    if (ret != ESP_OK) {
        ESP_LOGE(WS_TAG, "httpd_ws_send_frame failed with %d", ret);
    }
    free(buf);
    return ret;
}

static const httpd_uri_t ws = {
        .uri        = "/ws",
        .method     = HTTP_GET,
        .handler    = echo_handler,
        .user_ctx   = NULL,
        .is_websocket = true
};

static void initWebsocket(httpd_handle_t server)
{
    ESP_LOGI(WS_TAG, "Init websocket server");
    httpd_register_uri_handler(server, &ws);
}
