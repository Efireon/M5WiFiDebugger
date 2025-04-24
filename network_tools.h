#ifndef NETWORK_TOOLS_H
#define NETWORK_TOOLS_H

#include <WiFi.h>
#include <vector>
#include <M5StickCPlus2.h>
#include <ESPmDNS.h>
#include <ESP32Ping.h>
#include <WiFiClient.h>

// Структура для результата пинга
struct PingResult {
  bool success;
  String target;
  IPAddress ip;
  float avg_time;
  float loss;
  String error;
};

// Структура для результата сканирования сети
struct ScanResult {
  IPAddress ip;
  bool active;
  float response_time;
};

// Структура для результата сканирования портов
struct PortScanResult {
  int port;
  bool open;
  String service;
};

// Класс для сетевых инструментов
class NetworkTools {
private:
  std::vector<IPAddress> blockedIPs;
  
  // Определение сервиса по порту
  String identifyService(int port) {
    switch (port) {
      case 20: return "FTP-data";
      case 21: return "FTP";
      case 22: return "SSH";
      case 23: return "Telnet";
      case 25: return "SMTP";
      case 53: return "DNS";
      case 80: return "HTTP";
      case 110: return "POP3";
      case 143: return "IMAP";
      case 443: return "HTTPS";
      case 3306: return "MySQL";
      case 3389: return "RDP";
      case 5432: return "PostgreSQL";
      case 8080: return "HTTP-alt";
      case 8443: return "HTTPS-alt";
      default: return "";
    }
  }
  
  // Разбор диапазона IP
  bool parseIPRange(const String& range, IPAddress& startIP, IPAddress& endIP) {
    int dashIndex = range.indexOf('-');
    if (dashIndex == -1) {
      return false;
    }
    
    String startStr = range.substring(0, dashIndex);
    String endStr = range.substring(dashIndex + 1);
    
    startStr.trim();
    endStr.trim();
    
    return startIP.fromString(startStr) && endIP.fromString(endStr);
  }

public:
  NetworkTools() {}
  
  // Пинг хоста
  PingResult ping(const String& host, int count = 4) {
    PingResult result;
    result.target = host;
    result.success = false;
    result.avg_time = 0;
    result.loss = 100;
    
    // Попытка определить IP-адрес
    if (!result.ip.fromString(host)) {
      // Если это не IP, пробуем разрешить имя хоста
      if (!WiFi.hostByName(host.c_str(), result.ip)) {
        result.error = "Failed to resolve hostname";
        return result;
      }
    }
    
    // Выполняем пинг
    result.success = Ping.ping(result.ip, count);
    
    if (result.success) {
      result.avg_time = Ping.averageTime();
      // Вычисляем потери пакетов
      result.loss = 0; // ESP32Ping не предоставляет информацию о потерях
    } else {
      result.error = "Host unreachable";
    }
    
    return result;
  }
  
  // Сканирование сети
  std::vector<ScanResult> scanNetwork(const String& range) {
    std::vector<ScanResult> results;
    
    IPAddress startIP, endIP;
    if (!parseIPRange(range, startIP, endIP)) {
      return results; // Пустой результат при ошибке разбора
    }
    
    // Ограничиваем диапазон для предотвращения слишком долгого сканирования
    uint32_t startNum = startIP;
    uint32_t endNum = endIP;
    uint32_t maxHosts = 255;
    
    if (endNum - startNum > maxHosts) {
      endNum = startNum + maxHosts;
    }
    
    for (uint32_t ipNum = startNum; ipNum <= endNum; ipNum++) {
      IPAddress currentIP(ipNum);
      ScanResult result;
      result.ip = currentIP;
      
      // Пингуем хост с таймаутом
      if (Ping.ping(currentIP, 1)) {
        result.active = true;
        result.response_time = Ping.averageTime();
      } else {
        result.active = false;
        result.response_time = 0;
      }
      
      results.push_back(result);
      
      // Даём возможность обработать другие задачи
      delay(1);
      yield();
    }
    
    return results;
  }
  
  // Сканирование портов
  std::vector<PortScanResult> scanPorts(const String& host, int startPort, int endPort) {
    std::vector<PortScanResult> results;
    
    IPAddress ip;
    if (!ip.fromString(host) && !WiFi.hostByName(host.c_str(), ip)) {
      return results; // Не удалось разрешить хост
    }
    
    // Ограничиваем количество портов
    if (startPort < 1) startPort = 1;
    if (endPort > 65535) endPort = 65535;
    if (endPort - startPort > 100) endPort = startPort + 100;
    
    WiFiClient client;
    
    for (int port = startPort; port <= endPort; port++) {
      PortScanResult result;
      result.port = port;
      result.open = false;
      
      // Пытаемся подключиться с коротким таймаутом
      client.setTimeout(300);
      
      if (client.connect(ip, port)) {
        result.open = true;
        result.service = identifyService(port);
        client.stop();
      }
      
      if (result.open) {
        results.push_back(result);
      }
      
      // Даём возможность обработать другие задачи
      delay(1);
      yield();
    }
    
    return results;
  }
  
  // Блокировка IP
  bool blockIP(const String& ipStr) {
    IPAddress ip;
    if (!ip.fromString(ipStr)) {
      return false;
    }
    
    // Проверяем, не заблокирован ли уже IP
    for (const auto& blockedIP : blockedIPs) {
      if (blockedIP == ip) {
        return false; // Уже заблокирован
      }
    }
    
    blockedIPs.push_back(ip);
    return true;
  }
  
  // Разблокировка IP
  bool unblockIP(const String& ipStr) {
    IPAddress ip;
    if (!ip.fromString(ipStr)) {
      return false;
    }
    
    for (auto it = blockedIPs.begin(); it != blockedIPs.end(); ++it) {
      if (*it == ip) {
        blockedIPs.erase(it);
        return true;
      }
    }
    
    return false; // IP не был заблокирован
  }
  
  // Получение списка заблокированных IP
  const std::vector<IPAddress>& getBlockedIPs() const {
    return blockedIPs;
  }
  
  // Проверка, заблокирован ли IP
  bool isIPBlocked(const IPAddress& ip) const {
    for (const auto& blockedIP : blockedIPs) {
      if (blockedIP == ip) {
        return true;
      }
    }
    return false;
  }
};

#endif // NETWORK_TOOLS_H