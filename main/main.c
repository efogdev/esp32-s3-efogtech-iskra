#include <string.h>
#include <math.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/FreeRTOSConfig.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
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
#include "adc_cali_interface.h"
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
#include "driver/temperature_sensor.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "const.c"
#include "esp_event.h"
#include "PID/PID.h"

static const char *TAG = "EFOGTECH";

static nvs_handle_t nvs;
static TaskHandle_t dns_task_handle = NULL;
static volatile int requestUsbPdVolts = 0;
static volatile int usbVrefVoltage = -1;
static volatile int usbVrefVoltageValue = -1;
static volatile int usbVrefVoltageMv = -1;
static char voltageString[24] = "";
static volatile int cc1 = -1, cc2 = -1;
static volatile int boardTemperature = -1;
static volatile int heaterTemperature = -1;
static volatile int heaterTemperatureC = -1;
static volatile int heaterTemperatureConverted = -1;
static volatile int heaterPwm = 0;
static volatile int waterTemperature = -1;
static volatile int radiatorTemperature = -1;
static volatile int targetTemperature = 220;
static volatile int fanValue = 0;
static uint32_t freeRam = 0;
static volatile bool isAuthEnabled = true;
static volatile bool isThinking = false;
static volatile bool isPDTesting = false;
static volatile bool isHeating = false;
static volatile bool pdCapable12 = false, pdCapable15 = false, pdCapable20 = false;
static volatile bool isWaitingForConnection = true;
static const int voltageThreshold = 1100;
temperature_sensor_handle_t temp_sensor = NULL;
static adc_cali_handle_t adc_cali_handle;

static esp_err_t start_file_server(httpd_handle_t);
static void makeAdcReads();
static void rgb_tick();
static void rgb_init();
static void ws_log(char*);

static volatile enum THINKING_REASON thinkingReason = REASON_DEFAULT;

#include "server.c"
#include "ws.c"
#include "ota.c"

#define PWM_FREQ 12000
#define PWM_RESOLUTION LEDC_TIMER_5_BIT
#define PWM_MAX 31
#define PWM_MODE LEDC_LOW_SPEED_MODE

#define REPORT_URL "http://efog.tech/api/batmon/update"

#define PWM_CHANNEL_HEAT LEDC_CHANNEL_0
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

#define GPIO_LED_HEAT 46
#define GPIO_NUM_LED_HEAT GPIO_NUM_46

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

static void setThinking(enum THINKING_REASON reason, bool think) {
    if (think) {
        thinkingReason = reason;
        isThinking = true;
        return;
    }

    if (!think && thinkingReason == reason) {
        isThinking = false;
    }
}

static void pdTest() {
    isPDTesting = true;
}

static httpd_handle_t getServer() {
    return webserver;
}

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

static void setAuth(bool auth) {
    isAuthEnabled = auth;
    nvs_set_u8(nvs, "dns_auth", auth ? 1 : 0);
}

static bool getAuth() {
    return isAuthEnabled;
}

static void toggleAuth() {
    setAuth(!getAuth());
}

static void setConnected() {
    isWaitingForConnection = false;
    set_rgb_stage(RGB_STAGE_IDLE);
}

static void setPD(int volts) {
    requestUsbPdVolts = volts;

    vTaskDelay(pdMS_TO_TICKS(64));
}

static void setTargetTemperature(int temp) {
    targetTemperature = temp;
}

static void startHeating() {
    isHeating = true;

    ledc_set_duty(PWM_MODE, PWM_CHANNEL_HEAT, PWM_MAX);
    ledc_update_duty(PWM_MODE, PWM_CHANNEL_HEAT);
}

