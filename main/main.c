#include <string.h>
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

static const char *TAG = "EFOGTECH-ISKRA";

static int usbVrefVoltage = -1;
static int heaterTemperature = -1;
static int targetTemperature = -1;
static bool isHeating = false;

#include "server.c"
#include "ws.c"

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
#define USB_VREF_ADC_CHANNEL ADC_CHANNEL_6

#define GPIO_RGB_R 9
#define GPIO_NUM_RGB_R GPIO_NUM_9

#define GPIO_RGB_G 10
#define GPIO_NUM_RGB_G GPIO_NUM_10

#define GPIO_RGB_B 11
#define GPIO_NUM_RGB_B GPIO_NUM_11

#define GPIO_FAN 12
#define GPIO_NUM_FAN GPIO_NUM_12

#define GPIO_HEAT 41
#define GPIO_NUM_HEAT GPIO_NUM_41

#define GPIO_PELTIER 40
#define GPIO_NUM_PELTIER GPIO_NUM_40

#define GPIO_TR 6
#define GPIO_NUM_TR GPIO_NUM_6

#define GPIO_USB_NC 17
#define GPIO_NUM_USB_NC GPIO_NUM_17

#define GPIO_USB_CON 18
#define GPIO_NUM_USB_CON GPIO_NUM_18

#define GPIO_USB_VREF 7
#define GPIO_NUM_USB_VREF GPIO_NUM_7

#define GPIO_GP_0 13
#define GPIO_NUM_GP_0 GPIO_NUM_13

#define GPIO_GP_1 14
#define GPIO_NUM_GP_1 GPIO_NUM_14

#define GPIO_LED_0 45
#define GPIO_NUM_LED_0 GPIO_NUM_45

#define GPIO_LED_1 45
#define GPIO_NUM_LED_1 GPIO_NUM_48

#define MAX_HTTP_RECV_BUFFER 512
#define MAX_HTTP_OUTPUT_BUFFER 2048

static adc_oneshot_unit_handle_t adc1_handle;
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

static void setPeltierValue(int value) {
    ledc_set_duty(PWM_MODE, PWM_CHANNEL_PELTIER, (int) ((value * PWM_MAX) / 255));
    ledc_update_duty(PWM_MODE, PWM_CHANNEL_PELTIER);
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

    ledc_channel_config_t ledc_peltier = {
        .speed_mode     = PWM_MODE,
        .channel        = PWM_CHANNEL_PELTIER,
        .timer_sel      = LEDC_TIMER_0,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = GPIO_PELTIER,
        .duty           = 0, 
        .hpoint         = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_peltier));

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

    // ledc_fade_func_install(0);
}

static void initAdc() {
    adc_oneshot_unit_init_cfg_t init_config1 = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &adc1_handle));

    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_12,
        .atten = ADC_ATTEN_DB_11,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, TEMP_ADC_CHANNEL, &config));
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, USB_VREF_ADC_CHANNEL, &config));
}

static void http_rest(int data)
{
    char local_response_buffer[MAX_HTTP_OUTPUT_BUFFER] = {0};
   
    esp_http_client_config_t config = {
        // .host = "efog.tech",
        // .path = "/api/batmon",
        // .query = "update",
        .event_handler = _http_event_handler,
        .user_data = local_response_buffer,        // Pass address of local buffer to get response
        .disable_auto_redirect = true,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);

    char post_data[32];
    sprintf(post_data, "{\"test\":%d}", data);
    esp_http_client_set_url(client, "http://efog.tech/api/batmon/update");
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, post_data, strlen(post_data));
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "HTTP POST Status = %d, content_length = %lld",
                esp_http_client_get_status_code(client),
                esp_http_client_get_content_length(client));
    } else {
        ESP_LOGE(TAG, "HTTP POST request failed: %s", esp_err_to_name(err));
    }


    esp_http_client_cleanup(client);
}

void makeJob() {
    int temperature = -1, usbVref = -1;

    adc_oneshot_read(adc1_handle, TEMP_ADC_CHANNEL, &temperature);
    adc_oneshot_read(adc1_handle, USB_VREF_ADC_CHANNEL, &usbVref);

    if (temperature <= 0 || usbVref <= 0) {
        ESP_LOGI(TAG, "ADC ERROR!");
    }

    usbVrefVoltage = usbVref;
    heaterTemperature = temperature;

    ws_update_voltage(usbVref);
    ws_update_temperature(temperature);
    // http_rest(result);
}

