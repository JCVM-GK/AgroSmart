#include <Arduino.h>
#include <DHT.h>
#include <InfluxDbClient.h>
#include <LiquidCrystal_I2C.h>
#include <Preferences.h>
#include <WiFi.h>
#include <Wire.h>


// ==========================================
// 1. RED Y TELEMETRÍA (INFLUXDB DOCKER)
// ==========================================
#define WIFI_SSID "TU_RED_WIFI"
#define WIFI_PASS "TU_CONTRASEÑA_WIFI"
#define INFLUXDB_URL "http://192.168.X.X:8086" // IP Local IPv4 de tu PC
#define INFLUXDB_TOKEN "PEGAR_AQUI_EL_TOKEN_DE_DOCKER"
#define INFLUXDB_ORG "mi_proyecto"
#define INFLUXDB_BUCKET "sensores"

InfluxDBClient client(INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET, 
                      INFLUXDB_TOKEN);
Point sensorData("invernadero");

// ==========================================
// 2. HARDWARE Y SENSORES
// ==========================================
Preferences pref;
LiquidCrystal_I2C lcd(0x27, 16, 2);

#define DHTPIN 14
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

#define PIN_LDR 34
#define PIN_GAS 35
#define PIN_SUELO 32

// PINOUT DSKY OPTIMIZADO
#define BTN_VERB 27  // Verbo en GPIO 27
#define BTN_NOUN 4   // Nombre en GPIO 4
#define BTN_ENTER 19 // Enter en GPIO 19

const int listaVerbos[] = {16, 17, 21, 37, 44};
const int TOTAL_VERBOS = 5;
volatile int indexVerbo = 0;

const int listaNombres[] = {1, 2, 3};
const int TOTAL_NOMBRES = 3;
volatile int indexNombre = 0;

int umbralLuz = 2000;

const int FABRICA_GAS_CERO = 400;
const int FABRICA_SUELO_SECO = 3200;
const int FABRICA_SUELO_AGUA = 1400;

float tempAire = 0.0, humAire = 0.0;
int pctGas = 0, pctSuelo = 0;
bool hayLuz = false;

int rawGasCero = FABRICA_GAS_CERO;
int rawSueloSeco = FABRICA_SUELO_SECO;
int rawSueloAgua = FABRICA_SUELO_AGUA;

float sumaTemp = 0.0, sumaHum = 0.0, sumaGas = 0.0, sumaSuelo = 0.0;
unsigned long cantidadLecturas = 0;

float maxTemp = -100.0, minTemp = 100.0;
float maxHum = 0.0, minHum = 100.0;
int maxGas = 0, maxSuelo = 0, minSuelo = 100;

unsigned long tUltimoVerb = 0;
unsigned long tUltimoNoun = 0;
unsigned long tUltimoEnter = 0;
unsigned long tiempoAntSensores = 0;
unsigned long tUltimoEnvioInflux = 0;

void cargarCalibracionesNVS();
void guardarCalibracionNVS(const char *clave, int valor);
void borrarClaveNVS(const char *clave);
void leerSensores();
void procesarBotones();
void reiniciarPromedio();
void ejecutarAccion();
void actualizarPantalla();
void enviarTelemetriaInflux();

// ==========================================
// 3. SETUP
// ==========================================
void setup() {
  setCpuFrequencyMhz(80);

  Serial.begin(115200);
  Wire.begin(21, 22);

  lcd.init();
  lcd.backlight();
  dht.begin();

  pinMode(BTN_VERB, INPUT_PULLUP);
  pinMode(BTN_NOUN, INPUT_PULLUP);
  pinMode(BTN_ENTER, INPUT_PULLUP);

  lcd.setCursor(0, 0);
  lcd.print("V37N01: AGC BOOT");
  lcd.setCursor(0, 1);
  lcd.print("CARGANDO NVS... ");

  cargarCalibracionesNVS();

  lcd.setCursor(0, 1);
  lcd.print("CONECTANDO WIFI ");
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  sensorData.addTag("dispositivo", "ESP32_Invernadero");

  int intentos = 0;
  while (WiFi.status() != WL_CONNECTED && intentos < 10) {
    delay(500);
    intentos++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[Wi-Fi] Conectado exitosamente.");
    client.validateConnection();
  } else {
    Serial.println("\n[Wi-Fi] Modo offline activado.");
  }

  delay(500);
  lcd.clear();
}

// ==========================================
// 4. LOOP
// ==========================================
void loop() {
  unsigned long ahora = millis();

  procesarBotones();

  if (ahora - tiempoAntSensores >= 2000) {
    tiempoAntSensores = ahora;
    leerSensores();
  }

  if (ahora - tUltimoEnvioInflux >= 5000) {
    tUltimoEnvioInflux = ahora;
    enviarTelemetriaInflux();
  }

  actualizarPantalla();
  delay(10);
}

