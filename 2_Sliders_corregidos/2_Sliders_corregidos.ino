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

// WiFi
const char* ssid = "Escaner";
const char* password = "12345678";

WebServer server(80);
Preferences prefs;

// Estados motores automáticos
bool motorActive2 = false, movingRight2 = true, canChangeDirection2 = true;
bool motorActive3 = false, movingRight3 = true, canChangeDirection3 = true;
unsigned long lastDebounceTime2 = 0, lastDebounceTime3 = 0;

// Motor 1 (ahora con control por slider)
volatile bool motorBusy1 = false;
volatile bool running1 = false;
volatile int stepsToMove1 = 0;
volatile int direction1 = 1;
int currentPosition1 = 0;
const int maxPosition1 = 100;
const int minPosition1 = 0;

// Motor 4
volatile bool motorBusy4 = false;
volatile bool running4 = false;
volatile int stepsToMove4 = 0;
volatile int direction4 = 1;
int currentPosition4 = 0;
const int maxPosition4 = 100;
const int speedMicro = 800;

// Ticker
Ticker ticker2, ticker3;
const unsigned long debounceDelay = 300;

// HTML Web Interface
const char htmlPage[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head><title>Control Motores</title>
<style>
  .slider-container {
    margin-bottom: 20px;
  }
  .limit-status {
    color: red;
    font-weight: bold;
  }
</style>
</head><body>
<h1>Control Motores</h1>

<div class="slider-container">
  <h2>Motor 1 - Posición: <span id='posVal1'>0</span></h2>
  <input type="range" min="0" max="100" value="0" id="posSlider1" onchange="handleSlider1()" oninput="updateLabel1()"><br>
  <span id="limitStatus1"></span>
</div>

<h2>Motor 2</h2>
<button onclick="location.href='/start2'">Iniciar</button>
<button onclick="location.href='/stop2'">Detener</button>

<h2>Motor 3</h2>
<button onclick="location.href='/start3'">Iniciar</button>
<button onclick="location.href='/stop3'">Detener</button>

<div class="slider-container">
  <h2>Motor 4 - Ángulo (0 a 90): <span id='posVal4'>0</span></h2>
  <input type="range" min="0" max="90" value="0" id="posSlider4" onchange="handleSlider4()" oninput="updateLabel4()"><br>
</div>

<script>
function updateLabel1() {
  const pos = document.getElementById("posSlider1").value;
  document.getElementById("posVal1").innerText = pos;
}
function handleSlider1() {
  const pos = document.getElementById("posSlider1").value;
  fetch("/set1?pos=" + pos);
}

function updateLabel4() {
  const angle = document.getElementById("posSlider4").value;
  document.getElementById("posVal4").innerText = angle;
}
function handleSlider4() {
  const angle = document.getElementById("posSlider4").value;
  fetch("/set4?angle=" + angle);
}

setInterval(() => {
  // Actualizar posición motor 1
  fetch("/get1").then(res => res.json()).then(data => {
    document.getElementById("posVal1").innerText = data.position;
    document.getElementById("posSlider1").value = data.position;
    if(data.limit_left) {
      document.getElementById("limitStatus1").innerText = "LÍMITE MÍNIMO ALCANZADO";
    } else if(data.limit_right) {
      document.getElementById("limitStatus1").innerText = "LÍMITE MÁXIMO ALCANZADO";
    } else {
      document.getElementById("limitStatus1").innerText = "";
    }
  });
  
  // Actualizar posición motor 4
  fetch("/get4").then(res => res.json()).then(data => {
    document.getElementById("posVal4").innerText = data.angle;
    document.getElementById("posSlider4").value = data.angle;
  });
}, 500);
</script>
</body></html>
)rawliteral";

// Pulsos automáticos motores 2-3
void togglePulse2() { digitalWrite(PUL2, motorActive2 ? !digitalRead(PUL2) : LOW); }
void togglePulse3() { digitalWrite(PUL3, motorActive3 ? !digitalRead(PUL3) : LOW); }

// Mover motor 1 a posición específica
void moveMotor1ToPosition(int target) {
  if (target == currentPosition1) return;
  
  stepsToMove1 = abs(target - currentPosition1);
  direction1 = (target > currentPosition1) ? 1 : 0;
  running1 = true;
}

// Mover motor 4 a cero
void moveToZero4() {
  Serial.println("Moviendo Motor 4 a 0...");
  digitalWrite(DIR4, LOW);
  for (int i = 0; i < currentPosition4; i++) {
    digitalWrite(PUL4, HIGH);
    delayMicroseconds(speedMicro);
    digitalWrite(PUL4, LOW);
    delayMicroseconds(speedMicro);
  }
  currentPosition4 = 0;
  prefs.putInt("pos4", currentPosition4);
}

