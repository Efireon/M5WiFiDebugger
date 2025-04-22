#include <M5StickCPlus2.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <vector>

// Подключаем наши модули
#include "honeypot.h"
#include "network_tools.h"
#include "device_manager.h"

// Определение разделов меню
enum MenuSection {
  MENU_MAIN,       // Главное меню
  MENU_AP_OPTIONS, // Опции режима точки доступа
  MENU_WIFI_SCAN,  // Сканирование и отладка WiFi
  MENU_KVM_OPTIONS,// Опции KVM
  MENU_KVM_MONITOR,// Мониторинг KVM
  MENU_IR_CONTROL  // ИК-управление (заглушка)
};

// Структура для пунктов меню
struct MenuItem {
  const char* title;
  MenuSection section;
};

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

// Структура для хранения результатов сканирования WiFi
struct WiFiResult {
  String ssid;
  int32_t rssi;
  uint8_t encryptionType;
  int32_t channel;
};

// Расширенная структура для пинов
struct EnhancedPinConfig {
  int pin;
  String name;
  bool state;
  PinMonitorMode monitorMode;
  unsigned long lastStateChange;
};

// Расширенная конфигурация для AP режима
struct APConfig {
  APMode mode;
  String ssid;
  String password;
  bool hidden;
  int channel;
};

// Класс для управления KVM пинами
class KVMModule {
private:
  std::vector<EnhancedPinConfig> pins;
  ConnectionCheckInterval checkInterval;
  bool useDHCP;
  unsigned long lastCheckTime;

public:
  KVMModule() : checkInterval(CHECK_OFF), useDHCP(true), lastCheckTime(0) {}
  
  // Инициализация
  void begin() {
    // Загрузка конфигурации из файла
    loadConfig();
  }
  
  // Добавление нового пина
  bool addPin(int pin, const String& name) {
    // Проверяем, не существует ли уже такой пин
    for (const auto& p : pins) {
      if (p.pin == pin) {
        return false; // Пин уже добавлен
      }
    }
    
    // Добавляем новый пин
    EnhancedPinConfig newPin;
    newPin.pin = pin;
    newPin.name = name;
    newPin.state = false;
    newPin.monitorMode = PIN_MONITOR_OFF;
    newPin.lastStateChange = 0;
    
    pins.push_back(newPin);
    
    // Настраиваем пин
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);
    
    // Сохраняем конфигурацию
    saveConfig();
    
    return true;
  }
  
  // Переключение состояния пина
  void togglePin(int index) {
    if (index >= 0 && index < pins.size()) {
      pins[index].state = !pins[index].state;
      digitalWrite(pins[index].pin, pins[index].state ? HIGH : LOW);
      pins[index].lastStateChange = millis();
      saveConfig();
    }
  }
  
  // Установка режима мониторинга пина
  void setMonitorMode(int index, PinMonitorMode mode) {
    if (index >= 0 && index < pins.size()) {
      pins[index].monitorMode = mode;
      saveConfig();
    }
  }
  
  // Установка интервала проверки соединения
  void setCheckInterval(ConnectionCheckInterval interval) {
    checkInterval = interval;
    saveConfig();
  }
  
  // Получение интервала проверки соединения
  ConnectionCheckInterval getCheckInterval() const {
    return checkInterval;
  }
  
  // Установка режима DHCP
  void setUseDHCP(bool use) {
    useDHCP = use;
    saveConfig();
  }
  
  // Получение состояния DHCP
  bool getUseDHCP() const {
    return useDHCP;
  }
  
  // Получение списка пинов
  const std::vector<EnhancedPinConfig>& getPins() const {
    return pins;
  }
  
  // Обновление состояния пинов
  void update() {
    // Текущее время
    unsigned long currentTime = millis();
    
    // Проверка состояния мониторинга
    for (auto& pin : pins) {
      if (pin.monitorMode != PIN_MONITOR_OFF) {
        // Считываем текущее состояние пина
        int currentState = digitalRead(pin.pin);
        
        // Если состояние изменилось
        if (currentState != pin.state) {
          pin.state = currentState;
          pin.lastStateChange = currentTime;
          
          // Если включен режим со звуком
          if (pin.monitorMode == PIN_MONITOR_BUZZ) {
            // Воспроизводим звуковой сигнал
            if (currentState) {
              M5.Speaker.tone(2000, 100); // Высокий сигнал для HIGH
            } else {
              M5.Speaker.tone(1000, 100); // Низкий сигнал для LOW
            }
          }
        }
      }
    }
    
    // Проверка соединения по таймеру
    if (checkInterval != CHECK_OFF) {
      unsigned long interval = 0;
      
      switch (checkInterval) {
        case CHECK_10SEC: interval = 10000; break;
        case CHECK_30SEC: interval = 30000; break;
        case CHECK_1MIN:  interval = 60000; break;
        case CHECK_5MIN:  interval = 300000; break;
        case CHECK_30MIN: interval = 1800000; break;
        default: break;
      }
      
      if (interval > 0 && currentTime - lastCheckTime > interval) {
        lastCheckTime = currentTime;
        // Выполняем проверку соединения
        checkConnection();
      }
    }
  }
  
  // Проверка соединения
  void checkConnection() {
    // Проверяем состояние WiFi соединения
    if (WiFi.status() == WL_CONNECTED) {
      // Измеряем силу сигнала
      int rssi = WiFi.RSSI();
      
      // Выводим информацию о соединении, если нужно
      // ...
    } else {
      // Соединение отсутствует
      // ...
    }
  }
  
  // Сохранение конфигурации
  void saveConfig() {
    // Создаем JSON документ
    DynamicJsonDocument doc(4096);
    
    // Сохраняем пины
    JsonArray pinsArray = doc.createNestedArray("pins");
    for (const auto& pin : pins) {
      JsonObject pinObj = pinsArray.createNestedObject();
      pinObj["pin"] = pin.pin;
      pinObj["name"] = pin.name;
      pinObj["state"] = pin.state;
      pinObj["monitorMode"] = pin.monitorMode;
    }
    
    // Сохраняем настройки
    doc["checkInterval"] = checkInterval;
    doc["useDHCP"] = useDHCP;
    
    // Открываем файл для записи
    File configFile = LittleFS.open("/kvm_config.json", "w");
    if (!configFile) {
      Serial.println("Failed to open kvm_config.json for writing");
      return;
    }
    
    // Записываем JSON в файл
    serializeJson(doc, configFile);
    configFile.close();
  }
  
  // Загрузка конфигурации
  void loadConfig() {
    // Проверяем, существует ли файл
    if (!LittleFS.exists("/kvm_config.json")) {
      Serial.println("kvm_config.json not found, using defaults");
      return;
    }
    
    // Открываем файл
    File configFile = LittleFS.open("/kvm_config.json", "r");
    if (!configFile) {
      Serial.println("Failed to open kvm_config.json");
      return;
    }
    
    // Определяем размер файла
    size_t size = configFile.size();
    if (size > 4096) {
      Serial.println("Config file size is too large");
      configFile.close();
      return;
    }
    
    // Читаем содержимое файла
    std::unique_ptr<char[]> buf(new char[size]);
    configFile.readBytes(buf.get(), size);
    configFile.close();
    
    // Парсим JSON
    DynamicJsonDocument doc(4096);
    DeserializationError error = deserializeJson(doc, buf.get());
    
    if (error) {
      Serial.print("JSON parsing failed: ");
      Serial.println(error.c_str());
      return;
    }
    
    // Очищаем текущий список пинов
    pins.clear();
    
    // Загружаем пины
    if (doc.containsKey("pins")) {
      JsonArray pinsArray = doc["pins"];
      for (JsonObject pinObj : pinsArray) {
        EnhancedPinConfig pin;
        pin.pin = pinObj["pin"];
        pin.name = pinObj["name"].as<String>();
        pin.state = pinObj["state"];
        pin.monitorMode = (PinMonitorMode)pinObj["monitorMode"].as<int>();
        pin.lastStateChange = 0;
        
        pins.push_back(pin);
        
        // Настраиваем пин
        pinMode(pin.pin, OUTPUT);
        digitalWrite(pin.pin, pin.state ? HIGH : LOW);
      }
    }
    
    // Загружаем настройки
    if (doc.containsKey("checkInterval")) {
      checkInterval = (ConnectionCheckInterval)doc["checkInterval"].as<int>();
    }
    
    if (doc.containsKey("useDHCP")) {
      useDHCP = doc["useDHCP"].as<bool>();
    }
  }
};