// ==========================================
// 5. FUNCIONES Y LÓGICA
// ==========================================
void procesarBotones() {
  unsigned long ahora = millis();

  // 1. Botón VERB
  if (digitalRead(BTN_VERB) == LOW) {
    if (ahora - tUltimoVerb > 250) {
      indexVerbo = (indexVerbo + 1) % TOTAL_VERBOS;
      reiniciarPromedio();
      tUltimoVerb = ahora;
    }
  }

  // 2. Botón NOUN
  if (digitalRead(BTN_NOUN) == LOW) {
    if (ahora - tUltimoNoun > 250) {
      indexNombre = (indexNombre + 1) % TOTAL_NOMBRES;
      reiniciarPromedio();
      tUltimoNoun = ahora;
      Serial.println("[DSKY] Botón NOUN presionado");
    }
  }

  // 3. Botón ENTER
  if (digitalRead(BTN_ENTER) == LOW) {
    if (ahora - tUltimoEnter > 300) {
      ejecutarAccion();
      tUltimoEnter = ahora;
    }
  }
}

void leerSensores() {
  float t = dht.readTemperature();
  float h = dht.readHumidity();

  if (!isnan(t))
    tempAire = t;
  if (!isnan(h))
    humAire = h;

  if (tempAire > maxTemp)
    maxTemp = tempAire;
  if (tempAire < minTemp)
    minTemp = tempAire;
  if (humAire > maxHum)
    maxHum = humAire;
  if (humAire < minHum)
    minHum = humAire;

  int valLDR = analogRead(PIN_LDR);
  hayLuz = (valLDR < umbralLuz);

  int valGasRaw = analogRead(PIN_GAS);
  int offsetGas = valGasRaw - rawGasCero;

  if (offsetGas < 0) {
    offsetGas = 0;
  }

  pctGas = map(offsetGas, 0, 4095 - rawGasCero, 0, 100);

  pctGas = constrain(pctGas, 0, 100);

  if (pctGas > maxGas) {
    maxGas = pctGas;
  }

  int valSueloRaw = analogRead(PIN_SUELO);

  pctSuelo = map(valSueloRaw, rawSueloSeco, rawSueloAgua, 0, 100);

  pctSuelo = constrain(pctSuelo, 0, 100);

  if (pctSuelo > maxSuelo) {
    maxSuelo = pctSuelo;
  }

  if (pctSuelo < minSuelo) {
    minSuelo = pctSuelo;
  }

  if (listaVerbos[indexVerbo] == 17) {
    int noun = listaNombres[indexNombre];

    if (noun == 1) {
      sumaTemp += tempAire;
      sumaHum += humAire;
    } else if (noun == 2) {
      sumaGas += pctGas;
    } else if (noun == 3) {
      sumaSuelo += pctSuelo;
    }

    cantidadLecturas++;
  }
}

void enviarTelemetriaInflux() {
  if (WiFi.status() == WL_CONNECTED) {
    sensorData.clearFields();
    sensorData.addField("temperatura", tempAire);
    sensorData.addField("humedad_aire", humAire);
    sensorData.addField("gas", pctGas);
    sensorData.addField("humedad_suelo", pctSuelo);
    sensorData.addField("luz", hayLuz ? 1 : 0);

    client.writePoint(sensorData);
  }
}

void cargarCalibracionesNVS() {
  pref.begin("calibracion", true);
  rawGasCero = pref.getInt("gas_cero", FABRICA_GAS_CERO);
  rawSueloSeco = pref.getInt("s_seco", FABRICA_SUELO_SECO);
  rawSueloAgua = pref.getInt("s_agua", FABRICA_SUELO_AGUA);
  pref.end();
}

void guardarCalibracionNVS(const char *clave, int valor) {
  pref.begin("calibracion", false);
  pref.putInt(clave, valor);
  pref.end();
}

void borrarClaveNVS(const char *clave) {
  pref.begin("calibracion", false);
  pref.remove(clave);
  pref.end();
}

void reiniciarPromedio() {
  sumaTemp = 0.0;
  sumaHum = 0.0;
  sumaGas = 0.0;
  sumaSuelo = 0.0;
  cantidadLecturas = 0;
}

