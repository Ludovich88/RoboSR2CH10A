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
  #include "zcl/esp_zigbee_zcl_common.h"
#include "ha/esp_zigbee_ha_standard.h"
#include "device_config.h"

// Определение тега для логирования
static const char *TAG = "ROBO_SR2CH10A";

/* Флаг для предотвращения циклических обновлений атрибутов */
static bool updating_from_zigbee = false;

/* Состояния LED индикатора - Комбинированная логика */
typedef enum {
    LED_STATE_OFF = 0,           // Выключен - устройство не инициализировано
    LED_STATE_INIT_GPIO,         // 1 короткое мигание - GPIO инициализирован
    LED_STATE_INIT_ZIGBEE,       // 2 коротких мигания - Zigbee инициализирован
    LED_STATE_SEARCHING,         // Медленное мигание (1 сек) - поиск сети
    LED_STATE_CONNECTING,        // Быстрое мигание (0.5 сек) - подключение к сети
    LED_STATE_CONNECTED,         // Постоянно горит - подключен к сети
    LED_STATE_ERROR,             // Очень быстрое мигание (0.1 сек) - ошибка
    LED_STATE_PAIRING,           // Длинное мигание (2 сек) - режим пэйринга
    LED_STATE_RELAY_ACTIVE,      // Мигание при работе реле (0.3 сек)
    LED_STATE_FACTORY_RESET,     // 3 быстрых мигания - factory reset
    LED_STATE_NETWORK_LOST,      // 2 длинных мигания - потеря сети
    LED_STATE_REBOOTING          // 5 коротких миганий - перезагрузка
} led_state_t;

/* Текущее состояние LED */
static led_state_t current_led_state = LED_STATE_OFF;
static led_state_t previous_led_state = LED_STATE_OFF;
static bool relay1_active = false;
static bool relay2_active = false;
static uint32_t last_relay_change = 0;
static bool network_connected = false;

/* Функции управления LED */
static void led_set_state(led_state_t state);
static void led_task(void *pvParameters);
static void led_blink_pattern(uint8_t count, uint32_t on_time, uint32_t off_time);
static void led_continuous_blink(uint32_t on_time, uint32_t off_time);
static void led_show_sequence(uint8_t *pattern, uint8_t length, uint32_t base_time);
static void led_show_error_code(uint8_t error_code);

/* Функции отправки изменений состояния */
static void send_relay_state_change(uint8_t relay_num, relay_state_t state);
static void send_on_off_attribute(uint8_t endpoint, relay_state_t state);
static void send_all_relay_states(void);

/* Основные параметры устройства */
#define DEVICE_NAME                 "RoboSR2CH10A"
#define DEVICE_MANUFACTURER         "Robo"
#define DEVICE_MODEL                "SR2CH10A"
#define DEVICE_VERSION              "1.0.0"
#define DEVICE_TYPE                 "Zigbee Router"
#define DEVICE_CAPABILITIES         "Relay Control, Network Extension"