// Глобальные переменные
MenuSection currentSection = MENU_MAIN;  // Текущий раздел меню
int selectedMenuItem = 0;                // Выбранный пункт меню
int menuStartPosition = 0;               // Начальная позиция для отображения меню
AsyncWebServer server(80);               // Веб-сервер на порту 80
WiFiManager wifiManager;                 // Менеджер WiFi
APConfig apConfig = {AP_MODE_OFF, "M5StickDebug", "12345678", false, 1}; // Конфигурация AP

std::vector<WiFiResult> networks;        // Список найденных сетей

// Наши модули
KVMModule kvmModule;
NetworkTools networkTools;
DeviceManager deviceManager;
Honeypot honeypot;

// Описание пунктов главного меню
const MenuItem mainMenuItems[] = {
  {"AP Options", MENU_AP_OPTIONS},
  {"WiFi Scan & Debug", MENU_WIFI_SCAN},
  {"KVM Options", MENU_KVM_OPTIONS},
  {"KVM Monitor", MENU_KVM_MONITOR},
  {"IR Control", MENU_IR_CONTROL}
};
#define MAIN_MENU_ITEMS_COUNT (sizeof(mainMenuItems) / sizeof(MenuItem))

// Пункты подменю AP Options
const char* apOptionsItems[] = {
  "AP Mode: Off",
  "AP Mode: Normal",
  "AP Mode: Repeater",
  "AP Mode: Hidden",
  "AP Mode: Honeypot",
  "SSID & Password",
  "Back to Main Menu"
};
#define AP_OPTIONS_ITEMS_COUNT (sizeof(apOptionsItems) / sizeof(char*))

// Буфер для сообщений на дисплее
char displayBuffer[128];

// Переменные для обработки кнопок
unsigned long buttonALastPress = 0;
unsigned long buttonCLastPress = 0;
bool buttonALongPress = false;
bool buttonCLongPress = false;

// Прототипы функций
void setupDisplay();
void setupWiFi();
void setupWebServer();
void handleButtons();
void drawMenu();
void handleMenuAction();
void scanWiFiNetworks();
void updateAccessPointMode();
void performNetworkDiagnostics();
void saveConfiguration();
void loadConfiguration();
void playFindMeSound();

// Функция инициализации
void setup() {
  // Инициализация M5StickCPlus2
  M5.begin();
  M5.Lcd.setRotation(3);  // Горизонтальная ориентация
  
  // Инициализация файловой системы
  if (!LittleFS.begin(true)) {
    M5.Lcd.println("LittleFS Mount Failed");
  }
  
  // Загрузка конфигурации
  loadConfiguration();
  
  // Инициализируем наши модули
  kvmModule.begin();
  deviceManager.begin();
  
  // Настройка экрана
  setupDisplay();
  
  // Настройка WiFi
  setupWiFi();
  
  // Настройка веб-сервера
  setupWebServer();
  
  // Вывод информации о запуске
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setCursor(0, 0);
  M5.Lcd.setTextSize(1);
  M5.Lcd.println("M5Stick WiFi Debug Tool");
  M5.Lcd.println("----------------------");

  // Отображаем главное меню
  drawMenu();
}