void ejecutarAccion() {
  int verb = listaVerbos[indexVerbo];
  int noun = listaNombres[indexNombre];

  if (verb == 17) {
    reiniciarPromedio();
    lcd.clear();
    lcd.print(">> PROM RESET <<");
    delay(400);
    lcd.clear();
  } else if (verb == 21) {
    if (noun == 1) {
      maxTemp = tempAire;
      minTemp = tempAire;
      maxHum = humAire;
      minHum = humAire;
    } else if (noun == 2) {
      maxGas = pctGas;
    } else if (noun == 3) {
      maxSuelo = pctSuelo;
      minSuelo = pctSuelo;
    }
    lcd.clear();
    lcd.print(">> HI/LO RESET <");
    delay(400);
    lcd.clear();
  } else if (verb == 37) {
    lcd.clear();
    if (noun == 1) {
      rawGasCero = analogRead(PIN_GAS);
      guardarCalibracionNVS("gas_cero", rawGasCero);
      lcd.print("CERO GAS SET NVS");
    } else if (noun == 2) {
      rawSueloSeco = analogRead(PIN_SUELO);
      guardarCalibracionNVS("s_seco", rawSueloSeco);
      lcd.print("SUELO SECO NVS  ");
    } else if (noun == 3) {
      rawSueloAgua = analogRead(PIN_SUELO);
      guardarCalibracionNVS("s_agua", rawSueloAgua);
      lcd.print("SUELO AGUA NVS  ");
    }
    delay(800);
    lcd.clear();
  } else if (verb == 44) {
    lcd.clear();
    if (noun == 1) {
      borrarClaveNVS("gas_cero");
      rawGasCero = FABRICA_GAS_CERO;
      lcd.print("GAS DEF RESTORED");
    } else if (noun == 2) {
      borrarClaveNVS("s_seco");
      rawSueloSeco = FABRICA_SUELO_SECO;
      lcd.print("SECO DEF RESTORE");
    } else if (noun == 3) {
      borrarClaveNVS("s_agua");
      rawSueloAgua = FABRICA_SUELO_AGUA;
      lcd.print("AGUA DEF RESTORE");
    }
    delay(800);
    lcd.clear();
  }
}

void actualizarPantalla() {
  int verb = listaVerbos[indexVerbo];
  int noun = listaNombres[indexNombre];

  char bufL0[17];
  char bufL1[17];

  snprintf(bufL0, sizeof(bufL0), "V%02d N%02d | AGC OK", verb, noun);

  if (verb == 16) {
    if (noun == 1)
      snprintf(bufL1, sizeof(bufL1), "T:%.1fC H:%.0f%%   ", tempAire, humAire);
    else if (noun == 2)
      snprintf(bufL1, sizeof(bufL1), "LUZ:%s GAS:%d%%  ", hayLuz ? "SI" : "NO",
               pctGas);
    else if (noun == 3)
      snprintf(bufL1, sizeof(bufL1), "S.SUELO: %d%%    ", pctSuelo);
  } else if (verb == 17) {
    if (cantidadLecturas == 0) {
      snprintf(bufL1, sizeof(bufL1), "MEDICION EN 2s  ");
    } else {
      if (noun == 1)
        snprintf(bufL1, sizeof(bufL1), "PT:%.1fC PH:%.0f%% ",
                 sumaTemp / cantidadLecturas, sumaHum / cantidadLecturas);
      else if (noun == 2)
        snprintf(bufL1, sizeof(bufL1), "PROM.GAS: %.0f%%  ",
                 sumaGas / cantidadLecturas);
      else if (noun == 3)
        snprintf(bufL1, sizeof(bufL1), "PROM.SUELO:%.0f%% ",
                 sumaSuelo / cantidadLecturas);
    }
  } else if (verb == 21) {
    if (noun == 1)
      snprintf(bufL1, sizeof(bufL1), "MAX:%.0fC MIN:%.0fC ", maxTemp, minTemp);
    else if (noun == 2)
      snprintf(bufL1, sizeof(bufL1), "MAX GAS: %d%%    ", maxGas);
    else if (noun == 3)
      snprintf(bufL1, sizeof(bufL1), "MAX:%d%% MIN:%d%%  ", maxSuelo, minSuelo);
  } else if (verb == 37) {
    if (noun == 1)
      snprintf(bufL1, sizeof(bufL1), "ENT:SET CERO GAS");
    else if (noun == 2)
      snprintf(bufL1, sizeof(bufL1), "ENT:SET 0%% SECO ");
    else if (noun == 3)
      snprintf(bufL1, sizeof(bufL1), "ENT:SET 100%% AGUA");
  } else if (verb == 44) {
    if (noun == 1)
      snprintf(bufL1, sizeof(bufL1), "ENT:RESET GAS   ");
    else if (noun == 2)
      snprintf(bufL1, sizeof(bufL1), "ENT:RESET SECO  ");
    else if (noun == 3)
      snprintf(bufL1, sizeof(bufL1), "ENT:RESET AGUA  ");
  }

  lcd.setCursor(0, 0);
  lcd.print(bufL0);
  lcd.setCursor(0, 1);
  lcd.print(bufL1);
}
