#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <BLEBeacon.h>

#define DEBUG false

#define ESP32DK_LED                    2
#define REMOTE_CONTROLLER_GPIO         18
#define TRANSMIT_DURATION_SEC          2
#define TRANSMIT_COOLDOWN_DURATION_SEC 10
#define MIN_RSSI                       -90
#define IBEACON_MAC_ADDRESS            "c5:b0:e6:53:59:02"
#define IBEACON_UUID                   "fda50693-a4e2-4fb1-afcf-c6eb07647825"
#define SEC_TO_MS(x)                   ((x) * 1000)

#if DEBUG == false
#define SCAN_INTERVAL_SEC 1
#else
#define SCAN_INTERVAL_SEC 5
#endif

static BLEScan *pBLEScan;
static BLEAddress ibeacon_address(IBEACON_MAC_ADDRESS);

void ibeacon_founded(BLEAdvertisedDevice &device);

void setup() {
    Serial.begin(115200);

    pinMode(ESP32DK_LED, OUTPUT);
    pinMode(REMOTE_CONTROLLER_GPIO, OUTPUT);

    BLEDevice::init("");
    pBLEScan = BLEDevice::getScan();

    Serial.println("Program started");
}

void loop() {
    BLEScanResults res = pBLEScan->start(SCAN_INTERVAL_SEC);
    for (int i = 0; i < res.getCount(); i++) {
        BLEAdvertisedDevice device = res.getDevice(i);

        BLEAddress mac_address = device.getAddress();
        if (mac_address.equals(ibeacon_address)) {
            ibeacon_founded(device);
            break;
        }
    }
}

#if DEBUG == false
void ibeacon_founded(BLEAdvertisedDevice &device) {
    if (device.haveRSSI() == false || device.getRSSI() < MIN_RSSI)
        return;

    digitalWrite(REMOTE_CONTROLLER_GPIO, HIGH);
    digitalWrite(ESP32DK_LED, HIGH);
    delay(SEC_TO_MS(TRANSMIT_DURATION_SEC));

    digitalWrite(REMOTE_CONTROLLER_GPIO, LOW);
    digitalWrite(ESP32DK_LED, LOW);

    delay(SEC_TO_MS(TRANSMIT_COOLDOWN_DURATION_SEC));
}
#else
void ibeacon_founded(BLEAdvertisedDevice &device) {
    Serial.println("Found specified iBeacon!");

    int rssi = device.getRSSI();
    Serial.println("Device rssi: " + String(rssi));

    digitalWrite(ESP32DK_LED, LOW);
    bool blink = false;
    int blink_count = ((abs(rssi) / 10) * 2) + 1;
    for (size_t i = 0; i < blink_count; i++) {
        delay(200);
        digitalWrite(ESP32DK_LED, blink);
        blink = !blink;
    }

    digitalWrite(ESP32DK_LED, LOW);
}

#endif