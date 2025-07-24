#include <WiFi.h>
#include <WebServer.h>
#include <Ticker.h>

// Config WiFi AP
const char* ssid = "Escaner";
const char* password = "12345678";

// Pines Motor 2 (giro)
#define PUL2 26
#define DIR2 25
#define LIMIT_LEFT2 14
#define LIMIT_RIGHT2 12

// Pines Motor 3 (vertical)
#define PUL3 33
#define DIR3 32
#define LIMIT_BOTTOM3 19
#define LIMIT_TOP3 5

WebServer server(80);

// Variables de control
volatile bool escaneoActivo = false;
volatile bool cancelarEscaneo = false;
bool enPosInicial = false;
bool finCarreraTopDetectado = false;  // NUEVA bandera

// Ticker de pulsos
Ticker ticker2, ticker3;

// Control motor 2
bool motor2Activo = false;

// Control motor 3
bool motor3Activo = false;

// Estado de posición inicial
bool motor2EnInicio = false;
bool motor3EnInicio = false;

// HTML simple
const char* html = R"rawliteral(
<!DOCTYPE html><html><head><title>Escaneo</title>
<style>
body { font-family: Arial; padding: 20px; }
button { font-size: 20px; padding: 10px 20px; margin: 10px; }
</style></head><body>
<h2>Secuencia de Escaneo</h2>
<button onclick="fetch('/start')">Iniciar</button>
<button onclick="fetch('/cancel')">Cancelar</button>
</body></html>
)rawliteral";

// Pulsos motores
void togglePulse2() {
  digitalWrite(PUL2, motor2Activo ? !digitalRead(PUL2) : LOW);
}

void togglePulse3() {
  digitalWrite(PUL3, motor3Activo ? !digitalRead(PUL3) : LOW);
}

// Inicializar pines
void setupMotores() {
  pinMode(PUL2, OUTPUT); pinMode(DIR2, OUTPUT);
  pinMode(LIMIT_LEFT2, INPUT); pinMode(LIMIT_RIGHT2, INPUT);

  pinMode(PUL3, OUTPUT); pinMode(DIR3, OUTPUT);
  pinMode(LIMIT_BOTTOM3, INPUT); pinMode(LIMIT_TOP3, INPUT);
}

// Función para limpiar estados
void resetEstados() {
  escaneoActivo = false;
  cancelarEscaneo = false;
  motor2Activo = false;
  motor3Activo = false;
  finCarreraTopDetectado = false;
}

// Búsqueda de posición inicial al encender
void buscarPosInicial() {
  Serial.println("Buscando posición inicial...");

  digitalWrite(DIR2, HIGH);  // Motor 2: izquierda
  motor2Activo = true;
  while (digitalRead(LIMIT_LEFT2) == LOW && !cancelarEscaneo) delay(1);
  motor2Activo = false;
  delay(200);
  motor2EnInicio = true;
  Serial.println("Motor 2 en inicio");

  digitalWrite(DIR3, HIGH);  // Motor 3: abajo
  motor3Activo = true;
  while (digitalRead(LIMIT_BOTTOM3) == LOW && !cancelarEscaneo) delay(1);
  motor3Activo = false;
  delay(200);
  motor3EnInicio = true;
  Serial.println("Motor 3 en inicio");

  enPosInicial = true;
}

// Subir motor 3 por 1 segundo, con posibilidad de cancelación
void subirMotor3PorTiempo() {
  digitalWrite(DIR3, HIGH); // Subir
  motor3Activo = true;

  unsigned long startTime = millis();
  while (millis() - startTime < 1000 && escaneoActivo && !cancelarEscaneo) {
    delay(1);
  }

  motor3Activo = false;
  delay(200);
}

// Secuencia de escaneo principal
void ejecutarEscaneo() {
  Serial.println("Iniciando escaneo...");

  while (escaneoActivo && !finCarreraTopDetectado && !cancelarEscaneo) {
    // Motor 2 → derecha
    Serial.println("Motor 2 → Derecha");
    digitalWrite(DIR2, LOW);
    motor2Activo = true;
    while (digitalRead(LIMIT_RIGHT2) == LOW && escaneoActivo && !cancelarEscaneo) {
      if (digitalRead(LIMIT_TOP3) == HIGH) finCarreraTopDetectado = true;
      delay(1);
    }
    motor2Activo = false;
    delay(300);

    // Motor 2 ← izquierda
    Serial.println("Motor 2 ← Izquierda");
    digitalWrite(DIR2, HIGH);
    motor2Activo = true;
    while (digitalRead(LIMIT_LEFT2) == LOW && escaneoActivo && !cancelarEscaneo) {
      if (digitalRead(LIMIT_TOP3) == HIGH) finCarreraTopDetectado = true;
      delay(1);
    }
    motor2Activo = false;
    delay(300);

    // Subir motor 3
    Serial.println("Subiendo motor 3");
    subirMotor3PorTiempo();

    if (cancelarEscaneo) break;
  }

  if (finCarreraTopDetectado && !cancelarEscaneo) {
    Serial.println("Motor 3 alcanzó límite superior. Escaneo finalizado.");
    // Último barrido motor 2 → derecha
    digitalWrite(DIR2, LOW);
    motor2Activo = true;
    while (digitalRead(LIMIT_RIGHT2) == LOW && !cancelarEscaneo) delay(1);
    motor2Activo = false;
  }

  resetEstados();
}

// Volver a posición inicial
void volverAInicio() {
  Serial.println("Volviendo a posición inicial...");

  // Bajar motor 3
  digitalWrite(DIR3, HIGH);
  motor3Activo = true;
  while (digitalRead(LIMIT_BOTTOM3) == LOW && !escaneoActivo && cancelarEscaneo) delay(1);
  motor3Activo = false;
  delay(300);

  // Motor 2 ← izquierda
  digitalWrite(DIR2, HIGH);
  motor2Activo = true;
  while (digitalRead(LIMIT_LEFT2) == LOW && !escaneoActivo && cancelarEscaneo) delay(1);
  motor2Activo = false;

  Serial.println("Posición inicial alcanzada.");
  enPosInicial = true;
  resetEstados();
}

// Setup inicial
void setup() {
  Serial.begin(115200);
  setupMotores();

  WiFi.softAP(ssid, password);
  Serial.println("WiFi AP: Escaner");
  Serial.println(WiFi.softAPIP());

  // Tickers para pulsos
  ticker2.attach_us(1050, togglePulse2); // Motor 2 ~476 pasos/s
  ticker3.attach_us(30, togglePulse3);  // Motor 3 ~15000 pasos/s

  // WebServer
  server.on("/", []() {
    server.send(200, "text/html", html);
  });

  server.on("/start", []() {
    if (!escaneoActivo) {
      cancelarEscaneo = false;
      escaneoActivo = true;
    }
    server.send(200, "text/plain", "Escaneo iniciado");
  });

  server.on("/cancel", []() {
    cancelarEscaneo = true;
    escaneoActivo = false;
    server.send(200, "text/plain", "Escaneo cancelado");
  });

  server.begin();

  // Posición inicial
  buscarPosInicial();
}

void loop() {
  server.handleClient();

  if (escaneoActivo) {
    enPosInicial = false;
    ejecutarEscaneo();
  }

  if (cancelarEscaneo && !enPosInicial) {
    volverAInicio();
  }
}
