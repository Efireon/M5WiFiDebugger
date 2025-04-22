#ifndef NETWORK_TOOLS_H
#define NETWORK_TOOLS_H

#include <WiFi.h>
#include <vector>
#include <M5StickCPlus2.h>
#include <ESPmDNS.h>
#include <ESP32Ping.h>

// Структура для хранения результатов пинга
struct PingResult {
  bool success;
  int packetsSent;
  int packetsReceived;
  float lossPercentage;
  float averageTime;
  String targetHost;
  String targetIP;
};

// Структура для хранения найденных устройств
struct FoundDevice {
  String ipAddress;
  bool reachable;
  float responseTime;
  String hostname;
};

// Структура для порта
struct PortInfo {
  int port;
  bool open;
  String service;
};

// Класс для хранения заблокированных IP адресов
class BlockedIPs {
private:
  std::vector<String> blockedIPs;

public:
  // Добавить IP в черный список
  bool add(const String& ip) {
    // Проверяем, не добавлен ли уже этот IP
    for (const auto& blockedIP : blockedIPs) {
      if (blockedIP == ip) {
        return false; // IP уже в списке
      }
    }
    
    // Добавляем IP
    blockedIPs.push_back(ip);
    return true;
  }
  
  // Удалить IP из черного списка
  bool remove(const String& ip) {
    for (auto it = blockedIPs.begin(); it != blockedIPs.end(); ++it) {
      if (*it == ip) {
        blockedIPs.erase(it);
        return true;
      }
    }
    return false; // IP не найден
  }
  
  // Проверить, заблокирован ли IP
  bool isBlocked(const String& ip) const {
    for (const auto& blockedIP : blockedIPs) {
      if (blockedIP == ip) {
        return true;
      }
    }
    return false;
  }
  
  // Получить список всех заблокированных IP
  const std::vector<String>& getAll() const {
    return blockedIPs;
  }
  
  // Очистить список
  void clear() {
    blockedIPs.clear();
  }
  
  // Количество заблокированных IP
  size_t count() const {
    return blockedIPs.size();
  }
};

// Класс для сетевых инструментов
class NetworkTools {
private:
  BlockedIPs blockedIPs;
  
  // Проверка валидности IP
  bool isValidIP(const String& ip) {
    IPAddress testIP;
    return testIP.fromString(ip);
  }
  
  // Получение имени хоста по IP
  String getHostname(const String& ip) {
    if (!isValidIP(ip)) {
      return "Invalid IP";
    }
    
    // Преобразуем строку IP в объект IPAddress
    IPAddress ipAddr;
    ipAddr.fromString(ip);
    
    // Вместо метода lookup используем resolve, который приемлем для ESP32 mDNS
    // Или можно полностью пропустить получение имени хоста через mDNS
    String hostname = "";
    
    // Для ESP32 MDNSResponder не имеет метода lookup
    // Просто используем IP-адрес как имя хоста
    hostname = ip; // Временное решение
    
    return hostname;
  }

public:
  // Конструктор
  NetworkTools() {
    // Инициализация mDNS
    if (!MDNS.begin("m5stick")) {
      M5.Lcd.println("Failed to start mDNS");
    }
  }
  
  // Пинг хоста/IP
  PingResult pingHost(const String& target, int pingCount = 5) {
    PingResult result;
    result.success = false;
    result.packetsSent = pingCount;
    result.packetsReceived = 0;
    result.lossPercentage = 100.0;
    result.averageTime = 0.0;
    result.targetHost = target;
    
    // Проверяем, является ли target IP-адресом или именем хоста
    IPAddress targetIP;
    if (targetIP.fromString(target)) {
      // Это IP-адрес
      result.targetIP = target;
    } else {
      // Пытаемся разрешить имя хоста
      if (WiFi.hostByName(target.c_str(), targetIP)) {
        result.targetIP = targetIP.toString();
      } else {
        // Не удалось разрешить имя хоста
        return result;
      }
    }
    
    // Проверяем, не заблокирован ли этот IP
    if (blockedIPs.isBlocked(result.targetIP)) {
      return result; // IP заблокирован
    }
    
    // Выполняем пинг
    result.success = Ping.ping(targetIP, pingCount);
    
    if (result.success) {
      // ESP32Ping не имеет метода packetsReceived, будем вычислять по потере пакетов
      // result.packetsReceived = Ping.packetsReceived();
      
      // Получаем время отклика
      result.averageTime = Ping.averageTime();
      
      // Вычисляем количество полученных пакетов на основе успешного пинга
      // Предполагаем, что все пакеты дошли, если пинг успешен
      result.packetsReceived = pingCount;
      result.lossPercentage = 0.0;
    }
    
    return result;
  }
  
