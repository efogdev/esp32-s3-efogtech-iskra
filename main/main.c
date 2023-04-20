#include <string.h>
#include <math.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/FreeRTOSConfig.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "protocol_examples_common.h"
#include "esp_tls.h"
#include "soc/soc_caps.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "driver/gpio.h"
#include "driver/dedic_gpio.h"
#include <inttypes.h>
#include "esp_http_client.h"
#include "esp_http_server.h"
#include "driver/ledc.h"
#include "lwip/inet.h"
#include "esp_mac.h"
#include <sys/param.h>
#include "esp_ota_ops.h"
#include "esp_flash_partitions.h"
#include "esp_partition.h"

static const char *TAG = "EFOGTECH-ISKRA";

static int usbVrefVoltage = -1;
static int cc1 = -1, cc2 = -1;
static int heaterTemperature = -1;
static int heaterPwm = 1;
static int waterTemperature = -1;
static int radiatorTemperature = -1;
static int targetTemperature = 220;
static int peltierValue = 0;
static int fanValue = 16;
static bool isHeating = false;
static bool isCooling = false;
static bool isWaitingForConnection = true;
static const int voltageMaxThreshold = 1600;

static esp_err_t start_file_server(httpd_handle_t);

#include "server.c"
#include "ws.c"
#include "ota.c"

#define PWM_FREQ 12000
#define PWM_RESOLUTION LEDC_TIMER_5_BIT
#define PWM_MAX 31
#define PWM_MODE LEDC_LOW_SPEED_MODE

#define REPORT_URL "http://efog.tech/api/batmon/update"

#define PWM_CHANNEL_HEAT LEDC_CHANNEL_0
#define PWM_CHANNEL_PELTIER LEDC_CHANNEL_1
#define PWM_CHANNEL_FAN LEDC_CHANNEL_2

#define RGB_FREQ 5000
#define RGB_RESOLUTION LEDC_TIMER_13_BIT
#define RGB_MAX 8191
#define RGB_CHANNEL_R LEDC_CHANNEL_3
#define RGB_CHANNEL_G LEDC_CHANNEL_4
#define RGB_CHANNEL_B LEDC_CHANNEL_5

#define TEMP_ADC_CHANNEL ADC_CHANNEL_5
#define WATER_TEMP_ADC_CHANNEL ADC_CHANNEL_4
#define RADIATOR_TEMP_ADC_CHANNEL ADC_CHANNEL_6
//#define USB_CC1_ADC_CHANNEL ADC_CHANNEL_6
//#define USB_CC2_ADC_CHANNEL ADC_CHANNEL_7
#define USB_VREF_ADC_CHANNEL ADC_CHANNEL_7

#define GPIO_RGB_R 10
#define GPIO_NUM_RGB_R GPIO_NUM_10

#define GPIO_RGB_G 11
#define GPIO_NUM_RGB_G GPIO_NUM_11

#define GPIO_RGB_B 12
#define GPIO_NUM_RGB_B GPIO_NUM_12

#define GPIO_FAN 13
#define GPIO_NUM_FAN GPIO_NUM_13

#define GPIO_HEAT 14
#define GPIO_NUM_HEAT GPIO_NUM_14

#define GPIO_PELTIER 21
#define GPIO_NUM_PELTIER GPIO_NUM_21

#define GPIO_TR 16
#define GPIO_NUM_TR GPIO_NUM_16

#define GPIO_TR_WATER 15
#define GPIO_NUM_TR_WATER GPIO_NUM_15

#define GPIO_TR_RADIATOR 7
#define GPIO_NUM_TR_RADIATOR GPIO_NUM_7

#define GPIO_USB_VREF 8
#define GPIO_NUM_USB_VREF GPIO_NUM_8

#define GPIO_USB_CC1_VREF 17
#define GPIO_NUM_USB_CC1_VREF GPIO_NUM_17

#define GPIO_USB_CC2_VREF 18
#define GPIO_NUM_USB_CC2_VREF GPIO_NUM_18

