//WiFi libs
#include <ESP8266WiFi.h>
#include <DNSServer.h>            //Local DNS Server used for redirecting all requests to the configuration portal
#include <ESP8266WebServer.h>     //Local WebServer used to serve the configuration portal
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager WiFi Configuration Magic

//ADS 1115 libs
#include <Adafruit_ADS1015.h>

//I2C libs
#include <Wire.h>

// GPS libs
#include <TinyGPS++.h>

// The TinyGPS++ object
TinyGPSPlus gps;

//GeoHash libs
#include <arduino-geohash.h>
GeoHash hasher_coarse(7);
GeoHash hasher_normal(8);
GeoHash hasher_fine(9);

// Wifi Config
char MEASUREMENT_NAME[34] = "alphasense2";
const char* AutoConnectAPName = "AutoConnectAP";
const char* AutoConnectAPPW = "password";

// Display libs
#include <Adafruit_GFX.h>

// Mit OLED
#include <Adafruit_SSD1306.h>
#define OLED_RESET 4
Adafruit_SSD1306 display(LED_BUILTIN);

// Config Messung
const int MessInterval = 2000; // Zeit in ms zwischen den einzelnen gemittelten Messwerten
const int Messwerte_Mittel = 10; // Anzahl der Messungen, die zu einem einzelnen Messwert gemittelt werden
const int MessDelay = MessInterval / Messwerte_Mittel; /* Pause zwischen Messungen, die zu einem Messwert gemittelt werden -> alle "MessDelay" ms ein Messwert,
bis Messwerte_Mittel mal gemessen wurde, dann wird ein Mittelwert gebildet und ausgegeben. */
unsigned long time;
unsigned long counter = 0;

int WarmUp = 10000;

// Sensor Config
const char* SN1    = "NO2_WE";
const char* SN2    = "O3_WE";
const char* SN3    = "NO_WE";
const char* TEMP   = "PT1000";
const char* SN1_AE = "NO2_AE";
const char* SN2_AE = "O3_AE";
const char* SN3_AE = "NO_AE";

// Server Config
const char* InfluxDB_Server_IP = "130.149.67.141";
const int InfluxDB_Server_Port = 8086;
const char* InfluxDB_Database = "MESSCONTAINER";

int conState = 0;
//GPS Variablen
String latitude, longitude, Geohash_fine, Geohash_normal, Geohash_coarse, Position,last_lat, last_lng;

String connectionIndicator="-", gpsIndicator="+";
// Initialisiert WiFiClient als "client"
WiFiClient client;

// Initialisiert I2C ADS1115 als "ads"; PORTS am ESP8266: SDA = D2, SCL = D1
Adafruit_ADS1115 ads_A(0x48);
Adafruit_ADS1115 ads_B(0x49);

// Umrechnung der 16Bit Werte des ADS1115 mit dem entsprechenden GAIN abhängigen Faktor
float Umrechnungsfaktor;

String Uploadstring, serverResponse;

char incomingChar; // Dummy variable um Serverresponse zu lesen, aufs Display auszugeben und ggf. auf Serial auszugeben.
int  gpsIsUpdated=0, gpsIsValid=0, gpsIfTriggered=0, gpsAge=20000, gpsSpeed=0, gpsHour=0, gpsMinute=0; // Debugvariablen fuer das GPS

float SN1_value, SN2_value, SN3_value, Temp_value;      // globale ADC Variablen
float SN1_AE_value,SN2_AE_value,SN3_AE_value;           // fuer Ausgabe am Display

float getUmrechnungsfaktor(){
  float Faktor;
  if(ads_A.getGain()==GAIN_TWOTHIRDS){
    Faktor = 0.1875;
  }
  if(ads_A.getGain()==GAIN_ONE){
    Faktor = 0.125;
  }
  if(ads_A.getGain()==GAIN_TWO){
    Faktor = 0.0625;
  }
  if(ads_A.getGain()==GAIN_FOUR){
    Faktor = 0.03125;
  }
  return Faktor;
}

