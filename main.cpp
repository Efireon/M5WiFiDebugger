#include <M5StickCPlus2.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <vector>

// Подключаем модули
#include "kvm_module.h"
#include "honeypot.h"
#include "network_tools.h"
#include "device_manager.h"
#include "ir_controller.h"

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

// Структура для хранения результатов сканирования WiFi
struct WiFiResult {
  String ssid;
  int32_t rssi;
  uint8_t encryptionType;
  int32_t channel;
};

// Расширенная конфигурация для AP режима
struct APConfig {
  APMode mode;
  String ssid;
  String password;
  bool hidden;
  int channel;
};

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

// Глобальные переменные
MenuSection currentSection = MENU_MAIN;  // Текущий раздел меню
int selectedMenuItem = 0;                // Выбранный пункт меню
int menuStartPosition = 0;               // Начальная позиция для отображения меню
AsyncWebServer server(80);               // Веб-сервер на порту 80
WiFiManager wifiManager;                 // Менеджер WiFi
APConfig apConfig = {AP_MODE_OFF, "M5StickDebug", "12345678", false, 1}; // Конфигурация AP

std::vector<WiFiResult> networks;        // Список найденных сетей

// Модули
KVMModule kvmModule(&server);
Honeypot honeypot(&server);
NetworkTools networkTools(&server);
DeviceManager deviceManager(&server);
IRController irController(&server);

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
  
  // Инициализация модулей
  kvmModule.begin();
  networkTools.setupAPI();
  honeypot.setupAPI();
  deviceManager.begin();
  irController.begin();
  
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

void loop() {
  // Обновление состояния кнопок
  M5.update();
  handleButtons();
  
  // Проверка соединения по таймеру
  kvmModule.performConnectionCheck();
  
  // Мониторинг пинов
  if (currentSection == MENU_KVM_MONITOR) {
    kvmModule.updatePinMonitoring();
  }
  
  // Обновление информации о состоянии устройства
  deviceManager.update();
  
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
    M5.Lcd.println("M5StickDebug");
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
  
  // Обновляем режим AP для сетевых инструментов
  networkTools.setAPMode(apConfig.mode != AP_MODE_OFF);
}

// Обновление режима точки доступа
void updateAccessPointMode() {
  // Если был активен режим honeypot, выключаем его
  if (honeypot.isActive()) {
    honeypot.stop();
  }
  
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
      honeypot.start(apConfig.ssid, apConfig.channel);
      break;
  }
  
  // Обновляем состояние AP для сетевых инструментов
  networkTools.setAPMode(apConfig.mode != AP_MODE_OFF);
  
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
    
    // Дополнительная информация о точке доступа
    if (apConfig.mode != AP_MODE_OFF) {
      doc["ap_mode"] = apConfig.mode;
      doc["ap_ssid"] = apConfig.ssid;
      doc["ap_ip"] = WiFi.softAPIP().toString();
      doc["ap_stations"] = WiFi.softAPgetStationNum();
    }
    
    // Информация об устройстве
    const SensorData& sensorData = deviceManager.getSensorData();
    doc["battery"] = sensorData.batteryVoltage;
    doc["batteryPercentage"] = sensorData.batteryPercentage;
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });
  
  // API для поиска устройства (Find Me)
  server.on("/device/findme", HTTP_POST, [](AsyncWebServerRequest *request){
    // Запуск звукового сигнала
    deviceManager.playFindMe();
    request->send(200, "text/plain", "Find Me signal activated");
  });
  
  // Запуск веб-сервера
  server.begin();
}