// Основной цикл
void loop() {
  // Обновление состояния кнопок
  M5.update();
  handleButtons();
  
  // Обновляем наши модули
  kvmModule.update();
  deviceManager.update();
  
  // Отображаем актуальную информацию в зависимости от режима
  if (currentSection == MENU_KVM_MONITOR) {
    // Периодически обновляем информацию на экране
    static unsigned long lastUpdateTime = 0;
    unsigned long currentTime = millis();
    
    if (currentTime - lastUpdateTime > 1000) { // Обновляем раз в секунду
      lastUpdateTime = currentTime;
      drawMenu(); // Перерисовываем меню с актуальной информацией
    }
  }
  
  // Небольшая задержка для стабильности
  delay(50);
}

// Настройка экрана
void setupDisplay() {
  M5.Lcd.setRotation(3);  // Горизонтальная ориентация
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setTextColor(WHITE);
  M5.Lcd.setCursor(0, 0);
}

// Настройка WiFi
void setupWiFi() {
  // Настройка параметров WiFiManager
  wifiManager.setAPCallback([](WiFiManager* mgr) {
    M5.Lcd.fillScreen(BLACK);
    M5.Lcd.setCursor(0, 0);
    M5.Lcd.println("Configuration Mode");
    M5.Lcd.println("Connect to WiFi:");
    M5.Lcd.println(mgr->getConfigPortalSSID());
    M5.Lcd.println("Visit: 192.168.4.1");
  });
  
  // Проверка сохраненных настроек и режима AP
  if (apConfig.mode != AP_MODE_OFF) {
    // Если активирован режим AP, запускаем точку доступа
    updateAccessPointMode();
  } else {
    // Пытаемся подключиться к Wi-Fi, если есть сохраненные настройки
    String savedSSID = WiFi.SSID();
    String savedPass = WiFi.psk();
    
    if (savedSSID.length() > 0) {
      WiFi.begin(savedSSID.c_str(), savedPass.c_str());
      
      // Ожидание подключения с таймаутом
      int timeout = 0;
      while (WiFi.status() != WL_CONNECTED && timeout < 20) {
        delay(500);
        M5.Lcd.print(".");
        timeout++;
      }
      
      if (WiFi.status() == WL_CONNECTED) {
        M5.Lcd.println("\nConnected!");
      } else {
        // Если не удалось подключиться, запускаем режим точки доступа
        apConfig.mode = AP_MODE_NORMAL;
        updateAccessPointMode();
      }
    } else {
      // Если нет сохраненных настроек, запускаем режим точки доступа
      apConfig.mode = AP_MODE_NORMAL;
      updateAccessPointMode();
    }
  }
}

// Обновление режима точки доступа
void updateAccessPointMode() {
  switch (apConfig.mode) {
    case AP_MODE_OFF:
      // Выключаем режим AP если был активен
      if (WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA) {
        WiFi.softAPdisconnect(true);
        WiFi.mode(WIFI_STA);
      }
      break;
      
    case AP_MODE_NORMAL:
      // Обычный режим точки доступа
      WiFi.mode(WIFI_AP);
      WiFi.softAP(apConfig.ssid.c_str(), apConfig.password.c_str(), apConfig.channel);
      break;
      
    case AP_MODE_HIDDEN:
      // Скрытый режим точки доступа
      WiFi.mode(WIFI_AP);
      WiFi.softAP(apConfig.ssid.c_str(), apConfig.password.c_str(), apConfig.channel, true);
      break;
      
    case AP_MODE_REPEATER:
      // Режим ретранслятора (если подключены к WiFi)
      if (WiFi.status() == WL_CONNECTED) {
        WiFi.mode(WIFI_AP_STA);
        WiFi.softAP(apConfig.ssid.c_str(), apConfig.password.c_str(), apConfig.channel);
      } else {
        // Если не подключены, то обычный режим AP
        apConfig.mode = AP_MODE_NORMAL;
        WiFi.mode(WIFI_AP);
        WiFi.softAP(apConfig.ssid.c_str(), apConfig.password.c_str(), apConfig.channel);
      }
      break;
      
    case AP_MODE_HONEYPOT:
      // Режим ловушки
      honeypot.setSSID(apConfig.ssid);
      honeypot.setChannel(apConfig.channel);
      honeypot.begin(server);
      break;
  }
  
  // Сохраняем конфигурацию
  saveConfiguration();
}

