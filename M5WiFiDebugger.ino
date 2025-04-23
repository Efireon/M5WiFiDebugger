#include <M5StickCPlus2.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <vector>

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
  MENU_IR_CONTROL, // ИК-управление (заглушка)
  MENU_DEVICE_SETTINGS // Настройки устройства
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

// Структура настроек устройства
struct DeviceSettings {
  uint8_t brightness;       // Яркость экрана (0-100%)
  uint16_t sleepTimeout;    // Время до перехода в спящий режим (в секундах, 0 - отключено)
  String deviceId;          // Идентификатор устройства
  bool rotateDisplay;       // Поворот экрана
  uint8_t volume;           // Громкость (0-100%)
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
              M5.Speaker.tone(8000, 200); // Высокий сигнал для HIGH
            } else {
              M5.Speaker.tone(1000, 200); // Низкий сигнал для LOW
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
DeviceSettings deviceSettings = {80, 300, "M5WifiDebugger", false, 70}; // Настройки устройства по умолчанию

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
  {"IR Control", MENU_IR_CONTROL},
  {"Device Settings", MENU_DEVICE_SETTINGS}
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

// Пункты подменю Device Settings
const char* deviceSettingsItems[] = {
  "Brightness",
  "Sleep Timeout",
  "Device ID",
  "Display Rotation",
  "Volume",
  "Back to Main Menu"
};
#define DEVICE_SETTINGS_ITEMS_COUNT (sizeof(deviceSettingsItems) / sizeof(char*))

// Буфер для сообщений на дисплее
char displayBuffer[128];

// Переменные для обработки кнопок
unsigned long buttonALastPress = 0;
unsigned long buttonBLastPress = 0;
bool buttonALongPress = false;
bool buttonBLongPress = false;
bool isScanningWifi = false;
bool scanResultsReady = false;
unsigned long lastScanTime = 0;

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
void saveDeviceSettings();
void loadDeviceSettings();
void playFindMeSound();
int getMaxMenuItems();

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
  loadDeviceSettings();
  
  // Применяем настройки устройства
  M5.Lcd.setBrightness(deviceSettings.brightness);
  
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
  checkScanResults();
  
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
  M5.Lcd.setRotation(deviceSettings.rotateDisplay ? 1 : 3);  // Ориентация в зависимости от настроек
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
    if (LittleFS.exists("/index.html")) {
      request->send(LittleFS, "/index.html", "text/html");
      Serial.println("Отправка index.html из LittleFS");
    } else {
      Serial.println("Файл index.html не найден в LittleFS!");
      // Если файл не найден, отправляем сообщение об ошибке
      request->send(200, "text/html", "<html><body><h1>Ошибка!</h1><p>Файл index.html не найден в файловой системе.</p><p>Загрузите веб-интерфейс через инструмент LittleFS Data Upload в Arduino IDE.</p></body></html>");
    }
  });
  
  // Добавим маршрут для обслуживания других статических файлов из LittleFS
  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

  // Остальные маршруты серверa без изменений...
  
  // Маршрут для сканирования сетей
  server.on("/scan-start", HTTP_GET, [](AsyncWebServerRequest *request){
    if (isScanningWifi) {
      request->send(429, "application/json", "{\"status\":\"scanning\",\"message\":\"Сканирование уже выполняется\"}");
      return;
    }
    
    if (scanResultsReady) {
      request->send(200, "application/json", "{\"status\":\"ready\",\"message\":\"Результаты сканирования уже доступны\"}");
    } else {
      startWiFiScanAsync();
      request->send(202, "application/json", "{\"status\":\"started\",\"message\":\"Сканирование запущено\"}");
    }
  });
  
  // Маршрут для проверки статуса сканирования
  server.on("/scan-status", HTTP_GET, [](AsyncWebServerRequest *request){
    if (isScanningWifi) {
      request->send(200, "application/json", "{\"status\":\"scanning\",\"message\":\"Сканирование выполняется\"}");
    } else if (scanResultsReady) {
      request->send(200, "application/json", 
                   "{\"status\":\"ready\",\"message\":\"Результаты сканирования доступны\",\"count\":" + 
                   String(networks.size()) + "}");
    } else {
      request->send(200, "application/json", "{\"status\":\"idle\",\"message\":\"Сканирование не выполнялось\"}");
    }
  });
  
  // Маршрут для получения части результатов сканирования (с пагинацией)
  server.on("/scan-results", HTTP_GET, [](AsyncWebServerRequest *request){
    // Проверяем готовность результатов
    if (!scanResultsReady) {
      request->send(404, "application/json", "{\"error\":\"Результаты сканирования не доступны\"}");
      return;
    }
    
    // Получаем параметры пагинации
    int page = 0;
    int pageSize = 5;
    
    if (request->hasParam("page")) {
      page = request->getParam("page")->value().toInt();
    }
    
    if (request->hasParam("size")) {
      pageSize = request->getParam("size")->value().toInt();
      // Ограничиваем максимальный размер страницы
      if (pageSize > 10) pageSize = 10;
    }
    
    // Вычисляем начальный и конечный индексы
    int startIndex = page * pageSize;
    int endIndex = min(startIndex + pageSize, (int)networks.size());
    
    // Если начальный индекс за пределами массива
    if (startIndex >= networks.size()) {
      request->send(404, "application/json", "{\"error\":\"Страница не найдена\"}");
      return;
    }
    
    // Создаем документ JSON с меньшим размером буфера
    DynamicJsonDocument doc(1024);
    doc["page"] = page;
    doc["pageSize"] = pageSize;
    doc["totalNetworks"] = networks.size();
    doc["totalPages"] = (networks.size() + pageSize - 1) / pageSize;
    
    JsonArray networksArray = doc.createNestedArray("networks");
    
    // Добавляем сети только для текущей страницы
    for (int i = startIndex; i < endIndex; i++) {
      const auto& network = networks[i];
      JsonObject netObj = networksArray.createNestedObject();
      netObj["ssid"] = network.ssid;
      netObj["rssi"] = network.rssi;
      
      // Определяем тип шифрования
      String encType;
      switch (network.encryptionType) {
        case WIFI_AUTH_OPEN: encType = "Open"; break;
        case WIFI_AUTH_WEP: encType = "WEP"; break;
        case WIFI_AUTH_WPA_PSK: encType = "WPA-PSK"; break;
        case WIFI_AUTH_WPA2_PSK: encType = "WPA2-PSK"; break;
        case WIFI_AUTH_WPA_WPA2_PSK: encType = "WPA/WPA2-PSK"; break;
        case WIFI_AUTH_WPA2_ENTERPRISE: encType = "WPA2-Enterprise"; break;
        default: encType = "Unknown";
      }
      netObj["encryption"] = encType;
      netObj["channel"] = network.channel;
    }
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });
  
  // Дополнительный маршрут для поддержки старого API (для совместимости)
  server.on("/scan", HTTP_GET, [](AsyncWebServerRequest *request){
    if (!scanResultsReady && !isScanningWifi) {
      // Если сканирование еще не запущено, запускаем его
      startWiFiScanAsync();
      request->send(202, "application/json", "{\"status\":\"started\",\"message\":\"Сканирование запущено, запросите результаты позже через /scan-results\"}");
      return;
    }
    
    if (isScanningWifi) {
      // Если сканирование в процессе
      request->send(409, "application/json", "{\"status\":\"scanning\",\"message\":\"Сканирование выполняется, попробуйте позже\"}");
      return;
    }
    
    // Если результаты готовы, перенаправляем на первую страницу результатов
    request->redirect("/scan-results?page=0&size=10");
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
  
  // Маршрут для получения настроек устройства
  server.on("/device", HTTP_GET, [](AsyncWebServerRequest *request){
    DynamicJsonDocument doc(512);
    doc["brightness"] = deviceSettings.brightness;
    doc["sleepTimeout"] = deviceSettings.sleepTimeout;
    doc["deviceId"] = deviceSettings.deviceId;
    doc["rotateDisplay"] = deviceSettings.rotateDisplay;
    doc["volume"] = deviceSettings.volume;
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });
  
  // Маршрут для изменения настроек устройства
  server.on("/device/settings", HTTP_POST, [](AsyncWebServerRequest *request){
    if (request->hasParam("brightness", true)) {
      int brightness = request->getParam("brightness", true)->value().toInt();
      if (brightness >= 0 && brightness <= 100) {
        deviceSettings.brightness = brightness;
        M5.Lcd.setBrightness(brightness);
      }
    }
    
    if (request->hasParam("sleepTimeout", true)) {
      int sleepTimeout = request->getParam("sleepTimeout", true)->value().toInt();
      if (sleepTimeout >= 0) {
        deviceSettings.sleepTimeout = sleepTimeout;
      }
    }
    
    if (request->hasParam("deviceId", true)) {
      deviceSettings.deviceId = request->getParam("deviceId", true)->value();
    }
    
    if (request->hasParam("rotateDisplay", true)) {
      bool rotateDisplay = (request->getParam("rotateDisplay", true)->value() == "true" || 
                          request->getParam("rotateDisplay", true)->value() == "1");
      if (deviceSettings.rotateDisplay != rotateDisplay) {
        deviceSettings.rotateDisplay = rotateDisplay;
        M5.Lcd.setRotation(rotateDisplay ? 1 : 3);
        drawMenu(); // Перерисовываем меню при изменении ориентации
      }
    }
    
    if (request->hasParam("volume", true)) {
      int volume = request->getParam("volume", true)->value().toInt();
      if (volume >= 0 && volume <= 100) {
        deviceSettings.volume = volume;
      }
    }
    
    // Сохраняем настройки
    saveDeviceSettings();
    
    request->send(200, "text/plain", "Device settings updated");
  });
  
  // Маршрут для сетевых инструментов
  server.on("/network", HTTP_GET, [](AsyncWebServerRequest *request){
    // HTML страница с сетевыми инструментами
    String html = "<html><head><title>Network Tools</title></head><body>";
    html += "<h1>Network Tools</h1>";
    
    // Ping tool
    html += "<h2>Ping Tool</h2>";
    html += "<form action='/network/ping' method='post'>";
    html += "<label for='host'>Host or IP:</label>";
    html += "<input type='text' id='host' name='host' required>";
    html += "<button type='submit'>Ping</button>";
    html += "</form>";
    
    // IP Scanner
    html += "<h2>IP Scanner</h2>";
    html += "<form action='/network/scan' method='post'>";
    html += "<label for='range'>IP Range (e.g. 192.168.1.1-192.168.1.254):</label>";
    html += "<input type='text' id='range' name='range' required>";
    html += "<button type='submit'>Scan</button>";
    html += "</form>";
    
    // Port Scanner
    html += "<h2>Port Scanner</h2>";
    html += "<form action='/network/portscan' method='post'>";
    html += "<label for='ip'>IP Address:</label>";
    html += "<input type='text' id='ip' name='ip' required><br>";
    html += "<label for='startPort'>Start Port:</label>";
    html += "<input type='number' id='startPort' name='startPort' value='1'><br>";
    html += "<label for='endPort'>End Port:</label>";
    html += "<input type='number' id='endPort' name='endPort' value='1024'><br>";
    html += "<button type='submit'>Scan Ports</button>";
    html += "</form>";
    
    // IP Blocking (only for AP mode)
    html += "<h2>IP Blocking</h2>";
    if (apConfig.mode != AP_MODE_OFF) {
      html += "<form action='/network/block' method='post'>";
      html += "<label for='blockIP'>IP to Block:</label>";
      html += "<input type='text' id='blockIP' name='ip' required>";
      html += "<button type='submit'>Block IP</button>";
      html += "</form>";
      
      // Список заблокированных IP
      html += "<h3>Blocked IPs</h3>";
      html += "<div id='blockedIPs'>";
      const auto& blockedIPs = networkTools.getBlockedIPs();
      if (blockedIPs.size() > 0) {
        html += "<ul>";
        for (const auto& ip : blockedIPs) {
          html += "<li>" + ip + " <a href='/network/unblock?ip=" + ip + "'>Unblock</a></li>";
        }
        html += "</ul>";
      } else {
        html += "<p>No blocked IPs</p>";
      }
      html += "</div>";
    } else {
      html += "<p>IP Blocking is only available in AP mode</p>";
    }
    
    html += "</body></html>";
    request->send(200, "text/html", html);
  });
  
  // Маршрут для Device Settings
  server.on("/device", HTTP_GET, [](AsyncWebServerRequest *request){
    // HTML страница с настройками устройства
    String html = "<html><head><title>Device Settings</title></head><body>";
    html += "<h1>Device Settings</h1>";
    
    html += "<form action='/device/settings' method='post'>";
    
    // Brightness
    html += "<div>";
    html += "<label for='brightness'>Brightness (0-100%):</label>";
    html += "<input type='number' id='brightness' name='brightness' min='0' max='100' value='" + String(deviceSettings.brightness) + "'>";
    html += "</div><br>";
    
    // Sleep Timeout
    html += "<div>";
    html += "<label for='sleepTimeout'>Sleep Timeout (seconds, 0 = disabled):</label>";
    html += "<input type='number' id='sleepTimeout' name='sleepTimeout' min='0' value='" + String(deviceSettings.sleepTimeout) + "'>";
    html += "</div><br>";
    
    // Device ID
    html += "<div>";
    html += "<label for='deviceId'>Device ID:</label>";
    html += "<input type='text' id='deviceId' name='deviceId' value='" + deviceSettings.deviceId + "'>";
    html += "</div><br>";
    
    // Rotate Display
    html += "<div>";
    html += "<label for='rotateDisplay'>Rotate Display:</label>";
    html += "<input type='checkbox' id='rotateDisplay' name='rotateDisplay' value='1' " + String(deviceSettings.rotateDisplay ? "checked" : "") + ">";
    html += "</div><br>";
    
    // Volume
    html += "<div>";
    html += "<label for='volume'>Volume (0-100%):</label>";
    html += "<input type='number' id='volume' name='volume' min='0' max='100' value='" + String(deviceSettings.volume) + "'>";
    html += "</div><br>";
    
    html += "<button type='submit'>Save Settings</button>";
    html += "</form>";
    
    // Hardware Info
    html += "<h2>Hardware Information</h2>";
    html += "<ul>";
    html += "<li>Model: M5StickC Plus2</li>";
    html += "<li>Battery: " + String(M5.Power.getBatteryVoltage() / 1000.0f, 2) + "V</li>";
    html += "<li>MAC Address: " + WiFi.macAddress() + "</li>";
    html += "</ul>";
    
    html += "</body></html>";
    request->send(200, "text/html", html);
  });
  
  // Запуск веб-сервера
  server.begin();
}

// Функция воспроизведения сигнала "Find Me"
void playFindMeSound() {
  // Воспроизводим звуковой сигнал прерывисто с учетом настройки громкости
  int toneVolume = map(deviceSettings.volume, 0, 100, 0, 255);
  for (int i = 0; i < 5; i++) {
    M5.Speaker.setVolume(toneVolume);
    M5.Speaker.tone(2000, 300);
    delay(300);
    M5.Speaker.tone(1500, 300);
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
  
  // Кнопка B
  if (M5.BtnB.isPressed()) {
    if (buttonBLastPress == 0) {
      buttonBLastPress = millis();
    } else if (!buttonBLongPress && millis() - buttonBLastPress > 1000) {
      // Долгое нажатие на B (1 секунда) - перемещение вверх
      buttonBLongPress = true;
      
      // Получаем максимальное количество элементов для цикличного перехода
      int maxItems = getMaxMenuItems();
      
      // Цикличное перемещение вверх 
      selectedMenuItem = (selectedMenuItem > 0) ? selectedMenuItem - 1 : maxItems - 1;
      
      // Корректируем позицию прокрутки если нужно
      int displayLines = M5.Lcd.height() / 16;
      if (selectedMenuItem < menuStartPosition) {
        menuStartPosition = selectedMenuItem;
      } else if (selectedMenuItem >= menuStartPosition + displayLines) {
        menuStartPosition = selectedMenuItem - displayLines + 1;
      }
      
      drawMenu();
    }
  } else {
    if (buttonBLastPress > 0) {
      if (!buttonBLongPress) {
        // Короткое нажатие на B - перемещение вниз
        
        // Получаем максимальное количество элементов 
        int maxItems = getMaxMenuItems();
        
        // Цикличное перемещение вниз
        selectedMenuItem = (selectedMenuItem < maxItems - 1) ? selectedMenuItem + 1 : 0;
        
        // Корректируем позицию прокрутки если нужно
        int displayLines = M5.Lcd.height() / 16;
        if (selectedMenuItem < menuStartPosition) {
          menuStartPosition = selectedMenuItem;
        } else if (selectedMenuItem >= menuStartPosition + displayLines) {
          menuStartPosition = selectedMenuItem - displayLines + 1;
        }
        
        drawMenu();
      }
      buttonBLastPress = 0;
      buttonBLongPress = false;
    }
  }
}

// Функция для получения максимального количества элементов в текущем меню
int getMaxMenuItems() {
  int maxItems = 0;
  
  switch (currentSection) {
    case MENU_MAIN:
      maxItems = MAIN_MENU_ITEMS_COUNT;
      break;
      
    case MENU_AP_OPTIONS:
      maxItems = AP_OPTIONS_ITEMS_COUNT;
      break;
      
    case MENU_WIFI_SCAN:
      // Динамическое количество - количество найденных сетей 
      // (или минимум 1, если список пуст)
      maxItems = networks.size() > 0 ? networks.size() : 1;
      break;
      
    case MENU_KVM_OPTIONS: {
      // Динамическое количество - все пины + 3 дополнительных пункта
      const auto& pins = kvmModule.getPins();
      maxItems = pins.size() + 3; // Пины + настройки + DHCP + возврат
      break;
    }
      
    case MENU_KVM_MONITOR:
      // В режиме мониторинга можно прокручивать список пинов
      maxItems = kvmModule.getPins().size() + 3; // Пины + информация о сети + заголовок
      break;
      
    case MENU_IR_CONTROL:
      // Базовое количество элементов для IR Control
      maxItems = 5;
      break;
      
    case MENU_DEVICE_SETTINGS:
      // Пункты настроек устройства
      maxItems = DEVICE_SETTINGS_ITEMS_COUNT;
      break;
      
    default:
      // По умолчанию даем возможность прокрутки
      maxItems = 10;
  }
  
  return maxItems;
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
    case MENU_DEVICE_SETTINGS:
      M5.Lcd.println("DEVICE SETTINGS       ");
      break;
  }
  
  M5.Lcd.setCursor(M5.Lcd.width() - 70, 0);
  M5.Lcd.print(batteryBuf);
  M5.Lcd.drawLine(0, 10, M5.Lcd.width(), 10, WHITE);
  
  // Отображение элементов меню в зависимости от раздела
  int y = 15;
  int displayLines = (M5.Lcd.height() - 15) / 16; // Расчет количества строк на экране
  
  // Получаем максимальное количество элементов в текущем меню
  int maxItems = getMaxMenuItems();
  
  // Убедимся, что menuStartPosition в пределах допустимого диапазона
  if (menuStartPosition > maxItems - displayLines) {
    menuStartPosition = max(0, maxItems - displayLines);
  }
  
  // Убедимся, что selectedMenuItem в пределах допустимого диапазона
  if (selectedMenuItem >= maxItems) {
    selectedMenuItem = maxItems - 1;
  }
  if (selectedMenuItem < 0) {
    selectedMenuItem = 0;
  }
  
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
      
      // Отображаем информацию о сети только если не в режиме прокрутки
      if (menuStartPosition == 0) {
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
      }
      
      // Отображаем состояние пинов с учетом прокрутки
      const auto& kvmPins = kvmModule.getPins();
      
      // Вычисляем смещение для отображения пинов
      int pinOffset = (menuStartPosition > 0) ? menuStartPosition - 3 : 0;
      if (pinOffset < 0) pinOffset = 0;
      
      // Отображаем пины с учетом смещения
      for (int i = pinOffset; i < kvmPins.size() && y < M5.Lcd.height(); i++) {
        M5.Lcd.setCursor(5, y);
        // Вычисляем соответствующий индекс для выделения
        int itemIndex = i + (menuStartPosition > 0 ? 3 : 0);
        
        if (itemIndex == selectedMenuItem) {
          M5.Lcd.fillRect(0, y-1, M5.Lcd.width(), 12, BLUE);
          M5.Lcd.setTextColor(WHITE);
        }
        
        M5.Lcd.print(kvmPins[i].name);
        M5.Lcd.print(": ");
        if (kvmPins[i].state) {
          M5.Lcd.print("ON");
        } else {
          M5.Lcd.print("OFF");
        }
        y += 16;
        M5.Lcd.setTextColor(WHITE);
      }
      break;
    }
      
    case MENU_KVM_OPTIONS: {
      // Меню настроек KVM
      const auto& pins = kvmModule.getPins();
      
      // Отображаем заголовок только если не в режиме прокрутки
      if (menuStartPosition == 0) {
        M5.Lcd.setCursor(5, y);
        M5.Lcd.print("Configure KVM pins:");
        y += 16;
      }
      
      // Вычисляем смещение для отображения пинов
      int pinOffset = (menuStartPosition > 0) ? menuStartPosition - 1 : 0;
      if (pinOffset < 0) pinOffset = 0;
      
      // Отображаем пины для настройки
      for (int i = pinOffset; i < pins.size() && y < M5.Lcd.height() - 32; i++) {
        M5.Lcd.setCursor(5, y);
        // Вычисляем соответствующий индекс для выделения
        int itemIndex = i + (menuStartPosition > 0 ? 1 : 0);
        
        if (itemIndex == selectedMenuItem) {
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
      
      // Отображаем дополнительные пункты меню
      int extraItemsStart = pins.size();
      
      // Connection Check
      if (extraItemsStart <= menuStartPosition + displayLines && y < M5.Lcd.height() - 16) {
        M5.Lcd.setCursor(5, y);
        if (extraItemsStart == selectedMenuItem) {
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
      
      // DHCP
      if (extraItemsStart + 1 <= menuStartPosition + displayLines && y < M5.Lcd.height() - 16) {
        M5.Lcd.setCursor(5, y);
        if (extraItemsStart + 1 == selectedMenuItem) {
          M5.Lcd.fillRect(0, y-1, M5.Lcd.width(), 12, BLUE);
          M5.Lcd.setTextColor(WHITE);
        }
        
        M5.Lcd.print("Use DHCP: ");
        M5.Lcd.print(kvmModule.getUseDHCP() ? "YES" : "NO");
        y += 16;
        M5.Lcd.setTextColor(WHITE);
      }
      
      // Back to Main Menu
      if (extraItemsStart + 2 <= menuStartPosition + displayLines && y < M5.Lcd.height()) {
        M5.Lcd.setCursor(5, y);
        if (extraItemsStart + 2 == selectedMenuItem) {
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
      
    case MENU_DEVICE_SETTINGS: {
      // Меню настроек устройства
      for (int i = menuStartPosition; i < DEVICE_SETTINGS_ITEMS_COUNT && i < menuStartPosition + displayLines; i++) {
        M5.Lcd.setCursor(5, y);
        if (i == selectedMenuItem) {
          M5.Lcd.fillRect(0, y-1, M5.Lcd.width(), 12, BLUE);
          M5.Lcd.setTextColor(WHITE);
        } else {
          M5.Lcd.setTextColor(WHITE);
        }
        
        // Вывод название пункта и текущего значения
        M5.Lcd.print(deviceSettingsItems[i]);
        
        // Отображаем текущее значение параметра
        switch (i) {
          case 0: // Brightness
            M5.Lcd.setCursor(100, y);
            M5.Lcd.print(deviceSettings.brightness);
            M5.Lcd.print("%");
            break;
          case 1: // Sleep Timeout
            M5.Lcd.setCursor(100, y);
            if (deviceSettings.sleepTimeout == 0) {
              M5.Lcd.print("Off");
            } else {
              M5.Lcd.print(deviceSettings.sleepTimeout);
              M5.Lcd.print("s");
            }
            break;
          case 2: // Device ID
            if (deviceSettings.deviceId.length() > 10) {
              M5.Lcd.setCursor(100, y);
              M5.Lcd.print(deviceSettings.deviceId.substring(0, 10));
              M5.Lcd.print("...");
            } else if (deviceSettings.deviceId.length() > 0) {
              M5.Lcd.setCursor(100, y);
              M5.Lcd.print(deviceSettings.deviceId);
            }
            break;
          case 3: // Display Rotation
            M5.Lcd.setCursor(100, y);
            M5.Lcd.print(deviceSettings.rotateDisplay ? "On" : "Off");
            break;
          case 4: // Volume
            M5.Lcd.setCursor(100, y);
            M5.Lcd.print(deviceSettings.volume);
            M5.Lcd.print("%");
            break;
        }
        
        y += 16;
        M5.Lcd.setTextColor(WHITE);
      }
      break;
    }
  }
  
  // Отображение подсказок для кнопок внизу экрана
  M5.Lcd.drawLine(0, M5.Lcd.height() - 15, M5.Lcd.width(), M5.Lcd.height() - 15, WHITE);
  M5.Lcd.setCursor(5, M5.Lcd.height() - 12);
  M5.Lcd.setTextSize(1);
  M5.Lcd.print("A:Select  B:Down  Hold B:Up");
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
          buttonPressed = M5.BtnA.wasPressed() || M5.BtnB.wasPressed();
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
      // В режиме мониторинга кнопка A отвечает за переключение состояния пина
      {
        const auto& kvmPins = kvmModule.getPins();
        // Вычисляем реальный индекс пина с учетом прокрутки
        int pinOffset = (menuStartPosition > 0) ? menuStartPosition - 3 : 0;
        if (pinOffset < 0) pinOffset = 0;
        int pinIndex = selectedMenuItem - (menuStartPosition > 0 ? 3 : 0) + pinOffset;
        
        if (pinIndex >= 0 && pinIndex < kvmPins.size()) {
          // Переключаем пин
          kvmModule.togglePin(pinIndex);
        } else {
          // Если нажатие не на пин, то возвращаемся в главное меню
          currentSection = MENU_MAIN;
          selectedMenuItem = 0;
          menuStartPosition = 0;
        }
      }
      break;
      
    case MENU_IR_CONTROL:
      // Возврат в главное меню при нажатии на любом элементе
      currentSection = MENU_MAIN;
      selectedMenuItem = 0;
      menuStartPosition = 0;
      break;
      
    case MENU_DEVICE_SETTINGS:
      if (selectedMenuItem == 0) {
        // Brightness - изменение яркости по циклу
        // Изменяем яркость по шагам 20%
        deviceSettings.brightness = (deviceSettings.brightness + 20) % 120;
        if (deviceSettings.brightness > 100) deviceSettings.brightness = 20;
        M5.Lcd.setBrightness(deviceSettings.brightness);
        saveDeviceSettings();
      } else if (selectedMenuItem == 1) {
        // Sleep Timeout - изменение таймаута сна
        // Цикл значений: 0 (выкл), 30, 60, 120, 300, 600 (10 мин)
        int timeouts[] = {0, 30, 60, 120, 300, 600};
        int currentIndex = 0;
        
        // Находим текущий индекс
        for (int i = 0; i < 6; i++) {
          if (deviceSettings.sleepTimeout == timeouts[i]) {
            currentIndex = i;
            break;
          }
        }
        
        // Переходим к следующему значению
        currentIndex = (currentIndex + 1) % 6;
        deviceSettings.sleepTimeout = timeouts[currentIndex];
        saveDeviceSettings();
      } else if (selectedMenuItem == 2) {
        // Device ID - просмотр и копирование ID (нельзя изменить с экрана)
        M5.Lcd.fillScreen(BLACK);
        M5.Lcd.setCursor(0, 0);
        M5.Lcd.println("Device ID");
        M5.Lcd.println("-----------------");
        M5.Lcd.println(deviceSettings.deviceId);
        M5.Lcd.println("\nUse web interface to change");
        M5.Lcd.println("Device ID");
        
        delay(3000); // Пауза для чтения
      } else if (selectedMenuItem == 3) {
        // Display Rotation - переключение ориентации экрана
        deviceSettings.rotateDisplay = !deviceSettings.rotateDisplay;
        M5.Lcd.setRotation(deviceSettings.rotateDisplay ? 1 : 3);
        saveDeviceSettings();
      } else if (selectedMenuItem == 4) {
        // Volume - изменение громкости по циклу
        // Изменяем громкость по шагам 20%
        deviceSettings.volume = (deviceSettings.volume + 20) % 120;
        if (deviceSettings.volume > 100) deviceSettings.volume = 0;
        saveDeviceSettings();
        
        // Воспроизведём тестовый звук если громкость не нулевая
        if (deviceSettings.volume > 0) {
          M5.Speaker.setVolume(map(deviceSettings.volume, 0, 100, 0, 255));
          M5.Speaker.tone(1000, 100);
        }
      } else if (selectedMenuItem == 5) {
        // Back to Main Menu
        currentSection = MENU_MAIN;
        selectedMenuItem = 0;
        menuStartPosition = 0;
      }
      break;
  }
  
  // Перерисовываем меню после действия
  drawMenu();
}

void startWiFiScanAsync() {
  if (isScanningWifi) {
    Serial.println("Сканирование уже выполняется, пропускаем");
    return;
  }
  
  // Очищаем предыдущие результаты
  networks.clear();
  scanResultsReady = false;
  isScanningWifi = true;
  
  Serial.println("Запуск асинхронного сканирования WiFi...");
  WiFi.scanNetworks(true); // Асинхронное сканирование
  lastScanTime = millis();
}

void checkScanResults() {
  if (!isScanningWifi || scanResultsReady) {
    return; // Сканирование не запущено или результаты уже обработаны
  }
  
  int scanStatus = WiFi.scanComplete();
  
  // Таймаут сканирования (15 секунд)
  if (scanStatus == WIFI_SCAN_RUNNING && (millis() - lastScanTime > 15000)) {
    Serial.println("Таймаут сканирования WiFi");
    WiFi.scanDelete();
    isScanningWifi = false;
    return;
  }
  
  // Сканирование завершено
  if (scanStatus >= 0) {
    Serial.printf("Сканирование завершено, найдено %d сетей\n", scanStatus);
    
    // Ограничиваем количество сетей для предотвращения проблем с памятью
    int networksToProcess = min(scanStatus, 30);
    
    for (int i = 0; i < networksToProcess; i++) {
      // Даем возможность процессору обработать фоновые задачи
      if (i % 3 == 0) yield();
      
      WiFiResult network;
      network.ssid = WiFi.SSID(i);
      network.rssi = WiFi.RSSI(i);
      network.encryptionType = WiFi.encryptionType(i);
      network.channel = WiFi.channel(i);
      networks.push_back(network);
    }
    
    // Освобождаем память, выделенную для результатов сканирования
    WiFi.scanDelete();
    
    // Сортируем по уровню сигнала, если у нас есть сети
    if (!networks.empty()) {
      std::sort(networks.begin(), networks.end(), [](const WiFiResult& a, const WiFiResult& b) {
        return a.rssi > b.rssi;
      });
    }
    
    scanResultsReady = true;
    isScanningWifi = false;
    Serial.println("Результаты сканирования обработаны и готовы");
  }
}

bool scanWiFiNetworksForWeb() {
  // Проверяем, не выполняется ли уже сканирование
  if (isScanningWifi) {
    Serial.println("Сканирование WiFi уже выполняется, пропускаем");
    return false;
  }
  
  isScanningWifi = true;
  Serial.println("Начинаем сканирование WiFi сетей...");
  
  // Очищаем предыдущие результаты
  networks.clear();
  
  // Запускаем сканирование с таймаутом
  int scanCount = WiFi.scanNetworks(false, true, false, 300);  // false=не блокировать, true=показывать скрытые, 300ms на канал
  
  if (scanCount == WIFI_SCAN_RUNNING) {
    Serial.println("Сканирование запущено асинхронно");
    // Даем немного времени для начала сканирования, чтобы не перегружать CPU
    delay(100);
    
    // Ждем результата сканирования с таймаутом
    unsigned long startTime = millis();
    int numNetworks = WIFI_SCAN_RUNNING;
    
    while (numNetworks == WIFI_SCAN_RUNNING && (millis() - startTime < 10000)) {
      delay(100);  // Небольшая задержка между проверками
      numNetworks = WiFi.scanComplete();
      yield();  // Позволяет обработать фоновые задачи, чтобы избежать срабатывания WDT
    }
    
    scanCount = numNetworks;
  }
  
  // Проверяем результат сканирования
  if (scanCount < 0) {
    Serial.println("Ошибка сканирования WiFi: " + String(scanCount));
    isScanningWifi = false;
    return false;
  }
  
  Serial.print("Найдено сетей: ");
  Serial.println(scanCount);
  
  // Ограничиваем количество сетей для предотвращения проблем с памятью
  const int MAX_NETWORKS = 20;
  int networksToProcess = min(scanCount, MAX_NETWORKS);
  
  // Сохраняем результаты сканирования
  for (int i = 0; i < networksToProcess; i++) {
    // Даем возможность процессору обработать фоновые задачи
    if (i % 5 == 0) {
      yield();
    }
    
    WiFiResult network;
    network.ssid = WiFi.SSID(i);
    network.rssi = WiFi.RSSI(i);
    network.encryptionType = WiFi.encryptionType(i);
    network.channel = WiFi.channel(i);
    networks.push_back(network);
    
    Serial.print(i + 1);
    Serial.print(": ");
    Serial.print(network.ssid);
    Serial.print(" (");
    Serial.print(network.rssi);
    Serial.println(" dBm)");
  }
  
  // Освобождаем память, выделенную для результатов сканирования
  WiFi.scanDelete();
  
  // Сортируем по уровню сигнала, если у нас есть сети
  if (!networks.empty()) {
    std::sort(networks.begin(), networks.end(), [](const WiFiResult& a, const WiFiResult& b) {
      return a.rssi > b.rssi;
    });
  }
  
  Serial.println("Сканирование WiFi завершено");
  isScanningWifi = false;
  return true;
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

// Сохранение настроек устройства в LittleFS
void saveDeviceSettings() {
  DynamicJsonDocument doc(1024);
  
  // Сохраняем настройки устройства
  doc["brightness"] = deviceSettings.brightness;
  doc["sleepTimeout"] = deviceSettings.sleepTimeout;
  doc["deviceId"] = deviceSettings.deviceId;
  doc["rotateDisplay"] = deviceSettings.rotateDisplay;
  doc["volume"] = deviceSettings.volume;
  
  // Открываем файл для записи
  File configFile = LittleFS.open("/device_settings.json", "w");
  if (!configFile) {
    return;
  }
  
  // Записываем JSON в файл
  serializeJson(doc, configFile);
  configFile.close();
}

// Загрузка настроек устройства из LittleFS
void loadDeviceSettings() {
  // Проверяем, существует ли файл конфигурации
  if (!LittleFS.exists("/device_settings.json")) {
    // Если файла нет, создаем его с настройками по умолчанию
    saveDeviceSettings();
    return;
  }
  
  // Открываем файл для чтения
  File configFile = LittleFS.open("/device_settings.json", "r");
  if (!configFile) {
    return;
  }
  
  // Разбираем JSON
  DynamicJsonDocument doc(1024);
  DeserializationError error = deserializeJson(doc, configFile);
  configFile.close();
  
  if (error) {
    return;
  }
  
  // Загружаем настройки устройства
  if (doc.containsKey("brightness")) {
    deviceSettings.brightness = doc["brightness"];
  }
  if (doc.containsKey("sleepTimeout")) {
    deviceSettings.sleepTimeout = doc["sleepTimeout"];
  }
  if (doc.containsKey("deviceId")) {
    deviceSettings.deviceId = doc["deviceId"].as<String>();
  }
  if (doc.containsKey("rotateDisplay")) {
    deviceSettings.rotateDisplay = doc["rotateDisplay"];
  }
  if (doc.containsKey("volume")) {
    deviceSettings.volume = doc["volume"];
  }
}