#define GPIO_LED_0 37
#define GPIO_NUM_LED_0 GPIO_NUM_37

#define GPIO_LED_1 48
#define GPIO_NUM_LED_1 GPIO_NUM_48

#define GPIO_RST_BTN 36
#define GPIO_NUM_RST_BTN GPIO_NUM_36

#define GPIO_PD_CFG2 1
#define GPIO_NUM_PD_CFG2 GPIO_NUM_1

#define GPIO_PD_CFG3 2
#define GPIO_NUM_PD_CFG3 GPIO_NUM_2

#define MAX_HTTP_RECV_BUFFER 512
#define MAX_HTTP_OUTPUT_BUFFER 2048

static adc_oneshot_unit_handle_t adc1_handle;
static adc_oneshot_unit_handle_t adc2_handle;
static httpd_handle_t webserver;

esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
        case HTTP_EVENT_ON_CONNECTED:
        case HTTP_EVENT_HEADER_SENT:
        case HTTP_EVENT_ON_HEADER:
        case HTTP_EVENT_ON_FINISH:
        case HTTP_EVENT_ON_DATA:
        case HTTP_EVENT_DISCONNECTED:
        case HTTP_EVENT_REDIRECT:
            break;    
    }
    return ESP_OK;
}

static void setHeatValue(int value) {
    ledc_set_duty(PWM_MODE, PWM_CHANNEL_HEAT, (int) ((value * PWM_MAX) / 255));
    ledc_update_duty(PWM_MODE, PWM_CHANNEL_HEAT);
}

static void setConnected() {
    isWaitingForConnection = false;
}

static void setTargetTemperature(int temp) {
    targetTemperature = temp;
}

static void startHeating() {
    isHeating = true;

    ledc_set_duty(PWM_MODE, PWM_CHANNEL_HEAT, PWM_MAX);
    ledc_update_duty(PWM_MODE, PWM_CHANNEL_HEAT);
}

static void stopHeating() {
    isHeating = false;

    ledc_set_duty(PWM_MODE, PWM_CHANNEL_HEAT, 0);
    ledc_update_duty(PWM_MODE, PWM_CHANNEL_HEAT);
}

static void toggleHeating() {
    isHeating = !isHeating;

    if (isHeating) {
        startHeating();
    } else {
        stopHeating();
    }
}

static void startCooling() {
    fanValue = 16;
    isCooling = true;

    ledc_set_duty(PWM_MODE, PWM_CHANNEL_PELTIER, PWM_MAX * (peltierValue / 32) * 0.5);
    ledc_update_duty(PWM_MODE, PWM_CHANNEL_PELTIER);
}

static void stopCooling() {
    fanValue = 0;
    isCooling = false;

    ledc_set_duty(PWM_MODE, PWM_CHANNEL_PELTIER, 0);
    ledc_update_duty(PWM_MODE, PWM_CHANNEL_PELTIER);
}

static void toggleCooling() {
    isCooling = !isCooling;

    if (isCooling) {
        startCooling();
    } else {
        stopCooling();
    }
}

static void setPeltierValue(int value) {
    peltierValue = value;
}

static void setFanValue(int value) {
    fanValue = value;
}

