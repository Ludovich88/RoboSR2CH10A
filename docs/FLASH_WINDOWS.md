# Инструкции по прошивке для Windows

## Требования
- Windows 10/11
- Python 3.8+ установлен
- esptool.exe (устанавливается с ESP-IDF)
- Драйверы USB для ESP32-C6

## Установка esptool.exe

### Через ESP-IDF (рекомендуется)
```bash
# Активируйте ESP-IDF
C:\esp_idf\esp-idf\export.ps1

# esptool.exe будет доступен в PATH
```

### Через pip
```bash
pip install esptool
```

## Определение COM порта

### Через Диспетчер устройств
1. Откройте Диспетчер устройств
2. Найдите "Порты (COM и LPT)"
3. Найдите устройство ESP32-C6 (обычно COM3, COM4, etc.)

### Через командную строку
```cmd
mode
```

## Прошивка устройства

### 1. Подключение
- Подключите ESP32-C6 к компьютеру через USB
- Убедитесь, что устройство определилось в Диспетчере устройств

### 2. Прошивка через esptool.exe
```cmd
# Замените COM3 на ваш порт
esptool.exe --chip esp32c6 --port COM3 --baud 460800 write_flash 0x0 bootloader.bin 0x8000 partition-table.bin 0x10000 RoboSR2CH10A.bin
```

### 3. Проверка прошивки
```cmd
# Чтение flash памяти для проверки
esptool.exe --chip esp32c6 --port COM3 read_flash 0x0 0x100000 flash_read.bin
```

## Устранение неполадок

### Ошибка "Failed to connect"
- Проверьте подключение USB
- Попробуйте другой USB порт
- Убедитесь, что драйверы установлены
- Попробуйте понизить скорость: `--baud 115200`

### Ошибка "Permission denied"
- Закройте все программы, использующие COM порт
- Попробуйте запустить командную строку от имени администратора

### Устройство не определяется
- Установите драйверы CP210x или CH340
- Проверьте кабель USB (должен поддерживать передачу данных)
- Попробуйте другой кабель

### Ошибка "Chip not found"
- Удерживайте кнопку BOOT при подключении
- Нажмите кнопку RESET
- Попробуйте команду с флагом `--before default_reset --after hard_reset`

## Дополнительные команды

### Стирание flash памяти
```cmd
esptool.exe --chip esp32c6 --port COM3 erase_flash
```

### Информация о чипе
```cmd
esptool.exe --chip esp32c6 --port COM3 chip_id
```

### Список портов
```cmd
esptool.exe --port list
```

## Автоматическая прошивка

Создайте batch файл `flash.bat`:
```batch
@echo off
echo Прошивка RoboSR2CH10A...
esptool.exe --chip esp32c6 --port COM3 --baud 460800 write_flash 0x0 bootloader.bin 0x8000 partition-table.bin 0x10000 RoboSR2CH10A.bin
if %errorlevel% equ 0 (
    echo Прошивка завершена успешно!
) else (
    echo Ошибка прошивки!
)
pause
```

Запустите: `flash.bat`
