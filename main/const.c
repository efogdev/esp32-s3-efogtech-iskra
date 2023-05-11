#ifndef C_H
#define C_H
#include "stdio.h"
#include "esp_timer.h"
#include <esp_http_server.h>
#include "esp_log.h"
#include "esp_random.h"
#include "math.h"
#include "string.h"
#include "nvs_flash.h"
#include "nvs.h"

enum THINKING_REASON {  REASON_DEFAULT = 0, REASON_PD, REASON_FW, REASON_TEST };
enum PWM_FN { PWM_FN_OFF = 0, PWM_FN_NONE = 1, PWM_FN_PULSE, PWM_FN_FADE_IN, PWM_FN_FADE_OUT, PWM_FN_FADE };
enum RGB_STAGE { RGB_STAGE_WAIT = 0, RGB_STAGE_IDLE, RGB_STAGE_HEAT };

static void rgb_set(int, int, int);
static httpd_handle_t getServer();
static esp_err_t ws_send_frame_to_all_clients(httpd_ws_frame_t *, httpd_handle_t);

const char *GLOBAL_TAG = "GLOBAL";
static nvs_handle_t nvs;

#define tickMax 1024
static const int tickMsDuration = 7200;
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

    size_t size = 256;
    nvs_get_str(nvs, "stage_0_data", (char *) &rgbFnData[0], &size);
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

    nvs_commit(nvs);
}

static int rgb_fn_data_cache[tickMax];

static void rgb_fn_restart() {
    tick = 0;
    rgbFnStart = esp_timer_get_time();
    sscanf((char *) &rgbFnData[0], "%d 0:", &rgbFnColors);

    for (int i = 0; i < tickMax; i++)
        rgb_fn_data_cache[i] = -1;

    if (rgbFnColors < 1)
        rgbFnColors = 1;

    rgbFnMsDuration = tickMsDuration * (100 - rgbSpeed + 1) / 100;
}

static void rgb_clean() {
    rgb_fn_restart();

    rgbR = 0;
    rgbG = 0;
    rgbB = 0;

    tick = 0;
    colorIndex = 0;
}

static void set_rgb_fn(enum PWM_FN fn) {
    if (rgbFn == fn)
        return;

    rgbFn = fn;
    rgb_clean();
}

static void set_rgb_speed(int speed) {
    rgbSpeed = speed;
    rgb_clean();
}

static void set_rgb_power(int power) {
    rgbPower = power;
    rgb_clean();
}

