/*
 * RoboSR2CH10A Zigbee Router Device
 *
 * Этот код предназначен для устройства RoboSR2CH10A Zigbee Router.
 * Устройство реализовано как Zigbee Router (маршрутизатор) с возможностью ретрансляции сигналов.
 * 
 * Основные функции устройства:
 * - Ретрансляция Zigbee сигналов между устройствами
 * - Расширение покрытия Zigbee сети
 * - Маршрутизация данных в сети
 * - Поддержка подключения новых устройств к сети
 * - Автоматическое подключение к существующей сети Coordinator
 */

#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_zigbee_core.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_netif.h"
#include "esp_zigbee_endpoint.h"
#include "ha/esp_zigbee_ha_standard.h"
#include "device_config.h"

// Определение тега для логирования
static const char *TAG = "ROBO_SR2CH10A";

/* Флаг для предотвращения циклических обновлений атрибутов */
static bool updating_from_zigbee = false;

/* Основные параметры устройства */
#define DEVICE_NAME                 "RoboSR2CH10A Zigbee Router"
#define DEVICE_MANUFACTURER         "Robo Technologies"
#define DEVICE_MODEL                "SR2CH10A"
#define DEVICE_VERSION              "1.0.0"
#define DEVICE_TYPE                 "Zigbee Router"
#define DEVICE_CAPABILITIES         "Relay Control, Network Extension"

/* Задачи */
#define GPIO_TASK_STACK_SIZE        2048
#define GPIO_TASK_PRIORITY          3
#define DEVICE_TASK_STACK_SIZE      4096
#define DEVICE_TASK_PRIORITY        4

/* Zigbee Router конфигурация */
#define ESP_ZB_ZR_CONFIG()                                       \
    {                                                            \
        .esp_zb_role = ESP_ZB_DEVICE_TYPE_ROUTER,                \
        .install_code_policy = false,                            \
        .nwk_cfg.zczr_cfg = {                                    \
            .max_children = 20,                                  \
        },                                                       \
    }

/* Callback wrapper functions */
static void bdb_start_top_level_commissioning_cb(uint8_t mode_mask)
{
    esp_zb_bdb_start_top_level_commissioning(mode_mask);
}

/**
 * @brief Обработчик атрибутов ZCL
 */
static esp_err_t zb_attribute_handler(esp_zb_zcl_set_attr_value_message_t *message)
{
    esp_err_t ret = ESP_OK;
    ESP_LOGI(TAG, "ZCL Attribute handler: EP=%d, Cluster=0x%04x, Attr=0x%04x, Size=%d",
             message->info.dst_endpoint, message->info.cluster,
             message->attribute.id, message->attribute.data.size);
    
    /* Обработка команд On/Off для обоих endpoints */
    if ((message->info.dst_endpoint == 1 || message->info.dst_endpoint == 2) &&
        message->info.cluster == ESP_ZB_ZCL_CLUSTER_ID_ON_OFF &&
        message->attribute.id == ESP_ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID &&
        message->attribute.data.type == ESP_ZB_ZCL_ATTR_TYPE_BOOL) {
        
        bool light_state = message->attribute.data.value ? *(bool *)message->attribute.data.value : false;
        uint8_t endpoint = message->info.dst_endpoint;
        uint8_t relay_num = (endpoint == 1) ? 1 : 2;
        relay_state_t relay_state = light_state ? RELAY_ON : RELAY_OFF;
        
        ESP_LOGI(TAG, "Received On/Off command: EP=%d, Relay=%d, State=%s", 
                 endpoint, relay_num, light_state ? "ON" : "OFF");
        
        /* Устанавливаем флаг для предотвращения циклических обновлений */
        updating_from_zigbee = true;
        
        /* Управление физическим реле */
        device_set_relay(relay_num, relay_state);
        
        /* Обновление состояния в структуре */
        device_status_t *status = device_get_status();
        if (relay_num == 1) {
            status->relay1_state = relay_state;
        } else {
            status->relay2_state = relay_state;
        }
        
        /* Сбрасываем флаг после обработки команды */
        updating_from_zigbee = false;
        
        ESP_LOGI(TAG, "Relay %d set to %s via Zigbee command", 
                 relay_num, relay_state == RELAY_ON ? "ON" : "OFF");
    }
    
    return ret;
}