  // Сканирование диапазона IP-адресов
  std::vector<FoundDevice> scanIPRange(const String& startIP, const String& endIP) {
    std::vector<FoundDevice> devices;
    
    IPAddress start, end;
    if (!start.fromString(startIP) || !end.fromString(endIP)) {
      // Неверный формат IP
      return devices;
    }
    
    // Преобразуем IP-адреса в 32-битные целые числа для удобства
    uint32_t startNum = (uint32_t)start;
    uint32_t endNum = (uint32_t)end;
    
    // Ограничиваем количество сканируемых адресов для безопасности
    uint32_t maxAddresses = 256; // Максимальное количество адресов для сканирования
    if (endNum - startNum > maxAddresses) {
      endNum = startNum + maxAddresses;
    }
    
    // Сканируем диапазон
    for (uint32_t ipNum = startNum; ipNum <= endNum; ipNum++) {
      IPAddress currentIP(ipNum);
      String currentIPStr = currentIP.toString();
      
      // Проверяем, не заблокирован ли IP
      if (blockedIPs.isBlocked(currentIPStr)) {
        continue; // Пропускаем заблокированный IP
      }
      
      // Пингуем устройство
      bool reachable = Ping.ping(currentIP, 1); // Пинг с одним пакетом
      
      // Создаем запись об устройстве
      FoundDevice device;
      device.ipAddress = currentIPStr;
      device.reachable = reachable;
      device.hostname = getHostname(currentIPStr);
      
      if (reachable) {
        device.responseTime = Ping.averageTime();
      } else {
        device.responseTime = 0;
      }
      
      devices.push_back(device);
    }
    
    return devices;
  }
  
  // Сканирование портов
  std::vector<PortInfo> scanPorts(const String& targetIP, int startPort, int endPort) {
    std::vector<PortInfo> ports;
    
    // Ограничиваем диапазон портов для безопасности
    if (startPort < 1) startPort = 1;
    if (endPort > 65535) endPort = 65535;
    if (endPort - startPort > 1000) endPort = startPort + 1000; // Максимум 1000 портов
    
    // Проверяем, не заблокирован ли IP
    if (blockedIPs.isBlocked(targetIP)) {
      return ports; // IP заблокирован
    }
    
    // Создаем TCP клиент
    WiFiClient client;
    client.setTimeout(300); // Таймаут 300 мс
    
    // Сканируем порты
    for (int port = startPort; port <= endPort; port++) {
      PortInfo portInfo;
      portInfo.port = port;
      
      // Пытаемся подключиться к порту
      portInfo.open = client.connect(targetIP.c_str(), port);
      
      // Закрываем соединение
      client.stop();
      
      // Определяем известные сервисы
      if (port == 21) portInfo.service = "FTP";
      else if (port == 22) portInfo.service = "SSH";
      else if (port == 23) portInfo.service = "Telnet";
      else if (port == 25) portInfo.service = "SMTP";
      else if (port == 53) portInfo.service = "DNS";
      else if (port == 80) portInfo.service = "HTTP";
      else if (port == 110) portInfo.service = "POP3";
      else if (port == 143) portInfo.service = "IMAP";
      else if (port == 443) portInfo.service = "HTTPS";
      else if (port == 3306) portInfo.service = "MySQL";
      else if (port == 3389) portInfo.service = "RDP";
      else portInfo.service = "Unknown";
      
      // Добавляем порт в список, если он открыт
      if (portInfo.open) {
        ports.push_back(portInfo);
      }
    }
    
    return ports;
  }
  
  // Добавить IP в черный список
  bool blockIP(const String& ip) {
    if (!isValidIP(ip)) {
      return false; // Некорректный IP
    }
    
    return blockedIPs.add(ip);
  }
  
  // Удалить IP из черного списка
  bool unblockIP(const String& ip) {
    return blockedIPs.remove(ip);
  }
  
  // Получить список всех заблокированных IP
  const std::vector<String>& getBlockedIPs() const {
    return blockedIPs.getAll();
  }
  
  // Проверить, заблокирован ли IP
  bool isIPBlocked(const String& ip) const {
    return blockedIPs.isBlocked(ip);
  }
  
  // Получить локальный IP адрес устройства
  String getLocalIP() const {
    return WiFi.localIP().toString();
  }
  
  // Получить IP адрес точки доступа
  String getAPIP() const {
    return WiFi.softAPIP().toString();
  }
  
  // Получить MAC адрес устройства
  String getMAC() const {
    return WiFi.macAddress();
  }
};

#endif // NETWORK_TOOLS_H