// Функция воспроизведения сигнала "Find Me"
void playFindMeSound() {
  deviceManager.playFindMe();
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
      // Выключение устройства
      M5.Lcd.fillScreen(BLACK);
      M5.Lcd.setCursor(0, 0);
      M5.Lcd.println("Shutting down...");
      delay(1000);
      deviceManager.powerOff();
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
      case MENU_KVM_OPTIONS:
        // Пины + доп. пункты меню (интервал проверки, DHCP, возврат)
        maxItems = kvmModule.getPins().size() + 3;
        break;
      case MENU_KVM_MONITOR:
      case MENU_IR_CONTROL:
        maxItems = 1; // Только возврат в главное меню
        break;
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
  const SensorData& sensorData = deviceManager.getSensorData();
  char batteryBuf[20];
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
    case MENU_MAIN:
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
      
    case MENU_AP_OPTIONS:
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
      
    // Другие разделы меню
    case MENU_WIFI_SCAN:
      // Если у нас есть результаты сканирования, отображаем их
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
      
    case MENU_KVM_MONITOR:
      // Отображаем информацию о подключении и состоянии пинов
      M5.Lcd.setCursor(5, y);
      if (WiFi.status() == WL_CONNECTED) {
        M5.Lcd.print("WiFi: ");
        M5.Lcd.print(WiFi.SSID());
        y += 16;
        M5.Lcd.setCursor(5, y);
        M5.Lcd.print("IP: ");
        M5.Lcd.print(WiFi.localIP().toString());
      } else {
        M5.Lcd.print("WiFi: Not Connected");
      }
      
      y += 16;
      M5.Lcd.setCursor(5, y);
      M5.Lcd.print("KVM Pins:");
      y += 16;
      
      // Отображаем состояние пинов
      const auto& pins = kvmModule.getPins();
      for (int i = 0; i < pins.size() && y < M5.Lcd.height(); i++) {
        M5.Lcd.setCursor(5, y);
        M5.Lcd.print(pins[i].name);
        M5.Lcd.print(": ");
        if (pins[i].state) {
          M5.Lcd.print("ON");
        } else {
          M5.Lcd.print("OFF");
        }
        y += 16;
      }
      break;
      
    case MENU_KVM_OPTIONS:
      // Меню настроек KVM
      M5.Lcd.setCursor(5, y);
      M5.Lcd.print("Configure KVM pins:");
      y += 16;
      
      // Отображаем список пинов
      const auto& kvmPins = kvmModule.getPins();
      for (int i = menuStartPosition; i < kvmPins.size() && i < menuStartPosition + displayLines - 3; i++) {
        M5.Lcd.setCursor(5, y);
        if (i == selectedMenuItem) {
          M5.Lcd.fillRect(0, y-1, M5.Lcd.width(), 12, BLUE);
          M5.Lcd.setTextColor(WHITE);
        }
        M5.Lcd.print(kvmPins[i].name);
        M5.Lcd.print(" (Pin ");
        M5.Lcd.print(kvmPins[i].pin);
        M5.Lcd.print(")");
        y += 16;
        M5.Lcd.setTextColor(WHITE);
      }
      
      // Добавляем дополнительные пункты меню
      if (y < M5.Lcd.height() - 30) {
        M5.Lcd.setCursor(5, y);
        if (kvmPins.size() == selectedMenuItem) {
          M5.Lcd.fillRect(0, y-1, M5.Lcd.width(), 12, BLUE);
          M5.Lcd.setTextColor(WHITE);
        }
        M5.Lcd.print("Connection Check: ");
        
        switch (kvmModule.getConnectionCheckInterval()) {
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
        if (kvmPins.size() + 1 == selectedMenuItem) {
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
        if (kvmPins.size() + 2 == selectedMenuItem) {
          M5.Lcd.fillRect(0, y-1, M5.Lcd.width(), 12, BLUE);
          M5.Lcd.setTextColor(WHITE);
        }
        M5.Lcd.print("Back to Main Menu");
        M5.Lcd.setTextColor(WHITE);
      }
      break;
      
    case MENU_IR_CONTROL:
      // Отображаем доступные IR-команды
      M5.Lcd.setCursor(5, y);
      const auto& irCommands = irController.getCommands();
      if (irCommands.size() > 0) {
        // Заголовок
        M5.Lcd.print("IR Commands:");
        y += 16;
        
        for (int i = menuStartPosition; i < irCommands.size() && i < menuStartPosition + displayLines - 2; i++) {
          M5.Lcd.setCursor(5, y);
          if (i == selectedMenuItem) {
            M5.Lcd.fillRect(0, y-1, M5.Lcd.width(), 12, BLUE);
            M5.Lcd.setTextColor(WHITE);
          }
          M5.Lcd.print(irCommands[i].name);
          y += 16;
          M5.Lcd.setTextColor(WHITE);
        }
      } else {
        M5.Lcd.print("No IR commands yet");
        y += 16;
        M5.Lcd.setCursor(5, y);
        M5.Lcd.print("Use web interface to add");
        y += 32;
      }
      
      M5.Lcd.setCursor(5, y);
      if (irCommands.size() > 0 ? selectedMenuItem == irCommands.size() : selectedMenuItem == 0) {
        M5.Lcd.fillRect(0, y-1, M5.Lcd.width(), 12, BLUE);
        M5.Lcd.setTextColor(WHITE);
      }
      M5.Lcd.print("Back to Main Menu");
      M5.Lcd.setTextColor(WHITE);
      break;
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
        
        M5.Lcd.println("\nConnect to this network?");
        M5.Lcd.println("A:Yes B:No");
        
        // Ждем нажатия кнопки
        bool buttonPressed = false;
        bool connectToNetwork = false;
        while (!buttonPressed) {
          M5.update();
          
          if (M5.BtnA.wasPressed()) {
            // Пользователь хочет подключиться к сети
            buttonPressed = true;
            connectToNetwork = true;
          } else if (M5.BtnB.wasPressed() || M5.BtnC.wasPressed()) {
            // Пользователь отменил подключение
            buttonPressed = true;
          }
          
          delay(50);
        }
        
        if (connectToNetwork) {
          // Показываем экран для ввода пароля (в упрощенном варианте)
          M5.Lcd.fillScreen(BLACK);
          M5.Lcd.setCursor(0, 0);
          M5.Lcd.println("Connecting...");
          M5.Lcd.println("-----------------");
          
          // Отключаем текущий режим AP, если активен
          if (apConfig.mode != AP_MODE_OFF) {
            apConfig.mode = AP_MODE_OFF;
            updateAccessPointMode();
          }
          
          // Пробуем подключиться к сети без пароля (для открытых сетей)
          WiFi.disconnect();
          WiFi.begin(networks[selectedMenuItem].ssid.c_str(), "");
          
          // В реальной реализации тут должен быть экран ввода пароля
          
          M5.Lcd.println("Waiting for connection...");
          
          int timeout = 0;
          while (WiFi.status() != WL_CONNECTED && timeout < 10) {
            delay(1000);
            M5.Lcd.print(".");
            timeout++;
          }
          
          if (WiFi.status() == WL_CONNECTED) {
            M5.Lcd.println("\nConnected!");
          } else {
            M5.Lcd.println("\nFailed. Use web interface");
            M5.Lcd.println("for password protected");
            M5.Lcd.println("networks.");
          }
          
          delay(3000);
        }
      } else {
        // Если нет сетей или пользователь нажал в пустом месте, запускаем сканирование
        scanWiFiNetworks();
      }
      break;
      
    case MENU_KVM_OPTIONS: {
      const auto& kvmPins = kvmModule.getPins();
      
      if (selectedMenuItem >= 0 && selectedMenuItem < kvmPins.size()) {
        // Управление пином KVM
        kvmModule.togglePin(selectedMenuItem);
      } else if (selectedMenuItem == kvmPins.size()) {
        // Изменение интервала проверки соединения
        ConnectionCheckInterval currentInterval = kvmModule.getConnectionCheckInterval();
        ConnectionCheckInterval newInterval = static_cast<ConnectionCheckInterval>(
          (static_cast<int>(currentInterval) + 1) % 6
        );
        kvmModule.setConnectionCheckInterval(newInterval);
      } else if (selectedMenuItem == kvmPins.size() + 1) {
        // Переключение DHCP
        kvmModule.setUseDHCP(!kvmModule.getUseDHCP());
      } else if (selectedMenuItem == kvmPins.size() + 2) {
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
      
    case MENU_IR_CONTROL: {
      const auto& irCommands = irController.getCommands();
      
      if (irCommands.size() > 0 && selectedMenuItem < irCommands.size()) {
        // Отправляем ИК-команду
        irController.transmitCommand(selectedMenuItem);
        
        M5.Lcd.fillScreen(BLACK);
        M5.Lcd.setCursor(0, 0);
        M5.Lcd.println("Transmitting IR...");
        M5.Lcd.println("-----------------");
        M5.Lcd.print("Command: ");
        M5.Lcd.println(irCommands[selectedMenuItem].name);
        
        delay(1500); // Показываем информацию 1.5 секунды
      } else {
        // Возврат в главное меню
        currentSection = MENU_MAIN;
        selectedMenuItem = 0;
        menuStartPosition = 0;
      }
      break;
    }
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
  
  // Сохраняем настройки модулей
  kvmModule.saveConfig(LittleFS);
  honeypot.saveLogs(LittleFS);
  networkTools.saveConfig(LittleFS);
  irController.saveConfig(LittleFS);
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
  
  // Загружаем настройки модулей
  kvmModule.loadConfig(LittleFS);
  honeypot.loadLogs(LittleFS);
  networkTools.loadConfig(LittleFS);
  irController.loadConfig(LittleFS);
}