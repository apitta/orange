#include <Time.h>
#include <SPI.h>
#include <Ethernet.h>
#include <EthernetUdp.h>
#include <PubSubClient.h>
#include <OneWire.h>

//Ethernet
byte mac[] = {0xAA,0xA1,0xA2,0xA3,0xA4,0xA5};
byte server[] = {127,0,0,1}; 
EthernetClient eth;
PubSubClient mqtt(server, 1883, callback, eth);

//udp
EthernetUDP udp;
IPAddress timeServer(200,160,7,193); //pool.ntp.org

//control
boolean mqtt_connect = false;
int eth_maintain;
unsigned long keepalivetime=0;
char timebuff[24];

//analog
int moistureA = 0; //green
int lightA = 1; //orange
//int temperatureA = 2; //yellow

// digital
int plug1 = 5; //on pin D5 (relay plug 1)
int plug2 = 6; //on pin D6 (relay plug 2)
int plug3 = 7; //on pin D7 (relay plug 3)
int plug4 = 8; //on pin D8 (relay plug 4)

//sensors onewire
OneWire ds(9); // on pin D9

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
  delay(10);Serial.begin (9600);delay(10);
  Serial.print("[STARTUP] ");
  Serial.print(__FILE__);
  Serial.print(" @ ");
  Serial.print(__DATE__);
  Serial.print(" ");
  Serial.println(__TIME__);
  // disable SD SPI bus
  pinMode(4,OUTPUT);
  digitalWrite(4,HIGH);
  // enable ethernet SPI bus
  pinMode(10,OUTPUT);
  digitalWrite(10,LOW); // Ethernet ONLY from now on
  Serial.println("[STARTUP] inciando plugs");
  pinMode(plug1, OUTPUT);
  pinMode(plug2, OUTPUT);
  pinMode(plug3, OUTPUT);
  pinMode(plug4, OUTPUT);
  // ethernet 
  while(!Ethernet.begin(mac)){ //wait for IP address
    eth_maintain = Ethernet.maintain();
    if(eth_maintain % 2 == 0){//eth ok!
      Serial.print("[STARTUP] Ethernet IP: ");
      Serial.println(Ethernet.localIP());
    }else{
      Serial.println("[STARTUP] Error getting IP address via DHCP, trying again...");
    }
  }
  Serial.print("[STARTUP] Ethernet IP: ");
  Serial.println(Ethernet.localIP());

  // ntp udp stuff
  while (udp.begin(123) != 1){
    Serial.println("[STARTUP] Error getting udp started... trying again...");
  }
  Serial.println("[STARTUP] NTP sync request started.");
  setSyncProvider(getNtpTime);
  setSyncInterval(86400); //one day
  // mqtt stuff
  mqtt_connect = mqtt.connect("OrangeClient");
  while(!mqtt_connect){
    Serial.println("[STARTUP] Error getting mqtt started... trying again...");
    mqtt_connect = mqtt.connect("OrangeClient");
    if(mqtt.connected()){
      Serial.println("[STARTUP] mqtt connected.");
      mqtt.publish("log/startup/info","MQTT connected! Channel log created.");
    }
  }

  //digital control
  Serial.println("[STARTUP] Iniciando as tomadas.");  
  digitalWrite(plug1, HIGH);  
  mqtt.publish("orange/plugs/1", "1");  
  digitalWrite(plug2, LOW);  
  mqtt.publish("orange/plugs/2", "0");
  digitalWrite(plug3, HIGH); 
  mqtt.publish("orange/plugs/3", "1");
  digitalWrite(plug4, LOW);  
  mqtt.publish("orange/plugs/4", "0");   
  Serial.println("[STARTUP] Inscricao no canal orange/plugs/#");
  mqtt.subscribe("orange/plugs/#");
  
  // control stuff
  keepalivetime=millis();
}// end of setup

/************************/

void loop(){
  //ethernet status check
  eth_maintain = Ethernet.maintain();
  if(eth_maintain % 2 == 1){//eth not ok
    while(!Ethernet.begin(mac))
      Serial.println("[RUNNING] Error getting IP address via DHCP, trying again...");
  }
  
  //ntp time refresh check
  while(timeStatus() == timeNeedsSync){
    Serial.println("[RUNNING] Refreshing ntp time.");
    getNtpTime();
    if(timeStatus() == timeSet){
      Serial.println("[RUNNING] time ok");
    }
  }
  //mqtt check
  while(!mqtt_connect){
    Serial.println("[RUNNING] mqtt disconnected... reconnecting...");
    mqtt_connect = mqtt.connect("OrangeClient");
    delay(300);
    if(mqtt.connected()){
      Serial.println("[RUNNING] mqtt connected. resuming normal logging.");
    }
  }
  //mqtt.loop();
  if ((millis() - keepalivetime)>1000){
    sensores();
    sol();
    Ethernet.maintain();
    if (!mqtt.loop()) {
      Serial.print("[RUNNING] Client disconnected... ");
      mqtt.disconnect();
      if (mqtt.connect("OrangeClient")) {
        Serial.println("reconnected.");
      } else {
        Serial.println("failed.");
      }
    }
    keepalivetime = millis();
    //mqtt.publish("log/running/localtime",timestamp());
    //Serial.print(timestamp());
    //Serial.println("[RUNNING] ping");
  }
}// end of loop

