#include <Arduino.h>
#include <WiFi.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <BLEEddystoneURL.h>
#include <BLEEddystoneTLM.h>
#include <BLEBeacon.h>

#include "secrets.h"

#define ENDIAN_CHANGE_U16(x) ((((x)&0xFF00) >> 8) + (((x)&0xFF) << 8))
#define uS_TO_M_FACTOR 60000000ULL

int loopInterval = 1; // In minutes
int bleScanTime = 5; //In seconds

class Tilt {
    public: 
        char *color;
        double gravity;
        double temp;
};

Tilt curTilt;

class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks
{
    void onResult(BLEAdvertisedDevice advertisedDevice)
    {
        if (advertisedDevice.haveManufacturerData() == true)
        {
            std::string strManufacturerData = advertisedDevice.getManufacturerData();

            uint8_t cManufacturerData[100];
            strManufacturerData.copy((char *)cManufacturerData, strManufacturerData.length(), 0);

            if (strManufacturerData.length() == 25 && cManufacturerData[0] == 0x4C && cManufacturerData[1] == 0x00)
            {
                Serial.println("Found an iBeacon!");
                BLEBeacon oBeacon = BLEBeacon();
                oBeacon.setData(strManufacturerData);
                Serial.printf("iBeacon Frame\n");
                Serial.printf("ID: %04X Major: %d Minor: %d UUID: %s Power: %d\n", oBeacon.getManufacturerId(), ENDIAN_CHANGE_U16(oBeacon.getMajor()), ENDIAN_CHANGE_U16(oBeacon.getMinor()), oBeacon.getProximityUUID().toString().c_str(), oBeacon.getSignalPower());
                curTilt.color = "Black";
                curTilt.gravity = ENDIAN_CHANGE_U16(oBeacon.getMajor());
                curTilt.temp = ENDIAN_CHANGE_U16(oBeacon.getMinor());
            }
        }
    }
};

void setup()
{
    Serial.begin(115200);
    pinMode(LED_BUILTIN, OUTPUT); // set up onboard LED

    digitalWrite(LED_BUILTIN, HIGH);
    Serial.println("Scanning for Tilts...");
    BLEDevice::init("");
    BLEScan *pBLEScan = BLEDevice::getScan(); //create new scan
    pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
    pBLEScan->setActiveScan(true); //active scan uses more power, but get results faster
    pBLEScan->setInterval(100);
    pBLEScan->setWindow(99); // less or equal setInterval value
    BLEScanResults foundDevices = pBLEScan->start(bleScanTime, false);
    Serial.println("Tilts found: " + foundDevices.getCount());
    digitalWrite(LED_BUILTIN, LOW);

    if (foundDevices.getCount() > 0 && false) // THIS ISN'T SET UP YET
    {
        digitalWrite(LED_BUILTIN, HIGH);
        Serial.println("Connecting to " + String(ssid));
        WiFi.begin(ssid, password);
        while (WiFi.status() != WL_CONNECTED) {
            delay(500);
            Serial.print(".");
        }
        Serial.println("");
        Serial.println("IP address: " + WiFi.localIP());
        
        // Use WiFiClient class to create TCP connections
        Serial.println("connecting to " + String(host));
        WiFiClient client;
        const int httpPort = 80;
        if (!client.connect(host, httpPort)) {
            Serial.println("connection failed");
            return;
        }

        // We now create a URI for the request
        String url = "/input/";
        url += streamId;
        url += "?private_key=";
        url += privateKey;
        url += "&value=";
        url += "0";

        // This will send the request to the server
        client.print(String("GET ") + url + " HTTP/1.1\r\n" +
                    "Host: " + host + "\r\n" +
                    "Connection: close\r\n\r\n");
        unsigned long timeout = millis();
        while (client.available() == 0) {
            if (millis() - timeout > 5000) {
                Serial.println(">>> Client Timeout !");
                client.stop();
                return;
            }
        }

        // Read all the lines of the reply from server and print them to Serial
        while(client.available()) {
            String line = client.readStringUntil('\r');
            Serial.print(line);
        }
        digitalWrite(LED_BUILTIN, LOW);
    }

    pBLEScan->clearResults(); // delete results fromBLEScan buffer to release memory

    esp_sleep_enable_timer_wakeup(loopInterval * uS_TO_M_FACTOR);
    Serial.flush(); 
    esp_deep_sleep_start();
}

void loop()
{
    // no loop with deep sleep
}
