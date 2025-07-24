#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <Ticker.h>

// Red WiFi generada por el ESP32
const char* ssid = "Escaner";
const char* password = "12345678";

// Motor 1 (riel)
#define PUL1 23
#define DIR1 27
#define LIMIT_LEFT1 18
#define LIMIT_RIGHT1 13

// Motor 2
#define PUL2 26
#define DIR2 25
#define LIMIT_LEFT2 14
#define LIMIT_RIGHT2 12

// Motor 3
#define PUL3 33
#define DIR3 32
#define LIMIT_LEFT3 19
#define LIMIT_RIGHT3 5

// Motor 4 (slider)
#define PUL4 21
#define DIR4 22

// Variables motor 4
volatile int stepsToMove4 = 0;
volatile int direction4 = 1;
volatile bool running4 = false;
volatile bool motorBusy4 = false;
int currentPosition4 = 0;
const int maxPosition4 = 100;
const int speedMicro4 = 800;

Preferences prefs;
WebServer server(80);

// Estados motores 1-3
bool motorActive1 = false, motorActive2 = false, motorActive3 = false;
bool movingRight1 = true, movingRight2 = true, movingRight3 = true;
bool canChangeDirection2 = true, canChangeDirection3 = true;

unsigned long lastDebounceTime2 = 0, lastDebounceTime3 = 0;
const unsigned long debounceDelay = 300;

Ticker motorPulseTicker1, motorPulseTicker2, motorPulseTicker3;

void togglePulse1() { digitalWrite(PUL1, motorActive1 ? !digitalRead(PUL1) : LOW); }
void togglePulse2() { digitalWrite(PUL2, motorActive2 ? !digitalRead(PUL2) : LOW); }
void togglePulse3() { digitalWrite(PUL3, motorActive3 ? !digitalRead(PUL3) : LOW); }

const char* html = R"rawliteral(
<!DOCTYPE html><html><head><title>Escaner</title>
<meta name="viewport" content="width=device-width, initial-scale=1">
<style>
body { font-family: Arial; padding: 20px; }
button, input { font-size: 1.2em; margin: 5px; padding: 10px; }
</style>
</head><body>

<h2>Ancho de objeto</h2>
<button 
  onmousedown="fetch('/startRight1')" 
  onmouseup="fetch('/stop1')" 
  ontouchstart="fetch('/startRight1')" 
  ontouchend="fetch('/stop1')">Derecha</button>

<button 
  onmousedown="fetch('/startLeft1')" 
  onmouseup="fetch('/stop1')" 
  ontouchstart="fetch('/startLeft1')" 
  ontouchend="fetch('/stop1')">Izquierda</button>

<h2>Motor 2</h2>
<button onclick="location.href='/start2'">Iniciar</button>
<button onclick="location.href='/stop2'">Detener</button>

<h2>Motor 3</h2>
<button onclick="location.href='/start3'">Iniciar</button>
<button onclick="location.href='/stop3'">Detener</button>

<h2>Inclinacion de camara</h2>
<label>Angulo (0 a 90): <span id="stepVal">45</span></label><br>
<input type="range" min="0" max="90" value="45" id="stepsSlider" onchange="handleSlider()" oninput="updateLabel()"><br><br>

<script>
function updateLabel() {
  const angle = document.getElementById("stepsSlider").value;
  document.getElementById("stepVal").innerText = angle;
}
function handleSlider() {
  const angle = document.getElementById("stepsSlider").value;
  fetch("/set?angle=" + angle);
}
setInterval(() => {
  fetch("/get").then(res => res.json()).then(data => {
    document.getElementById("stepVal").innerText = data.angle;
    document.getElementById("stepsSlider").value = data.angle;
  });
}, 1000);
</script>
</body></html>
)rawliteral";