// Настройка веб-сервера
void setupWebServer() {
  // Маршрут для корневой страницы
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    // Чтение HTML-файла из LittleFS
    File file = LittleFS.open("/index.html", "r");
    if (!file) {
      // Если файл не найден, отправляем базовый HTML
      request->send(200, "text/html", "<html><body><h1>M5Stick WiFi Debug Tool</h1>"
                                        "<p>Configuration Interface</p>"
                                        "<a href='/scan'>Scan Networks</a><br>"
                                        "<a href='/kvm'>KVM Controls</a><br>"
                                        "<a href='/ap'>AP Settings</a></body></html>");
    } else {
      request->send(LittleFS, "/index.html", "text/html");
    }
  });
  
  // Маршрут для сканирования сетей
  server.on("/scan", HTTP_GET, [](AsyncWebServerRequest *request){
    scanWiFiNetworks();
    
    // Формирование JSON с результатами сканирования
    DynamicJsonDocument doc(4096);
    JsonArray networksArray = doc.createNestedArray("networks");
    
    for (auto& network : networks) {
      JsonObject netObj = networksArray.createNestedObject();
      netObj["ssid"] = network.ssid;
      netObj["rssi"] = network.rssi;
      netObj["encryption"] = network.encryptionType == WIFI_AUTH_OPEN ? "Open" : "Encrypted";
      netObj["channel"] = network.channel;
    }
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });
  
  // Маршрут для управления режимом AP
  server.on("/ap", HTTP_GET, [](AsyncWebServerRequest *request){
    DynamicJsonDocument doc(512);
    doc["mode"] = apConfig.mode;
    doc["ssid"] = apConfig.ssid;
    doc["hidden"] = apConfig.hidden;
    doc["channel"] = apConfig.channel;
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });
  
  // Маршрут для изменения настроек AP
  server.on("/ap/config", HTTP_POST, [](AsyncWebServerRequest *request){
    if (request->hasParam("mode", true)) {
      int modeValue = request->getParam("mode", true)->value().toInt();
      if (modeValue >= AP_MODE_OFF && modeValue <= AP_MODE_HONEYPOT) {
        apConfig.mode = (APMode)modeValue;
      }
    }
    
    if (request->hasParam("ssid", true)) {
      apConfig.ssid = request->getParam("ssid", true)->value();
    }
    
    if (request->hasParam("password", true)) {
      apConfig.password = request->getParam("password", true)->value();
    }
    
    if (request->hasParam("channel", true)) {
      int channel = request->getParam("channel", true)->value().toInt();
      if (channel >= 1 && channel <= 13) {
        apConfig.channel = channel;
      }
    }
    
    if (request->hasParam("hidden", true)) {
      apConfig.hidden = (request->getParam("hidden", true)->value() == "true");
    }
    
    // Применяем новые настройки
    updateAccessPointMode();
    
    request->send(200, "text/plain", "AP settings updated");
  });
  
  // Маршрут для просмотра логов honeypot
  server.on("/ap/honeypot/logs", HTTP_GET, [](AsyncWebServerRequest *request){
    DynamicJsonDocument doc(2048);
    JsonArray logsArray = doc.createNestedArray("logs");
    
    const HoneypotConnection* logs = honeypot.getConnections();
    int logsCount = honeypot.getConnectionCount();
    
    for (int i = 0; i < logsCount; i++) {
      JsonObject log = logsArray.createNestedObject();
      log["ip"] = logs[i].clientIP.toString();
      log["port"] = logs[i].port;
      log["timestamp"] = logs[i].timestamp;
      log["data"] = logs[i].requestData;
    }
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });
  
  // Маршрут для подключения к сети
  server.on("/connect", HTTP_POST, [](AsyncWebServerRequest *request){
    // Проверяем наличие необходимых параметров
    if (!request->hasParam("ssid", true)) {
      request->send(400, "text/plain", "Missing SSID parameter");
      return;
    }
    
    String ssid = request->getParam("ssid", true)->value();
    String password = "";
    
    if (request->hasParam("password", true)) {
      password = request->getParam("password", true)->value();
    }
    
    // Отключаем текущий режим AP, если активен
    if (apConfig.mode != AP_MODE_OFF) {
      apConfig.mode = AP_MODE_OFF;
      updateAccessPointMode();
    }
    
    // Подключаемся к сети
    WiFi.disconnect();
    WiFi.begin(ssid.c_str(), password.c_str());
    
    // Отправляем статус
    request->send(200, "text/plain", "Connecting to network...");
  });
  
  // Маршрут для управления GPIO (KVM)
  server.on("/kvm", HTTP_GET, [](AsyncWebServerRequest *request){
    // Формирование JSON с состоянием пинов
    DynamicJsonDocument doc(2048);
    JsonArray pinsArray = doc.createNestedArray("pins");
    
    const auto& pins = kvmModule.getPins();
    for (const auto& pin : pins) {
      JsonObject pinObj = pinsArray.createNestedObject();
      pinObj["pin"] = pin.pin;
      pinObj["name"] = pin.name;
      pinObj["state"] = pin.state;
      pinObj["monitorMode"] = pin.monitorMode;
    }
    
    doc["connectionCheck"] = (int)kvmModule.getCheckInterval();
    doc["useDHCP"] = kvmModule.getUseDHCP();
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });
  
  // Маршрут для управления состоянием пина
  server.on("/kvm/pin", HTTP_POST, [](AsyncWebServerRequest *request){
    if (!request->hasParam("index", true)) {
      request->send(400, "text/plain", "Missing pin index parameter");
      return;
    }
    
    int pinIndex = request->getParam("index", true)->value().toInt();
    const auto& pins = kvmModule.getPins();
    
    // Проверяем, существует ли такой пин
    if (pinIndex >= 0 && pinIndex < pins.size()) {
      // Если указан параметр state, меняем состояние
      if (request->hasParam("state", true)) {
        bool newState = (request->getParam("state", true)->value() == "true" || 
                         request->getParam("state", true)->value() == "1");
        // Используем метод togglePin для изменения состояния
        if (newState != pins[pinIndex].state) {
          kvmModule.togglePin(pinIndex);
        }
      } else {
        // Иначе просто переключаем
        kvmModule.togglePin(pinIndex);
      }
      
      // Если указан режим мониторинга
      if (request->hasParam("monitor", true)) {
        int mode = request->getParam("monitor", true)->value().toInt();
        if (mode >= PIN_MONITOR_OFF && mode <= PIN_MONITOR_BUZZ) {
          kvmModule.setMonitorMode(pinIndex, (PinMonitorMode)mode);
        }
      }
      
      request->send(200, "text/plain", "Pin updated");
    } else {
      request->send(404, "text/plain", "Pin not found");
    }
  });
  
  // API для управления через curl
  server.on("/api/kvm/pin", HTTP_POST, [](AsyncWebServerRequest *request){
    // Тот же функционал, что и в /kvm/pin, но в формате API
    // для совместимости с curl и скриптами
    if (!request->hasParam("index", true)) {
      request->send(400, "application/json", "{\"error\":\"Missing pin index parameter\"}");
      return;
    }
    
    int pinIndex = request->getParam("index", true)->value().toInt();
    const auto& pins = kvmModule.getPins();
    
    if (pinIndex >= 0 && pinIndex < pins.size()) {
      if (request->hasParam("state", true)) {
        bool newState = (request->getParam("state", true)->value() == "true" || 
                         request->getParam("state", true)->value() == "1");
        // Используем метод togglePin для изменения состояния
        if (newState != pins[pinIndex].state) {
          kvmModule.togglePin(pinIndex);
        }
      } else {
        kvmModule.togglePin(pinIndex);
      }
      
      // Формируем JSON с результатом
      DynamicJsonDocument doc(256);
      doc["success"] = true;
      doc["pin"] = pins[pinIndex].pin;
      doc["state"] = pins[pinIndex].state;
      
      String response;
      serializeJson(doc, response);
      request->send(200, "application/json", response);
    } else {
      request->send(404, "application/json", "{\"error\":\"Pin not found\"}");
    }
  });
  
  // Маршрут для добавления нового пина
  server.on("/kvm/add", HTTP_POST, [](AsyncWebServerRequest *request){
    if (!request->hasParam("pin", true) || !request->hasParam("name", true)) {
      request->send(400, "text/plain", "Missing pin or name parameter");
      return;
    }
    
    int pin = request->getParam("pin", true)->value().toInt();
    String name = request->getParam("name", true)->value();
    
    // Добавляем новый пин
    if (kvmModule.addPin(pin, name)) {
      request->send(200, "text/plain", "Pin added successfully");
    } else {
      request->send(400, "text/plain", "Pin already configured");
    }
  });
  
  // Маршрут для настройки интервала проверки соединения
  server.on("/kvm/connectioncheck", HTTP_POST, [](AsyncWebServerRequest *request){
    if (request->hasParam("interval", true)) {
      int interval = request->getParam("interval", true)->value().toInt();
      if (interval >= CHECK_OFF && interval <= CHECK_30MIN) {
        kvmModule.setCheckInterval((ConnectionCheckInterval)interval);
        request->send(200, "text/plain", "Connection check interval updated");
        return;
      }
    }
    request->send(400, "text/plain", "Invalid interval parameter");
  });
  
  // Маршрут для диагностики сети
  server.on("/diagnostic", HTTP_GET, [](AsyncWebServerRequest *request){
    performNetworkDiagnostics();
    
    const auto& networkInfo = deviceManager.getNetworkInfo();
    const auto& sensorData = deviceManager.getSensorData();
    
    DynamicJsonDocument doc(1024);
    doc["connected"] = networkInfo.connected;
    doc["ssid"] = networkInfo.ssid;
    doc["rssi"] = networkInfo.rssi;
    doc["ip"] = networkInfo.localIP;
    doc["gateway"] = networkInfo.gateway;
    doc["subnet"] = networkInfo.subnet;
    doc["dns"] = networkInfo.dns;
    
    // Дополнительная информация о точке доступа
    if (apConfig.mode != AP_MODE_OFF) {
      doc["ap_mode"] = apConfig.mode;
      doc["ap_ssid"] = apConfig.ssid;
      doc["ap_ip"] = WiFi.softAPIP().toString();
      doc["ap_stations"] = WiFi.softAPgetStationNum();
    }
    
    // Информация об устройстве
    doc["battery"] = sensorData.batteryVoltage;
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });
  
  // API для поиска устройства (Find Me)
  server.on("/device/findme", HTTP_POST, [](AsyncWebServerRequest *request){
    // Запуск звукового сигнала
    deviceManager.activateFindMe();
    request->send(200, "text/plain", "Find Me signal activated");
  });
  
  // Запуск веб-сервера
  server.begin();
}