/* Задачи */
#define GPIO_TASK_STACK_SIZE        4096
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
            relay1_active = (relay_state == RELAY_ON);
        } else {
            status->relay2_state = relay_state;
            relay2_active = (relay_state == RELAY_ON);
        }
        last_relay_change = xTaskGetTickCount();
        
        /* НЕ отправляем изменение состояния, так как это команда от Zigbee */
        /* Флаг updating_from_zigbee предотвращает отправку обратно в сеть */
        
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
    led_set_state(LED_STATE_OFF);
    
    /* Переменная для мониторинга стека */
    UBaseType_t stack_high_water_mark;
    
    while (1) {
        /* Обработка кнопки */
        device_handle_button();
        
        /* Обновление состояния реле для LED индикации */
        device_status_t *status = device_get_status();
        bool new_relay1_active = (status->relay1_state == RELAY_ON);
        bool new_relay2_active = (status->relay2_state == RELAY_ON);
        
        /* Проверяем изменения состояния реле и отправляем в Zigbee2MQTT */
        if (new_relay1_active != relay1_active) {
            relay1_active = new_relay1_active;
            if (!updating_from_zigbee) {
                send_relay_state_change(1, status->relay1_state);
            }
        }
        
        if (new_relay2_active != relay2_active) {
            relay2_active = new_relay2_active;
            if (!updating_from_zigbee) {
                send_relay_state_change(2, status->relay2_state);
            }
        }
        
        /* LED управляется через отдельную задачу led_task */
        
        /* Мониторинг стека каждые 100 итераций */
        static uint32_t iteration_count = 0;
        if (++iteration_count >= 100) {
            iteration_count = 0;
            stack_high_water_mark = uxTaskGetStackHighWaterMark(NULL);
            if (stack_high_water_mark < 512) {
                ESP_LOGW(TAG, "GPIO task stack low: %d bytes remaining", stack_high_water_mark * sizeof(StackType_t));
            }
        }
        
        /* Периодическая отправка состояния реле для синхронизации (каждые 30 секунд) */
        static uint32_t last_sync_time = 0;
        uint32_t current_time = xTaskGetTickCount();
        if (current_time - last_sync_time > pdMS_TO_TICKS(30000)) {
            last_sync_time = current_time;
            if (network_connected && !updating_from_zigbee) {
                send_all_relay_states();
            }
        }
        
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
            led_set_state(LED_STATE_PAIRING);
            ESP_LOGI(TAG, "Device in pairing mode");
            
            /* Очистка данных предыдущего пэйринга */
            if (status->factory_reset) {
                ESP_LOGI(TAG, "Factory reset requested - performing full memory cleanup");
                led_set_state(LED_STATE_FACTORY_RESET);
                vTaskDelay(pdMS_TO_TICKS(2000)); // Показываем индикацию
                clear_zigbee_data();
                /* clear_zigbee_data() вызывает esp_restart(), поэтому код ниже не выполнится */
            } else {
                ESP_LOGI(TAG, "Standard pairing mode - clearing Zigbee data only");
                clear_zigbee_data();
            }
            
            /* Выход из режима пэйринга через 60 секунд */
            vTaskDelay(pdMS_TO_TICKS(60000));
            status->pairing_mode = false;
            status->factory_reset = false;
            ESP_LOGI(TAG, "Pairing mode timeout, returning to normal operation");
        }
        
        /* Обновление состояния устройства */
        /* LED управляется через отдельную задачу led_task */
        
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
    
        /* Создаем Basic Cluster с правильными атрибутами для обоих endpoints */
        for (int ep = 1; ep <= 2; ep++) {
            /* Создаем Basic Cluster */
            esp_zb_attribute_list_t *basic_cluster = esp_zb_zcl_attr_list_create(ESP_ZB_ZCL_CLUSTER_ID_BASIC);
            
            /* Добавляем атрибуты Basic Cluster */
            uint8_t zcl_version = ESP_ZB_ZCL_BASIC_ZCL_VERSION_DEFAULT_VALUE;
            uint8_t power_source = ESP_ZB_ZCL_BASIC_POWER_SOURCE_MAINS_SINGLE_PHASE; // Устройство питается от сети 220V
            esp_zb_basic_cluster_add_attr(basic_cluster, ESP_ZB_ZCL_ATTR_BASIC_ZCL_VERSION_ID, &zcl_version);
            esp_zb_basic_cluster_add_attr(basic_cluster, ESP_ZB_ZCL_ATTR_BASIC_POWER_SOURCE_ID, &power_source);
            
            /* Создаем ZCL-строки с правильным форматом (длина + данные) */
            uint8_t manuf_name[1 + sizeof(DEVICE_MANUFACTURER)] = {0};
            uint8_t model_id[1 + sizeof(DEVICE_MODEL)] = {0};
            
            manuf_name[0] = strlen(DEVICE_MANUFACTURER);
            memcpy(&manuf_name[1], DEVICE_MANUFACTURER, manuf_name[0]);
            
            model_id[0] = strlen(DEVICE_MODEL);
            memcpy(&model_id[1], DEVICE_MODEL, model_id[0]);
            
            esp_zb_basic_cluster_add_attr(basic_cluster, ESP_ZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID, manuf_name);
            esp_zb_basic_cluster_add_attr(basic_cluster, ESP_ZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID, model_id);
            
            /* Identify Cluster */
            esp_zb_attribute_list_t *identify_cluster = esp_zb_zcl_attr_list_create(ESP_ZB_ZCL_CLUSTER_ID_IDENTIFY);
            uint16_t identify_time = ESP_ZB_ZCL_IDENTIFY_IDENTIFY_TIME_DEFAULT_VALUE;
            esp_zb_identify_cluster_add_attr(identify_cluster, ESP_ZB_ZCL_ATTR_IDENTIFY_IDENTIFY_TIME_ID, &identify_time);
            
            /* Groups Cluster */
            esp_zb_attribute_list_t *groups_cluster = esp_zb_zcl_attr_list_create(ESP_ZB_ZCL_CLUSTER_ID_GROUPS);
            uint8_t name_support = 0;
            esp_zb_groups_cluster_add_attr(groups_cluster, ESP_ZB_ZCL_ATTR_GROUPS_NAME_SUPPORT_ID, &name_support);
            
            /* Scenes Cluster */
            esp_zb_attribute_list_t *scenes_cluster = esp_zb_zcl_attr_list_create(ESP_ZB_ZCL_CLUSTER_ID_SCENES);
            uint8_t scene_count = ESP_ZB_ZCL_SCENES_SCENE_COUNT_DEFAULT_VALUE;
            uint8_t current_scene = ESP_ZB_ZCL_SCENES_CURRENT_SCENE_DEFAULT_VALUE;
            uint16_t current_group = ESP_ZB_ZCL_SCENES_CURRENT_GROUP_DEFAULT_VALUE;
            uint8_t scene_valid = ESP_ZB_ZCL_SCENES_SCENE_VALID_DEFAULT_VALUE;
            uint8_t name_support_scenes = ESP_ZB_ZCL_SCENES_NAME_SUPPORT_DEFAULT_VALUE;
            esp_zb_scenes_cluster_add_attr(scenes_cluster, ESP_ZB_ZCL_ATTR_SCENES_SCENE_COUNT_ID, &scene_count);
            esp_zb_scenes_cluster_add_attr(scenes_cluster, ESP_ZB_ZCL_ATTR_SCENES_CURRENT_SCENE_ID, &current_scene);
            esp_zb_scenes_cluster_add_attr(scenes_cluster, ESP_ZB_ZCL_ATTR_SCENES_CURRENT_GROUP_ID, &current_group);
            esp_zb_scenes_cluster_add_attr(scenes_cluster, ESP_ZB_ZCL_ATTR_SCENES_SCENE_VALID_ID, &scene_valid);
            esp_zb_scenes_cluster_add_attr(scenes_cluster, ESP_ZB_ZCL_ATTR_SCENES_NAME_SUPPORT_ID, &name_support_scenes);
            
            /* OnOff Cluster */
            esp_zb_attribute_list_t *on_off_cluster = esp_zb_zcl_attr_list_create(ESP_ZB_ZCL_CLUSTER_ID_ON_OFF);
            bool on_off_state = ESP_ZB_ZCL_ON_OFF_ON_OFF_DEFAULT_VALUE;
            esp_zb_on_off_cluster_add_attr(on_off_cluster, ESP_ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID, &on_off_state);
            
            /* Создаем cluster list для endpoint */
            esp_zb_cluster_list_t *cluster_list = esp_zb_zcl_cluster_list_create();
            esp_zb_cluster_list_add_basic_cluster(cluster_list, basic_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
            esp_zb_cluster_list_add_identify_cluster(cluster_list, identify_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
            esp_zb_cluster_list_add_groups_cluster(cluster_list, groups_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
            esp_zb_cluster_list_add_scenes_cluster(cluster_list, scenes_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
            esp_zb_cluster_list_add_on_off_cluster(cluster_list, on_off_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
            
            /* Конфигурация Endpoint */
            esp_zb_endpoint_config_t endpoint_config = {
                .endpoint = ep,
        .app_profile_id = ESP_ZB_AF_HA_PROFILE_ID,
        .app_device_id = ESP_ZB_HA_ON_OFF_LIGHT_DEVICE_ID,
        .app_device_version = 1,
    };
    
            /* Добавляем endpoint */
            esp_zb_ep_list_add_ep(ep_list, cluster_list, endpoint_config);
    
            ESP_LOGI(TAG, "Created Endpoint %d: Relay %d (On/Off Light) with Basic attributes", ep, ep);
        }
    
        ESP_LOGI(TAG, "Created endpoints with Manufacturer='%s' Model='%s'",
                 DEVICE_MANUFACTURER, DEVICE_MODEL);
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
            
             /* Устанавливаем Manufacturer Code для node descriptor */
            esp_zb_set_node_descriptor_manufacturer_code(ZIGBEE_MANUFACTURER_CODE);
            
            ESP_LOGI(TAG, "Basic cluster attributes set for both endpoints (Manufacturer Code: 0x%04X)", ZIGBEE_MANUFACTURER_CODE);
            
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
            network_connected = true;  // Явно устанавливаем флаг подключения
            led_set_state(LED_STATE_CONNECTED);
            
             /* Включение реле по умолчанию (выкл) */
            device_set_relay(1, RELAY_OFF);
            device_set_relay(2, RELAY_OFF);
            
            /* Отправляем начальное состояние реле в Zigbee2MQTT */
            vTaskDelay(pdMS_TO_TICKS(1000)); // Даем время для стабилизации соединения
            send_relay_state_change(1, RELAY_OFF);
            send_relay_state_change(2, RELAY_OFF);
            
            ESP_LOGI(TAG, "Device ready for operation");
        } else {
            ESP_LOGI(TAG, "Network Steering failed (status: %s)", err_name);
            ESP_LOGI(TAG, "No Coordinator found in range. Retrying in 30 seconds...");
            
            /* Обновление состояния устройства */
            device_set_state(DEVICE_STATE_SEARCHING);
            network_connected = false;  // Сбрасываем флаг при потере соединения
            led_set_state(LED_STATE_SEARCHING);
            
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
                led_set_state(LED_STATE_CONNECTING);
            } else {
                ESP_LOGW(TAG, "Network(0x%04hx) closed, devices joining not allowed.", 
                         esp_zb_get_pan_id());
                led_set_state(LED_STATE_SEARCHING);
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
    
    /* Устанавливаем источник питания на уровне узла - устройство питается от сети */
    esp_zb_set_node_descriptor_power_source(true); // true = main power (сеть 220V)
    
    esp_zb_init(&zb_nwk_cfg);
    
    /* Создание endpoint list для Router устройства */
    esp_zb_ep_list_t *ep_list = esp_zb_router_ep_list_create();
    esp_zb_device_register(ep_list);
        
        ESP_LOGI(TAG, "Basic cluster attributes set during endpoint creation: Manufacturer='%s', Model='%s'",
                DEVICE_MANUFACTURER, DEVICE_MODEL);
    
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
 * @brief Очистка данных Zigbee координатора
 * 
 * Очищает NVS разделы zb_storage и zb_fct, а также выполняет factory reset
 * для удаления старых данных о координаторе перед новым пэйрингом.
 */
void clear_zigbee_data(void)
{
    ESP_LOGI(TAG, "Clearing Zigbee coordinator data...");
    
    /* Очистка NVS раздела zb_storage */
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("zb_storage", NVS_READWRITE, &nvs_handle);
    if (err == ESP_OK) {
        nvs_erase_all(nvs_handle);
        nvs_commit(nvs_handle);
        nvs_close(nvs_handle);
        ESP_LOGI(TAG, "Zigbee storage data cleared");
    } else {
        ESP_LOGW(TAG, "Failed to open Zigbee storage: %s", esp_err_to_name(err));
    }
    
    /* Очистка NVS раздела zb_fct */
    err = nvs_open("zb_fct", NVS_READWRITE, &nvs_handle);
    if (err == ESP_OK) {
        nvs_erase_all(nvs_handle);
        nvs_commit(nvs_handle);
        nvs_close(nvs_handle);
        ESP_LOGI(TAG, "Zigbee factory data cleared");
    } else {
        ESP_LOGW(TAG, "Failed to open Zigbee factory storage: %s", esp_err_to_name(err));
    }
    
    /* Дополнительная очистка основного NVS раздела */
    err = nvs_open("nvs", NVS_READWRITE, &nvs_handle);
    if (err == ESP_OK) {
        /* Очищаем только Zigbee-связанные ключи */
        nvs_erase_key(nvs_handle, "zb_network");
        nvs_erase_key(nvs_handle, "zb_security");
        nvs_erase_key(nvs_handle, "zb_address");
        nvs_commit(nvs_handle);
        nvs_close(nvs_handle);
        ESP_LOGI(TAG, "Main NVS Zigbee keys cleared");
    }
    
    /* Выполнение factory reset для Zigbee стека */
    esp_zb_factory_reset();
    ESP_LOGI(TAG, "Zigbee factory reset flag set");
    
    /* Принудительная перезагрузка для полной очистки памяти */
    ESP_LOGI(TAG, "Rebooting device to complete memory cleanup...");
    led_set_state(LED_STATE_REBOOTING);
    vTaskDelay(pdMS_TO_TICKS(3000)); // Показываем индикацию перезагрузки
    esp_restart();
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
    /* Инициализация NVS с полной очисткой при необходимости */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGI(TAG, "NVS partition was truncated and needs to be erased");
        ESP_LOGI(TAG, "Erasing the entire NVS partition...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    /* Дополнительная очистка Zigbee разделов при первом запуске */
    ESP_LOGI(TAG, "Checking Zigbee storage partitions...");

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
    
    /* Задача управления LED */
    xTaskCreate(led_task, "LED_task", 4096, NULL, 5, NULL);
    
    /* Задача управления устройством */
    xTaskCreate(device_task, "Device_task", DEVICE_TASK_STACK_SIZE, NULL, DEVICE_TASK_PRIORITY, NULL);
    
    /* Задача Zigbee стека */
    xTaskCreate(zigbee_task, "Zigbee_task", 4096, NULL, 5, NULL);
    
    ESP_LOGI(TAG, "Device initialization complete - waiting for Coordinator...");
    ESP_LOGI(TAG, "Button functions:");
    ESP_LOGI(TAG, "  - Short press (<3s): Toggle Relay 1 (sends state to Zigbee2MQTT)");
    ESP_LOGI(TAG, "  - Long press (3-5s): Enter pairing mode");
    ESP_LOGI(TAG, "  - Very long press (5s+): Factory reset + pairing mode");
    ESP_LOGI(TAG, "Relay state synchronization:");
    ESP_LOGI(TAG, "  - Manual changes sent to Zigbee2MQTT automatically");
    ESP_LOGI(TAG, "  - Periodic sync every 30 seconds");
    ESP_LOGI(TAG, "  - Protection against command loops");
    ESP_LOGI(TAG, "LED indicators (Combined Logic):");
    ESP_LOGI(TAG, "  - Status LED: Device state with combined logic");
    ESP_LOGI(TAG, "    * Off: Not initialized");
    ESP_LOGI(TAG, "    * 1 blink: GPIO initialized");
    ESP_LOGI(TAG, "    * 2 blinks: Zigbee initialized");
    ESP_LOGI(TAG, "    * Slow blink (2s): Searching network");
    ESP_LOGI(TAG, "    * Fast blink (0.5s): Connecting to network");
    ESP_LOGI(TAG, "    * On: Connected and ready");
    ESP_LOGI(TAG, "    * Blink when relay active: Relay 1 or 2 is ON");
    ESP_LOGI(TAG, "    * Very fast blink: Error");
    ESP_LOGI(TAG, "    * Long blink (5s): Pairing mode");
    ESP_LOGI(TAG, "    * 3 fast blinks: Factory reset");
    ESP_LOGI(TAG, "    * 2 long blinks: Network lost");
    ESP_LOGI(TAG, "    * 5 short blinks: Rebooting");
}

/* ============================================================================
 * LED Control Functions
 * ============================================================================ */

/**
 * @brief Установка состояния LED с умной логикой
 */
static void led_set_state(led_state_t state)
{
    /* Сохраняем предыдущее состояние */
    previous_led_state = current_led_state;
    current_led_state = state;
    
    /* Обновляем флаг подключения к сети */
    if (state == LED_STATE_CONNECTED) {
        network_connected = true;
    } else if (state == LED_STATE_CONNECTING) {
        network_connected = false;
    }
    /* Не сбрасываем network_connected при LED_STATE_SEARCHING, 
       так как это может быть временное состояние */
    
    ESP_LOGD(TAG, "LED state changed: %d -> %d", previous_led_state, current_led_state);
}

/**
 * @brief Выполнение паттерна мигания
 */
static void led_blink_pattern(uint8_t count, uint32_t on_time, uint32_t off_time)
{
    for (uint8_t i = 0; i < count; i++) {
        gpio_set_level(STATUS_LED_GPIO, 1);
        vTaskDelay(pdMS_TO_TICKS(on_time));
        gpio_set_level(STATUS_LED_GPIO, 0);
        if (i < count - 1) {
            vTaskDelay(pdMS_TO_TICKS(off_time));
        }
    }
}

/**
 * @brief Непрерывное мигание
 */
static void led_continuous_blink(uint32_t on_time, uint32_t off_time)
{
    gpio_set_level(STATUS_LED_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(on_time));
    gpio_set_level(STATUS_LED_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(off_time));
}

/**
 * @brief Показать последовательность миганий
 * @param pattern Массив состояний (1=включено, 0=выключено)
 * @param length Длина массива
 * @param base_time Базовое время для каждого состояния
 */
static void led_show_sequence(uint8_t *pattern, uint8_t length, uint32_t base_time)
{
    for (uint8_t i = 0; i < length; i++) {
        gpio_set_level(STATUS_LED_GPIO, pattern[i]);
        vTaskDelay(pdMS_TO_TICKS(base_time));
    }
    gpio_set_level(STATUS_LED_GPIO, 0);
}

/**
 * @brief Показать код ошибки (количество миганий)
 * @param error_code Код ошибки (1-9)
 */
static void led_show_error_code(uint8_t error_code)
{
    if (error_code == 0 || error_code > 9) return;
    
    /* Пауза перед показом кода */
    vTaskDelay(pdMS_TO_TICKS(500));
    
    /* Показываем код ошибки */
    for (uint8_t i = 0; i < error_code; i++) {
        gpio_set_level(STATUS_LED_GPIO, 1);
        vTaskDelay(pdMS_TO_TICKS(200));
        gpio_set_level(STATUS_LED_GPIO, 0);
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    
    /* Длинная пауза после кода */
    vTaskDelay(pdMS_TO_TICKS(1000));
}

/* ============================================================================
 * Relay State Change Functions
 * ============================================================================ */

/**
 * @brief Отправка изменения состояния реле в Zigbee2MQTT
 * @param relay_num Номер реле (1 или 2)
 * @param state Новое состояние реле
 */
static void send_relay_state_change(uint8_t relay_num, relay_state_t state)
{
    /* Проверяем, что устройство подключено к сети */
    if (!network_connected) {
        ESP_LOGW(TAG, "Cannot send relay state change - not connected to network");
        return;
    }
    
    /* Определяем endpoint */
    uint8_t endpoint = (relay_num == 1) ? 1 : 2;
    
    /* Отправляем изменение атрибута On/Off */
    send_on_off_attribute(endpoint, state);
    
    ESP_LOGI(TAG, "Relay %d state change sent to Zigbee2MQTT: %s", 
             relay_num, state == RELAY_ON ? "ON" : "OFF");
}

/**
 * @brief Отправка атрибута On/Off в Zigbee сеть
 * @param endpoint Номер endpoint (1 или 2)
 * @param state Состояние реле
 */
static void send_on_off_attribute(uint8_t endpoint, relay_state_t state)
{
    /* Используем простую функцию для установки атрибута */
    uint8_t value = (state == RELAY_ON) ? 0x01 : 0x00;
    
    /* Блокируем Zigbee стек */
    esp_zb_lock_acquire(portMAX_DELAY);
    
    /* Устанавливаем атрибут в локальном кластере */
    esp_zb_zcl_status_t ret = esp_zb_zcl_set_attribute_val(endpoint, 
                                                           ESP_ZB_ZCL_CLUSTER_ID_ON_OFF,
                                                           ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                                                           ESP_ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID,
                                                           &value, 
                                                           false);
    
    /* Отправляем отчет об изменении атрибута */
    if (ret == ESP_ZB_ZCL_STATUS_SUCCESS) {
        /* Создаем команду отчета об атрибуте */
        esp_zb_zcl_report_attr_cmd_t report_cmd = {0};
        report_cmd.zcl_basic_cmd.dst_addr_u.addr_short = 0x0000; // Координатор
        report_cmd.zcl_basic_cmd.src_endpoint = endpoint;
        report_cmd.zcl_basic_cmd.dst_endpoint = 0x01;
        report_cmd.address_mode = ESP_ZB_APS_ADDR_MODE_16_ENDP_PRESENT;
        report_cmd.direction = ESP_ZB_ZCL_CMD_DIRECTION_TO_CLI;
        report_cmd.clusterID = ESP_ZB_ZCL_CLUSTER_ID_ON_OFF;
        report_cmd.attributeID = ESP_ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID;
        
        esp_err_t report_ret = esp_zb_zcl_report_attr_cmd_req(&report_cmd);
        if (report_ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to send attribute report for endpoint %d: %s", 
                     endpoint, esp_err_to_name(report_ret));
        }
    }
    
    esp_zb_lock_release();
    
    if (ret != ESP_ZB_ZCL_STATUS_SUCCESS) {
        ESP_LOGE(TAG, "Failed to set On/Off attribute for endpoint %d: %d", 
                 endpoint, ret);
    } else {
        ESP_LOGI(TAG, "On/Off attribute sent for endpoint %d: %s", 
                 endpoint, state == RELAY_ON ? "ON" : "OFF");
    }
}

/**
 * @brief Отправка состояния всех реле в Zigbee2MQTT
 */
static void send_all_relay_states(void)
{
    if (!network_connected) {
        ESP_LOGW(TAG, "Cannot send relay states - not connected to network");
        return;
    }
    
    device_status_t *status = device_get_status();
    
    /* Отправляем состояние реле 1 */
    send_relay_state_change(1, status->relay1_state);
    vTaskDelay(pdMS_TO_TICKS(100)); // Небольшая задержка между отправками
    
    /* Отправляем состояние реле 2 */
    send_relay_state_change(2, status->relay2_state);
    
    ESP_LOGI(TAG, "All relay states sent to Zigbee2MQTT");
}

/**
 * @brief Задача управления LED
 */
static void led_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Starting LED task...");
    
    /* Инициализация GPIO для LED */
    gpio_config_t led_config = {
        .pin_bit_mask = (1ULL << STATUS_LED_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&led_config);
    gpio_set_level(STATUS_LED_GPIO, 0);
    
    /* Показываем инициализацию GPIO */
    led_set_state(LED_STATE_INIT_GPIO);
    led_blink_pattern(1, 200, 0);
    vTaskDelay(pdMS_TO_TICKS(500));
    
    /* Показываем инициализацию Zigbee */
    led_set_state(LED_STATE_INIT_ZIGBEE);
    led_blink_pattern(2, 200, 200);
    vTaskDelay(pdMS_TO_TICKS(500));
    
    /* Переходим в режим поиска сети */
    led_set_state(LED_STATE_SEARCHING);
    
    while (1) {
        /* Мониторинг стека каждые 1000 итераций */
        static uint32_t led_iteration_count = 0;
        if (++led_iteration_count >= 1000) {
            led_iteration_count = 0;
            UBaseType_t stack_high_water_mark = uxTaskGetStackHighWaterMark(NULL);
            if (stack_high_water_mark < 512) {
                ESP_LOGW(TAG, "LED task stack low: %d bytes remaining", stack_high_water_mark * sizeof(StackType_t));
            }
        }
        
        /* Проверяем, что если мы подключены к сети, не переходим в режим поиска */
        if (current_led_state == LED_STATE_SEARCHING && network_connected) {
            ESP_LOGW(TAG, "LED: Network is connected but LED shows searching - fixing state");
            led_set_state(LED_STATE_CONNECTED);
        }
        
        switch (current_led_state) {
            case LED_STATE_OFF:
                gpio_set_level(STATUS_LED_GPIO, 0);
                vTaskDelay(pdMS_TO_TICKS(100));
                break;
                
            case LED_STATE_INIT_GPIO:
                led_blink_pattern(1, 200, 0);
                vTaskDelay(pdMS_TO_TICKS(1000));
                break;
                
            case LED_STATE_INIT_ZIGBEE:
                led_blink_pattern(2, 200, 200);
                vTaskDelay(pdMS_TO_TICKS(1000));
                break;
                
            case LED_STATE_SEARCHING:
                led_continuous_blink(2000, 2000);  // Медленное мигание (2 сек)
                break;
                
            case LED_STATE_CONNECTING:
                led_continuous_blink(500, 500);    // Быстрое мигание
                break;
                
            case LED_STATE_CONNECTED:
                /* Если реле активны, показываем их состояние */
                if (relay1_active || relay2_active) {
                    led_continuous_blink(300, 300);
                } else {
                    gpio_set_level(STATUS_LED_GPIO, 1);
                    vTaskDelay(pdMS_TO_TICKS(100));
                }
                break;
                
            case LED_STATE_ERROR:
                led_continuous_blink(100, 100);    // Очень быстрое мигание
                break;
                
            case LED_STATE_PAIRING:
                led_continuous_blink(5000, 1000);  // Длинное мигание (5 сек)
                break;
                
            case LED_STATE_RELAY_ACTIVE:
                led_continuous_blink(300, 300);    // Мигание при работе реле
                break;
                
            case LED_STATE_FACTORY_RESET:
                {
                    uint8_t pattern[] = {1, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0};
                    led_show_sequence(pattern, sizeof(pattern), 150);
                    vTaskDelay(pdMS_TO_TICKS(2000));
                }
                break;
                
            case LED_STATE_NETWORK_LOST:
                led_blink_pattern(2, 1000, 500);   // 2 длинных мигания
                vTaskDelay(pdMS_TO_TICKS(2000));
                break;
                
            case LED_STATE_REBOOTING:
                led_blink_pattern(5, 200, 200);    // 5 коротких миганий
                vTaskDelay(pdMS_TO_TICKS(2000));
                break;
                
            default:
                gpio_set_level(STATUS_LED_GPIO, 0);
                vTaskDelay(pdMS_TO_TICKS(100));
                break;
        }
    }
}