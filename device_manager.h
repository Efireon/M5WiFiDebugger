#ifndef DEVICE_MANAGER_H
#define DEVICE_MANAGER_H

#include <Arduino.h>
#include <M5StickCPlus2.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <vector>
#include <LittleFS.h>

// Структура для хранения данных датчиков
struct SensorData {
    float batteryVoltage;
    float batteryPercentage;
    float temperature;
    float humidity;
    float pressure;
    float acceleration[3]; // x, y, z
    float gyro[3]; // x, y, z
    unsigned long timestamp;
};

// Константы для управления питанием
#define LOW_BATTERY_THRESHOLD 3.3  // Порог низкого заряда батареи (в вольтах)
#define VERY_LOW_BATTERY_THRESHOLD 3.2 // Критически низкий заряд
#define AUTO_SHUTDOWN_TIMEOUT_MS 300000 // Автоматическое выключение через 5 минут при низком заряде

// Класс для управления устройством и датчиками
class DeviceManager {
private:
    AsyncWebServer* server;
    SensorData currentData;
    bool autoShutdown;
    unsigned long lastActivityTime;
    unsigned long lowBatteryDetectedTime;
    bool lowBatteryWarningShown;
    
    // Сбор данных датчиков
    void updateSensorData();
    
    // Расчет заряда батареи в процентах
    float calculateBatteryPercentage(float voltage);
    
public:
    DeviceManager(AsyncWebServer* server);
    
    // Инициализация
    void begin();
    
    // Основные функции устройства
    void update();  // Вызывается периодически для обновления состояния
    void resetActivity(); // Сбрасывает таймер активности
    
    // Управление питанием
    void powerOff();
    void deepSleep(uint64_t time_us);
    void restart();
    
    // Звуковые сигналы
    void playBeep(int frequency, int duration);
    void playAlert();
    void playFindMe();
    
    // Получение данных датчиков
    const SensorData& getSensorData() const;
    
    // Настройки автоматического выключения
    void setAutoShutdown(bool enabled);
    bool getAutoShutdown() const;
    
    // Настройка API
    void setupAPI();
    
    // Обработка событий
    void handleBatteryStatus();
};

// Реализация класса DeviceManager

DeviceManager::DeviceManager(AsyncWebServer* server) : 
    server(server),
    autoShutdown(true),
    lastActivityTime(0),
    lowBatteryDetectedTime(0),
    lowBatteryWarningShown(false) {
    
    // Инициализация структуры данных датчиков
    memset(&currentData, 0, sizeof(SensorData));
    currentData.timestamp = 0;
}

void DeviceManager::begin() {
    // Сбрасываем таймер активности
    resetActivity();
    
    // Первоначальное обновление данных датчиков
    updateSensorData();
    
    // Настройка API
    setupAPI();
}

void DeviceManager::update() {
    // Обновляем данные датчиков каждые 5 секунд
    static unsigned long lastSensorUpdate = 0;
    if (millis() - lastSensorUpdate > 5000) {
        updateSensorData();
        lastSensorUpdate = millis();
    }
    
    // Проверяем состояние батареи
    handleBatteryStatus();
    
    // Проверяем необходимость автоматического отключения при неактивности
    // Для этой функции нужен дополнительный код для отслеживания активности пользователя
}

void DeviceManager::resetActivity() {
    lastActivityTime = millis();
}