// Функция воспроизведения сигнала "Find Me"
void playFindMeSound() {
  // Воспроизводим звуковой сигнал прерывисто
  for (int i = 0; i < 5; i++) {
    M5.Speaker.tone(2000, 200);
    delay(300);
    M5.Speaker.tone(1500, 200);
    delay(300);
  }
  // Останавливаем звук
  M5.Speaker.tone(0);
}

// Обработка нажатий кнопок
void handleButtons() {
  // Проверка долгого нажатия на кнопку A (Home)
  if (M5.BtnA.isPressed()) {
    if (buttonALastPress == 0) {
      buttonALastPress = millis();
    } else if (!buttonALongPress && millis() - buttonALastPress > 2000) {
      // Долгое нажатие на A - возвращаемся в главное меню
      buttonALongPress = true;
      currentSection = MENU_MAIN;
      selectedMenuItem = 0;
      menuStartPosition = 0;
      drawMenu();
    }
  } else {
    if (buttonALastPress > 0 && !buttonALongPress) {
      // Короткое нажатие на A - выбор пункта меню
      handleMenuAction();
    }
    buttonALastPress = 0;
    buttonALongPress = false;
  }
  
  // Проверка долгого нажатия на кнопку C (Power)
  if (M5.BtnC.isPressed()) {
    if (buttonCLastPress == 0) {
      buttonCLastPress = millis();
    } else if (!buttonCLongPress && millis() - buttonCLastPress > 3000) {
      // Долгое нажатие на C - вкл/выкл устройства
      buttonCLongPress = true;
      // Здесь можно реализовать выключение или переход в режим сна
      M5.Lcd.fillScreen(BLACK);
      M5.Lcd.setCursor(0, 0);
      M5.Lcd.println("Shutting down...");
      delay(1000);
      M5.Power.powerOff();
    }
  } else {
    if (buttonCLastPress > 0 && !buttonCLongPress) {
      // Короткое нажатие на C - навигация вверх
      if (selectedMenuItem > 0) {
        selectedMenuItem--;
        if (selectedMenuItem < menuStartPosition) {
          menuStartPosition = selectedMenuItem;
        }
        drawMenu();
      }
    }
    buttonCLastPress = 0;
    buttonCLongPress = false;
  }
  
  // Кнопка B - навигация вниз
  if (M5.BtnB.wasPressed()) {
    int maxItems = 0;
    switch (currentSection) {
      case MENU_MAIN:
        maxItems = MAIN_MENU_ITEMS_COUNT;
        break;
      case MENU_AP_OPTIONS:
        maxItems = AP_OPTIONS_ITEMS_COUNT;
        break;
      case MENU_WIFI_SCAN:
        maxItems = networks.size() > 0 ? networks.size() : 1;
        break;
      case MENU_KVM_OPTIONS: {
        const auto& pins = kvmModule.getPins();
        maxItems = pins.size() + 3; // Пины + настройки + возврат
        break;
      }
      default:
        maxItems = 5; // По умолчанию
    }
    
    if (selectedMenuItem < maxItems - 1) {
      selectedMenuItem++;
      // Если выбранный элемент выходит за пределы экрана, прокручиваем
      int displayLines = M5.Lcd.height() / 16; // Примерное количество строк на экране
      if (selectedMenuItem >= menuStartPosition + displayLines) {
        menuStartPosition = selectedMenuItem - displayLines + 1;
      }
      drawMenu();
    }
  }
}

