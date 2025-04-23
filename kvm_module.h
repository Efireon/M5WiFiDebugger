#ifndef KVM_MODULE_H
#define KVM_MODULE_H

#include <Arduino.h>
#include <M5StickCPlus2.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <vector>
#include <LittleFS.h>

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

// Расширенная структура для пинов
struct EnhancedPinConfig {
  int pin;
  String name;
  bool state;
  PinMonitorMode monitorMode;
  unsigned long lastStateChange;
};

// Класс для управления KVM
class KVMModule {
private:
  std::vector<EnhancedPinConfig> pins;
  AsyncWebServer* server;
  ConnectionCheckInterval checkInterval;
  bool useDHCP;
  unsigned long lastConnectionCheck;
  
  // Технические параметры для звуковых сигналов
  int highToneFrequency;  // Частота высокого тона (Гц)
  int lowToneFrequency;   // Частота низкого тона (Гц)
  int toneDuration;       // Длительность звукового сигнала (мс)

public:
  KVMModule(AsyncWebServer* server);
  
  // Инициализация
  void begin();
  
  // Основные функции управления пинами
  bool addPin(int pin, const String& name);
  bool removePin(int index);
  bool togglePin(int index);
  bool setPin(int index, bool state);
  bool setPinMonitorMode(int index, PinMonitorMode mode);
  
  // Получение информации о пинах
  const std::vector<EnhancedPinConfig>& getPins() const;
  const EnhancedPinConfig* getPin(int index) const;
  
  // Настройки интервала проверки соединения
  void setConnectionCheckInterval(ConnectionCheckInterval interval);
  ConnectionCheckInterval getConnectionCheckInterval() const;
  
  // Настройки DHCP
  void setUseDHCP(bool use);
  bool getUseDHCP() const;
  
  // Мониторинг пинов
  void updatePinMonitoring();
  void performConnectionCheck();
  
  // Сохранение/загрузка настроек
  void saveConfig(fs::FS &fs);
  void loadConfig(fs::FS &fs);
  
  // Настройка API
  void setupAPI();

  // Проверка наличия пина с заданным номером
  bool hasPin(int gpioPin) const;
};

// Реализация класса KVMModule

KVMModule::KVMModule(AsyncWebServer* server) : 
  server(server), 
  checkInterval(CHECK_OFF), 
  useDHCP(true), 
  lastConnectionCheck(0),
  highToneFrequency(6000),
  lowToneFrequency(2000),
  toneDuration(200) {
}

void KVMModule::begin() {
  // Загружаем конфигурацию
  loadConfig(LittleFS);
  
  // Настраиваем пины
  for (auto& pin : pins) {
    pinMode(pin.pin, OUTPUT);
    digitalWrite(pin.pin, pin.state ? HIGH : LOW);
  }
  
  // Настраиваем API
  setupAPI();
}

bool KVMModule::addPin(int pin, const String& name) {
  // Проверяем, существует ли уже пин с таким номером
  for (const auto& p : pins) {
    if (p.pin == pin) {
      return false;
    }
  }
  
  // Проверяем валидность номера пина и избегаем системных пинов
  if (pin < 0 || pin >= 40) {
    return false;
  }
  
  // Добавляем новый пин
  EnhancedPinConfig newPin = {
    pin,
    name,
    false,
    PIN_MONITOR_OFF,
    0
  };
  
  pins.push_back(newPin);
  
  // Настраиваем пин как выход
  pinMode(pin, OUTPUT);
  digitalWrite(pin, LOW);
  
  // Сохраняем конфигурацию
  saveConfig(LittleFS);
  
  return true;
}

bool KVMModule::removePin(int index) {
  if (index < 0 || index >= pins.size()) {
    return false;
  }
  
  pins.erase(pins.begin() + index);
  saveConfig(LittleFS);
  return true;
}

bool KVMModule::togglePin(int index) {
  if (index < 0 || index >= pins.size()) {
    return false;
  }
  
  // Инвертируем состояние
  pins[index].state = !pins[index].state;
  
  // Устанавливаем физический пин
  digitalWrite(pins[index].pin, pins[index].state ? HIGH : LOW);
  
  // Сохраняем конфигурацию
  saveConfig(LittleFS);
  
  return true;
}

bool KVMModule::setPin(int index, bool state) {
  if (index < 0 || index >= pins.size()) {
    return false;
  }
  
  // Устанавливаем новое состояние
  pins[index].state = state;
  
  // Устанавливаем физический пин
  digitalWrite(pins[index].pin, state ? HIGH : LOW);
  
  // Сохраняем конфигурацию
  saveConfig(LittleFS);
  
  return true;
}

bool KVMModule::setPinMonitorMode(int index, PinMonitorMode mode) {
  if (index < 0 || index >= pins.size()) {
    return false;
  }
  
  pins[index].monitorMode = mode;
  saveConfig(LittleFS);
  return true;
}

const std::vector<EnhancedPinConfig>& KVMModule::getPins() const {
  return pins;
}

