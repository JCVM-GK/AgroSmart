#include <Arduino.h>
#include <DHT.h>

#define DHTPIN 14
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

#define PIN_LDR 34
#define PIN_GAS 35
#define PIN_SUELO 32

int umbralLuz = 2000;
unsigned long tiempoAntSensores = 0;

void setup() {
  Serial.begin(115200);
  // Inicializa UART2: TX2=GPIO 17, RX2=GPIO 16
  Serial2.begin(115200, SERIAL_8N1, 16, 17);

  dht.begin();
  Serial.println("[ESP32] Nodo Central Emisor Iniciado.");
}

void loop() {
  unsigned long ahora = millis();

  if (ahora - tiempoAntSensores >= 1000) {
    tiempoAntSensores = ahora;

    float tempAire = dht.readTemperature();
    float humAire = dht.readHumidity();

    if (isnan(tempAire)) tempAire = 0.0;
    if (isnan(humAire)) humAire = 0.0;

    int valLDR = analogRead(PIN_LDR);
    bool hayLuz = (valLDR < umbralLuz);

    int valGasRaw = analogRead(PIN_GAS);
    int valSueloRaw = analogRead(PIN_SUELO);

    // Trama: TEMP,HUM,GAS_RAW,SUELO_RAW,LUZ
    String trama = String(tempAire, 1) + "," +
                   String(humAire, 1) + "," +
                   String(valGasRaw) + "," +
                   String(valSueloRaw) + "," +
                   String(hayLuz ? 1 : 0);

    Serial2.println(trama);
    Serial.print("[ESP32 TX] ");
    Serial.println(trama);
  }
}
