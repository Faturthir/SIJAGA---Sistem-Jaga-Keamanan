#include <Arduino.h>
#include <SPI.h>
#include <MFRC522.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <time.h>

// WiFi credentials
const char* ssid = "CPSRG";
const char* password = "CPSJAYA123";

// Pin definitions
const int buzzer = 32;
const int LED_R = 12;
const int LED_G = 14;
const int Getar = 15;
#define RELAY_PIN 21  
#define TRIG_PIN 26
#define ECHO_PIN 25
#define RST_PIN 4
#define SS_PIN 5

// API endpoints
const char* API_URL = "https://sijaga-railway-production.up.railway.app";
const char* PostUID = "";
const char* PostLog = "";  // Replace with actual endpoint
const char* endpointStatusBarang = "";  // Replace with actual endpoint
const char* UsageHistory = "";  // Replace with actual endpoint

// Status variables
String solenoidStatus = "KUNCI";
String uid = "";
String status = "";
bool isFirstTap = true;
String tap = "KUNCI";

// Vibration sensor variables
volatile bool getaranTerdeteksi = false;
unsigned long pulseDuration = 0;
int buzzerLevel = 0;

// RFID setup
MFRC522 mfrc522(SS_PIN, RST_PIN);
WiFiClientSecure client;

// Function prototypes
void connectWiFi();
void ukurJarak();
void sensorGetar();
void readRFID();
void statusBarang(String status);
void sendUidToDatabase(String uid);
void controlSolenoid(String uid);
bool checkAuthorization(String uid);
void logSolenoidStatus(String uid, String time, String status);
void historyPemakaian(String cardId, String status, String solenoidStatus);
String getFormattedTime();
void IRAM_ATTR handleGetar();

void setup() {
    Serial.begin(115200);
    
    // Initialize pins
    pinMode(RELAY_PIN, OUTPUT);
    digitalWrite(RELAY_PIN, HIGH);  // Lock the solenoid initially
    pinMode(TRIG_PIN, OUTPUT);
    pinMode(ECHO_PIN, INPUT);
    pinMode(LED_R, OUTPUT);
    pinMode(LED_G, OUTPUT);
    pinMode(Getar, INPUT);
    pinMode(buzzer, OUTPUT);
    digitalWrite(LED_R, LOW);
    digitalWrite(LED_G, HIGH);
    
    // Connect to WiFi
    connectWiFi();
    
    // Initialize RFID
    SPI.begin();
    mfrc522.PCD_Init();
    mfrc522.PCD_SetAntennaGain(MFRC522::RxGain_max);
    mfrc522.PCD_Reset();

    
    // Perform RFID self-test
    if (!mfrc522.PCD_PerformSelfTest()) {
        Serial.println("RFID self-test failed.");
    } else {
        Serial.println("RFID initialized successfully.");
    }
    
    // Configure time
    configTime(7 * 3600, 0, "pool.ntp.org", "time.nist.gov");
    
    // Set client to insecure (skip certificate validation)
    client.setInsecure();
    
    // Attach interrupt for vibration sensor
    attachInterrupt(digitalPinToInterrupt(Getar), handleGetar, RISING);
    
    Serial.println("System initialized. Solenoid is locked.");
}

void loop() {
    // Debug relay state
    Serial.println("Relay State: " + String(digitalRead(RELAY_PIN)));
    
    // Handle vibration sensor
    if (getaranTerdeteksi) {
        sensorGetar();
        getaranTerdeteksi = false;
    }
    
    // Read RFID and check distance
    readRFID();
    ukurJarak();
    
    // Reset RFID communication
    mfrc522.PICC_HaltA();
    mfrc522.PCD_StopCrypto1();
    
    delay(300);
}

void connectWiFi() {
    Serial.println("Connecting to WiFi...");
    WiFi.begin(ssid, password);
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nWiFi connected!");
        Serial.print("IP Address: ");
        Serial.println(WiFi.localIP());
    } else {
        Serial.println("\nWiFi connection failed!");
        ESP.restart();
    }
}

