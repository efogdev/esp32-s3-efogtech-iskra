#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_ota_ops.h"
#include "esp_flash_partitions.h"
#include "esp_partition.h"
#include "esp_log.h"
#include "esp_http_server.h"

#include "esp_system.h"
#include "esp_app_format.h"

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

#define BUFFSIZE 8192
static char ota_write_data[BUFFSIZE + 1] = {0};

static void disable_all();
static const char *OTA_TAG = "OTA";

#include "const.c"
static void setThinking(enum THINKING_REASON, bool);

static esp_err_t upload_post_handler(httpd_req_t *req)
{
    esp_err_t err;
    /* update handle : set by esp_ota_begin(), must be freed via esp_ota_end() */
    esp_ota_handle_t update_handle = 0;
    const esp_partition_t *update_partition = NULL;

    ESP_LOGI(OTA_TAG, "Starting OTA");

    esp_ota_get_boot_partition();
    const esp_partition_t *running = esp_ota_get_running_partition();

    update_partition = esp_ota_get_next_update_partition(NULL);
    assert(update_partition != NULL);

    int received;

    /* Content length of the request gives
     * the size of the file being uploaded */
    int remaining = req->content_len;
    bool image_header_was_checked = false;
    while (remaining > 0)
    {
        ESP_LOGI(OTA_TAG, "Remaining size : %d", remaining);
        /* Receive the file part by part into a buffer */
        if ((received = httpd_req_recv(req, ota_write_data, MIN(remaining, BUFFSIZE))) <= 0)
        {
            if (received == HTTPD_SOCK_ERR_TIMEOUT)
            {
                /* Retry if timeout occurred */
                continue;
            }

            ESP_LOGE(OTA_TAG, "File reception failed!");
            /* Respond with 500 Internal Server Error */
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to receive file");
            return ESP_FAIL;
        }

        if (image_header_was_checked == false)
        {
            setThinking(REASON_FW, true);

            esp_app_desc_t new_app_info;
            if (received > sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t))
            {
                // check current version with downloading
                memcpy(&new_app_info, &ota_write_data[sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t)], sizeof(esp_app_desc_t));
                ESP_LOGI(OTA_TAG, "New firmware version: %s", new_app_info.version);

                esp_app_desc_t running_app_info;
                if (esp_ota_get_partition_description(running, &running_app_info) == ESP_OK)
                {
                    ESP_LOGI(OTA_TAG, "Running firmware version: %s", running_app_info.version);
                }

                const esp_partition_t *last_invalid_app = esp_ota_get_last_invalid_partition();
                esp_app_desc_t invalid_app_info;
                if (esp_ota_get_partition_description(last_invalid_app, &invalid_app_info) == ESP_OK)
                {
                    ESP_LOGI(OTA_TAG, "Last invalid firmware version: %s", invalid_app_info.version);
                }

                image_header_was_checked = true;

                err = esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &update_handle);
                if (err != ESP_OK)
                {
                    ESP_LOGE(OTA_TAG, "esp_ota_begin failed (%s)", esp_err_to_name(err));
                    esp_ota_abort(update_handle);
                }
                ESP_LOGI(OTA_TAG, "esp_ota_begin succeeded");
            }
            else
            {
                ESP_LOGE(OTA_TAG, "received package is not fit len");
                esp_ota_abort(update_handle);
            }
        }
        err = esp_ota_write(update_handle, (const void *)ota_write_data, received);
        if (err != ESP_OK)
        {
            esp_ota_abort(update_handle);
        }
        ESP_LOGD(OTA_TAG, "Written image length %d", received);

        /* Keep track of remaining size of
         * the file left to be uploaded */
        remaining -= received;
    }

    err = esp_ota_end(update_handle);
    if (err != ESP_OK)
    {
        if (err == ESP_ERR_OTA_VALIDATE_FAILED)
        {
            ESP_LOGE(OTA_TAG, "Image validation failed, image is corrupted");
        }
        else
        {
            ESP_LOGE(OTA_TAG, "esp_ota_end failed (%s)!", esp_err_to_name(err));
        }
    }

    setThinking(REASON_FW, false);

    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK)
    {
        ESP_LOGE(OTA_TAG, "esp_ota_set_boot_partition failed (%s)!", esp_err_to_name(err));
    }
    ESP_LOGI(OTA_TAG, "File reception complete");

    httpd_resp_set_hdr(req, "Connection", "close");
    httpd_resp_sendstr(req, "File uploaded successfully");

    disable_all();
    esp_restart();

    return ESP_OK;
}

static esp_err_t __attribute__((unused)) start_file_server(httpd_handle_t server)
{
    httpd_uri_t file_upload = {
        .uri = "/upload",
        .method = HTTP_POST,
        .handler = upload_post_handler,
    };
    httpd_register_uri_handler(server, &file_upload);

    return ESP_OK;
}
