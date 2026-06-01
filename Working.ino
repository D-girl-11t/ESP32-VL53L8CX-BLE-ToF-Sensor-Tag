#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <freertos/semphr.h>
#include "esp_sleep.h" // For light sleep

// -------------------- UUIDs --------------------
#define SERVICE_UUID        "12345678-1234-1234-1234-1234567890ab"
#define DATA_CHAR_UUID      "abcd1234-1234-1234-1234-1234567890ab"
#define CONTROL_CHAR_UUID   "dcba4321-1234-1234-1234-1234567890ab"

// -------------------- Globals --------------------
BLEServer* pServer;
BLECharacteristic* pDataChar;
BLECharacteristic* pControlChar;

bool sampling = true;         // Sensor acquisition flag
bool bleSendEnabled = false;    // BLE notification flag
bool deviceConnected = false;  // BLE connection state

#define WIDTH 8
#define HEIGHT 8
#define SAMPLE_SIZE (WIDTH*HEIGHT*4) // 8x8 array, 4 bytes per value = 256 bytes
#define BUFFER_SECONDS 10             // Buffer stores last N seconds
#define LPN_PIN 15                     // Example LNT / LPn pin

uint16_t effectiveRate = 20; // Samples per second, adjustable via BLE

// Ring buffer parameters
#define MAX_RING_SAMPLES 200  // max slots (can be bigger for large N)
struct RingSample {
    int64_t timestamp_ms;          // Timestamp in ms
    uint8_t data[SAMPLE_SIZE];     // 8x8 ToF data
};
RingSample ringBuffer[MAX_RING_SAMPLES];

uint16_t writeIndex = 0;
uint16_t readIndex  = 0;
uint16_t bufferSize = 0;
uint32_t missedSamples = 0;

SemaphoreHandle_t ringMutex; // Mutex to protect ring buffer

// -------------------- Helper: timestamp string --------------------
String ts() {
    uint32_t t = millis();
    uint32_t s = t / 1000;
    uint32_t ms = t % 1000;
    return String(s) + "." + String(ms) + "s";
}

// -------------------- Mock ToF Reading --------------------
void getToFArray(uint32_t *array) {
    for (int i = 0; i < WIDTH*HEIGHT; i++) {
        array[i] = random(50, 100); // Mock 50-100
    }
    Serial.print("["); Serial.print(ts()); Serial.println("] [ToF] Read 8x8 mock array");
}

// -------------------- BLE Server Callbacks --------------------
class MyServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) override {
        deviceConnected = true;
        Serial.print("["); Serial.print(ts()); Serial.println("] [BLE] Device connected!");
    }
    void onDisconnect(BLEServer* pServer) override {
        deviceConnected = false;
        Serial.print("["); Serial.print(ts()); Serial.println("] [BLE] Device disconnected!");
        pServer->getAdvertising()->start(); // Restart advertising
        Serial.print("["); Serial.print(ts()); Serial.println("] [BLE] Restarted advertising...");
    }
};

// -------------------- BLE Control Callbacks --------------------
class ControlCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* pChar) override {
        String value = pChar->getValue();
        Serial.print("["); Serial.print(ts()); Serial.print("] [BLE] Control Write Received: "); 
        Serial.println(value);

        // --- Start/Stop Sampling ---
        if (value == "startSampling") {
            sampling = true;
            Serial.print("bufferSize");
            Serial.print(bufferSize);
            Serial.print("["); Serial.print(ts()); Serial.println("] [Control] Sampling started");
        } 
        else if (value == "stopSampling") {
            sampling = false;
            Serial.print("bufferSize ");
            Serial.println(bufferSize);
            Serial.print("["); Serial.print(ts()); Serial.println("] [Control] Sampling stopped");
        }

        // --- Start/Stop BLE notifications ---
        else if (value == "startBLE") {
            bleSendEnabled = true;
            Serial.print("bufferSize");
            Serial.println(bufferSize);
            Serial.print("["); Serial.print(ts()); Serial.println("] [Control] BLE sending enabled");
        } 
        else if (value == "stopBLE") {
            bleSendEnabled = false;
            Serial.println("["); Serial.print(ts()); Serial.println("] [Control] BLE sending disabled");
            Serial.print("bufferSize");
            Serial.println(bufferSize);
        }
        // --- Set sample rate ---
        else if (value.startsWith("setRate:")) {
            int newRate = value.substring(8).toInt(); // get number after 'setRate:'
            if (newRate > 0 && newRate <= 1000) {
                effectiveRate = newRate; // Change FreeRTOS task rate dynamically
                Serial.print("["); Serial.print(ts()); Serial.print("] [Control] Sample rate set to ");
                Serial.println(effectiveRate);
            }
        }

        // --- Clear ring buffer ---
        else if (value == "clear") {
            if (xSemaphoreTake(ringMutex, portMAX_DELAY) == pdTRUE) {
                writeIndex = 0; readIndex = 0; bufferSize = 0; missedSamples = 0;
                xSemaphoreGive(ringMutex);
                Serial.print("["); Serial.print(ts()); Serial.println("] [Control] Ring buffer cleared");
            }
        }
    }
};