void DeviceManager::handleBatteryStatus() {
    float batteryVoltage = currentData.batteryVoltage;
    
    // Проверка на критически низкий заряд батареи
    if (batteryVoltage < VERY_LOW_BATTERY_THRESHOLD) {
        if (lowBatteryDetectedTime == 0) {
            // Первое обнаружение низкого заряда
            lowBatteryDetectedTime = millis();
            lowBatteryWarningShown = false;
        } else if (!lowBatteryWarningShown) {
            // Показываем предупреждение
            M5.Lcd.fillScreen(RED);
            M5.Lcd.setCursor(0, 0);
            M5.Lcd.setTextColor(WHITE);
            M5.Lcd.setTextSize(2);
            M5.Lcd.println("LOW BATTERY");
            M5.Lcd.println("CRITICAL!");
            M5.Lcd.setTextSize(1);
            M5.Lcd.println("\nAutomatic shutdown");
            M5.Lcd.printf("in %d seconds", (AUTO_SHUTDOWN_TIMEOUT_MS - (millis() - lowBatteryDetectedTime)) / 1000);
            
            // Звуковой сигнал
            playAlert();
            
            lowBatteryWarningShown = true;
        } else if (millis() - lowBatteryDetectedTime > AUTO_SHUTDOWN_TIMEOUT_MS) {
            // Автоматическое выключение при длительном низком заряде
            powerOff();
        }
    } else if (batteryVoltage < LOW_BATTERY_THRESHOLD) {
        // Просто низкий заряд, показываем предупреждение периодически
        static unsigned long lastLowBatteryWarning = 0;
        if (millis() - lastLowBatteryWarning > 300000) { // Каждые 5 минут
            M5.Lcd.fillScreen(YELLOW);
            M5.Lcd.setCursor(0, 0);
            M5.Lcd.setTextColor(BLACK);
            M5.Lcd.setTextSize(2);
            M5.Lcd.println("LOW BATTERY");
            M5.Lcd.setTextSize(1);
            M5.Lcd.println("\nPlease charge soon");
            M5.Lcd.printf("Battery: %.2fV (%.0f%%)", 
                         batteryVoltage, 
                         currentData.batteryPercentage);
            
            // Звуковой сигнал
            playBeep(1000, 100);
            
            delay(3000); // Показываем предупреждение на 3 секунды
            
            lastLowBatteryWarning = millis();
        }
    } else {
        // Нормальный заряд батареи, сбрасываем флаги
        lowBatteryDetectedTime = 0;
        lowBatteryWarningShown = false;
    }
}

void DeviceManager::updateSensorData() {
    currentData.timestamp = millis();
    
    // Получаем данные о батарее от M5Unified
    // Используем правильный метод для M5StickCPlus2
    currentData.batteryVoltage = M5.Power.getBatteryVoltage() / 1000.0; // Преобразуем мВ в В
    currentData.batteryPercentage = calculateBatteryPercentage(currentData.batteryVoltage);
    
    // Получаем данные от IMU (акселерометр и гироскоп), если доступны
    float accX = 0, accY = 0, accZ = 0;
    float gyroX = 0, gyroY = 0, gyroZ = 0;
    float temp = 0;
    
    // Обновляем данные IMU и температуры если IMU доступен
    if (M5.Imu.isEnabled()) {
        M5.Imu.getAccelData(&accX, &accY, &accZ);
        M5.Imu.getGyroData(&gyroX, &gyroY, &gyroZ);
        if (M5.Imu.isEnabled()) {
            float gyroX, gyroY, gyroZ;
            M5.Imu.getImuData(&gyroX, &gyroY, &gyroZ, &temp);
        }
        
        currentData.acceleration[0] = accX;
        currentData.acceleration[1] = accY;
        currentData.acceleration[2] = accZ;
        
        currentData.gyro[0] = gyroX;
        currentData.gyro[1] = gyroY;
        currentData.gyro[2] = gyroZ;
        
        currentData.temperature = temp;
    }
    
    // Данные других датчиков (если они были бы подключены)
    currentData.humidity = 0;  // Заглушка
    currentData.pressure = 0;  // Заглушка
}

float DeviceManager::calculateBatteryPercentage(float voltage) {
    // Простая линейная аппроксимация для M5StickC Plus2
    // 4.15V = 100%, 3.2V = 0%
    float percentage = (voltage - 3.2) / (4.15 - 3.2) * 100.0;
    
    // Ограничиваем значение в диапазоне 0-100
    if (percentage > 100.0) percentage = 100.0;
    if (percentage < 0.0) percentage = 0.0;
    
    return percentage;
}

void DeviceManager::powerOff() {
    M5.Lcd.fillScreen(BLACK);
    M5.Lcd.setCursor(0, 0);
    M5.Lcd.println("Shutting down...");
    delay(1000);
    M5.Power.powerOff();
}

void DeviceManager::deepSleep(uint64_t time_us) {
    M5.Lcd.fillScreen(BLACK);
    M5.Lcd.setCursor(0, 0);
    M5.Lcd.println("Entering deep sleep...");
    delay(1000);
    
    M5.Power.deepSleep(time_us / 1000); // M5 принимает мс, а не мкс
}

void DeviceManager::restart() {
    M5.Lcd.fillScreen(BLACK);
    M5.Lcd.setCursor(0, 0);
    M5.Lcd.println("Restarting...");
    delay(1000);
    ESP.restart();
}

