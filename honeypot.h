#ifndef HONEYPOT_H
#define HONEYPOT_H

#include <ESPAsyncWebServer.h>
#include <IPAddress.h>
#include <vector>
#include <M5StickCPlus2.h>

// Максимальное количество соединений для логирования
#define MAX_HONEYPOT_CONNECTIONS 10

// Структура для хранения информации о соединении
struct HoneypotConnection {
  IPAddress clientIP;
  uint16_t port;
  String requestData;
  unsigned long timestamp;
};

// Класс Honeypot для управления режимом ловушки
class Honeypot {
private:
  // Буфер соединений
  HoneypotConnection connections[MAX_HONEYPOT_CONNECTIONS];
  int connectionCount;
  
  // Точка доступа
  String ssid;
  int channel;
  
  // Сетевые настройки
  IPAddress localIP;
  IPAddress gateway;
  IPAddress subnet;
  
  // Колбэк для обработки соединений
  std::function<void(HoneypotConnection&)> onConnectionCallback;

public:
  // Конструктор
  Honeypot() : connectionCount(0), ssid("HoneyPot"), channel(1) {
    localIP.fromString("192.168.4.1");
    gateway.fromString("192.168.4.1");
    subnet.fromString("255.255.255.0");
  }
  
  // Начать работу Honeypot
  void begin(AsyncWebServer& server) {
    // Включаем точку доступа
    WiFi.mode(WIFI_AP);
    WiFi.softAP(ssid.c_str(), "", channel); // Открытая точка доступа без пароля
    WiFi.softAPConfig(localIP, gateway, subnet);
    
    // Сбрасываем счетчик соединений
    connectionCount = 0;
    
    // Настраиваем запись всех соединений
    server.onNotFound([this](AsyncWebServerRequest *request){
      // Логируем соединение
      logConnection(request, this);
      
      // Отправляем ответ
      request->send(200, "text/plain", "OK");
    });
    
    // Добавляем обработчик для всех типов запросов на корневой URL
    server.on("/", HTTP_ANY, [this](AsyncWebServerRequest *request){
      // Логируем соединение
      logConnection(request, this);
      
      // Отправляем страницу-приманку
      request->send(200, "text/html", 
        "<html><body><h1>Welcome to WiFi Network</h1>"
        "<p>Please wait while we check your connection...</p>"
        "<script>setTimeout(function() { "
        "window.location.href = '/dashboard'; }, 3000);</script>"
        "</body></html>");
    });
    
    // Страница "панели управления"
    server.on("/dashboard", HTTP_ANY, [this](AsyncWebServerRequest *request){
      // Логируем соединение
      logConnection(request, this);
      
      // Отправляем форму входа
      request->send(200, "text/html", 
        "<html><body><h1>Login Required</h1>"
        "<form action='/login' method='post'>"
        "Username: <input type='text' name='username'><br>"
        "Password: <input type='password' name='password'><br>"
        "<input type='submit' value='Login'>"
        "</form></body></html>");
    });
    
    // Страница логина
    server.on("/login", HTTP_ANY, [this](AsyncWebServerRequest *request){
      // Логируем соединение с расширенной проверкой наличия учетных данных
      logConnection(request, this);
      
      // Сообщаем об ошибке входа
      request->send(403, "text/html", 
        "<html><body><h1>Authentication Failed</h1>"
        "<p>Invalid username or password.</p>"
        "<a href='/dashboard'>Try again</a>"
        "</body></html>");
    });
    
    M5.Lcd.println("Honeypot started on " + WiFi.softAPIP().toString());
  }
  
  // Установить SSID точки доступа
  void setSSID(const String& newSSID) {
    ssid = newSSID;
  }
  
  // Получить SSID точки доступа
  String getSSID() const {
    return ssid;
  }
  
  // Установить канал точки доступа
  void setChannel(int newChannel) {
    if (newChannel >= 1 && newChannel <= 13) {
      channel = newChannel;
    }
  }
  
  // Получить все логи соединений
  const HoneypotConnection* getConnections() const {
    return connections;
  }
  
  // Получить количество логов
  int getConnectionCount() const {
    return connectionCount;
  }
  
  // Очистить логи
  void clearConnections() {
    connectionCount = 0;
  }
  
  // Установить колбэк для обработки новых соединений
  void setOnConnectionCallback(std::function<void(HoneypotConnection&)> callback) {
    onConnectionCallback = callback;
  }
  
  // Статический метод для логирования соединений
  static void logConnection(AsyncWebServerRequest* request, Honeypot* honeypot) {
    // Проверяем, не переполнен ли буфер
    if (honeypot->connectionCount >= MAX_HONEYPOT_CONNECTIONS) {
      // Если буфер заполнен, смещаем все записи на одну позицию назад
      for (int i = 0; i < MAX_HONEYPOT_CONNECTIONS - 1; i++) {
        honeypot->connections[i] = honeypot->connections[i + 1];
      }
      honeypot->connectionCount = MAX_HONEYPOT_CONNECTIONS - 1;
    }
    
    // Получаем индекс для новой записи
    int index = honeypot->connectionCount;
    
    // Заполняем информацию о соединении
    honeypot->connections[index].clientIP = request->client()->remoteIP();
    honeypot->connections[index].port = request->client()->remotePort();
    honeypot->connections[index].timestamp = millis();
    
    // Собираем информацию о запросе
    String requestData = request->url();
    // Исправление строки для устранения ошибки конкатенации
    String methodStr = " [";
    methodStr += request->methodToString();
    methodStr += "]";
    requestData += methodStr;
    
    // Добавляем информацию о заголовках
    for (int i = 0; i < request->headers(); i++) {
      // Используем const AsyncWebHeader* вместо AsyncWebHeader*
      const AsyncWebHeader* h = request->getHeader(i);
      requestData += " " + h->name() + ":" + h->value();
    }
    
    // Добавляем информацию о параметрах
    for (int i = 0; i < request->params(); i++) {
      // Используем const AsyncWebParameter* вместо AsyncWebParameter*
      const AsyncWebParameter* p = request->getParam(i);
      requestData += " " + p->name() + "=" + p->value();
    }
    
    // Сохраняем данные запроса
    honeypot->connections[index].requestData = requestData;
    
    // Увеличиваем счетчик соединений
    honeypot->connectionCount++;
    
    // Вызываем колбэк, если он установлен
    if (honeypot->onConnectionCallback) {
      honeypot->onConnectionCallback(honeypot->connections[index]);
    }
    
    // Выводим информацию на экран
    M5.Lcd.fillScreen(BLACK);
    M5.Lcd.setCursor(0, 0);
    M5.Lcd.println("Honeypot Activity");
    M5.Lcd.println("-----------------");
    M5.Lcd.print("Client: ");
    M5.Lcd.println(honeypot->connections[index].clientIP.toString());
    M5.Lcd.print("URL: ");
    M5.Lcd.println(request->url());
    M5.Lcd.print("Total connections: ");
    M5.Lcd.println(honeypot->connectionCount);
  }
};

#endif // HONEYPOT_H