void setup() {
  Serial.begin(115200);
  delay(1000); 

  // Pines motores
  pinMode(PUL1, OUTPUT); pinMode(DIR1, OUTPUT); pinMode(LIMIT_LEFT1, INPUT); pinMode(LIMIT_RIGHT1, INPUT);
  pinMode(PUL2, OUTPUT); pinMode(DIR2, OUTPUT); pinMode(LIMIT_LEFT2, INPUT); pinMode(LIMIT_RIGHT2, INPUT);
  pinMode(PUL3, OUTPUT); pinMode(DIR3, OUTPUT); pinMode(LIMIT_LEFT3, INPUT); pinMode(LIMIT_RIGHT3, INPUT);
  pinMode(PUL4, OUTPUT); pinMode(DIR4, OUTPUT);


  // WiFi como AP
  WiFi.softAP(ssid, password);
  Serial.println("Conectado. IP: " + WiFi.softAPIP().toString());

  // Cargar posiciones guardadas
  prefs.begin("motors", false);
  currentPosition1 = prefs.getInt("pos1", 0);
  currentPosition4 = prefs.getInt("pos4", 0);
  moveToZero4();

  // WebServer rutas
  server.on("/", []() { server.send_P(200, "text/html", htmlPage); });

  // Motor 2
  server.on("/start2", []() { motorActive2 = true; server.send(200, "text/html", htmlPage); });
  server.on("/stop2", []() { motorActive2 = false; server.send(200, "text/html", htmlPage); });

  // Motor 3
  server.on("/start3", []() { motorActive3 = true; server.send(200, "text/html", htmlPage); });
  server.on("/stop3", []() { motorActive3 = false; server.send(200, "text/html", htmlPage); });

  // Motor 1 (nuevo control por slider)
  server.on("/set1", []() {
    if (!server.hasArg("pos")) return;
    int pos = constrain(server.arg("pos").toInt(), minPosition1, maxPosition1);
    moveMotor1ToPosition(pos);
    server.send(200, "text/html", htmlPage);
  });

  server.on("/get1", []() {
    bool limitLeft = digitalRead(LIMIT_LEFT1);
    bool limitRight = digitalRead(LIMIT_RIGHT1);
    String json = "{\"position\":" + String(currentPosition1) + 
                 ",\"limit_left\":" + String(limitLeft ? "true" : "false") + 
                 ",\"limit_right\":" + String(limitRight ? "true" : "false") + "}";
    server.send(200, "application/json", json);
  });

  // Motor 4
  server.on("/set4", []() {
    if (!server.hasArg("angle")) return;
    int angle = constrain(server.arg("angle").toInt(), 0, 90);
    int target = map(angle, 0, 90, 0, maxPosition4);
    if (target == currentPosition4) return;
    if (!motorBusy4 && !running4) {
      stepsToMove4 = abs(target - currentPosition4);
      direction4 = (target > currentPosition4) ? 1 : 0;
      running4 = true;
    }
    server.send(200, "text/html", htmlPage);
  });

  server.on("/get4", []() {
    int angle = map(currentPosition4, 0, maxPosition4, 0, 90);
    server.send(200, "application/json", "{\"angle\":" + String(angle) + "}");
  });

  server.begin();

  // Iniciar pulsos para motores automáticos
  ticker2.attach_us(1000, togglePulse2);
  ticker3.attach_us(100, togglePulse3);
}

void loop() {
  server.handleClient();
  unsigned long now = millis();

  // Lógica motores 2-3
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

  // Movimiento Motor 1 (por slider)
  if (running1 && !motorBusy1) {
    motorBusy1 = true;
    digitalWrite(DIR1, direction1 ? HIGH : LOW);
    
    for (int i = 0; i < stepsToMove1; i++) {
      // Verificar límites durante el movimiento
      if ((direction1 == 1 && digitalRead(LIMIT_RIGHT1)) || 
          (direction1 == 0 && digitalRead(LIMIT_LEFT1))) {
        break; // Detener si se alcanza un límite
      }
      
      digitalWrite(PUL1, HIGH);
      delayMicroseconds(speedMicro);
      digitalWrite(PUL1, LOW);
      delayMicroseconds(speedMicro);
    }
    
    // Actualizar posición (teniendo en cuenta los límites)
    int newPos = currentPosition1 + (direction1 ? stepsToMove1 : -stepsToMove1);
    
    // Ajustar posición si se alcanzó un límite
    if (direction1 == 1 && digitalRead(LIMIT_RIGHT1)) {
      newPos = maxPosition1;
    } else if (direction1 == 0 && digitalRead(LIMIT_LEFT1)) {
      newPos = minPosition1;
    }
    
    currentPosition1 = constrain(newPos, minPosition1, maxPosition1);
    prefs.putInt("pos1", currentPosition1);
    running1 = false;
    motorBusy1 = false;
  }

  // Movimiento Motor 4 (por slider)
  if (running4 && !motorBusy4) {
    motorBusy4 = true;
    digitalWrite(DIR4, direction4 ? HIGH : LOW);
    for (int i = 0; i < stepsToMove4; i++) {
      digitalWrite(PUL4, HIGH);
      delayMicroseconds(speedMicro);
      digitalWrite(PUL4, LOW);
      delayMicroseconds(speedMicro);
    }
    int newPos = currentPosition4 + (direction4 ? stepsToMove4 : -stepsToMove4);
    currentPosition4 = constrain(newPos, 0, maxPosition4);
    prefs.putInt("pos4", currentPosition4);
    running4 = false;
    motorBusy4 = false;
  }
}
