#include <Arduino.h>
#include <NewPing.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <WebSocketsServer.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>

#define MAX_CM_DISTANCE 400
#define MEDIAN_WIDTH_WINDOW 5
#define CONST_WAVE_SPEED 58.3 //(2/(soundSpeed(cm/us)))
#define SECS_BTW_SAMPLES 3 //time between sensor samples in seconds
#define MIN_DISTANCE_RUDYS_TANK 20  //min distance(cm) from water to sensor in Rudy's tank (CALIBRATION)
#define MAX_DISTANCE_RUDYS_TANK 165 //max distance from(cm) water to sensor in Rudy's tank (CALIBRATION)
#define MIN_DISTANCE_RAQUELS_TANK 10   //min distance from(cm) water to sensor in Rudy's tank (CALIBRATION)
#define MAX_DISTANCE_RAQUELS_TANK 165  //max distance from(cm) water to sensor in Rudy's tank (CALIBRATION)

//Network credentials
const char* ssid = "WARP";
const char* password = "elluismaestonto";
IPAddress ip(192,168,1,100);
IPAddress gateway(192,168,1,1);
IPAddress subnet(255,255,255,0);

//Create AsyncWebServer object on port 80
AsyncWebServer server(80);
//Create WebSocketServer
AsyncWebSocket ws("/ws");

NewPing Lvl_sensor_UP(GPIO_NUM_19, GPIO_NUM_19, MAX_CM_DISTANCE); //Ultrasonic Sensor Rudy's water tank
NewPing Lvl_sensor_DOWN(GPIO_NUM_32, GPIO_NUM_32, MAX_CM_DISTANCE); //Ultrasonic Sensor Raquel's water tank

//Timer0 variables
volatile bool timerflag = false; 
hw_timer_t * timer = NULL;
//Interruption Subrutine Request Function
void timer_ISR(){
  timerflag = true;
}

void setData(AsyncWebSocketClient * client, String request)
{
  StaticJsonDocument<200> doc;
  DeserializationError error = deserializeJson(doc, request);
  if (error) { return; }
 
  int id = doc["id"];
  bool ledStatus = doc["status"];
  Serial.println(id);
  Serial.println(ledStatus);
}
 
void getData(AsyncWebSocketClient * client, String request)
{
   String response;
   StaticJsonDocument<300> jsonDoc;
   jsonDoc["id"] = random(0,10);
   jsonDoc["status"] = random(0,2);
   serializeJson(jsonDoc, response);
   
   client->text(response);
}

void ProcessRequest(AsyncWebSocketClient * client, String request)
{
   Serial.println(request);
   StaticJsonDocument<200> doc;
   DeserializationError error = deserializeJson(doc, request);
   if (error) { return; }
   
   String command = doc["command"];
   if(command == "Set")
   {
      setData(client, request);
   }
   if(command == "Get")
   {
      getData(client, request);
   }
}

void onWsEvent(AsyncWebSocket * server, AsyncWebSocketClient * client, AwsEventType type, void * arg, uint8_t *data, size_t len){ 
   if(type == WS_EVT_CONNECT){
      //Serial.printf("ws[%s][%u] connect\n", server->url(), client->id());
      client->printf("Hello Client %u :)", client->id());
      client->ping();
   } else if(type == WS_EVT_DISCONNECT){
      //Serial.printf("ws[%s][%u] disconnect: %u\n", server->url(), client->id());
   } else if(type == WS_EVT_ERROR){
      //Serial.printf("ws[%s][%u] error(%u): %s\n", server->url(), client->id(), *((uint16_t*)arg), (char*)data);
   } else if(type == WS_EVT_PONG){
      //Serial.printf("ws[%s][%u] pong[%u]: %s\n", server->url(), client->id(), len, (len)?(char*)data:"");
   } else if(type == WS_EVT_DATA){
      AwsFrameInfo * info = (AwsFrameInfo*)arg;
      String msg = "";
      if(info->final && info->index == 0 && info->len == len){
         if(info->opcode == WS_TEXT){
            for(size_t i=0; i < info->len; i++) {
               msg += (char) data[i];
            }
         } else {
            char buff[3];
            for(size_t i=0; i < info->len; i++) {
               sprintf(buff, "%02x ", (uint8_t) data[i]);
               msg += buff ;
            }
         }
 
         if(info->opcode == WS_TEXT)
         ProcessRequest(client, msg);
         
      } else {
         //message is comprised of multiple frames or the frame is split into multiple packets
         if(info->opcode == WS_TEXT){
            for(size_t i=0; i < len; i++) {
               msg += (char) data[i];
            }
         } else {
            char buff[3];
            for(size_t i=0; i < len; i++) {
               sprintf(buff, "%02x ", (uint8_t) data[i]);
               msg += buff ;
            }
         }
         Serial.printf("%s\n",msg.c_str());
 
         if((info->index + len) == info->len){
            if(info->final){
               if(info->message_opcode == WS_TEXT)
               ProcessRequest(client, msg);
            }
         }
      }
   }
}
 