const EnhancedPinConfig* KVMModule::getPin(int index) const {
  if (index < 0 || index >= pins.size()) {
    return nullptr;
  }
  
  return &pins[index];
}

void KVMModule::setConnectionCheckInterval(ConnectionCheckInterval interval) {
  checkInterval = interval;
  saveConfig(LittleFS);
}

ConnectionCheckInterval KVMModule::getConnectionCheckInterval() const {
  return checkInterval;
}

void KVMModule::setUseDHCP(bool use) {
  useDHCP = use;
  saveConfig(LittleFS);
}

bool KVMModule::getUseDHCP() const {
  return useDHCP;
}

void KVMModule::updatePinMonitoring() {
  for (auto& pin : pins) {
    if (pin.monitorMode != PIN_MONITOR_OFF) {
      // Читаем текущее состояние пина
      int currentState = digitalRead(pin.pin);
      
      // Если состояние изменилось
      if ((currentState == HIGH) != pin.state) {
        // Обновляем состояние
        pin.state = (currentState == HIGH);
        pin.lastStateChange = millis();
        
        // Если активирован режим со звуком
        if (pin.monitorMode == PIN_MONITOR_BUZZ) {
          // Подаем звуковой сигнал
          if (pin.state) {
            M5.Speaker.tone(highToneFrequency, toneDuration);
          } else {
            M5.Speaker.tone(lowToneFrequency, toneDuration);
          }
          
          // Через заданное время отключаем сигнал
          delay(toneDuration + 10);
          M5.Speaker.stop();
        }
      }
    }
  }
}

void KVMModule::performConnectionCheck() {
  if (checkInterval == CHECK_OFF) {
    return;
  }
  
  unsigned long currentMillis = millis();
  unsigned long interval;
  
  switch (checkInterval) {
    case CHECK_10SEC:
      interval = 10000;
      break;
    case CHECK_30SEC:
      interval = 30000;
      break;
    case CHECK_1MIN:
      interval = 60000;
      break;
    case CHECK_5MIN:
      interval = 300000;
      break;
    case CHECK_30MIN:
      interval = 1800000;
      break;
    default:
      return;
  }
  
  if (currentMillis - lastConnectionCheck >= interval) {
    lastConnectionCheck = currentMillis;
    
    // Проверяем соединение
    if (WiFi.status() == WL_CONNECTED) {
      // Соединение активно, выводим информацию о качестве
      Serial.print("WiFi RSSI: ");
      Serial.print(WiFi.RSSI());
      Serial.println(" dBm");
    } else {
      // Соединение отсутствует
      Serial.println("WiFi connection lost");
      
      // Если настроено автоматическое подключение, пытаемся восстановить
      if (WiFi.SSID().length() > 0) {
        Serial.println("Attempting to reconnect...");
        WiFi.reconnect();
      }
    }
  }
}

void KVMModule::saveConfig(fs::FS &fs) {
  DynamicJsonDocument doc(4096);
  
  // Сохраняем настройки пинов
  JsonArray pinsArray = doc.createNestedArray("pins");
  for (const auto& pin : pins) {
    JsonObject pinObj = pinsArray.createNestedObject();
    pinObj["pin"] = pin.pin;
    pinObj["name"] = pin.name;
    pinObj["state"] = pin.state;
    pinObj["monitorMode"] = pin.monitorMode;
  }
  
  // Сохраняем настройки проверки соединения
  doc["checkInterval"] = static_cast<int>(checkInterval);
  doc["useDHCP"] = useDHCP;
  
  // Открываем файл для записи
  File configFile = fs.open("/kvm_config.json", "w");
  if (!configFile) {
    return;
  }
  
  serializeJson(doc, configFile);
  configFile.close();
}

void KVMModule::loadConfig(fs::FS &fs) {
  if (!fs.exists("/kvm_config.json")) {
    return;
  }
  
  File configFile = fs.open("/kvm_config.json", "r");
  if (!configFile) {
    return;
  }
  
  DynamicJsonDocument doc(4096);
  DeserializationError error = deserializeJson(doc, configFile);
  configFile.close();
  
  if (error) {
    return;
  }
  
  // Загружаем настройки пинов
  pins.clear();
  if (doc.containsKey("pins")) {
    JsonArray pinsArray = doc["pins"];
    
    for (JsonObject pinObj : pinsArray) {
      EnhancedPinConfig pin;
      pin.pin = pinObj["pin"];
      pin.name = pinObj["name"].as<String>();
      pin.state = pinObj["state"];
      pin.monitorMode = static_cast<PinMonitorMode>(pinObj["monitorMode"].as<int>());
      pin.lastStateChange = 0;
      
      pins.push_back(pin);
    }
  }
  
  // Загружаем настройки проверки соединения
  if (doc.containsKey("checkInterval")) {
    checkInterval = static_cast<ConnectionCheckInterval>(doc["checkInterval"].as<int>());
  }
  
  if (doc.containsKey("useDHCP")) {
    useDHCP = doc["useDHCP"].as<bool>();
  }
}

