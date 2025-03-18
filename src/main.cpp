#include <Arduino.h>
#include <SPI.h>
#include <MFRC522.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <time.h>
#include <WiFiManager.h>

const int buzzer = 32;
const int LED_R = 12;
const int LED_G = 14;
const int pinGetar = 15;
#define RELAY_PIN 21  
#define TRIG_PIN 26
#define ECHO_PIN 25
#define RST_PIN 4
#define SS_PIN 5

// Deklarasi variabel sensor getar
unsigned long pulseDuration = 0; 
int buzzerLevel = 0;

String API_URL = "";
String PostUID = ""; 
String PostLog = ""; 
String endpointStatusBarang = ""; //endpoint availability status
String UsageHistory = ""; //endpoint usage history
const int httpsPort = 443;
String solenoidStatus = "KUNCI";

MFRC522 mfrc522(SS_PIN, RST_PIN);
String uid = "";
String status = "";
bool isFirstTap = true;
bool refresh = false;
String tap = "KUNCI";

volatile bool getaranTerdeteksi = false;
WiFiClientSecure client;

void setupWiFi();
void ukurjarak();
void SensorGetar();
void ReadRFID();
void StatusBarang(String status);
void sendUidToDatabase(String uid);
void ControlSolenoid(String uid);
bool checkAuthorization(String uid);
void logSolenoidStatus(String uid, String time, String status);
void historypemakaian(String cardId, String status, String solenoidStatus);
String getFormattedTime();
void IRAM_ATTR handleGetar();

void setup() {
    Serial.begin(115200);
    setupWiFi();
    SPI.begin();
    mfrc522.PCD_Init();

    pinMode(RELAY_PIN, OUTPUT);
    digitalWrite(RELAY_PIN, HIGH);
    pinMode(TRIG_PIN, OUTPUT);
    pinMode(ECHO_PIN, INPUT);
    pinMode(LED_R, OUTPUT);
    pinMode(LED_G, OUTPUT);
    pinMode(buzzer, OUTPUT);
    pinMode(pinGetar, INPUT);
    digitalWrite(LED_R, LOW);
    digitalWrite(LED_G, HIGH);
    Serial.println("System initialized. Solenoid is locked.");
    
    mfrc522.PCD_SetAntennaGain(MFRC522::RxGain_max);
    if (!mfrc522.PCD_PerformSelfTest()) {
        Serial.println("RFID self-test failed.");
    } else {
        Serial.println("RFID initialized successfully.");
    }
    configTime(7 * 3600, 0, "pool.ntp.org", "time.nist.gov");
    attachInterrupt(digitalPinToInterrupt(pinGetar), handleGetar, RISING);
}

void loop() {
    Serial.println("Relay State: " + String(digitalRead(RELAY_PIN)));
    ReadRFID();
    ukurjarak();
    SensorGetar();
    mfrc522.PICC_HaltA();
    mfrc522.PCD_StopCrypto1();
    delay(100);
}

