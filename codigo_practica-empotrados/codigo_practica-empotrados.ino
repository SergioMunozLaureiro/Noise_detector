// ============================================================
//  Monitor de Ruido para Aula - Arduino Uno
//  LCD I2C + KY-038 + LEDs + Buzzer + Bluetooth HC-05
//
//  Flujo del buzzer:
//    1. Sube el nivel → pitido 2-3 segundos
//    2. Para el pitido → sensor bloqueado 500ms
//    3. Vuelve a escuchar normal
//
//  Detección doble:
//    - Media móvil para sonidos continuos
//    - Pico directo para palmadas y sonidos cortos
//
//  Umbrales:
//    0-12   → VERDE
//    13-49  → AMARILLO
//    50+    → ROJO
//
//  Comandos desde el móvil:
//    V = forzar verde
//    A = forzar amarillo
//    R = forzar rojo
//    M = volver a modo automático
// ============================================================

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <SoftwareSerial.h>

LiquidCrystal_I2C lcd(0x27, 16, 2);
SoftwareSerial bluetooth(4, 7); // RX=4, TX=7

const int pinBuzzer   = 6;
const int pinVerde    = 8;
const int pinAmarillo = 9;
const int pinRojo     = 10;
const int pinSensor   = A2;

// --- Umbrales ---
const int UMBRAL_AMARILLO      = 13;  // media > 13 → amarillo
const int UMBRAL_ROJO          = 50;  // media > 50 → rojo
const int UMBRAL_PICO_AMARILLO = 13;  // pico > 13 → amarillo
const int UMBRAL_PICO_ROJO     = 50;  // pico > 50 → rojo

// --- Estado ---
int  estadoActual = 0;
bool modoManual   = false;

// --- Filtro de media móvil ---
int historial[5] = {0, 0, 0, 0, 0};
int indice = 0;

// --- Temporización ---
unsigned long ultimoEnvio        = 0;
unsigned long ultimoEstadoRojo   = 0;
int ruidoMaxEnvio                = 0;
const int INTERVALO_ENVIO        = 200;
const int DURACION_ROJO          = 1000;

// --- Control del buzzer ---
int estadoBuzzer                   = 0; // 0=libre, 1=sonando, 2=bloqueado
unsigned long inicioBuzzer         = 0;
unsigned long finBuzzer            = 0;
const int DURACION_PITIDO_AMARILLO = 2000;
const int DURACION_PITIDO_ROJO     = 3000;
const int BLOQUEO_SENSOR           = 500;

// ----------------------------------------------------------------
int leerSensor() {
  int minimo = 1023;
  int maximo = 0;
  for (int i = 0; i < 250; i++) {
    int lectura = analogRead(pinSensor);
    if (lectura > maximo) maximo = lectura;
    if (lectura < minimo) minimo = lectura;
    delayMicroseconds(300);
  }
  return maximo - minimo;
}

// ----------------------------------------------------------------
void apagarLeds() {
  digitalWrite(pinVerde,    LOW);
  digitalWrite(pinAmarillo, LOW);
  digitalWrite(pinRojo,     LOW);
}

// ----------------------------------------------------------------
void leerBluetooth() {
  while (bluetooth.available()) {
    char comando = bluetooth.read();

    if (comando == '\n' || comando == '\r' || comando == ' ') continue;

    if (comando == 'V' || comando == 'v') {
      modoManual = true; estadoActual = 0;
      bluetooth.println(">> Modo manual: VERDE");
    } else if (comando == 'A' || comando == 'a') {
      modoManual = true; estadoActual = 1;
      bluetooth.println(">> Modo manual: AMARILLO");
    } else if (comando == 'R' || comando == 'r') {
      modoManual = true; estadoActual = 2;
      bluetooth.println(">> Modo manual: ROJO");
    } else if (comando == 'M' || comando == 'm') {
      modoManual = false; estadoActual = 0;
      bluetooth.println(">> Modo automatico activado");
    }
  }
}

