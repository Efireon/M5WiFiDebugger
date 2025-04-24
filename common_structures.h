#ifndef COMMON_STRUCTURES_H
#define COMMON_STRUCTURES_H

#include <Arduino.h>

// Определение режимов точки доступа
enum APMode {
  AP_MODE_OFF,      // Выключена
  AP_MODE_NORMAL,   // Обычный режим
  AP_MODE_REPEATER, // Режим ретранслятора
  AP_MODE_HIDDEN,   // Скрытый режим
  AP_MODE_HONEYPOT  // Режим ловушки
};

// Режимы мониторинга пинов
enum PinMonitorMode {
  PIN_MONITOR_OFF,  // Выключен
  PIN_MONITOR_ON,   // Включен
  PIN_MONITOR_BUZZ  // Со звуком
};

// Интервалы проверки соединения
enum ConnectionCheckInterval {
  CHECK_OFF,    // Выключено
  CHECK_10SEC,  // 10 секунд
  CHECK_30SEC,  // 30 секунд
  CHECK_1MIN,   // 1 минута
  CHECK_5MIN,   // 5 минут
  CHECK_30MIN   // 30 минут
};

// Структура настроек устройства
struct DeviceSettings {
  uint8_t brightness;       // Яркость экрана (0-100%)
  uint16_t sleepTimeout;    // Время до перехода в спящий режим (в секундах, 0 - отключено)
  String deviceId;          // Идентификатор устройства
  bool rotateDisplay;       // Поворот экрана
  uint8_t volume;           // Громкость (0-100%)
  bool invertPins;          // Инвертировать логику пинов KVM
};

// Расширенная конфигурация для AP режима
struct APConfig {
  APMode mode;
  String ssid;
  String password;
  bool hidden;
  int channel;
};

// Структура для пинов
struct EnhancedPinConfig {
  int pin;
  String name;
  bool state;
  PinMonitorMode monitorMode;
  unsigned long lastStateChange;
};

// Структура для хранения информации о сохраненной сети
struct SavedNetwork {
  String ssid;
  String password;
};

// Структура для хранения результатов сканирования WiFi
struct WiFiResult {
  String ssid;
  int32_t rssi;
  uint8_t encryptionType;
  int32_t channel;
};

// Глобальные переменные
extern DeviceSettings globalDeviceSettings;

#endif // COMMON_STRUCTURES_H