static void disable_all() {
    isHeating = false;
    fanValue = 0;

    ledc_set_duty(PWM_MODE, PWM_CHANNEL_HEAT, 0);
    ledc_update_duty(PWM_MODE, PWM_CHANNEL_HEAT);

    gpio_set_level(GPIO_NUM_HEAT, 0);
    gpio_set_level(GPIO_NUM_FAN, 0);
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
    temperature_sensor_config_t temp_sensor_config = TEMPERATURE_SENSOR_CONFIG_DEFAULT(10, 50);
    ESP_ERROR_CHECK(temperature_sensor_install(&temp_sensor_config, &temp_sensor));

    ESP_LOGI(TAG, "Enable temperature sensor");
    ESP_ERROR_CHECK(temperature_sensor_enable(temp_sensor));

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

    adc_cali_curve_fitting_config_t cali_config = {
            .unit_id = ADC_UNIT_1,
            .atten = ADC_ATTEN_DB_11,
            .bitwidth = ADC_BITWIDTH_12,
    };
    ESP_ERROR_CHECK(adc_cali_create_scheme_curve_fitting(&cali_config, &adc_cali_handle));

    adc_cali_curve_fitting_config_t cali2_config = {
            .unit_id = ADC_UNIT_2,
            .atten = ADC_ATTEN_DB_11,
            .bitwidth = ADC_BITWIDTH_12,
    };
    ESP_ERROR_CHECK(adc_cali_create_scheme_curve_fitting(&cali2_config, &adc_cali_handle));

    makeAdcReads();
}

static volatile int averageVoltageRaw;
static volatile bool voltageWarning = false;
static volatile int vbuffer_index = 0;
#define vbuffer_size 16
static volatile int voltageBuffer[vbuffer_size];

static void makeAdcReads() {
    int sum_1 = 0, sum_2 = 0;
    uint8_t multisample = 127;

    for (int8_t i = 0; i < multisample; i++) {
        int temp_1 = 0, temp_2 = 0;

        adc_oneshot_read(adc1_handle, USB_VREF_ADC_CHANNEL, &temp_1);
        adc_oneshot_read(adc2_handle, TEMP_ADC_CHANNEL, &temp_2);

        sum_1 += temp_1;
        sum_2 += temp_2;
    }

    freeRam = esp_get_free_heap_size();

    usbVrefVoltage = sum_1 / multisample;
    heaterTemperature = sum_2 / multisample;

    float btemp;
    temperature_sensor_get_celsius(temp_sensor, &btemp);
    boardTemperature = (signed int) btemp;

    int usbCalibrated = 0;
    adc_cali_raw_to_voltage(adc_cali_handle, usbVrefVoltage, &usbCalibrated);

    int volts = (usbCalibrated * (10000 + 1000) / 1000) / 1000;
    int mV = ((usbCalibrated * (10000 + 1000) / 1000) % 1000) / 100;

    sprintf(voltageString, "%d.%dV", volts, mV);

    usbVrefVoltageValue = usbCalibrated;

    if (vbuffer_index >= vbuffer_size) {
        vbuffer_index = 0;
    }

    voltageBuffer[vbuffer_index] = usbCalibrated;

    ws_update_voltage(usbVrefVoltage);
    ws_update_temperature(heaterTemperature);

    vbuffer_index++;
}

extern const char root_start[] asm("_binary_index_html_start");
extern const char root_end[] asm("_binary_index_html_end");

extern const char manifest_start[] asm("_binary_manifest_json_start");
extern const char manifest_end[] asm("_binary_manifest_json_end");

extern const char ota_start[] asm("_binary_upload_html_start");
extern const char ota_end[] asm("_binary_upload_html_end");

extern const char bundle_start[] asm("_binary_bundle_js_start");
extern const char bundle_end[] asm("_binary_bundle_js_end");

extern const char sw_start[] asm("_binary_sw_js_start");
extern const char sw_end[] asm("_binary_sw_js_end");

extern const char icon_start[] asm("_binary_icon_png_start");
extern const char icon_end[] asm("_binary_icon_png_end");

