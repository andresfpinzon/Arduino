#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <Ticker.h>

// Motor 1
#define PUL1 23
#define DIR1 27
#define LIMIT_LEFT1 18
#define LIMIT_RIGHT1 13

//Motor 2
#define PUL2 26
#define DIR2 25
#define LIMIT_LEFT2 14
#define LIMIT_RIGHT2 12

//Motor 3
#define PUL3 33
#define DIR3 32
#define LIMIT_LEFT3 19
#define LIMIT_RIGHT3 5

// Motor 4 
#define PUL4 21
#define DIR4 22

// Botón
#define BUTTON_PIN 17

// WiFi
const char* ssid = "Escaner";
const char* password = "12345678";

WebServer server(80);
Preferences prefs;

// Estados motores automáticos
bool motorActive2 = false, movingRight2 = true, canChangeDirection2 = true;
bool motorActive3 = false, movingRight3 = true, canChangeDirection3 = true;
unsigned long lastDebounceTime1 = 0, lastDebounceTime2 = 0, lastDebounceTime3 = 0;

// Motor 4
volatile bool motorBusy4 = false;
volatile bool running4 = false;
volatile int stepsToMove4 = 0;
volatile int direction4 = 1;
int currentPosition4 = 0;
const int maxPosition4 = 100;
const int speedMicro = 800;

// Motor 1 con slider
volatile bool motorBusy1 = false;
volatile bool running1 = false;
volatile int stepsToMove1 = 0;
volatile int direction1 = 1;
int currentPosition1 = 0;
const int maxPosition1 = 100;
bool limitReached1 = false;

// Ticker
Ticker ticker2, ticker3;
const unsigned long debounceDelay = 300;

// HTML Web Interface
const char htmlPage[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head><title>Control Motores</title></head><body>
<h1>Control Motores</h1>

<h2>Motor 1 - Ancho (0 a 90): <span id='anchoVal'>0</span></h2>
<input type="range" min="0" max="90" value="0" id="anchoSlider" onchange="setAncho()" oninput="updateAncho()"><br><br>

<h2>Motor 2</h2>
<button onclick="location.href='/start2'">Iniciar</button>
<button onclick="location.href='/stop2'">Detener</button>

<h2>Motor 3</h2>
<button onclick="location.href='/start3'">Iniciar</button>
<button onclick="location.href='/stop3'">Detener</button>

<h2>Motor 4 - Ángulo (0 a 90): <span id='stepVal'>0</span></h2>
<input type="range" min="0" max="90" value="0" id="stepsSlider" onchange="handleSlider()" oninput="updateLabel()"><br><br>

<script>
function updateAncho() {
  const v = document.getElementById("anchoSlider").value;
  document.getElementById("anchoVal").innerText = v;
}
function updateLabel() {
  const v = document.getElementById("stepsSlider").value;
  document.getElementById("stepVal").innerText = v;
}
function setAncho() {
  const v = document.getElementById("anchoSlider").value;
  fetch("/setAncho?valor=" + v);
}
function handleSlider() {
  const v = document.getElementById("stepsSlider").value;
  fetch("/set?angle=" + v);
}
setInterval(() => {
  fetch("/get").then(res => res.json()).then(data => {
    document.getElementById("anchoVal").innerText = data.ancho;
    document.getElementById("anchoSlider").value = data.ancho;
    document.getElementById("stepVal").innerText = data.angle;
    document.getElementById("stepsSlider").value = data.angle;
  });
}, 1000);
</script>
</body></html>
)rawliteral";