/**
 * @brief Обработчик действий Zigbee
 */
static esp_err_t zb_action_handler(esp_zb_core_action_callback_id_t callback_id, const void *message)
{
    esp_err_t ret = ESP_OK;
    switch (callback_id) {
    case ESP_ZB_CORE_SET_ATTR_VALUE_CB_ID:
        ret = zb_attribute_handler((esp_zb_zcl_set_attr_value_message_t *)message);
        break;
    default:
        ESP_LOGW(TAG, "Receive Zigbee action(0x%x) callback", callback_id);
        break;
    }
    return ret;
}

/**
 * @brief Обновление атрибута On/Off в Zigbee
 * @param endpoint Номер endpoint (1 или 2)
 * @param state Состояние реле (RELAY_ON или RELAY_OFF)
 */
void update_relay_zigbee_attr(uint8_t endpoint, relay_state_t state)
{
    /* Если обновление идет от Zigbee команды, не обновляем атрибут повторно */
    if (updating_from_zigbee) {
        ESP_LOGD(TAG, "Skipping Zigbee attribute update - already updating from Zigbee command");
        return;
    }
    
    uint8_t attr_value = (state == RELAY_ON) ? 1 : 0;
    
    /* Блокируем Zigbee стек для безопасного обновления атрибута */
    esp_zb_lock_acquire(portMAX_DELAY);
    
    esp_zb_zcl_status_t status = esp_zb_zcl_set_attribute_val(
        endpoint,
        ESP_ZB_ZCL_CLUSTER_ID_ON_OFF,
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
        ESP_ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID,
        &attr_value,
        false
    );
    
    /* Разблокируем Zigbee стек */
    esp_zb_lock_release();
    
    if (status == ESP_ZB_ZCL_STATUS_SUCCESS) {
        ESP_LOGI(TAG, "Updated Zigbee attribute: EP=%d, State=%s", 
                 endpoint, state == RELAY_ON ? "ON" : "OFF");
    } else {
        ESP_LOGE(TAG, "Failed to update Zigbee attribute: EP=%d, Status=%d", 
                 endpoint, status);
    }
}

/**
 * @brief Задача обработки GPIO
 * 
 * Обрабатывает кнопки, обновляет индикаторы и управляет реле
 */