void WiFiStart(){
  /* Baut Verbindung mit dem letzten bekannten WiFi auf, wenn nicht vorhanden
  wird eigener AP erstellt -> IP: 192.168.4.1*/
  WiFiManager wifiManager;
  wifiManager.autoConnect(AutoConnectAPName, AutoConnectAPPW);
}
void Verbindungstest(){
  // Verbindungstest zum InfluxSB Server
  conState = client.connect(InfluxDB_Server_IP, InfluxDB_Server_Port);
  // Verbindung erfolgreich
  if (conState > 0) {
    Serial.println("Verbindung zum InfluxDB Server hergestellt");
    client.stop();
  }
  //Verbindung nicht erfolgreich
  else {
    Serial.print("Keine Verbindung zum InfluxDB Server, Error #");
    Serial.println(conState);
  }
}
void Messung(){

  float SN1_Integral = 0;
  float SN2_Integral = 0;
  float SN3_Integral = 0;
  float TEMP_Integral = 0;

  float SN1_AE_Integral = 0;
  float SN2_AE_Integral = 0;
  float SN3_AE_Integral = 0;

  //Messung über i = Messwerte_Mittel Messwerte
  for (int i = 0; i < Messwerte_Mittel; i++) {
    // Abrufen der Analogeinganswerte am ADS1115
    // Abrufen der an den Pins anliegenden Werte &
    //Integration über Anzahl der zu mittelnden Messwerte

    //ADC_A
    SN1_AE_Integral+=ads_A.readADC_SingleEnded(1);  // NO2   Aux 1
    SN2_AE_Integral+=ads_A.readADC_SingleEnded(2);  // O3/NO Aux 2
    SN3_AE_Integral+=ads_A.readADC_SingleEnded(3);  // NO    Aux 3
    TEMP_Integral  +=ads_A.readADC_SingleEnded(0);  // PT+

    //ADC_B
    SN1_Integral+=ads_B.readADC_SingleEnded(1); // NO2   Work 1
    SN2_Integral+=ads_B.readADC_SingleEnded(2); // O3/NO Work 2
    SN3_Integral+=ads_B.readADC_SingleEnded(3); // NO    Work 3

    delay(15);
  }

   SN1_Integral = SN1_Integral / Messwerte_Mittel; // Bildung des arithmetischen Mittels
   SN2_Integral = SN2_Integral / Messwerte_Mittel;
   SN3_Integral = SN3_Integral / Messwerte_Mittel;
   TEMP_Integral = TEMP_Integral / Messwerte_Mittel;
   SN1_AE_Integral = SN1_AE_Integral / Messwerte_Mittel; // Bildung des arithmetischen Mittels
   SN2_AE_Integral = SN2_AE_Integral / Messwerte_Mittel;
   SN3_AE_Integral = SN3_AE_Integral / Messwerte_Mittel;

   Umrechnungsfaktor = getUmrechnungsfaktor();

   SN1_value = Umrechnungsfaktor * SN1_Integral;
   SN2_value = Umrechnungsfaktor * SN2_Integral;
   SN3_value = Umrechnungsfaktor * SN3_Integral;
   Temp_value = Umrechnungsfaktor * TEMP_Integral;

   SN1_AE_value = Umrechnungsfaktor * SN1_AE_Integral;
   SN2_AE_value = Umrechnungsfaktor * SN2_AE_Integral;
   SN3_AE_value = Umrechnungsfaktor * SN3_AE_Integral;
}

void Upload(String Daten){
  // Print the buffer on the serial line to see how it looks
  Serial.print("Sending following dataset to InfluxDB: ");
  Serial.println(Daten);

  //send to InfluxDB
  // Verbindungstest mit dem InfluxDB Server connect() liefert bool false / true als return
  conState = client.connect(InfluxDB_Server_IP, InfluxDB_Server_Port);

  //Sende HTTP Header und Buffer
  if (millis()-time>=WarmUp) {
    client.println("POST /write?db=MESSCONTAINER HTTP/1.1");
    client.println("User-Agent: esp8266/0.1");
    client.println("Host: localhost:8086");
    client.println("Accept: */*");
    client.print("Content-Length: " + String(Uploadstring.length()) + "\r\n");
    client.println("Content-Type: application/x-www-form-urlencoded");
    client.println(); /*HTTP Header und Body müssen durch Leerzeile getrennt werden*/
    client.println(Daten); // Übermittlung des eigentlichen Data-Strings
    client.flush(); //
    delay(1000); //wait for server to process data

    // Antwort des Servers wird gelesen, ausgegeben und anschließend die Verbindung geschlossen
    Serial.println("Antwort des Servers");
    serverResponse= "";
    while(client.available()) { // Empfange Antwort
      incomingChar=char(client.read());
      serverResponse += incomingChar;
    }
    Serial.println(serverResponse);
  }
  Serial.println();
  client.stop();
}