void KVMModule::setupAPI() {
  // API для получения информации о пинах
  server->on("/api/kvm/pins", HTTP_GET, [this](AsyncWebServerRequest *request) {
    DynamicJsonDocument doc(4096);
    JsonArray pinsArray = doc.createNestedArray("pins");
    
    for (const auto& pin : pins) {
      JsonObject pinObj = pinsArray.createNestedObject();
      pinObj["pin"] = pin.pin;
      pinObj["name"] = pin.name;
      pinObj["state"] = pin.state;
      pinObj["monitorMode"] = pin.monitorMode;
    }
    
    doc["checkInterval"] = static_cast<int>(checkInterval);
    doc["useDHCP"] = useDHCP;
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });
  
  // API для управления пином
  server->on("/api/kvm/pin", HTTP_POST, [this](AsyncWebServerRequest *request) {
    if (!request->hasParam("index", true)) {
      request->send(400, "application/json", "{\"error\":\"Missing pin index\"}");
      return;
    }
    
    int index = request->getParam("index", true)->value().toInt();
    
    if (index < 0 || index >= pins.size()) {
      request->send(404, "application/json", "{\"error\":\"Pin not found\"}");
      return;
    }
    
    bool result;
    
    if (request->hasParam("state", true)) {
      bool state = (request->getParam("state", true)->value() == "true" || 
                    request->getParam("state", true)->value() == "1");
      result = setPin(index, state);
    } else {
      result = togglePin(index);
    }
    
    if (request->hasParam("monitor", true)) {
      int mode = request->getParam("monitor", true)->value().toInt();
      if (mode >= PIN_MONITOR_OFF && mode <= PIN_MONITOR_BUZZ) {
        setPinMonitorMode(index, static_cast<PinMonitorMode>(mode));
      }
    }
    
    DynamicJsonDocument doc(512);
    doc["success"] = result;
    doc["pin"] = pins[index].pin;
    doc["state"] = pins[index].state;
    doc["monitorMode"] = pins[index].monitorMode;
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });
  
  // API для добавления нового пина
  server->on("/api/kvm/add", HTTP_POST, [this](AsyncWebServerRequest *request) {
    if (!request->hasParam("pin", true) || !request->hasParam("name", true)) {
      request->send(400, "application/json", "{\"error\":\"Missing pin or name parameter\"}");
      return;
    }
    
    int pin = request->getParam("pin", true)->value().toInt();
    String name = request->getParam("name", true)->value();
    
    bool result = addPin(pin, name);
    
    DynamicJsonDocument doc(256);
    doc["success"] = result;
    
    if (result) {
      doc["pin"] = pin;
      doc["name"] = name;
    } else {
      doc["error"] = "Pin already exists or invalid pin number";
    }
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });
  
  // API для удаления пина
  server->on("/api/kvm/remove", HTTP_POST, [this](AsyncWebServerRequest *request) {
    if (!request->hasParam("index", true)) {
      request->send(400, "application/json", "{\"error\":\"Missing pin index\"}");
      return;
    }
    
    int index = request->getParam("index", true)->value().toInt();
    bool result = removePin(index);
    
    DynamicJsonDocument doc(256);
    doc["success"] = result;
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });
  
  // API для настройки интервала проверки соединения
  server->on("/api/kvm/connectioncheck", HTTP_POST, [this](AsyncWebServerRequest *request) {
    if (!request->hasParam("interval", true)) {
      request->send(400, "application/json", "{\"error\":\"Missing interval parameter\"}");
      return;
    }
    
    int intervalValue = request->getParam("interval", true)->value().toInt();
    
    if (intervalValue < CHECK_OFF || intervalValue > CHECK_30MIN) {
      request->send(400, "application/json", "{\"error\":\"Invalid interval value\"}");
      return;
    }
    
    setConnectionCheckInterval(static_cast<ConnectionCheckInterval>(intervalValue));
    
    DynamicJsonDocument doc(256);
    doc["success"] = true;
    doc["checkInterval"] = static_cast<int>(checkInterval);
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });
  
  // API для настройки DHCP
  server->on("/api/kvm/dhcp", HTTP_POST, [this](AsyncWebServerRequest *request) {
    if (!request->hasParam("enabled", true)) {
      request->send(400, "application/json", "{\"error\":\"Missing enabled parameter\"}");
      return;
    }
    
    bool enabled = (request->getParam("enabled", true)->value() == "true" || 
                   request->getParam("enabled", true)->value() == "1");
    
    setUseDHCP(enabled);
    
    DynamicJsonDocument doc(256);
    doc["success"] = true;
    doc["useDHCP"] = useDHCP;
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });
}

bool KVMModule::hasPin(int gpioPin) const {
  for (const auto& pin : pins) {
    if (pin.pin == gpioPin) {
      return true;
    }
  }
  return false;
}

#endif // KVM_MODULE_H