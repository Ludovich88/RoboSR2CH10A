/*
 * Device Configuration Header
 * 
 * Конфигурация GPIO пинов для устройства RoboSR2CH10A Zigbee Router
 * 
 * Устройство: ESP32-C6 Zigbee Router
 * Функции: 
 * - Zigbee Router (ретрансляция сигналов)
 * - Управление двумя реле
 * - Кнопка для пэйринга
 * - Индикаторы состояния
 */

 #ifndef DEVICE_CONFIG_H
 #define DEVICE_CONFIG_H
 
 #include "driver/gpio.h"
 
 /* GPIO Configuration for ESP32-C6 */
 
 /* Кнопка для пэйринга */
 #define PAIRING_BUTTON_GPIO          GPIO_NUM_0    // BOOT button (GPIO0)
 #define PAIRING_BUTTON_ACTIVE_LEVEL  0             // Active LOW (pressed = 0)
 
 /* Индикаторы состояния */
 #define STATUS_LED_GPIO              GPIO_NUM_1    // Status LED (GPIO1) - единственный индикатор
 
 /* Реле управления нагрузкой */
 #define RELAY_1_GPIO                 GPIO_NUM_19   // Relay 1 (GPIO19)
 #define RELAY_2_GPIO                 GPIO_NUM_18   // Relay 2 (GPIO18)
 
 /* Настройки кнопки */
 #define BUTTON_DEBOUNCE_TIME_MS      50            // Время подавления дребезга (мс)
 #define BUTTON_LONG_PRESS_TIME_MS    3000          // Время длительного нажатия (мс)
 
 /* Настройки индикаторов */
 #define LED_BLINK_FAST_MS           100            // Быстрое мигание (мс)
 #define LED_BLINK_SLOW_MS           500            // Медленное мигание (мс)
 #define LED_BLINK_VERY_SLOW_MS      1000           // Очень медленное мигание (мс)
 
 /* Zigbee Manufacturer Configuration */
 #define ZIGBEE_MANUFACTURER_CODE    0xA0FF         // Manufacturer code для manufacturer-specific атрибутов
 
 /* Состояния устройства */
 typedef enum {
     DEVICE_STATE_INIT = 0,           // Инициализация
     DEVICE_STATE_SEARCHING,          // Поиск сети
     DEVICE_STATE_CONNECTING,         // Подключение к сети
     DEVICE_STATE_CONNECTED,          // Подключено к сети
     DEVICE_STATE_PAIRING,            // Режим пэйринга
     DEVICE_STATE_ERROR,              // Ошибка
     DEVICE_STATE_MAX
 } device_state_t;
 
 /* Состояния реле */
 typedef enum {
     RELAY_OFF = 0,                   // Реле выключено
     RELAY_ON = 1                     // Реле включено
 } relay_state_t;
 
/* Состояния индикаторов (устарело - используется новая логика в main.c) */
 
/* Структура состояния устройства */
typedef struct {
    device_state_t state;            // Текущее состояние устройства
    relay_state_t relay1_state;      // Состояние реле 1
    relay_state_t relay2_state;      // Состояние реле 2
    bool pairing_mode;               // Режим пэйринга
    bool factory_reset;              // Флаг полной очистки памяти
    bool button_pressed;             // Кнопка нажата
    uint32_t button_press_time;      // Время нажатия кнопки
} device_status_t;
 
/* Функции для работы с GPIO */
void device_gpio_init(void);
void device_set_relay(uint8_t relay_num, relay_state_t state);
void device_handle_button(void);
device_status_t* device_get_status(void);
void device_set_state(device_state_t state);
 
 /* Функции для работы с Zigbee */
 void update_relay_zigbee_attr(uint8_t endpoint, relay_state_t state);
 void clear_zigbee_data(void);
 
 #endif // DEVICE_CONFIG_H