// -------------------- Setup BLE --------------------
void setupBLE() {
    Serial.print("["); Serial.print(ts()); Serial.println("] [BLE] Initializing BLE...");
    BLEDevice::init("ESP32_ToF");
    pServer = BLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());

    BLEService *pService = pServer->createService(SERVICE_UUID);

    // Data characteristic (Notify)
    pDataChar = pService->createCharacteristic(DATA_CHAR_UUID, BLECharacteristic::PROPERTY_NOTIFY);
    pDataChar->addDescriptor(new BLE2902());
    Serial.print("["); Serial.print(ts()); Serial.println("] [BLE] Data characteristic created");

    // Control characteristic (Write)
    pControlChar = pService->createCharacteristic(CONTROL_CHAR_UUID, BLECharacteristic::PROPERTY_WRITE);
    pControlChar->setCallbacks(new ControlCallbacks());
    Serial.print("["); Serial.print(ts()); Serial.println("] [BLE] Control characteristic created");

    pService->start();
    pServer->getAdvertising()->start();
    Serial.print("["); Serial.print(ts()); Serial.println("] [BLE] BLE Advertising started...");
}

// -------------------- Task 1: Sensor acquisition --------------------
void SensorTask(void* parameter) {
    Serial.print("["); Serial.print(ts()); Serial.println("] [Task] SensorTask started");

    uint32_t tofArray[WIDTH*HEIGHT];

    while(true) {
        // Low-power mode if sampling stopped
        if (!sampling) {
            digitalWrite(LPN_PIN, LOW); // HIGH = ACTIVE
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
            if(!bleSendEnabled) {
              esp_light_sleep_start();
            }
        } 
        else {
            digitalWrite(LPN_PIN, HIGH); // sleep mode
        }

        // --- Read ToF data ---
        getToFArray(tofArray);
        Serial.print(bufferSize);

        // --- Store in ring buffer ---
        if (xSemaphoreTake(ringMutex, portMAX_DELAY) == pdTRUE) {

            // Buffer full -> overwrite oldest sample
            if (bufferSize == MAX_RING_SAMPLES) {
                missedSamples++;
                readIndex = (readIndex + 1) % MAX_RING_SAMPLES;
                bufferSize--;
                Serial.print("["); Serial.print(ts()); Serial.print("] [RingBuffer] BUFFER FULL! Overwriting oldest sample. Missed: ");
                Serial.println(missedSamples);
            }

            ringBuffer[writeIndex].timestamp_ms = millis();
            memcpy(ringBuffer[writeIndex].data, tofArray, SAMPLE_SIZE);

            Serial.print("["); Serial.print(ts()); Serial.print("] [RingBuffer] Stored sample at writeIndex ");
            Serial.print(writeIndex);
            bufferSize++;
            Serial.print(", Buffer size: "); Serial.print(bufferSize);
            Serial.print(", Missed samples: "); Serial.println(missedSamples);

            writeIndex = (writeIndex + 1) % MAX_RING_SAMPLES;
            xSemaphoreGive(ringMutex);
        }

        // Delay according to effectiveRate
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// -------------------- Task 2: BLE Notifications --------------------
void BLETask(void* parameter) {
    Serial.print("["); Serial.print(ts()); Serial.println("] [Task] BLETask started");

    while(true) {
        if (deviceConnected && bleSendEnabled) {
            uint8_t data[SAMPLE_SIZE];
            int64_t timestamp = 0;

            if (xSemaphoreTake(ringMutex, portMAX_DELAY) == pdTRUE) {
                if (bufferSize > 0) {
                    // Get oldest sample
                    memcpy(data, ringBuffer[readIndex].data, SAMPLE_SIZE);
                    timestamp = ringBuffer[readIndex].timestamp_ms;

                    readIndex = (readIndex + 1) % MAX_RING_SAMPLES;
                    bufferSize--;
                } else {
                    xSemaphoreGive(ringMutex);
                    vTaskDelay(pdMS_TO_TICKS(1));
                    continue;
                }
                xSemaphoreGive(ringMutex);
            }

            // Notify BLE
            pDataChar->setValue(data, SAMPLE_SIZE);
            pDataChar->notify();
            Serial.print("["); Serial.print(ts()); Serial.print("] [BLE] Sent sample at timestamp ms: ");
            Serial.println(timestamp);
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// -------------------- Setup --------------------
void setup() {
    Serial.begin(115200);
    delay(1000);

    pinMode(LPN_PIN, OUTPUT);

    // Create mutex
    ringMutex = xSemaphoreCreateMutex();
    if (ringMutex == NULL) {
        Serial.println("[Error] Failed to create ring buffer mutex!");
        while(1);
    }

    setupBLE();

    // Create FreeRTOS tasks
    xTaskCreatePinnedToCore(SensorTask, "SensorTask", 8192, NULL, 1, NULL, 1);
    xTaskCreatePinnedToCore(BLETask, "BLETask", 8192, NULL, 1, NULL, 1);

    Serial.print("["); Serial.print(ts()); Serial.println("] [Setup] FreeRTOS tasks created");
}

// -------------------- Loop --------------------
void loop() {
    // Nothing; tasks handle everything
    vTaskDelay(pdMS_TO_TICKS(1000));
}