// Отрисовка меню
void drawMenu() {
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setCursor(0, 0);
  M5.Lcd.setTextSize(1);
  
  // Отображаем заголовок и заряд батареи
  char batteryBuf[20];
  const auto& sensorData = deviceManager.getSensorData();
  sprintf(batteryBuf, "Batt: %.2fV", sensorData.batteryVoltage);
  
  switch (currentSection) {
    case MENU_MAIN:
      M5.Lcd.println("MAIN MENU             ");
      break;
    case MENU_AP_OPTIONS:
      M5.Lcd.println("AP OPTIONS            ");
      break;
    case MENU_WIFI_SCAN:
      M5.Lcd.println("WiFi SCAN & DEBUG     ");
      break;
    case MENU_KVM_OPTIONS:
      M5.Lcd.println("KVM OPTIONS           ");
      break;
    case MENU_KVM_MONITOR:
      M5.Lcd.println("KVM MONITOR           ");
      break;
    case MENU_IR_CONTROL:
      M5.Lcd.println("IR CONTROL            ");
      break;
  }
  
  M5.Lcd.setCursor(M5.Lcd.width() - 70, 0);
  M5.Lcd.print(batteryBuf);
  M5.Lcd.drawLine(0, 10, M5.Lcd.width(), 10, WHITE);
  
  // Отображение элементов меню в зависимости от раздела
  int y = 15;
  int displayLines = (M5.Lcd.height() - 15) / 16; // Расчет количества строк на экране
  
  switch (currentSection) {
    case MENU_MAIN: {
      // Главное меню
      for (int i = menuStartPosition; i < MAIN_MENU_ITEMS_COUNT && i < menuStartPosition + displayLines; i++) {
        M5.Lcd.setCursor(5, y);
        if (i == selectedMenuItem) {
          M5.Lcd.fillRect(0, y-1, M5.Lcd.width(), 12, BLUE);
          M5.Lcd.setTextColor(WHITE);
        } else {
          M5.Lcd.setTextColor(WHITE);
        }
        M5.Lcd.print(mainMenuItems[i].title);
        y += 16;
        M5.Lcd.setTextColor(WHITE);
      }
      break;
    }
      
    case MENU_AP_OPTIONS: {
      // Меню настроек точки доступа
      for (int i = menuStartPosition; i < AP_OPTIONS_ITEMS_COUNT && i < menuStartPosition + displayLines; i++) {
        M5.Lcd.setCursor(5, y);
        if (i == selectedMenuItem) {
          M5.Lcd.fillRect(0, y-1, M5.Lcd.width(), 12, BLUE);
          M5.Lcd.setTextColor(WHITE);
        } else {
          M5.Lcd.setTextColor(WHITE);
        }
        
        // Отмечаем активный режим AP
        if (i >= 0 && i <= 4) { // Режимы AP
          if ((i-0) == apConfig.mode) {
            M5.Lcd.print("> ");
          } else {
            M5.Lcd.print("  ");
          }
        }
        
        M5.Lcd.print(apOptionsItems[i]);
        y += 16;
        M5.Lcd.setTextColor(WHITE);
      }
      break;
    }
      
    case MENU_WIFI_SCAN: {
      // Меню сканирования WiFi
      if (networks.size() > 0) {
        for (int i = menuStartPosition; i < networks.size() && i < menuStartPosition + displayLines; i++) {
          M5.Lcd.setCursor(5, y);
          if (i == selectedMenuItem) {
            M5.Lcd.fillRect(0, y-1, M5.Lcd.width(), 12, BLUE);
            M5.Lcd.setTextColor(WHITE);
          } else {
            M5.Lcd.setTextColor(WHITE);
          }
          
          // Показываем базовую информацию о сети
          String networkInfo = networks[i].ssid;
          if (networkInfo.length() > 10) {
            networkInfo = networkInfo.substring(0, 10) + "...";
          }
          networkInfo += " " + String(networks[i].rssi) + "dBm";
          M5.Lcd.print(networkInfo);
          
          y += 16;
          M5.Lcd.setTextColor(WHITE);
        }
      } else {
        M5.Lcd.setCursor(5, y);
        M5.Lcd.print("Press A to scan WiFi");
      }
      break;
    }
      
    case MENU_KVM_MONITOR: {
      // Отображение информации KVM-монитора
      const auto& networkInfo = deviceManager.getNetworkInfo();
      
      M5.Lcd.setCursor(5, y);
      if (networkInfo.connected) {
        M5.Lcd.print("WiFi: ");
        M5.Lcd.print(networkInfo.ssid);
        y += 16;
        M5.Lcd.setCursor(5, y);
        M5.Lcd.print("IP: ");
        M5.Lcd.print(networkInfo.localIP);
      } else {
        M5.Lcd.print("WiFi: Not Connected");
      }
      
      y += 16;
      M5.Lcd.setCursor(5, y);
      M5.Lcd.print("KVM Pins:");
      y += 16;
      
      // Отображаем состояние пинов
      const auto& kvmPins = kvmModule.getPins();
      for (int i = 0; i < kvmPins.size() && y < M5.Lcd.height(); i++) {
        M5.Lcd.setCursor(5, y);
        M5.Lcd.print(kvmPins[i].name);
        M5.Lcd.print(": ");
        if (kvmPins[i].state) {
          M5.Lcd.print("ON");
        } else {
          M5.Lcd.print("OFF");
        }
        y += 16;
      }
      break;
    }
      
    case MENU_KVM_OPTIONS: {
      // Меню настроек KVM
      const auto& pins = kvmModule.getPins();
      
      M5.Lcd.setCursor(5, y);
      M5.Lcd.print("Configure KVM pins:");
      y += 16;
      
      for (int i = 0; i < pins.size() && y < M5.Lcd.height() - 30; i++) {
        M5.Lcd.setCursor(5, y);
        if (i + menuStartPosition == selectedMenuItem) {
          M5.Lcd.fillRect(0, y-1, M5.Lcd.width(), 12, BLUE);
          M5.Lcd.setTextColor(WHITE);
        }
        M5.Lcd.print(pins[i].name);
        M5.Lcd.print(" (Pin ");
        M5.Lcd.print(pins[i].pin);
        M5.Lcd.print(")");
        y += 16;
        M5.Lcd.setTextColor(WHITE);
      }
      
      // Добавляем дополнительные пункты меню
      if (y < M5.Lcd.height() - 30) {
        M5.Lcd.setCursor(5, y);
        if (pins.size() + menuStartPosition == selectedMenuItem) {
          M5.Lcd.fillRect(0, y-1, M5.Lcd.width(), 12, BLUE);
          M5.Lcd.setTextColor(WHITE);
        }
        M5.Lcd.print("Connection Check: ");
        
        switch (kvmModule.getCheckInterval()) {
          case CHECK_OFF:    M5.Lcd.print("OFF"); break;
          case CHECK_10SEC:  M5.Lcd.print("10s"); break;
          case CHECK_30SEC:  M5.Lcd.print("30s"); break;
          case CHECK_1MIN:   M5.Lcd.print("1m"); break;
          case CHECK_5MIN:   M5.Lcd.print("5m"); break;
          case CHECK_30MIN:  M5.Lcd.print("30m"); break;
        }
        
        y += 16;
        M5.Lcd.setTextColor(WHITE);
      }
      
      if (y < M5.Lcd.height() - 15) {
        M5.Lcd.setCursor(5, y);
        if (pins.size() + 1 + menuStartPosition == selectedMenuItem) {
          M5.Lcd.fillRect(0, y-1, M5.Lcd.width(), 12, BLUE);
          M5.Lcd.setTextColor(WHITE);
        }
        M5.Lcd.print("Use DHCP: ");
        M5.Lcd.print(kvmModule.getUseDHCP() ? "YES" : "NO");
        y += 16;
        M5.Lcd.setTextColor(WHITE);
      }
      
      if (y < M5.Lcd.height()) {
        M5.Lcd.setCursor(5, y);
        if (pins.size() + 2 + menuStartPosition == selectedMenuItem) {
          M5.Lcd.fillRect(0, y-1, M5.Lcd.width(), 12, BLUE);
          M5.Lcd.setTextColor(WHITE);
        }
        M5.Lcd.print("Back to Main Menu");
        M5.Lcd.setTextColor(WHITE);
      }
      break;
    }
      
    case MENU_IR_CONTROL: {
      // Заглушка для ИК-управления
      M5.Lcd.setCursor(5, y);
      M5.Lcd.print("IR Control - Coming Soon");
      y += 16;
      M5.Lcd.setCursor(5, y);
      M5.Lcd.print("This feature is not");
      y += 16;
      M5.Lcd.setCursor(5, y);
      M5.Lcd.print("implemented yet.");
      y += 32;
      M5.Lcd.setCursor(5, y);
      M5.Lcd.print("Press A to return");
      break;
    }
  }
  
  // Отображение подсказок для кнопок внизу экрана
  M5.Lcd.drawLine(0, M5.Lcd.height() - 15, M5.Lcd.width(), M5.Lcd.height() - 15, WHITE);
  M5.Lcd.setCursor(5, M5.Lcd.height() - 12);
  M5.Lcd.setTextSize(1);
  M5.Lcd.print("A:Select  B:Down  C:Up");
}