static void http_test_task(void *pvParameters)
{
    bool level = 0;
    int counter = 0;

    gpio_set_level(GPIO_NUM_LED_1, 1);

    while (1) {
        counter++;

        if (counter % 7 == 0) {
            // blink = work :)
            level = level == 1 ? 0 : 1;
            gpio_set_level(GPIO_NUM_LED_0, level);
        }

        makeJob();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

extern const char root_start[] asm("_binary_root_html_start");
extern const char root_end[] asm("_binary_root_html_end");

static esp_err_t root_get_handler(httpd_req_t *req)
{
    const uint32_t root_len = root_end - root_start;

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, root_start, root_len);

    return ESP_OK;
}

static const httpd_uri_t root = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = root_get_handler
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
    config.max_open_sockets = 6;
    config.lru_purge_enable = true;

    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_register_uri_handler(server, &root);
        httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, http_404_error_handler);
        initWebsocket(server);
    }

    return server;
}

// static bool fade = false;
// static int fadeSpeed = 
// static int fadeTo = 0;
// static int fadeCurrent = 0;

static void rgb_task(void *pvParameters)
{
    int duty = 0;
    const int BRIGHTNESS_MAX = RGB_MAX * 0.7;

    while (1) {
        duty += 8;
        duty *= 1.013;

        if (duty >= BRIGHTNESS_MAX) {
            vTaskDelay(pdMS_TO_TICKS(600));
            duty = 0;
        }

        ledc_set_duty(PWM_MODE, RGB_CHANNEL_G, duty);
        ledc_update_duty(PWM_MODE, RGB_CHANNEL_G);

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static void report_task(void *pvParameters) {
    vTaskDelay(pdMS_TO_TICKS(2000));

    while (1) {
        char buf[128];
        sprintf(buf,
            "{\"type\":\"update\",\"content\":{\"heat\":%s,\"t\":\"%d\",\"v\":\"%d\"}}",
            isHeating ? "true" : "false",
            heaterTemperature,
            usbVrefVoltage
        );

        httpd_ws_frame_t pkt;
        memset(&pkt, 0, sizeof(httpd_ws_frame_t));
        pkt.payload = (uint8_t *) buf;
        pkt.type = HTTPD_WS_TYPE_TEXT;
        pkt.len = strlen(buf);

        ws_send_frame_to_all_clients(&pkt, webserver);

        vTaskDelay(pdMS_TO_TICKS(100));
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
            | (1ULL << GPIO_NUM_USB_CON) 
            | (1ULL << GPIO_NUM_USB_NC) 
            | (1ULL << GPIO_NUM_LED_0) 
            | (1ULL << GPIO_NUM_LED_1) 
        );  
    io_conf.pull_down_en = 1;
    io_conf.pull_up_en = 0;
    gpio_config(&io_conf);

    gpio_config_t io_conf2 = {};
    io_conf2.intr_type = GPIO_INTR_DISABLE;
    io_conf2.mode = GPIO_MODE_INPUT;
    io_conf2.pin_bit_mask = 1ULL << GPIO_NUM_USB_VREF;  
    io_conf2.pull_down_en = 1;
    io_conf2.pull_up_en = 0;
    gpio_config(&io_conf2);

    gpio_config_t io_conf3 = {};
    io_conf3.intr_type = GPIO_INTR_DISABLE;
    io_conf3.mode = GPIO_MODE_INPUT;
    io_conf3.pin_bit_mask = 1ULL << GPIO_NUM_TR;  
    io_conf3.pull_down_en = 0;
    io_conf3.pull_up_en = 1;
    gpio_config(&io_conf3);

    gpio_set_level(GPIO_NUM_FAN, 0);
    gpio_set_level(GPIO_NUM_USB_NC, 0);
    gpio_set_level(GPIO_NUM_USB_CON, 1);

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

    ESP_LOGI(TAG, "Init server");
    webserver = start_webserver();

    ESP_LOGI(TAG, "Init DNS");
    start_dns_server();

    ESP_LOGI(TAG, "Init wireless");
    initWifi();

    ESP_LOGI(TAG, "Init load");
    setHeatValue(0);
    setPeltierValue(0);

    ESP_LOGI(TAG, "Init RGB");
    xTaskCreate(&rgb_task, "rgb_task", 1024, NULL, 12, NULL);

    ESP_LOGI(TAG, "Init report service");
    xTaskCreate(&report_task, "report_task", 2048, NULL, 15, NULL);

    ESP_LOGI(TAG, "Init everything");
    xTaskCreate(&http_test_task, "http_test_task", 4096, NULL, 5, NULL);
}
