#include "FS.h"
#include "SD.h"
#include "SPI.h"
#include <Ticker.h>
#include "LittleFS.h"

#define CS_PIN 5
#define LED_PIN 2
#define MAX_BACKUP_FILES 5
#define LOG_LEVEL 3 // 0: NONE, 1: ERROR, 2: WARNING, 3: INFO, 4: DEBUG

Ticker sdChecker;
Ticker dataWriter;
Ticker stopWriting;
Ticker backupTimer;

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

void setup() {
  pinMode(LED_PIN, OUTPUT);
  Serial.begin(115200);
  if (!LittleFS.begin()) {
    logMessage(1, "Failed to initialize LittleFS");
  }
  sdChecker.attach(5, checkSDCard);
  dataWriter.attach(1, bufferData);
  stopWriting.attach(60, []() { stopWritingAndSaveAsync(); }); // Make async
  backupTimer.attach(3600, createBackup); // Changed to every hour
}

void checkSDCard() {
  if (!SD.begin(CS_PIN)) {
    logMessage(1, "SD card not found!");
    sdAvailable = false;
    digitalWrite(LED_PIN, HIGH);
    return;
  }
  digitalWrite(LED_PIN, LOW);
  logMessage(3, "SD card found.");
  sdAvailable = true;
}

void bufferData() {
  if (!writeEnabled || !sdAvailable) return;

  lastNumber++;
  dataBuffer += String(lastNumber) + "\n";

  if (dataBuffer.length() >= bufferSizeLimit) {
    writeDataFromBuffer();
  }
}

void writeDataFromBuffer() {
  if (!sdAvailable) {
    logMessage(1, "SD card is not available for writing.");
    return;
  }

  File file = SD.open(dataFileName, FILE_APPEND);
  if (!file) {
    logMessage(1, "Error opening file for writing.");
    return;
  }

  file.print(dataBuffer);
  file.close();
  logMessage(3, "Data written: " + dataBuffer);
  dataBuffer = "";
}

void createBackup() {
  if (!sdAvailable) return;

  String timestamp = String(millis());
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
  // Delay removed for async behavior

  if (dataBuffer.length() > 0) {
    writeDataFromBuffer(); // Ensure this operation is quick; otherwise, consider offloading to a background task
  }
  
  // Async delay simulation with Ticker
  Ticker resumeWritingTicker;
  resumeWritingTicker.once(2, []() {
    digitalWrite(LED_PIN, LOW);
    writeEnabled = true;
  });
}

void loop() {
  // Main logic runs on timer
}