void InitWebSockets()
{
   ws.onEvent(onWsEvent);
   server.addHandler(&ws);
   Serial.println("WebSocket server started");
}

void InitServer(){
   server.serveStatic("/", SPIFFS, "/").setDefaultFile("index.html");
      
   server.onNotFound([](AsyncWebServerRequest *request) {
      request->send(400, "text/plain", "Not found");
   });
 
   server.begin();
   Serial.println("HTTP server started");
}

void setup() {
  //Serial port for debugging purposes 
  Serial.begin(9600);

  //Mount SPIFFS for storage Web HTML
  if(!SPIFFS.begin(true)){
    Serial.println("An ERROR has ocurred while mounting SPIFFS");
  }

  // Connect to Wi-Fi
  WiFi.config(ip, gateway, subnet);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi...");
  }

  // Print ESP Local IP Address
  Serial.println(WiFi.localIP());

  //Start embedded Async Web Server and Websocket on ESP32
  InitServer();
  InitWebSockets();

  //Configuration HARDWARE TIMER O for sampling rate
  timer = timerBegin(0, 8000, true);  //timer0, frec(80MHz)/80000 = 10kHz, counts up
  timerAttachInterrupt(timer, &timer_ISR, true); //timer0 attach ISR timer_ISR, edge
  timerAlarmWrite(timer, 10000 * SECS_BTW_SAMPLES, true); //timer0, frec(10kHz)*10000 = 1Hz(1s), autoreaload
  timerAlarmEnable(timer); //start timer0
}

void read_Sensors(){
   //float water_lvl_up = Lvl_sensor_UP.ping_median(MEDIAN_WIDTH_WINDOW, MAX_CM_DISTANCE)/CONST_WAVE_SPEED;
   //int wtr_lvl_UP_prcent = map(water_lvl_up, MAX_DISTANCE_RUDYS_TANK, MIN_DISTANCE_RUDYS_TANK, 0, 100); //convert distance(cm) to %, min_distance ->100%
   //Serial.println("Raquel's tank: " + String(water_lvl_up,'\002') + "cm -> " + String(wtr_lvl_UP_prcent, DEC));
   float water_lvl_down = Lvl_sensor_DOWN.ping_median(MEDIAN_WIDTH_WINDOW, MAX_CM_DISTANCE)/CONST_WAVE_SPEED;
   int wtr_lvl_DOWN_prcent = map(water_lvl_down, MAX_DISTANCE_RAQUELS_TANK, MIN_DISTANCE_RAQUELS_TANK, 0, 100); //convert distance(cm) to %, min_distance ->100%
   Serial.println("Raquel's tank: " + String(water_lvl_down,'\002') + "cm -> " + String(wtr_lvl_DOWN_prcent, DEC));

   String response;
   StaticJsonDocument<300> jsonDoc;
   jsonDoc["lvlUP"] = random(0,100);
   jsonDoc["lvlDOWN"] = wtr_lvl_DOWN_prcent;
   serializeJson(jsonDoc, response);
   ws.textAll(response);

   timerflag = false;   //Unmark the interrupt
}

void loop() {
  if(timerflag)
    read_Sensors();
}