static esp_err_t root_get_handler(httpd_req_t *req)
{
    const uint32_t root_len = root_end - root_start;

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, root_start, root_len);

    return ESP_OK;
}

static esp_err_t sw_get_handler(httpd_req_t *req)
{
    const uint32_t sw_len = sw_end - sw_start;

    httpd_resp_set_type(req, "text/javascript");
    httpd_resp_send(req, sw_start, sw_len);

    return ESP_OK;
}

static esp_err_t bundle_get_handler(httpd_req_t *req)
{
    const uint32_t bundle_len = bundle_end - bundle_start;

    httpd_resp_set_type(req, "text/javascript");
    httpd_resp_send(req, bundle_start, bundle_len);

    return ESP_OK;
}

static esp_err_t manifest_get_handler(httpd_req_t *req)
{
    const uint32_t manifest_len = manifest_end - manifest_start;

    httpd_resp_set_type(req, "application/json");
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

static esp_err_t icon_get_handler(httpd_req_t *req)
{
    const uint32_t icon_len = icon_end - icon_start;

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, icon_start, icon_len);

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

static const httpd_uri_t sw = {
     .uri = "/sw.js",
     .method = HTTP_GET,
     .handler = sw_get_handler,
     .user_ctx = NULL
};

static const httpd_uri_t icon = {
     .uri = "/icon.png",
     .method = HTTP_GET,
     .handler = icon_get_handler,
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

    config.task_priority = 2;
    config.server_port = 80;
    config.max_open_sockets = 12;
    config.max_uri_handlers = 12;
    config.lru_purge_enable = true;
    config.send_wait_timeout = 3;
    config.uri_match_fn = httpd_uri_match_wildcard;

    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_register_uri_handler(server, &ota);
        httpd_register_uri_handler(server, &manifest);
        httpd_register_uri_handler(server, &bundle);
        httpd_register_uri_handler(server, &sw);
        httpd_register_uri_handler(server, &icon);
        httpd_register_uri_handler(server, &root);

        initWebsocket(server);
        start_file_server(server);

        if (isAuthEnabled) {
            start_dns_server(dns_task_handle);
        }

        httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, http_404_error_handler);
    } else {
        esp_restart();
    }

    return server;
}

static void rgb_set(int r, int g, int b) {
    ledc_set_duty(PWM_MODE, RGB_CHANNEL_R, r);
    ledc_update_duty(PWM_MODE, RGB_CHANNEL_R);

    ledc_set_duty(PWM_MODE, RGB_CHANNEL_G, g);
    ledc_update_duty(PWM_MODE, RGB_CHANNEL_G);

    ledc_set_duty(PWM_MODE, RGB_CHANNEL_B, b);
    ledc_update_duty(PWM_MODE, RGB_CHANNEL_B);
}

static void rgb_task(void *pvParameters)
{
    rgb_clean();

    gpio_set_level(GPIO_NUM_LED_0, 1);
    vTaskDelay(pdMS_TO_TICKS(300));
    gpio_set_level(GPIO_NUM_LED_0, 0);

    while (1) {
        gpio_set_level(GPIO_NUM_LED_HEAT, heaterTemperatureC > 120);

        if (usbVrefVoltage < voltageThreshold || isPDTesting) {
            rgb_set(0, 0, 0);
            vTaskDelay(pdMS_TO_TICKS(320));
            continue;
        }

        rgb_tick();
        vTaskDelay(pdMS_TO_TICKS(24));
    }
}

static void call_911_task(void *pvParameters) {
    while (1) {
        if (heaterTemperatureC > 300 && heaterTemperatureC > targetTemperature + 20) {
            isHeating = 0;
            targetTemperature = 0;
            heaterPwm = 0;
            stopHeating();
        }

        if (boardTemperature > 70) {
            esp_system_abort("OVERHEAT");
        }

        if (heaterTemperatureConverted > 360) {
            stopHeating();
            esp_restart();
        }

        vTaskDelay(pdMS_TO_TICKS(128));
    }
}

