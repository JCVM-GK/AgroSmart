#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <EEPROM.h>

// ==========================================
// 1. HARDWARE Y PINES (NodeMCU v2)
// ==========================================
LiquidCrystal_I2C lcd(0x27, 16, 2); // SDA = D2 (GPIO 4), SCL = D1 (GPIO 5)

#define BTN_VERB  D5  // GPIO 14
#define BTN_NOUN  D3  // GPIO 0
#define BTN_ENTER D7  // GPIO 13

// ==========================================
// 2. CONFIGURACIÓN Y MEMORIA EEPROM
// ==========================================
#define EEPROM_SIZE    512
#define MAGIC_WORD     0x44534B59 // "DSKY" en ASCII HEX
const int FABRICA_GAS_CERO = 400; // Valor RAW típico en aire limpio

struct CalibracionConfig {
  uint32_t magic;
  int rawGasCero;
  uint16_t checksum;
};

int rawGasCero = FABRICA_GAS_CERO;

// ==========================================
// 3. VARIABLES DE SENSORES Y ESTADÍSTICAS
// ==========================================
// Lecturas actuales
float tempAire1 = 0.0, humAire1 = 0.0;
float tempAire2 = 0.0, humAire2 = 0.0;
int pctGas      = 0;
int valGasRaw   = 0;
bool hayLuz     = false;

// Acumuladores para Promedios (V17)
float sumaTemp1 = 0.0, sumaHum1 = 0.0;
float sumaTemp2 = 0.0, sumaHum2 = 0.0;
float sumaGas   = 0.0;
unsigned long cantidadLecturas = 0;

// Registros de Máximos y Mínimos (V21)
float maxTemp1 = -100.0, minTemp1 = 100.0;
float maxTemp2 = -100.0, minTemp2 = 100.0;
int maxGas     = 0;

// ==========================================
// 4. MÁQUINA DE ESTADOS DSKY (VERBOS/NOMBRES)
// ==========================================
const int listaVerbos[] = {16, 17, 21, 37, 44};
const int TOTAL_VERBOS  = 5;
volatile int indexVerbo = 0;
volatile int verb       = listaVerbos[0];

// N01: DHT 1 | N02: DHT 2 | N03: Gas y Luz
const int listaNombres[] = {1, 2, 3};
const int TOTAL_NOMBRES  = 3;
volatile int indexNombre = 0;
volatile int noun       = listaNombres[0];

// ==========================================
// 5. DEBOUNCING DE BOTONES Y UI
// ==========================================
struct Boton {
  uint8_t pin;
  bool estadoConfirmado;
  bool ultimoEstadoLectura;
  unsigned long tiempoUltimoCambio;
};

Boton btnVerb  = {BTN_VERB,  HIGH, HIGH, 0};
Boton btnNoun  = {BTN_NOUN,  HIGH, HIGH, 0};
Boton btnEnter = {BTN_ENTER, HIGH, HIGH, 0};

char mensajeOverlay[24] = "";
unsigned long tiempoFinOverlay = 0;
unsigned long tUltimaPantalla = 0;

// ==========================================
// DECLARACIÓN DE FUNCIONES
// ==========================================
uint16_t calcularChecksum(const CalibracionConfig &cfg);
void cargarEEPROM();
void guardarEEPROM();
void recibirDatosUART();
void parsearTramaUART(char *trama);
bool actualizarBoton(Boton &b);
void procesarBotones();
void ejecutarAccion();
void reiniciarPromedio();
void mostrarOverlay(const char *mensaje, unsigned long duracionMs);
void actualizarPantalla();

// ==========================================
// 6. SETUP
// ==========================================
void setup() {
  Serial.begin(115200); // Conexión UART con ESP32 (RX en GPIO 3)

  EEPROM.begin(EEPROM_SIZE);
  Wire.begin(D2, D1);

  lcd.init();
  lcd.backlight();

  pinMode(BTN_VERB, INPUT_PULLUP);
  pinMode(BTN_NOUN, INPUT_PULLUP);
  pinMode(BTN_ENTER, INPUT_PULLUP);

  lcd.setCursor(0, 0); lcd.print("V37N01: AGC BOOT");
  lcd.setCursor(0, 1); lcd.print("CARGANDO MEM... ");

  cargarEEPROM();

  lcd.setCursor(0, 1); lcd.print("SISTEMA LISTO   ");
  tiempoFinOverlay = millis() + 1000;
}

// ==========================================
// 7. LOOP PRINCIPAL
// ==========================================
void loop() {
  unsigned long ahora = millis();

  recibirDatosUART();
  procesarBotones();

  // Refresco de pantalla a 5Hz
  if (ahora - tUltimaPantalla >= 200) {
    tUltimaPantalla = ahora;
    actualizarPantalla();
  }
}

