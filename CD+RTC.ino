#include "FS.h"
#include "SD.h"
#include "SPI.h"
#include <Ticker.h>
#include "LittleFS.h"
#include <Arduino.h>
#include <Ds1302.h>

#define CS_PIN 5
#define LED_PIN 2
#define MAX_BACKUP_FILES 10
#define LOG_LEVEL 3 // 0: NONE, 1: ERROR, 2: WARNING, 3: INFO, 4: DEBUG

// Пины для DS1302
const int PIN_RST = 15;  // Reset
const int PIN_CLK = 32; // Clock
const int PIN_DAT = 14; // Data

Ds1302 rtc(PIN_RST, PIN_CLK, PIN_DAT);

Ticker sdChecker;
Ticker dataWriter;
Ticker stopWriting;
Ticker backupTimer;
Ticker resumeWritingTicker;

volatile bool sdAvailable = false;
volatile long lastNumber = 0;
volatile bool writeEnabled = true;
String dataBuffer = "";
const int bufferSizeLimit = 10;
String backupFileName = "/backup";
const String dataFileName = "/example.txt";

void logMessage(int level, const String& message) {
  if (level <= LOG_LEVEL) {
    Serial.println(message);
  }
}

String getTimestamp() {
    Ds1302::DateTime now;
    rtc.getDateTime(&now);
    
    int hour = now.hour;
    int minute = now.minute;
    int second = now.second + 2; // Компенсация времени

    // Корректировка переполнения
    if (second >= 60) {
        second -= 60;
        minute += 1;
    }
    if (minute >= 60) {
        minute -= 60;
        hour += 1;
    }
    if (hour >= 24) hour -= 24;

    char timestamp[20];
    snprintf(timestamp, sizeof(timestamp), "%04d-%02d-%02d %02d:%02d:%02d", now.year + 2000, now.month, now.day, hour, minute, second);
    return String(timestamp);
}

void setup() {
  pinMode(LED_PIN, OUTPUT);
  Serial.begin(115200);
  rtc.init();
  Serial.println("DS1302 RTC module initialized");
  
  if (!LittleFS.begin()) {
    logMessage(1, "Failed to initialize LittleFS");
  }
  sdChecker.attach(5, checkSDCard);
  dataWriter.attach(1, bufferData);
  stopWriting.attach(60, []() { stopWritingAndSaveAsync(); });
  backupTimer.attach(3600, createBackup);
}

void checkSDCard() {
  bool wasSdAvailable = sdAvailable; // Сохраняем предыдущее состояние доступности SD-карты
  sdAvailable = SD.begin(CS_PIN);

  if (sdAvailable) {
    digitalWrite(LED_PIN, LOW);
    if (!wasSdAvailable) { // Если SD-карта стала доступна
      logMessage(3, "SD card found.");
    }
  } else {
    digitalWrite(LED_PIN, HIGH);
    if (wasSdAvailable) { // Если SD-карта была доступна, но теперь нет
      logMessage(1, "SD card not found!");
    }
  }
}

void bufferData() {
  if (!writeEnabled || !sdAvailable) return;

  lastNumber++;
  String currentTimestamp = getTimestamp();
  dataBuffer += currentTimestamp + ", " + String(lastNumber) + "\n";

  if (dataBuffer.length() >= bufferSizeLimit) {
    writeDataFromBuffer();
  }
}

void writeDataFromBuffer() {
    if (!SD.begin(CS_PIN)) { // Попытка инициализации SD-карты
        sdAvailable = false;
        digitalWrite(LED_PIN, HIGH); // Включаем LED, указывая на проблему
        logMessage(1, "SD card is not available for writing.");
        return; // Прекращаем выполнение функции
    }
    
    // Если SD.begin() успешно, продолжаем с записью
    sdAvailable = true;
    digitalWrite(LED_PIN, LOW); // Выключаем LED, SD-карта доступна

    File file = SD.open(dataFileName, FILE_APPEND);
    if (!file) {
        logMessage(1, "Error opening file for writing.");
        return;
    }

    file.print(dataBuffer);
    file.close();
    logMessage(3, "Data written: " + dataBuffer);
    dataBuffer = ""; // Очистка буфера после записи
}

void createBackup() {
  if (!sdAvailable) return;

  String timestamp = getTimestamp();
  String backupFilePath = backupFileName + "_" + timestamp + ".txt";

  File dataFile = SD.open(dataFileName, FILE_READ);
  if (!dataFile) {
    logMessage(1, "Error opening data file for backup.");
    return;
  }

  File backupFile = SD.open(backupFilePath, FILE_WRITE);
  if (!backupFile) {
    logMessage(1, "Error creating backup file.");
    dataFile.close();
    return;
  }

  while (dataFile.available()) {
    backupFile.write(dataFile.read());
  }

  dataFile.close();
  backupFile.close();

  logMessage(3, "Backup created: " + backupFilePath);

  cleanupOldBackups();
}

void cleanupOldBackups() {
  File root = SD.open("/");
  File file = root.openNextFile();
  int fileCount = 0;

  while (file) {
    String fileName = file.name();
    if (fileName.startsWith(backupFileName)) {
      fileCount++;
    }
    file.close();
    file = root.openNextFile();
  }

  while (fileCount > MAX_BACKUP_FILES) {
    File oldestFile;
    String oldestFileName;
    unsigned long oldestFileTime = millis();

    file = root.openNextFile();
    while (file) {
      String fileName = file.name();
      if (fileName.startsWith(backupFileName)) {
        String fileTimeStr = fileName.substring(backupFileName.length() + 1, fileName.lastIndexOf('.'));
        unsigned long fileTime = fileTimeStr.toInt();
        if (fileTime < oldestFileTime) {
          oldestFileTime = fileTime;
          oldestFileName = fileName;
        }
      }
      file.close();
      file = root.openNextFile();
    }

    if (SD.remove(oldestFileName.c_str())) {
      logMessage(3, "Deleted old backup: " + oldestFileName);
      fileCount--;
    } else {
      logMessage(1, "Failed to delete old backup: " + oldestFileName);
      break;
    }
  }
}

void stopWritingAndSaveAsync() {
  writeEnabled = false;
  digitalWrite(LED_PIN, HIGH);

  if (dataBuffer.length() > 0) {
    writeDataFromBuffer(); // Ensure this operation is quick; otherwise, consider offloading to a background task
  }
  
  // Async delay simulation with Ticker
  resumeWritingTicker.once(2, []() {
    digitalWrite(LED_PIN, LOW);
    writeEnabled = true;
  });
}

void loop() {
  // Main logic runs on timer
}
