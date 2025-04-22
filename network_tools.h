#ifndef NETWORK_TOOLS_H
#define NETWORK_TOOLS_H

#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <vector>
#include <ESPping.h>
#include <IPAddress.h>

// Структура для хранения результатов пинга
struct PingResult {
  String host;
  bool success;
  int packetsSent;
  int packetsReceived;
  float avgTime;
};

// Структура для хранения найденных устройств
struct FoundDevice {
  IPAddress ip;
  String mac;
  bool reachable;
  int responseTime;
};

// Структура для хранения заблокированных IP адресов
struct BlockedIP {
  IPAddress ip;
  String reason;
  unsigned long timestamp;
};

// Класс для сетевых инструментов
class NetworkTools {
private:
  std::vector<BlockedIP> blockedIPs;
  AsyncWebServer* server;
  bool isAPMode;

public:
  NetworkTools(AsyncWebServer* server);
  
  // Методы для API
  void setupAPI();
  
  // Пинг хоста
  PingResult pingHost(const String& host, int count = 5);
  
  // Сканирование диапазона IP
  std::vector<FoundDevice> scanIPRange(const String& startIP, const String& endIP);
  
  // Управление блокировкой IP (только для режима AP)
  bool blockIP(const IPAddress& ip, const String& reason);
  bool unblockIP(const IPAddress& ip);
  std::vector<BlockedIP> getBlockedIPs();
  
  // Сохранение/загрузка настроек
  void saveConfig(fs::FS &fs);
  void loadConfig(fs::FS &fs);
  
  // Установка режима AP
  void setAPMode(bool isAP);
};

// Реализация класса NetworkTools

NetworkTools::NetworkTools(AsyncWebServer* server) {
  this->server = server;
  this->isAPMode = false;
}

