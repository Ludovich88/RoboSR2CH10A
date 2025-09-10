# Инструкции по прошивке для Linux

## Требования
- Linux (Ubuntu, Debian, CentOS, etc.)
- Python 3.8+ установлен
- esptool.py
- Права доступа к USB портам

## Установка esptool.py

### Через pip (рекомендуется)
```bash
pip3 install esptool
```

### Через пакетный менеджер
```bash
# Ubuntu/Debian
sudo apt install esptool

# CentOS/RHEL
sudo yum install esptool
```

### Через ESP-IDF
```bash
# Активируйте ESP-IDF
source ~/esp/esp-idf/export.sh

# esptool.py будет доступен в PATH
```

## Настройка прав доступа

### Добавление пользователя в группу dialout
```bash
sudo usermod -a -G dialout $USER
```

### Перезагрузка или перелогин
```bash
# Перезагрузите систему или выполните
newgrp dialout
```

## Определение USB порта

### Поиск устройства
```bash
# Список USB устройств
lsusb

# Поиск ESP32-C6
lsusb | grep -i "silicon\|cp210\|ch340"

# Список tty устройств
ls /dev/ttyUSB* /dev/ttyACM*
```

### Обычные порты
- `/dev/ttyUSB0` - CP210x чипы
- `/dev/ttyACM0` - CH340 чипы

## Прошивка устройства

### 1. Подключение
```bash
# Подключите ESP32-C6 к USB
# Проверьте, что устройство определилось
ls /dev/ttyUSB* /dev/ttyACM*
```

### 2. Прошивка через esptool.py
```bash
# Замените /dev/ttyUSB0 на ваш порт
esptool.py --chip esp32c6 --port /dev/ttyUSB0 --baud 460800 write_flash 0x0 bootloader.bin 0x8000 partition-table.bin 0x10000 RoboSR2CH10A.bin
```

### 3. Проверка прошивки
```bash
# Чтение flash памяти для проверки
esptool.py --chip esp32c6 --port /dev/ttyUSB0 read_flash 0x0 0x100000 flash_read.bin
```

## Устранение неполадок

### Ошибка "Permission denied"
```bash
# Добавьте права на устройство
sudo chmod 666 /dev/ttyUSB0

# Или добавьте пользователя в группу dialout
sudo usermod -a -G dialout $USER
```

### Ошибка "Device not found"
```bash
# Проверьте подключение
lsusb | grep -i "silicon\|cp210\|ch340"

# Проверьте драйверы
lsmod | grep usbserial
```

### Ошибка "Failed to connect"
```bash
# Попробуйте понизить скорость
esptool.py --chip esp32c6 --port /dev/ttyUSB0 --baud 115200 write_flash ...

# Или попробуйте другой порт
ls /dev/ttyUSB* /dev/ttyACM*
```

### Устройство не определяется
```bash
# Установите драйверы
sudo apt install linux-modules-extra-$(uname -r)

# Или для старых систем
sudo modprobe usbserial
sudo modprobe cp210x
```

## Дополнительные команды

### Стирание flash памяти
```bash
esptool.py --chip esp32c6 --port /dev/ttyUSB0 erase_flash
```

### Информация о чипе
```bash
esptool.py --chip esp32c6 --port /dev/ttyUSB0 chip_id
```

### Список портов
```bash
esptool.py --port list
```

## Автоматическая прошивка

Создайте скрипт `flash.sh`:
```bash
#!/bin/bash

echo "Прошивка RoboSR2CH10A..."

# Определяем порт автоматически
PORT=$(ls /dev/ttyUSB* /dev/ttyACM* 2>/dev/null | head -1)

if [ -z "$PORT" ]; then
    echo "Ошибка: Устройство не найдено!"
    exit 1
fi

echo "Используется порт: $PORT"

# Прошивка
esptool.py --chip esp32c6 --port $PORT --baud 460800 write_flash 0x0 bootloader.bin 0x8000 partition-table.bin 0x10000 RoboSR2CH10A.bin

if [ $? -eq 0 ]; then
    echo "Прошивка завершена успешно!"
else
    echo "Ошибка прошивки!"
    exit 1
fi
```

Сделайте скрипт исполняемым и запустите:
```bash
chmod +x flash.sh
./flash.sh
```

## Мониторинг работы устройства

### Через minicom
```bash
# Установка
sudo apt install minicom

# Запуск
minicom -D /dev/ttyUSB0 -b 115200
```

### Через screen
```bash
# Установка
sudo apt install screen

# Запуск
screen /dev/ttyUSB0 115200
```

### Выход из мониторинга
- minicom: `Ctrl+A`, затем `X`
- screen: `Ctrl+A`, затем `K`
