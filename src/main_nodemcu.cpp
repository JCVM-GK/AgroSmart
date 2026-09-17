#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <EEPROM.h>

// ==========================================
// 1. HARDWARE Y ASIGNACIÓN DE PINES (NodeMCU v2)
// ==========================================
// LCD I2C: SDA -> D2 (GPIO 4), SCL -> D1 (GPIO 5)
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Botones DSKY (INPUT_PULLUP hacia GND compartida)
#define BTN_VERB  D5  // GPIO 14
#define BTN_NOUN  D3  // GPIO 0
#define BTN_ENTER D7  // GPIO 13

// ==========================================
// 2. ESTRUCTURAS Y MEMORIA PERSISTENTE (EEPROM)
// ==========================================
#define EEPROM_SIZE    512
#define MAGIC_WORD     0x44534B59 // Identifier hex ASCII "DSKY"

const int FABRICA_GAS_CERO   = 400;
const int FABRICA_SUELO_SECO = 3200;
const int FABRICA_SUELO_AGUA = 1400;

struct CalibracionConfig {
  uint32_t magic;
  int rawGasCero;
  int rawSueloSeco;
  int rawSueloAgua;
  uint16_t checksum;
};

// Variables de Calibración activas
int rawGasCero   = FABRICA_GAS_CERO;
int rawSueloSeco = FABRICA_SUELO_SECO;
int rawSueloAgua = FABRICA_SUELO_AGUA;

// ==========================================
// 3. MENÚS Y ESTADO DSKY (V/N)
// ==========================================
const int listaVerbos[] = {16, 17, 21, 37, 44};
const int TOTAL_VERBOS  = 5;
volatile int indexVerbo = 0;
volatile int verb       = listaVerbos[0];

const int listaNombres[] = {1, 2, 3};
const int TOTAL_NOMBRES  = 3;
volatile int indexNombre = 0;
volatile int noun       = listaNombres[0];

// ==========================================
// 4. DATOS DE SENSORES Y ESTADÍSTICAS
// ==========================================
float tempAire  = 0.0;
float humAire   = 0.0;
int pctGas      = 0;
int pctSuelo    = 0;
bool hayLuz     = false;
int valGasRaw   = 0;
int valSueloRaw = 0;

// Promedios acumulados (V17)
float sumaTemp   = 0.0;
float sumaHum    = 0.0;
float sumaGas    = 0.0;
float sumaSuelo  = 0.0;
unsigned long cantidadLecturas = 0;

// Extremos HI/LO (V21)
float maxTemp = -100.0, minTemp = 100.0;
float maxHum  = 0.0,    minHum  = 100.0;
int maxGas    = 0;
int maxSuelo  = 0,      minSuelo = 100;

// ==========================================
// 5. MÁQUINA DE ESTADOS DE BOTONES (DEBOUNCING)
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

// Overlay LCD sin delay()
char mensajeOverlay[24] = "";
unsigned long tiempoFinOverlay = 0;
unsigned long tUltimaPantalla = 0;

// Declaración de Funciones
uint16_t calcularChecksum(const CalibracionConfig &cfg);
void cargarCalibracionesEEPROM();
void guardarCalibracionEEPROM();
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
  Serial.begin(115200); // UART Receptor desde ESP32
  
  EEPROM.begin(EEPROM_SIZE);
  Wire.begin(D2, D1);   // SDA=D2 (GPIO4), SCL=D1 (GPIO5)

  lcd.init();
  lcd.backlight();

  pinMode(BTN_VERB, INPUT_PULLUP);
  pinMode(BTN_NOUN, INPUT_PULLUP);
  pinMode(BTN_ENTER, INPUT_PULLUP);

  lcd.setCursor(0, 0); lcd.print("V37N01: AGC BOOT");
  lcd.setCursor(0, 1); lcd.print("CARGANDO MEM... ");

  cargarCalibracionesEEPROM();

  lcd.setCursor(0, 1); lcd.print("SISTEMA LISTO   ");
  tiempoFinOverlay = millis() + 1000;
}

