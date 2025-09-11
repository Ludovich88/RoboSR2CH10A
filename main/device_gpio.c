/*
 * Device GPIO Control
 * 
 * Управление GPIO пинами для устройства RoboSR2CH10A Zigbee Router
 * 
 * Устройство: ESP32-C6 Zigbee Router
 * Функции: 
 * - Управление двумя реле
 * - Обработка кнопки для пэйринга
 * - LED управляется через отдельную задачу в main.c
 */

#include "device_config.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "esp_zigbee_core.h"
#include "zcl/esp_zigbee_zcl_common.h"

static const char *TAG = "DEVICE_GPIO";

/* Глобальное состояние устройства */
static device_status_t g_device_status = {
    .state = DEVICE_STATE_INIT,
    .relay1_state = RELAY_OFF,
    .relay2_state = RELAY_OFF,
    .pairing_mode = false,
    .button_pressed = false,
    .button_press_time = 0
};

// LED управление перенесено в main.c (led_task)

/**
 * @brief Инициализация GPIO пинов
 */
void device_gpio_init(void)
{
    ESP_LOGI(TAG, "Initializing device GPIO...");
    
    /* Конфигурация кнопки пэйринга */
    gpio_config_t button_config = {
        .pin_bit_mask = (1ULL << PAIRING_BUTTON_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&button_config);
    
    /* Конфигурация реле 1 */
    gpio_config_t relay1_config = {
        .pin_bit_mask = (1ULL << RELAY_1_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&relay1_config);
    
    /* Конфигурация реле 2 */
    gpio_config_t relay2_config = {
        .pin_bit_mask = (1ULL << RELAY_2_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&relay2_config);
    
    /* Инициализация состояний */
    gpio_set_level(RELAY_1_GPIO, 0);
    gpio_set_level(RELAY_2_GPIO, 0);
    
    // LED управление перенесено в main.c (led_task)
    
    ESP_LOGI(TAG, "Device GPIO initialized successfully");
}

/**
 * @brief Управление реле
 * @param relay_num Номер реле (1 или 2)
 * @param state Состояние реле (RELAY_ON или RELAY_OFF)
 */
void device_set_relay(uint8_t relay_num, relay_state_t state)
{
    if (relay_num == 1) {
        g_device_status.relay1_state = state;
        gpio_set_level(RELAY_1_GPIO, state);
        ESP_LOGI(TAG, "Relay 1 set to %s", state == RELAY_ON ? "ON" : "OFF");
    } else if (relay_num == 2) {
        g_device_status.relay2_state = state;
        gpio_set_level(RELAY_2_GPIO, state);
        ESP_LOGI(TAG, "Relay 2 set to %s", state == RELAY_ON ? "ON" : "OFF");
    }
}

/**
 * @brief Обработка нажатия кнопки
 */
void device_handle_button(void)
{
    static bool last_button_state = true; // Кнопка активна LOW, поэтому true = не нажата
    bool current_button_state = gpio_get_level(PAIRING_BUTTON_GPIO);
    
    /* Обработка нажатия кнопки */
    if (last_button_state && !current_button_state) {
        /* Кнопка нажата */
        g_device_status.button_pressed = true;
        g_device_status.button_press_time = xTaskGetTickCount();
        ESP_LOGI(TAG, "Button pressed");
    }
    
    /* Обработка отпускания кнопки */
    if (!last_button_state && current_button_state) {
        /* Кнопка отпущена */
        g_device_status.button_pressed = false;
        uint32_t press_duration = xTaskGetTickCount() - g_device_status.button_press_time;
        
        if (press_duration < pdMS_TO_TICKS(3000)) {
            /* Короткое нажатие - переключение реле 1 */
            ESP_LOGD(TAG, "Short press - toggling Relay 1");
            relay_state_t new_state = (g_device_status.relay1_state == RELAY_ON) ? RELAY_OFF : RELAY_ON;
            device_set_relay(1, new_state);
            /* Обновление состояния реле для LED индикации будет в main.c */
        } else if (press_duration < pdMS_TO_TICKS(5000)) {
            /* Длинное нажатие (3-5 сек) - режим пэйринга */
            ESP_LOGI(TAG, "Long press - entering pairing mode");
            g_device_status.pairing_mode = true;
        } else {
            /* Очень длинное нажатие (5+ сек) - полная очистка и пэйринг */
            ESP_LOGI(TAG, "Very long press - factory reset and pairing mode");
            g_device_status.pairing_mode = true;
            g_device_status.factory_reset = true;
        }
    }
    
    last_button_state = current_button_state;
}

/**
 * @brief Обновление состояния устройства
 * @param new_state Новое состояние устройства
 */
void device_set_state(device_state_t new_state)
{
    g_device_status.state = new_state;
    ESP_LOGI(TAG, "Device state changed to: %d", new_state);
}

// LED callback функции перенесены в main.c (led_task)

/**
 * @brief Получение текущего состояния устройства
 * @return Указатель на структуру состояния устройства
 */
device_status_t* device_get_status(void)
{
    return &g_device_status;
}

/**
 * @brief Обновление атрибута On/Off в Zigbee
 * @param endpoint Номер endpoint (1 или 2)
 * @param state Состояние реле (RELAY_ON или RELAY_OFF)
 */
void update_relay_zigbee_attr(uint8_t endpoint, relay_state_t state)
{
    /* Внешняя переменная из main.c для предотвращения циклических обновлений */
    extern bool updating_from_zigbee;
    
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