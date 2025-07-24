#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <Ticker.h>

const char* ssid = "Escaner";
const char* password = "12345678";

// Pines Motores
#define PUL1 23
#define DIR1 27
#define LIMIT_LEFT1 18
#define LIMIT_RIGHT1 13

#define PUL2 26
#define DIR2 25
#define LIMIT_LEFT2 14
#define LIMIT_RIGHT2 12

#define PUL3 33
#define DIR3 32
#define LIMIT_BOTTOM3 19
#define LIMIT_TOP3 5

#define PUL4 21
#define DIR4 22
#define POT_PIN 39

Preferences prefs;
WebServer server(80);
Ticker ticker1, ticker2, ticker3; // Unificado tickers

// Variables Motor 1
bool motorActive1 = false;
bool movingRight1 = true;

// Variables Motor 4
volatile int stepsToMove4 = 0;
volatile int direction4 = 1;
volatile bool running4 = false;
volatile bool motorBusy4 = false;
int currentPosition4 = 0;
const int maxPosition4 = 50;
const int speedMicro4 = 800;
const int POT_MIN = 1616;
const int POT_MAX = 3120;

// Variables Escaneo (Motores 2 y 3)
volatile bool escaneoActivo = false;
volatile bool cancelarEscaneo = false;
volatile bool detenerEmergencia = false;
volatile bool pausa = false;
bool enPosInicial = false;
bool finCarreraTopDetectado = false;
int contadorSubidas = 0;
bool motor2Activo = false;
bool motor3Activo = false;
bool motor2EnInicio = false;
bool motor3EnInicio = false;

// Funciones pulsos motores
void togglePulse1() { digitalWrite(PUL1, motorActive1 ? !digitalRead(PUL1) : LOW); }
void togglePulse2() { digitalWrite(PUL2, motor2Activo ? !digitalRead(PUL2) : LOW); }
void togglePulse3() { digitalWrite(PUL3, motor3Activo ? !digitalRead(PUL3) : LOW); }

// Configuración pines
void setupPines() {
  // Motor 1
  pinMode(PUL1, OUTPUT);
  pinMode(DIR1, OUTPUT);
  pinMode(LIMIT_LEFT1, INPUT);
  pinMode(LIMIT_RIGHT1, INPUT);
  
  // Motor 2 (giro)
  pinMode(PUL2, OUTPUT);
  pinMode(DIR2, OUTPUT);
  pinMode(LIMIT_LEFT2, INPUT);
  pinMode(LIMIT_RIGHT2, INPUT);
  
  // Motor 3 (vertical)
  pinMode(PUL3, OUTPUT);
  pinMode(DIR3, OUTPUT);
  pinMode(LIMIT_BOTTOM3, INPUT);
  pinMode(LIMIT_TOP3, INPUT);
  
  // Motor 4 (cámara)
  pinMode(PUL4, OUTPUT);
  pinMode(DIR4, OUTPUT);
  pinMode(POT_PIN, INPUT);
}

// ================== Funciones Motor 4 ==================
int getAngleFromPosition(int pos) {
  return map(pos, 0, maxPosition4, 90, 0);
}

int getPotReading() {
  return analogRead(POT_PIN);
}

void buscarPosicionValidaAlInicio() {
  int pot = getPotReading();
  if (pot >= POT_MIN && pot <= POT_MAX) return;

  const int maxIntentos = 150;
  bool direccionSubir = (pot < POT_MIN);
  digitalWrite(DIR4, direccionSubir ? LOW : HIGH);

  for (int i = 0; i < maxIntentos; i++) {
    digitalWrite(PUL4, HIGH); delayMicroseconds(speedMicro4);
    digitalWrite(PUL4, LOW); delayMicroseconds(speedMicro4);
    currentPosition4 += (direccionSubir ? -1 : 1);
    currentPosition4 = constrain(currentPosition4, 0, maxPosition4);
    pot = getPotReading();
    if (pot >= POT_MIN && pot <= POT_MAX) break;
    if (currentPosition4 == 0 || currentPosition4 == maxPosition4) break;
  }
  prefs.putInt("pos", currentPosition4);
}