void NetworkTools::setupAPI() {
  // API для пинга
  server->on("/api/network/ping", HTTP_POST, [this](AsyncWebServerRequest *request) {
    if (!request->hasParam("host", true)) {
      request->send(400, "application/json", "{\"error\":\"Missing host parameter\"}");
      return;
    }
    
    String host = request->getParam("host", true)->value();
    int count = 5;
    
    if (request->hasParam("count", true)) {
      count = request->getParam("count", true)->value().toInt();
      if (count < 1) count = 1;
      if (count > 20) count = 20; // Ограничиваем максимальное число пакетов
    }
    
    PingResult result = pingHost(host, count);
    
    DynamicJsonDocument doc(512);
    doc["host"] = result.host;
    doc["success"] = result.success;
    doc["packetsSent"] = result.packetsSent;
    doc["packetsReceived"] = result.packetsReceived;
    doc["avgTime"] = result.avgTime;
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });
  
  // API для сканирования IP
  server->on("/api/network/scan", HTTP_POST, [this](AsyncWebServerRequest *request) {
    if (!request->hasParam("startIP", true) || !request->hasParam("endIP", true)) {
      request->send(400, "application/json", "{\"error\":\"Missing IP range parameters\"}");
      return;
    }
    
    String startIP = request->getParam("startIP", true)->value();
    String endIP = request->getParam("endIP", true)->value();
    
    // Ограничиваем размер диапазона для экономии памяти
    IPAddress start, end;
    if (!start.fromString(startIP) || !end.fromString(endIP)) {
      request->send(400, "application/json", "{\"error\":\"Invalid IP format\"}");
      return;
    }
    
    // Убеждаемся, что диапазон не превышает 254 адресов
    uint32_t startInt = (uint32_t)start;
    uint32_t endInt = (uint32_t)end;
    if (endInt - startInt > 254) {
      request->send(400, "application/json", "{\"error\":\"IP range too large (max 254 addresses)\"}");
      return;
    }
    
    std::vector<FoundDevice> devices = scanIPRange(startIP, endIP);
    
    DynamicJsonDocument doc(4096);
    JsonArray devicesArray = doc.createNestedArray("devices");
    
    for (const auto& device : devices) {
      JsonObject deviceObj = devicesArray.createNestedObject();
      deviceObj["ip"] = device.ip.toString();
      deviceObj["mac"] = device.mac;
      deviceObj["reachable"] = device.reachable;
      deviceObj["responseTime"] = device.responseTime;
    }
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });
  
  // API для блокировки IP
  server->on("/api/network/block", HTTP_POST, [this](AsyncWebServerRequest *request) {
    if (!isAPMode) {
      request->send(400, "application/json", "{\"error\":\"IP blocking is only available in AP mode\"}");
      return;
    }
    
    if (!request->hasParam("ip", true)) {
      request->send(400, "application/json", "{\"error\":\"Missing IP parameter\"}");
      return;
    }
    
    String ipStr = request->getParam("ip", true)->value();
    String reason = "Manual block";
    
    if (request->hasParam("reason", true)) {
      reason = request->getParam("reason", true)->value();
    }
    
    IPAddress ip;
    if (!ip.fromString(ipStr)) {
      request->send(400, "application/json", "{\"error\":\"Invalid IP format\"}");
      return;
    }
    
    bool success = blockIP(ip, reason);
    
    DynamicJsonDocument doc(256);
    doc["success"] = success;
    doc["ip"] = ipStr;
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });
  
  // API для разблокировки IP
  server->on("/api/network/unblock", HTTP_POST, [this](AsyncWebServerRequest *request) {
    if (!isAPMode) {
      request->send(400, "application/json", "{\"error\":\"IP blocking is only available in AP mode\"}");
      return;
    }
    
    if (!request->hasParam("ip", true)) {
      request->send(400, "application/json", "{\"error\":\"Missing IP parameter\"}");
      return;
    }
    
    String ipStr = request->getParam("ip", true)->value();
    
    IPAddress ip;
    if (!ip.fromString(ipStr)) {
      request->send(400, "application/json", "{\"error\":\"Invalid IP format\"}");
      return;
    }
    
    bool success = unblockIP(ip);
    
    DynamicJsonDocument doc(256);
    doc["success"] = success;
    doc["ip"] = ipStr;
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });
  
  // API для получения списка заблокированных IP
  server->on("/api/network/blocked", HTTP_GET, [this](AsyncWebServerRequest *request) {
    std::vector<BlockedIP> ips = getBlockedIPs();
    
    DynamicJsonDocument doc(2048);
    JsonArray ipsArray = doc.createNestedArray("blockedIPs");
    
    for (const auto& ip : ips) {
      JsonObject ipObj = ipsArray.createNestedObject();
      ipObj["ip"] = ip.ip.toString();
      ipObj["reason"] = ip.reason;
      ipObj["timestamp"] = ip.timestamp;
    }
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });
}

PingResult NetworkTools::pingHost(const String& host, int count) {
  PingResult result;
  result.host = host;
  result.success = false;
  result.packetsSent = count;
  result.packetsReceived = 0;
  result.avgTime = 0;
  
  Ping ping;
  IPAddress targetIP;
  
  // Проверяем, является ли host IP-адресом или доменным именем
  if (targetIP.fromString(host)) {
    // Это IP-адрес
    ping.begin(targetIP);
  } else {
    // Это доменное имя, попробуем преобразовать
    if (!WiFi.hostByName(host.c_str(), targetIP)) {
      return result; // Не смогли преобразовать имя в IP
    }
    ping.begin(targetIP);
  }
  
  float totalTime = 0;
  int successCount = 0;
  
  for (int i = 0; i < count; i++) {
    bool pingSuccess = ping.ping();
    if (pingSuccess) {
      successCount++;
      totalTime += ping.averageTime();
    }
    delay(100); // Небольшая пауза между пингами
  }
  
  result.packetsReceived = successCount;
  
  if (successCount > 0) {
    result.success = true;
    result.avgTime = totalTime / successCount;
  }
  
  return result;
}

