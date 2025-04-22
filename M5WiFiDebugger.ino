#include <M5StickCPlus2.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <ArduinoJson.h>
#include <LittleFS.h>

// Определение режимов работы
// Определение структуры для хранения результатов сканирования WiFi
struct WiFiResult {
    String ssid;
    int32_t rssi;
    uint8_t encryptionType;
    int32_t channel;
};

// Определение режимов работы
enum OperationMode {
  MODE_CLIENT,     // Обычный клиентский режим WiFi
  MODE_AP,         // Режим точки доступа
  MODE_SCAN,       // Режим сканирования WiFi
  MODE_DIAGNOSTIC  // Режим диагностики
};

// Конфигурация GPIO для KVM
struct PinConfig {
  int pin;
  String name;
  bool state;
};

// Глобальные переменные
OperationMode currentMode = MODE_CLIENT;
AsyncWebServer server(80);  // Веб-сервер на порту 80
WiFiManager wifiManager;
std::vector<PinConfig> kvmPins;  // Пины для KVM
String ssid = "";
String password = "";
bool isConfigMode = false;
bool buttonPressed = false;

// Параметры для сканирования WiFi
int scanCount = 0;
int selectedNetwork = 0;
std::vector<WiFiResult> networks;

// Буфер для сообщений на дисплее
char displayBuffer[128];

// Прототипы функций
void setupDisplay();
void setupWiFi();
void setupWebServer();
void handleButtons();
void scanWiFiNetworks();
void displayNetworkInfo();
void saveConfiguration();
void loadConfiguration();
void togglePin(int pinIndex);
void performNetworkDiagnostics();

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
  M5.Lcd.println("BtnA: Change Mode");
  M5.Lcd.println("BtnB: Action/Select");
}

void loop() {
  // Обновление состояния кнопок
  M5.update();
  handleButtons();
  
  // Действия в зависимости от текущего режима
  switch (currentMode) {
    case MODE_CLIENT:
      // Проверка статуса WiFi
      if (WiFi.status() == WL_CONNECTED) {
        sprintf(displayBuffer, "Connected to: %s\nIP: %s\nSignal: %ddBm",
                WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(),
                WiFi.RSSI());
      } else {
        sprintf(displayBuffer, "Disconnected\nPress BtnB to connect");
      }
      break;
      
    case MODE_AP:
      // Отображение информации о точке доступа
      sprintf(displayBuffer, "AP Mode\nSSID: M5StickDebug\nIP: %s",
              WiFi.softAPIP().toString().c_str());
      break;
      
    case MODE_SCAN:
      // Обновление списка сетей при нажатии кнопки
      if (buttonPressed) {
        scanWiFiNetworks();
        buttonPressed = false;
      }
      // Отображение результатов сканирования
      displayNetworkInfo();
      break;
      
    case MODE_DIAGNOSTIC:
      // Запуск диагностики при нажатии кнопки
      if (buttonPressed) {
        performNetworkDiagnostics();
        buttonPressed = false;
      }
      break;
  }
  
  // Обновление экрана каждые 1000 мс
  static unsigned long lastDisplayUpdate = 0;
  if (millis() - lastDisplayUpdate > 1000) {
    // Обновляем только часть экрана, где меняются данные
    M5.Lcd.fillRect(0, 20, M5.Lcd.width(), M5.Lcd.height() - 20, BLACK);
    M5.Lcd.setCursor(0, 20);
    M5.Lcd.println(displayBuffer);
    lastDisplayUpdate = millis();
  }
  
  // Небольшая задержка для стабильности
  delay(100);
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
    M5.Lcd.println("M5StickDebug");
    M5.Lcd.println("Visit: 192.168.4.1");
    isConfigMode = true;
  });
  
  // Проверка сохраненных настроек
  if (ssid.length() > 0 && password.length() > 0) {
    WiFi.begin(ssid.c_str(), password.c_str());
    
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
      WiFi.mode(WIFI_AP);
      WiFi.softAP("M5StickDebug", "12345678");
      currentMode = MODE_AP;
    }
  } else {
    // Если нет сохраненных настроек, сразу запускаем AP
    WiFi.mode(WIFI_AP);
    WiFi.softAP("M5StickDebug", "12345678");
    currentMode = MODE_AP;
  }
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
                                        "<a href='/kvm'>KVM Controls</a></body></html>");
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
    
    for (int i = 0; i < scanCount; i++) {
      JsonObject network = networksArray.createNestedObject();
      network["ssid"] = WiFi.SSID(i);
      network["rssi"] = WiFi.RSSI(i);
      network["encryption"] = WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "Open" : "Encrypted";
      network["channel"] = WiFi.channel(i);
    }
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });
  
  // Маршрут для подключения к сети
  server.on("/connect", HTTP_POST, [](AsyncWebServerRequest *request){
    // Проверяем наличие необходимых параметров
    if (!request->hasParam("ssid", true) || !request->hasParam("password", true)) {
      request->send(400, "text/plain", "Missing SSID or password");
      return;
    }
    
    // Получаем значения параметров
    ssid = request->getParam("ssid", true)->value();
    password = request->getParam("password", true)->value();
    
    // Сохраняем конфигурацию
    saveConfiguration();
    
    // Подключаемся к сети
    WiFi.disconnect();
    WiFi.begin(ssid.c_str(), password.c_str());
    
    // Отправляем статус
    request->send(200, "text/plain", "Connecting to network...");
    
    // Переключаемся в клиентский режим
    currentMode = MODE_CLIENT;
  });
  
  // Маршрут для управления GPIO (KVM)
  server.on("/kvm", HTTP_GET, [](AsyncWebServerRequest *request){
    // Формирование JSON с состоянием пинов
    DynamicJsonDocument doc(1024);
    JsonArray pinsArray = doc.createNestedArray("pins");
    
    for (const auto& pin : kvmPins) {
      JsonObject pinObj = pinsArray.createNestedObject();
      pinObj["pin"] = pin.pin;
      pinObj["name"] = pin.name;
      pinObj["state"] = pin.state;
    }
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });
  
  // Маршрут для управления состоянием пина
  server.on("/kvm/toggle", HTTP_POST, [](AsyncWebServerRequest *request){
    if (!request->hasParam("pin", true)) {
      request->send(400, "text/plain", "Missing pin parameter");
      return;
    }
    
    int pinIndex = request->getParam("pin", true)->value().toInt();
    
    // Проверяем, существует ли такой пин
    if (pinIndex >= 0 && pinIndex < kvmPins.size()) {
      togglePin(pinIndex);
      request->send(200, "text/plain", "Pin state changed");
    } else {
      request->send(404, "text/plain", "Pin not found");
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
    
    // Проверяем, не существует ли уже такой пин
    for (const auto& p : kvmPins) {
      if (p.pin == pin) {
        request->send(400, "text/plain", "Pin already configured");
        return;
      }
    }
    
    // Добавляем новый пин
    PinConfig newPin = {pin, name, false};
    kvmPins.push_back(newPin);
    
    // Настраиваем пин как выход
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);
    
    // Сохраняем конфигурацию
    saveConfiguration();
    
    request->send(200, "text/plain", "Pin added successfully");
  });
  
  // Маршрут для диагностики сети
  server.on("/diagnostic", HTTP_GET, [](AsyncWebServerRequest *request){
    performNetworkDiagnostics();
    
    DynamicJsonDocument doc(1024);
    doc["connected"] = WiFi.status() == WL_CONNECTED;
    doc["ssid"] = WiFi.SSID();
    doc["rssi"] = WiFi.RSSI();
    doc["ip"] = WiFi.localIP().toString();
    doc["gateway"] = WiFi.gatewayIP().toString();
    doc["subnet"] = WiFi.subnetMask().toString();
    doc["dns"] = WiFi.dnsIP().toString();
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });
  
  // Запуск веб-сервера
  server.begin();
}