static void gpio_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Starting GPIO task...");
    
    /* Инициализация GPIO */
    device_gpio_init();
    
    /* Установка начального состояния */
    device_set_state(DEVICE_STATE_INIT);
    
    while (1) {
        /* Обработка кнопки */
        device_handle_button();
        
        /* Обновление LED */
        device_update_leds();
        
        /* Задержка для снижения нагрузки на CPU */
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

/**
 * @brief Задача управления устройством
 * 
 * Основная логика работы устройства
 */
static void device_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Starting device task...");
    
    device_status_t *status = device_get_status();
    
    while (1) {
        /* Обработка режима пэйринга */
        if (status->pairing_mode) {
            device_set_state(DEVICE_STATE_PAIRING);
            ESP_LOGI(TAG, "Device in pairing mode");
            
            /* Здесь можно добавить логику для входа в режим пэйринга */
            /* Например, отправка специальных команд Zigbee */
            
            /* Выход из режима пэйринга через 60 секунд */
            vTaskDelay(pdMS_TO_TICKS(60000));
            status->pairing_mode = false;
            ESP_LOGI(TAG, "Pairing mode timeout, returning to normal operation");
        }
        
        /* Обновление состояния устройства */
        device_update_leds();
        
        /* Задержка */
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/**
 * @brief Функция для логирования информации о Zigbee сети
 */
static void log_nwk_info(const char *status_string)
{
    esp_zb_ieee_addr_t extended_pan_id;
    esp_zb_get_extended_pan_id(extended_pan_id);
    ESP_LOGI(TAG, "%s (Extended PAN ID: %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x, PAN ID: 0x%04hx, "
                  "Channel:%d, Short Address: 0x%04hx)", status_string,
                  extended_pan_id[7], extended_pan_id[6], extended_pan_id[5], extended_pan_id[4],
                  extended_pan_id[3], extended_pan_id[2], extended_pan_id[1], extended_pan_id[0],
                  esp_zb_get_pan_id(), esp_zb_get_current_channel(), esp_zb_get_short_address());
}

/**
 * @brief Создание endpoint list для Router устройства с двумя реле
 * 
 * Создаем два независимых endpoint для управления реле:
 * - Endpoint 1: Управление Реле 1
 * - Endpoint 2: Управление Реле 2
 */
esp_zb_ep_list_t *esp_zb_router_ep_list_create(void)
{
    esp_zb_ep_list_t *ep_list = esp_zb_ep_list_create();
    
    /* Конфигурация для Реле 1 (Endpoint 1) */
    esp_zb_on_off_light_cfg_t relay1_cfg = {
        .basic_cfg = {
            .zcl_version = ESP_ZB_ZCL_BASIC_ZCL_VERSION_DEFAULT_VALUE,
            .power_source = ESP_ZB_ZCL_BASIC_POWER_SOURCE_DEFAULT_VALUE,
        },
        .identify_cfg = {
            .identify_time = ESP_ZB_ZCL_IDENTIFY_IDENTIFY_TIME_DEFAULT_VALUE,
        },
        .groups_cfg = {
        },
        .scenes_cfg = {
            .scenes_count = ESP_ZB_ZCL_SCENES_SCENE_COUNT_DEFAULT_VALUE,
            .current_scene = ESP_ZB_ZCL_SCENES_CURRENT_SCENE_DEFAULT_VALUE,
            .current_group = ESP_ZB_ZCL_SCENES_CURRENT_GROUP_DEFAULT_VALUE,
            .scene_valid = ESP_ZB_ZCL_SCENES_SCENE_VALID_DEFAULT_VALUE,
            .name_support = ESP_ZB_ZCL_SCENES_NAME_SUPPORT_DEFAULT_VALUE,
        },
        .on_off_cfg = {
            .on_off = ESP_ZB_ZCL_ON_OFF_ON_OFF_DEFAULT_VALUE,
        },
    };
    
    /* Конфигурация для Реле 2 (Endpoint 2) */
    esp_zb_on_off_light_cfg_t relay2_cfg = {
        .basic_cfg = {
            .zcl_version = ESP_ZB_ZCL_BASIC_ZCL_VERSION_DEFAULT_VALUE,
            .power_source = ESP_ZB_ZCL_BASIC_POWER_SOURCE_DEFAULT_VALUE,
        },
        .identify_cfg = {
            .identify_time = ESP_ZB_ZCL_IDENTIFY_IDENTIFY_TIME_DEFAULT_VALUE,
        },
        .groups_cfg = {
        },
        .scenes_cfg = {
            .scenes_count = ESP_ZB_ZCL_SCENES_SCENE_COUNT_DEFAULT_VALUE,
            .current_scene = ESP_ZB_ZCL_SCENES_CURRENT_SCENE_DEFAULT_VALUE,
            .current_group = ESP_ZB_ZCL_SCENES_CURRENT_GROUP_DEFAULT_VALUE,
            .scene_valid = ESP_ZB_ZCL_SCENES_SCENE_VALID_DEFAULT_VALUE,
            .name_support = ESP_ZB_ZCL_SCENES_NAME_SUPPORT_DEFAULT_VALUE,
        },
        .on_off_cfg = {
            .on_off = ESP_ZB_ZCL_ON_OFF_ON_OFF_DEFAULT_VALUE,
        },
    };
    
    /* Создаем cluster list для Реле 1 */
    esp_zb_cluster_list_t *relay1_cluster_list = esp_zb_on_off_light_clusters_create(&relay1_cfg);
    
    /* Создаем cluster list для Реле 2 */
    esp_zb_cluster_list_t *relay2_cluster_list = esp_zb_on_off_light_clusters_create(&relay2_cfg);
    
    /* Конфигурация Endpoint 1 (Реле 1) */
    esp_zb_endpoint_config_t relay1_endpoint_config = {
        .endpoint = 1,
        .app_profile_id = ESP_ZB_AF_HA_PROFILE_ID,
        .app_device_id = ESP_ZB_HA_ON_OFF_LIGHT_DEVICE_ID,
        .app_device_version = 1,
    };
    
    /* Конфигурация Endpoint 2 (Реле 2) */
    esp_zb_endpoint_config_t relay2_endpoint_config = {
        .endpoint = 2,
        .app_profile_id = ESP_ZB_AF_HA_PROFILE_ID,
        .app_device_id = ESP_ZB_HA_ON_OFF_LIGHT_DEVICE_ID,
        .app_device_version = 1,
    };
    
    /* Добавляем Endpoint 1 для Реле 1 */
    esp_zb_ep_list_add_ep(ep_list, relay1_cluster_list, relay1_endpoint_config);
    
    /* Добавляем Endpoint 2 для Реле 2 */
    esp_zb_ep_list_add_ep(ep_list, relay2_cluster_list, relay2_endpoint_config);
    
    ESP_LOGI(TAG, "Created 2 endpoints:");
    ESP_LOGI(TAG, "  - Endpoint 1: Relay 1 (On/Off Light)");
    ESP_LOGI(TAG, "  - Endpoint 2: Relay 2 (On/Off Light)");
    ESP_LOGI(TAG, "Both endpoints use HA Profile with On/Off Light Device ID");
    
    return ep_list;
}

/**
 * @brief Обработчик сигналов Zigbee стека
 * 
 * Обрабатывает все события от Zigbee стека:
 * - Инициализация стека
 * - Подключение к сети
 * - Обработка ошибок
 * - Ретрансляция сигналов
 */
void esp_zb_app_signal_handler(esp_zb_app_signal_t *signal_struct)
{
    uint32_t *p_sg_p = signal_struct->p_app_signal;
    esp_err_t err_status = signal_struct->esp_err_status;
    esp_zb_app_signal_type_t sig_type = *p_sg_p;
    const char *err_name = esp_err_to_name(err_status);
    
    switch (sig_type) {
    case ESP_ZB_ZDO_SIGNAL_SKIP_STARTUP:
        ESP_LOGI(TAG, "Initialize Zigbee stack for Router mode");
        esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_INITIALIZATION);
        break;
        
    case ESP_ZB_BDB_SIGNAL_DEVICE_FIRST_START:
    case ESP_ZB_BDB_SIGNAL_DEVICE_REBOOT:
        if (err_status == ESP_OK) {
            ESP_LOGI(TAG, "Device started up in%s factory-reset mode", 
                     esp_zb_bdb_is_factory_new() ? "" : " non");
            
            /* Устанавливаем атрибуты Basic Cluster для обоих endpoints */
            /* Manufacturer Name для обоих реле */
            esp_zb_zcl_set_attribute_val(1, ESP_ZB_ZCL_CLUSTER_ID_BASIC,
                                         ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                                         ESP_ZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID,
                                         (void *)"Robo Technologies", false);
            
            esp_zb_zcl_set_attribute_val(2, ESP_ZB_ZCL_CLUSTER_ID_BASIC,
                                         ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                                         ESP_ZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID,
                                         (void *)"Robo Technologies", false);
            
            /* Model Identifier для обоих реле */
            esp_zb_zcl_set_attribute_val(1, ESP_ZB_ZCL_CLUSTER_ID_BASIC,
                                         ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                                         ESP_ZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID,
                                         (void *)"SR2CH10A", false);
            
            esp_zb_zcl_set_attribute_val(2, ESP_ZB_ZCL_CLUSTER_ID_BASIC,
                                         ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                                         ESP_ZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID,
                                         (void *)"SR2CH10A", false);
            
            /* Location Description для идентификации реле */
            esp_zb_zcl_set_attribute_val(1, ESP_ZB_ZCL_CLUSTER_ID_BASIC,
                                         ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                                         ESP_ZB_ZCL_ATTR_BASIC_LOCATION_DESCRIPTION_ID,
                                         (void *)"Relay 1", false);
            
            esp_zb_zcl_set_attribute_val(2, ESP_ZB_ZCL_CLUSTER_ID_BASIC,
                                         ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                                         ESP_ZB_ZCL_ATTR_BASIC_LOCATION_DESCRIPTION_ID,
                                         (void *)"Relay 2", false);
            
            ESP_LOGI(TAG, "Basic cluster attributes set for both endpoints");
            
            if (esp_zb_bdb_is_factory_new()) {
                ESP_LOGI(TAG, "New device - starting Network Steering to find Coordinator");
                esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING);
            } else {
                ESP_LOGI(TAG, "Device rebooted - attempting to reconnect to network");
                esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING);
            }
        } else {
            ESP_LOGW(TAG, "%s failed with status: %s, retrying in 1 second", 
                     esp_zb_zdo_signal_to_string(sig_type), err_name);
            esp_zb_scheduler_alarm((esp_zb_callback_t)bdb_start_top_level_commissioning_cb,
                                   ESP_ZB_BDB_MODE_INITIALIZATION, 1000);
        }
        break;
        
    case ESP_ZB_BDB_SIGNAL_STEERING:
        if (err_status == ESP_OK) {
            log_nwk_info("Successfully joined Zigbee network");
            ESP_LOGI(TAG, "Router mode activated - ready to relay signals");
            
            /* Обновление состояния устройства */
            device_set_state(DEVICE_STATE_CONNECTED);
            
            /* Включение реле по умолчанию */
            device_set_relay(1, RELAY_OFF);
            device_set_relay(2, RELAY_OFF);
            
            ESP_LOGI(TAG, "Device ready for operation");
        } else {
            ESP_LOGI(TAG, "Network Steering failed (status: %s)", err_name);
            ESP_LOGI(TAG, "No Coordinator found in range. Retrying in 30 seconds...");
            
            /* Обновление состояния устройства */
            device_set_state(DEVICE_STATE_SEARCHING);
            
            esp_zb_scheduler_alarm((esp_zb_callback_t)bdb_start_top_level_commissioning_cb, 
                                   ESP_ZB_BDB_MODE_NETWORK_STEERING, 30000);
        }
        break;
        
    case ESP_ZB_ZDO_SIGNAL_LEAVE:
        if (err_status == ESP_OK) {
            ESP_LOGI(TAG, "Left network successfully");
        } else {
            ESP_LOGE(TAG, "Failed to leave network (status: %s)", err_name);
        }
        break;
        
    case ESP_ZB_ZDO_SIGNAL_DEVICE_ANNCE:
        {
            esp_zb_zdo_signal_device_annce_params_t *dev_annce_params = 
                (esp_zb_zdo_signal_device_annce_params_t *)esp_zb_app_signal_get_params(p_sg_p);
            ESP_LOGI(TAG, "New device commissioned or rejoined (short: 0x%04hx)", 
                     dev_annce_params->device_short_addr);
        }
        break;
        
    case ESP_ZB_NWK_SIGNAL_PERMIT_JOIN_STATUS:
        if (err_status == ESP_OK) {
            if (*(uint8_t *)esp_zb_app_signal_get_params(p_sg_p)) {
                ESP_LOGI(TAG, "Network(0x%04hx) is open for %d seconds", 
                         esp_zb_get_pan_id(), *(uint8_t *)esp_zb_app_signal_get_params(p_sg_p));
            } else {
                ESP_LOGW(TAG, "Network(0x%04hx) closed, devices joining not allowed.", 
                         esp_zb_get_pan_id());
            }
        }
        break;
        
    default:
        ESP_LOGI(TAG, "ZDO signal: %s (0x%x), status: %s", 
                 esp_zb_zdo_signal_to_string(sig_type), sig_type, err_name);
        break;
    }
}