static void initPwm() {
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = PWM_MODE,
        .timer_num        = LEDC_TIMER_0, 
        .duty_resolution  = PWM_RESOLUTION,
        .freq_hz          = PWM_FREQ,
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    ledc_timer_config_t ledc_timer2 = {
        .duty_resolution = RGB_RESOLUTION, 
        .freq_hz = RGB_FREQ,               
        .speed_mode = PWM_MODE,           
        .timer_num = LEDC_TIMER_1,        
        .clk_cfg = LEDC_AUTO_CLK,         
    };
    ledc_timer_config(&ledc_timer2);

    ledc_channel_config_t ledc_heat = {
        .speed_mode     = PWM_MODE,
        .channel        = PWM_CHANNEL_HEAT,
        .timer_sel      = LEDC_TIMER_0,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = GPIO_HEAT,
        .duty           = 0, 
        .hpoint         = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_heat));

    ledc_channel_config_t ledc_rgb_r = {
        .channel    = RGB_CHANNEL_R,
        .duty       = 0,
        .gpio_num   = GPIO_RGB_R,
        .speed_mode = PWM_MODE,
        .hpoint     = 0,
        .timer_sel  = LEDC_TIMER_1,
        .flags.output_invert = 0
    };

    ledc_channel_config_t ledc_rgb_g = {
        .channel    = RGB_CHANNEL_G,
        .duty       = 0,
        .gpio_num   = GPIO_RGB_G,
        .speed_mode = PWM_MODE,
        .hpoint     = 0,
        .timer_sel  = LEDC_TIMER_1,
        .flags.output_invert = 0
    };

    ledc_channel_config_t ledc_rgb_b = {
        .channel    = RGB_CHANNEL_B,
        .duty       = 0,
        .gpio_num   = GPIO_RGB_B,
        .speed_mode = PWM_MODE,
        .hpoint     = 0,
        .timer_sel  = LEDC_TIMER_1,
        .flags.output_invert = 0
    };

    ESP_ERROR_CHECK(ledc_channel_config(&ledc_rgb_r));
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_rgb_g));
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_rgb_b));
}

static void initAdc() {
    adc_oneshot_unit_init_cfg_t init_config1 = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &adc1_handle));

    adc_oneshot_unit_init_cfg_t init_config2 = {
            .unit_id = ADC_UNIT_2,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config2, &adc2_handle));

    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_12,
        .atten = ADC_ATTEN_DB_11,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, USB_VREF_ADC_CHANNEL, &config));
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc2_handle, TEMP_ADC_CHANNEL, &config));
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, RADIATOR_TEMP_ADC_CHANNEL, &config));
}

static void IRAM_ATTR makeJob() {
    int temperature = -1, usbVref = -1;

    adc_oneshot_read(adc1_handle, USB_VREF_ADC_CHANNEL, &usbVref);
    adc_oneshot_read(adc2_handle, TEMP_ADC_CHANNEL, &temperature);
    adc_oneshot_read(adc1_handle, RADIATOR_TEMP_ADC_CHANNEL, &waterTemperature);

    usbVrefVoltage = usbVref;
    heaterTemperature = temperature;

    ws_update_voltage(usbVref);
    ws_update_temperature(temperature);
}

static void pcb_led_task(void *pvParameters)
{
    while (1) {
        gpio_set_level(GPIO_NUM_LED_1, usbVrefVoltage >= voltageMaxThreshold);
        vTaskDelay(pdMS_TO_TICKS(4000));
    }
}

extern const char root_start[] asm("_binary_index_html_start");
extern const char root_end[] asm("_binary_index_html_end");

extern const char manifest_start[] asm("_binary_manifest_json_start");
extern const char manifest_end[] asm("_binary_manifest_json_end");

extern const char ota_start[] asm("_binary_upload_html_start");
extern const char ota_end[] asm("_binary_upload_html_end");

extern const char bundle_start[] asm("_binary_bundle_js_start");
extern const char bundle_end[] asm("_binary_bundle_js_end");

static esp_err_t root_get_handler(httpd_req_t *req)
{
    const uint32_t root_len = root_end - root_start;

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, root_start, root_len);

    return ESP_OK;
}

static esp_err_t bundle_get_handler(httpd_req_t *req)
{
    const uint32_t bundle_len = bundle_end - bundle_start;

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, bundle_start, bundle_len);

    return ESP_OK;
}

static esp_err_t manifest_get_handler(httpd_req_t *req)
{
    const uint32_t manifest_len = manifest_end - manifest_start;

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, manifest_start, manifest_len);

    return ESP_OK;
}

static esp_err_t ota_get_handler(httpd_req_t *req)
{
    const uint32_t ota_len = ota_end - ota_start;

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, ota_start, ota_len);

    return ESP_OK;
}

static const httpd_uri_t root = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = root_get_handler,
    .user_ctx = NULL
};

static const httpd_uri_t ota = {
     .uri = "/ota",
     .method = HTTP_GET,
     .handler = ota_get_handler,
     .user_ctx = NULL
};

