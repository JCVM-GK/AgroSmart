# 📡 AgroSmart v0.2

![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)
![PlatformIO](https://img.shields.io/badge/Framework-PlatformIO-orange.svg)
![C++](https://img.shields.io/badge/Language-C%2B%2B-blue.svg)
![Build](https://img.shields.io/badge/Status-Stable-brightgreen.svg)

Sistema de adquisición, procesamiento y despliegue de telemetría en tiempo real para variables ambientales, basado en una arquitectura de procesamiento distribuido multinodo mediante enlace de comunicación dedicado.

---

## 📐 Arquitectura del Sistema

El proyecto utiliza una topología distribuida para separar la captura de datos sensados de la interfaz gráfica de usuario (DSKY):

- **Nodo Central de Procesamiento (ESP32):** Captura las variables analógicas y digitales de los sensores locales, ejecuta el filtrado de señales y consolida la trama de datos.
- **Nodo de Interfaz DSKY (NodeMCU ESP8266 v2/v3):** Administra la visualización en pantalla LCD 16x2 vía I2C y procesa las entradas de la botonera de navegación local.
- **Enlace de Comunicación Inter-Procesador:** Bus serie asíncrono UART2 (`TX2`/`RX2`) cruzado a **115200 baudios**, transmitiendo tramas estructuradas en texto plano (CSV).

---

## 🔌 Mapa de Conexiones y Hardware

### 1. Nodo Central (ESP32)

| Componente / Periférico      | Pin del Módulo | Pin GPIO ESP32                    | Modo / Función                       |
| :--------------------------- | :------------- | :-------------------------------- | :----------------------------------- |
| **DHT22** (Temp / Humedad)   | DATA           | **GPIO 14**                       | Lectura Digital Monocanal            |
| **LDR** (Sensor de Luz)      | AO (Análogo)   | **GPIO 34**                       | Entrada Analógica (ADC1)             |
| **MQ-Gas** (Calidad de Aire) | AO (Análogo)   | **GPIO 35**                       | Entrada Analógica (ADC1)             |
| **Enlace UART2 -> NodeMCU**  | TX / RX        | **GPIO 17 (TX2) / GPIO 16 (RX2)** | Enlace Serie Transmisión / Recepción |

### 2. Nodo de Interfaz DSKY (NodeMCU)

| Componente / Periférico   | Pin del Módulo      | Pin NodeMCU                   | Modo / Función                       |
| :------------------------ | :------------------ | :---------------------------- | :----------------------------------- |
| **Pantalla LCD 16x2 I2C** | SDA / SCL           | **D2 (GPIO 4) / D1 (GPIO 5)** | Bus I2C Datos y Reloj                |
| **Botonera DSKY**         | VERB / NOUN / ENTER | **D5 / D3 / D7**              | Entradas `INPUT_PULLUP` hacia GND    |
| **Enlace UART <- ESP32**  | RX / TX             | **RX (GPIO 3) / TX (GPIO 1)** | Enlace Serie Recepción / Transmisión |

### 3. Distribución de Alimentación y Referencia Común

- **NodeMCU + LCD 16x2:** Alimentados a $5\text{ V}$ mediante módulo dedicado **MB102** conectado al pin `VIN`.
- **ESP32 + Sensores:** Fuente externa regulada de $5\text{ V}$ dedicada para aislamiento térmico y prevención de caídas de tensión.
- **Masa Común (GND Unificado):** Todos los puntos de `GND` de ambas placas, el módulo MB102 y los sensores están físicamente unificados para establecer la referencia de $0\text{ V}$ del bus UART.

---

## 🛠️ Stack de Desarrollo y Herramientas

- **Entorno de Desarrollo:** VSCodium con **PlatformIO Core**
- **Lenguaje:** C++ (Arduino Framework / ESP-IDF primitives)
- **Control de Versiones:** Git / GitHub
- **Licenciamiento:** GNU General Public License v3.0

---

## 🚀 Cómo Replicar y Compilar el Proyecto

1. Clonar el repositorio:
   ```bash
   git clone [https://github.com/TU_USUARIO/Invernadero_Telemetria.git](https://github.com/TU_USUARIO/Invernadero_Telemetria.git)
   ```
