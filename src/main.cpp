#include <FirebaseESP8266.h>
#include <ESP8266WiFi.h>
#include <Wire.h>
#include <Adafruit_BMP085.h>
#include <DHT.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

#include <Config.h>

Adafruit_BMP085 bmp;
DHT dht(DHT_PIN, DHT_TYPE);
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);
FirebaseData firebaseData;

String _deviceUid;
String _lastSessionUid;
bool _isMeasuring = false;
int _interval = INTERVAL;

void initSerial() {
  Serial.begin(9600);
  Serial.setDebugOutput(true);
  Serial.println("Serial successfully initialized.");
}

void initWifi()
{
  Serial.printf("Attempting to connect to SSID: %s.\r\n", WIFI_SSID);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  
  while (WiFi.status() != WL_CONNECTED)
  {
    Serial.printf("You device failed to connect! Waiting 10 seconds to retry.\r\n");
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    delay(10000);
  }
  
  Serial.printf("Connected to wifi %s.\r\n", WIFI_SSID);
  Serial.printf("Your IP address is: ");
  Serial.print(WiFi.localIP());
  Serial.println();
}

void initBarometer() {
  if (!bmp.begin()) {
    Serial.println("Could not find a valid BMP085/BMP180 sensor, check wiring!");
  }
}

void initHumidity() {
  dht.begin();
}

void initFirebase() {
  Firebase.begin(FIREBASE_HOST, FIREBASE_AUTH);
  Firebase.reconnectWiFi(true);
}

void initTimeClient(){
  timeClient.begin();
}

bool registerDevice() {
  FirebaseJson data;
  data.addString("name", DEVICE_NAME);
  data.addString("mac", DEVICE_MAC_ADDRESS);
  data.addString("ownerId", DEVICE_OWNER_ID);
  data.addBool("isMeasuring", false);

  if (Firebase.pushJSON(firebaseData, "devices", data)) {
    Serial.println("PASSED");
    Serial.println(firebaseData.jsonData());
    _deviceUid = firebaseData.pushName();
    return true;
  } else {
    Serial.println("FAILED");
    Serial.println("REASON: " + firebaseData.errorReason());
    return false;
  }

  data.clear();
}

void checkRegistration() {
  QueryFilter query;
  query.orderBy("mac");
  query.equalTo(DEVICE_MAC_ADDRESS);
  query.limitToFirst(1);

  if (Firebase.getJSON(firebaseData, "/devices", query)) {
    if (firebaseData.jsonData() == "null") {
      if (registerDevice()) {
        checkRegistration();
      }
    } else {
      FirebaseJson deviceJson;
      FirebaseJsonObject jsonParseResult;
      String key;
      String value;
      
      deviceJson.setJsonData(firebaseData.jsonData());
      deviceJson.parse();

      size_t count = deviceJson.getJsonObjectIteratorCount();

      for (size_t i = 0; i < count; i++) {
        deviceJson.jsonObjectiterator(i,key,value);
        jsonParseResult = deviceJson.parseResult();

        if (jsonParseResult.type == "object") {
          _deviceUid = key;
        } else if (jsonParseResult.type == "string" && key == "lastSessionUid") {
          _lastSessionUid = value;
        } else if (jsonParseResult.type == "bool" && key == "isMeasuring") {
          _isMeasuring = value == "true";
        } else if (jsonParseResult.type == "int" && key == "interval") {
          _interval = value.toInt();
        }
      }

      deviceJson.clear();
    }
  } else {
    Serial.println(firebaseData.errorReason());
  }

  query.clear();
}

void setup() {
  initSerial();
  delay(2000);

  initWifi();
  initBarometer();
  initHumidity();
  initTimeClient();
  initFirebase();
  checkRegistration();

  firebaseData.clear();
}

void checkForChanges() {
  if (Firebase.getJSON(firebaseData, "/devices/" + _deviceUid)) {
    if (firebaseData.jsonData() != "null") {
      FirebaseJson deviceJson;
      FirebaseJsonObject jsonParseResult;
      String key;
      String value;
      
      deviceJson.setJsonData(firebaseData.jsonData());
      deviceJson.parse();

      size_t count = deviceJson.getJsonObjectIteratorCount();

      for (size_t i = 0; i < count; i++) {
        deviceJson.jsonObjectiterator(i,key,value);
        jsonParseResult = deviceJson.parseResult();

        if (jsonParseResult.type == "string" && key == "lastSessionUid") {
          _lastSessionUid = value;
        } else if (jsonParseResult.type == "bool" && key == "isMeasuring") {
          _isMeasuring = value == "true";
        } else if (jsonParseResult.type == "int" && key == "interval") {
          _interval = value.toInt();
        }
      }

      deviceJson.clear();
    }
  } else {
    Serial.println(firebaseData.errorReason());
  }
}

void measure() {
  timeClient.update();

  unsigned long epochTime = timeClient.getEpochTime();
  
  int pressure = bmp.readPressure();
  int temperature = bmp.readTemperature();
  float humidity = dht.readHumidity();

  Serial.printf("Pressure: %d Pa. Temperature: %d C. Humidity: %f \n", pressure, temperature, humidity);

  FirebaseJson data;
  data.addDouble("timestamp", epochTime);
  data.addInt("pressure", pressure);
  data.addInt("temperature", temperature);
  data.addString("sessionUid", _lastSessionUid);
  data.addDouble("humidity", 0.0f);

  if (Firebase.pushJSON(firebaseData, "measurements", data)) {
    Serial.println(firebaseData.jsonData());
  } else {
    Serial.println("ERROR: " + firebaseData.errorReason());
  }

  data.clear();
}

void loop() {

  if (!_isMeasuring) {
    delay(5000);
  } else {
    measure();
    delay(_interval);
  }

  checkForChanges();
}
