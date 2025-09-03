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
#include "ha/esp_zigbee_ha_standard.h"

#if !defined ZB_ZR_ROLE
#error Define ZB_ZR_ROLE in idf.py menuconfig to compile router source code.
#endif

static const char *TAG = "ROBO_SR2CH10A";

/********************* Define functions **************************/
static esp_err_t deferred_driver_init(void)
{
    static bool is_inited = false;
    if (!is_inited) {
        // TODO: Initialize device-specific drivers
        ESP_LOGI(TAG, "Device drivers initialized");
        is_inited = true;
    }
    return is_inited ? ESP_OK : ESP_FAIL;
}

static void bdb_start_top_level_commissioning_cb(uint8_t mode_mask)
{
    ESP_RETURN_ON_FALSE(esp_zb_bdb_start_top_level_commissioning(mode_mask) == ESP_OK, , TAG, "Failed to start Zigbee commissioning");
}

void esp_zb_app_signal_handler(esp_zb_app_signal_t *signal_struct)
{
    uint32_t *p_sg_p       = signal_struct->p_app_signal;
    esp_err_t err_status = signal_struct->esp_err_status;
    esp_zb_app_signal_type_t sig_type = *p_sg_p;
    switch (sig_type) {
    case ESP_ZB_ZDO_SIGNAL_SKIP_STARTUP:
        ESP_LOGI(TAG, "Initialize Zigbee stack");
        esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_INITIALIZATION);
        break;
    case ESP_ZB_BDB_SIGNAL_DEVICE_FIRST_START:
        ESP_LOGI(TAG, "Device first start");
        deferred_driver_init();
        esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING);
        break;
    case ESP_ZB_BDB_SIGNAL_DEVICE_REBOOT:
        ESP_LOGI(TAG, "Device reboot");
        deferred_driver_init();
        break;
    case ESP_ZB_BDB_SIGNAL_STEERING:
        if (err_status == ESP_OK) {
            ESP_LOGI(TAG, "Network steering was successful");
            esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_FINDING_BINDING);
        } else {
            ESP_LOGI(TAG, "Network steering was not successful");
            esp_zb_scheduler_alarm((esp_zb_callback_t)bdb_start_top_level_commissioning_cb, ESP_ZB_BDB_MODE_NETWORK_STEERING, 1000);
        }
        break;
    case ESP_ZB_BDB_SIGNAL_FINDING_BINDING:
        if (err_status == ESP_OK) {
            ESP_LOGI(TAG, "Finding binding was successful");
        } else {
            ESP_LOGI(TAG, "Finding binding was not successful");
            esp_zb_scheduler_alarm((esp_zb_callback_t)bdb_start_top_level_commissioning_cb, ESP_ZB_BDB_MODE_FINDING_BINDING, 1000);
        }
        break;
    default:
        ESP_LOGI(TAG, "ZDO signal: %s (0x%x), status: %s", esp_zb_zdo_signal_to_string(sig_type), sig_type, esp_err_to_name(err_status));
        break;
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "RoboSR2CH10A Zigbee Device Starting...");
    ESP_LOGI(TAG, "ESP-IDF Version: %s", esp_get_idf_version());
    
    /* Initialize NVS flash */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* Initialize Zigbee stack */
    ESP_ERROR_CHECK(esp_zb_init());
    ESP_LOGI(TAG, "Zigbee stack initialized");
    
    ESP_LOGI(TAG, "Device initialization complete");
}