/**
 * @brief Основная задача Zigbee стека
 * 
 * Инициализирует и запускает Zigbee стек в отдельной задаче FreeRTOS.
 * Это рекомендуется для ESP-IDF Zigbee приложений.
 */
static void zigbee_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Starting Zigbee Router task...");
    
    /* Инициализация Zigbee стека с конфигурацией Router */
    esp_zb_cfg_t zb_nwk_cfg = ESP_ZB_ZR_CONFIG();
    esp_zb_init(&zb_nwk_cfg);
    
    /* Создание endpoint list для Router устройства */
    esp_zb_ep_list_t *ep_list = esp_zb_router_ep_list_create();
    esp_zb_device_register(ep_list);
    
    /* Регистрация обработчика действий Zigbee */
    esp_zb_core_action_handler_register(zb_action_handler);
    
    /* Установка разрешенных каналов сети */
    esp_zb_set_channel_mask(ESP_ZB_TRANSCEIVER_ALL_CHANNELS_MASK);
    esp_zb_set_primary_network_channel_set(ESP_ZB_TRANSCEIVER_ALL_CHANNELS_MASK);
    esp_zb_set_secondary_network_channel_set(ESP_ZB_TRANSCEIVER_ALL_CHANNELS_MASK);
    
    ESP_LOGI(TAG, "Zigbee stack initialized as Router (ZCZR)");
    
    /* Запуск Zigbee стека */
    ESP_LOGI(TAG, "Starting Zigbee stack...");
    esp_zb_start(false);
    
    ESP_LOGI(TAG, "Starting Zigbee main loop...");
    
    /* Основной цикл обработки событий Zigbee */
    while (1) {
        /* Обработка событий Zigbee стека */
        esp_zb_stack_main_loop();
    }
}