// Обработка выбора пункта меню
void handleMenuAction() {
  switch (currentSection) {
    case MENU_MAIN:
      if (selectedMenuItem >= 0 && selectedMenuItem < MAIN_MENU_ITEMS_COUNT) {
        currentSection = mainMenuItems[selectedMenuItem].section;
        selectedMenuItem = 0;
        menuStartPosition = 0;
        
        // Дополнительные действия при переходе в раздел
        if (currentSection == MENU_WIFI_SCAN) {
          scanWiFiNetworks();
        }
      }
      break;
      
    case MENU_AP_OPTIONS:
      if (selectedMenuItem >= 0 && selectedMenuItem <= 4) {
        // Изменение режима AP
        apConfig.mode = (APMode)selectedMenuItem;
        updateAccessPointMode();
      } else if (selectedMenuItem == 5) {
        // Настройка SSID и пароля - пока просто перейдём в режим редактирования
        // В реальной реализации здесь будет экран с вводом данных
        M5.Lcd.fillScreen(BLACK);
        M5.Lcd.setCursor(0, 0);
        M5.Lcd.println("AP Settings");
        M5.Lcd.println("-----------------");
        M5.Lcd.print("SSID: ");
        M5.Lcd.println(apConfig.ssid);
        M5.Lcd.print("Pass: ");
        M5.Lcd.println(apConfig.password);
        M5.Lcd.println("\nUse web interface to change");
        M5.Lcd.println("these settings");
        
        delay(3000); // Пауза для чтения
      } else if (selectedMenuItem == 6) {
        // Возврат в главное меню
        currentSection = MENU_MAIN;
        selectedMenuItem = 0;
        menuStartPosition = 0;
      }
      break;
      
    case MENU_WIFI_SCAN:
      if (networks.size() > 0 && selectedMenuItem >= 0 && selectedMenuItem < networks.size()) {
        // Показываем подробную информацию о выбранной сети
        M5.Lcd.fillScreen(BLACK);
        M5.Lcd.setCursor(0, 0);
        M5.Lcd.println("Network Details");
        M5.Lcd.println("-----------------");
        
        M5.Lcd.print("SSID: ");
        M5.Lcd.println(networks[selectedMenuItem].ssid);
        
        M5.Lcd.print("Signal: ");
        M5.Lcd.print(networks[selectedMenuItem].rssi);
        M5.Lcd.println(" dBm");
        
        M5.Lcd.print("Channel: ");
        M5.Lcd.println(networks[selectedMenuItem].channel);
        
        M5.Lcd.print("Security: ");
        switch (networks[selectedMenuItem].encryptionType) {
          case WIFI_AUTH_OPEN:
            M5.Lcd.println("Open");
            break;
          case WIFI_AUTH_WEP:
            M5.Lcd.println("WEP");
            break;
          case WIFI_AUTH_WPA_PSK:
            M5.Lcd.println("WPA-PSK");
            break;
          case WIFI_AUTH_WPA2_PSK:
            M5.Lcd.println("WPA2-PSK");
            break;
          case WIFI_AUTH_WPA_WPA2_PSK:
            M5.Lcd.println("WPA/WPA2-PSK");
            break;
          default:
            M5.Lcd.println("Unknown");
        }
        
        M5.Lcd.println("\nPress any button to return");
        
        // Ждем нажатия любой кнопки для возврата
        bool buttonPressed = false;
        while (!buttonPressed) {
          M5.update();
          buttonPressed = M5.BtnA.wasPressed() || M5.BtnB.wasPressed() || M5.BtnC.wasPressed();
          delay(50);
        }
      } else {
        // Если нет сетей или пользователь нажал в пустом месте, запускаем сканирование
        scanWiFiNetworks();
      }
      break;
      
    case MENU_KVM_OPTIONS: {
      const auto& pins = kvmModule.getPins();
      if (selectedMenuItem >= 0 && selectedMenuItem < pins.size()) {
        // Управление пином KVM
        kvmModule.togglePin(selectedMenuItem);
      } else if (selectedMenuItem == pins.size()) {
        // Изменение интервала проверки соединения
        ConnectionCheckInterval current = kvmModule.getCheckInterval();
        ConnectionCheckInterval next = (ConnectionCheckInterval)(((int)current + 1) % 6);
        kvmModule.setCheckInterval(next);
      } else if (selectedMenuItem == pins.size() + 1) {
        // Переключение DHCP
        kvmModule.setUseDHCP(!kvmModule.getUseDHCP());
      } else if (selectedMenuItem == pins.size() + 2) {
        // Возврат в главное меню
        currentSection = MENU_MAIN;
        selectedMenuItem = 0;
        menuStartPosition = 0;
      }
      break;
    }
      
    case MENU_KVM_MONITOR:
      // Возврат в главное меню при нажатии на любом элементе
      currentSection = MENU_MAIN;
      selectedMenuItem = 0;
      menuStartPosition = 0;
      break;
      
    case MENU_IR_CONTROL:
      // Возврат в главное меню при нажатии на любом элементе
      currentSection = MENU_MAIN;
      selectedMenuItem = 0;
      menuStartPosition = 0;
      break;
  }
  
  // Перерисовываем меню после действия
  drawMenu();
}