// Обработка нажатий кнопок
void handleButtons() {
  // Кнопка A: Изменение режима
  if (M5.BtnA.wasPressed()) {
    // Циклически переключаем режимы
    currentMode = (OperationMode)((currentMode + 1) % 4);
    
    // Обновляем заголовок на дисплее
    M5.Lcd.fillRect(0, 0, M5.Lcd.width(), 20, BLACK);
    M5.Lcd.setCursor(0, 0);
    
    switch (currentMode) {
      case MODE_CLIENT:
        M5.Lcd.println("Client Mode");
        break;
      case MODE_AP:
        M5.Lcd.println("AP Mode");
        
        // Переключение в режим точки доступа
        WiFi.disconnect();
        WiFi.mode(WIFI_AP);
        WiFi.softAP("M5StickDebug", "12345678");
        break;
      case MODE_SCAN:
        M5.Lcd.println("Scan Mode");
        scanWiFiNetworks();  // Начальное сканирование
        break;
      case MODE_DIAGNOSTIC:
        M5.Lcd.println("Diagnostic Mode");
        break;
    }
  }
  
  // Кнопка B: Действие/Выбор в зависимости от режима
  if (M5.BtnB.wasPressed()) {
    buttonPressed = true;
    
    switch (currentMode) {
      case MODE_CLIENT:
        // В клиентском режиме повторно подключаемся к сети
        if (WiFi.status() != WL_CONNECTED && ssid.length() > 0) {
          WiFi.begin(ssid.c_str(), password.c_str());
        }
        break;
      case MODE_AP:
        // В режиме AP запускаем портал настройки
        wifiManager.startConfigPortal("M5StickDebug", "12345678");
        break;
      case MODE_SCAN:
        // В режиме сканирования выбираем сеть
        selectedNetwork = (selectedNetwork + 1) % scanCount;
        break;
      case MODE_DIAGNOSTIC:
        // В режиме диагностики запускаем проверку
        performNetworkDiagnostics();
        break;
    }
  }
}