void setupWiFi() {
    WiFiManager wifiManager;
    wifiManager.setDebugOutput(true);

    Serial.println("Menghapus kredensial WiFi sebelumnya...");
    wifiManager.resetSettings();  // Hapus kredensial WiFi sebelumnya untuk memastikan AP muncul

    Serial.println("Memulai WiFiManager...");
    
    if (!wifiManager.autoConnect("Sijaga", "sijaga123")) {
        Serial.println("Gagal menyambungkan WiFi, perangkat akan restart...");
        delay(3000);
        ESP.restart();
    }

    Serial.println("Wi-Fi berhasil terhubung!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
}

void ukurjarak() {
    digitalWrite(TRIG_PIN, LOW);
    delayMicroseconds(2);
    digitalWrite(TRIG_PIN, HIGH);
    delayMicroseconds(10);
    digitalWrite(TRIG_PIN, LOW);

    long duration = pulseIn(ECHO_PIN, HIGH);
    int distance_cm = (duration /2 ) / 29.1;

    Serial.print("Distance: ");
    Serial.print(distance_cm);
    Serial.println(" cm");
    if (distance_cm < 52) {
        digitalWrite(LED_R, HIGH);
        digitalWrite(LED_G, LOW);
        status = "ADA BARANG";
    } 
    else {
        digitalWrite(LED_R, LOW);
        digitalWrite(LED_G, HIGH);
        status = "TIDAK ADA BARANG";
    }
    Serial.println(status);
    delay(1000);
}

void SensorGetar() {
    pulseDuration = pulseIn(pinGetar, HIGH); 
    float membershipRendah = constrain(map(pulseDuration,0,900,1,0),0,1);  
    float membershipSedang = constrain(map(pulseDuration,900, 1250, 0, 1), 0, 1);  
    float membershipTinggi = constrain(map(pulseDuration, 1250, 3000, 0, 1), 0, 1);  

    // Debugging untuk melihat hasil keanggotaan
    Serial.print("Pulse Duration: ");
    Serial.print(pulseDuration);
    Serial.print(" | Rendah: ");
    Serial.print(membershipRendah);
    Serial.print(" | Sedang: ");
    Serial.print(membershipSedang);
    Serial.print(" | Tinggi: ");
    Serial.println(membershipTinggi);

    if (membershipTinggi > 0.5) {
        buzzerLevel = 1; 
    } else if (membershipSedang > 0.5) {
        buzzerLevel = 1; 
    } else {
        buzzerLevel = 0; 
    }

     buzzerLevel = (membershipTinggi > 0.5 || membershipSedang > 0.5) ? 1 : 0;
     static unsigned long buzzerStartTime = 0;
     static bool buzzerState = false;
 
     if (buzzerLevel == 1) {
         if (millis() - buzzerStartTime >= 100) { 
             buzzerStartTime = millis();
             buzzerState = !buzzerState;  // Toggle buzzer
             digitalWrite(buzzer, buzzerState);
         }
     } else {
         digitalWrite(buzzer, LOW);
     }
 }

void ReadRFID() {
    uid = ""; 
    if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
        byte cardUID[4];
        for (byte i = 0; i < 4; i++) {
            cardUID[i] = mfrc522.uid.uidByte[i];
        }
        Serial.print("Card UID: ");
        for (byte i = 0; i < 4; i++) {
            Serial.print(cardUID[i], HEX);
            Serial.print(" ");
            uid += String(cardUID[i] < 0x10 ? "0" : "");
            uid += String(cardUID[i], HEX);
        }
        Serial.println();
        uid.toUpperCase();
        Serial.println("RFID Detected UID: " + uid);
        if (uid.length() == 0) {
            Serial.println("UID is empty, cannot proceed.");
            return;
        }
        bool isAuthorized = checkAuthorization(uid);
        if (isAuthorized) {
            Serial.println("UID Match: Authorized!");
            ControlSolenoid(uid);
            sendUidToDatabase(uid);
            StatusBarang(status);
            historypemakaian(uid, status, solenoidStatus); 
        } else {
            Serial.println("UID Not Found: Access Denied.");
            digitalWrite(buzzer, HIGH);
            delay(1000);
            digitalWrite(buzzer, LOW);
            sendUidToDatabase(uid);
        }
        }
    }            

void ControlSolenoid(String uid) {
    if (checkAuthorization(uid)) { 
        Serial.println("UID Authorized: " + uid);
        if (isFirstTap) {
            Serial.println("Unlocking solenoid...");
            digitalWrite(RELAY_PIN, LOW); 
            tap = "BUKA";
        } else {
            Serial.println("Locking solenoid...");
            digitalWrite(RELAY_PIN, HIGH); 
            tap = "TUTUP";
        }

        solenoidStatus = tap;
        Serial.println("Solenoid Status: " + solenoidStatus);

        delay(2000); 
        String currentTime = getFormattedTime();
        logSolenoidStatus(uid, currentTime, tap);
        ukurjarak();
        isFirstTap = !isFirstTap;
    } else {
        Serial.println("Access Denied: Unauthorized UID");
        digitalWrite(buzzer, HIGH); 
        delay(1000);
        digitalWrite(buzzer, LOW);
    }
    return;
}

bool checkAuthorization(String uid) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("Wi-Fi disconnected, cannot check authorization.");
        return false;
    }

    HTTPClient http;
    String query = API_URL + "/history/users?uid=eq." + uid + "&select=*";
    http.begin(client, query);
    int httpResponseCode = http.GET();
    if (httpResponseCode > 0) {
        String payload = http.getString();
        if (payload.indexOf(uid) > -1) {  
            Serial.println("UID Found: Authorized.");
            http.end();
            return true;
        } else {
            Serial.println("UID Not Authorized.");
        }
    } else {
        Serial.print("Error on GET request. HTTP Response code: ");
        Serial.println(httpResponseCode);
    }
    http.end();
    return false;  
}

