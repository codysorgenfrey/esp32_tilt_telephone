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
#define LED_BUILTIN 2

struct Tilt {
    char* color;
    double gravity;
    double temp;
};

const char* ntpServer = "pool.ntp.org"; // for getting time
const long pstOffset_sec = -28800;
const int daylightOffset_sec = 3600;
const int loopInterval = 15; // In minutes
const int bleScanTime = 30; // In seconds
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
                Tilt *curTilt = new Tilt;
                const String uuid = (String)oBeacon.getProximityUUID().toString().c_str();
                if (uuid == "de742df0-7013-12b5-444b-b1c510bb95a4") {
                    curTilt->color = "Red";
                } else if (uuid == "de742df0-7013-12b5-444b-b1c520bb95a4") {
                    curTilt->color = "Green";
                } else if (uuid == "de742df0-7013-12b5-444b-b1c530bb95a4") {
                    curTilt->color = "Black";
                } else if (uuid == "de742df0-7013-12b5-444b-b1c540bb95a4") {
                    curTilt->color = "Purple";
                } else if (uuid == "de742df0-7013-12b5-444b-b1c550bb95a4") {
                    curTilt->color = "Orange";
                } else if (uuid == "de742df0-7013-12b5-444b-b1c560bb95a4") {
                    curTilt->color = "Blue";
                } else if (uuid == "de742df0-7013-12b5-444b-b1c570bb95a4") {
                    curTilt->color = "Yellow";
                } else if (uuid == "de742df0-7013-12b5-444b-b1c580bb95a4") {
                    curTilt->color = "Pink";
                } else {
                    return;
                }
                
                curTilt->gravity = ENDIAN_CHANGE_U16(oBeacon.getMinor());
                curTilt->gravity *= 0.001;
                curTilt->temp = ENDIAN_CHANGE_U16(oBeacon.getMajor());
                curTiltIndex++;
                foundTilts[curTiltIndex] = curTilt;
                Serial.print("Found ");
                Serial.print(curTilt->color);
                Serial.print(" Tilt: ");
                Serial.print("Temp: ");
                Serial.print(String(curTilt->temp, 1));
                Serial.print(" SG: ");
                Serial.println(String(curTilt->gravity, 3));
            }
        }
    }
};

void indicateSection(int blinks = 3) {
    Serial.println();
    digitalWrite(LED_BUILTIN, LOW); // clear LED status
    // Blink three times in 1 second for begining scan
    for (int x = 0; x < (blinks * 2); x++) {
        digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN)); // update blinking LED
        delay(166.5);
    }
}

void cleanupAndSleep() {
    digitalWrite(LED_BUILTIN, LOW); // clear LED status
    Serial.flush();
    WiFi.disconnect(true);
    esp_bt_controller_disable();
    esp_sleep_enable_timer_wakeup(loopInterval * uS_TO_M_FACTOR);
    delay(1000); // delay to be sure timer was set
    esp_deep_sleep_start();
}

String formattedDateTime(struct tm *timeInfo) {
    time_t t = mktime(timeInfo);
    char buf[20];
    strftime(buf, sizeof(buf), "%m/%d/%Y %H:%M:%S", localtime(&t));
    return String(buf);
}

void setup() {
    Serial.begin(115200);
    while (!Serial) {
        ; // wait for serial port to connect. Needed for native USB port only
    }
    pinMode(LED_BUILTIN, OUTPUT); // set up onboard LED
    esp_bt_controller_enable(ESP_BT_MODE_BLE); // turn on bluetooth
    Serial.println("Initializing");

    // BLE scan for tilts
    indicateSection(1);
    Serial.println("Scanning for Tilts...");
    BLEDevice::init("Tilt Telephone");
    BLEScan *pBLEScan = BLEDevice::getScan(); //create new scan
    pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
    pBLEScan->setActiveScan(true); //active scan uses more power, but get results faster
    pBLEScan->setInterval(100);
    pBLEScan->setWindow(100); // less or equal setInterval value
    pBLEScan->start(bleScanTime, false);
    pBLEScan->clearResults(); // delete results fromBLEScan buffer to release memory

    // test without tilt present
    // foundTilts[0] = new Tilt;
    // foundTilts[0]->color = "Black";
    // foundTilts[0]->gravity = 1.072;
    // foundTilts[0]->temp = 66.0;
    // curTiltIndex = 0;

    if (curTiltIndex >= 0) {
        // Connect to WiFi
        indicateSection(2);
        Serial.print("Connect to ");
        Serial.println(ssid);
        WiFi.begin(ssid, password);
        Serial.print("Connecting");
        while (WiFi.status() != WL_CONNECTED) {
            delay(500);
            Serial.print(".");
        }
        Serial.println("");
        Serial.print("Connected. IP address: "); 
        Serial.println(WiFi.localIP());

        // Congifure time now that we have WiFi
        indicateSection(3);
        configTime(pstOffset_sec, daylightOffset_sec, ntpServer);
        Serial.println("Requesting current time...");
        struct tm timeInfo;
        if (!getLocalTime(&timeInfo)) {
            Serial.println(">>> Failed to obtain time !");
            cleanupAndSleep();
        }
        Serial.print("Current time: ");
        Serial.println(formattedDateTime(&timeInfo));
        
        // Connect to cloud service
        indicateSection(4);
        Serial.print("Connecting to ");
        Serial.println(host);
        WiFiClient client;
        if (!client.connect(host, httpPort)) {
            Serial.print(">>> Connection to ");
            Serial.print(host);
            Serial.println(" failed !");
            cleanupAndSleep();
        }
        Serial.println("\tConnected");

        // Log found tilts to cloud
        indicateSection(5);
        for (int x = 0; x <= curTiltIndex; x++)
        {
            String url = "/tilt";
            url += "?id=";
            url += privateKey;
            
            String content = "Temp=";
            content += String(foundTilts[x]->temp, 1);
            content += "&SG=";
            content += String(foundTilts[x]->gravity, 3);
            content += "&Color=";
            content += foundTilts[x]->color;
            content += "&Timepoint=";
            content += formattedDateTime(&timeInfo);

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
            }

            // Read all the lines of the reply from server and print them to Serial
            while(client.available()) {
                String line = client.readStringUntil('\r');
                Serial.print(line);
            }
            Serial.println();
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