// Сканирование WiFi сетей
void scanWiFiNetworks() {
  M5.Lcd.fillRect(0, 20, M5.Lcd.width(), M5.Lcd.height() - 20, BLACK);
  M5.Lcd.setCursor(0, 20);
  M5.Lcd.println("Scanning WiFi...");
  
  scanCount = WiFi.scanNetworks();
  
  if (scanCount == 0) {
    M5.Lcd.println("No networks found");
  } else {
    snprintf(displayBuffer, sizeof(displayBuffer), 
             "Found %d networks", scanCount);
  }
  
  selectedNetwork = 0;  // Сбрасываем выбранную сеть
}

// Отображение информации о сети
void displayNetworkInfo() {
  if (scanCount <= 0) {
    return;
  }
  
  if (selectedNetwork >= 0 && selectedNetwork < scanCount) {
    snprintf(displayBuffer, sizeof(displayBuffer),
             "Network %d/%d\nSSID: %s\nRSSI: %ddBm\nCh: %d\nEnc: %s",
             selectedNetwork + 1, scanCount,
             WiFi.SSID(selectedNetwork).c_str(),
             WiFi.RSSI(selectedNetwork),
             WiFi.channel(selectedNetwork),
             WiFi.encryptionType(selectedNetwork) == WIFI_AUTH_OPEN ? "Open" : "Encrypted");
  }
}

// Сохранение конфигурации в LittleFS
void saveConfiguration() {
  DynamicJsonDocument doc(1024);
  
  // Сохраняем настройки WiFi
  doc["ssid"] = ssid;
  doc["password"] = password;
  
  // Сохраняем настройки пинов
  JsonArray pinsArray = doc.createNestedArray("pins");
  for (const auto& pin : kvmPins) {
    JsonObject pinObj = pinsArray.createNestedObject();
    pinObj["pin"] = pin.pin;
    pinObj["name"] = pin.name;
    pinObj["state"] = pin.state;
  }
  
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
  DynamicJsonDocument doc(1024);
  DeserializationError error = deserializeJson(doc, configFile);
  configFile.close();
  
  if (error) {
    return;
  }
  
  // Загружаем настройки WiFi
  if (doc.containsKey("ssid")) {
    ssid = doc["ssid"].as<String>();
  }
  
  if (doc.containsKey("password")) {
    password = doc["password"].as<String>();
  }
  
  // Загружаем настройки пинов
  kvmPins.clear();
  if (doc.containsKey("pins")) {
    JsonArray pinsArray = doc["pins"];
    for (JsonObject pinObj : pinsArray) {
      PinConfig pin = {
        pinObj["pin"],
        pinObj["name"].as<String>(),
        pinObj["state"]
      };
      kvmPins.push_back(pin);
      
      // Настраиваем пин
      pinMode(pin.pin, OUTPUT);
      digitalWrite(pin.pin, pin.state ? HIGH : LOW);
    }
  }
}

// Переключение состояния пина
void togglePin(int pinIndex) {
  if (pinIndex >= 0 && pinIndex < kvmPins.size()) {
    // Инвертируем состояние
    kvmPins[pinIndex].state = !kvmPins[pinIndex].state;
    
    // Устанавливаем физический пин
    digitalWrite(kvmPins[pinIndex].pin, kvmPins[pinIndex].state ? HIGH : LOW);
    
    // Сохраняем конфигурацию
    saveConfiguration();
  }
}

// Выполнение диагностики сети
void performNetworkDiagnostics() {
  M5.Lcd.fillRect(0, 20, M5.Lcd.width(), M5.Lcd.height() - 20, BLACK);
  M5.Lcd.setCursor(0, 20);
  
  if (WiFi.status() != WL_CONNECTED) {
    M5.Lcd.println("Not connected to WiFi");
    snprintf(displayBuffer, sizeof(displayBuffer), "Not connected to WiFi\nCan't run diagnostics");
    return;
  }
  
  // Базовая информация о подключении
  IPAddress ip = WiFi.localIP();
  IPAddress gateway = WiFi.gatewayIP();
  IPAddress subnet = WiFi.subnetMask();
  IPAddress dns = WiFi.dnsIP();
  
  // Формируем результат диагностики
  snprintf(displayBuffer, sizeof(displayBuffer),
           "SSID: %s\nRSSI: %ddBm\nIP: %s\nGW: %s\nMask: %s\nDNS: %s",
           WiFi.SSID().c_str(),
           WiFi.RSSI(),
           ip.toString().c_str(),
           gateway.toString().c_str(),
           subnet.toString().c_str(),
           dns.toString().c_str());
           
  // Дополнительно можно сделать пинг до шлюза
  // Однако, для простоты и экономии места, этот функционал опущен
}