// ==========================================
// 8. FUNCIONES DE MEMORIA EEPROM
// ==========================================
uint16_t calcularChecksum(const CalibracionConfig &cfg) {
  uint16_t sum = 0;
  sum ^= (cfg.rawGasCero & 0xFFFF) ^ (cfg.rawGasCero >> 16);
  return sum ^ 0xA55A;
}

void cargarEEPROM() {
  CalibracionConfig cfg;
  EEPROM.get(0, cfg);

  bool valida = (cfg.magic == MAGIC_WORD) &&
                (cfg.checksum == calcularChecksum(cfg)) &&
                (cfg.rawGasCero >= 0 && cfg.rawGasCero <= 4095);

  if (valida) {
    rawGasCero = cfg.rawGasCero;
  } else {
    rawGasCero = FABRICA_GAS_CERO;
    guardarEEPROM();
  }
}

void guardarEEPROM() {
  CalibracionConfig cfg;
  cfg.magic      = MAGIC_WORD;
  cfg.rawGasCero = rawGasCero;
  cfg.checksum   = calcularChecksum(cfg);

  EEPROM.put(0, cfg);
  EEPROM.commit();
}

// ==========================================
// 9. RECEPCIÓN Y PARSEO DE UART
// ==========================================
void recibirDatosUART() {
  static char rxBuf[128];
  static size_t rxIndex = 0;

  while (Serial.available() > 0) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (rxIndex > 0) {
        rxBuf[rxIndex] = '\0';
        parsearTramaUART(rxBuf);
        rxIndex = 0;
      }
    } else {
      if (rxIndex < sizeof(rxBuf) - 1) rxBuf[rxIndex++] = c;
    }
  }
}

void parsearTramaUART(char *trama) {
  // Esperamos 6 tokens separados por comas: Temp1,Hum1,Temp2,Hum2,GasRaw,Luz
  char *token = strtok(trama, ",");
  if (token == NULL) return;
  float t1 = atof(token);

  token = strtok(NULL, ",");
  if (token == NULL) return;
  float h1 = atof(token);

  token = strtok(NULL, ",");
  if (token == NULL) return;
  float t2 = atof(token);

  token = strtok(NULL, ",");
  if (token == NULL) return;
  float h2 = atof(token);

  token = strtok(NULL, ",");
  if (token == NULL) return;
  int gasRaw = atoi(token);

  token = strtok(NULL, ",");
  if (token == NULL) return;
  int luzVal = atoi(token);

  // --- Si llegó hasta aquí, los 6 datos son válidos ---
  tempAire1 = t1;
  humAire1  = h1;
  tempAire2 = t2;
  humAire2  = h2;
  valGasRaw = gasRaw;
  hayLuz    = (luzVal == 1);

  // Porcentaje Gas (Mapeado contra el cero calibrado en EEPROM)
  int offsetGas = valGasRaw - rawGasCero;
  if (offsetGas < 0) offsetGas = 0;
  int divGas = 4095 - rawGasCero;
  pctGas = (divGas != 0) ? constrain(map(offsetGas, 0, divGas, 0, 100), 0, 100) : 0;

  // Actualizar Máximos y Mínimos (V21)
  if (tempAire1 > maxTemp1) maxTemp1 = tempAire1;
  if (tempAire1 < minTemp1) minTemp1 = tempAire1;
  if (tempAire2 > maxTemp2) maxTemp2 = tempAire2;
  if (tempAire2 < minTemp2) minTemp2 = tempAire2;
  if (pctGas > maxGas)      maxGas   = pctGas;

  // Acumular para promedios (V17)
  if (verb == 17) {
    if (noun == 1)      { sumaTemp1 += tempAire1; sumaHum1 += humAire1; }
    else if (noun == 2) { sumaTemp2 += tempAire2; sumaHum2 += humAire2; }
    else if (noun == 3) { sumaGas += pctGas; }
    cantidadLecturas++;
  }
}

// ==========================================
// 10. BOTONES Y ACCIONES (DSKY LOGIC)
// ==========================================
bool actualizarBoton(Boton &b) {
  bool lectura = digitalRead(b.pin);
  unsigned long ahora = millis();
  bool pulsado = false;

  if (lectura != b.ultimoEstadoLectura) {
    b.tiempoUltimoCambio = ahora;
    b.ultimoEstadoLectura = lectura;
  }

  if ((ahora - b.tiempoUltimoCambio) > 40) {
    if (lectura != b.estadoConfirmado) {
      b.estadoConfirmado = lectura;
      if (b.estadoConfirmado == LOW) pulsado = true;
    }
  }
  return pulsado;
}

void procesarBotones() {
  if (actualizarBoton(btnVerb)) {
    indexVerbo = (indexVerbo + 1) % TOTAL_VERBOS;
    verb = listaVerbos[indexVerbo];
  }
  if (actualizarBoton(btnNoun)) {
    indexNombre = (indexNombre + 1) % TOTAL_NOMBRES;
    noun = listaNombres[indexNombre];
  }
  if (actualizarBoton(btnEnter)) {
    ejecutarAccion();
  }
}

