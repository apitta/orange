#include <Time.h>
#include <SPI.h>
#include <Ethernet.h>
#include <EthernetUdp.h>
#include <PubSubClient.h>

#define LIGADO 1
#define DESLIGADO 0

// asc code de 0 e 1
#define ZERO 48
#define UM 49

#define INTERVALO 60

// mac da interface de rede da ethershield
byte mac[] = { 0x90, 0xA2, 0xDA, 0x, 0x, 0x };
// byte ip[] = { 192, 168, 1, 5}; // quando indefinido usa a funcao dhcp

// ip e porta do servidor rodando o mqtt server (mosquitto)
byte server[] = { 127, 0, 0, 1 };
int port = 1883;

EthernetClient eth; 
PubSubClient mqtt(server, port, callback, eth);

EthernetUDP Udp;
IPAddress timeServer(200,192,232,8);
unsigned int localPort = 8888;
const int timeZone = -3;
time_t prevDisplay = 0; // when the digital clock was displayed

// analog
int moistureA = 0;
int lightA = 1;
int temperatureA = 2;

// digital
int plug1 = 4;
int plug2 = 5;
int plug3 = 6;
int plug4 = 7;

// status das tomadas (ligado/desligado)
int statusPlug1 = DESLIGADO;
int statusPlug2 = DESLIGADO;
int statusPlug3 = DESLIGADO;
int statusPlug4 = DESLIGADO;

int index = 0;
int serie1[INTERVALO];
int serie2[INTERVALO];
float serie3[INTERVALO];

int valor = 0;

/*
 * Inicializacao do circuito
 *
 */
void setup() {       
  Serial.begin(9600);  
  
  Serial.println("Iniciando sistema.");
  pinMode(plug1, OUTPUT);
  pinMode(plug2, OUTPUT);
  pinMode(plug3, OUTPUT);     
  pinMode(plug4, OUTPUT);  

  Serial.println("Iniciando a rede."); 
  Ethernet.begin(mac);
  Serial.print("IP definido pelo DHCP ");
  Serial.println(Ethernet.localIP());

  Udp.begin(localPort);
  Serial.println("Sincronizando com o servidor ntp.");
  setSyncProvider(getNtpTime);  
  delay(3000);

  Serial.println("Iniciando mqtt.");
  if(!mqtt.connected()) {
     mqtt.connect("OrangeClient");
  }  

  Serial.println("Iniciando as tomadas.");
  
  digitalWrite(plug1, HIGH);  
  mqtt.publish("orange/plugs/1", "1");  
  digitalWrite(plug2, LOW);  
  mqtt.publish("orange/plugs/2", "0");
  digitalWrite(plug3, HIGH); 
  mqtt.publish("orange/plugs/3", "1");
  digitalWrite(plug4, LOW);  
  mqtt.publish("orange/plugs/4", "0"); 
  
  Serial.println("Inscricao no canal orange/plugs/#");
  mqtt.subscribe("orange/plugs/#"); 
  
}

void loop() {
  sensores();
  sol();
  delay(1000);
  mqtt.loop();
}

/*
 * leitura dos sensores do circuito
 *
 */
void sensores() {
  
  int a = 0;
  int b = 0; 
  float c = 0;
  char buf[8];

  a = analogRead(moistureA);  
  b = light();
  c = temperature();  
 
  index++;
  serie1[index] = a;
  serie2[index] = b;
  serie3[index] = c;
  
  if(index == INTERVALO) {
    publicar();      
    index = 0; 
  }
 
  Serial.println("");
  
}


/*
 *
 * funcao dia e noite usano como referencia a hora do cliente ntp
 *
 */
void sol() { 
  // inicia noite
  if(hour() >= 12 && statusPlug3 == LIGADO){
    togglePlug(1);
  }
  // inicia dia
  if(hour() >= 18 && statusPlug3 == DESLIGADO){
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
 *
 * Luminosidade por fotoresistor
 *
 */
int light() { 
   int l = 0;
   l = analogRead(lightA);
   l = map(l, 0, 900, 0, 255);
   l = constrain(l, 0, 255);
   return 255-l;
}

/*
 *
 * calculo e publicacao das leituras dos sensores de luminosidade, humidade e temperatura 
 *
 */
void publicar() {
    int j = 0;
    int totalSerie1, totalSerie2 = 0;
    float totalSerie3 = 0;
    char buffer[8];
    
    for(j = 1; j <= INTERVALO; j++) {
      totalSerie1 = totalSerie1 + serie1[j];
      totalSerie2 = totalSerie2 + serie2[j];
      totalSerie3 = totalSerie3 + serie3[j];
    }
    totalSerie1 = (int) (totalSerie1 / INTERVALO);
    totalSerie2 = (int) (totalSerie2 / INTERVALO);
    totalSerie3 = totalSerie3 / INTERVALO;

    Serial.print(" Humidade ");
    sprintf(buffer, "%d", totalSerie1);
    mqtt.publish("orange/sensor/moisture", buffer);
    Serial.print(buffer);
  
    Serial.print(" Luminosidade ");
    sprintf(buffer, "%d", totalSerie2);
    mqtt.publish("orange/sensor/luminosity", buffer);
    Serial.print(buffer);  
    
    Serial.print(" Temperatura "); 
    sprintf(buffer, "%d", (int) totalSerie3);
    mqtt.publish("orange/sensor/temperature" , buffer);
    Serial.print(buffer);    

}



/*
 *
 * Funcao para alternar o estado do plug entre ligado e desligado
 *
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
  mqtt.publish("orange/outTopic", p, length);  
  
  // Free the memory
  free(p);
}

/*-------- NTP code ----------*/

const int NTP_PACKET_SIZE = 48; // NTP time is in the first 48 bytes of message
byte packetBuffer[NTP_PACKET_SIZE]; //buffer to hold incoming & outgoing packets

time_t getNtpTime()
{
  while (Udp.parsePacket() > 0) ; // discard any previously received packets
  Serial.println("Transmit NTP Request");
  sendNTPpacket(timeServer);
  uint32_t beginWait = millis();
  while (millis() - beginWait < 1500) {
    int size = Udp.parsePacket();
    if (size >= NTP_PACKET_SIZE) {
      Serial.println("Receive NTP Response");
      Udp.read(packetBuffer, NTP_PACKET_SIZE);  // read packet into the buffer
      unsigned long secsSince1900;
      // convert four bytes starting at location 40 to a long integer
      secsSince1900 =  (unsigned long)packetBuffer[40] << 24;
      secsSince1900 |= (unsigned long)packetBuffer[41] << 16;
      secsSince1900 |= (unsigned long)packetBuffer[42] << 8;
      secsSince1900 |= (unsigned long)packetBuffer[43];
      return secsSince1900 - 2208988800UL + timeZone * SECS_PER_HOUR;
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
  Udp.beginPacket(address, 123); //NTP requests are to port 123
  Udp.write(packetBuffer, NTP_PACKET_SIZE);
  Udp.endPacket();
}

/** Time display code **/

void digitalClockDisplay(){
  // digital clock display of the time
  Serial.print(hour());
  printDigits(minute());
  printDigits(second());
  Serial.print(" ");
  Serial.print(day());
  Serial.print(" ");
  Serial.print(month());
  Serial.print(" ");
  Serial.print(year()); 
  Serial.println(); 
}

void printDigits(int digits){
  // utility for digital clock display: prints preceding colon and leading 0
  Serial.print(":");
  if(digits < 10)
    Serial.print('0');
  Serial.print(digits);
}