// ================== Funciones Escaneo ==================
void resetEstados() {
  escaneoActivo = false;
  cancelarEscaneo = false;
  motor2Activo = false;
  motor3Activo = false;
  finCarreraTopDetectado = false;
  pausa = false;
}

void manejarPausa() {
  if (pausa) {
    bool estadoMotor2 = motor2Activo;
    bool estadoMotor3 = motor3Activo;
    motor2Activo = false;
    motor3Activo = false;
    
    while (pausa && !cancelarEscaneo && !detenerEmergencia) {
      delay(10);
      server.handleClient();
    }
    
    if (!cancelarEscaneo && !detenerEmergencia) {
      motor2Activo = estadoMotor2;
      motor3Activo = estadoMotor3;
    }
  }
}

void buscarPosInicial() {
  // Motor 2 a izquierda
  digitalWrite(DIR2, HIGH);
  motor2Activo = true;
  while (digitalRead(LIMIT_LEFT2) == LOW && !cancelarEscaneo) {
    manejarPausa();
    delay(1);
    server.handleClient();
  }
  motor2Activo = false;
  delay(200);
  motor2EnInicio = true;

  // Motor 3 abajo
  digitalWrite(DIR3, LOW);
  motor3Activo = true;
  while (digitalRead(LIMIT_BOTTOM3) == LOW && !cancelarEscaneo) {
    manejarPausa();
    delay(1);
    server.handleClient();
  }
  motor3Activo = false;
  delay(200);
  motor3EnInicio = true;

  enPosInicial = true;
}

void subirMotor3PorTiempo() {
  if (digitalRead(LIMIT_TOP3) == HIGH) {
    finCarreraTopDetectado = true;
    return;
  }

  digitalWrite(DIR3, HIGH);
  motor3Activo = true;
  unsigned long start = millis();
  while (millis() - start < 1000 && escaneoActivo && !cancelarEscaneo) {
    manejarPausa();
    delay(1);
    server.handleClient();
    if (digitalRead(LIMIT_TOP3) == HIGH) {
      finCarreraTopDetectado = true;
      break;
    }
  }
  motor3Activo = false;
  delay(200);
  contadorSubidas++;
}

void detenerTodosLosMotores() {
  detenerEmergencia = true;  
  escaneoActivo = false;
  pausa = false;
  motorActive1 = false;
  motor2Activo = false;
  motor3Activo = false;
}

void volverAInicio() {
  // Motor 3 abajo
  digitalWrite(DIR3, LOW);
  motor3Activo = true;
  while (digitalRead(LIMIT_BOTTOM3) == LOW && !escaneoActivo) {
    manejarPausa();
    delay(1);
    server.handleClient();
  }
  motor3Activo = false;
  delay(200);

  // Motor 2 izquierda
  digitalWrite(DIR2, HIGH);
  motor2Activo = true;
  while (digitalRead(LIMIT_LEFT2) == LOW && !escaneoActivo) {
    manejarPausa();
    delay(1);
    server.handleClient();
  }
  motor2Activo = false;

  enPosInicial = true;
  resetEstados();
}

void ejecutarEscaneo() {
  while (escaneoActivo && !finCarreraTopDetectado && !cancelarEscaneo) {
    // Derecha
    digitalWrite(DIR2, LOW);
    motor2Activo = true;
    while (digitalRead(LIMIT_RIGHT2) == LOW && escaneoActivo && !cancelarEscaneo) {
      manejarPausa();
      delay(1);
      server.handleClient();
    }
    motor2Activo = false;
    delay(200);
    subirMotor3PorTiempo();

    if (cancelarEscaneo || finCarreraTopDetectado) break;

    // Izquierda
    digitalWrite(DIR2, HIGH);
    motor2Activo = true;
    while (digitalRead(LIMIT_LEFT2) == LOW && escaneoActivo && !cancelarEscaneo) {
      manejarPausa();
      delay(1);
      server.handleClient();
    }
    motor2Activo = false;
    delay(200);
    subirMotor3PorTiempo();
  }

  if (finCarreraTopDetectado && !cancelarEscaneo) {
    delay(2000);
    digitalWrite(DIR2, LOW);
    motor2Activo = true;
    while (digitalRead(LIMIT_RIGHT2) == LOW && !cancelarEscaneo) {
      manejarPausa();
      delay(1);
      server.handleClient();
    }
    motor2Activo = false;
    resetEstados();
    volverAInicio();
  }
}

