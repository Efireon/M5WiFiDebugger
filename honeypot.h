#ifndef HONEYPOT_H
#define HONEYPOT_H

#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <ArduinoJson.h>
#include <vector>
#include <LittleFS.h>

// Максимальное количество записей в логе
#define MAX_HONEYPOT_LOGS 10

// Структура для хранения информации о соединении
struct HoneypotConnection {
  IPAddress clientIP;
  uint16_t port;
  String requestData;
  unsigned long timestamp;
};

// Класс для режима Honeypot
class Honeypot {
private:
  std::vector<HoneypotConnection> connections;
  AsyncWebServer* server;
  bool active;
  
  // Callback для журналирования соединений
  static void logConnection(AsyncWebServerRequest *request, Honeypot* honeypot);
  
  // Обработчик клиентских соединений
  void handleClientConnection(AsyncClient* client);

public:
  Honeypot(AsyncWebServer* server);
  
  // Запуск/остановка режима Honeypot
  bool start(const String& ssid, int channel = 1);
  void stop();
  
  // Получение журнала соединений
  const std::vector<HoneypotConnection>& getConnections() const;
  
  // Очистка журнала
  void clearLogs();
  
  // Сохранение/загрузка логов
  void saveLogs(fs::FS &fs);
  void loadLogs(fs::FS &fs);
  
  // Настройка API
  void setupAPI();
  
  // Проверка активности
  bool isActive() const;
};

// Реализация класса Honeypot

Honeypot::Honeypot(AsyncWebServer* server) : server(server), active(false) {
  // Ограничиваем размер вектора для экономии памяти
  connections.reserve(MAX_HONEYPOT_LOGS);
}

bool Honeypot::start(const String& ssid, int channel) {
  if (active) {
    return false; // Уже запущен
  }
  
  // Запускаем режим точки доступа без пароля
  if (!WiFi.softAP(ssid.c_str(), "", channel)) {
    return false;
  }
  
  active = true;
  
  // Настраиваем маршруты для обработки всех запросов
  server->onNotFound([this](AsyncWebServerRequest *request) {
    logConnection(request, this);
    
    // Отправляем поддельный ответ, как будто это обычный веб-сервер
    request->send(200, "text/html", "<html><body><h1>Welcome</h1><p>This is a test page.</p></body></html>");
  });
  
  return true;
}

void Honeypot::stop() {
  if (!active) {
    return;
  }
  
  // Останавливаем точку доступа
  WiFi.softAPdisconnect(true);
  active = false;
  
  // Удаляем обработчик для всех запросов
  server->onNotFound([](AsyncWebServerRequest *request) {
    request->send(404);
  });
}

const std::vector<HoneypotConnection>& Honeypot::getConnections() const {
  return connections;
}

void Honeypot::clearLogs() {
  connections.clear();
}

void Honeypot::logConnection(AsyncWebServerRequest *request, Honeypot* honeypot) {
  if (!honeypot->active) {
    return;
  }
  
  HoneypotConnection connection;
  connection.clientIP = request->client()->remoteIP();
  connection.port = request->client()->remotePort();
  connection.timestamp = millis();
  
  // Собираем данные о запросе
  String requestData = request->methodToString();
  requestData += " ";
  requestData += request->url();
  requestData += " HTTP/1.1\n";
  
  // Добавляем заголовки (ограничиваем количество для экономии памяти)
  int headerCount = 0;
  for (int i = 0; i < request->headers(); i++) {
    if (headerCount < 5) { // Максимум 5 заголовков
      AsyncWebHeader* h = request->getHeader(i);
      requestData += h->name();
      requestData += ": ";
      requestData += h->value();
      requestData += "\n";
      headerCount++;
    }
  }
  
  connection.requestData = requestData;
  
  // Если достигнут лимит, удаляем самую старую запись
  if (honeypot->connections.size() >= MAX_HONEYPOT_LOGS) {
    honeypot->connections.erase(honeypot->connections.begin());
  }
  
  // Добавляем новую запись
  honeypot->connections.push_back(connection);
}

void Honeypot::saveLogs(fs::FS &fs) {
  DynamicJsonDocument doc(4096);
  JsonArray logsArray = doc.createNestedArray("logs");
  
  for (const auto& connection : connections) {
    JsonObject logObj = logsArray.createNestedObject();
    logObj["ip"] = connection.clientIP.toString();
    logObj["port"] = connection.port;
    logObj["timestamp"] = connection.timestamp;
    logObj["data"] = connection.requestData;
  }
  
  File configFile = fs.open("/honeypot_logs.json", "w");
  if (!configFile) {
    return;
  }
  
  serializeJson(doc, configFile);
  configFile.close();
}

void Honeypot::loadLogs(fs::FS &fs) {
  if (!fs.exists("/honeypot_logs.json")) {
    return;
  }
  
  File configFile = fs.open("/honeypot_logs.json", "r");
  if (!configFile) {
    return;
  }
  
  DynamicJsonDocument doc(4096);
  DeserializationError error = deserializeJson(doc, configFile);
  configFile.close();
  
  if (error) {
    return;
  }
  
  connections.clear();
  
  if (doc.containsKey("logs")) {
    JsonArray logsArray = doc["logs"];
    
    for (JsonObject logObj : logsArray) {
      HoneypotConnection connection;
      
      if (connection.clientIP.fromString(logObj["ip"].as<String>())) {
        connection.port = logObj["port"].as<uint16_t>();
        connection.timestamp = logObj["timestamp"].as<unsigned long>();
        connection.requestData = logObj["data"].as<String>();
        
        if (connections.size() < MAX_HONEYPOT_LOGS) {
          connections.push_back(connection);
        }
      }
    }
  }
}

void Honeypot::setupAPI() {
  // API для получения журнала соединений
  server->on("/api/honeypot/logs", HTTP_GET, [this](AsyncWebServerRequest *request) {
    DynamicJsonDocument doc(4096);
    JsonArray logsArray = doc.createNestedArray("logs");
    
    for (const auto& connection : connections) {
      JsonObject logObj = logsArray.createNestedObject();
      logObj["ip"] = connection.clientIP.toString();
      logObj["port"] = connection.port;
      logObj["timestamp"] = connection.timestamp;
      logObj["data"] = connection.requestData;
    }
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });
  
  // API для очистки журнала
  server->on("/api/honeypot/clear", HTTP_POST, [this](AsyncWebServerRequest *request) {
    clearLogs();
    request->send(200, "application/json", "{\"success\":true}");
  });
  
  // API для запуска/остановки Honeypot
  server->on("/api/honeypot/toggle", HTTP_POST, [this](AsyncWebServerRequest *request) {
    bool wantActive = true;
    String ssid = "HoneypotAP";
    int channel = 1;
    
    if (request->hasParam("active", true)) {
      wantActive = (request->getParam("active", true)->value() == "true" || 
                    request->getParam("active", true)->value() == "1");
    }
    
    if (request->hasParam("ssid", true)) {
      ssid = request->getParam("ssid", true)->value();
    }
    
    if (request->hasParam("channel", true)) {
      channel = request->getParam("channel", true)->value().toInt();
      if (channel < 1 || channel > 13) channel = 1;
    }
    
    bool success = false;
    
    if (wantActive && !active) {
      success = start(ssid, channel);
    } else if (!wantActive && active) {
      stop();
      success = true;
    } else {
      success = true; // Уже в нужном состоянии
    }
    
    DynamicJsonDocument doc(256);
    doc["success"] = success;
    doc["active"] = active;
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });
}

bool Honeypot::isActive() const {
  return active;
}

#endif // HONEYPOT_H