// ==========================================
// 7. LOOP PRINCIPAL (ASÍNCRONO / NO BLOQUEANTE)
// ==========================================
void loop() {
  unsigned long ahora = millis();

  recibirDatosUART();
  procesarBotones();

  if (ahora - tUltimaPantalla >= 200) {
    tUltimaPantalla = ahora;
    actualizarPantalla();
  }
}

// ==========================================
// 8. MEMORIA PERSISTENTE (EEPROM + CHECKSUM)
// ==========================================
uint16_t calcularChecksum(const CalibracionConfig &cfg) {
  uint16_t sum = 0;
  sum ^= (cfg.rawGasCero & 0xFFFF) ^ (cfg.rawGasCero >> 16);
  sum ^= (cfg.rawSueloSeco & 0xFFFF) ^ (cfg.rawSueloSeco >> 16);
  sum ^= (cfg.rawSueloAgua & 0xFFFF) ^ (cfg.rawSueloAgua >> 16);
  return sum ^ 0xA55A;
}

void cargarCalibracionesEEPROM() {
  CalibracionConfig cfg;
  EEPROM.get(0, cfg);

  bool valida = (cfg.magic == MAGIC_WORD) &&
                (cfg.checksum == calcularChecksum(cfg)) &&
                (cfg.rawGasCero >= 0 && cfg.rawGasCero <= 4095) &&
                (cfg.rawSueloSeco >= 0 && cfg.rawSueloSeco <= 4095) &&
                (cfg.rawSueloAgua >= 0 && cfg.rawSueloAgua <= 4095);

  if (valida) {
    rawGasCero   = cfg.rawGasCero;
    rawSueloSeco = cfg.rawSueloSeco;
    rawSueloAgua = cfg.rawSueloAgua;
  } else {
    // Si la EEPROM está corrupta o vacía, cargar valores de fábrica de forma segura
    rawGasCero   = FABRICA_GAS_CERO;
    rawSueloSeco = FABRICA_SUELO_SECO;
    rawSueloAgua = FABRICA_SUELO_AGUA;
    guardarCalibracionEEPROM();
  }
}

void guardarCalibracionEEPROM() {
  CalibracionConfig cfg;
  cfg.magic        = MAGIC_WORD;
  cfg.rawGasCero   = rawGasCero;
  cfg.rawSueloSeco = rawSueloSeco;
  cfg.rawSueloAgua = rawSueloAgua;
  cfg.checksum     = calcularChecksum(cfg);

  EEPROM.put(0, cfg);
  EEPROM.commit();
}

// ==========================================
// 9. PARSEO ASÍNCRONO UART (BYTE A BYTE)
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
      if (rxIndex < sizeof(rxBuf) - 1) {
        rxBuf[rxIndex++] = c;
      } else {
        rxIndex = 0; // Prevenir desbordamiento de búfer
      }
    }
  }
}