void ukurJarak() {
    // Clear the trigger pin
    digitalWrite(TRIG_PIN, LOW);
    delayMicroseconds(2);
    
    // Send 10μs pulse to trigger
    digitalWrite(TRIG_PIN, HIGH);
    delayMicroseconds(10);
    digitalWrite(TRIG_PIN, LOW);
    
    // Read the echo pin
    long duration = pulseIn(ECHO_PIN, HIGH);
    int distance_cm = (duration / 2) / 29.1;
    
    Serial.print("Distance: ");
    Serial.print(distance_cm);
    Serial.println(" cm");
    
    // Update status based on distance
    if (distance_cm < 52) {
        digitalWrite(LED_R, HIGH);
        digitalWrite(LED_G, LOW);
        status = "ADA BARANG";
    } else {
        digitalWrite(LED_R, LOW);
        digitalWrite(LED_G, HIGH);
        status = "TIDAK ADA BARANG";
    }
    
    Serial.println(status);
    Serial.println(" ");
    delay(1000);
}

void sensorGetar() {
    pulseDuration = pulseIn(Getar, HIGH);
    
    // Fuzzy logic membership functions
    float membershipRendah = constrain(map(pulseDuration, 0, 900, 1, 0), 0, 1);
    float membershipSedang = constrain(map(pulseDuration, 900, 1250, 0, 1), 0, 1);
    float membershipTinggi = constrain(map(pulseDuration, 1250, 3000, 0, 1), 0, 1);
    
    Serial.print("Pulse Duration: ");
    Serial.print(pulseDuration);
    Serial.print(" | Rendah: ");
    Serial.print(membershipRendah);
    Serial.print(" | Sedang: ");
    Serial.print(membershipSedang);
    Serial.print(" | Tinggi: ");
    Serial.println(membershipTinggi);
    
    // Determine buzzer level based on membership
    if (membershipTinggi > 0.5 || membershipSedang > 0.5) {
        buzzerLevel = 1;
    } else {
        buzzerLevel = 0;
    }
    
    // Control buzzer
    if (buzzerLevel == 1) {
        Serial.println("Alat Bergetar (Buzzer Aktif)");
        
        unsigned long startTime = millis();
        while (millis() - startTime < 3000) {
            digitalWrite(buzzer, HIGH);
            delay(100);
            digitalWrite(buzzer, LOW);
            delay(100);
        }
    } else {
        digitalWrite(buzzer, LOW);
    }
}

void readRFID() {
    uid = "";  // Clear previous UID
    
    // Check if a card is present
    if (mfrc522.PICC_IsNewCardPresent()) {
        Serial.println("Card detected");
        
        if (mfrc522.PICC_ReadCardSerial()) {
            // Read the UID
            for (byte i = 0; i < 4; i++) {
                uid += String(mfrc522.uid.uidByte[i] < 0x10 ? "0" : "");
                uid += String(mfrc522.uid.uidByte[i], HEX);
            }
            
            uid.toUpperCase();
            Serial.println("RFID Detected UID: " + uid);
            
            // Check if UID is not empty
            if (uid.length() > 0) {
                // Check authorization
                bool isAuthorized = checkAuthorization(uid);
                
                if (isAuthorized) {
                    Serial.println("UID Match: Authorized!");
                    controlSolenoid(uid);
                    sendUidToDatabase(uid);
                    statusBarang(status);
                    historyPemakaian(uid, status, solenoidStatus);
                } else {
                    Serial.println("UID Not Found: Access Denied.");
                    digitalWrite(buzzer, HIGH);
                    delay(1000);
                    digitalWrite(buzzer, LOW);
                    sendUidToDatabase(uid);
                }
            } else {
                Serial.println("UID is empty, cannot proceed.");
            }
        }
    }
}