static void fetch_stages() {
    httpd_handle_t server = getServer();

    for (uint8_t i = 0; i < 3; i++) {
        size_t size = 256;
        uint8_t _speed = 255, _fn = 255, _power = 255;
        char _data[size];

        char stageName[64] = "";
        sprintf(stageName, "stage_%d_data", (int) i);
        nvs_get_str(nvs, stageName, &_data[0], &size);

        sprintf(stageName, "stage_%d_fn", (int) i);
        nvs_get_u8(nvs, stageName, (uint8_t *) &_fn);

        sprintf(stageName, "stage_%d_speed", (int) i);
        nvs_get_u8(nvs, stageName, (uint8_t *) &_speed);

        sprintf(stageName, "stage_%d_power", (int) i);
        nvs_get_u8(nvs, stageName, (uint8_t *) &_power);

        if (_fn == 255 || _speed == 255 || _power == 255) {
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

        char buf[400];
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
    size_t size = 256;

    char stageName[64] = "";
    sprintf(stageName, "stage_%d_data", (int) stage);
    nvs_get_str(nvs, stageName, (char *) &rgbFnData[0], &size);

    sprintf(stageName, "stage_%d_fn", (int) stage);
    nvs_get_u8(nvs, stageName, (uint8_t *) &rgbFn);

    sprintf(stageName, "stage_%d_speed", (int) stage);
    nvs_get_u8(nvs, stageName, (uint8_t *) &rgbSpeed);

    sprintf(stageName, "stage_%d_power", (int) stage);
    nvs_get_u8(nvs, stageName, (uint8_t *) &rgbPower);

    rgbStage = stage;
    set_rgb_fn(rgbFn);
}

static void reset_rgb_stage() {
    set_rgb_stage(rgbStage);
}

static void parse_rgb_fn_data(char* fnData, int colorIndex, int *r, int *g, int *b) {
    if (rgb_fn_data_cache[colorIndex] > -1) {
        *r = (rgb_fn_data_cache[colorIndex] >> 16) & 0x0ff;
        *g = (rgb_fn_data_cache[colorIndex] >> 8) & 0x0ff;
        *b = rgb_fn_data_cache[colorIndex] & 0x0ff;
    }

    char search[256];
    sprintf(search, " %d:", colorIndex);
    char* positionData = strstr(fnData, search);

    sprintf(search, " %d:%%d,%%d,%%d", colorIndex);
    sscanf(positionData, search, r, g, b);

    rgb_fn_data_cache[colorIndex] = ((*r & 0x0ff) << 16) | ((*g & 0x0ff) << 8) | (*b & 0x0ff);
}

static float ease_out_circ(float val) {
    return sqrtf(1 - (val - 1) * (val - 1));
}

static float ease_in_circ(float val) {
    return 1 - sqrtf(1 - (val - 1) * (val - 1));
}

static float ease_out_cubic(float val) {
    return sqrtf(1 - (val - 1) * (val - 1) * (val - 1));
}

static float ease_in_quint(float val) {
    return val * val * val * val * val;
}

static float ease_in_out_expo(float val) {
    if (val == 0) return 0;
    if (val == 1) return 1;

    if (val < 0.5) {
        return powf(2, (20 * val - 10)) / 2;
    } else {
        return (2 - powf(2, (-20 * val + 10))) / 2;
    }
}

static float ease_in_out_quad(float val) {
    if (val < 0.5) {
        return 2 * val * val;
    } else {
        return 1 - (-2 * val + 2) * (-2 * val + 2) / 2;
    }
}

static void tick_window(int* tick, uint8_t window_percent) {
    int diff = tickMax * window_percent / 100;
    int _tick = *tick;

    if (_tick < diff) {
        *tick = diff;
        return;
    }

   if (_tick > (tickMax - diff)) {
        *tick = tickMax - diff;
        return;
    }
}

static void IRAM_ATTR rgb_tick() {
    int msPassed = (int) (((float) (esp_timer_get_time() - rgbFnStart)) / 1000);

    int tickMsLen = rgbFnMsDuration / tickMax;

    if (tickMsLen < 1)
        tickMsLen = 1;

    tick = msPassed / tickMsLen;

    tick_window(&tick, 1);

    if (msPassed > rgbFnMsDuration) {
        colorIndex++;
        rgb_fn_restart();
        return;
    }

    if (rgbFnColors < 1) {
        rgb_fn_restart();
        return;
    }

    int r = 0, g = 0, b = 0;
    int finalR = 0, finalG = 0, finalB = 0;
    float fR = -1, fG = -1, fB = -1;
    float percent = (float) tick / tickMax;

    parse_rgb_fn_data((char *) &rgbFnData[0], colorIndex % rgbFnColors, &r, &g, &b);

    switch (rgbFn) {
        case PWM_FN_OFF:
            r = g = b = 0;
            break;
        case PWM_FN_NONE:
            break;
        case PWM_FN_FADE_IN:
            tick_window(&tick, 5);

            fR = (float) r * ease_in_out_quad(percent);
            fG = (float) g * ease_in_out_quad(percent);
            fB = (float) b * ease_in_out_quad(percent);

            r = (int) fR; g = (int) fG; b = (int) fB;
            break;
        case PWM_FN_FADE_OUT:
            tick_window(&tick, 8);

            fR = (float) r * ease_in_out_quad((float) (tickMax - tick) / tickMax);
            fG = (float) g * ease_in_out_quad((float) (tickMax - tick) / tickMax);
            fB = (float) b * ease_in_out_quad((float) (tickMax - tick) / tickMax);

            r = (int) fR; g = (int) fG; b = (int) fB;
            break;
        case PWM_FN_PULSE:
            tick_window(&tick, 5);

            if (percent < 0.5) {
                fR = (float) r * ease_in_out_expo(percent);
                fG = (float) g * ease_in_out_expo(percent);
                fB = (float) b * ease_in_out_expo(percent);
            } else {
                fR = (float) r * ease_in_out_quad(1 - percent);
                fG = (float) g * ease_in_out_quad(1 - percent);
                fB = (float) b * ease_in_out_quad(1 - percent);
            }

            r = (int) fR; g = (int) fG; b = (int) fB;
            break;
        case PWM_FN_FADE:
            int r_next = -1, g_next = -1, b_next = -1;
            parse_rgb_fn_data((char *) &rgbFnData[0], (colorIndex + 1) % rgbFnColors, &r_next, &g_next, &b_next);

            if (r_next == -1 || g_next == -1 || b_next == -1) {
                r = g = b = 0;
                break;
            }

            fR = (float) r + ((float) (r_next - r) * percent);
            fG = (float) g + ((float) (g_next - g) * percent);
            fB = (float) b + ((float) (b_next - b) * percent);

            r = (int) fR; g = (int) fG; b = (int) fB;
            break;
        default:
            r = g = b = 0;
            break;
    }

    r = rgbR = r * rgbPower / 100;
    g = rgbG = g * rgbPower / 100;
    b = rgbB = b * rgbPower / 100;

    if (fR == -1 || fG == -1 || fB == -1) {
        finalR = r << 5;
        finalG = g << 5;
        finalB = b << 5;
    } else {
        finalR = (int) (fR * (float) rgbPower / 100 * pow(2, 5));
        finalG = (int) (fG * (float) rgbPower / 100 * pow(2, 5));
        finalB = (int) (fB * (float) rgbPower / 100 * pow(2, 5));
    }

    rgb_set(finalR, finalG, finalB);
}

#endif
