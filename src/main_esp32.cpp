#include <Arduino.h>
#include <DHT.h>

#define DHTPIN1 14
#define DHTPIN2 27  // Cambia esto al pin de tu segundo DHT22
#define DHTTYPE DHT22

DHT dht1(DHTPIN1, DHTTYPE);
DHT dht2(DHTPIN2, DHTTYPE);

#define PIN_LDR 34
#define PIN_GAS 35

int umbralLuz = 2000;
unsigned long tiempoAntSensores = 0;

void setup() {
  Serial.begin(115200);
  // Inicializa UART2: RX2=GPIO 16, TX2=GPIO 17
  Serial2.begin(115200, SERIAL_8N1, 16, 17);

  dht1.begin();
  dht2.begin();
  Serial.println("[ESP32] Nodo Central Emisor Iniciado.");
}

void loop() {
  unsigned long ahora = millis();

  if (ahora - tiempoAntSensores >= 1000) {
    tiempoAntSensores = ahora;

    // Lecturas DHT 1
    float temp1 = dht1.readTemperature();
    float hum1  = dht1.readHumidity();
    if (isnan(temp1)) temp1 = 0.0;
    if (isnan(hum1))  hum1  = 0.0;

    // Lecturas DHT 2
    float temp2 = dht2.readTemperature();
    float hum2  = dht2.readHumidity();
    if (isnan(temp2)) temp2 = 0.0;
    if (isnan(hum2))  hum2  = 0.0;

    // Lecturas Analógicas y Digitales
    int valLDR = analogRead(PIN_LDR);
    bool hayLuz = (valLDR < umbralLuz);

    int valGasRaw = analogRead(PIN_GAS);

    // NUEVA TRAMA (6 VALORES): Temp1,Hum1,Temp2,Hum2,GasRaw,Luz
    String trama = String(temp1, 1) + "," +
                   String(hum1, 1)  + "," +
                   String(temp2, 1) + "," +
                   String(hum2, 1)  + "," +
                   String(valGasRaw) + "," +
                   String(hayLuz ? 1 : 0);

    Serial2.println(trama); // Envío hacia el NodeMCU
    Serial.print("[ESP32 TX] ");
    Serial.println(trama);  // Consola de depuración
  }
}
