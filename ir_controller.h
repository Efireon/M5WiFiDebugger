#ifndef IR_CONTROLLER_H
#define IR_CONTROLLER_H

#include <Arduino.h>
#include <M5StickCPlus2.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <vector>
#include <LittleFS.h>

// Максимальное количество сохраненных ИК команд
#define MAX_IR_COMMANDS 10

// Структура для хранения ИК команды
struct IRCommand {
    String name;
    uint32_t code;
    uint8_t bits;
    String description;
};

// Класс для управления ИК-функционалом
class IRController {
private:
    AsyncWebServer* server;
    std::vector<IRCommand> commands;
    bool irEnabled;
    int irPin;
    
public:
    IRController(AsyncWebServer* server);
    
    // Инициализация
    void begin(int pin = 9); // GPIO9 - стандартный ИК-пин для M5StickC PLUS2
    
    // Управление командами
    bool addCommand(const String& name, uint32_t code, uint8_t bits, const String& description);
    bool removeCommand(int index);
    bool transmitCommand(int index);
    bool transmitRawCode(uint32_t code, uint8_t bits);
    
    // Получение списка команд
    const std::vector<IRCommand>& getCommands() const;
    
    // Запись и чтение конфигурации
    void saveConfig(fs::FS &fs);
    void loadConfig(fs::FS &fs);
    
    // Настройка API
    void setupAPI();
    
    // Проверка статуса
    bool isEnabled() const;
    
    // Функция для симуляции (заглушка)
    bool simulateTransmit(const String& name);
};

// Реализация класса IRController

IRController::IRController(AsyncWebServer* server) : 
    server(server), 
    irEnabled(false),
    irPin(9) {
    commands.reserve(MAX_IR_COMMANDS);
}

void IRController::begin(int pin) {
    irPin = pin;
    
    // В реальной реализации здесь была бы инициализация ИК-передатчика
    // Например: irSender.begin(irPin);
    
    // Пока это просто заглушка
    irEnabled = true;
    
    // Загружаем сохраненные команды
    loadConfig(LittleFS);
    
    // Настраиваем API
    setupAPI();
}

bool IRController::addCommand(const String& name, uint32_t code, uint8_t bits, const String& description) {
    // Проверяем, не превышен ли лимит команд
    if (commands.size() >= MAX_IR_COMMANDS) {
        return false;
    }
    
    // Проверяем, что нет команды с таким же именем
    for (const auto& cmd : commands) {
        if (cmd.name == name) {
            return false;
        }
    }
    
    // Добавляем новую команду
    IRCommand newCommand = {
        name,
        code,
        bits,
        description
    };
    
    commands.push_back(newCommand);
    saveConfig(LittleFS);
    return true;
}

bool IRController::removeCommand(int index) {
    if (index < 0 || index >= commands.size()) {
        return false;
    }
    
    commands.erase(commands.begin() + index);
    saveConfig(LittleFS);
    return true;
}

bool IRController::transmitCommand(int index) {
    if (!irEnabled || index < 0 || index >= commands.size()) {
        return false;
    }
    
    // В реальной реализации здесь был бы код для отправки ИК сигнала
    // Например: irSender.sendNEC(commands[index].code, commands[index].bits);
    
    // Пока просто симулируем отправку
    Serial.printf("IR: Transmitting %s (0x%08X, %d bits)\n", 
                 commands[index].name.c_str(), 
                 commands[index].code, 
                 commands[index].bits);
    
    // Имитируем передачу с помощью светодиода
    M5.Power.setLed(true);   // Включить LED
    delay(100);
    M5.Power.setLed(false);  // Выключить LED
    
    return true;
}

bool IRController::transmitRawCode(uint32_t code, uint8_t bits) {
    if (!irEnabled) {
        return false;
    }
    
    // В реальной реализации здесь был бы код для отправки ИК сигнала
    // Например: irSender.sendNEC(code, bits);
    
    // Пока просто симулируем отправку
    Serial.printf("IR: Transmitting raw code 0x%08X (%d bits)\n", code, bits);
    
    // Имитируем передачу с помощью светодиода
    M5.Power.setLed(true);   // Включить LED
    delay(100);
    M5.Power.setLed(false);  // Выключить LED
    
    return true;
}

const std::vector<IRCommand>& IRController::getCommands() const {
    return commands;
}

void IRController::saveConfig(fs::FS &fs) {
    DynamicJsonDocument doc(2048);
    
    JsonArray commandsArray = doc.createNestedArray("commands");
    for (const auto& cmd : commands) {
        JsonObject cmdObj = commandsArray.createNestedObject();
        cmdObj["name"] = cmd.name;
        cmdObj["code"] = cmd.code;
        cmdObj["bits"] = cmd.bits;
        cmdObj["description"] = cmd.description;
    }
    
    File configFile = fs.open("/ir_config.json", "w");
    if (!configFile) {
        return;
    }
    
    serializeJson(doc, configFile);
    configFile.close();
}

