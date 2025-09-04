/*
 * Device GPIO Implementation
 * 
 * Реализация функций управления GPIO для устройства RoboSR2CH10A Zigbee Router
 */

#include "device_config.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "driver/gpio.h"

static const char *TAG = "DEVICE_GPIO";

/* Глобальная структура состояния устройства */
static device_status_t g_device_status = {
    .state = DEVICE_STATE_INIT,
    .relay1_state = RELAY_OFF,
    .relay2_state = RELAY_OFF,
    .status_led_state = LED_OFF,
    .connection_led_state = LED_OFF,
    .pairing_mode = false,
    .button_pressed = false,
    .button_press_time = 0
};

/* Таймеры для мигания LED */
static TimerHandle_t status_led_timer = NULL;
static TimerHandle_t connection_led_timer = NULL;

/* Callback функции для таймеров LED */
static void status_led_timer_callback(TimerHandle_t xTimer);
static void connection_led_timer_callback(TimerHandle_t xTimer);

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
    
    /* Конфигурация LED статуса */
    gpio_config_t status_led_config = {
        .pin_bit_mask = (1ULL << STATUS_LED_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&status_led_config);
    
    /* Конфигурация LED подключения */
    gpio_config_t connection_led_config = {
        .pin_bit_mask = (1ULL << CONNECTION_LED_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&connection_led_config);
    
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
    gpio_set_level(STATUS_LED_GPIO, 0);
    gpio_set_level(CONNECTION_LED_GPIO, 0);
    gpio_set_level(RELAY_1_GPIO, 0);
    gpio_set_level(RELAY_2_GPIO, 0);
    
    /* Создание таймеров для мигания LED */
    status_led_timer = xTimerCreate("status_led_timer", 
                                   pdMS_TO_TICKS(100), 
                                   pdTRUE, 
                                   (void*)0, 
                                   status_led_timer_callback);
    
    connection_led_timer = xTimerCreate("connection_led_timer", 
                                       pdMS_TO_TICKS(100), 
                                       pdTRUE, 
                                       (void*)0, 
                                       connection_led_timer_callback);
    
    ESP_LOGI(TAG, "Device GPIO initialized successfully");
}

/**
 * @brief Управление реле
 * @param relay_num Номер реле (1 или 2)
 * @param state Состояние реле (RELAY_ON или RELAY_OFF)
 */
void device_set_relay(uint8_t relay_num, relay_state_t state)
{
    gpio_num_t relay_gpio;
    
    if (relay_num == 1) {
        relay_gpio = RELAY_1_GPIO;
        g_device_status.relay1_state = state;
    } else if (relay_num == 2) {
        relay_gpio = RELAY_2_GPIO;
        g_device_status.relay2_state = state;
    } else {
        ESP_LOGE(TAG, "Invalid relay number: %d", relay_num);
        return;
    }
    
    gpio_set_level(relay_gpio, state);
    ESP_LOGI(TAG, "Relay %d set to %s", relay_num, state == RELAY_ON ? "ON" : "OFF");
    
    /* Обновление Zigbee атрибута */
    update_relay_zigbee_attr(relay_num, state);
}

/**
 * @brief Управление LED статуса
 * @param state Состояние LED
 */
void device_set_status_led(led_state_t state)
{
    g_device_status.status_led_state = state;
    
    if (state == LED_OFF) {
        gpio_set_level(STATUS_LED_GPIO, 0);
        xTimerStop(status_led_timer, 0);
    } else if (state == LED_ON) {
        gpio_set_level(STATUS_LED_GPIO, 1);
        xTimerStop(status_led_timer, 0);
    } else {
        /* Настройка таймера для мигания */
        TickType_t period;
        switch (state) {
            case LED_BLINK_FAST:
                period = pdMS_TO_TICKS(LED_BLINK_FAST_MS);
                break;
            case LED_BLINK_SLOW:
                period = pdMS_TO_TICKS(LED_BLINK_SLOW_MS);
                break;
            case LED_BLINK_VERY_SLOW:
                period = pdMS_TO_TICKS(LED_BLINK_VERY_SLOW_MS);
                break;
            default:
                period = pdMS_TO_TICKS(LED_BLINK_SLOW_MS);
                break;
        }
        xTimerChangePeriod(status_led_timer, period, 0);
        xTimerStart(status_led_timer, 0);
    }
}

/**
 * @brief Управление LED подключения
 * @param state Состояние LED
 */
void device_set_connection_led(led_state_t state)
{
    g_device_status.connection_led_state = state;
    
    if (state == LED_OFF) {
        gpio_set_level(CONNECTION_LED_GPIO, 0);
        xTimerStop(connection_led_timer, 0);
    } else if (state == LED_ON) {
        gpio_set_level(CONNECTION_LED_GPIO, 1);
        xTimerStop(connection_led_timer, 0);
    } else {
        /* Настройка таймера для мигания */
        TickType_t period;
        switch (state) {
            case LED_BLINK_FAST:
                period = pdMS_TO_TICKS(LED_BLINK_FAST_MS);
                break;
            case LED_BLINK_SLOW:
                period = pdMS_TO_TICKS(LED_BLINK_SLOW_MS);
                break;
            case LED_BLINK_VERY_SLOW:
                period = pdMS_TO_TICKS(LED_BLINK_VERY_SLOW_MS);
                break;
            default:
                period = pdMS_TO_TICKS(LED_BLINK_SLOW_MS);
                break;
        }
        xTimerChangePeriod(connection_led_timer, period, 0);
        xTimerStart(connection_led_timer, 0);
    }
}

/**
 * @brief Обработка нажатия кнопки
 */
void device_handle_button(void)
{
    static bool last_button_state = true; // true = не нажата (pull-up)
    static uint32_t press_start_time = 0;
    bool current_button_state = gpio_get_level(PAIRING_BUTTON_GPIO);
    
    /* Обнаружение нажатия кнопки */
    if (last_button_state && !current_button_state) {
        /* Кнопка нажата */
        press_start_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
        g_device_status.button_pressed = true;
        g_device_status.button_press_time = press_start_time;
        ESP_LOGI(TAG, "Button pressed");
    }
    
    /* Обнаружение отпускания кнопки */
    if (!last_button_state && current_button_state) {
        /* Кнопка отпущена */
        uint32_t press_duration = (xTaskGetTickCount() * portTICK_PERIOD_MS) - press_start_time;
        g_device_status.button_pressed = false;
        
        if (press_duration >= BUTTON_DEBOUNCE_TIME_MS) {
            if (press_duration >= BUTTON_LONG_PRESS_TIME_MS) {
                ESP_LOGI(TAG, "Long button press detected (%u ms)", (unsigned int)press_duration);
                /* Длительное нажатие - переключение режима пэйринга */
                g_device_status.pairing_mode = !g_device_status.pairing_mode;
                ESP_LOGI(TAG, "Pairing mode: %s", g_device_status.pairing_mode ? "ON" : "OFF");
            } else {
                ESP_LOGI(TAG, "Short button press detected (%u ms)", (unsigned int)press_duration);
                /* Короткое нажатие - переключение реле */
                g_device_status.relay1_state = (g_device_status.relay1_state == RELAY_ON) ? RELAY_OFF : RELAY_ON;
                device_set_relay(1, g_device_status.relay1_state);
            }
        }
    }
    
    last_button_state = current_button_state;
}

/**
 * @brief Обновление состояния LED
 */
void device_update_leds(void)
{
    /* Обновление LED статуса в зависимости от состояния устройства */
    switch (g_device_status.state) {
        case DEVICE_STATE_INIT:
            device_set_status_led(LED_BLINK_VERY_SLOW);
            break;
        case DEVICE_STATE_SEARCHING:
            device_set_status_led(LED_BLINK_SLOW);
            break;
        case DEVICE_STATE_CONNECTING:
            device_set_status_led(LED_BLINK_FAST);
            break;
        case DEVICE_STATE_CONNECTED:
            device_set_status_led(LED_ON);
            break;
        case DEVICE_STATE_PAIRING:
            device_set_status_led(LED_BLINK_FAST);
            break;
        case DEVICE_STATE_ERROR:
            device_set_status_led(LED_BLINK_VERY_SLOW);
            break;
        default:
            device_set_status_led(LED_OFF);
            break;
    }
    
    /* Обновление LED подключения */
    if (g_device_status.state == DEVICE_STATE_CONNECTED) {
        device_set_connection_led(LED_ON);
    } else if (g_device_status.pairing_mode) {
        device_set_connection_led(LED_BLINK_FAST);
    } else {
        device_set_connection_led(LED_OFF);
    }
}

/**
 * @brief Callback для таймера LED статуса
 */
static void status_led_timer_callback(TimerHandle_t xTimer)
{
    static bool led_state = false;
    led_state = !led_state;
    gpio_set_level(STATUS_LED_GPIO, led_state);
}

/**
 * @brief Callback для таймера LED подключения
 */
static void connection_led_timer_callback(TimerHandle_t xTimer)
{
    static bool led_state = false;
    led_state = !led_state;
    gpio_set_level(CONNECTION_LED_GPIO, led_state);
}

/**
 * @brief Получение текущего состояния устройства
 */
device_status_t* device_get_status(void)
{
    return &g_device_status;
}

/**
 * @brief Установка состояния устройства
 */
void device_set_state(device_state_t state)
{
    g_device_status.state = state;
    device_update_leds();
    ESP_LOGI(TAG, "Device state changed to: %d", state);
}
