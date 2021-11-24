#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
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

class Tilt {
    public: 
        char *color;
        float gravity;
        float temp;
};

const char* ntpServer = "pool.ntp.org"; // for getting time
const long pstOffset_sec = -28800;
const int daylightOffset_sec = 3600;
const int loopInterval = 1; // In minutes
const int bleScanTime = 5; // In seconds
const int postRequestTimeout = 5; // In seconds
Tilt *foundTilts[8]; // Max one of each color
int curTiltIndex = -1;

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
                BLEBeacon oBeacon = BLEBeacon();
                oBeacon.setData(strManufacturerData);
                Tilt *curTilt = new Tilt();
                const char *uuid = oBeacon.getProximityUUID().toString().c_str();
                if (uuid == "10bb95a4-b1c5-444b-b512-1370f02d74de") {
                    curTilt->color = "Red";
                } else if (uuid == "20bb95a4-b1c5-444b-b512-1370f02d74de") {
                    curTilt->color = "Green";
                } else if (uuid == "30bb95a4-b1c5-444b-b512-1370f02d74de") {
                    curTilt->color = "Black";
                } else if (uuid == "40bb95a4-b1c5-444b-b512-1370f02d74de") {
                    curTilt->color = "Purple";
                } else if (uuid == "50bb95a4-b1c5-444b-b512-1370f02d74de") {
                    curTilt->color = "Orange";
                } else if (uuid == "60bb95a4-b1c5-444b-b512-1370f02d74de") {
                    curTilt->color = "Blue";
                } else if (uuid == "70bb95a4-b1c5-444b-b512-1370f02d74de") {
                    curTilt->color = "Yellow";
                } else if (uuid == "80bb95a4-b1c5-444b-b512-1370f02d74de") {
                    curTilt->color = "Pink";
                } else {
                    return;
                }
                
                curTilt->gravity = ENDIAN_CHANGE_U16(oBeacon.getMinor());
                curTilt->temp = ENDIAN_CHANGE_U16(oBeacon.getMajor());
                curTiltIndex++;
                foundTilts[curTiltIndex] = curTilt;
                Serial.println("\tFound " + String(curTilt->color) + " Tilt:");
                Serial.println("\t\t Temp:" + String(curTilt->temp));
                Serial.println("\t\t SG:" + String(curTilt->gravity));
            }
        }
        digitalWrite(LED_BUILTIN, (millis() / 1000) % 2); // update blinking LED
    }
};

void cleanupAndSleep() {
    Serial.flush();
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    esp_bt_controller_disable();
    esp_sleep_enable_timer_wakeup(loopInterval * uS_TO_M_FACTOR);
    delay(1000); // delay to be sure timer was set
    esp_deep_sleep_start();
}

void setup()
{
    Serial.begin(115200);
    pinMode(LED_BUILTIN, OUTPUT); // set up onboard LED
    esp_bt_controller_enable(ESP_BT_MODE_BLE); // turn on bluetooth

    Serial.println("Scanning for Tilts...");
    BLEDevice::init("Tilt Telephone");
    BLEScan *pBLEScan = BLEDevice::getScan(); //create new scan
    pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
    pBLEScan->setActiveScan(true); //active scan uses more power, but get results faster
    pBLEScan->setInterval(100);
    pBLEScan->setWindow(100); // less or equal setInterval value
    pBLEScan->start(bleScanTime, false);
    pBLEScan->clearResults(); // delete results fromBLEScan buffer to release memory
    digitalWrite(LED_BUILTIN, LOW); // clear LED status

    if (curTiltIndex >= 0)
    {
        // Connect to WiFi
        Serial.println("Connecting to " + String(ssid));
        WiFi.mode(WIFI_STA);
        WiFi.setHostname("Tilt Telephone");
        WiFi.begin(ssid, password);
        while (WiFi.status() != WL_CONNECTED) {
            delay(500);
            Serial.print(".");
            digitalWrite(LED_BUILTIN, (millis() / 1000) % 2); // blink LED while connecting
        }
        Serial.println("");
        Serial.println("\tConnected... IP address: " + WiFi.localIP());
        digitalWrite(LED_BUILTIN, LOW);

        // Congifure time now that we have WiFi
        Serial.println("Requesting current time...");
        configTime(pstOffset_sec, daylightOffset_sec, ntpServer);
        struct tm timeInfo;
        if (!getLocalTime(&timeInfo)) {
            Serial.println(">>> Failed to obtain time !");
            cleanupAndSleep();
        }
        Serial.println("\tCurrent time: " +
                    String(timeInfo.tm_mon) + 
                    "/" + String(timeInfo.tm_mday) +
                    "/" + String(timeInfo.tm_year) +
                    " " + String(timeInfo.tm_hour) + 
                    ":" + String(timeInfo.tm_min) + 
                    ":" + String(timeInfo.tm_sec));
        
        // Connect to cloud service
        Serial.println("Connecting to " + String(host));
        WiFiClient client;
        if (!client.connect(host, httpPort)) {
            Serial.println(">>> Connection to " + String(host) + " failed !");
            cleanupAndSleep();
        }
        Serial.println("\tConnected");

        // Log found tilts to cloud
        for (int x = 0; x <= curTiltIndex; x++)
        {
            String url = "/tilt";
            url += "?id=";
            url += privateKey;
            
            String content = "Temp=";
            content += foundTilts[x]->temp;
            content += "&SG=";
            content += foundTilts[x]->gravity;
            content += "&Color=";
            content += foundTilts[x]->color;
            content += "&Timepoint=";
            content += String(timeInfo.tm_mon) + 
                    "\%2F" + String(timeInfo.tm_mday) +
                    "\%2F" + String(timeInfo.tm_year) +
                    "\%20" + String(timeInfo.tm_hour) + 
                    "\%3A" + String(timeInfo.tm_min) + 
                    "\%3A" + String(timeInfo.tm_sec);

            // This will send the request to the server
            client.print(String("POST ") + url + " HTTP/1.1\r\n" +
                        "Host: " + host + "\r\n" +
                        "Content-Type: application/x-www-form-urlencoded\r\n" + 
                        "Content-Length: " + content.length() + "\r\n\r\n" +
                        content);
            unsigned long requestStart = millis();
            while (client.available() == 0) {
                if (millis() - requestStart > (postRequestTimeout * 1000)) {
                    Serial.println(">>> Post Request Timeout !");
                    client.stop();
                    cleanupAndSleep();
                }
                digitalWrite(LED_BUILTIN, (millis() / 1000) % 2); // blink every second while waiting
            }

            // Read all the lines of the reply from server and print them to Serial
            while(client.available()) {
                String line = client.readStringUntil('\r');
                Serial.print(line);
            }
            digitalWrite(LED_BUILTIN, LOW);
        }
    } else {
        Serial.println("No tilts found.");
    }

    cleanupAndSleep();
}

void loop()
{
    // no loop with deep sleep
}