void parsearTramaUART(char *trama) {
  // Formato esperado: Temp,Hum,GasRaw,SueloRaw,Luz
  float t = 0.0, h = 0.0;
  int gasRaw = 0, sueloRaw = 0, luzVal = 0;

  int asignados = sscanf(trama, "%f,%f,%d,%d,%d", &t, &h, &gasRaw, &sueloRaw, &luzVal);
  if (asignados >= 5) {
    tempAire    = t;
    humAire     = h;
    valGasRaw   = gasRaw;
    valSueloRaw = sueloRaw;
    hayLuz      = (luzVal == 1);

    // Mapeo seguro contra división por cero
    int offsetGas = valGasRaw - rawGasCero;
    if (offsetGas < 0) offsetGas = 0;

    int divGas = 4095 - rawGasCero;
    if (divGas != 0) {
      pctGas = map(offsetGas, 0, divGas, 0, 100);
      pctGas = constrain(pctGas, 0, 100);
    } else {
      pctGas = 0;
    }

    int divSuelo = rawSueloAgua - rawSueloSeco;
    if (divSuelo != 0) {
      pctSuelo = map(valSueloRaw, rawSueloSeco, rawSueloAgua, 0, 100);
      pctSuelo = constrain(pctSuelo, 0, 100);
    } else {
      pctSuelo = 0;
    }

    // Actualizar registro HI/LO (V21)
    if (tempAire > maxTemp)  maxTemp  = tempAire;
    if (tempAire < minTemp)  minTemp  = tempAire;
    if (humAire > maxHum)    maxHum   = humAire;
    if (humAire < minHum)    minHum   = humAire;
    if (pctGas > maxGas)     maxGas   = pctGas;
    if (pctSuelo > maxSuelo) maxSuelo = pctSuelo;
    if (pctSuelo < minSuelo) minSuelo = pctSuelo;

    // Acumular promedios si el verbo activo es V17
    if (verb == 17) {
      if (noun == 1) {
        sumaTemp += tempAire;
        sumaHum  += humAire;
      } else if (noun == 2) {
        sumaGas += pctGas;
      } else if (noun == 3) {
        sumaSuelo += pctSuelo;
      }
      cantidadLecturas++;
    }
  }
}

