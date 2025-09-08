# Настройка внешнего конвертера Zigbee2MQTT для RoboSR2CH10A

## Описание

Внешний конвертер `RoboSR2CH10A.js` предназначен для корректного отображения устройства RoboSR2CH10A в Zigbee2MQTT. Конвертер поддерживает:

- 2 реле (Endpoints 1 и 2)
- Manufacturer Code: 0xA0FF
- Поддержка модели "RoboSR2CH10A"

## Установка

### 1. Копирование файла конвертера

Скопируйте файл `RoboSR2CH10A.js` в папку внешних конвертеров Zigbee2MQTT:

```bash
# Для Docker
docker cp RoboSR2CH10A.js <container_name>:/app/data/external_converters/

# Для локальной установки
cp RoboSR2CH10A.js /path/to/zigbee2mqtt/data/external_converters/
```

### 2. Настройка конфигурации Zigbee2MQTT

Отредактируйте файл `configuration.yaml`:

```yaml
external_converters:
  - 'RoboSR2CH10A.js'
```

### 3. Перезапуск Zigbee2MQTT

```bash
# Для Docker
docker restart <container_name>

# Для локальной установки
pm2 restart zigbee2mqtt
# или
systemctl restart zigbee2mqtt
```

## Функциональность

### Реле управления

- **l1** (Endpoint 1): Управление реле 1
- **l2** (Endpoint 2): Управление реле 2

Оба реле поддерживают:
- Включение/выключение
- Состояние (state)
- Яркость (brightness) - для совместимости с On/Off Light

## Использование в Home Assistant

После настройки конвертера устройство будет отображаться в Home Assistant как:

```yaml
# Реле
switch:
  - platform: mqtt
    name: "SR2CH10A Relay 1"
    state_topic: "zigbee2mqtt/SR2CH10A/l1/state"
    command_topic: "zigbee2mqtt/SR2CH10A/l1/set"
    payload_on: "ON"
    payload_off: "OFF"
    state_on: "ON"
    state_off: "OFF"

  - platform: mqtt
    name: "SR2CH10A Relay 2"
    state_topic: "zigbee2mqtt/SR2CH10A/l2/state"
    command_topic: "zigbee2mqtt/SR2CH10A/l2/set"
    payload_on: "ON"
    payload_off: "OFF"
    state_on: "ON"
    state_off: "OFF"

```

## Troubleshooting

### Устройство не распознается

1. Проверьте, что файл конвертера скопирован в правильную папку
2. Убедитесь, что `external_converters` настроен в `configuration.yaml`
3. Перезапустите Zigbee2MQTT
4. Проверьте логи Zigbee2MQTT на наличие ошибок


### Реле не управляются

1. Проверьте, что endpoints 1 и 2 настроены с On/Off кластером
2. Убедитесь, что binding настроен правильно
3. Проверьте логи Zigbee2MQTT

## Логи

Для отладки включите подробные логи в Zigbee2MQTT:

```yaml
log_level: debug
```

Полезные логи:
- `Configuring SR2CH10A device...` - начало настройки
- `Relay X (Endpoint Y) configured` - успешная настройка реле
- `SR2CH10A device configured successfully` - полная настройка завершена

## Поддержка

При возникновении проблем:

1. Проверьте логи Zigbee2MQTT
2. Проверьте логи устройства (через монитор ESP-IDF)
3. Убедитесь, что firmware соответствует конвертеру
4. Проверьте совместимость версий Zigbee2MQTT и zigbee-herdsman-converters