void DeviceManager::playBeep(int frequency, int duration) {
    M5.Speaker.tone(frequency, duration);
    delay(duration);
    M5.Speaker.stop();
}

void DeviceManager::playAlert() {
    for (int i = 0; i < 3; i++) {
        M5.Speaker.tone(2000, 100);
        delay(150);
        M5.Speaker.tone(1500, 100);
        delay(150);
    }
    M5.Speaker.stop();
}

void DeviceManager::playFindMe() {
    for (int i = 0; i < 5; i++) {
        M5.Speaker.tone(2000, 200);
        delay(300);
        M5.Speaker.tone(1500, 200);
        delay(300);
    }
    M5.Speaker.stop();
}

const SensorData& DeviceManager::getSensorData() const {
    return currentData;
}

void DeviceManager::setAutoShutdown(bool enabled) {
    autoShutdown = enabled;
}

bool DeviceManager::getAutoShutdown() const {
    return autoShutdown;
}

void DeviceManager::setupAPI() {
    // API для получения данных датчиков
    server->on("/api/device/status", HTTP_GET, [this](AsyncWebServerRequest *request) {
        DynamicJsonDocument doc(1024);
        
        doc["battery"] = currentData.batteryVoltage;
        doc["batteryPercentage"] = currentData.batteryPercentage;
        doc["temperature"] = currentData.temperature;
        
        JsonObject accel = doc.createNestedObject("acceleration");
        accel["x"] = currentData.acceleration[0];
        accel["y"] = currentData.acceleration[1];
        accel["z"] = currentData.acceleration[2];
        
        JsonObject gyro = doc.createNestedObject("gyro");
        gyro["x"] = currentData.gyro[0];
        gyro["y"] = currentData.gyro[1];
        gyro["z"] = currentData.gyro[2];
        
        doc["uptime"] = millis() / 1000; // Время работы в секундах
        
        // Информация о WiFi, если подключен
        if (WiFi.status() == WL_CONNECTED) {
            JsonObject wifi = doc.createNestedObject("wifi");
            wifi["ssid"] = WiFi.SSID();
            wifi["rssi"] = WiFi.RSSI();
            wifi["ip"] = WiFi.localIP().toString();
        }
        
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });
    
    // API для функции "найти меня"
    server->on("/api/device/findme", HTTP_POST, [this](AsyncWebServerRequest *request) {
        playFindMe();
        request->send(200, "application/json", "{\"success\":true}");
    });
    
    // API для перезагрузки устройства
    server->on("/api/device/restart", HTTP_POST, [this](AsyncWebServerRequest *request) {
        request->send(200, "application/json", "{\"success\":true,\"message\":\"Restarting device...\"}");
        
        // Добавляем небольшую задержку, чтобы ответ успел уйти
        delay(500);
        restart();
    });
    
    // API для перехода в режим глубокого сна
    server->on("/api/device/sleep", HTTP_POST, [this](AsyncWebServerRequest *request) {
        uint64_t sleepTime = 3600000000ULL; // По умолчанию 1 час
        
        if (request->hasParam("time", true)) {
            uint64_t timeSec = request->getParam("time", true)->value().toInt();
            sleepTime = timeSec * 1000000ULL; // Переводим в микросекунды
        }
        
        request->send(200, "application/json", "{\"success\":true,\"message\":\"Entering deep sleep...\"}");
        
        // Добавляем небольшую задержку, чтобы ответ успел уйти
        delay(500);
        deepSleep(sleepTime);
    });
    
    // API для выключения устройства
    server->on("/api/device/poweroff", HTTP_POST, [this](AsyncWebServerRequest *request) {
        request->send(200, "application/json", "{\"success\":true,\"message\":\"Powering off...\"}");
        
        // Добавляем небольшую задержку, чтобы ответ успел уйти
        delay(500);
        powerOff();
    });
    
    // API для настройки автоматического выключения
    server->on("/api/device/autoshutdown", HTTP_POST, [this](AsyncWebServerRequest *request) {
        if (request->hasParam("enabled", true)) {
            bool enabled = (request->getParam("enabled", true)->value() == "true" || 
                           request->getParam("enabled", true)->value() == "1");
            setAutoShutdown(enabled);
        }
        
        DynamicJsonDocument doc(256);
        doc["success"] = true;
        doc["autoShutdown"] = getAutoShutdown();
        
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });
}

#endif // DEVICE_MANAGER_H