// ==========================================
// 10. GESTIÓN NO BLOQUEANTE DE BOTONES
// ==========================================
bool actualizarBoton(Boton &b) {
  bool lectura = digitalRead(b.pin);
  unsigned long ahora = millis();
  bool pulsado = false;

  if (lectura != b.ultimoEstadoLectura) {
    b.tiempoUltimoCambio = ahora;
    b.ultimoEstadoLectura = lectura;
  }

  if ((ahora - b.tiempoUltimoCambio) > 40) { // 40ms antirrebote software
    if (lectura != b.estadoConfirmado) {
      b.estadoConfirmado = lectura;
      if (b.estadoConfirmado == LOW) {
        pulsado = true; // Flanco de bajada (HIGH -> LOW)
      }
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
  sumaTemp = 0.0; sumaHum = 0.0; sumaGas = 0.0; sumaSuelo = 0.0;
  cantidadLecturas = 0;
}

void mostrarOverlay(const char *mensaje, unsigned long duracionMs) {
  snprintf(mensajeOverlay, sizeof(mensajeOverlay), "%-16s", mensaje);
  tiempoFinOverlay = millis() + duracionMs;
}

void ejecutarAccion() {
  if (verb == 17) { // REINICIAR PROMEDIOS
    reiniciarPromedio();
    mostrarOverlay(">> PROM RESET <<", 1200);
  }
  else if (verb == 21) { // REINICIAR REGISTROS HI/LO
    if (noun == 1)      { maxTemp = tempAire; minTemp = tempAire; maxHum = humAire; minHum = humAire; }
    else if (noun == 2) { maxGas = pctGas; }
    else if (noun == 3) { maxSuelo = pctSuelo; minSuelo = pctSuelo; }
    mostrarOverlay(">> HI/LO RESET <", 1200);
  }
  else if (verb == 37) { // CALIBRACIÓN DE CAMPO (GUARDAR VALOR RAW ACTUAL)
    if (noun == 1) {
      rawGasCero = valGasRaw;
      guardarCalibracionEEPROM();
      mostrarOverlay("CERO GAS SET MEM", 1500);
    }
    else if (noun == 2) {
      rawSueloSeco = valSueloRaw;
      guardarCalibracionEEPROM();
      mostrarOverlay("SUELO SECO MEM  ", 1500);
    }
    else if (noun == 3) {
      rawSueloAgua = valSueloRaw;
      guardarCalibracionEEPROM();
      mostrarOverlay("SUELO AGUA MEM  ", 1500);
    }
  }
  else if (verb == 44) { // RESTAURAR VALORES DE FÁBRICA
    if (noun == 1) {
      rawGasCero = FABRICA_GAS_CERO;
      guardarCalibracionEEPROM();
      mostrarOverlay("GAS DEF RESTORED", 1500);
    }
    else if (noun == 2) {
      rawSueloSeco = FABRICA_SUELO_SECO;
      guardarCalibracionEEPROM();
      mostrarOverlay("SECO DEF RESTORE", 1500);
    }
    else if (noun == 3) {
      rawSueloAgua = FABRICA_SUELO_AGUA;
      guardarCalibracionEEPROM();
      mostrarOverlay("AGUA DEF RESTORE", 1500);
    }
  }
}

// ==========================================
// 11. INTERFAZ LCD FLICKER-FREE
// ==========================================
void actualizarPantalla() {
  char bufL0[24];
  char bufL1[24];

  bool enOverlay = (millis() < tiempoFinOverlay);
  snprintf(bufL0, sizeof(bufL0), "V%02d N%02d | %s", verb, noun, enOverlay ? "EXEC" : "AGC ");

  if (enOverlay) {
    snprintf(bufL1, sizeof(bufL1), "%-16s", mensajeOverlay);
  } else {
    if (verb == 16) {
      if (noun == 1)      snprintf(bufL1, sizeof(bufL1), "T:%.1fC H:%.0f%%   ", tempAire, humAire);
      else if (noun == 2) snprintf(bufL1, sizeof(bufL1), "LUZ:%s GAS:%d%%  ", hayLuz ? "SI" : "NO", pctGas);
      else if (noun == 3) snprintf(bufL1, sizeof(bufL1), "S.SUELO: %d%%    ", pctSuelo);
    }
    else if (verb == 17) {
      if (cantidadLecturas == 0) {
        snprintf(bufL1, sizeof(bufL1), "ESPERANDO DATOS ");
      } else {
        if (noun == 1)      snprintf(bufL1, sizeof(bufL1), "PT:%.1fC PH:%.0f%% ", sumaTemp/cantidadLecturas, sumaHum/cantidadLecturas);
        else if (noun == 2) snprintf(bufL1, sizeof(bufL1), "PROM.GAS: %.0f%%  ", sumaGas/cantidadLecturas);
        else if (noun == 3) snprintf(bufL1, sizeof(bufL1), "PROM.SUELO:%.0f%% ", sumaSuelo/cantidadLecturas);
      }
    }
    else if (verb == 21) {
      if (noun == 1)      snprintf(bufL1, sizeof(bufL1), "MAX:%.0fC MIN:%.0fC ", maxTemp, minTemp);
      else if (noun == 2) snprintf(bufL1, sizeof(bufL1), "MAX GAS: %d%%    ", maxGas);
      else if (noun == 3) snprintf(bufL1, sizeof(bufL1), "MAX:%d%% MIN:%d%%  ", maxSuelo, minSuelo);
    }
    else if (verb == 37) {
      if (noun == 1)      snprintf(bufL1, sizeof(bufL1), "ENT:SET CERO GAS");
      else if (noun == 2) snprintf(bufL1, sizeof(bufL1), "ENT:SET 0%% SECO ");
      else if (noun == 3) snprintf(bufL1, sizeof(bufL1), "ENT:SET 100%% AGUA");
    }
    else if (verb == 44) {
      if (noun == 1)      snprintf(bufL1, sizeof(bufL1), "ENT:RESET GAS   ");
      else if (noun == 2) snprintf(bufL1, sizeof(bufL1), "ENT:RESET SECO  ");
      else if (noun == 3) snprintf(bufL1, sizeof(bufL1), "ENT:RESET AGUA  ");
    }
  }

  // Refresco no bloqueante anti-parpadeo
  static char ultL0[24] = "";
  static char ultL1[24] = "";

  if (strcmp(bufL0, ultL0) != 0) {
    lcd.setCursor(0, 0);
    lcd.print(bufL0);
    strncpy(ultL0, bufL0, sizeof(ultL0));
  }
  if (strcmp(bufL1, ultL1) != 0) {
    lcd.setCursor(0, 1);
    lcd.print(bufL1);
    strncpy(ultL1, bufL1, sizeof(ultL1));
  }
}