void logSolenoidStatus(String uid, String time, String status) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("Wi-Fi disconnected, cannot log status.");
        return;
    }

    HTTPClient http;
    String logEndpoint = API_URL + PostLog; 
    http.begin(logEndpoint); 
    http.addHeader("Content-Type", "application/json");

    // JSON payload
    String payload = "{";
    payload += "\"uid\":\"" + uid + "\",";
    payload += "\"time\":\"" + time + "\",";
    payload += "\"status\":\"" + status + "\"";
    payload += "}";

    int httpResponseCode = http.POST(payload);
    if (httpResponseCode > 0) {
        String response = http.getString();
        Serial.println("POST Response:");
        Serial.println(response);
    } else {
        Serial.print("Error on POST request. HTTP Response code: ");
        Serial.println(httpResponseCode);
    }
    http.end();
}

void sendUidToDatabase(String uid) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("Wi-Fi disconnected, cannot send UID to database.");
        return;
    }

    if (uid.length() == 0) {
        Serial.println("UID is empty, cannot send to database.");
        return;
    }

    HTTPClient http;
    String endpoint = "https://sijaga-railway-production.up.railway.app/card-id/create";
    http.begin(endpoint);
    http.addHeader("Content-Type", "application/json");

    String payload = "{\"cardId\":\"" + uid + "\"}";
    Serial.println("Payload: " + payload); 
    int httpResponseCode = http.POST(payload);
    if (httpResponseCode > 0) {
        String response = http.getString();
        Serial.println("POST Response:");
        Serial.println(response);
    } else {
        Serial.print("Error on sending POST: ");
        Serial.println(httpResponseCode);
    }
    http.end();
}

String getFormattedDate() {
    time_t now = time(nullptr);
    struct tm* timeinfo = localtime(&now);
    char dateStr[11];
    strftime(dateStr, sizeof(dateStr), "%Y-%m-%d", timeinfo);
    return String(dateStr);
}

String getFormattedTime() {
    time_t now = time(nullptr);
    struct tm* timeinfo = localtime(&now);
    char timeStr[20];
    strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", timeinfo);
    return String(timeStr);
}

void StatusBarang(String status) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("Wi-Fi disconnected, cannot send status to database.");
        return;
    }

    HTTPClient http;
    String endpoint = API_URL + endpointStatusBarang;
    http.begin(endpoint);
    http.addHeader("Content-Type", "application/json"); 

    // JSON payload
    String payload = "{";
    payload += "\"status\":\"" + status + "\"";
    payload += "}";

    // Debugging URL dan Payload
    Serial.println("Endpoint: " + endpoint);
    Serial.println("Payload: " + payload);
    int httpResponseCode = http.POST(payload);
    if (httpResponseCode > 0) {
        String response = http.getString();
        Serial.println("POST Response:");
        Serial.println(response);
    } else {
        Serial.print("Error on POST request. HTTP Response code: ");
        Serial.println(httpResponseCode);
    }
    http.end();
}

void historypemakaian(String cardId, String status, String solenoidStatus) {
    String payload = "{";
    payload += "\"card_id\":\"" + cardId + "\","; 
    payload += "\"status\":\"" + solenoidStatus + "\",";  
    payload += "\"availStatus\":\"" + status + "\""; 
    payload += "}";
    Serial.println("Payload to send:");
    Serial.println(payload);

    if (WiFi.status() == WL_CONNECTED) { 
        HTTPClient http;

        String url = API_URL + UsageHistory; 
        http.begin(url); 
        http.addHeader("Content-Type", "application/json");
        int httpResponseCode = http.POST(payload);
        if (httpResponseCode > 0) {
            Serial.print("HTTP Response code: ");
            Serial.println(httpResponseCode);
            String response = http.getString();
            Serial.println("Server response:");
            Serial.println(response);
        } else {
            Serial.print("Error on sending POST: ");
            Serial.println(httpResponseCode);
        }
        http.end();
    } else {
        Serial.println("WiFi Disconnected. Cannot send data.");
    }
}

void IRAM_ATTR handleGetar() {
    getaranTerdeteksi = true; 
}