std::vector<FoundDevice> NetworkTools::scanIPRange(const String& startIP, const String& endIP) {
  std::vector<FoundDevice> devices;
  
  IPAddress start, end;
  if (!start.fromString(startIP) || !end.fromString(endIP)) {
    return devices; // Неверный формат IP
  }
  
  // Преобразуем IP-адреса в целочисленный формат для легкого итерирования
  uint32_t startInt = (uint32_t)start;
  uint32_t endInt = (uint32_t)end;
  
  // Ограничиваем диапазон для экономии памяти
  if (endInt - startInt > 254) {
    endInt = startInt + 254;
  }
  
  for (uint32_t addr = startInt; addr <= endInt; addr++) {
    IPAddress currentIP(addr);
    
    Ping ping;
    ping.begin(currentIP);
    bool reachable = ping.ping(1); // Пинг с одним пакетом
    
    if (reachable) {
      FoundDevice device;
      device.ip = currentIP;
      device.reachable = true;
      device.responseTime = ping.averageTime();
      
      // Попытка получить MAC-адрес (работает только для устройств, которые уже в ARP-таблице)
      device.mac = "Unknown";
      
      devices.push_back(device);
    }
    
    // Небольшая пауза для предотвращения перегрузки
    delay(5);
    
    // Разрешаем обработку событий WiFi
    yield();
  }
  
  return devices;
}

bool NetworkTools::blockIP(const IPAddress& ip, const String& reason) {
  if (!isAPMode) {
    return false; // Блокировка доступна только в режиме AP
  }
  
  // Проверяем, не заблокирован ли уже этот IP
  for (const auto& blockedIP : blockedIPs) {
    if (blockedIP.ip == ip) {
      return false; // IP уже заблокирован
    }
  }
  
  // Добавляем IP в список заблокированных
  BlockedIP newBlock;
  newBlock.ip = ip;
  newBlock.reason = reason;
  newBlock.timestamp = millis();
  
  blockedIPs.push_back(newBlock);
  
  // Здесь должен быть код для добавления правила в брандмауэр
  // Но ESP32 не имеет полноценного брандмауэра, поэтому это просто заглушка
  
  return true;
}

bool NetworkTools::unblockIP(const IPAddress& ip) {
  if (!isAPMode) {
    return false; // Разблокировка доступна только в режиме AP
  }
  
  for (auto it = blockedIPs.begin(); it != blockedIPs.end(); ++it) {
    if (it->ip == ip) {
      blockedIPs.erase(it);
      return true;
    }
  }
  
  return false; // IP не был заблокирован
}

std::vector<BlockedIP> NetworkTools::getBlockedIPs() {
  return blockedIPs;
}

void NetworkTools::saveConfig(fs::FS &fs) {
  DynamicJsonDocument doc(2048);
  JsonArray blockedIPsArray = doc.createNestedArray("blockedIPs");
  
  for (const auto& ip : blockedIPs) {
    JsonObject ipObj = blockedIPsArray.createNestedObject();
    ipObj["ip"] = ip.ip.toString();
    ipObj["reason"] = ip.reason;
    ipObj["timestamp"] = ip.timestamp;
  }
  
  File configFile = fs.open("/network_config.json", "w");
  if (!configFile) {
    return;
  }
  
  serializeJson(doc, configFile);
  configFile.close();
}

void NetworkTools::loadConfig(fs::FS &fs) {
  if (!fs.exists("/network_config.json")) {
    return;
  }
  
  File configFile = fs.open("/network_config.json", "r");
  if (!configFile) {
    return;
  }
  
  DynamicJsonDocument doc(2048);
  DeserializationError error = deserializeJson(doc, configFile);
  configFile.close();
  
  if (error) {
    return;
  }
  
  blockedIPs.clear();
  
  if (doc.containsKey("blockedIPs")) {
    JsonArray blockedIPsArray = doc["blockedIPs"];
    
    for (JsonObject ipObj : blockedIPsArray) {
      IPAddress ip;
      if (ip.fromString(ipObj["ip"].as<String>())) {
        BlockedIP blockedIP;
        blockedIP.ip = ip;
        blockedIP.reason = ipObj["reason"].as<String>();
        blockedIP.timestamp = ipObj["timestamp"].as<unsigned long>();
        
        blockedIPs.push_back(blockedIP);
      }
    }
  }
}

void NetworkTools::setAPMode(bool isAP) {
  isAPMode = isAP;
}

#endif // NETWORK_TOOLS_H