// ================== HTML Combinado ==================
const char* html = R"rawliteral(
<!DOCTYPE html><html><head><title>Escaneo</title>
<meta name="viewport" content="width=device-width, initial-scale=1">
<style>
body { font-family: Arial; padding: 20px; }
button, input { font-size: 1.2em; margin: 5px; padding: 10px; }
#contador { font-size: 24px; margin: 20px 0; }
#pausaBtn { background-color: #FFC107; color: black; }
</style></head><body>

<h1>Sistema de Escaneo</h1>

<div style="border:1px solid #ccc; padding:20px; margin-bottom:20px;">
  <h2>Ancho de objeto (Motor 1)</h2>
  <button onmousedown="fetch('/startRight1')" onmouseup="fetch('/stop1')">Derecha</button>
  <button onmousedown="fetch('/startLeft1')" onmouseup="fetch('/stop1')">Izquierda</button>
</div>

<div style="border:1px solid #ccc; padding:20px; margin-bottom:20px;">
  <h2>Secuencia de Escaneo</h2>
  <button onclick="fetch('/startScan')">Iniciar Escaneo</button>
  <button id="pausaBtn" onclick="togglePausa()">Pausar</button>
  <button onclick="fetch('/cancelScan')">Cancelar</button>
  <button onclick="fetch('/emergencia')" style="background-color:red;color:white;">Parada Emergencia</button>
  <div id="contador">Subidas: 0</div>
</div>

<div style="border:1px solid #ccc; padding:20px;">
  <h2>Inclinación de cámara</h2>
  <label>Ángulo (0° a 90°): <span id="stepVal">45</span></label><br>
  <input type="range" min="0" max="90" value="45" id="stepsSlider" onchange="handleSlider()"><br>
  <div>Valor Potenciómetro: <span id="potValue">---</span></div>
</div>

<script>
let pausado = false;

function togglePausa() {
  fetch('/pausa').then(() => {
    pausado = !pausado;
    document.getElementById('pausaBtn').innerText = pausado ? 'Reanudar' : 'Pausar';
    document.getElementById('pausaBtn').style.backgroundColor = pausado ? '#4CAF50' : '#FFC107';
  });
}

function handleSlider() {
  const angle = document.getElementById("stepsSlider").value;
  fetch("/set?angle=" + angle);
}

setInterval(() => {
  // Actualizar contador
  fetch('/contador').then(r => r.text()).then(t => {
    document.getElementById('contador').innerText = "Subidas: " + t;
  });
  
  // Actualizar ángulo
  fetch("/get").then(res => res.json()).then(data => {
    document.getElementById("stepVal").innerText = data.angle;
    document.getElementById("stepsSlider").value = data.angle;
  });
  
  // Actualizar potenciómetro
  fetch("/pot").then(res => res.text()).then(val => {
    document.getElementById("potValue").innerText = val;
  });
}, 500);
</script></body></html>
)rawliteral";