void IRController::loadConfig(fs::FS &fs) {
    if (!fs.exists("/ir_config.json")) {
        // Если конфигурации нет, создаем несколько тестовых команд
        addCommand("Power", 0x20DF10EF, 32, "Power On/Off");
        addCommand("Volume Up", 0x20DF40BF, 32, "Increase Volume");
        addCommand("Volume Down", 0x20DFC03F, 32, "Decrease Volume");
        addCommand("Channel Up", 0x20DF00FF, 32, "Next Channel");
        addCommand("Channel Down", 0x20DF807F, 32, "Previous Channel");
        return;
    }
    
    File configFile = fs.open("/ir_config.json", "r");
    if (!configFile) {
        return;
    }
    
    DynamicJsonDocument doc(2048);
    DeserializationError error = deserializeJson(doc, configFile);
    configFile.close();
    
    if (error) {
        return;
    }
    
    commands.clear();
    
    if (doc.containsKey("commands")) {
        JsonArray commandsArray = doc["commands"];
        
        for (JsonObject cmdObj : commandsArray) {
            if (commands.size() < MAX_IR_COMMANDS) {
                IRCommand cmd;
                cmd.name = cmdObj["name"].as<String>();
                cmd.code = cmdObj["code"].as<uint32_t>();
                cmd.bits = cmdObj["bits"].as<uint8_t>();
                cmd.description = cmdObj["description"].as<String>();
                
                commands.push_back(cmd);
            }
        }
    }
}

void IRController::setupAPI() {
    // API для получения списка команд
    server->on("/api/ir/commands", HTTP_GET, [this](AsyncWebServerRequest *request) {
        DynamicJsonDocument doc(2048);
        JsonArray commandsArray = doc.createNestedArray("commands");
        
        for (int i = 0; i < commands.size(); i++) {
            JsonObject cmdObj = commandsArray.createNestedObject();
            cmdObj["index"] = i;
            cmdObj["name"] = commands[i].name;
            cmdObj["code"] = commands[i].code;
            cmdObj["bits"] = commands[i].bits;
            cmdObj["description"] = commands[i].description;
        }
        
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });
    
    // API для отправки команды
    server->on("/api/ir/transmit", HTTP_POST, [this](AsyncWebServerRequest *request) {
        // Проверяем активность ИК-функции
        if (!irEnabled) {
            request->send(503, "application/json", "{\"error\":\"IR functionality is not enabled\"}");
            return;
        }
        
        bool success = false;
        String message = "Invalid parameters";
        
        if (request->hasParam("index", true)) {
            // Отправка команды по индексу
            int index = request->getParam("index", true)->value().toInt();
            if (index >= 0 && index < commands.size()) {
                success = transmitCommand(index);
                message = success ? "Command transmitted" : "Failed to transmit command";
            } else {
                message = "Invalid command index";
            }
        } else if (request->hasParam("code", true) && request->hasParam("bits", true)) {
            // Отправка произвольного кода
            uint32_t code = strtoul(request->getParam("code", true)->value().c_str(), NULL, 16);
            uint8_t bits = request->getParam("bits", true)->value().toInt();
            
            if (bits > 0 && bits <= 32) {
                success = transmitRawCode(code, bits);
                message = success ? "Raw code transmitted" : "Failed to transmit raw code";
            } else {
                message = "Invalid number of bits (must be between 1 and 32)";
            }
        } else if (request->hasParam("name", true)) {
            // Отправка команды по имени
            String name = request->getParam("name", true)->value();
            success = simulateTransmit(name);
            message = success ? "Command transmitted" : "Command not found";
        }
        
        DynamicJsonDocument doc(256);
        doc["success"] = success;
        doc["message"] = message;
        
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });
    
    // API для добавления новой команды
    server->on("/api/ir/add", HTTP_POST, [this](AsyncWebServerRequest *request) {
        if (!request->hasParam("name", true) || 
            !request->hasParam("code", true) || 
            !request->hasParam("bits", true)) {
            request->send(400, "application/json", "{\"error\":\"Missing required parameters\"}");
            return;
        }
        
        String name = request->getParam("name", true)->value();
        uint32_t code = strtoul(request->getParam("code", true)->value().c_str(), NULL, 16);
        uint8_t bits = request->getParam("bits", true)->value().toInt();
        String description = "";
        
        if (request->hasParam("description", true)) {
            description = request->getParam("description", true)->value();
        }
        
        bool success = addCommand(name, code, bits, description);
        
        DynamicJsonDocument doc(256);
        doc["success"] = success;
        if (!success) {
            doc["error"] = "Failed to add command. Maximum limit reached or duplicate name.";
        }
        
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });
    
    // API для удаления команды
    server->on("/api/ir/remove", HTTP_POST, [this](AsyncWebServerRequest *request) {
        if (!request->hasParam("index", true)) {
            request->send(400, "application/json", "{\"error\":\"Missing index parameter\"}");
            return;
        }
        
        int index = request->getParam("index", true)->value().toInt();
        bool success = removeCommand(index);
        
        DynamicJsonDocument doc(256);
        doc["success"] = success;
        if (!success) {
            doc["error"] = "Invalid command index";
        }
        
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });
}

bool IRController::isEnabled() const {
    return irEnabled;
}

bool IRController::simulateTransmit(const String& name) {
    for (int i = 0; i < commands.size(); i++) {
        if (commands[i].name == name) {
            return transmitCommand(i);
        }
    }
    return false;
}

#endif // IR_CONTROLLER_H