static const httpd_uri_t manifest = {
     .uri = "/manifest.json",
     .method = HTTP_GET,
     .handler = manifest_get_handler,
     .user_ctx = NULL
};

static const httpd_uri_t bundle = {
     .uri = "/bundle.js",
     .method = HTTP_GET,
     .handler = bundle_get_handler,
     .user_ctx = NULL
};

esp_err_t http_404_error_handler(httpd_req_t *req, httpd_err_code_t err)
{
    httpd_resp_set_status(req, "302 Temporary Redirect");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, "Redirect to the captive portal", HTTPD_RESP_USE_STRLEN);

    return ESP_OK;
}

static httpd_handle_t start_webserver(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.max_open_sockets = 5;
    config.max_uri_handlers = 12;
    config.lru_purge_enable = true;
    config.uri_match_fn = httpd_uri_match_wildcard;

    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        start_dns_server();

        httpd_register_uri_handler(server, &ota);
        httpd_register_uri_handler(server, &manifest);
        httpd_register_uri_handler(server, &bundle);
        httpd_register_uri_handler(server, &root);

        initWebsocket(server);
        start_file_server(server);

        httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, http_404_error_handler);
    } else {
        esp_restart();
    }

    return server;
}

// static bool fade = false;
// static int fadeSpeed = 
// static int fadeTo = 0;
// static int fadeCurrent = 0;

