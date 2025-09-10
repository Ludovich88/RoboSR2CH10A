# Инструкции по прошивке через ESP-IDF

## Требования
- ESP-IDF v5.3.2 или новее
- Python 3.8+
- Настроенная среда разработки

## Настройка ESP-IDF

### Windows
```powershell
# Перейдите в папку ESP-IDF
cd C:\esp_idf\esp-idf

# Активируйте среду
.\export.ps1

# Перейдите в проект
cd C:\esp_idf\Robo_zigbee_projects\devices\RoboSR2CH10A
```

### Linux
```bash
# Перейдите в папку ESP-IDF
cd ~/esp/esp-idf

# Активируйте среду
source export.sh

# Перейдите в проект
cd ~/Robo_zigbee_projects/devices/RoboSR2CH10A
```

## Сборка проекта

### Полная сборка
```bash
# Очистка и сборка
idf.py fullclean
idf.py build
```

### Быстрая сборка (если уже собирали)
```bash
idf.py build
```

### Сборка с подробным выводом
```bash
idf.py build -v
```

## Прошивка устройства

### 1. Подключение устройства
- Подключите ESP32-C6 к компьютеру через USB
- Убедитесь, что устройство определилось

### 2. Определение порта

#### Windows
```cmd
# Список COM портов
mode

# Или через ESP-IDF
idf.py -p list
```

#### Linux
```bash
# Список портов
ls /dev/ttyUSB* /dev/ttyACM*

# Или через ESP-IDF
idf.py -p list
```

### 3. Прошивка

#### Автоматическое определение порта
```bash
# ESP-IDF автоматически найдет порт
idf.py flash
```

#### Указание конкретного порта
```bash
# Windows
idf.py -p COM3 flash

# Linux
idf.py -p /dev/ttyUSB0 flash
```

#### Прошивка с указанием скорости
```bash
idf.py -p COM3 -b 460800 flash
```

## Мониторинг работы

### Запуск мониторинга
```bash
# Мониторинг после прошивки
idf.py -p COM3 flash monitor

# Только мониторинг (если уже прошито)
idf.py -p COM3 monitor
```

### Выход из мониторинга
- Нажмите `Ctrl+]` для выхода

### Мониторинг с фильтрацией
```bash
# Фильтр по тегу
idf.py monitor --print_filter="ROBO_SR2CH10A"

# Фильтр по уровню
idf.py monitor --print_filter="*:INFO"
```

## Дополнительные команды

### Очистка flash памяти
```bash
idf.py -p COM3 erase-flash
```

### Стирание конкретной области
```bash
idf.py -p COM3 erase-region 0x10000 0x100000
```

### Информация о проекте
```bash
# Информация о конфигурации
idf.py show-efuse-table

# Информация о разделе
idf.py partition-table
```

### Создание образа для OTA
```bash
# Создание OTA образа
idf.py build
idf.py gen-esp32c6-app-ota.bin
```

## Устранение неполадок

### Ошибка "Failed to connect"
```bash
# Попробуйте понизить скорость
idf.py -p COM3 -b 115200 flash

# Или попробуйте другой порт
idf.py -p list
idf.py -p COM4 flash
```

### Ошибка "Permission denied" (Linux)
```bash
# Добавьте права на порт
sudo chmod 666 /dev/ttyUSB0

# Или добавьте пользователя в группу dialout
sudo usermod -a -G dialout $USER
```

### Устройство не определяется
```bash
# Проверьте подключение
idf.py -p list

# Попробуйте переподключить USB
# Удерживайте кнопку BOOT при подключении
```

### Ошибка сборки
```bash
# Очистите проект
idf.py fullclean

# Пересоберите
idf.py build

# Проверьте конфигурацию
idf.py menuconfig
```

## Продвинутые команды

### Сборка с отладочной информацией
```bash
idf.py build --debug
```

### Сборка с оптимизацией размера
```bash
idf.py build --size-opt
```

### Создание дампа памяти
```bash
idf.py coredump-info
idf.py coredump-debug
```

### Анализ размера кода
```bash
idf.py size
idf.py size-components
idf.py size-files
```

## Автоматизация

### Создание скрипта сборки и прошивки

#### Windows (build_and_flash.bat)
```batch
@echo off
echo Сборка проекта...
idf.py build
if %errorlevel% neq 0 (
    echo Ошибка сборки!
    pause
    exit /b 1
)

echo Прошивка устройства...
idf.py -p COM3 flash
if %errorlevel% neq 0 (
    echo Ошибка прошивки!
    pause
    exit /b 1
)

echo Готово!
pause
```

#### Linux (build_and_flash.sh)
```bash
#!/bin/bash

echo "Сборка проекта..."
idf.py build
if [ $? -ne 0 ]; then
    echo "Ошибка сборки!"
    exit 1
fi

echo "Прошивка устройства..."
idf.py -p /dev/ttyUSB0 flash
if [ $? -ne 0 ]; then
    echo "Ошибка прошивки!"
    exit 1
fi

echo "Готово!"
```

## Конфигурация проекта

### Открытие меню конфигурации
```bash
idf.py menuconfig
```

### Основные настройки для RoboSR2CH10A
- **Serial flasher config** → **Default baud rate for flashing**: 460800
- **Component config** → **Log output** → **Default log verbosity**: Info
- **Component config** → **FreeRTOS** → **configTICK_RATE_HZ**: 1000

### Сохранение конфигурации
```bash
# Конфигурация сохраняется в sdkconfig
idf.py save-defconfig
```