// ================== SETUP ==================
void setup() {
  Serial.begin(115200);
  setupPines();
  
  // Configurar tickers
  ticker1.attach_us(30, togglePulse1);  // Motor 1
  ticker2.attach_us(1050, togglePulse2); // Motor 2
  ticker3.attach_us(100, togglePulse3);  // Motor 3

  // Configurar preferencias Motor 4
  prefs.begin("motor4", false);
  currentPosition4 = prefs.getInt("pos", 0);

  // Iniciar WiFi
  WiFi.softAP(ssid, password);
  Serial.println("IP: " + WiFi.softAPIP().toString());
  
  // Calibrar Motor 4
  buscarPosicionValidaAlInicio();
  
  // Calibrar Motores 2 y 3
  buscarPosInicial();

  // ======= Configurar rutas =======
  // Motor 1
  server.on("/startRight1", []() { 
    motorActive1 = true; 
    movingRight1 = true; 
    digitalWrite(DIR1, LOW); 
    server.send(200, "text/plain", "Right"); 
  });
  
  server.on("/startLeft1",  []() { 
    motorActive1 = true; 
    movingRight1 = false; 
    digitalWrite(DIR1, HIGH); 
    server.send(200, "text/plain", "Left"); 
  });
  
  server.on("/stop1", []() { 
    motorActive1 = false; 
    server.send(200, "text/plain", "Stopped"); 
  });

  // Escaneo
  server.on("/startScan", []() {
    detenerEmergencia = false;
    if (!escaneoActivo) {
      cancelarEscaneo = false;
      pausa = false;
      contadorSubidas = 0;
      volverAInicio();
      escaneoActivo = true;
      server.send(200, "text/plain", "Escaneo iniciado");
    }
  });
  
  server.on("/cancelScan", []() {
    cancelarEscaneo = true;
    escaneoActivo = false;
    pausa = false;
    motor2Activo = false;
    motor3Activo = false;
    delay(200);
    volverAInicio();
    server.send(200, "text/plain", "Escaneo cancelado");
  });
  
  server.on("/emergencia", []() {
    detenerTodosLosMotores();
    server.send(200, "text/plain", "EMERGENCIA: Motores detenidos");
  });
  
  server.on("/pausa", []() {
    pausa = !pausa;
    server.send(200, "text/plain", pausa ? "PAUSADO" : "REANUDADO");
  });
  
  server.on("/contador", []() {
    server.send(200, "text/plain", String(contadorSubidas));
  });

  // Motor 4
  server.on("/", []() { server.send(200, "text/html", html); });
  
  server.on("/get", []() {
    int angle = getAngleFromPosition(currentPosition4);
    server.send(200, "application/json", "{\"angle\":" + String(angle) + "}");
  });
  
  server.on("/set", []() {
    if (!server.hasArg("angle")) return;
    int angle = constrain(server.arg("angle").toInt(), 0, 90);
    int target = map(angle, 90, 0, 0, maxPosition4);
    if (target == currentPosition4 || running4 || motorBusy4) return;
    direction4 = (target > currentPosition4) ? 1 : 0;
    stepsToMove4 = abs(target - currentPosition4);
    running4 = true;
    server.send(200, "text/plain", "OK");
  });
  
  server.on("/pot", []() {
    server.send(200, "text/plain", String(getPotReading()));
  });

  server.begin();
}

// ================== LOOP ==================
void loop() {
  server.handleClient();

  // Control Motor 1
  if (motorActive1) {
    if (movingRight1 && digitalRead(LIMIT_RIGHT1) == HIGH) {
      motorActive1 = false;
    } else if (!movingRight1 && digitalRead(LIMIT_LEFT1) == HIGH) {
      motorActive1 = false;
    }
  }

  // Control Escaneo
  if (escaneoActivo && !detenerEmergencia) {
    ejecutarEscaneo();
  }

  // Control Motor 4
  if (running4 && !motorBusy4 && !detenerEmergencia) {
    motorBusy4 = true;
    digitalWrite(DIR4, direction4 ? LOW : HIGH);
    
    for (int i = 0; i < stepsToMove4; i++) {
      if (detenerEmergencia) break;
      digitalWrite(PUL4, HIGH); 
      delayMicroseconds(speedMicro4);
      digitalWrite(PUL4, LOW);
      delayMicroseconds(speedMicro4);
    }
    
    if (!detenerEmergencia) {
      currentPosition4 += (direction4 ? stepsToMove4 : -stepsToMove4);
      currentPosition4 = constrain(currentPosition4, 0, maxPosition4);
      prefs.putInt("pos", currentPosition4);
    }
    
    stepsToMove4 = 0;
    running4 = false;
    motorBusy4 = false;
  }
}