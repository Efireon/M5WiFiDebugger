#define IP_NAPT 1
#define IP_FORWARD 1
#define LWIP_IPV4_NAPT 1

#include <M5StickCPlus2.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <vector>
#include <esp_wifi.h>
#include <tcpip_adapter.h>
#include <esp_system.h>
#include <lwip/ip_addr.h>
#include <esp_wifi.h>
#include <esp_netif.h>

extern "C" {
  int ip_napt_enable_no(int if_no, int enable);
}
extern "C" {
  #include "lwip/lwip_napt.h"
}


#include "common_structures.h"
#include "honeypot.h"
#include "network_tools.h"
#include "device_manager.h"

// Определение разделов меню
enum MenuSection {
  MENU_MAIN,           // Главное меню
  MENU_WIFI,           // Раздел Wi-Fi
  MENU_KVM,            // Раздел KVM
  MENU_IR_CONTROL,     // ИК-управление (заглушка)
  MENU_DEVICE_SETTINGS,// Настройки устройства
  
  // Подразделы Wi-Fi
  MENU_AP_OPTIONS,     // Опции режима точки доступа
  MENU_AP_STATUS,      // Статус AP и управление
  MENU_AP_USERS,       // Список пользователей AP
  MENU_AP_USER_MENU,   // Меню для конкретного пользователя
  MENU_AP_USER_INFO,   // Информация о пользователе
  MENU_AP_USER_SNIFF,  // Сниффинг пользователя
  MENU_WIFI_SCAN,      // Сканирование и отладка WiFi
  MENU_WIFI_SAVED,     // Сохраненные сети WiFi
  MENU_AP_MODE_SELECT, // Выбор режима AP
  
  // Подразделы KVM
  MENU_KVM_OPTIONS,    // Опции KVM
  MENU_KVM_MONITOR     // Мониторинг KVM
};

// Пункты меню для конкретного пользователя
const char* apUserMenuOptions[] = {
  "Info",
  "Sniff",
  "Block/Unblock",
  "Back to Users"
};
#define AP_USER_MENU_OPTIONS_COUNT (sizeof(apUserMenuOptions) / sizeof(char*))

// Структура для хранения информации о клиенте AP
struct APClient {
  IPAddress ip;
  uint8_t mac[6];
  bool blocked;
  unsigned int totalBytes;
  String lastPacket;
  unsigned long lastSeen;
};

// Структура для пунктов меню пользователя AP
struct APUserMenuItem {
  const char* title;
  bool expandable;    // Может быть раскрыт
  bool expanded;      // Текущее состояние (развернут/свернут)
  bool action;        // Является ли это действием
};

APUserMenuItem apUserMenuItems[] = {
  {"Info", false, true, false},  // Всегда развернут
  {"Sniff", true, false, true},  // Можно раскрыть
  {"Block", false, false, true}, // Действие блокировки
  {"Back", false, false, true}   // Возврат назад
};
#define AP_USER_MENU_ITEMS_COUNT (sizeof(apUserMenuItems) / sizeof(APUserMenuItem))

// Структура для пунктов меню
struct MenuItem {
  const char* title;
  MenuSection section;
};

#define MAX_PACKET_BUFFER 20  // Максимальное количество пакетов в буфере
#define ESPIF_STA 0
#define ESPIF_AP  1

// Структура для хранения перехваченного пакета
struct SniffedPacket {
  String sourceMAC;
  String destMAC;
  String type;
  int size;
  int8_t rssi;
  unsigned long timestamp;
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
    // Загружаем конфигурацию
    loadConfig();
    
    // Настраиваем пины
    for (auto& pin : pins) {
      pinMode(pin.pin, OUTPUT);
      // При инициализации устанавливаем пины в сохраненное состояние
      digitalWrite(pin.pin, pin.state ? HIGH : LOW);
    }
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
    newPin.state = false; // Начальное состояние - выключено
    newPin.monitorMode = PIN_MONITOR_OFF;
    newPin.lastStateChange = 0;
    
    pins.push_back(newPin);
    