// ----------------------------------------------------------------
void gestionarBuzzer() {
  unsigned long ahora = millis();
  if (estadoBuzzer == 1) {
    int duracion = (estadoActual == 2) ? DURACION_PITIDO_ROJO : DURACION_PITIDO_AMARILLO;
    if (ahora - inicioBuzzer > duracion) {
      noTone(pinBuzzer);
      finBuzzer    = ahora;
      estadoBuzzer = 2;
    }
  } else if (estadoBuzzer == 2) {
    if (ahora - finBuzzer > BLOQUEO_SENSOR) {
      estadoBuzzer = 0;
    }
  }
}

// ----------------------------------------------------------------
void activarBuzzer(int frecuencia) {
  if (estadoBuzzer == 0) {
    tone(pinBuzzer, frecuencia);
    inicioBuzzer = millis();
    estadoBuzzer = 1;
  }
}

// ================================================================
void setup() {
  pinMode(pinVerde,    OUTPUT);
  pinMode(pinAmarillo, OUTPUT);
  pinMode(pinRojo,     OUTPUT);
  pinMode(pinBuzzer,   OUTPUT);

  Serial.begin(9600);
  bluetooth.begin(9600);

  Wire.begin();
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.print("SISTEMA ESTABLE");
  delay(1000);
}

// ================================================================
void loop() {
  leerBluetooth();
  gestionarBuzzer();

  // Leer sensor solo si no está bloqueado
  int ruido = (estadoBuzzer == 2) ? 0 : leerSensor();

  // --- Filtro de media móvil ---
  historial[indice] = ruido;
  indice = (indice + 1) % 5;
  int media = 0;
  for (int i = 0; i < 5; i++) media += historial[i];
  media = media / 5;

  Serial.print("Sensor: ");
  Serial.print(ruido);
  Serial.print(" | Media: ");
  Serial.println(media);

  // --- Lógica automática con detección doble ---
  if (!modoManual) {
    bool picoRojo      = ruido > UMBRAL_PICO_ROJO;
    bool picoAmarillo  = ruido > UMBRAL_PICO_AMARILLO;
    bool mediaRoja     = media > UMBRAL_ROJO;
    bool mediaAmarilla = media > UMBRAL_AMARILLO;

    if (mediaRoja || picoRojo) {
      estadoActual     = 2;
      ultimoEstadoRojo = millis();

    } else if ((mediaAmarilla || picoAmarillo) && estadoActual != 2) {
      estadoActual = 1;

    } else if (media < 10) {
      if (estadoActual == 2 && millis() - ultimoEstadoRojo > DURACION_ROJO) {
        estadoActual = 0;
      } else if (estadoActual != 2) {
        estadoActual = 0;
      }
    }
  }

  // --- Guardar máximo para Bluetooth ---
  if (ruido > ruidoMaxEnvio) ruidoMaxEnvio = ruido;
  if (millis() - ultimoEnvio > INTERVALO_ENVIO) {
    ultimoEnvio = millis();
    String modo   = modoManual ? "MANUAL" : "AUTO";
    String estado = estadoActual == 0 ? "VERDE" : estadoActual == 1 ? "AMARILLO" : "ROJO";
    bluetooth.print("Ruido: ");
    bluetooth.print(ruidoMaxEnvio);
    bluetooth.print(" | ");
    bluetooth.print(estado);
    bluetooth.print(" | ");
    bluetooth.println(modo);
    ruidoMaxEnvio = 0;
  }

  // --- Mostrar en LCD ---
  lcd.setCursor(0, 0);
  lcd.print(" Nivel:");
  lcd.print(media);
  lcd.print("   ");

  // --- Actuar según estado ---
  apagarLeds();

  if (estadoActual == 2) {
    digitalWrite(pinRojo, HIGH);
    lcd.setCursor(0, 1);
    lcd.print("!ALERTA ROJA!  ");
    activarBuzzer(2000);

  } else if (estadoActual == 1) {
    digitalWrite(pinAmarillo, HIGH);
    lcd.setCursor(0, 1);
    lcd.print("AVISO MODERADO ");
    activarBuzzer(500);

  } else {
    digitalWrite(pinVerde, HIGH);
    noTone(pinBuzzer);
    estadoBuzzer = 0;
    lcd.setCursor(0, 1);
    lcd.print("TODO EN ORDEN  ");
  }

  delay(20);
}