void updateDisplay(){

if (gpsAge < 10000){
  gpsIndicator = "+";
}
else{
  gpsIndicator = "-";
}

if (conState = 1){
  connectionIndicator = "+";
}
else{
  connectionIndicator = "-";
}

display.clearDisplay();
display.setCursor(0,0);
display.print("C:"+ connectionIndicator);
display.setCursor(20,0);
display.println("G:"+ gpsIndicator);

display.println("NO2: "+ String(SN1_value));
display.println("Speed: "+ String(gpsSpeed));

display.display();
}

void checkGPS(){
  gpsIfTriggered = 0;
  while (Serial.available()){
    gps.encode(Serial.read());
    // gpsIsUpdated   = gps.location.isUpdated();
    // gpsIsValid     = gps.location.isValid();
    gpsAge         = gps.location.age();

    if (gps.location.isValid()){

      gpsIfTriggered = 1;
      latitude  = String(gps.location.lat(),6);
      longitude = String(gps.location.lng(),6);
      gpsSpeed  = gps.speed.kmph();
      gpsHour   = gps.time.hour();
      gpsMinute = gps.time.minute();
       if ((longitude != last_lng) or (latitude != last_lat)){
         gpsIsUpdated = 1;
       }

      last_lat = latitude;
      last_lng = longitude;

      Geohash_fine = hasher_fine.encode(gps.location.lat(), gps.location.lng());
      Geohash_normal = hasher_normal.encode(gps.location.lat(), gps.location.lng());
      Geohash_coarse = hasher_coarse.encode(gps.location.lat(), gps.location.lng());
    }
  }

}

void generateUploadString(){
  // Messwerte in String zusammenbauen
  Uploadstring =
  String(MEASUREMENT_NAME) + "," +

  //Tags
  "host=esp8266" + "," +
  "hour=" + String(gpsHour) + "," +
  "minute=" + String(gpsMinute) + "," +
  "geohash_fine="   + Geohash_fine   + "," +
  "geohash=" + Geohash_normal + "," +
  "geohash_coarse=" + Geohash_coarse +
   " " + // Leerzeichen trennt Tags und Fields

  //Messwerte
  SN1 +    "=" + String(SN1_value , 4) + "," +
  SN2 +    "=" + String(SN2_value, 4) + "," +
  SN3 +    "=" + String(SN3_value,4) + "," +
  TEMP +   "=" + String(Temp_value, 4) + "," +
  SN1_AE + "=" + String(SN1_AE_value, 4) + "," +
  SN2_AE + "=" + String(SN2_AE_value, 4) + "," +
  SN3_AE + "=" + String(SN3_AE_value, 4) + "," +

  //Position & Speed
  "lng=" + longitude + "," +
  "lat=" + latitude + "," +
  "speed=" + gpsSpeed;
}

void setup() {
  Serial.begin(9600);
  Serial.println("Setup");

  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0,0);
  display.clearDisplay();

  display.println("Boot...");
  display.display();
  delay(1000);

  // Startet Kommunikation mit IDC über Port 4 und 5 auf ESP8266 und setzt Gain des ADCs
  ads_A.setGain(GAIN_TWO);        // 1x gain   +/- 4.096V  1 bit = 2mV      0.125mV
  ads_B.setGain(GAIN_TWO);
  ads_A.begin();
  ads_B.begin();

  display.println("Start ADCs...");
  display.display();
  delay(1000);

  display.println("Start Wifimanager...");
  display.display();
  delay(1000);

  WiFiStart();

  //if you get here you have connected to the WiFi

  display.println("Connected to Wifi");
  display.print("IP:"); display.println(WiFi.localIP());
  display.display();
  delay(1000);

  display.print("Start Loop()");
  display.display();
  delay(1000);
  display.clearDisplay();
  display.display();
  time = millis();
}

void loop() {
Messung();
checkGPS();

if (gpsIsUpdated == 1){
generateUploadString();
Upload(Uploadstring);
gpsIsUpdated = 0;
}
updateDisplay();
}
