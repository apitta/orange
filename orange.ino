#include <Time.h>
#include <SPI.h>
#include <Ethernet.h>
#include <EthernetUdp.h>
#include <PubSubClient.h>

//Ethernet
byte mac[]    = {  0xxx, 0xxx, 0xxx, 0xxx, 0xxx, 0xxx };
byte server[] = { 127, 0, 0, 1 }; 
EthernetClient eth;
PubSubClient client(server, 1883, callback, eth);

//udp
EthernetUDP udp;
IPAddress timeServer(200,192,232,8);

//control
unsigned long keepalivetime=0;
char strbuffer[80];

// analog
int moistureA = 0; //green
int lightA = 1; //orange
int temperatureA = 2; //yellow

// digital
int plug1 = 4;
int plug2 = 5;
int plug3 = 6;
int plug4 = 7;

// status das tomadas (ligado/desligado)
#define ZERO 48
#define UM 49
#define LIGADO 1
#define DESLIGADO 0
#define INTERVALO 60
int statusPlug1 = LIGADO;
int statusPlug2 = DESLIGADO;
int statusPlug3 = LIGADO;
int statusPlug4 = DESLIGADO;

//serie de valores dos sensores
int index = 0;
int serie1[INTERVALO];
int serie2[INTERVALO];
int serie3[INTERVALO];

/*****************************/

void setup()
{
  Serial.begin (9600);
  Serial.println("Iniciando sistema.");
  pinMode(plug1, OUTPUT);
  pinMode(plug2, OUTPUT);
  pinMode(plug3, OUTPUT);     
  pinMode(plug4, OUTPUT);  
  //wait for IP address
  while (Ethernet.begin(mac) != 1){
    Serial.println("Error getting IP address via DHCP, trying again...");
    delay(1000);
  }
  //mqtt stuff
  while(client.connect("OrangeClient") != 1){
    Serial.println("Error getting mqtt started... trying again...");
    client.connect("OrangeClient");
    delay(1000);
    if(client.connected()){
      Serial.println("mqtt connected.");
      client.publish("log","MQTT connected! Channel log created.");
    }
  }
  //ntp udp stuff
  while (udp.begin(123) != 1){
    Serial.println("Error getting dup started... trying again...");
    client.publish("log","Error getting udp... trying again...");
    delay(1000);
  }
  Serial.println("NTP sync request started.");
  setSyncProvider(getNtpTime);
  setSyncInterval(600); //ten minutes
  
  //digital control
  Serial.println("Iniciando as tomadas.");  
  digitalWrite(plug1, HIGH);  
  client.publish("orange/plugs/1", "1");  
  digitalWrite(plug2, LOW);  
  client.publish("orange/plugs/2", "0");
  digitalWrite(plug3, HIGH); 
  client.publish("orange/plugs/3", "1");
  digitalWrite(plug4, LOW);  
  client.publish("orange/plugs/4", "0");   
  Serial.println("Inscricao no canal orange/plugs/#");
  client.subscribe("orange/plugs/#");

  //control stuff
  keepalivetime=millis();
  client.publish("log","Init");
}// end of setup

/************************/

void loop(){
  //ethernet status check
  while(!eth.connected()){
    while (Ethernet.begin(mac) != 1){
      Serial.println("Error getting IP address via DHCP, trying again...");
      delay(1000);
    }
  }
  //ntp time refresh check
  while(timeStatus() == timeNeedsSync){
    Serial.println("refreshing ntp time.");
    getNtpTime();
    if(timeStatus() == timeSet){
      Serial.println("time ok");
    }
  }
  //mqtt check
  while(client.connected() != 1){
    Serial.println("mqtt disconnected... reconnecting...");
    client.connect("OrangeClient");
    delay(1000);
    if(client.connected()){
      Serial.println("mqtt connected. resuming normal logging.");
      // TODO: log via client.publish()
    }
  }
  //client.loop();
  if ((millis() - keepalivetime)>1000){
    sensores();
    sol();
    Ethernet.maintain();
    if (!client.loop()) {
      Serial.print("Client disconnected...");
      if (client.connect("OrangeClient")) {
        Serial.println("reconnected.");
      } else {
        Serial.println("failed.");
      }
    }
    keepalivetime = millis();
    //client.publish("localtime",timestamp());
  }
}// end of loop

/********* SENSORS CODE *********/
void sensores() {
  int a = 0;
  int b = 0; 
  float c = 0;
  char buf[8];

  a = analogRead(moistureA);  
  b = light();
  c = temperature2();  
 
  index++;
  serie1[index] = a;
  serie2[index] = b;
  serie3[index] = c;
  
  if(index == INTERVALO) {
    publicar();      
    index = 0; 
  }
}