static void pd_task(void *pvParameters) {
    int actualValue = 0;

    gpio_set_level(GPIO_NUM_PD_CFG2, 1);
    gpio_set_level(GPIO_NUM_PD_CFG3, 1);

    uint8_t isPdTested;
    nvs_get_u8(nvs, "pd_tested", &isPdTested);

    if (isPdTested == 1) {
        isPDTesting = false;

        uint8_t vcc12, vcc15, vcc20;
        nvs_get_u8(nvs, "vcc12", &vcc12);
        nvs_get_u8(nvs, "vcc15", &vcc15);
        nvs_get_u8(nvs, "vcc20", &vcc20);

        if (vcc12 == 1)
            pdCapable12 = true;

        if (vcc15 == 1)
            pdCapable15 = true;

        if (vcc20 == 1)
            pdCapable20 = true;

        if (pdCapable15)
            setPD(15);
    } else {
        isPDTesting = true;
    }

    nvs_set_u8(nvs, "pd_tested", 0);

    while (1) {
        if (usbVrefVoltage < voltageThreshold) {
            disable_all();
        }

        if (isPDTesting && (usbVrefVoltage > voltageThreshold)) {
            setThinking(REASON_TEST, true);
            disable_all();
            vTaskDelay(pdMS_TO_TICKS(120));

            nvs_set_u8(nvs, "vcc12", 0);
            nvs_set_u8(nvs, "vcc15", 0);
            nvs_set_u8(nvs, "vcc20", 0);
            nvs_set_u8(nvs, "pd_tested", 1);

            ledc_set_duty(PWM_MODE, PWM_CHANNEL_HEAT, PWM_MAX);
            ledc_update_duty(PWM_MODE, PWM_CHANNEL_HEAT);

            gpio_set_level(GPIO_NUM_PD_CFG2, 0);
            gpio_set_level(GPIO_NUM_PD_CFG3, 1);
            vTaskDelay(pdMS_TO_TICKS(400));

            pdCapable12 = true;
            nvs_set_u8(nvs, "vcc12", 1);

            gpio_set_level(GPIO_NUM_PD_CFG2, 1);
            gpio_set_level(GPIO_NUM_PD_CFG3, 1);
            vTaskDelay(pdMS_TO_TICKS(400));

            pdCapable15 = true;
            nvs_set_u8(nvs, "vcc15", 1);

            gpio_set_level(GPIO_NUM_PD_CFG2, 1);
            gpio_set_level(GPIO_NUM_PD_CFG3, 0);
            vTaskDelay(pdMS_TO_TICKS(400));

            pdCapable20 = true;
            nvs_set_u8(nvs, "vcc20", 1);

            if (pdCapable15) {
                setPD(15);
            } else {
                setPD(12);
            }

            nvs_set_u8(nvs, "pd_tested", 0);
            setThinking(REASON_TEST, false);
            isPDTesting = false;

            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        if (vbuffer_index > 0) {
            int count = 0, sum = 0;
            for (int i = 0; i < vbuffer_index; i++) {
                int value = voltageBuffer[i];

                if (value > 0) {
                    count++;
                    sum += value;
                }
            }

            if (count > 0) {
                averageVoltageRaw = sum / count;
            }
        }

        if (averageVoltageRaw > 0) {
            int percentChange = 100 - (usbVrefVoltageValue * 100 / averageVoltageRaw);

            if (percentChange < -5) {
                char text[64];
                sprintf(text, "Voltage peak? Vraw_avg: %d, Vraw: %d", averageVoltageRaw, usbVrefVoltageValue);
                ws_log(text);
            }

            if (percentChange > 5) {
                char text[64];
                sprintf(text, "Voltage drop! Vraw_avg = %d, Vraw = %d", averageVoltageRaw, usbVrefVoltageValue);
                ws_log(text);

                voltageWarning = true;

                if (isHeating) {
                    stopHeating();
                    vTaskDelay(pdMS_TO_TICKS(250));
                    startHeating();
                }

                vTaskDelay(pdMS_TO_TICKS(1400));
                voltageWarning = false;
            }
        }

        if (requestUsbPdVolts == actualValue) {
            vTaskDelay(pdMS_TO_TICKS(128));
            continue;
        }

        setThinking(REASON_PD, true);

        actualValue = requestUsbPdVolts;
        vTaskDelay(pdMS_TO_TICKS(12));

        // 0 1   12V
        // 1 1   15V
        // 1 0   20V

        if (requestUsbPdVolts == 12) {
            gpio_set_level(GPIO_NUM_PD_CFG2, 0);
            gpio_set_level(GPIO_NUM_PD_CFG3, 1);
        } else if (requestUsbPdVolts == 15) {
            gpio_set_level(GPIO_NUM_PD_CFG2, 1);
            gpio_set_level(GPIO_NUM_PD_CFG3, 1);
        } else if (requestUsbPdVolts == 20) {
            gpio_set_level(GPIO_NUM_PD_CFG2, 1);
            gpio_set_level(GPIO_NUM_PD_CFG3, 0);
        }

        vTaskDelay(pdMS_TO_TICKS(250));
        setThinking(REASON_PD, false);
    }
}

static float kp = 4.91, ki = 0.86, kd = 0.92;
static float pid_input = 0, pid_output = PWM_MAX, pid_setpoint = 0;

static struct pid_controller pid_ctrl_data;
static pid_ctrl_t pid;

static int displayUnitsToTemp(int displayUnits) {
    return displayUnits;
}

static int tempToDisplayUnits(int temp) {
    return temp;
}

static void heater_task(void *pvParameters) {
    pid = pid_create(&pid_ctrl_data, &pid_input, &pid_output, &pid_setpoint, kp, ki, kd);
    pid_limits(pid, 0, PWM_MAX);
    pid_auto(pid);

    while (1) {
        float temperature;
        float ntcResistance = 5600 * (1 / (3.3 / ((3.3 * heaterTemperature) / 4096) - 1));

        temperature = ntcResistance / 104000;
        temperature = log(temperature);
        temperature /= 5460;
        temperature += 1.0 / (25 + 273.15);
        temperature = 1.0 / temperature;
        temperature -= 273.15;

        heaterTemperatureConverted = tempToDisplayUnits((int) temperature);
        heaterTemperatureC = temperature;

        if (!isPDTesting) {
            if (isHeating && (usbVrefVoltage > voltageThreshold)) {
                pid_setpoint = targetTemperature;

                if (pid_need_compute(pid)) {
                    pid_input = temperature;
                    pid_compute(pid);

                    heaterPwm = pid_output;
                }

                ledc_set_duty(PWM_MODE, PWM_CHANNEL_HEAT, heaterPwm);
                ledc_update_duty(PWM_MODE, PWM_CHANNEL_HEAT);
            } else {
                ledc_set_duty(PWM_MODE, PWM_CHANNEL_HEAT, 0);
                ledc_update_duty(PWM_MODE, PWM_CHANNEL_HEAT);

                heaterPwm = 0;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(192));
    }
}

static void report_task(void *pvParameters) {
    while (1) {
        if (!webserver) {
            vTaskDelay(pdMS_TO_TICKS(128));
            continue;
        }

        char buf[640];
        sprintf(buf,
            "{\"type\":\"update\",\"content\":{\"isHeating\":%s,\"temperature\":\"%d\",\"voltage\":\"%s\",\"isVoltageOk\":%s,\"boardTemperature\":\"%d\",\"heaterPower\":\"%d\",\"fanPower\":\"%d\",\"freeRam\":\"%d\",\"voltageRaw\":\"%d\",\"isServerThinking\":%s,\"12V\":%s,\"15V\":%s,\"20V\":%s,\"speed\":%d,\"brightness\":%d,\"rgbCurrentFn\":%d,\"authEn\":%d}}",
            isHeating ? "true" : "false", // isHeating
            (int) heaterTemperatureConverted, // temperature, C
            voltageString, // voltage
            (!voltageWarning && (usbVrefVoltage > voltageThreshold)) ? "true" : "false", // isVoltageOk
            (int) boardTemperature, // board temp, C
            isHeating ? (int) (((float) heaterPwm / PWM_MAX) * 100) : 0, // heater %
            (int) (((float) fanValue / PWM_MAX) * 100), // fan %
            (int) freeRam / 1000, // free RAM, kb
            (int) usbVrefVoltage, // raw voltage data
            isThinking ? "true" : "false",
            pdCapable12 ? "true" : "false",
            pdCapable15 ? "true" : "false",
            pdCapable20 ? "true" : "false",
            rgbSpeed,
            rgbPower,
            rgbFn,
            isAuthEnabled ? 1 : 0
        );

        httpd_ws_frame_t pkt;
        memset(&pkt, 0, sizeof(httpd_ws_frame_t));
        pkt.payload = (uint8_t *) buf;
        pkt.type = HTTPD_WS_TYPE_TEXT;
        pkt.len = strlen(buf);

        ws_send_frame_to_all_clients(&pkt, webserver);
        makeAdcReads();

        vTaskDelay(pdMS_TO_TICKS(128));
    }
}

void app_main(void)
{
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = (
        (1ULL << GPIO_NUM_HEAT)
        | (1ULL << GPIO_NUM_RGB_R)
        | (1ULL << GPIO_NUM_RGB_G)
        | (1ULL << GPIO_NUM_RGB_B)
        | (1ULL << GPIO_NUM_LED_0)
        | (1ULL << GPIO_NUM_LED_1)
        | (1ULL << GPIO_NUM_LED_HEAT)
        | (1ULL << GPIO_NUM_FAN)
    );
    io_conf.pull_down_en = 1;
    io_conf.pull_up_en = 0;
    gpio_config(&io_conf);

    io_conf.pin_bit_mask = (
        (1ULL << GPIO_NUM_PD_CFG2) |
        (1ULL << GPIO_NUM_PD_CFG3)
    );
    io_conf.pull_down_en = 0;
    io_conf.pull_up_en = 1;
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

    nvs_open("storage", NVS_READWRITE, &nvs);

    uint8_t authEn = 2;
    nvs_get_u8(nvs, "dns_auth", &authEn);

    if (authEn != 2) {
        isAuthEnabled = (bool) authEn;
    }

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(esp_netif_init());

    ESP_LOGI(TAG, "Init emergency");
    xTaskCreatePinnedToCore(&call_911_task, "call_911_task", 1024, NULL, 1, NULL, 0);

    ESP_LOGI(TAG, "Init ADC");
    initAdc();

    ESP_LOGI(TAG, "Init PWM");
    initPwm();

    ESP_LOGI(TAG, "Init PD");
    xTaskCreatePinnedToCore(&pd_task, "pd_task", 4096, NULL, 11, NULL, 1);

    ESP_LOGI(TAG, "Init RGB");
    rgb_init();
    xTaskCreatePinnedToCore(&rgb_task, "rgb_task", 4096, NULL, 12, NULL, 1);

    ESP_LOGI(TAG, "Init wireless");
    initWifi();

    ESP_LOGI(TAG, "Init server");
    webserver = start_webserver();

    ESP_LOGI(TAG, "Init report service");
    xTaskCreatePinnedToCore(&report_task, "report_task", 8192, NULL, 2, NULL, 1);

    ESP_LOGI(TAG, "Init heater service");
    xTaskCreatePinnedToCore(&heater_task, "heater_task", 4096, NULL, 6, NULL, 1);

    ESP_LOGI(TAG, "Initialization successful");
}
