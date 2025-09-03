/*
 * RoboSR2CH10A Zigbee Device
 *
 * This example code is for RoboSR2CH10A Zigbee device.
 * The device will be implemented as a Zigbee Router with signal relaying capabilities.
 */

#include "esp_check.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_zigbee_core.h"

static const char *TAG = "ROBO_SR2CH10A";

void esp_zb_app_signal_handler(esp_zb_app_signal_t *signal_struct)
{
    uint32_t *p_sg_p = signal_struct->p_app_signal;
    esp_err_t err_status = signal_struct->esp_err_status;
    esp_zb_app_signal_type_t sig_type = *p_sg_p;
    
    ESP_LOGI(TAG, "ZDO signal: %s (0x%x), status: %s", 
             esp_zb_zdo_signal_to_string(sig_type), sig_type, esp_err_to_name(err_status));
}

void app_main(void)
{
    ESP_LOGI(TAG, "RoboSR2CH10A Zigbee Router Starting...");
    ESP_LOGI(TAG, "ESP-IDF Version: %s", esp_get_idf_version());
    
    /* Initialize NVS flash */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* Initialize Zigbee stack */
    esp_zb_cfg_t zigbee_cfg = {
        .install_code_policy = false,
    };
    esp_zb_init(&zigbee_cfg);
    ESP_LOGI(TAG, "Zigbee stack initialized as Router");
    
    ESP_LOGI(TAG, "Device initialization complete");

    // Main application loop
    while (1) {
        // TODO: Implement main application logic
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}