void reiniciarPromedio() {
  sumaTemp1 = 0; sumaHum1 = 0;
  sumaTemp2 = 0; sumaHum2 = 0;
  sumaGas = 0; cantidadLecturas = 0;
}

void mostrarOverlay(const char *mensaje, unsigned long duracionMs) {
  snprintf(mensajeOverlay, sizeof(mensajeOverlay), "%-16s", mensaje);
  tiempoFinOverlay = millis() + duracionMs;
}

void ejecutarAccion() {
  if (verb == 17) { // Reset Promedios
    reiniciarPromedio();
    mostrarOverlay(">> PROM RESET <<", 1200);
  }
  else if (verb == 21) { // Reset Max/Min
    if (noun == 1)      { maxTemp1 = tempAire1; minTemp1 = tempAire1; }
    else if (noun == 2) { maxTemp2 = tempAire2; minTemp2 = tempAire2; }
    else if (noun == 3) { maxGas = pctGas; }
    mostrarOverlay(">> HI/LO RESET <", 1200);
  }
  else if (verb == 37) { // Calibración Set Cero
    if (noun == 3) {
      rawGasCero = valGasRaw;
      guardarEEPROM();
      mostrarOverlay("CERO GAS SET MEM", 1500);
    } else {
      mostrarOverlay("DHT CALIB AUTO  ", 1200);
    }
  }
  else if (verb == 44) { // Restaurar Fábrica
    if (noun == 3) {
      rawGasCero = FABRICA_GAS_CERO;
      guardarEEPROM();
      mostrarOverlay("GAS DEF RESTORED", 1500);
    } else {
      mostrarOverlay("NO REQUERIDO    ", 1200);
    }
  }
}

// ==========================================
// 11. REFRESCO LCD
// ==========================================
void actualizarPantalla() {
  char bufL0[24], bufL1[24];
  bool enOverlay = (millis() < tiempoFinOverlay);

  snprintf(bufL0, sizeof(bufL0), "V%02d N%02d | %s", verb, noun, enOverlay ? "EXEC" : "AGC ");

  if (enOverlay) {
    snprintf(bufL1, sizeof(bufL1), "%-16s", mensajeOverlay);
  } else {
    if (verb == 16) {
      if (noun == 1)      snprintf(bufL1, sizeof(bufL1), "T1:%.1fC H1:%.0f%%", tempAire1, humAire1);
      else if (noun == 2) snprintf(bufL1, sizeof(bufL1), "T2:%.1fC H2:%.0f%%", tempAire2, humAire2);
      else if (noun == 3) snprintf(bufL1, sizeof(bufL1), "LUZ:%s GAS:%d%%", hayLuz ? "SI" : "NO", pctGas);
    }
    else if (verb == 17) {
      if (cantidadLecturas == 0) snprintf(bufL1, sizeof(bufL1), "ESPERANDO DATOS ");
      else {
        if (noun == 1)      snprintf(bufL1, sizeof(bufL1), "PT1:%.1fC PH1:%.0f%%", sumaTemp1/cantidadLecturas, sumaHum1/cantidadLecturas);
        else if (noun == 2) snprintf(bufL1, sizeof(bufL1), "PT2:%.1fC PH2:%.0f%%", sumaTemp2/cantidadLecturas, sumaHum2/cantidadLecturas);
        else if (noun == 3) snprintf(bufL1, sizeof(bufL1), "PROM.GAS: %.0f%%", sumaGas/cantidadLecturas);
      }
    }
    else if (verb == 21) {
      if (noun == 1)      snprintf(bufL1, sizeof(bufL1), "MX1:%.0fC MN1:%.0fC", maxTemp1, minTemp1);
      else if (noun == 2) snprintf(bufL1, sizeof(bufL1), "MX2:%.0fC MN2:%.0fC", maxTemp2, minTemp2);
      else if (noun == 3) snprintf(bufL1, sizeof(bufL1), "MAX GAS: %d%%    ", maxGas);
    }
    else if (verb == 37) {
      if (noun == 3)      snprintf(bufL1, sizeof(bufL1), "ENT:SET CERO GAS");
      else                snprintf(bufL1, sizeof(bufL1), "DHT AUTO CALIB  ");
    }
    else if (verb == 44) {
      if (noun == 3)      snprintf(bufL1, sizeof(bufL1), "ENT:RESET GAS   ");
      else                snprintf(bufL1, sizeof(bufL1), "NO REQUERIDO    ");
    }
  }

  // Refresco no bloqueante
  static char ultL0[24] = "", ultL1[24] = "";
  if (strcmp(bufL0, ultL0) != 0) {
    lcd.setCursor(0, 0); lcd.print(bufL0);
    strncpy(ultL0, bufL0, sizeof(ultL0));
  }
  if (strcmp(bufL1, ultL1) != 0) {
    lcd.setCursor(0, 1); lcd.print(bufL1);
    strncpy(ultL1, bufL1, sizeof(ultL1));
  }
}