/**
 * @brief Главная функция приложения
 * 
 * Инициализирует систему и создает задачу для Zigbee стека.
 */
void app_main(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "%s Starting...", DEVICE_NAME);
    ESP_LOGI(TAG, "Manufacturer: %s", DEVICE_MANUFACTURER);
    ESP_LOGI(TAG, "Model: %s", DEVICE_MODEL);
    ESP_LOGI(TAG, "Version: %s", DEVICE_VERSION);
    ESP_LOGI(TAG, "Type: %s", DEVICE_TYPE);
    ESP_LOGI(TAG, "Capabilities: %s", DEVICE_CAPABILITIES);
    ESP_LOGI(TAG, "ESP-IDF Version: %s", esp_get_idf_version());
    ESP_LOGI(TAG, "========================================");
    
    /* Инициализация NVS (Non-Volatile Storage) */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* Инициализация ESP network */
    ESP_ERROR_CHECK(esp_netif_init());
    
    /* Конфигурация платформы Zigbee */
    esp_zb_platform_config_t config = {
        .radio_config = {
            .radio_mode = ZB_RADIO_MODE_NATIVE,
        },
        .host_config = {
            .host_connection_mode = ZB_HOST_CONNECTION_MODE_NONE,
        },
    };
    ESP_ERROR_CHECK(esp_zb_platform_config(&config));
    
    /* Создание задач */
    ESP_LOGI(TAG, "Creating tasks...");
    
    /* Задача обработки GPIO */
    xTaskCreate(gpio_task, "GPIO_task", GPIO_TASK_STACK_SIZE, NULL, GPIO_TASK_PRIORITY, NULL);
    
    /* Задача управления устройством */
    xTaskCreate(device_task, "Device_task", DEVICE_TASK_STACK_SIZE, NULL, DEVICE_TASK_PRIORITY, NULL);
    
    /* Задача Zigbee стека */
    xTaskCreate(zigbee_task, "Zigbee_task", 4096, NULL, 5, NULL);
    
    ESP_LOGI(TAG, "Device initialization complete - waiting for Coordinator...");
    ESP_LOGI(TAG, "Button functions:");
    ESP_LOGI(TAG, "  - Short press: Toggle Relay 1");
    ESP_LOGI(TAG, "  - Long press (3s): Toggle pairing mode");
    ESP_LOGI(TAG, "LED indicators:");
    ESP_LOGI(TAG, "  - Status LED: Device state");
    ESP_LOGI(TAG, "  - Connection LED: Network status");
}