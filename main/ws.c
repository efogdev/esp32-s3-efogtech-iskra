#include <esp_log.h>
#include <nvs_flash.h>
#include <esp_http_server.h>
#include <const.c>

static httpd_handle_t server = NULL;
static const char *WS_TAG = "WS";
static int voltage = 0;
static int _temperature = 0;

static void toggleHeating();
static void setConnected();
static void disable_all();
static void setPD(int);
static void pdTest();
static void fetch_stages();
static void setTargetTemperature(int);
static void rgb_save_stage(uint8_t, char*, uint8_t, uint8_t, uint8_t);
static void set_rgb_speed(int);
static void set_rgb_power(int);
static int displayUnitsToTemp(int);
static void reset_rgb_stage();
static void toggleAuth();

static void ws_update_temperature(int new_temperature) {
    _temperature = new_temperature;
}

static void ws_update_voltage(int new_voltage) {
    voltage = new_voltage;
}

static esp_err_t ws_send_frame_to_all_clients(httpd_ws_frame_t *ws_pkt, httpd_handle_t server_handle) {
    server = server_handle;

    static const size_t max_clients = CONFIG_LWIP_MAX_LISTENING_TCP;
    size_t fds = max_clients;
    int client_fds[max_clients];

    esp_err_t ret = httpd_get_client_list(server_handle, &fds, client_fds);

    if (ret != ESP_OK) {
        return ret;
    }

    ws_pkt->final = true;

    for (int i = 0; i < fds; i++) {
        int client_info = httpd_ws_get_fd_info(server_handle, client_fds[i]);
        if (client_info == HTTPD_WS_CLIENT_WEBSOCKET) {
            esp_err_t err = httpd_ws_send_frame_async(server_handle, client_fds[i], ws_pkt);

            if (err == ESP_FAIL) {
                httpd_sess_trigger_close(server_handle, client_fds[i]);
            }
        }
    }

    return ESP_OK;
}

static void ws_log(char* text) {
    if (server == NULL)
        return;

    char buf[256];
    sprintf(buf, "{\"type\":\"log\",\"content\":\"%s\"}", text);

    httpd_ws_frame_t pkt;
    memset(&pkt, 0, sizeof(httpd_ws_frame_t));
    pkt.payload = (uint8_t *) buf;
    pkt.type = HTTPD_WS_TYPE_TEXT;
    pkt.len = strlen(buf);

    ws_send_frame_to_all_clients(&pkt, server);
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

    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);

    if (ret != ESP_OK) {
        ESP_LOGE(WS_TAG, "httpd_ws_recv_frame failed to get frame len with %d", ret);
        return ret;
    }

    if (ws_pkt.len) {
        buf = calloc(1, ws_pkt.len + 1);
        if (buf == NULL) {
            ESP_LOGE(WS_TAG, "Failed to calloc memory for buf");
            return ESP_ERR_NO_MEM;
        }

        ws_pkt.payload = buf;

        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        if (ret != ESP_OK) {
            ESP_LOGE(WS_TAG, "httpd_ws_recv_frame failed with %d", ret);
            free(buf);
            return ret;
        }

        ESP_LOGI(WS_TAG, "PACKET: %s", ws_pkt.payload);

        ret = ESP_OK;

        if (strcmp((const char *) ws_pkt.payload, "on") == 0) {
            ESP_LOGI(WS_TAG, "WiFi connection established!");
            setConnected();
            fetch_stages();
        }

        if (strcmp((const char *) ws_pkt.payload, "heat") == 0) {
            ESP_LOGI(WS_TAG, "Heat toggle request.");

            toggleHeating();
        }

        if (strcmp((const char *) ws_pkt.payload, "reboot") == 0) {
            esp_restart();
        }

        if (strcmp((const char *) ws_pkt.payload, "auth") == 0) {
            toggleAuth();
        }

        if (strcmp((const char *) ws_pkt.payload, "pd_test") == 0) {
            pdTest();
        }

        if (strcmp((const char *) ws_pkt.payload, "stages") == 0) {
            fetch_stages();
        }

        if (strncmp((const char *) ws_pkt.payload, "pd_request", 10) == 0) {
            int volts = -1;
            sscanf((const char *) ws_pkt.payload, "pd_request %d", &volts);

            ESP_LOGI(WS_TAG, "Requesting %dV with USB PD.", volts);

            if (volts > 5) {
                disable_all();
                setPD(volts);
            }
        }

        if (strncmp((const char *) ws_pkt.payload, "save_stage", 5) == 0) {
            ESP_LOGI(WS_TAG, "Set RGB stage.");

            int new_stage = -1;
            int new_speed = -1;
            int new_power = -1;
            int new_fn = -1;
            char new_data[256] = "";

            char* stageData = strstr((const char *) ws_pkt.payload, "stage=");
            sscanf(stageData, "stage=%d fn=%d speed=%d power=%d", &new_stage, &new_fn, &new_speed, &new_power);

            char* fnData = strstr((const char *) ws_pkt.payload, "data=");
            unsigned int len = &ws_pkt.payload[0] + ws_pkt.len - (unsigned char *) &fnData[0] - 5;
            memcpy(&new_data[0], &fnData[0] + 5, len);
            new_data[len] = '\0';

            rgb_save_stage(new_stage, new_data, (uint8_t) new_fn, (uint8_t) new_speed, (uint8_t) new_power);
            reset_rgb_stage();
            fetch_stages();
        }

        if (strncmp((const char *) ws_pkt.payload, "set", 3) == 0) {
            ESP_LOGI(WS_TAG, "Set target temperature.");

            int target = -1;
            sscanf((const char *) ws_pkt.payload, "set t=%d", &target);
            target = displayUnitsToTemp(target);

            setTargetTemperature(target);
            ESP_LOGI(WS_TAG, "Target: %d degrees.", target);
        }

        if (strncmp((const char *) ws_pkt.payload, "rgb_speed", 9) == 0) {
            int target = -1;
            sscanf((const char *) ws_pkt.payload, "rgb_speed %d", &target);

            if (target > -1)
                set_rgb_speed(target);
        }

        if (strncmp((const char *) ws_pkt.payload, "rgb_power", 9) == 0) {
            int target = -1;
            sscanf((const char *) ws_pkt.payload, "rgb_power %d", &target);

            if (target > -1)
                set_rgb_power(target);
        }

        if (strncmp((const char *) ws_pkt.payload, "rgb_fn", 6) == 0) {
            int target = -1;
            sscanf((const char *) ws_pkt.payload, "rgb_fn %d", &target);

            if (target > -1)
                set_rgb_fn((enum PWM_FN) target);
        }
    }

    free(buf);
    return ret;
}

static const httpd_uri_t ws = {
    .uri        = "/ws",
    .method     = HTTP_GET,
    .handler    = echo_handler,
    .user_ctx   = NULL,
    .is_websocket = true,
};

static void initWebsocket(httpd_handle_t server)
{
    ESP_LOGI(WS_TAG, "Init websocket server");
    httpd_register_uri_handler(server, &ws);
}
