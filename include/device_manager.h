#ifndef DEVICE_MANAGER_H
#define DEVICE_MANAGER_H

#include <M5StickCPlus2.h>
#include <vector>
#include <WiFi.h>
#include "common_structures.h"

// Структура для хранения данных датчиков
struct SensorData {
  float batteryVoltage;
  float batteryPercentage;
  float temperature;
  float gyroX;
  float gyroY;
  float gyroZ;
  unsigned long timestamp;
};

// Структура для хранения информации о сети
struct NetworkInfo {
  bool connected;
  String ssid;
  int32_t rssi;
  String localIP;
  String gateway;
  String subnet;
  String dns;
  String mac;
};

// Класс для управления устройством и его датчиками
class DeviceManager {
private:
  // Датчики и данные
  SensorData sensorData;
  NetworkInfo networkInfo;
  
  // Бипер для сигнала Find Me
  bool findMeActive;
  unsigned long findMeStartTime;
  unsigned long findMeDuration;
  
  // Частота обновления данных сенсоров (мс)
  unsigned long sensorUpdateInterval;
  unsigned long lastSensorUpdate;
  
  // Данные мониторинга
  std::vector<SensorData> sensorHistory;
  unsigned long historyMaxDuration; // Максимальная длительность истории в мс
  
  // Обработчик сигнализации Find Me
  void handleFindMeSignal() {
    if (findMeActive) {
      unsigned long currentMillis = millis();
      unsigned long elapsedTime = currentMillis - findMeStartTime;
      
      // Проверяем, не истекло ли время активности сигнала
      if (elapsedTime > findMeDuration) {
        findMeActive = false;
        M5.Speaker.stop(); // Выключаем звук
      } else {
        // Чередуем сигналы с интервалом 300 мс
        unsigned long beepCycle = (elapsedTime / 300) % 4;
        
        if (beepCycle == 0) {
          playTone(2000, 200);
        } else if (beepCycle == 2) {
          playTone(1500, 200);
        } else {
          M5.Speaker.stop(); // Пауза между сигналами
        }
      }
    }
  }
  
  // Воспроизведение тона с учетом настроек громкости
  void playTone(int frequency, int duration) {
    // Получаем настройки громкости
    uint8_t targetVolume = map(globalDeviceSettings.volume, 0, 100, 0, 255);
    M5.Speaker.setVolume(targetVolume);
    
    // Воспроизводим звук
    M5.Speaker.tone(frequency, duration);
  }

public:
  // Конструктор
  DeviceManager() 
    : findMeActive(false), 
      findMeStartTime(0), 
      findMeDuration(5000), // 5 секунд по умолчанию
      sensorUpdateInterval(1000), // 1 секунда по умолчанию
      lastSensorUpdate(0),
      historyMaxDuration(3600000) // 1 час по умолчанию
  {
    // Инициализация данных сенсоров
    sensorData.batteryVoltage = 0.0f;
    sensorData.batteryPercentage = 0.0f;
    sensorData.temperature = 0.0f;
    sensorData.gyroX = 0.0f;
    sensorData.gyroY = 0.0f;
    sensorData.gyroZ = 0.0f;
    sensorData.timestamp = 0;
    
    // Инициализация информации о сети
    networkInfo.connected = false;
    networkInfo.ssid = "";
    networkInfo.rssi = 0;
    networkInfo.localIP = "";
    networkInfo.gateway = "";
    networkInfo.subnet = "";
    networkInfo.dns = "";
    networkInfo.mac = WiFi.macAddress();
  }
  
  // Инициализация устройства
  void begin() {
    // Обновляем данные сенсоров при инициализации
    updateSensorData();
    updateNetworkInfo();
  }
  
  // Обновление (вызывается в основном цикле)
  void update() {
    // Проверяем, пора ли обновлять данные сенсоров
    unsigned long currentMillis = millis();
    if (currentMillis - lastSensorUpdate > sensorUpdateInterval) {
      updateSensorData();
      updateNetworkInfo();
      lastSensorUpdate = currentMillis;
      
      // Добавляем текущие данные в историю
      sensorHistory.push_back(sensorData);
      
      // Удаляем устаревшие данные из истории
      while (!sensorHistory.empty() && 
            (currentMillis - sensorHistory.front().timestamp > historyMaxDuration)) {
        sensorHistory.erase(sensorHistory.begin());
      }
    }
    
    // Обработка сигнала Find Me
    handleFindMeSignal();
  }
  
  // Обновление данных сенсоров
  void updateSensorData() {
    // Обновляем данные батареи
    sensorData.batteryVoltage = M5.Power.getBatteryVoltage() / 1000.0f; // В вольтах
    sensorData.batteryPercentage = M5.Power.getBatteryLevel();
    
    // Обновляем данные IMU (гироскоп, акселерометр)
    if (M5.Imu.isEnabled()) {
        // Получаем данные IMU
        auto imuData = M5.Imu.getImuData();
        
        // Используем поля gyro из структуры imu_data_t
        sensorData.gyroX = imuData.gyro.x;
        sensorData.gyroY = imuData.gyro.y;
        sensorData.gyroZ = imuData.gyro.z;
    }
    
    // Устанавливаем временную метку
    sensorData.timestamp = millis();
  }
  
  // Обновление информации о сети
  void updateNetworkInfo() {
    networkInfo.connected = WiFi.status() == WL_CONNECTED;
    
    if (networkInfo.connected) {
      networkInfo.ssid = WiFi.SSID();
      networkInfo.rssi = WiFi.RSSI();
      networkInfo.localIP = WiFi.localIP().toString();
      networkInfo.gateway = WiFi.gatewayIP().toString();
      networkInfo.subnet = WiFi.subnetMask().toString();
      networkInfo.dns = WiFi.dnsIP().toString();
    } else {
      networkInfo.ssid = "";
      networkInfo.rssi = 0;
      networkInfo.localIP = "0.0.0.0";
      networkInfo.gateway = "0.0.0.0";
      networkInfo.subnet = "0.0.0.0";
      networkInfo.dns = "0.0.0.0";
    }
  }
  
  // Активация сигнала Find Me
  void activateFindMe(unsigned long duration = 5000) {
    findMeActive = true;
    findMeStartTime = millis();
    findMeDuration = duration;
    
    // Воспроизводим первый сигнал сразу
    playTone(2000, 200);
  }
  
  // Деактивация сигнала Find Me
  void deactivateFindMe() {
    findMeActive = false;
    M5.Speaker.stop(); // Выключаем звук
  }
  
  // Получение текущих данных сенсоров
  const SensorData& getSensorData() const {
    return sensorData;
  }
  
  // Получение информации о сети
  const NetworkInfo& getNetworkInfo() const {
    return networkInfo;
  }
  
  // Получение истории данных сенсоров
  const std::vector<SensorData>& getSensorHistory() const {
    return sensorHistory;
  }
  
  // Установка интервала обновления сенсоров
  void setSensorUpdateInterval(unsigned long interval) {
    sensorUpdateInterval = interval;
  }
  
  // Получение интервала обновления сенсоров
  unsigned long getSensorUpdateInterval() const {
    return sensorUpdateInterval;
  }
  
  // Установка максимальной длительности истории
  void setHistoryMaxDuration(unsigned long duration) {
    historyMaxDuration = duration;
  }
  
  // Перезагрузка устройства
  void restart() {
    ESP.restart();
  }
  
  // Выключение устройства
  void powerOff() {
    M5.Power.powerOff();
  }
  
  // Проверка, активен ли сигнал Find Me
  bool isFindMeActive() const {
    return findMeActive;
  }
};

#endif // DEVICE_MANAGER_H