/*
 * funcao dia e noite usano como referencia a hora do cliente ntp
 */
void sol() { 
  // inicia noite
  if(hour() >= 12 && statusPlug1 == LIGADO){
    togglePlug(1);
  }
  // inicia dia
  if(hour() >= 18 && statusPlug1 == DESLIGADO){
    togglePlug(1);
  }
  // cicla plug2
  if(hour() == 18 && minute() == 15 && statusPlug2 == DESLIGADO){
    togglePlug(2);
    delay(15000);
    togglePlug(2);
  }
  // inicia plug2
  if(hour() == 00 && minute() == 15 && statusPlug2 == DESLIGADO){
    togglePlug(2);
    delay(15000);
    togglePlug(2);
  }
}

/*
 * Temperatura usando sensor LM35 
 *
 */
int temperature2(){
  int temp;
  temp = analogRead(temperatureA);
  temp = temp * 0.48828125;
  return temp;
}

float temperature() {
  int i = 0;
  int val = 0;
  float temp = 0;
  float total = 0;
  for(i = 0; i < 5; i++) {
   val = analogRead(temperatureA);
   temp = (val * 0.00488);
   temp = temp * 100;
   total += temp;
  }  
  temp = total / 5;
  return temp;
}

/*
 * Luminosidade por fotoresistor
 */
int light() { 
   int l = 0;
   l = analogRead(lightA);
   l = map(l, 0, 900, 0, 255);
   l = constrain(l, 0, 255);
   return 255-l;
}

/*
 * calculo e publicacao das leituras dos sensores 
 * de luminosidade, humidade e temperatura 
 */
void publicar() {
    int j = 0;
    int totalSerie1, totalSerie2 = 0;
    float totalSerie3 = 0;
    char outbuffer[8];
    
    for(j = 1; j <= INTERVALO; j++) {
      totalSerie1 = totalSerie1 + serie1[j];
      totalSerie2 = totalSerie2 + serie2[j];
      totalSerie3 = totalSerie3 + serie3[j];
    }
    totalSerie1 = (int) (totalSerie1 / INTERVALO);
    totalSerie2 = (int) (totalSerie2 / INTERVALO);
    totalSerie3 = totalSerie3 / INTERVALO;

    Serial.print(" Humidade ");
    sprintf(outbuffer, "%d", totalSerie1);
    client.publish("orange/sensor/moisture", outbuffer);
    Serial.print(outbuffer);
  
    Serial.print(" Luminosidade ");
    sprintf(outbuffer, "%d", totalSerie2);
    client.publish("orange/sensor/luminosity", outbuffer);
    Serial.print(outbuffer);  
    
    Serial.print(" Temperatura "); 
    sprintf(outbuffer, "%d", (int) totalSerie3);
    client.publish("orange/sensor/temperature" , outbuffer);
    Serial.print(outbuffer);    
}

/*
 * Funcao para alternar o estado do plug entre ligado e desligado
 */
void togglePlug(int plug) {
  switch(plug) {
    case 1:
      if(statusPlug1 == LIGADO) {
       digitalWrite(plug1, LOW);
       statusPlug1 = DESLIGADO;
      } else {
       digitalWrite(plug1, HIGH);
       statusPlug1 = LIGADO;
      } 
      break;
   case 2:
      if(statusPlug2 == LIGADO) {
       digitalWrite(plug2, LOW);
       statusPlug2 = DESLIGADO;
      } else {
       digitalWrite(plug2, HIGH);
       statusPlug2 = LIGADO;
      } 
      break;
   case 3:
      if(statusPlug3 == LIGADO) {
       digitalWrite(plug3, LOW);
       statusPlug3 = DESLIGADO;
      } else {
       digitalWrite(plug3, HIGH);
       statusPlug3 = LIGADO;
      } 
      break;
   case 4:
      if(statusPlug4 == LIGADO) {
       digitalWrite(plug4, LOW);
       statusPlug4 = DESLIGADO;
      } else {
       digitalWrite(plug4, HIGH);
       statusPlug1 = LIGADO;
      } 
      break;
  }
}

/*
 *
 * funcao de callback do cliente mqtt (pubsubclient)
 *
 *
 */