// Сканирование WiFi сетей
void scanWiFiNetworks() {
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setCursor(0, 0);
  M5.Lcd.println("Scanning WiFi...");
  
  int scanCount = WiFi.scanNetworks();
  
  networks.clear();
  
  if (scanCount == 0) {
    M5.Lcd.println("No networks found");
  } else {
    M5.Lcd.print("Found ");
    M5.Lcd.print(scanCount);
    M5.Lcd.println(" networks");
    
    // Сохраняем результаты сканирования
    for (int i = 0; i < scanCount; i++) {
      WiFiResult network;
      network.ssid = WiFi.SSID(i);
      network.rssi = WiFi.RSSI(i);
      network.encryptionType = WiFi.encryptionType(i);
      network.channel = WiFi.channel(i);
      networks.push_back(network);
    }
    
    // Сортируем по уровню сигнала
    std::sort(networks.begin(), networks.end(), [](const WiFiResult& a, const WiFiResult& b) {
      return a.rssi > b.rssi;
    });
  }
  
  selectedMenuItem = 0;
  menuStartPosition = 0;
  
  // Небольшая задержка для чтения
  delay(1000);
  
  // Перерисовываем меню с результатами
  drawMenu();
}

// Выполнение диагностики сети
void performNetworkDiagnostics() {
  // Здесь можно добавить дополнительные проверки сети
  // Пока просто обновляем информацию, которая уже собирается
  deviceManager.updateNetworkInfo();
}

// Сохранение конфигурации в LittleFS
void saveConfiguration() {
  DynamicJsonDocument doc(4096);
  
  // Сохраняем настройки WiFi
  JsonObject apObj = doc.createNestedObject("ap");
  apObj["mode"] = apConfig.mode;
  apObj["ssid"] = apConfig.ssid;
  apObj["password"] = apConfig.password;
  apObj["hidden"] = apConfig.hidden;
  apObj["channel"] = apConfig.channel;
  
  // Открываем файл для записи
  File configFile = LittleFS.open("/config.json", "w");
  if (!configFile) {
    return;
  }
  
  // Записываем JSON в файл
  serializeJson(doc, configFile);
  configFile.close();
}

// Загрузка конфигурации из LittleFS
void loadConfiguration() {
  // Проверяем, существует ли файл конфигурации
  if (!LittleFS.exists("/config.json")) {
    return;
  }
  
  // Открываем файл для чтения
  File configFile = LittleFS.open("/config.json", "r");
  if (!configFile) {
    return;
  }
  
  // Разбираем JSON
  DynamicJsonDocument doc(4096);
  DeserializationError error = deserializeJson(doc, configFile);
  configFile.close();
  
  if (error) {
    return;
  }
  
  // Загружаем настройки AP
  if (doc.containsKey("ap")) {
    JsonObject apObj = doc["ap"];
    apConfig.mode = (APMode)apObj["mode"].as<int>();
    apConfig.ssid = apObj["ssid"].as<String>();
    apConfig.password = apObj["password"].as<String>();
    apConfig.hidden = apObj["hidden"].as<bool>();
    apConfig.channel = apObj["channel"].as<int>();
  }
}