void controlSolenoid(String uid) {
    if (checkAuthorization(uid)) {
        Serial.println("UID Authorized: " + uid);
        
        // Toggle solenoid state
        if (isFirstTap) {
            Serial.println("Unlocking solenoid...");
            digitalWrite(RELAY_PIN, LOW); 
            tap = "Terbuka";
        } else {
            Serial.println("Locking solenoid...");
            digitalWrite(RELAY_PIN, HIGH);  
            tap = "Tertutup";
        }
        
        // Update solenoid status
        solenoidStatus = tap;
        Serial.println("Solenoid Status: " + solenoidStatus);
        delay(1000);
        // Log solenoid status
        String currentTime = getFormattedTime();
        logSolenoidStatus(uid, currentTime, tap);
        // Check item status
        ukurJarak();
        // Toggle tap status
        isFirstTap = !isFirstTap;
    } else {
        Serial.println("Access Denied: Unauthorized UID");
        digitalWrite(buzzer, HIGH);
        delay(1000);
        digitalWrite(buzzer, LOW);
    }
}

bool checkAuthorization(String uid) {
    HTTPClient http;
    String query = String(API_URL) + "/history/users?uid=eq." + uid + "&select=*";
    
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
    
    HTTPClient http;
    String logEndpoint = String(API_URL) + PostLog;
    
    http.begin(client, logEndpoint);
    http.addHeader("Content-Type", "application/json");
    
    // Create JSON payload
    String payload = "{";
    payload += "\"uid\":\"" + uid + "\",";
    payload += "\"time\":\"" + time + "\",";
    payload += "\"status\":\"" + status + "\"";
    payload += "}";
    
    int httpResponseCode = http.POST(payload);
    
    if (httpResponseCode > 0) {
        String response = http.getString();
        Serial.println("Log Status Response: " + response);
    } else {
        Serial.print("Error on POST request. HTTP Response code: ");
        Serial.println(httpResponseCode);
    }
    
    http.end();
}

void sendUidToDatabase(String uid) {
    if (uid.length() == 0) {
        Serial.println("UID is empty, cannot send to database.");
        return;
    }

    HTTPClient http;
    String endpoint = "https://sijaga-railway-production.up.railway.app/card-id/create"; // Endpoint dari gambar
    http.begin(endpoint);
    http.addHeader("Content-Type", "application/json");

    // JSON payload
    String payload = "{\"cardId\":\"" + uid + "\"}";
    Serial.println("Payload: " + payload); // Debugging

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

void statusBarang(String status) {
    HTTPClient http;
    String endpoint = String(API_URL) + endpointStatusBarang;
    
    http.begin(endpoint);
    http.addHeader("Content-Type", "application/json");
    
    // Create JSON payload
    String payload = "{\"status\":\"" + status + "\"}";
    Serial.println("Status Payload: " + payload);
    
    int httpResponseCode = http.POST(payload);
    
    if (httpResponseCode > 0) {
        String response = http.getString();
        Serial.println("Status Update Response: " + response);
    } else {
        Serial.print("Error updating status. HTTP Response code: ");
        Serial.println(httpResponseCode);
    }
    
    http.end();
}

void historyPemakaian(String cardId, String status, String solenoidStatus) {
    HTTPClient http;
    String url = String(API_URL) + UsageHistory;
    
    http.begin(client, url);
    http.addHeader("Content-Type", "application/json");
    
    // Create JSON payload
    String payload = "{";
    payload += "\"card_id\":\"" + cardId + "\",";
    payload += "\"status\":\"" + solenoidStatus + "\",";
    payload += "\"availStatus\":\"" + status + "\"";
    payload += "}";
    
    Serial.println("History Payload: " + payload);
    
    int httpResponseCode = http.POST(payload);
    
    if (httpResponseCode > 0) {
        String response = http.getString();
        Serial.println("History Response: " + response);
    } else {
        Serial.print("Error sending history. HTTP Response code: ");
        Serial.println(httpResponseCode);
    }
    
    http.end();
}

String getFormattedTime() {
    time_t now = time(nullptr);
    struct tm* timeinfo = localtime(&now);
    char timeStr[20];
    strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", timeinfo);
    return String(timeStr);
}

void IRAM_ATTR handleGetar() {
    getaranTerdeteksi = true;
}