void setup() {
  Serial.begin(115200);

  pinMode(PUL1, OUTPUT); pinMode(DIR1, OUTPUT); pinMode(LIMIT_LEFT1, INPUT); pinMode(LIMIT_RIGHT1, INPUT);
  pinMode(PUL2, OUTPUT); pinMode(DIR2, OUTPUT); pinMode(LIMIT_LEFT2, INPUT); pinMode(LIMIT_RIGHT2, INPUT);
  pinMode(PUL3, OUTPUT); pinMode(DIR3, OUTPUT); pinMode(LIMIT_LEFT3, INPUT); pinMode(LIMIT_RIGHT3, INPUT);
  pinMode(PUL4, OUTPUT); pinMode(DIR4, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  WiFi.softAP(ssid, password);
  Serial.println("Conectado. IP: " + WiFi.softAPIP().toString());

  prefs.begin("motor4", false);
  currentPosition4 = prefs.getInt("pos", 0);

  server.on("/", []() { server.send_P(200, "text/html", htmlPage); });
  server.on("/start2", []() { motorActive2 = true; server.send(200, "text/html", htmlPage); });
  server.on("/stop2", []() { motorActive2 = false; server.send(200, "text/html", htmlPage); });
  server.on("/start3", []() { motorActive3 = true; server.send(200, "text/html", htmlPage); });
  server.on("/stop3", []() { motorActive3 = false; server.send(200, "text/html", htmlPage); });

  server.on("/set", []() {
    if (!server.hasArg("angle")) return;
    int angle = constrain(server.arg("angle").toInt(), 0, 90);
    int target = map(angle, 0, 90, 0, maxPosition4);
    if (target == currentPosition4) return;
    if (!motorBusy4 && !running4) {
      stepsToMove4 = abs(target - currentPosition4);
      direction4 = (target > currentPosition4) ? 1 : 0;
      running4 = true;
    }
  });

  server.on("/setAncho", []() {
    if (!server.hasArg("valor")) return;
    int angle = constrain(server.arg("valor").toInt(), 0, 90);
    int target = map(angle, 0, 90, 0, maxPosition1);
    if (target == currentPosition1) return;
    if (!motorBusy1 && !running1 && !limitReached1) {
      stepsToMove1 = abs(target - currentPosition1);
      direction1 = (target > currentPosition1) ? 1 : 0;
      running1 = true;
    }
  });

  server.on("/get", []() {
    int angle = map(currentPosition4, 0, maxPosition4, 0, 90);
    int ancho = map(currentPosition1, 0, maxPosition1, 0, 90);
    server.send(200, "application/json", "{\"angle\":" + String(angle) + ",\"ancho\":" + String(ancho) + "}");
  });

  server.on("/limit1", []() {
    String estado = "{\"limit\":";
    if (digitalRead(LIMIT_LEFT1)) estado += "\"left\"";
    else if (digitalRead(LIMIT_RIGHT1)) estado += "\"right\"";
    else estado += "\"none\"";
    estado += "}";
    server.send(200, "application/json", estado);
  });

  server.begin();
  ticker2.attach_us(1000, []() { digitalWrite(PUL2, motorActive2 ? !digitalRead(PUL2) : LOW); });
  ticker3.attach_us(100, []() { digitalWrite(PUL3, motorActive3 ? !digitalRead(PUL3) : LOW); });
}

void loop() {
  server.handleClient();
  unsigned long now = millis();

  // Motor 1 con slider y final de carrera
  if (running1 && !motorBusy1 && !limitReached1) {
    motorBusy1 = true;
    digitalWrite(DIR1, direction1 ? HIGH : LOW);
    for (int i = 0; i < stepsToMove1; i++) {
      if ((direction1 && digitalRead(LIMIT_RIGHT1)) || (!direction1 && digitalRead(LIMIT_LEFT1))) {
        limitReached1 = true;
        Serial.println("Motor 1: Límite alcanzado");
        break;
      }
      digitalWrite(PUL1, HIGH); delayMicroseconds(speedMicro);
      digitalWrite(PUL1, LOW); delayMicroseconds(speedMicro);
    }
    if (!limitReached1) {
      int newPos = currentPosition1 + (direction1 ? stepsToMove1 : -stepsToMove1);
      currentPosition1 = constrain(newPos, 0, maxPosition1);
    }
    running1 = false;
    motorBusy1 = false;
  }

  // Motor 2
  if (motorActive2) {
    if (canChangeDirection2) {
      if (movingRight2 && digitalRead(LIMIT_RIGHT2)) {
        movingRight2 = false; digitalWrite(DIR2, HIGH);
        canChangeDirection2 = false; lastDebounceTime2 = now;
      } else if (!movingRight2 && digitalRead(LIMIT_LEFT2)) {
        movingRight2 = true; digitalWrite(DIR2, LOW);
        canChangeDirection2 = false; lastDebounceTime2 = now;
      }
    } else if (now - lastDebounceTime2 > debounceDelay) canChangeDirection2 = true;
  }

  // Motor 3
  if (motorActive3) {
    if (canChangeDirection3) {
      if (movingRight3 && digitalRead(LIMIT_RIGHT3)) {
        movingRight3 = false; digitalWrite(DIR3, HIGH);
        canChangeDirection3 = false; lastDebounceTime3 = now;
      } else if (!movingRight3 && digitalRead(LIMIT_LEFT3)) {
        movingRight3 = true; digitalWrite(DIR3, LOW);
        canChangeDirection3 = false; lastDebounceTime3 = now;
      }
    } else if (now - lastDebounceTime3 > debounceDelay) canChangeDirection3 = true;
  }

  // Motor 4
  if (running4 && !motorBusy4) {
    motorBusy4 = true;
    digitalWrite(DIR4, direction4 ? HIGH : LOW);
    for (int i = 0; i < stepsToMove4; i++) {
      digitalWrite(PUL4, HIGH); delayMicroseconds(speedMicro);
      digitalWrite(PUL4, LOW); delayMicroseconds(speedMicro);
    }
    int newPos = currentPosition4 + (direction4 ? stepsToMove4 : -stepsToMove4);
    currentPosition4 = constrain(newPos, 0, maxPosition4);
    prefs.putInt("pos", currentPosition4);
    running4 = false;
    motorBusy4 = false;
  }
}