/******************/
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
  if(hour() >= 0 && statusPlug1 == DESLIGADO){
    togglePlug(1);
  }
  // inicia dia
  if(hour() >= 12 && statusPlug1 == LIGADO){
    togglePlug(1);
  }
  // cicla plug2
  if(hour() == 2 && minute() == 15 && second() < 61 && statusPlug2 == DESLIGADO){
    togglePlug(2);
    delay(60000);
    togglePlug(2);
  }
  // inicia plug2
  if(hour() == 8 && minute() == 15 && second() < 61 && statusPlug2 == DESLIGADO){
    togglePlug(2);
    delay(60000);
    togglePlug(2);
  }
}

/*
 * Temperatura usando sensor LM35 
 *
 */
float temperature2(){
//float getTemp_DS18S20(){
  byte data[12];
  byte addr[8];
  if ( !ds.search(addr)) {
    //no more sensors on chain, reset search
    ds.reset_search();
    return -1000;
   }

 if ( OneWire::crc8( addr, 7) != addr[7]) {
   //Serial.println("DS18S20 CRC is not valid!");
   return -1000;
 }

 if ( addr[0] != 0x10 && addr[0] != 0x28) {
   //Serial.print("DS18S20 Device is not recognized");
   return -1000;
 }

 ds.reset();
 ds.select(addr);
 ds.write(0x44,1); // start conversion, with parasite power on at the end

 byte present = ds.reset();
 ds.select(addr);  
 ds.write(0xBE); // Read Scratchpad

 
 for (int i = 0; i < 9; i++) { // we need 9 bytes
  data[i] = ds.read();
 }
 
 ds.reset_search();
 
 byte MSB = data[1];
 byte LSB = data[0];

 float tempRead = ((MSB << 8) | LSB); //using two's compliment
 float TemperatureSum = tempRead / 16;
// //Serial.print("DS18S20: ");
// //Serial.println(TemperatureSum);
 return TemperatureSum;
}
//
//float temperature() {
//  int i = 0;
//  int val = 0;
//  float temp = 0;
//  float total = 0;
//  for(i = 0; i < 5; i++) {
//   val = analogRead(temperatureA);
//   temp = (val * 0.00488);
//   temp = temp * 100;
//   total += temp;
//  }  
//  temp = total / 5;
//  return temp;
//}

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
    Serial.print(timestamp());
    Serial.print(" Humidade ");
    sprintf(outbuffer, "%d", totalSerie1);
    mqtt.publish("orange/sensor/moisture", outbuffer);
    Serial.print(outbuffer);
  
    Serial.print(" Luminosidade ");
    sprintf(outbuffer, "%d", totalSerie2);
    mqtt.publish("orange/sensor/luminosity", outbuffer);
    Serial.print(outbuffer);  
    
    Serial.print(" Temperatura "); 
    sprintf(outbuffer, "%d", (int) totalSerie3);
    mqtt.publish("orange/sensor/temperature" , outbuffer);
    Serial.println(outbuffer);    
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
 * funcao de callback do cliente mqtt (pubsubclient)
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
  mqtt.publish("orange/outTopic", p, length);  
  
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
  if(mqtt.connected()){
    mqtt.publish(bufftpc,buffmsg);
  }else{
    Serial.println(message);
  }
}

//format time
char *timestamp(){
  String timestring = "[";
  timestring += monthShortStr(month());
  timestring += " ";
  if(day() < 10)
    timestring += "0";
  timestring += String(day());
  timestring += " ";
  timestring += String(year());
    if(hour() < 10){
    timestring += "0";
  }
  timestring += " ";
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
  timestring += "]";
  timestring.toCharArray(timebuff,24);
  return timebuff;
}

/*-------- NTP code ----------*/
const int NTP_PACKET_SIZE = 48; // NTP time is in the first 48 bytes of message
byte packetBuffer[NTP_PACKET_SIZE]; //buffer to hold incoming & outgoing packets

time_t getNtpTime()
{
  while (udp.parsePacket() > 0) ; // discard any previously received packets
  Serial.println("[NTP DEBUG] Transmit NTP Request");
  sendNTPpacket(timeServer);
  uint32_t beginWait = millis();
  while (millis() - beginWait < 1500) {
    int size = udp.parsePacket();
    if (size >= NTP_PACKET_SIZE) {
      Serial.println("[NTP DEBUG] Receive NTP Response");
      udp.read(packetBuffer, NTP_PACKET_SIZE);  // read packet into the buffer
      unsigned long secsSince1900;
      // convert four bytes starting at location 40 to a long integer
      secsSince1900 =  (unsigned long)packetBuffer[40] << 24;
      secsSince1900 |= (unsigned long)packetBuffer[41] << 16;
      secsSince1900 |= (unsigned long)packetBuffer[42] << 8;
      secsSince1900 |= (unsigned long)packetBuffer[43];
      return secsSince1900 - 2208988800UL - 3 * SECS_PER_HOUR;
    }
  }
  Serial.println("[NTP DEBUG] No NTP Response :-(");
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
