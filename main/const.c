#ifndef C_H
#define C_H
#include "stdio.h"
#include "esp_timer.h"
#include <esp_http_server.h>
#include "esp_log.h"
#include "string.h"
#include "nvs_flash.h"
#include "nvs.h"

enum THINKING_REASON {  REASON_DEFAULT = 0, REASON_PD, REASON_FW, REASON_TEST };
enum PWM_FN { PWM_FN_OFF = 0, PWM_FN_NONE = 1, PWM_FN_RAINBOW, PWM_FN_PULSE, PWM_FN_PULSE_RAINBOW, PWM_FN_FADE_IN, PWM_FN_FADE_OUT, PWM_FN_FADE_TO };
enum RGB_STAGE { RGB_STAGE_WAIT = 0, RGB_STAGE_IDLE, RGB_STAGE_HEAT };

static void rgb_set(int, int, int);
static httpd_handle_t getServer();
static esp_err_t ws_send_frame_to_all_clients(httpd_ws_frame_t *, httpd_handle_t);

const char *GLOBAL_TAG = "GLOBAL";
static nvs_handle_t nvs;

// speed 100 = color change takes 200 ms
// speed 1 = color change takes 20000 ms
static const int tickMax = 2048;
static const int tickMsDuration = 5000;
static const int rgbSpeedDefault = 50;
static const int rgbPowerDefault = 75;
static const enum PWM_FN rgbFnDefault = PWM_FN_NONE;
static const char rgbFnDataDefault[64] = "1 0:255,0,0";

static volatile int rgbPower = 100; // 0 - 100
static enum RGB_STAGE rgbStage = RGB_STAGE_WAIT;
static volatile int rgbSpeed = 0; // 0 - 100
static volatile int rgbR = 0;
static volatile int rgbG = 0;
static volatile int rgbB = 0;
static enum PWM_FN rgbFn = PWM_FN_NONE;
static volatile char rgbFnData[256] = "1 0:255,0,0";
static int64_t rgbFnStart = 0;
static int rgbFnColors = 0;
static int rgbFnMsDuration = 0;
static int tick = 0;
static int colorIndex = 0;

static void rgb_init() {
    nvs_open("storage", NVS_READWRITE, &nvs);

    nvs_get_str(nvs, "stage_0_data", (char *) &rgbFnData[0], 256);
    nvs_get_u8(nvs, "stage_0_fn", (uint8_t *) &rgbFn);
    nvs_get_u8(nvs, "stage_0_speed", (uint8_t *) &rgbSpeed);
    nvs_get_u8(nvs, "stage_0_power", (uint8_t *) &rgbPower);
}

static void rgb_save_stage(uint8_t new_stage, char* fn_data, uint8_t new_fn, uint8_t new_speed, uint8_t new_power) {
    char stageName[64] = "";

    sprintf(stageName, "stage_%d_speed", (int) new_stage);
    nvs_set_u8(nvs, stageName, new_speed);

    sprintf(stageName, "stage_%d_fn", (int) new_stage);
    nvs_set_u8(nvs, stageName, new_fn);

    sprintf(stageName, "stage_%d_power", (int) new_stage);
    nvs_set_u8(nvs, stageName, new_power);

    sprintf(stageName, "stage_%d_data", (int) new_stage);
    nvs_set_str(nvs, stageName, fn_data);
}

static void rgb_fn_restart() {
    tick = 0;
    rgbFnStart = esp_timer_get_time();
    sscanf((char *) &rgbFnData[0], "%d 0:", &rgbFnColors);

    if (rgbFnColors < 1)
        rgbFnColors = 1;
}

static void rgb_clean() {
    rgb_fn_restart();

    rgbFnStart = esp_timer_get_time();
    rgbSpeed = rgbSpeedDefault;

    rgbFnMsDuration = tickMsDuration * (100 - rgbSpeed) / 100;

    rgbR = 0;
    rgbG = 0;
    rgbB = 0;

    tick = 0;
    colorIndex = 0;
}