void setup() {
  Serial.begin(115200);
  delay(1000);

  // Pines motores 1-3
  pinMode(PUL1, OUTPUT); pinMode(DIR1, OUTPUT);
  pinMode(LIMIT_LEFT1, INPUT); pinMode(LIMIT_RIGHT1, INPUT);

  pinMode(PUL2, OUTPUT); pinMode(DIR2, OUTPUT);
  pinMode(LIMIT_LEFT2, INPUT); pinMode(LIMIT_RIGHT2, INPUT);

  pinMode(PUL3, OUTPUT); pinMode(DIR3, OUTPUT);
  pinMode(LIMIT_LEFT3, INPUT); pinMode(LIMIT_RIGHT3, INPUT);

  // Pines motor 4
  pinMode(PUL4, OUTPUT);
  pinMode(DIR4, OUTPUT);

  // Preferencias
  prefs.begin("motor4", false);
  currentPosition4 = prefs.getInt("pos", 0);

  // Crear red WiFi
  WiFi.softAP(ssid, password);
  Serial.println("Red WiFi: " + String(ssid));
  Serial.println("IP: " + WiFi.softAPIP().toString());

  // Rutas
  server.on("/", []() { server.send(200, "text/html", html); });

  // Motor 1 control dinámico
  server.on("/startRight1", []() {
    motorActive1 = true;
    movingRight1 = true;
    digitalWrite(DIR1, LOW);
    server.send(200, "text/plain", "Right");
  });

  server.on("/startLeft1", []() {
    motorActive1 = true;
    movingRight1 = false;
    digitalWrite(DIR1, HIGH);
    server.send(200, "text/plain", "Left");
  });

  server.on("/stop1", []() {
    motorActive1 = false;
    server.send(200, "text/plain", "Stopped");
  });

  // Motor 2
  server.on("/start2", []() {
    motorActive2 = true;
    server.send(200, "text/html", html);
  });
  server.on("/stop2", []() {
    motorActive2 = false;
    server.send(200, "text/html", html);
  });

  // Motor 3
  server.on("/start3", []() {
    motorActive3 = true;
    server.send(200, "text/html", html);
  });
  server.on("/stop3", []() {
    motorActive3 = false;
    server.send(200, "text/html", html);
  });

  // Motor 4 - slider
  server.on("/get", []() {
    int angle = map(currentPosition4, 0, maxPosition4, 0, 90);
    server.send(200, "application/json", "{\"angle\":" + String(angle) + "}");
  });

  server.on("/set", []() {
    if (!server.hasArg("angle")) return;
    int angle = constrain(server.arg("angle").toInt(), 0, 90);
    int target = map(angle, 0, 90, 0, maxPosition4);
    if (target == currentPosition4 || running4 || motorBusy4) return;
    direction4 = (target > currentPosition4) ? 1 : 0;
    stepsToMove4 = abs(target - currentPosition4);
    running4 = true;
  });

  server.begin();

  // Iniciar pulsos
  motorPulseTicker1.attach_us(30, togglePulse1);
  motorPulseTicker2.attach_us(1050, togglePulse2);
  motorPulseTicker3.attach_us(100, togglePulse3);
}

void loop() {
  server.handleClient();
  unsigned long now = millis();

  // Motor 1 (manual con límites)
  if (motorActive1) {
    if (movingRight1 && digitalRead(LIMIT_RIGHT1) == HIGH) {
      motorActive1 = false;
      Serial.println("Motor 1: Límite DERECHA alcanzado");
    } else if (!movingRight1 && digitalRead(LIMIT_LEFT1) == HIGH) {
      motorActive1 = false;
      Serial.println("Motor 1: Límite IZQUIERDA alcanzado");
    }
  }

  // Motor 2 (automático)
  if (motorActive2) {
    if (canChangeDirection2) {
      if (movingRight2 && digitalRead(LIMIT_RIGHT2) == HIGH) {
        movingRight2 = false;
        digitalWrite(DIR2, HIGH);
        canChangeDirection2 = false;
        lastDebounceTime2 = now;
      } else if (!movingRight2 && digitalRead(LIMIT_LEFT2) == HIGH) {
        movingRight2 = true;
        digitalWrite(DIR2, LOW);
        canChangeDirection2 = false;
        lastDebounceTime2 = now;
      }
    } else if (now - lastDebounceTime2 > debounceDelay) {
      canChangeDirection2 = true;
    }
  }

  // Motor 3 (automático)
  if (motorActive3) {
    if (canChangeDirection3) {
      if (movingRight3 && digitalRead(LIMIT_RIGHT3) == HIGH) {
        movingRight3 = false;
        digitalWrite(DIR3, HIGH);
        canChangeDirection3 = false;
        lastDebounceTime3 = now;
      } else if (!movingRight3 && digitalRead(LIMIT_LEFT3) == HIGH) {
        movingRight3 = true;
        digitalWrite(DIR3, LOW);
        canChangeDirection3 = false;
        lastDebounceTime3 = now;
      }
    } else if (now - lastDebounceTime3 > debounceDelay) {
      canChangeDirection3 = true;
    }
  }

  // Motor 4 (slider)
  if (running4 && !motorBusy4) {
    motorBusy4 = true;
    digitalWrite(DIR4, direction4 ? HIGH : LOW);

    for (int i = 0; i < stepsToMove4; i++) {
      digitalWrite(PUL4, HIGH); delayMicroseconds(speedMicro4);
      digitalWrite(PUL4, LOW); delayMicroseconds(speedMicro4);
    }

    currentPosition4 += (direction4 ? stepsToMove4 : -stepsToMove4);
    currentPosition4 = constrain(currentPosition4, 0, maxPosition4);
    prefs.putInt("pos", currentPosition4);

    stepsToMove4 = 0;
    running4 = false;
    motorBusy4 = false;
  }
}