static void rgb_task(void *pvParameters)
{
    const int BRIGHTNESS_MAX = RGB_MAX * 0.8;
    const int RED_BRIGHTNESS_MIN = RGB_MAX * 0.1;
    int pulseDuty = 0;
    int redPulseDuty = BRIGHTNESS_MAX;
    bool redPulseDirection = 0;

    while (1) {
        if (isWaitingForConnection) {
            pulseDuty += 8;
            pulseDuty *= 1.013;

            if (pulseDuty >= BRIGHTNESS_MAX) {
                vTaskDelay(pdMS_TO_TICKS(600));
                pulseDuty = 0;
            }

            ledc_set_duty(PWM_MODE, RGB_CHANNEL_G, pulseDuty);
            ledc_update_duty(PWM_MODE, RGB_CHANNEL_G);
        } else {
            ledc_set_duty(PWM_MODE, RGB_CHANNEL_G, 0);
            ledc_update_duty(PWM_MODE, RGB_CHANNEL_G);

            if (isHeating) {
                if (redPulseDuty >= BRIGHTNESS_MAX)
                    redPulseDirection = 0;

                if (redPulseDuty < RED_BRIGHTNESS_MIN) {
                    redPulseDuty = RED_BRIGHTNESS_MIN;
                    redPulseDirection = 1;
                }

                if (redPulseDirection == 1) {
                    redPulseDuty += 16;
                } else {
                    redPulseDuty -= 16;
                }


                ledc_set_duty(PWM_MODE, RGB_CHANNEL_B, 0);
                ledc_update_duty(PWM_MODE, RGB_CHANNEL_B);

                ledc_set_duty(PWM_MODE, RGB_CHANNEL_R, redPulseDuty);
                ledc_update_duty(PWM_MODE, RGB_CHANNEL_R);
            } else {
                ledc_set_duty(PWM_MODE, RGB_CHANNEL_B, BRIGHTNESS_MAX);
                ledc_update_duty(PWM_MODE, RGB_CHANNEL_B);

                ledc_set_duty(PWM_MODE, RGB_CHANNEL_R, 0);
                ledc_update_duty(PWM_MODE, RGB_CHANNEL_R);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(24));
    }
}

static void IRAM_ATTR soft_pwm_task(void *pvParameters) {
    int baseDelay = 4;
    int state = 0;

    while (1) {
        if (!isCooling) {
            gpio_set_level(GPIO_NUM_PELTIER, 0);
            vTaskDelay(pdMS_TO_TICKS(600));
        }

        if (peltierValue == 0) {
            gpio_set_level(GPIO_NUM_PELTIER, 0);
            vTaskDelay(pdMS_TO_TICKS(baseDelay * 16));
            continue;
        } else if (peltierValue == PWM_MAX) {
            gpio_set_level(GPIO_NUM_PELTIER, 1);
            vTaskDelay(pdMS_TO_TICKS(baseDelay * 2));
            continue;
        }

        if (isHeating && heaterPwm < PWM_MAX / 2) {
            peltierValue = PWM_MAX;
        }

        int delayTicks;

        if (state == 0) {
            state = 1;
            delayTicks = peltierValue;
        } else {
            state = 0;
            delayTicks = PWM_MAX - peltierValue;
        }

        gpio_set_level(GPIO_NUM_PELTIER, state);
        vTaskDelay(pdMS_TO_TICKS(baseDelay * delayTicks));
    }
}

static void IRAM_ATTR fan_soft_pwm_task(void *pvParameters) {
    int baseDelay = 1;
    int state = 0;

    while (1) {
        if (fanValue == 0) {
            gpio_set_level(GPIO_NUM_FAN, 0);
            vTaskDelay(pdMS_TO_TICKS(baseDelay * 64));
            continue;
        } else if (fanValue == PWM_MAX) {
            gpio_set_level(GPIO_NUM_FAN, 1);
            vTaskDelay(pdMS_TO_TICKS(baseDelay * 32));
            continue;
        }

        int delayTicks;

        if (state == 0) {
            state = 1;
            delayTicks = fanValue;
        } else {
            state = 0;
            delayTicks = PWM_MAX - fanValue;
        }

        gpio_set_level(GPIO_NUM_FAN, state);
        vTaskDelay(pdMS_TO_TICKS(baseDelay * delayTicks));
    }
}

static void IRAM_ATTR pd_task(void *pvParameters) {
    vTaskDelay(pdMS_TO_TICKS(400));

    while (1) {
        gpio_set_level(GPIO_NUM_PD_CFG2, 1);
        gpio_set_level(GPIO_NUM_PD_CFG3, 0);

        vTaskDelay(pdMS_TO_TICKS(6000));
    }
}

static void IRAM_ATTR report_task(void *pvParameters) {
    vTaskDelay(pdMS_TO_TICKS(2000));

    while (1) {
        if (!webserver) {
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        float ntcResistance = 5600 * (1 / (3.3 / ((3.3 * heaterTemperature) / 4096) - 1));

        float temperature;
        temperature = ntcResistance / 100000;     // (R/Ro)
        temperature = log(temperature);                  // ln(R/Ro)
        temperature /= 3950;                   // 1/B * ln(R/Ro)
        temperature += 1.0 / (25 + 273.15); // + (1/To)
        temperature = 1.0 / temperature;                 // Invert
        temperature -= 273.15;                         // convert absolute temp to C

//        heaterTemperature = (int) temperature;

        if (isHeating) {
            if (temperature >= targetTemperature) {
                ledc_set_duty(PWM_MODE, PWM_CHANNEL_HEAT, PWM_MAX * 0.2);
                ledc_update_duty(PWM_MODE, PWM_CHANNEL_HEAT);
            }

            if (temperature < targetTemperature / 2) {
                heaterPwm = PWM_MAX;
                ledc_set_duty(PWM_MODE, PWM_CHANNEL_HEAT, PWM_MAX);
                ledc_update_duty(PWM_MODE, PWM_CHANNEL_HEAT);
            } else if (temperature < targetTemperature - 20) {
                heaterPwm = PWM_MAX * 0.85;
                ledc_set_duty(PWM_MODE, PWM_CHANNEL_HEAT, PWM_MAX * 0.85);
                ledc_update_duty(PWM_MODE, PWM_CHANNEL_HEAT);
            } else {
                heaterPwm = PWM_MAX * 0.6;
                ledc_set_duty(PWM_MODE, PWM_CHANNEL_HEAT, PWM_MAX * 0.6);
                ledc_update_duty(PWM_MODE, PWM_CHANNEL_HEAT);
            }
        }

        char buf[256];
        sprintf(buf,
            "{\"type\":\"update\",\"content\":{\"isHeating\":%s,\"temperature\":\"%d\",\"coolingTemperature\":\"%d\",\"voltage\":\"%s\",\"isVoltageOk\":%s,\"isCooling\":%s}}",
            isHeating ? "true" : "false",
            (int) temperature,
            (int) waterTemperature,
            voltage > voltageMaxThreshold ? "20V" : "5V",
            voltage > voltageMaxThreshold ? "true" : "false",
            isCooling ? "true" : "false"
        );

        httpd_ws_frame_t pkt;
        memset(&pkt, 0, sizeof(httpd_ws_frame_t));
        pkt.payload = (uint8_t *) buf;
        pkt.type = HTTPD_WS_TYPE_TEXT;
        pkt.len = strlen(buf);

        ws_send_frame_to_all_clients(&pkt, webserver);

        vTaskDelay(pdMS_TO_TICKS(120));

//        uint32_t freeRam = esp_get_free_heap_size();
//        ESP_LOGI(TAG, "Free heap: %d", (int) freeRam);

        makeJob();
    }
}

void app_main(void)
{
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = (
            (1ULL << GPIO_NUM_HEAT)
            | (1ULL << GPIO_NUM_PELTIER)
            | (1ULL << GPIO_NUM_RGB_R)
            | (1ULL << GPIO_NUM_RGB_G) 
            | (1ULL << GPIO_NUM_RGB_B)
            | (1ULL << GPIO_NUM_LED_0) 
            | (1ULL << GPIO_NUM_LED_1)
            | (1ULL << GPIO_NUM_FAN)
        );
    io_conf.pull_down_en = 1;
    io_conf.pull_up_en = 0;
    gpio_config(&io_conf);

    io_conf.pin_bit_mask = (
            (1ULL << GPIO_NUM_PD_CFG2)
        );
    io_conf.pull_down_en = 0;
    io_conf.pull_up_en = 1;
    gpio_config(&io_conf);

    io_conf.pin_bit_mask = (
            (1ULL << GPIO_NUM_PD_CFG3)
    );
    io_conf.pull_down_en = 1;
    io_conf.pull_up_en = 0;
    gpio_config(&io_conf);

    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = 1ULL << GPIO_NUM_USB_VREF;
    io_conf.pull_down_en = 0;
    io_conf.pull_up_en = 0;
    gpio_config(&io_conf);

    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (
        (1ULL << GPIO_NUM_TR) |
        (1ULL << GPIO_NUM_TR_WATER) |
        (1ULL << GPIO_NUM_TR_RADIATOR)
    );
    io_conf.pull_down_en = 0;
    io_conf.pull_up_en = 1;
    gpio_config(&io_conf);

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_LOGI(TAG, "Init ADC");
    initAdc();
    ESP_LOGI(TAG, "Init PWM");
    initPwm();

    ESP_LOGI(TAG, "Init load");
    setHeatValue(0);
    setPeltierValue(22);
    setFanValue(16);

    ESP_LOGI(TAG, "Init soft PWM");
    xTaskCreate(&soft_pwm_task, "soft_pwm_task", 1024, NULL, 6, NULL);
    xTaskCreate(&pd_task, "pd_task", 8192 * 2, NULL, 2, NULL);
    xTaskCreate(&fan_soft_pwm_task, "fan_soft_pwm_task", 1024, NULL, 14, NULL);

    ESP_LOGI(TAG, "Init RGB");
    xTaskCreate(&rgb_task, "rgb_task", 2048, NULL, 12, NULL);

    ESP_LOGI(TAG, "Init report service");
    xTaskCreate(&report_task, "report_task", 2048, NULL, 8, NULL);

    ESP_LOGI(TAG, "Init everything");
    xTaskCreate(&pcb_led_task, "pcb_led_task", 512, NULL, 14 , NULL);

    ESP_LOGI(TAG, "Init wireless");
    initWifi();

    ESP_LOGI(TAG, "Init server");
    webserver = start_webserver();
}
