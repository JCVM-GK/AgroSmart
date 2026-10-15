#include <Arduino.h>
#include <DHT.h>

// --- CONFIGURACIÓN DE HARDWARE ---
#define DHTPIN1 14
#define DHTPIN2 27
#define DHTTYPE DHT22

DHT dht1(DHTPIN1, DHTTYPE);
DHT dht2(DHTPIN2, DHTTYPE);

#define PIN_LDR 34
#define PIN_GAS 35

int umbralLuz = 2000;

// Temporizador no bloqueante
unsigned long tiempoAntSensores = 0;

void setup() {
  // Serial USB para monitoreo local en PC
  Serial.begin(115200);
  
  // Serial2 hacia el NodeMCU DSKY (RX2=16, TX2=17)
  Serial2.begin(115200, SERIAL_8N1, 16, 17);

  dht1.begin();
  dht2.begin();

  Serial.println("\n[ESP32] AgroSmart - Firmware Offline Directo");
}

void loop() {
  unsigned long ahora = millis();

  // Variables estáticas para mantener las últimas lecturas válidas
  static float tempAire1 = 0.0, humAire1 = 0.0;
  static float tempAire2 = 0.0, humAire2 = 0.0;
  static int valGasRaw = 0;
  static bool hayLuz = false;

  // -------------------------------------------------------------
  // LECTURA Y TRANSMISIÓN UART AL NODEMCU (Cada 1000 ms)
  // -------------------------------------------------------------
  if (ahora - tiempoAntSensores >= 1000) {
    tiempoAntSensores = ahora;

    // Lectura DHT22 #1
    float t1 = dht1.readTemperature();
    float h1 = dht1.readHumidity();
    if (!isnan(t1)) tempAire1 = t1;
    if (!isnan(h1)) humAire1 = h1;

    // Lectura DHT22 #2
    float t2 = dht2.readTemperature();
    float h2 = dht2.readHumidity();
    if (!isnan(t2)) tempAire2 = t2;
    if (!isnan(h2)) humAire2 = h2;

    // Lectura Sensores Analógicos (ADC1: Pins 34 y 35)
    int valLDR = analogRead(PIN_LDR);
    hayLuz = (valLDR < umbralLuz);
    valGasRaw = analogRead(PIN_GAS);

    // Formatear Trama CSV: Temp1,Hum1,Temp2,Hum2,GasRaw,Luz
    String trama = String(tempAire1, 1) + "," +
                   String(humAire1, 1)  + "," +
                   String(tempAire2, 1) + "," +
                   String(humAire2, 1)  + "," +
                   String(valGasRaw)    + "," +
                   String(hayLuz ? 1 : 0);

    // Enviar a la pantalla DSKY (NodeMCU)
    Serial2.println(trama);
    
    // Imprimir en la consola USB del computador
    Serial.print("[UART TX -> NodeMCU] ");
    Serial.println(trama);
  }
}