void callback(char* topic, byte* payload, unsigned int length) {
  
  // In order to republish this payload, a copy must be made
  // as the orignal payload buffer will be overwritten whilst
  // constructing the PUBLISH packet.
  
  // Allocate the correct amount of memory for the payload copy
  byte* p = (byte*)malloc(length);
  // Copy the payload to the new buffer
  memcpy(p,payload,length);
  
  char buffer[1];
  memcpy(buffer, p, length);
  
  Serial.print("Topico: ");
  Serial.println(topic);
  Serial.print("Status: ");
  Serial.println(buffer[0]);
 
  if(strcmp(topic, "orange/plugs/1") == 0) {
    Serial.println("Plug 1.");
    if(buffer[0] == UM) {
        Serial.println("Ligar.");
        digitalWrite(plug1, HIGH);
    } else {
        digitalWrite(plug1, LOW);
    }     
  } else if(strcmp(topic,"orange/plugs/2") == 0) {
    Serial.println("Plug 2.");
    if(buffer[0] == UM) {
        Serial.println("Ligar.");
        digitalWrite(plug2, HIGH);
    } else {
        digitalWrite(plug2, LOW);
    }    
  } else if(strcmp(topic,"orange/plugs/3") == 0) {
    Serial.println("Plug 3.");
    if(buffer[0] == UM) {
      Serial.println("Ligar.");
      digitalWrite(plug3, HIGH);
    } else {
        digitalWrite(plug3, LOW);
    }    
  } else if(strcmp(topic,"orange/plugs/4") == 0) {
    Serial.println("Plug 4.");
    if(buffer[0] == UM) {
        digitalWrite(plug4, HIGH);
    } else {
        digitalWrite(plug4, LOW);
    }  
  } else {
    Serial.println("Denied.");
  }  
  
  Serial.println("orange/OutTopic");
  client.publish("orange/outTopic", p, length);  
  
  // Free the memory
  free(p);
}

//log
void logger(String topic, String message){
  message = timestamp() + message;
  char buffmsg[80];
  char bufftpc[20];
  message.toCharArray(buffmsg,80);
  topic.toCharArray(bufftpc,20);
  if(client.connected()){
    client.publish(bufftpc,buffmsg);
  }else{
    Serial.println(message);
  }
}

//format time
char *timestamp(){
  String timestring = "[";
  if(hour() < 10){
    timestring += "0";
  }
  timestring += String(hour());
  timestring += ":";
  if(minute() < 10){
    timestring += "0";
  }
  timestring += String(minute());
  timestring += ":";
  if(second() < 10){
    timestring += "0";
  }
  timestring += String(second());
  timestring += " ";
  timestring += monthShortStr(month());
  timestring += " ";
  if(day() < 10)
    timestring += "0";
  timestring += String(day());
  timestring += " ";
  timestring += String(year());
  timestring += "]";
  timestring.toCharArray(strbuffer,24);
  return strbuffer;
}

/*-------- NTP code ----------*/
const int NTP_PACKET_SIZE = 48; // NTP time is in the first 48 bytes of message
byte packetBuffer[NTP_PACKET_SIZE]; //buffer to hold incoming & outgoing packets

time_t getNtpTime()
{
  while (udp.parsePacket() > 0) ; // discard any previously received packets
  Serial.println("Transmit NTP Request");
  sendNTPpacket(timeServer);
  uint32_t beginWait = millis();
  while (millis() - beginWait < 1500) {
    int size = udp.parsePacket();
    if (size >= NTP_PACKET_SIZE) {
      Serial.println("Receive NTP Response");
      udp.read(packetBuffer, NTP_PACKET_SIZE);  // read packet into the buffer
      unsigned long secsSince1900;
      // convert four bytes starting at location 40 to a long integer
      secsSince1900 =  (unsigned long)packetBuffer[40] << 24;
      secsSince1900 |= (unsigned long)packetBuffer[41] << 16;
      secsSince1900 |= (unsigned long)packetBuffer[42] << 8;
      secsSince1900 |= (unsigned long)packetBuffer[43];
      return secsSince1900 - 2208988800UL + -3 * SECS_PER_HOUR;
    }
  }
  Serial.println("No NTP Response :-(");
  return 0; // return 0 if unable to get the time
}

// send an NTP request to the time server at the given address
void sendNTPpacket(IPAddress &address)
{
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;     // Stratum, or type of clock
  packetBuffer[2] = 6;     // Polling Interval
  packetBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12]  = 49;
  packetBuffer[13]  = 0x4E;
  packetBuffer[14]  = 49;
  packetBuffer[15]  = 52;
  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:                 
  udp.beginPacket(address, 123); //NTP requests are to port 123
  udp.write(packetBuffer, NTP_PACKET_SIZE);
  udp.endPacket();
}
