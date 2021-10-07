#include <ArduinoMqttClient.h>
#include <time.h>
#include <stdarg.h>
#include <SPI.h>
#include <WiFiNINA.h>
#include "SCD30.h"
#include <RTCZero.h>
#include <utility/wifi_drv.h>

#include "./ntp.h"
#include "./base64.h"
#include "./utils.h"
#include "./sha256.h"

int getHubHostName(char *scopeId, char* deviceId, char* key, char *hostName);

#include "arduino_secrets.h"
///////please enter your sensitive data in the Secret tab/arduino_secrets.h
char ssid[] = SECRET_SSID;        // your network SSID (name)
char pass[] = SECRET_PASS;    // your network password (use for WPA, or use as key for WEP)

String iothubHost;
String deviceId;
String sharedAccessKey;
String topic;
int port = 8883;
int count = 0;
bool timeSet = false;

WiFiSSLClient wifiClient;
MqttClient mqttClient(wifiClient);

WiFiUDP wifiUdp;
// create an NTP object
NTP ntp(wifiUdp);
// Create an rtc object
RTCZero rtc;

#include "./iotc_dps.h"

// create an IoT Hub SAS token for authentication
String createIotHubSASToken(char *key, String url, long expire){
    url.toLowerCase();
    String stringToSign = url + "\n" + String(expire);
    int keyLength = strlen(key);

    int decodedKeyLength = base64_dec_len(key, keyLength);
    char decodedKey[decodedKeyLength];

    base64_decode(decodedKey, key, keyLength);

    Sha256 *sha256 = new Sha256();
    sha256->initHmac((const uint8_t*)decodedKey, (size_t)decodedKeyLength);
    sha256->print(stringToSign);
    char* sign = (char*) sha256->resultHmac();
    int encodedSignLen = base64_enc_len(HASH_LENGTH);
    char encodedSign[encodedSignLen];
    base64_encode(encodedSign, sign, HASH_LENGTH);
    delete(sha256);

    return (char*)F("SharedAccessSignature sr=") + url + (char*)F("&sig=") + urlEncode((const char*)encodedSign) + (char*)F("&se=") + String(expire);
}

// get the time from NTP and set the real-time clock on the MKR10x0
void getTime() {
    Serial.println(F("Getting the time from time service: "));

    ntp.begin();
    ntp.update();
    Serial.print(F("Current time: "));
    Serial.print(ntp.formattedTime("%d. %B %Y - "));
    Serial.println(ntp.formattedTime("%A %T"));

    rtc.begin();
    rtc.setEpoch(ntp.epoch());
    timeSet = true;
}

void setup() {
  Serial.begin(115200);

  //LED SetUp
  WiFiDrv::pinMode(25,OUTPUT);  // Red LED
  WiFiDrv::pinMode(26,OUTPUT);  // Green LED
  WiFiDrv::pinMode(27,OUTPUT);  // Blue LED

  // attempt to connect to Wifi network:
  Serial.print("Attempting to connect to WPA SSID: ");
  Serial.println(ssid);
  while (WiFi.begin(ssid, pass) != WL_CONNECTED) {
    // failed, retry
    Serial.print(".");
    WiFiDrv::analogWrite(25,0);
    WiFiDrv::analogWrite(26,0);
    WiFiDrv::analogWrite(27,10);
    
    delay(5000);
  }

  Serial.println("You're connected to the network");
  Serial.println();

  // get current UTC time
  getTime();

  // connect DPS
  Serial.println("Getting IoT Hub host from Azure IoT DPS");
  deviceId = iotc_deviceId;
  sharedAccessKey = iotc_deviceKey;
  char hostName[64] = {0};
  getHubHostName((char*)iotc_scopeId, (char*)iotc_deviceId, (char*)iotc_deviceKey, hostName);
  iothubHost = hostName;

//  Serial.println(deviceId);
//  Serial.println(sharedAccessKey);
//  Serial.println(iothubHost);

  // create SAS token and user name for connecting to MQTT broker
  String url = iothubHost + urlEncode(String((char*)F("/devices/") + deviceId).c_str());
  char *devKey = (char *)sharedAccessKey.c_str();
  long expire = rtc.getEpoch() + 864000;
  String sasToken = createIotHubSASToken(devKey, url, expire);
  String username = iothubHost + "/" + deviceId + (char*)F("/api-version=2018-06-30");

//  Serial.println(sasToken);
//  Serial.println(username);

  //mqtt connect
  mqttClient.setId(deviceId);
  mqttClient.setUsernamePassword(username,sasToken);
  
  Serial.print("Attempting to connect to the MQTT broker: ");
  Serial.println(iothubHost);

  if (!mqttClient.connect(iothubHost.c_str(), port)) {
    Serial.print("MQTT connection failed! Error code = ");
    Serial.println(mqttClient.connectError());

    WiFiDrv::analogWrite(25,10);
    WiFiDrv::analogWrite(26,0);
    WiFiDrv::analogWrite(27,0);

    while (1);
  }

  Serial.println("You're connected to the MQTT broker!");
  Serial.println();

  topic="devices/"+deviceId+"/messages/events/";

  //SCD30 initialize
  Wire.begin();
  Serial.println("SCD30 Raw Data");
  scd30.initialize();

}

void loop() {
  float result[3] = {0};
  String payload="";

  // get data
  if (scd30.isAvailable()) {
    scd30.getCarbonDioxideConcentration(result);
    Serial.print("Co2: ");
    Serial.print(result[0]);
    Serial.println(" ppm");
    Serial.println(" ");
    Serial.print("temperature: ");
    Serial.print(result[1]);
    Serial.println(" ℃");
    Serial.println(" ");
    Serial.print("humidity: ");
    Serial.print(result[2]);
    Serial.println(" %");
    Serial.println(" ");
    Serial.println(" ");
    Serial.println(" ");

    // build json data
    payload = "{";
    payload += "\"co2\":\"" + String(result[0],2)+"\"";
    payload += ",";
    payload += "\"temperature\":\"" + String(result[1],2)+"\"";
    payload += ",";
    payload += "\"humidity\":\"" + String(result[2],2)+"\"";
    payload += "}";

    //Serial.println(payload);
  }

  Serial.print("Sending message to topic: ");
  Serial.println(topic);
  Serial.print(payload);
  Serial.print(" ");
  Serial.println(count);

  // send message, the Print interface can be used to set the message contents
  mqttClient.beginMessage(topic);
  mqttClient.print(payload);
  mqttClient.print(count);
  mqttClient.endMessage();

  Serial.println();

  count++;

  WiFiDrv::analogWrite(25,0);
  WiFiDrv::analogWrite(26,10);
  WiFiDrv::analogWrite(27,0);

  delay(2000);

  
  WiFiDrv::analogWrite(25,0);
  WiFiDrv::analogWrite(26,0);
  WiFiDrv::analogWrite(27,0);
  
  delay(58000);

}