    // Настраиваем пин
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW); // Пин всегда инициализируется в LOW для состояния false
    
    // Сохраняем конфигурацию
    saveConfig();
    
    return true;
  }
  
  // Установка состояния пина
  void setPin(int index, bool state) {
    if (index >= 0 && index < pins.size()) {
      pins[index].state = state;
      // Физически пин всегда работает нормально: HIGH = включено, LOW = выключено
      digitalWrite(pins[index].pin, state ? HIGH : LOW);
      pins[index].lastStateChange = millis();
      saveConfig();
    }
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
  
  // Отправка импульса на пин
  void pulsePin(int index, int duration) {
    if (index >= 0 && index < pins.size()) {
      bool originalState = pins[index].state;
      // Инвертируем состояние
      setPin(index, !originalState);
      delay(duration);
      // Возвращаем обратно
      setPin(index, originalState);
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

// Предопределенные пины для M5StickC Plus2
const struct {
  int pin;
  const char* name;
  const char* description;
} AVAILABLE_PINS[] = {
  {0,  "GPIO0",  "GROVE"},
  {25, "GPIO25", "GROVE"},
  {26, "GPIO26", "GROVE"},
  {32, "GPIO32", "GROVE"},
  {33, "GPIO33", "GROVE"},
  {36, "GPIO36", "GROVE (Input only)"},
};
#define AVAILABLE_PINS_COUNT (sizeof(AVAILABLE_PINS) / sizeof(AVAILABLE_PINS[0]))

// Глобальные переменные
MenuSection currentSection = MENU_MAIN;  // Текущий раздел меню
int selectedMenuItem = 0;                // Выбранный пункт меню
int menuStartPosition = 0;               // Начальная позиция для отображения меню
AsyncWebServer server(80);               // Веб-сервер на порту 80
WiFiManager wifiManager;                 // Менеджер WiFi
APConfig apConfig = {AP_MODE_OFF, "M5StickDebug", "12345678", false, 1}; // Конфигурация AP
DeviceSettings deviceSettings = {80, 300, "M5WifiDebugger", false, 70, false}; // Настройки устройства по умолчанию
DeviceSettings globalDeviceSettings; // Глобальная переменная для других модулей (определена здесь)
std::vector<SavedNetwork> savedNetworks; // Сохраненные сети
std::vector<WiFiResult> networks;        // Список найденных сетей
std::vector<APClient> apClients;         // Список клиентов AP
std::vector<String> blockedMACs;         // Список заблокированных MAC адресов
std::vector<SniffedPacket> packetBuffer;  // Буфер перехваченных пакетов
int selectedAPUser = -1;                 // Выбранный пользователь AP
bool isSniffing = false;                 // Флаг активного сниффинга
int currentSniffingClient = -1;          // Текущий клиент для сниффинга
uint8_t* currentSniffingMAC = nullptr;   // MAC адрес текущего клиента для сниффинга

// Наши модули
KVMModule kvmModule;
NetworkTools networkTools;
DeviceManager deviceManager;
Honeypot honeypot;

// Описание пунктов главного меню
const MenuItem mainMenuItems[] = {
  {"Wi-Fi", MENU_WIFI},
  {"KVM", MENU_KVM},
  {"IR Control", MENU_IR_CONTROL},
  {"Device Settings", MENU_DEVICE_SETTINGS}
};
#define MAIN_MENU_ITEMS_COUNT (sizeof(mainMenuItems) / sizeof(MenuItem))

// Пункты подменю Wi-Fi
const MenuItem wifiMenuItems[] = {
  {"AP Options", MENU_AP_OPTIONS},
  {"AP Status", MENU_AP_STATUS},
  {"WiFi Scan & Debug", MENU_WIFI_SCAN},
  {"Saved Networks", MENU_WIFI_SAVED},
  {"Back to Main Menu", MENU_MAIN}
};
#define WIFI_MENU_ITEMS_COUNT (sizeof(wifiMenuItems) / sizeof(MenuItem))

// Пункты подменю KVM
const MenuItem kvmMenuItems[] = {
  {"KVM Options", MENU_KVM_OPTIONS},
  {"KVM Monitor", MENU_KVM_MONITOR},
  {"Back to Main Menu", MENU_MAIN}
};
#define KVM_MENU_ITEMS_COUNT (sizeof(kvmMenuItems) / sizeof(MenuItem))

// Пункты подменю AP Options
const char* apOptionsItems[] = {
  "AP Mode",
  "SSID & Password",
  "Back to Main Menu"
};
#define AP_OPTIONS_ITEMS_COUNT (sizeof(apOptionsItems) / sizeof(char*))

// Пункты подменю режимов AP
const char* apModeItems[] = {
  "Off",
  "Normal",
  "Repeater",
  "Hidden",
  "Honeypot",
  "Back"
};
#define AP_MODE_ITEMS_COUNT (sizeof(apModeItems) / sizeof(char*))

// Пункты подменю AP Status
const MenuItem apStatusMenuItems[] = {
  {"Users", MENU_AP_USERS},
  {"Shuffle IP", MENU_MAIN},  // Специальный пункт для смены IP
  {"Back to Wi-Fi", MENU_WIFI}
};
#define AP_STATUS_MENU_ITEMS_COUNT (sizeof(apStatusMenuItems) / sizeof(MenuItem))

// Пункты подменю Device Settings
const char* deviceSettingsItems[] = {
  "Brightness",
  "Sleep Timeout",
  "Device ID",
  "Display Rotation",
  "Volume",
  "Invert KVM Pins",
  "Back to Main Menu"
};
#define DEVICE_SETTINGS_ITEMS_COUNT (sizeof(deviceSettingsItems) / sizeof(char*))

// Буфер для сообщений на дисплее
char displayBuffer[128];

// Переменные для обработки кнопок
unsigned long buttonALastPress = 0;
unsigned long buttonBLastPress = 0;
unsigned long buttonCLastPress = 0;
bool buttonALongPress = false;
bool buttonBLongPress = false;
bool buttonCLongPress = false;
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
void saveSavedNetworks();
void loadSavedNetworks();
void connectToSavedNetwork(int index);
void updateAPClients();
void shuffleIP();
void startPacketSniffing(int clientIndex);
void stopPacketSniffing();
void promiscuous_rx_callback(void* buf, wifi_promiscuous_pkt_type_t type);
bool isMACBlocked(const uint8_t* mac);
void onStationConnected(WiFiEvent_t event, WiFiEventInfo_t info);

// Функция инициализации
void setup() {
  // Инициализация M5StickCPlus2
  M5.begin();
  
  // Инициализация файловой системы
  if (!LittleFS.begin(true)) {
    M5.Lcd.println("LittleFS Mount Failed");
  }
  
  // Загрузка конфигурации
  loadConfiguration();
  loadDeviceSettings();
  loadSavedNetworks();
  loadBlockedMACs(); 
  
  // Инициализируем глобальные настройки
  globalDeviceSettings = deviceSettings;
  
  // Применяем настройки устройства
  M5.Lcd.setBrightness(deviceSettings.brightness);
  M5.Lcd.setRotation(deviceSettings.rotateDisplay ? 1 : 3);
  globalDeviceSettings = deviceSettings; // Синхронизируем глобальные настройки
  
  // Инициализируем наши модули
  kvmModule.begin();
  deviceManager.begin();
  
  // Регистрируем обработчики событий WiFi
  WiFi.onEvent(onStationConnected, ARDUINO_EVENT_WIFI_AP_STACONNECTED);
  
  // Настройка экрана
  setupDisplay();
  
  // Настройка WiFi
  setupWiFi();
  
  // Настройка веб-сервера
  setupWebServer();
  
  // Отображаем главное меню
  drawMenu();
}

// Основной цикл
void loop() {
  static unsigned long lastCheck = 0;
  static unsigned long lastRepeaterCheck = 0;
  
  // Проверяем состояние каждые 5 секунд
  if (millis() - lastCheck > 5000) {
    lastCheck = millis();
    Serial.printf("WiFi mode: %d, Free heap: %d bytes\n", 
                  WiFi.getMode(), ESP.getFreeHeap());
  }
  
  // Проверяем состояние репитера каждые 10 секунд
  if (apConfig.mode == AP_MODE_REPEATER && millis() - lastRepeaterCheck > 10000) {
    lastRepeaterCheck = millis();
    
    // Проверяем, что мы все еще подключены к основной сети
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("Repeater: Lost WiFi connection, attempting to reconnect...");
      
      // Пытаемся переподключиться
      if (!WiFi.reconnect()) {
        // Если не удалось, пробуем подключиться заново
        if (savedNetworks.size() > 0) {
          WiFi.begin(savedNetworks[0].ssid.c_str(), savedNetworks[0].password.c_str());
        }
      }
    } else {
      // Проверяем, что AP все еще активна
      if (WiFi.getMode() != WIFI_AP_STA) {
        Serial.println("Repeater: AP mode lost, reactivating...");
        updateAccessPointMode();
      } else {
        // Выводим статистику репитера
        Serial.printf("Repeater stats - Clients: %d, Main IP: %s, AP IP: %s\n", 
                     WiFi.softAPgetStationNum(),
                     WiFi.localIP().toString().c_str(),
                     WiFi.softAPIP().toString().c_str());
      }
    }
  }
  
  // Обновление состояния кнопок
  M5.update();
  handleButtons();
  
  // Обновляем наши модули
  kvmModule.update();
  deviceManager.update();
  
  // Проверяем завершение сканирования WiFi
  if (isScanningWifi) {
    int scanResult = WiFi.scanComplete();
    if (scanResult >= 0) {
      networks.clear();
      for (int i = 0; i < scanResult; i++) {
        WiFiResult network;
        network.ssid = WiFi.SSID(i);
        network.rssi = WiFi.RSSI(i);
        network.encryptionType = WiFi.encryptionType(i);
        network.channel = WiFi.channel(i);
        networks.push_back(network);
      }
      WiFi.scanDelete();
      isScanningWifi = false;
      scanResultsReady = true;  // Убедитесь, что этот флаг устанавливается!
      
      // Обновляем отображение
      if (currentSection == MENU_WIFI_SCAN) {
        drawMenu();
      }
    }
  }
  
  // Обновляем информацию на экране в режиме мониторинга
  if (currentSection == MENU_KVM_MONITOR) {
    static unsigned long lastUpdateTime = 0;
    unsigned long currentTime = millis();
    
    if (currentTime - lastUpdateTime > 1000) { // Обновляем раз в секунду
      lastUpdateTime = currentTime;
      drawMenu();
    }
  }
  
  // Обновляем информацию о клиентах AP
  if (currentSection == MENU_AP_USERS || currentSection == MENU_AP_USER_INFO) {
    static unsigned long lastAPUpdateTime = 0;
    unsigned long currentTime = millis();
    
    if (currentTime - lastAPUpdateTime > 1000) { // Обновляем раз в секунду
      lastAPUpdateTime = currentTime;
      updateAPClients();
      drawMenu();
    }
  }
  
  // Небольшая задержка для стабильности
  delay(50);
}

// Настройка экрана
void setupDisplay() {
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setTextColor(WHITE);
  M5.Lcd.setCursor(0, 0);
}

// Настройка WiFi
void setupWiFi() {
  // Убедимся, что WiFi в правильном режиме
  WiFi.mode(WIFI_MODE_STA);
  
  // Проверяем сохраненные сети
  if (savedNetworks.size() > 0) {
    // Пробуем подключиться к последней сохраненной сети
    connectToSavedNetwork(0);
  }
  
  // Проверка сохраненных настроек и режима AP
  if (apConfig.mode != AP_MODE_OFF) {
    // Переключаемся в режим AP
    WiFi.mode(WIFI_MODE_AP);
    // Если активирован режим AP, запускаем точку доступа
    updateAccessPointMode();
  }
}

// Функция подключения к сохраненной сети
void connectToSavedNetwork(int index) {
  if (index >= 0 && index < savedNetworks.size()) {
    WiFi.begin(savedNetworks[index].ssid.c_str(), savedNetworks[index].password.c_str());
    
    M5.Lcd.fillScreen(BLACK);
    M5.Lcd.setCursor(0, 0);
    M5.Lcd.print("Connecting to ");
    M5.Lcd.println(savedNetworks[index].ssid);
    
    // Ожидание подключения с таймаутом
    int timeout = 0;
    while (WiFi.status() != WL_CONNECTED && timeout < 20) {
      delay(500);
      M5.Lcd.print(".");
      timeout++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
      M5.Lcd.println("\nConnected!");
      delay(1000);
    } else {
      M5.Lcd.println("\nConnection failed!");
      delay(2000);
    }
  }
}

// Обновление режима точки доступа
void updateAccessPointMode() {
  switch (apConfig.mode) {
    case AP_MODE_OFF:
      if (WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA) {
        WiFi.softAPdisconnect(true);
        WiFi.mode(WIFI_STA);
      }
      break;
      
    case AP_MODE_NORMAL:
      if (WiFi.status() == WL_CONNECTED) {
        // Устанавливаем режим STA+AP
        WiFi.mode(WIFI_AP_STA);
        
        // Настраиваем AP
        IPAddress apIP(192, 168, 4, 1);
        IPAddress apGateway(192, 168, 4, 1);
        IPAddress apSubnet(255, 255, 255, 0);
        
        // Сначала конфигурируем IP, потом запускаем AP
        WiFi.softAPConfig(apIP, apGateway, apSubnet);
        
        bool apSuccess = WiFi.softAP(apConfig.ssid.c_str(), apConfig.password.c_str(), apConfig.channel);
        if (!apSuccess) {
          Serial.println("Failed to start AP");
          return;
        }
        
        // Включаем IP forwarding
        tcpip_adapter_ip_info_t ip_info;
        tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_AP, &ip_info);
        
        // Для Arduino IDE используем следующий подход:
        IPAddress myIP = WiFi.localIP();
        IPAddress myAPIP = WiFi.softAPIP();
        
        // Настраиваем DHCP чтобы клиенты получали правильный шлюз и DNS
        dhcps_lease_t dhcp_lease;
        dhcp_lease.enable = true;
        dhcp_lease.start_ip.addr = static_cast<uint32_t>(IPAddress(192, 168, 4, 100));
        dhcp_lease.end_ip.addr = static_cast<uint32_t>(IPAddress(192, 168, 4, 200));
        
        tcpip_adapter_dhcps_stop(TCPIP_ADAPTER_IF_AP);
        tcpip_adapter_dhcps_option(
          TCPIP_ADAPTER_OP_SET,
          TCPIP_ADAPTER_REQUESTED_IP_ADDRESS,
          &dhcp_lease,
          sizeof(dhcps_lease_t)
        );
        
        // Устанавливаем DNS сервер
        uint32_t dns_ip = static_cast<uint32_t>(WiFi.dnsIP(0));
        if (dns_ip == 0) {
          dns_ip = static_cast<uint32_t>(IPAddress(8, 8, 8, 8));
        }
        
        tcpip_adapter_dhcps_option(
          TCPIP_ADAPTER_OP_SET,
          TCPIP_ADAPTER_DOMAIN_NAME_SERVER,
          &dns_ip,
          sizeof(dns_ip)
        );
        
        // Устанавливаем шлюз
        uint32_t gw_ip = static_cast<uint32_t>(apGateway);
        tcpip_adapter_dhcps_option(
          TCPIP_ADAPTER_OP_SET,
          TCPIP_ADAPTER_ROUTER_SOLICITATION_ADDRESS,
          &gw_ip,
          sizeof(gw_ip)
        );
        
        tcpip_adapter_dhcps_start(TCPIP_ADAPTER_IF_AP);
        
        Serial.println("AP mode with NAT enabled");
        Serial.print("AP IP: ");
        Serial.println(WiFi.softAPIP());
        Serial.print("STA IP: ");
        Serial.println(WiFi.localIP());
        Serial.print("Gateway: ");
        Serial.println(WiFi.gatewayIP());
        Serial.print("DNS: ");
        Serial.println(WiFi.dnsIP());
      } else {
        WiFi.mode(WIFI_AP);
        WiFi.softAP(apConfig.ssid.c_str(), apConfig.password.c_str(), apConfig.channel);
      }
      break;
      
    case AP_MODE_HIDDEN:
      // Скрытый режим точки доступа
      if (WiFi.status() == WL_CONNECTED) {
        WiFi.mode(WIFI_AP_STA);
        
        // Настраиваем AP с собственной подсетью
        IPAddress apIP(192, 168, 4, 1);
        IPAddress apSubnet(255, 255, 255, 0);
        
        WiFi.softAPConfig(apIP, apIP, apSubnet);
        WiFi.softAP(apConfig.ssid.c_str(), apConfig.password.c_str(), apConfig.channel, true);
        
        // Включаем маршрутизацию
        #ifdef ESP32
          WiFi.enableAP(true);
          WiFi.enableIpV6();
        #endif
      } else {
        WiFi.mode(WIFI_AP);
        WiFi.softAP(apConfig.ssid.c_str(), apConfig.password.c_str(), apConfig.channel, true);
      }
      break;
      
    case AP_MODE_REPEATER:
      // Режим ретранслятора - мимикрия под подключенную сеть
      if (WiFi.status() == WL_CONNECTED) {
        // Получаем параметры текущего подключения
        String connectedSSID = WiFi.SSID();
        int connectedChannel = WiFi.channel();
        
        // Находим пароль от текущей сети в сохраненных
        String connectedPassword = "";
        for (const auto& network : savedNetworks) {
          if (network.ssid == connectedSSID) {
            connectedPassword = network.password;
            break;
          }
        }
        
        // Сначала убеждаемся, что мы в режиме STA+AP
        WiFi.mode(WIFI_AP_STA);
        
        // Настраиваем сетевые параметры для режима репитера
        IPAddress apIP(192, 168, 5, 1);  // Другая подсеть чем у основной сети
        IPAddress apSubnet(255, 255, 255, 0);
        
        WiFi.softAPConfig(apIP, apIP, apSubnet);
        
        // Создаем точку доступа с теми же параметрами, что и у подключенной сети
        bool success = WiFi.softAP(connectedSSID.c_str(), connectedPassword.c_str(), connectedChannel, false, 4);
        
        if (!success) {
          Serial.println("Failed to start repeater AP");
          apConfig.mode = AP_MODE_NORMAL;
          return;
        }
        
        // Включаем маршрутизацию
        #ifdef ESP32
          WiFi.enableAP(true);
          WiFi.enableIpV6();
        #endif
        
        Serial.println("Repeater mode activated - mirroring connected network");
        Serial.print("Mirroring SSID: ");
        Serial.println(connectedSSID);
        Serial.print("Channel: ");
        Serial.println(connectedChannel);
        Serial.print("Our IP in main network: ");
        Serial.println(WiFi.localIP());
        Serial.print("AP IP: ");
        Serial.println(WiFi.softAPIP());
      } else {
        // Если не подключены к WiFi, не можем включить режим репитера
        Serial.println("Cannot enable repeater mode - not connected to WiFi");
        return;
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
  // Проверяем, что WiFi инициализирован
  if (WiFi.getMode() == WIFI_MODE_NULL) {
    Serial.println("WiFi not initialized, forcing STA mode");
    WiFi.mode(WIFI_MODE_STA);
    delay(100);
  }
  
  // Основная страница
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    Serial.println("Root page requested");
    
    if (LittleFS.exists("/index.html")) {
      Serial.println("index.html found");
      
      // Проверяем размер файла
      File file = LittleFS.open("/index.html", "r");
      if (!file) {
        Serial.println("Failed to open index.html");
        request->send(500, "text/plain", "Failed to open file");
        return;
      }
      
      size_t fileSize = file.size();
      Serial.printf("File size: %d bytes\n", fileSize);
      file.close();
      
      // Если файл слишком большой, отправляем по частям
      if (fileSize > 20000) {
        request->send(LittleFS, "/index.html", "text/html", false);
      } else {
        request->send(LittleFS, "/index.html", "text/html");
      }
    } else {
      Serial.println("index.html not found");
      request->send(200, "text/html", "<html><body><h1>File not found</h1><p>Upload the web interface files to LittleFS</p></body></html>");
    }
  });
  
  // Обслуживание статических файлов
  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
  
  // Маршрут для проверки статуса сканирования
  server.on("/scan-start", HTTP_GET, [](AsyncWebServerRequest *request){
    if (isScanningWifi) {
      request->send(429, "application/json", "{\"status\":\"scanning\",\"message\":\"Scanning already in progress\"}");
      return;
    }
    
    networks.clear();
    isScanningWifi = true;
    scanResultsReady = false;
    WiFi.scanNetworks(true, false, false, 300); // Асинхронное сканирование
    request->send(202, "application/json", "{\"status\":\"started\",\"message\":\"Scan started\"}");
  });
  
  // Маршрут для проверки статуса сканирования
  server.on("/scan-status", HTTP_GET, [](AsyncWebServerRequest *request){
    if (isScanningWifi) {
      int scanResult = WiFi.scanComplete();
      if (scanResult >= 0) {
        networks.clear();
        for (int i = 0; i < scanResult; i++) {
          WiFiResult network;
          network.ssid = WiFi.SSID(i);
          network.rssi = WiFi.RSSI(i);
          network.encryptionType = WiFi.encryptionType(i);
          network.channel = WiFi.channel(i);
          networks.push_back(network);
        }
        WiFi.scanDelete();
        isScanningWifi = false;
        scanResultsReady = true;  // Важно установить этот флаг!
        request->send(200, "application/json", 
                     "{\"status\":\"ready\",\"message\":\"Scan complete\",\"count\":" + String(networks.size()) + "}");
      } else if (scanResult == WIFI_SCAN_RUNNING) {
        request->send(200, "application/json", "{\"status\":\"scanning\",\"message\":\"Scanning in progress\"}");
      } else {
        // Ошибка сканирования
        WiFi.scanDelete();
        isScanningWifi = false;
        scanResultsReady = false;  // При ошибке устанавливаем в false
        request->send(500, "application/json", "{\"status\":\"error\",\"message\":\"Scan failed\"}");
      }
    } else if (scanResultsReady) {
      request->send(200, "application/json", 
                   "{\"status\":\"ready\",\"message\":\"Results available\",\"count\":" + String(networks.size()) + "}");
    } else {
      request->send(200, "application/json", "{\"status\":\"idle\",\"message\":\"No scan performed\"}");
    }
  });
  
  // Маршрут для получения результатов сканирования
  server.on("/scan-results", HTTP_GET, [](AsyncWebServerRequest *request){
    if (!scanResultsReady && networks.size() == 0) {
      request->send(404, "application/json", "{\"error\":\"No scan results available\"}");
      return;
    }
    
    DynamicJsonDocument doc(4096);  // Увеличим размер для большего количества сетей
    doc["totalNetworks"] = networks.size();
    
    JsonArray networksArray = doc.createNestedArray("networks");
    
    for (int i = 0; i < networks.size(); i++) {
      JsonObject netObj = networksArray.createNestedObject();
      netObj["ssid"] = networks[i].ssid;
      netObj["rssi"] = networks[i].rssi;
      
      String encType;
      switch (networks[i].encryptionType) {
        case WIFI_AUTH_OPEN: encType = "Open"; break;
        case WIFI_AUTH_WEP: encType = "WEP"; break;
        case WIFI_AUTH_WPA_PSK: encType = "WPA-PSK"; break;
        case WIFI_AUTH_WPA2_PSK: encType = "WPA2-PSK"; break;
        case WIFI_AUTH_WPA_WPA2_PSK: encType = "WPA/WPA2-PSK"; break;
        default: encType = "Unknown";
      }
      netObj["encryption"] = encType;
      netObj["channel"] = networks[i].channel;
    }
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });

  // Маршрут для получения сохраненных сетей
  server.on("/wifi/saved", HTTP_GET, [](AsyncWebServerRequest *request){
    DynamicJsonDocument doc(2048);
    JsonArray networksArray = doc.createNestedArray("networks");
    
    for (const auto& network : savedNetworks) {
      JsonObject netObj = networksArray.createNestedObject();
      netObj["ssid"] = network.ssid;
    }
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });
  
  // Маршрут для подключения к сохраненной сети
  server.on("/wifi/connect", HTTP_POST, [](AsyncWebServerRequest *request){
    if (!request->hasParam("ssid", true)) {
      request->send(400, "application/json", "{\"error\":\"Missing SSID parameter\"}");
      return;
    }
    
    String ssid = request->getParam("ssid", true)->value();
    
    // Ищем сохраненную сеть
    for (const auto& network : savedNetworks) {
      if (network.ssid == ssid) {
        WiFi.begin(network.ssid.c_str(), network.password.c_str());
        request->send(200, "application/json", "{\"success\":true,\"message\":\"Connecting to network\"}");
        return;
      }
    }
    
    request->send(404, "application/json", "{\"error\":\"Network not found in saved networks\"}");
  });
  
  // Маршрут для удаления сохраненной сети
  server.on("/wifi/delete", HTTP_POST, [](AsyncWebServerRequest *request){
    if (!request->hasParam("ssid", true)) {
      request->send(400, "application/json", "{\"error\":\"Missing SSID parameter\"}");
      return;
    }
    
    String ssid = request->getParam("ssid", true)->value();
    
    // Удаляем сеть из списка
    for (auto it = savedNetworks.begin(); it != savedNetworks.end(); ++it) {
      if (it->ssid == ssid) {
        savedNetworks.erase(it);
        saveSavedNetworks();
        request->send(200, "application/json", "{\"success\":true,\"message\":\"Network deleted\"}");
        return;
      }
    }
    
    request->send(404, "application/json", "{\"error\":\"Network not found\"}");
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
    
    // Применяем новые настройки
    updateAccessPointMode();
    
    request->send(200, "text/plain", "AP settings updated");
  });
  
  // Маршрут для подключения к сети
  server.on("/connect", HTTP_POST, [](AsyncWebServerRequest *request){
    if (!request->hasParam("ssid", true)) {
      request->send(400, "text/plain", "Missing SSID parameter");
      return;
    }
    
    String ssid = request->getParam("ssid", true)->value();
    String password = "";
    
    if (request->hasParam("password", true)) {
      password = request->getParam("password", true)->value();
    }
    
    // Сохраняем сеть
    bool found = false;
    for (auto& network : savedNetworks) {
      if (network.ssid == ssid) {
        network.password = password;
        found = true;
        break;
      }
    }
    
    if (!found) {
      SavedNetwork newNetwork;
      newNetwork.ssid = ssid;
      newNetwork.password = password;
      savedNetworks.push_back(newNetwork);
    }
    
    saveSavedNetworks();
    
    // Подключаемся к сети
    WiFi.disconnect();
    WiFi.begin(ssid.c_str(), password.c_str());
    
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
        kvmModule.setPin(pinIndex, newState);
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
  
  // API для получения доступных пинов
  server.on("/api/kvm/available-pins", HTTP_GET, [](AsyncWebServerRequest *request){
    DynamicJsonDocument doc(2048);
    JsonArray pinsArray = doc.createNestedArray("pins");
    
    const auto& usedPins = kvmModule.getPins();
    
    for (int i = 0; i < AVAILABLE_PINS_COUNT; i++) {
      JsonObject pinObj = pinsArray.createNestedObject();
      pinObj["pin"] = AVAILABLE_PINS[i].pin;
      pinObj["name"] = AVAILABLE_PINS[i].name;
      pinObj["description"] = AVAILABLE_PINS[i].description;
      
      // Проверяем, используется ли пин
      bool inUse = false;
      for (const auto& usedPin : usedPins) {
        if (usedPin.pin == AVAILABLE_PINS[i].pin) {
          inUse = true;
          break;
        }
      }
      pinObj["inUse"] = inUse;
    }
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });
  
  // API для управления через curl
  server.on("/api/kvm/pin", HTTP_POST, [](AsyncWebServerRequest *request){
    int pinIndex = -1;
    bool hasState = false;
    bool newState = false;
    
    // Проверяем параметры из URL (GET parameters)
    if (request->hasParam("index")) {
      pinIndex = request->getParam("index")->value().toInt();
    }
    
    if (request->hasParam("state")) {
      hasState = true;
      String stateStr = request->getParam("state")->value();
      newState = (stateStr == "true" || stateStr == "1");
    }
    
    // Проверяем параметры из тела запроса (POST parameters)
    if (pinIndex == -1 && request->hasParam("index", true)) {
      pinIndex = request->getParam("index", true)->value().toInt();
    }
    
    if (!hasState && request->hasParam("state", true)) {
      hasState = true;
      String stateStr = request->getParam("state", true)->value();
      newState = (stateStr == "true" || stateStr == "1");
    }
    
    if (pinIndex == -1) {
      request->send(400, "application/json", "{\"error\":\"Missing pin index parameter\"}");
      return;
    }
    
    const auto& pins = kvmModule.getPins();
    
    if (pinIndex >= 0 && pinIndex < pins.size()) {
      if (hasState) {
        kvmModule.setPin(pinIndex, newState);
      } else {
        kvmModule.togglePin(pinIndex);
      }
      
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
  
  // Добавляем поддержку GET метода для curl
  server.on("/api/kvm/pin", HTTP_GET, [](AsyncWebServerRequest *request){
    if (!request->hasParam("index")) {
      request->send(400, "application/json", "{\"error\":\"Missing pin index parameter\"}");
      return;
    }
    
    int pinIndex = request->getParam("index")->value().toInt();
    const auto& pins = kvmModule.getPins();
    
    if (pinIndex >= 0 && pinIndex < pins.size()) {
      bool hasState = request->hasParam("state");
      
      if (hasState) {
        String stateStr = request->getParam("state")->value();
        bool newState = (stateStr == "true" || stateStr == "1");
        kvmModule.setPin(pinIndex, newState);
      } else {
        kvmModule.togglePin(pinIndex);
      }
      
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

  // Маршрут для импульса на пин
  server.on("/api/kvm/pulse", HTTP_POST, [](AsyncWebServerRequest *request){
    if (!request->hasParam("index", true)) {
      request->send(400, "application/json", "{\"error\":\"Missing pin index parameter\"}");
      return;
    }
    
    int pinIndex = request->getParam("index", true)->value().toInt();
    int duration = 500; // Значение по умолчанию
    
    if (request->hasParam("duration", true)) {
      duration = request->getParam("duration", true)->value().toInt();
      if (duration < 50) duration = 50;
      if (duration > 10000) duration = 10000;
    }
    
    const auto& pins = kvmModule.getPins();
    
    if (pinIndex >= 0 && pinIndex < pins.size()) {
      kvmModule.pulsePin(pinIndex, duration);
      
      DynamicJsonDocument doc(256);
      doc["success"] = true;
      doc["pin"] = pins[pinIndex].pin;
      doc["duration"] = duration;
      
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
  
  // Маршрут для перезагрузки устройства
  server.on("/device/reset", HTTP_POST, [](AsyncWebServerRequest *request){
    request->send(200, "text/plain", "Device will restart");
    delay(1000);
    ESP.restart();
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
        globalDeviceSettings.brightness = brightness;
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
        drawMenu();
      }
    }
    
    if (request->hasParam("volume", true)) {
      int volume = request->getParam("volume", true)->value().toInt();
      if (volume >= 0 && volume <= 100) {
        deviceSettings.volume = volume;
        globalDeviceSettings.volume = volume;
      }
    }
    
    // Сохраняем настройки
    saveDeviceSettings();
    
    request->send(200, "text/plain", "Device settings updated");
  });
  
  // Маршрут для получения настроек инвертирования пинов
  server.on("/settings/pin-inversion", HTTP_GET, [](AsyncWebServerRequest *request){
    DynamicJsonDocument doc(256);
    doc["inverted"] = deviceSettings.invertPins;
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });
  
  // Маршрут для изменения настроек инвертирования пинов
  server.on("/settings/pin-inversion", HTTP_POST, [](AsyncWebServerRequest *request){
    if (!request->hasParam("inverted", true)) {
      request->send(400, "application/json", "{\"error\":\"Missing inverted parameter\"}");
      return;
    }
    
    bool inverted = (request->getParam("inverted", true)->value() == "true" || 
                     request->getParam("inverted", true)->value() == "1");
    
    if (deviceSettings.invertPins != inverted) {
      deviceSettings.invertPins = inverted;
      globalDeviceSettings.invertPins = inverted;
      saveDeviceSettings();
      // При изменении инверсии нет необходимости менять физическое состояние пинов
    }
    
    DynamicJsonDocument doc(256);
    doc["success"] = true;
    doc["inverted"] = deviceSettings.invertPins;
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });
  
  // Маршрут для сетевых инструментов - Ping
  server.on("/network/ping", HTTP_POST, [](AsyncWebServerRequest *request){
    if (!request->hasParam("host", true)) {
      request->send(400, "application/json", "{\"error\":\"Missing host parameter\"}");
      return;
    }
    
    String host = request->getParam("host", true)->value();
    PingResult result = networkTools.ping(host);
    
    DynamicJsonDocument doc(512);
    doc["success"] = result.success;
    doc["host"] = result.target;
    doc["ip"] = result.ip.toString();
    doc["time"] = result.avg_time;
    doc["loss"] = result.loss;
    
    if (!result.success && result.error.length() > 0) {
      doc["error"] = result.error;
    }
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });
  
  // Маршрут для сетевых инструментов - Сканирование IP
  server.on("/network/scan", HTTP_POST, [](AsyncWebServerRequest *request){
    if (!request->hasParam("range", true)) {
      request->send(400, "application/json", "{\"error\":\"Missing range parameter\"}");
      return;
    }
    
    String range = request->getParam("range", true)->value();
    std::vector<ScanResult> results = networkTools.scanNetwork(range);
    
    DynamicJsonDocument doc(4096);
    doc["success"] = true;
    JsonArray hostsArray = doc.createNestedArray("hosts");
    
    for (const auto& result : results) {
      JsonObject hostObj = hostsArray.createNestedObject();
      hostObj["ip"] = result.ip.toString();
      hostObj["active"] = result.active;
      hostObj["time"] = result.response_time;
    }
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });
  
  // Маршрут для сетевых инструментов - Сканирование портов
  server.on("/network/portscan", HTTP_POST, [](AsyncWebServerRequest *request){
    if (!request->hasParam("ip", true) || 
        !request->hasParam("startPort", true) || 
        !request->hasParam("endPort", true)) {
      request->send(400, "application/json", "{\"error\":\"Missing required parameters\"}");
      return;
    }
    
    String ip = request->getParam("ip", true)->value();
    int startPort = request->getParam("startPort", true)->value().toInt();
    int endPort = request->getParam("endPort", true)->value().toInt();
    
    std::vector<PortScanResult> results = networkTools.scanPorts(ip, startPort, endPort);
    
    DynamicJsonDocument doc(4096);
    doc["success"] = true;
    JsonArray portsArray = doc.createNestedArray("ports");
    
    for (const auto& result : results) {
      if (result.open) {
        JsonObject portObj = portsArray.createNestedObject();
        portObj["port"] = result.port;
        portObj["service"] = result.service;
      }
    }
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });
  
  // Маршрут для сетевых инструментов - Сканирование одного IP
  server.on("/network/scan-single", HTTP_POST, [](AsyncWebServerRequest *request){
    if (!request->hasParam("ip", true)) {
      request->send(400, "application/json", "{\"error\":\"Missing IP parameter\"}");
      return;
    }
    
    String ip = request->getParam("ip", true)->value();
    PingResult result = networkTools.ping(ip, 1);
    
    DynamicJsonDocument doc(256);
    doc["success"] = result.success;
    doc["active"] = result.success;
    doc["time"] = result.avg_time;
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });
  
  // Маршрут для сетевых инструментов - Блокировка IP
  server.on("/network/block", HTTP_POST, [](AsyncWebServerRequest *request){
    if (!request->hasParam("ip", true)) {
      request->send(400, "application/json", "{\"error\":\"Missing IP parameter\"}");
      return;
    }
    
    String ip = request->getParam("ip", true)->value();
    
    if (networkTools.blockIP(ip)) {
      request->send(200, "application/json", "{\"success\":true,\"message\":\"IP blocked\"}");
    } else {
      request->send(400, "application/json", "{\"error\":\"Failed to block IP\"}");
    }
  });
  
  // Маршрут для сетевых инструментов - Разблокировка IP
  server.on("/network/unblock", HTTP_POST, [](AsyncWebServerRequest *request){
    if (!request->hasParam("ip", true)) {
      request->send(400, "application/json", "{\"error\":\"Missing IP parameter\"}");
      return;
    }
    
    String ip = request->getParam("ip", true)->value();
    
    if (networkTools.unblockIP(ip)) {
      request->send(200, "application/json", "{\"success\":true,\"message\":\"IP unblocked\"}");
    } else {
      request->send(400, "application/json", "{\"error\":\"Failed to unblock IP\"}");
    }
  });
  
  // Маршрут для получения списка заблокированных IP
  server.on("/network/blocked", HTTP_GET, [](AsyncWebServerRequest *request){
    const auto& blockedIPs = networkTools.getBlockedIPs();
    
    DynamicJsonDocument doc(2048);
    JsonArray blockedArray = doc.createNestedArray("blocked");
    
    for (const auto& ip : blockedIPs) {
      blockedArray.add(ip);
    }
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });
  
  // API для работы с пользователями AP
  server.on("/ap/users", HTTP_GET, [](AsyncWebServerRequest *request){
    updateAPClients();
    
    DynamicJsonDocument doc(4096);
    JsonArray usersArray = doc.createNestedArray("users");
    
    for (const auto& client : apClients) {
      JsonObject userObj = usersArray.createNestedObject();
      userObj["ip"] = client.ip.toString();
      
      char macStr[18];
      sprintf(macStr, "%02X:%02X:%02X:%02X:%02X:%02X",
              client.mac[0], client.mac[1], client.mac[2],
              client.mac[3], client.mac[4], client.mac[5]);
      userObj["mac"] = macStr;
      
      userObj["blocked"] = client.blocked;
      userObj["totalBytes"] = client.totalBytes;
      userObj["lastPacket"] = client.lastPacket;
      userObj["lastSeen"] = client.lastSeen;
    }
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });
  
  // API для блокировки/разблокировки пользователя AP
  server.on("/ap/users/block", HTTP_POST, [](AsyncWebServerRequest *request){
    if (!request->hasParam("ip", true)) {
      request->send(400, "application/json", "{\"error\":\"Missing IP parameter\"}");
      return;
    }
    
    String ip = request->getParam("ip", true)->value();
    bool blocked = false;
    
    if (request->hasParam("blocked", true)) {
      blocked = (request->getParam("blocked", true)->value() == "true" || 
                 request->getParam("blocked", true)->value() == "1");
    }
    
    // Находим клиента по IP
    for (auto& client : apClients) {
      if (client.ip.toString() == ip) {
        char macStr[18];
        sprintf(macStr, "%02X:%02X:%02X:%02X:%02X:%02X",
                client.mac[0], client.mac[1], client.mac[2],
                client.mac[3], client.mac[4], client.mac[5]);
        
        if (blocked) {
          // Блокируем
          networkTools.blockIP(ip);
          bool found = false;
          for (const auto& blockedMAC : blockedMACs) {
            if (blockedMAC == macStr) {
              found = true;
              break;
            }
          }
          if (!found) {
            blockedMACs.push_back(macStr);
          }
          client.blocked = true;
        } else {
          // Разблокируем
          networkTools.unblockIP(ip);
          for (auto it = blockedMACs.begin(); it != blockedMACs.end(); ++it) {
            if (*it == macStr) {
              blockedMACs.erase(it);
              break;
            }
          }
          client.blocked = false;
        }
        
        request->send(200, "application/json", "{\"success\":true}");
        return;
      }
    }
    
    request->send(404, "application/json", "{\"error\":\"User not found\"}");
  });
  
  // API для получения трафика пользователя
  server.on("/ap/users/traffic", HTTP_GET, [](AsyncWebServerRequest *request){
    if (!request->hasParam("ip")) {
      request->send(400, "application/json", "{\"error\":\"Missing IP parameter\"}");
      return;
    }
    
    String ip = request->getParam("ip")->value();
    
    // Находим клиента по IP
    for (const auto& client : apClients) {
      if (client.ip.toString() == ip) {
        DynamicJsonDocument doc(1024);
        doc["ip"] = client.ip.toString();
        doc["totalBytes"] = client.totalBytes;
        doc["lastPacket"] = client.lastPacket;
        doc["lastSeen"] = client.lastSeen;
        
        // Если активен сниффинг для этого клиента, добавляем информацию
        if (isSniffing && currentSniffingClient >= 0 && 
            apClients[currentSniffingClient].ip == client.ip) {
          doc["sniffing"] = true;
          doc["sniffedPackets"] = client.totalBytes; // Обновляется в режиме реального времени
          doc["lastPacketInfo"] = client.lastPacket;
        } else {
          doc["sniffing"] = false;
        }
        
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
        return;
      }
    }
    
    request->send(404, "application/json", "{\"error\":\"User not found\"}");
  });
  
  // API для запуска/остановки сниффинга
  server.on("/ap/users/sniff", HTTP_POST, [](AsyncWebServerRequest *request){
    if (!request->hasParam("ip", true)) {
      request->send(400, "application/json", "{\"error\":\"Missing IP parameter\"}");
      return;
    }
    
    String ip = request->getParam("ip", true)->value();
    bool start = false;
    
    if (request->hasParam("action", true)) {
      start = (request->getParam("action", true)->value() == "start");
    }
    
    // Находим клиента по IP
    int clientIndex = -1;
    for (int i = 0; i < apClients.size(); i++) {
      if (apClients[i].ip.toString() == ip) {
        clientIndex = i;
        break;
      }
    }
    
    if (clientIndex >= 0) {
      if (start) {
        startPacketSniffing(clientIndex);
        request->send(200, "application/json", "{\"success\":true,\"message\":\"Sniffing started\"}");
      } else {
        stopPacketSniffing();
        request->send(200, "application/json", "{\"success\":true,\"message\":\"Sniffing stopped\"}");
      }
    } else {
      request->send(404, "application/json", "{\"error\":\"User not found\"}");
    }
  });

  // API для получения буфера перехваченных пакетов
  server.on("/ap/users/sniff/buffer", HTTP_GET, [](AsyncWebServerRequest *request){
    DynamicJsonDocument doc(4096);
    JsonArray packetsArray = doc.createNestedArray("packets");
    
    for (const auto& packet : packetBuffer) {
      JsonObject packetObj = packetsArray.createNestedObject();
      packetObj["sourceMAC"] = packet.sourceMAC;
      packetObj["destMAC"] = packet.destMAC;
      packetObj["type"] = packet.type;
      packetObj["size"] = packet.size;
      packetObj["rssi"] = packet.rssi;
      packetObj["timestamp"] = packet.timestamp;
    }
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });

  // API для получения списка всех заблокированных MAC
  server.on("/ap/blocked-macs", HTTP_GET, [](AsyncWebServerRequest *request){
    DynamicJsonDocument doc(2048);
    JsonArray blockedArray = doc.createNestedArray("blocked");
    
    for (const auto& mac : blockedMACs) {
      blockedArray.add(mac);
    }
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });

  // API для блокировки/разблокировки по MAC адресу
  server.on("/ap/block-mac", HTTP_POST, [](AsyncWebServerRequest *request){
    if (!request->hasParam("mac", true)) {
      request->send(400, "application/json", "{\"error\":\"Missing MAC parameter\"}");
      return;
    }
    
    String mac = request->getParam("mac", true)->value();
    bool block = true;
    
    if (request->hasParam("action", true)) {
      block = (request->getParam("action", true)->value() == "block");
    }
    
    if (block) {
      bool found = false;
      for (const auto& blockedMAC : blockedMACs) {
        if (blockedMAC == mac) {
          found = true;
          break;
        }
      }
      if (!found) {
        blockedMACs.push_back(mac);
        saveBlockedMACs();
      }
    } else {
      for (auto it = blockedMACs.begin(); it != blockedMACs.end(); ++it) {
        if (*it == mac) {
          blockedMACs.erase(it);
          saveBlockedMACs();
          break;
        }
      }
    }
    
    request->send(200, "application/json", "{\"success\":true}");
  });
  
  // Запуск веб-сервера
  server.begin();
}

// Обработка нажатий кнопок
void handleButtons() {
  // Проверка долгого нажатия на кнопку C (Power/Scroll Up)
  if (M5.BtnC.isPressed()) {
    if (buttonCLastPress == 0) {
      buttonCLastPress = millis();
    } else if (!buttonCLongPress && millis() - buttonCLastPress > 3000) {
      // Долгое нажатие на C - выключение устройства
      buttonCLongPress = true;
      M5.Power.powerOff();
    }
  } else {
    if (buttonCLastPress > 0 && !buttonCLongPress) {
      // Короткое нажатие на C - прокрутка вверх
      int maxItems = getMaxMenuItems();
      selectedMenuItem = (selectedMenuItem > 0) ? selectedMenuItem - 1 : maxItems - 1;
      
      // Корректируем позицию прокрутки
      int displayLines = (M5.Lcd.height() - 30) / 16;
      if (selectedMenuItem < menuStartPosition) {
        menuStartPosition = selectedMenuItem;
      } else if (selectedMenuItem == maxItems - 1) {
        menuStartPosition = max(0, maxItems - displayLines);
      }
      
      drawMenu();
    }
    buttonCLastPress = 0;
    buttonCLongPress = false;
  }
  
  // Проверка долгого нажатия на кнопку A (Home)
  if (M5.BtnA.isPressed()) {
    if (buttonALastPress == 0) {
      buttonALastPress = millis();
    } else if (!buttonALongPress && millis() - buttonALastPress > 500) {
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
  
  // Кнопка B (Control/Scroll Down) - добавляем обработку долгого нажатия
  if (M5.BtnB.isPressed()) {
    if (buttonBLastPress == 0) {
      buttonBLastPress = millis();
    } else if (!buttonBLongPress && millis() - buttonBLastPress > 500) {
      // Долгое нажатие на B - прокрутка вверх на один пункт
      buttonBLongPress = true;
      int maxItems = getMaxMenuItems();
      selectedMenuItem = (selectedMenuItem > 0) ? selectedMenuItem - 1 : maxItems - 1;
      
      int displayLines = (M5.Lcd.height() - 30) / 16;
      if (selectedMenuItem < menuStartPosition) {
        menuStartPosition = selectedMenuItem;
      } else if (selectedMenuItem == maxItems - 1) {
        menuStartPosition = max(0, maxItems - displayLines);
      }
      
      drawMenu();
    }
  } else {
    if (buttonBLastPress > 0 && !buttonBLongPress) {
      // Короткое нажатие на B — прокрутка вниз
      int maxItems = getMaxMenuItems();
      selectedMenuItem = (selectedMenuItem < maxItems - 1) ? selectedMenuItem + 1 : 0;
  
      int displayLines = (M5.Lcd.height() - 30) / 16;
      if (selectedMenuItem >= menuStartPosition + displayLines) {
        menuStartPosition = selectedMenuItem - displayLines + 1;
      } else if (selectedMenuItem == 0) {
        menuStartPosition = 0;
      }
  
      drawMenu();
    }
    buttonBLastPress = 0;
    buttonBLongPress = false;
  }  
}

// Функция для получения максимального количества элементов в текущем меню
int getMaxMenuItems() {
  switch (currentSection) {
    case MENU_MAIN:
      return MAIN_MENU_ITEMS_COUNT;
    
    case MENU_WIFI:
      return WIFI_MENU_ITEMS_COUNT;
    
    case MENU_KVM:
      return KVM_MENU_ITEMS_COUNT;
    
    case MENU_AP_OPTIONS:
      return AP_OPTIONS_ITEMS_COUNT;
    
    case MENU_AP_STATUS:
      return AP_STATUS_MENU_ITEMS_COUNT;
    
    case MENU_AP_MODE_SELECT:
      return AP_MODE_ITEMS_COUNT;
    
    case MENU_AP_USERS:
      return apClients.size() + 1; // +1 для кнопки возврата
    
    case MENU_AP_USER_MENU:
      return AP_USER_MENU_OPTIONS_COUNT;
    
    case MENU_AP_USER_INFO:
      return 1; // Только кнопка возврата
    
    case MENU_AP_USER_SNIFF:
      return 2; // Start/Stop и Back
    case MENU_WIFI_SCAN:
      return networks.size() > 0 ? networks.size() : 1;
    
    case MENU_WIFI_SAVED:
      return savedNetworks.size() + 1; // +1 для кнопки возврата
    
    case MENU_KVM_OPTIONS: {
      const auto& pins = kvmModule.getPins();
      return pins.size() + 3; // Пины + настройки
    }
    
    case MENU_KVM_MONITOR: {
      const auto& pins = kvmModule.getPins();
      return pins.size() + 2; // Пины + инфо
    }
    
    case MENU_IR_CONTROL:
      return 1; // Только кнопка возврата
    
    case MENU_DEVICE_SETTINGS:
      return DEVICE_SETTINGS_ITEMS_COUNT;
    
    default:
      return 1;
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
      M5.Lcd.println("MAIN MENU");
      break;
    case MENU_WIFI:
      M5.Lcd.println("WI-FI");
      break;
    case MENU_KVM:
      M5.Lcd.println("KVM");
      break;
    case MENU_AP_OPTIONS:
      M5.Lcd.println("AP OPTIONS");
      break;
    case MENU_AP_STATUS:
      M5.Lcd.println("AP STATUS");
      break;
    case MENU_AP_USERS:
      M5.Lcd.println("AP USERS");
      break;
    case MENU_AP_USER_MENU:
      M5.Lcd.println("USER MENU");
      if (selectedAPUser >= 0 && selectedAPUser < apClients.size()) {
        M5.Lcd.print("IP: ");
        M5.Lcd.println(apClients[selectedAPUser].ip.toString());
      }
      break;
    case MENU_AP_USER_INFO:
      M5.Lcd.println("USER INFO");
      break;
    case MENU_AP_USER_SNIFF:
      M5.Lcd.println("SNIFF");
      break;
    case MENU_AP_MODE_SELECT:
      M5.Lcd.println("AP MODE SELECT");
      break;
    case MENU_WIFI_SCAN:
      M5.Lcd.println("WiFi SCAN & DEBUG");
      break;
    case MENU_WIFI_SAVED:
      M5.Lcd.println("SAVED NETWORKS");
      break;
    case MENU_KVM_OPTIONS:
      M5.Lcd.println("KVM OPTIONS");
      break;
    case MENU_KVM_MONITOR:
      M5.Lcd.println("KVM MONITOR");
      break;
    case MENU_IR_CONTROL:
      M5.Lcd.println("IR CONTROL");
      break;
    case MENU_DEVICE_SETTINGS:
      M5.Lcd.println("DEVICE SETTINGS");
      break;
  }
  
  M5.Lcd.setCursor(M5.Lcd.width() - 70, 0);
  M5.Lcd.print(batteryBuf);
  M5.Lcd.drawLine(0, 10, M5.Lcd.width(), 10, WHITE);
  
  // Отображение элементов меню
  int y = 20;
  int displayLines = (M5.Lcd.height() - 30) / 16;
  int maxItems = getMaxMenuItems();
  
  // Корректируем позицию прокрутки
  if (menuStartPosition > maxItems - displayLines) {
    menuStartPosition = max(0, maxItems - displayLines);
  }
  
  switch (currentSection) {
    case MENU_MAIN: {
      for (int i = menuStartPosition; i < MAIN_MENU_ITEMS_COUNT && i < menuStartPosition + displayLines; i++) {
        M5.Lcd.setCursor(5, y);
        if (i == selectedMenuItem) {
          M5.Lcd.fillRect(0, y-1, M5.Lcd.width(), 12, BLUE);
          M5.Lcd.setTextColor(WHITE);
        }
        M5.Lcd.print(mainMenuItems[i].title);
        y += 16;
        M5.Lcd.setTextColor(WHITE);
      }
      break;
    }
    
    case MENU_WIFI: {
      for (int i = menuStartPosition; i < WIFI_MENU_ITEMS_COUNT && i < menuStartPosition + displayLines; i++) {
        M5.Lcd.setCursor(5, y);
        if (i == selectedMenuItem) {
          M5.Lcd.fillRect(0, y-1, M5.Lcd.width(), 12, BLUE);
          M5.Lcd.setTextColor(WHITE);
        }
        M5.Lcd.print(wifiMenuItems[i].title);
        y += 16;
        M5.Lcd.setTextColor(WHITE);
      }
      break;
    }
    
    case MENU_KVM: {
      for (int i = menuStartPosition; i < KVM_MENU_ITEMS_COUNT && i < menuStartPosition + displayLines; i++) {
        M5.Lcd.setCursor(5, y);
        if (i == selectedMenuItem) {
          M5.Lcd.fillRect(0, y-1, M5.Lcd.width(), 12, BLUE);
          M5.Lcd.setTextColor(WHITE);
        }
        M5.Lcd.print(kvmMenuItems[i].title);
        y += 16;
        M5.Lcd.setTextColor(WHITE);
      }
      break;
    }
    
    case MENU_AP_OPTIONS: {
      for (int i = menuStartPosition; i < AP_OPTIONS_ITEMS_COUNT && i < menuStartPosition + displayLines; i++) {
        M5.Lcd.setCursor(5, y);
        if (i == selectedMenuItem) {
          M5.Lcd.fillRect(0, y-1, M5.Lcd.width(), 12, BLUE);
          M5.Lcd.setTextColor(WHITE);
        }
        
        if (i == 0) { // AP Mode
          M5.Lcd.print(apOptionsItems[i]);
          M5.Lcd.print(": ");
          switch (apConfig.mode) {
            case AP_MODE_OFF: M5.Lcd.print("Off"); break;
            case AP_MODE_NORMAL: M5.Lcd.print("Normal"); break;
            case AP_MODE_REPEATER: M5.Lcd.print("Repeater"); break;
            case AP_MODE_HIDDEN: M5.Lcd.print("Hidden"); break;
            case AP_MODE_HONEYPOT: M5.Lcd.print("Honeypot"); break;
          }
        } else {
          M5.Lcd.print(apOptionsItems[i]);
        }
        
        y += 16;
        M5.Lcd.setTextColor(WHITE);
      }
      break;
    }
    
    case MENU_AP_STATUS: {
      // Отображаем информацию о статусе AP
      M5.Lcd.setCursor(5, y);
      M5.Lcd.print("Mode: ");
      switch (apConfig.mode) {
        case AP_MODE_OFF: M5.Lcd.print("Off"); break;
        case AP_MODE_NORMAL: M5.Lcd.print("Normal"); break;
        case AP_MODE_REPEATER: M5.Lcd.print("Repeater"); break;
        case AP_MODE_HIDDEN: M5.Lcd.print("Hidden"); break;
        case AP_MODE_HONEYPOT: M5.Lcd.print("Honeypot"); break;
      }
      y += 16;
      
      if (apConfig.mode != AP_MODE_OFF) {
        M5.Lcd.setCursor(5, y);
        M5.Lcd.print("AP IP: ");
        M5.Lcd.print(WiFi.softAPIP().toString());
        y += 16;
        
        M5.Lcd.setCursor(5, y);
        M5.Lcd.print("Clients: ");
        M5.Lcd.print(WiFi.softAPgetStationNum());
        y += 16;
      }
      
      if (WiFi.status() == WL_CONNECTED) {
        M5.Lcd.setCursor(5, y);
        M5.Lcd.print("Ext IP: ");
        M5.Lcd.print(WiFi.localIP().toString());
        y += 16;
      }
      
      y += 8; // Отступ перед меню
      
      // Отображаем пункты меню
      for (int i = menuStartPosition; i < AP_STATUS_MENU_ITEMS_COUNT && i < menuStartPosition + displayLines; i++) {
        M5.Lcd.setCursor(5, y);
        if (i == selectedMenuItem) {
          M5.Lcd.fillRect(0, y-1, M5.Lcd.width(), 12, BLUE);
          M5.Lcd.setTextColor(WHITE);
        }
        M5.Lcd.print(apStatusMenuItems[i].title);
        y += 16;
        M5.Lcd.setTextColor(WHITE);
      }
      break;
    }
    
    case MENU_AP_USERS: {
      // Отображаем список IP-адресов подключенных клиентов
      for (int i = menuStartPosition; i < apClients.size() && i < menuStartPosition + displayLines; i++) {
        M5.Lcd.setCursor(5, y);
        if (i == selectedMenuItem) {
          M5.Lcd.fillRect(0, y-1, M5.Lcd.width(), 12, BLUE);
          M5.Lcd.setTextColor(WHITE);
        }
        
        M5.Lcd.print(apClients[i].ip.toString());
        if (apClients[i].blocked) {
          M5.Lcd.print(" [BLK]");
        }
        
        y += 16;
        M5.Lcd.setTextColor(WHITE);
      }
      
      // Кнопка возврата
      if ((apClients.size() == selectedMenuItem) || (apClients.size() == 0 && selectedMenuItem == 0)) {
        M5.Lcd.setCursor(5, y);
        M5.Lcd.fillRect(0, y-1, M5.Lcd.width(), 12, BLUE);
        M5.Lcd.setTextColor(WHITE);
      }
      M5.Lcd.setCursor(5, y);
      M5.Lcd.print("Back to AP Status");
      M5.Lcd.setTextColor(WHITE);
      break;
    }
    
    case MENU_AP_USER_MENU: {
      for (int i = menuStartPosition; i < AP_USER_MENU_OPTIONS_COUNT && i < menuStartPosition + displayLines; i++) {
        M5.Lcd.setCursor(5, y);
        if (i == selectedMenuItem) {
          M5.Lcd.fillRect(0, y-1, M5.Lcd.width(), 12, BLUE);
          M5.Lcd.setTextColor(WHITE);
        }
        
        M5.Lcd.print(apUserMenuOptions[i]);
        
        // Добавляем индикатор состояния для Block/Unblock
        if (i == 2 && selectedAPUser >= 0 && selectedAPUser < apClients.size()) {
          M5.Lcd.print(apClients[selectedAPUser].blocked ? " [BLOCKED]" : " [ACTIVE]");
        }
        
        y += 16;
        M5.Lcd.setTextColor(WHITE);
      }
      break;
    }
    
    case MENU_AP_USER_INFO: {
      if (selectedAPUser >= 0 && selectedAPUser < apClients.size()) {
        const auto& client = apClients[selectedAPUser];
        
        M5.Lcd.setCursor(5, y);
        M5.Lcd.print("IP: ");
        M5.Lcd.println(client.ip.toString());
        y += 16;
        
        M5.Lcd.setCursor(5, y);
        char macStr[18];
        sprintf(macStr, "%02X:%02X:%02X:%02X:%02X:%02X",
                client.mac[0], client.mac[1], client.mac[2],
                client.mac[3], client.mac[4], client.mac[5]);
        M5.Lcd.print("MAC: ");
        M5.Lcd.println(macStr);
        y += 16;
        
        M5.Lcd.setCursor(5, y);
        M5.Lcd.print("Total: ");
        M5.Lcd.print(client.totalBytes);
        M5.Lcd.println(" bytes");
        y += 16;
        
        M5.Lcd.setCursor(5, y);
        M5.Lcd.print("Status: ");
        M5.Lcd.println(client.blocked ? "BLOCKED" : "ACTIVE");
        y += 16;
        
        M5.Lcd.setCursor(5, y);
        M5.Lcd.print("Last seen: ");
        M5.Lcd.print((millis() - client.lastSeen) / 1000);
        M5.Lcd.println("s ago");
        y += 16;
      }
      
      // Кнопка возврата
      y += 8;
      M5.Lcd.setCursor(5, y);
      if (selectedMenuItem == 0) {
        M5.Lcd.fillRect(0, y-1, M5.Lcd.width(), 12, BLUE);
        M5.Lcd.setTextColor(WHITE);
      }
      M5.Lcd.print("Back to User Menu");
      M5.Lcd.setTextColor(WHITE);
      break;
    }

    
    case MENU_AP_USER_SNIFF: {
      // Отображаем статус сниффинга
      M5.Lcd.setCursor(5, y);
      M5.Lcd.print("Status: ");
      M5.Lcd.println(isSniffing ? "ACTIVE" : "OFF");
      y += 16;
      
      if (isSniffing && selectedAPUser >= 0 && selectedAPUser < apClients.size()) {
        M5.Lcd.setCursor(5, y);
        M5.Lcd.print("Packets: ");
        M5.Lcd.println(packetBuffer.size());
        y += 16;
        
        // Отображаем последние пакеты
        int startIdx = max(0, (int)packetBuffer.size() - 5);
        for (int i = startIdx; i < packetBuffer.size(); i++) {
          M5.Lcd.setCursor(5, y);
          char packetInfo[64];
          snprintf(packetInfo, sizeof(packetInfo), "%s %dB", 
                   packetBuffer[i].type.c_str(), 
                   packetBuffer[i].size);
          M5.Lcd.println(packetInfo);
          y += 16;
          
          if (y > M5.Lcd.height() - 40) break;
        }
      }
      
      // Опции управления
      y = M5.Lcd.height() - 36;
      M5.Lcd.setCursor(5, y);
      if (selectedMenuItem == 0) {
        M5.Lcd.fillRect(0, y-1, M5.Lcd.width(), 12, BLUE);
        M5.Lcd.setTextColor(WHITE);
      }
      M5.Lcd.print(isSniffing ? "Stop Sniffing" : "Start Sniffing");
      M5.Lcd.setTextColor(WHITE);
      
      y += 16;
      M5.Lcd.setCursor(5, y);
      if (selectedMenuItem == 1) {
        M5.Lcd.fillRect(0, y-1, M5.Lcd.width(), 12, BLUE);
        M5.Lcd.setTextColor(WHITE);
      }
      M5.Lcd.print("Back to User Menu");
      M5.Lcd.setTextColor(WHITE);
      break;
    }
    
    case MENU_AP_MODE_SELECT: {
      for (int i = menuStartPosition; i < AP_MODE_ITEMS_COUNT && i < menuStartPosition + displayLines; i++) {
        M5.Lcd.setCursor(5, y);
        if (i == selectedMenuItem) {
          M5.Lcd.fillRect(0, y-1, M5.Lcd.width(), 12, BLUE);
          M5.Lcd.setTextColor(WHITE);
        }
        
        if (i == (int)apConfig.mode) {
          M5.Lcd.print("> ");
        } else {
          M5.Lcd.print("  ");
        }
        
        M5.Lcd.print(apModeItems[i]);
        y += 16;
        M5.Lcd.setTextColor(WHITE);
      }
      break;
    }
    
    case MENU_WIFI_SCAN: {
      if (networks.size() > 0) {
        for (int i = menuStartPosition; i < networks.size() && i < menuStartPosition + displayLines; i++) {
          M5.Lcd.setCursor(5, y);
          if (i == selectedMenuItem) {
            M5.Lcd.fillRect(0, y-1, M5.Lcd.width(), 12, BLUE);
            M5.Lcd.setTextColor(WHITE);
          }
          
          String networkInfo = networks[i].ssid;
          if (networkInfo.length() > 10) {
            networkInfo = networkInfo.substring(0, 10) + "...";
          }
          networkInfo += " " + String(networks[i].rssi) + "dBm";
          M5.Lcd.print(networkInfo);
          
          y += 16;
          M5.Lcd.setTextColor(WHITE);
        }
      } else if (isScanningWifi) {
        M5.Lcd.setCursor(5, y);
        M5.Lcd.print("Scanning...");
      } else {
        M5.Lcd.setCursor(5, y);
        M5.Lcd.print("Press A to scan WiFi");
      }
      break;
    }
    
    case MENU_WIFI_SAVED: {
      if (savedNetworks.size() > 0) {
        for (int i = menuStartPosition; i < savedNetworks.size() && i < menuStartPosition + displayLines; i++) {
          M5.Lcd.setCursor(5, y);
          if (i == selectedMenuItem) {
            M5.Lcd.fillRect(0, y-1, M5.Lcd.width(), 12, BLUE);
            M5.Lcd.setTextColor(WHITE);
          }
          
          M5.Lcd.print(savedNetworks[i].ssid);
          y += 16;
          M5.Lcd.setTextColor(WHITE);
        }
      } else {
        M5.Lcd.setCursor(5, y);
        M5.Lcd.print("No saved networks");
        y += 16;
      }
      
      // Кнопка возврата
      if (savedNetworks.size() <= menuStartPosition + displayLines - 1) {
        M5.Lcd.setCursor(5, y);
        if (savedNetworks.size() == selectedMenuItem) {
          M5.Lcd.fillRect(0, y-1, M5.Lcd.width(), 12, BLUE);
          M5.Lcd.setTextColor(WHITE);
        }
        M5.Lcd.print("Back to Main Menu");
        M5.Lcd.setTextColor(WHITE);
      }
      break;
    }
    
    case MENU_KVM_MONITOR: {
      const auto& networkInfo = deviceManager.getNetworkInfo();
      
      // Отображаем информацию о сети
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
      for (int i = 0; i < kvmPins.size() && y < M5.Lcd.height() - 16; i++) {
        M5.Lcd.setCursor(5, y);
        if (i == selectedMenuItem - 2) {
          M5.Lcd.fillRect(0, y-1, M5.Lcd.width(), 12, BLUE);
          M5.Lcd.setTextColor(WHITE);
        }
        
        M5.Lcd.print(kvmPins[i].name);
        M5.Lcd.print(": ");
        // Визуальная инверсия отображения при включенной инверсии
        if (globalDeviceSettings.invertPins) {
          M5.Lcd.print(kvmPins[i].state ? "OFF" : "ON");
        } else {
          M5.Lcd.print(kvmPins[i].state ? "ON" : "OFF");
        }
        y += 16;
        M5.Lcd.setTextColor(WHITE);
      }
      break;
    }
    
    case MENU_KVM_OPTIONS: {
      const auto& pins = kvmModule.getPins();
      
      // Отображаем пины для настройки
      for (int i = menuStartPosition; i < pins.size() && i < menuStartPosition + displayLines; i++) {
        M5.Lcd.setCursor(5, y);
        if (i == selectedMenuItem) {
          M5.Lcd.fillRect(0, y-1, M5.Lcd.width(), 12, BLUE);
          M5.Lcd.setTextColor(WHITE);
        }
        
        // Отображаем имя пина
        M5.Lcd.print(pins[i].name);
        M5.Lcd.print(" (");
        M5.Lcd.print(pins[i].pin);
        M5.Lcd.print(")");
        
        // Отображаем состояние
        M5.Lcd.print(" ");
        if (globalDeviceSettings.invertPins) {
          M5.Lcd.print(pins[i].state ? "OFF" : "ON");
        } else {
          M5.Lcd.print(pins[i].state ? "ON" : "OFF");
        }
        
        // Добавляем кнопку импульса
        M5.Lcd.setCursor(M5.Lcd.width() - 40, y);
        M5.Lcd.print("[P]");
        
        y += 16;
        
        // Отображаем настройку длительности импульса
        M5.Lcd.setCursor(20, y);
        M5.Lcd.setTextSize(1);
        M5.Lcd.print("Pulse: 500ms");
        y += 16;
        
        M5.Lcd.setTextColor(WHITE);
        M5.Lcd.setTextSize(1);
      }
      
      // Отображаем дополнительные пункты меню
      int extraItemsStart = pins.size();
      
      // Connection Check
      if (extraItemsStart <= menuStartPosition + displayLines) {
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
      if (extraItemsStart + 1 <= menuStartPosition + displayLines) {
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
      if (extraItemsStart + 2 <= menuStartPosition + displayLines) {
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
      for (int i = menuStartPosition; i < DEVICE_SETTINGS_ITEMS_COUNT && i < menuStartPosition + displayLines; i++) {
        M5.Lcd.setCursor(5, y);
        if (i == selectedMenuItem) {
          M5.Lcd.fillRect(0, y-1, M5.Lcd.width(), 12, BLUE);
          M5.Lcd.setTextColor(WHITE);
        }
        
        M5.Lcd.print(deviceSettingsItems[i]);
        
        switch (i) {
          case 0: // Brightness
            M5.Lcd.print(": ");
            M5.Lcd.print(deviceSettings.brightness);
            M5.Lcd.print("%");
            break;
          case 1: // Sleep Timeout
            M5.Lcd.print(": ");
            if (deviceSettings.sleepTimeout == 0) {
              M5.Lcd.print("Off");
            } else {
              M5.Lcd.print(deviceSettings.sleepTimeout);
              M5.Lcd.print("s");
            }
            break;
          case 2: // Device ID
            M5.Lcd.print(": ");
            if (deviceSettings.deviceId.length() > 8) {
              M5.Lcd.print(deviceSettings.deviceId.substring(0, 8));
              M5.Lcd.print("...");
            } else {
              M5.Lcd.print(deviceSettings.deviceId);
            }
            break;
          case 3: // Display Rotation
            M5.Lcd.print(": ");
            M5.Lcd.print(deviceSettings.rotateDisplay ? "On" : "Off");
            break;
          case 4: // Volume
            M5.Lcd.print(": ");
            M5.Lcd.print(deviceSettings.volume);
            M5.Lcd.print("%");
            break;
          case 5: // Invert KVM Pins
            M5.Lcd.print(": ");
            M5.Lcd.print(deviceSettings.invertPins ? "Yes" : "No");
            break;
        }
        
        y += 16;
        M5.Lcd.setTextColor(WHITE);
      }
      break;
    }
  }
  
  // Отображение подсказок для кнопок
  M5.Lcd.drawLine(0, M5.Lcd.height() - 15, M5.Lcd.width(), M5.Lcd.height() - 15, WHITE);
  M5.Lcd.setCursor(5, M5.Lcd.height() - 12);
  M5.Lcd.print("A:Select B:Down C:Up");
}

// Обработка выбора пункта меню
void handleMenuAction() {
  switch (currentSection) {
    case MENU_MAIN:
      if (selectedMenuItem >= 0 && selectedMenuItem < MAIN_MENU_ITEMS_COUNT) {
        currentSection = mainMenuItems[selectedMenuItem].section;
        selectedMenuItem = 0;
        menuStartPosition = 0;
      }
      break;
    
    case MENU_WIFI:
      if (selectedMenuItem >= 0 && selectedMenuItem < WIFI_MENU_ITEMS_COUNT) {
        currentSection = wifiMenuItems[selectedMenuItem].section;
        selectedMenuItem = 0;
        menuStartPosition = 0;
        
        // Запускаем сканирование при переходе в раздел сканирования
        if (currentSection == MENU_WIFI_SCAN) {
          scanWiFiNetworks();
        }
      }
      break;
    
    case MENU_KVM:
      if (selectedMenuItem >= 0 && selectedMenuItem < KVM_MENU_ITEMS_COUNT) {
        currentSection = kvmMenuItems[selectedMenuItem].section;
        selectedMenuItem = 0;
        menuStartPosition = 0;
      }
      break;

    case MENU_AP_OPTIONS:
      if (selectedMenuItem == 0) {
        // Переход в меню выбора режима AP
        currentSection = MENU_AP_MODE_SELECT;
        selectedMenuItem = (int)apConfig.mode;
        menuStartPosition = 0;
      } else if (selectedMenuItem == 1) {
        // Настройка SSID и пароля
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
        
        delay(3000);
      } else if (selectedMenuItem == 2) {
        // Возврат в главное меню
        currentSection = MENU_MAIN;
        selectedMenuItem = 0;
        menuStartPosition = 0;
      }
      break;
    
    case MENU_AP_STATUS:
      if (selectedMenuItem == 0) {
        // Users
        currentSection = MENU_AP_USERS;
        selectedMenuItem = 0;
        menuStartPosition = 0;
        updateAPClients();
      } else if (selectedMenuItem == 1) {
        // Shuffle IP
        shuffleIP();
      } else if (selectedMenuItem == 2) {
        // Back to Wi-Fi
        currentSection = MENU_WIFI;
        selectedMenuItem = 0;
        menuStartPosition = 0;
      }
      break;
    
    case MENU_AP_USERS:
      if (selectedMenuItem < apClients.size()) {
        // Выбираем пользователя и переходим в меню пользователя
        selectedAPUser = selectedMenuItem;
        currentSection = MENU_AP_USER_MENU;
        selectedMenuItem = 0;
        menuStartPosition = 0;
      } else if (selectedMenuItem == apClients.size()) {
        // Back to AP Status
        currentSection = MENU_AP_STATUS;
        selectedMenuItem = 0;
        menuStartPosition = 0;
      }
      break;

    case MENU_AP_USER_MENU:
      switch (selectedMenuItem) {
        case 0: // Info
          currentSection = MENU_AP_USER_INFO;
          selectedMenuItem = 0;
          break;
          
        case 1: // Sniff
          currentSection = MENU_AP_USER_SNIFF;
          selectedMenuItem = 0;
          break;
          
        case 2: // Block/Unblock
          if (selectedAPUser >= 0 && selectedAPUser < apClients.size()) {
            // Переключаем блокировку
            char macStr[18];
            sprintf(macStr, "%02X:%02X:%02X:%02X:%02X:%02X",
                    apClients[selectedAPUser].mac[0],
                    apClients[selectedAPUser].mac[1],
                    apClients[selectedAPUser].mac[2],
                    apClients[selectedAPUser].mac[3],
                    apClients[selectedAPUser].mac[4],
                    apClients[selectedAPUser].mac[5]);
            
            if (apClients[selectedAPUser].blocked) {
              // Разблокируем
              networkTools.unblockIP(apClients[selectedAPUser].ip.toString());
              for (auto it = blockedMACs.begin(); it != blockedMACs.end(); ++it) {
                if (*it == macStr) {
                  blockedMACs.erase(it);
                  break;
                }
              }
            } else {
              // Блокируем
              networkTools.blockIP(apClients[selectedAPUser].ip.toString());
              blockedMACs.push_back(macStr);
            }
            apClients[selectedAPUser].blocked = !apClients[selectedAPUser].blocked;
            saveBlockedMACs();
          }
          break;
          
        case 3: // Back to Users
          currentSection = MENU_AP_USERS;
          selectedMenuItem = selectedAPUser;
          menuStartPosition = 0;
          break;
      }
      break;

    case MENU_AP_USER_SNIFF:
        if (selectedMenuItem == 0) {
          // Start/Stop Sniffing
          if (isSniffing) {
            stopPacketSniffing();
          } else {
            startPacketSniffing(selectedAPUser);
          }
        } else if (selectedMenuItem == 1) {
          // Back to User Menu
          currentSection = MENU_AP_USER_MENU;
          selectedMenuItem = 1;
          if (isSniffing) {
            stopPacketSniffing();
          }
        }
      break;
    
    case MENU_AP_USER_INFO:
      if (selectedMenuItem == 0) {
        // Back to User Menu
        currentSection = MENU_AP_USER_MENU;
        selectedMenuItem = 0;
      }
      break;

    
    case MENU_AP_MODE_SELECT:
      if (selectedMenuItem >= 0 && selectedMenuItem < AP_MODE_ITEMS_COUNT - 1) {
        // Изменение режима AP
        apConfig.mode = (APMode)selectedMenuItem;
        updateAccessPointMode();
        currentSection = MENU_AP_OPTIONS;
        selectedMenuItem = 0;
        menuStartPosition = 0;
      } else if (selectedMenuItem == AP_MODE_ITEMS_COUNT - 1) {
        // Возврат в меню AP Options
        currentSection = MENU_AP_OPTIONS;
        selectedMenuItem = 0;
        menuStartPosition = 0;
      }
      break;
    
    case MENU_WIFI_SCAN:
      if (!isScanningWifi) {
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
          while (true) {
            M5.update();
            if (M5.BtnA.wasPressed() || M5.BtnB.wasPressed() || M5.BtnC.wasPressed()) {
              break;
            }
            delay(50);
          }
        } else {
          // Запускаем сканирование
          scanWiFiNetworks();
        }
      }
      break;
    
    case MENU_WIFI_SAVED:
      if (selectedMenuItem < savedNetworks.size()) {
        // Подключаемся к выбранной сети
        connectToSavedNetwork(selectedMenuItem);
      } else {
        // Возврат в главное меню
        currentSection = MENU_MAIN;
        selectedMenuItem = 0;
        menuStartPosition = 0;
      }
      break;
    
    case MENU_KVM_OPTIONS: {
      const auto& pins = kvmModule.getPins();
      if (selectedMenuItem >= 0 && selectedMenuItem < pins.size()) {
        // Проверяем, нажат ли импульс
        auto touchPoint = M5.Touch.getTouchPointRaw();
        if (touchPoint.x > M5.Lcd.width() - 50) { // Если касание в районе кнопки [P]
          kvmModule.pulsePin(selectedMenuItem, 500); // Отправляем импульс
        } else {
          // Обычное переключение пина
          kvmModule.togglePin(selectedMenuItem);
        }
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
    
    case MENU_KVM_MONITOR: {
      const auto& kvmPins = kvmModule.getPins();
      if (selectedMenuItem >= 2 && selectedMenuItem < 2 + kvmPins.size()) {
        // Переключаем пин
        kvmModule.togglePin(selectedMenuItem - 2);
      }
      break;
    }
    
    case MENU_IR_CONTROL:
      // Возврат в главное меню
      currentSection = MENU_MAIN;
      selectedMenuItem = 0;
      menuStartPosition = 0;
      break;
    
    case MENU_DEVICE_SETTINGS:
      if (selectedMenuItem == 0) {
        // Brightness
        deviceSettings.brightness = (deviceSettings.brightness + 20) % 120;
        if (deviceSettings.brightness > 100) deviceSettings.brightness = 20;
        M5.Lcd.setBrightness(deviceSettings.brightness);
        saveDeviceSettings();
      } else if (selectedMenuItem == 1) {
        // Sleep Timeout
        int timeouts[] = {0, 30, 60, 120, 300, 600};
        int currentIndex = 0;
        
        for (int i = 0; i < 6; i++) {
          if (deviceSettings.sleepTimeout == timeouts[i]) {
            currentIndex = i;
            break;
          }
        }
        
        currentIndex = (currentIndex + 1) % 6;
        deviceSettings.sleepTimeout = timeouts[currentIndex];
        saveDeviceSettings();
      } else if (selectedMenuItem == 2) {
        // Device ID
        M5.Lcd.fillScreen(BLACK);
        M5.Lcd.setCursor(0, 0);
        M5.Lcd.println("Device ID");
        M5.Lcd.println("-----------------");
        M5.Lcd.println(deviceSettings.deviceId);
        M5.Lcd.println("\nUse web interface to change");
        M5.Lcd.println("Device ID");
        
        delay(3000);
      } else if (selectedMenuItem == 3) {
        // Display Rotation
        deviceSettings.rotateDisplay = !deviceSettings.rotateDisplay;
        M5.Lcd.setRotation(deviceSettings.rotateDisplay ? 1 : 3);
        saveDeviceSettings();
      } else if (selectedMenuItem == 4) {
        // Volume
        deviceSettings.volume = (deviceSettings.volume + 20) % 120;
        if (deviceSettings.volume > 100) deviceSettings.volume = 0;
        saveDeviceSettings();
        
        // Воспроизводим тестовый звук
        if (deviceSettings.volume > 0) {
          uint8_t targetVolume = map(deviceSettings.volume, 0, 100, 0, 255);
          M5.Speaker.setVolume(targetVolume);
          M5.Speaker.tone(1000, 100);
          delay(100);
          M5.Speaker.stop();
        }
      } else if (selectedMenuItem == 5) {
        // Invert KVM Pins
        deviceSettings.invertPins = !deviceSettings.invertPins;
        globalDeviceSettings.invertPins = deviceSettings.invertPins;
        saveDeviceSettings();
        // Не нужно обновлять физическое состояние пинов при визуальной инверсии
      } else if (selectedMenuItem == 6) {
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

// Сканирование WiFi сетей
void scanWiFiNetworks() {
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setCursor(0, 0);
  M5.Lcd.println("Scanning WiFi...");
  
  networks.clear();
  isScanningWifi = true;
  scanResultsReady = false;
  
  WiFi.scanNetworks(true, false, false, 300); // Асинхронное сканирование
}

// Выполнение диагностики сети
void performNetworkDiagnostics() {
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
  doc["invertPins"] = deviceSettings.invertPins;
  
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
  if (doc.containsKey("invertPins")) {
    deviceSettings.invertPins = doc["invertPins"];
  }
  
  // Синхронизируем глобальные настройки
  globalDeviceSettings = deviceSettings;
}

// Сохранение списка сохраненных сетей
void saveSavedNetworks() {
  DynamicJsonDocument doc(4096);
  
  JsonArray networksArray = doc.createNestedArray("networks");
  for (const auto& network : savedNetworks) {
    JsonObject netObj = networksArray.createNestedObject();
    netObj["ssid"] = network.ssid;
    netObj["password"] = network.password;
  }
  
  File configFile = LittleFS.open("/saved_networks.json", "w");
  if (!configFile) {
    return;
  }
  
  serializeJson(doc, configFile);
  configFile.close();
}

// Загрузка списка сохраненных сетей
void loadSavedNetworks() {
  if (!LittleFS.exists("/saved_networks.json")) {
    return;
  }
  
  File configFile = LittleFS.open("/saved_networks.json", "r");
  if (!configFile) {
    return;
  }
  
  DynamicJsonDocument doc(4096);
  DeserializationError error = deserializeJson(doc, configFile);
  configFile.close();
  
  if (error) {
    return;
  }
  
  savedNetworks.clear();
  
  if (doc.containsKey("networks")) {
    JsonArray networksArray = doc["networks"];
    for (JsonObject netObj : networksArray) {
      SavedNetwork network;
      network.ssid = netObj["ssid"].as<String>();
      network.password = netObj["password"].as<String>();
      savedNetworks.push_back(network);
    }
  }
}

// Функция получения информации о подключенных клиентах
void updateAPClients() {
  apClients.clear();
  
  wifi_sta_list_t stationList;
  tcpip_adapter_sta_list_t adapterList;
  
  if (WiFi.softAPgetStationNum() == 0) {
    return;
  }
  
  esp_wifi_ap_get_sta_list(&stationList);
  tcpip_adapter_get_sta_list(&stationList, &adapterList);
  
  for (int i = 0; i < adapterList.num; i++) {
    APClient client;
    client.ip = IPAddress(adapterList.sta[i].ip.addr);
    memcpy(client.mac, adapterList.sta[i].mac, 6);
    client.lastSeen = millis();
    client.totalBytes = 0;
    client.lastPacket = "";
    
    // Проверяем, заблокирован ли MAC
    char macStr[18];
    sprintf(macStr, "%02X:%02X:%02X:%02X:%02X:%02X",
            client.mac[0], client.mac[1], client.mac[2],
            client.mac[3], client.mac[4], client.mac[5]);
    
    client.blocked = false;
    for (const auto& blockedMAC : blockedMACs) {
      if (blockedMAC == macStr) {
        client.blocked = true;
        break;
      }
    }
    
    // Проверяем, заблокирован ли IP
    if (client.blocked) {
      // Используем более низкоуровневый API ESP-IDF
      wifi_sta_list_t wifi_sta_list;
      tcpip_adapter_sta_list_t adapter_sta_list;
      
      esp_wifi_ap_get_sta_list(&wifi_sta_list);
      tcpip_adapter_get_sta_list(&wifi_sta_list, &adapter_sta_list);
      
      // Находим AID для конкретного MAC-адреса
      for (int j = 0; j < adapter_sta_list.num; j++) {
        if (memcmp(adapter_sta_list.sta[j].mac, adapterList.sta[i].mac, 6) == 0) {
          // Используем правильный AID для деавторизации
          for (int k = 0; k < wifi_sta_list.num; k++) {
            if (memcmp(wifi_sta_list.sta[k].mac, adapterList.sta[i].mac, 6) == 0) {
              // В структуре wifi_sta_list нужно найти поле aid
              // Если его нет, используем индекс + 1 как AID
              esp_wifi_deauth_sta(k + 1);
              break;
            }
          }
          break;
        }
      }
    }
    
    apClients.push_back(client);
  }
}

// Функция проверки MAC-адреса на блокировку
bool isMACBlocked(const uint8_t* mac) {
  char macStr[18];
  sprintf(macStr, "%02X:%02X:%02X:%02X:%02X:%02X",
          mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  
  for (const auto& blockedMAC : blockedMACs) {
    if (blockedMAC == macStr) {
      return true;
    }
  }
  return false;
}

// Запуск сниффинга пакетов для пользователя
void startPacketSniffing(int clientIndex) {
  if (clientIndex >= 0 && clientIndex < apClients.size()) {
    isSniffing = true;
    
    // Настраиваем WiFi для сниффинга
    esp_wifi_set_promiscuous(false);
    esp_wifi_set_promiscuous_rx_cb(&promiscuous_rx_callback);
    esp_wifi_set_promiscuous(true);
    
    // Устанавливаем фильтр для конкретного пользователя
    currentSniffingClient = clientIndex;
    currentSniffingMAC = apClients[clientIndex].mac;
    
    apClients[clientIndex].lastPacket = "Сниффинг активен...";
  }
}

// Остановка сниффинга пакетов
void stopPacketSniffing() {
  isSniffing = false;
  currentSniffingClient = -1;
  esp_wifi_set_promiscuous(false);
}

// Колбэк для обработки перехваченных пакетов
void promiscuous_rx_callback(void* buf, wifi_promiscuous_pkt_type_t type) {
  if (!isSniffing || currentSniffingClient < 0) return;
  
  wifi_promiscuous_pkt_t* pkt = (wifi_promiscuous_pkt_t*)buf;
  wifi_pkt_rx_ctrl_t ctrl = pkt->rx_ctrl;
  
  // Извлекаем MAC адреса
  uint8_t* payload = pkt->payload;
  uint8_t* dest_mac = payload + 4;  // Destination MAC
  uint8_t* source_mac = payload + 10; // Source MAC
  
  // Проверяем, соответствует ли MAC адрес нашему клиенту
  bool match = true;
  for (int i = 0; i < 6; i++) {
    if (source_mac[i] != currentSniffingMAC[i]) {
      match = false;
      break;
    }
  }
  
  if (match) {
    // Обновляем информацию о трафике
    apClients[currentSniffingClient].totalBytes += ctrl.sig_len;
    
    // Создаем запись о пакете
    SniffedPacket packet;
    
    char srcMAC[18], dstMAC[18];
    sprintf(srcMAC, "%02X:%02X:%02X:%02X:%02X:%02X",
            source_mac[0], source_mac[1], source_mac[2],
            source_mac[3], source_mac[4], source_mac[5]);
    sprintf(dstMAC, "%02X:%02X:%02X:%02X:%02X:%02X",
            dest_mac[0], dest_mac[1], dest_mac[2],
            dest_mac[3], dest_mac[4], dest_mac[5]);
    
    packet.sourceMAC = String(srcMAC);
    packet.destMAC = String(dstMAC);
    packet.type = type == WIFI_PKT_MGMT ? "MGMT" : type == WIFI_PKT_DATA ? "DATA" : "CTRL";
    packet.size = ctrl.sig_len;
    packet.rssi = ctrl.rssi;
    packet.timestamp = millis();
    
    // Добавляем в буфер
    if (packetBuffer.size() >= MAX_PACKET_BUFFER) {
      packetBuffer.erase(packetBuffer.begin());  // Удаляем самый старый
    }
    packetBuffer.push_back(packet);
    
    // Обновляем информацию для отображения
    char packetInfo[100];
    snprintf(packetInfo, sizeof(packetInfo), "To:%s Type:%s Size:%d", 
             dstMAC, packet.type.c_str(), ctrl.sig_len);
    
    apClients[currentSniffingClient].lastPacket = String(packetInfo);
    apClients[currentSniffingClient].lastSeen = millis();
  }
}

// Функция сохранения заблокированных MAC
void saveBlockedMACs() {
  DynamicJsonDocument doc(2048);
  
  JsonArray blockedArray = doc.createNestedArray("blocked");
  for (const auto& mac : blockedMACs) {
    blockedArray.add(mac);
  }
  
  File file = LittleFS.open("/blocked_macs.json", "w");
  if (!file) {
    Serial.println("Failed to open blocked_macs.json for writing");
    return;
  }
  
  serializeJson(doc, file);
  file.close();
}

// Функция загрузки заблокированных MAC
void loadBlockedMACs() {
  if (!LittleFS.exists("/blocked_macs.json")) {
    return;
  }
  
  File file = LittleFS.open("/blocked_macs.json", "r");
  if (!file) {
    return;
  }
  
  DynamicJsonDocument doc(2048);
  DeserializationError error = deserializeJson(doc, file);
  file.close();
  
  if (error) {
    return;
  }
  
  blockedMACs.clear();
  
  if (doc.containsKey("blocked")) {
    JsonArray blockedArray = doc["blocked"];
    for (JsonVariant v : blockedArray) {
      blockedMACs.push_back(v.as<String>());
    }
  }
}

// Функция смены IP адреса (реальная реализация)
void shuffleIP() {
  if (WiFi.status() == WL_CONNECTED) {
    // Сохраняем текущие учетные данные
    String currentSSID = WiFi.SSID();
    String currentPassword = "";
    
    // Находим пароль в сохраненных сетях
    for (const auto& network : savedNetworks) {
      if (network.ssid == currentSSID) {
        currentPassword = network.password;
        break;
      }
    }
    
    // Отключаемся от сети
    WiFi.disconnect(true);
    delay(1000);
    
    // Настраиваем новый MAC адрес
    uint8_t newMAC[6];
    esp_read_mac(newMAC, ESP_MAC_WIFI_STA);
    
    // Изменяем последний байт MAC адреса
    newMAC[5] = random(0, 255);
    
    // Применяем новый MAC адрес
    esp_wifi_set_mac(WIFI_IF_STA, newMAC);
    
    // Подключаемся заново с новым MAC адресом
    WiFi.begin(currentSSID.c_str(), currentPassword.c_str());
    
    // Показываем уведомление
    M5.Lcd.fillScreen(BLACK);
    M5.Lcd.setCursor(0, 0);
    M5.Lcd.println("Shuffling IP...");
    M5.Lcd.println("New MAC: ");
    
    char macStr[18];
    sprintf(macStr, "%02X:%02X:%02X:%02X:%02X:%02X",
            newMAC[0], newMAC[1], newMAC[2],
            newMAC[3], newMAC[4], newMAC[5]);
    M5.Lcd.println(macStr);
    M5.Lcd.println("Please wait...");
    
    // Ждем подключения
    int timeout = 0;
    while (WiFi.status() != WL_CONNECTED && timeout < 20) {
      delay(500);
      M5.Lcd.print(".");
      timeout++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
      M5.Lcd.println();
      M5.Lcd.println("New IP: ");
      M5.Lcd.println(WiFi.localIP());
    } else {
      M5.Lcd.println();
      M5.Lcd.println("Connection failed!");
    }
    
    delay(3000);
  }
}

// Обработчик подключения станции (реальная реализация)
void onStationConnected(WiFiEvent_t event, WiFiEventInfo_t info) {
  auto& sta = info.wifi_ap_staconnected;
  Serial.printf("Station connected: %02X:%02X:%02X:%02X:%02X:%02X\n",
                sta.mac[0], sta.mac[1], sta.mac[2],
                sta.mac[3], sta.mac[4], sta.mac[5]);

  if (isMACBlocked(sta.mac)) {
    Serial.println("Blocked MAC detected, disconnecting...");
    // В ESP32 Arduino нет прямого способа отключить конкретного клиента
    // Можно использовать альтернативный подход
    esp_wifi_disconnect();
  }
}