static void set_rgb_fn(enum PWM_FN fn) {
    if (rgbFn == fn) {
        return;
    }

    rgbFn = fn;
    rgb_clean();
}

static void fetch_stages() {
    httpd_handle_t server = getServer();

    for (uint8_t i = 0; i < 3; i++) {
        int _speed = -1, _fn = -1, _power = -1;
        char _data[256] = "";

        char stageName[64] = "";
        sprintf(stageName, "stage_%d_data", (int) i);
        nvs_get_str(nvs, stageName, &_data[0], 256);

        sprintf(stageName, "stage_%d_fn", (int) i);
        nvs_get_u8(nvs, stageName, (uint8_t *) &_fn);

        sprintf(stageName, "stage_%d_speed", (int) i);
        nvs_get_u8(nvs, stageName, (uint8_t *) &_speed);

        sprintf(stageName, "stage_%d_power", (int) i);
        nvs_get_u8(nvs, stageName, (uint8_t *) &_power);

        if (_fn == -1 || _speed == -1 || _power == -1) {
            _speed = rgbSpeedDefault;
            _fn = rgbFnDefault;
            _power = rgbPowerDefault;
            memcpy(_data, &rgbFnDataDefault[0], 64);
        }

        ESP_LOGI(
            GLOBAL_TAG,
            "Read stage: speed=%d fn=%d power=%d data=%s",
            (int) _speed,
            (int) _fn,
            (int) _power,
            _data
        );

        char buf[512];
        sprintf(buf,
            "{\"type\":\"stage\",\"content\":{\"stage\":%d,\"speed\":%d,\"fn\":%d,\"power\":%d,\"data\":\"%s\"}}",
            (int) i,
            (int) _speed,
            (int) _fn,
            (int) _power,
            _data
        );

        httpd_ws_frame_t pkt;
        memset(&pkt, 0, sizeof(httpd_ws_frame_t));
        pkt.payload = (uint8_t *) buf;
        pkt.type = HTTPD_WS_TYPE_TEXT;
        pkt.len = strlen(buf);

        ws_send_frame_to_all_clients(&pkt, server);
    }
}

static void set_rgb_stage(enum RGB_STAGE stage) {
    if (rgbStage == stage) {
        return;
    }

    char stageName[64] = "";
    sprintf(stageName, "stage_%d_data", (int) stage);
    nvs_get_str(nvs, stageName, (char *) &rgbFnData[0], 256);

    sprintf(stageName, "stage_%d_fn", (int) stage);
    nvs_get_u8(nvs, stageName, (uint8_t *) &rgbFn);

    sprintf(stageName, "stage_%d_speed", (int) stage);
    nvs_get_u8(nvs, stageName, (uint8_t *) &rgbSpeed);

    sprintf(stageName, "stage_%d_power", (int) stage);
    nvs_get_u8(nvs, stageName, (uint8_t *) &rgbPower);

    rgbStage = stage;
    set_rgb_fn(rgbFn);
}

static void parse_rgb_fn_data(char* fnData, int tickValue, int *r, int *g, int *b) {
    char search[256];
    sprintf(search, " %d:", tickValue);
    char* positionData = strstr(fnData, search);

    sprintf(search, " %d:%%d,%%d,%%d", tickValue);
    sscanf(positionData, search, r, g, b);
}

static void rgb_tick() {
    int msPassed = (int) (((float) (esp_timer_get_time() - rgbFnStart)) / 1000);

    int tickMsLen = rgbFnMsDuration / tickMax;
    tick = msPassed / tickMsLen;

    if (msPassed > rgbFnMsDuration) {
        colorIndex++;
        rgb_fn_restart();
    }

    if (rgbFnColors < 1)
        rgb_fn_restart();

    int r = -1, g = -1, b = -1;
    parse_rgb_fn_data((char *) &rgbFnData[0], colorIndex % rgbFnColors, &r, &g, &b);

    r = r * rgbPower / 100;
    g = g * rgbPower / 100;
    b = b * rgbPower / 100;

    rgbR = r;
    rgbG = g;
    rgbB = b;

    rgb_set(r << 5, g << 5, b << 5);
}

#endif
