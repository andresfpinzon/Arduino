#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <Ticker.h>

// Configuración de red WiFi
const char* ssid = "Escaner";
const char* password = "12345678";

// Definición de pines para los motores
#define PUL1 23      // Pulso motor 1 (horizontal)
#define DIR1 27      // Dirección motor 1
#define LIMIT_LEFT1 18  // Límite izquierdo motor 1
#define LIMIT_RIGHT1 13 // Límite derecho motor 1

#define PUL2 26      // Pulso motor 2 (rotación)
#define DIR2 25      // Dirección motor 2
#define LIMIT_LEFT2 14  // Límite izquierdo motor 2
#define LIMIT_RIGHT2 12 // Límite derecho motor 2

#define PUL3 33      // Pulso motor 3 (vertical)
#define DIR3 32      // Dirección motor 3
#define LIMIT_BOTTOM3 19 // Límite inferior motor 3
#define LIMIT_TOP3 5     // Límite superior motor 3

#define PUL4 21      // Pulso motor 4 (inclinación cámara)
#define DIR4 22      // Dirección motor 4
#define POT_PIN 39   // Pin del potenciómetro

// Variables globales y objetos
Preferences prefs;          // Para almacenamiento persistente
WebServer server(80);       // Servidor web en puerto 80
Ticker motorTicker1, motorTicker2, motorTicker3; // Temporizadores para pulsos

// ================== Variables Motor 1 (Horizontal) ==================
volatile bool motorActive1 = false;     // Estado de activación
volatile bool movingRight1 = true;      // Dirección actual
volatile bool limitRightPressed1 = false; // Bandera límite derecho
volatile bool limitLeftPressed1 = false;  // Bandera límite izquierdo

// ================== Variables Motor 4 (Inclinación cámara) ==================
volatile int stepsToMove4 = 0;
volatile int direction4 = 1;
volatile bool running4 = false;
volatile bool motorBusy4 = false;
int currentPosition4 = 0;
const int maxPosition4 = 50;            // Máximo de pasos posibles
const int speedMicro4 = 800;            // Microsegundos entre pasos (velocidad)
const int POT_MIN = 1400;               // Valor mínimo del potenciómetro
const int POT_MAX = 2000;               // Valor máximo del potenciómetro

// ================== Variables Escaneo (Motores 2 y 3) ==================
volatile bool escaneoActivo = false;    // Estado del escaneo
volatile bool cancelarEscaneo = false;  // Solicitud de cancelación
volatile bool detenerEmergencia = false; // Parada de emergencia
volatile bool pausa = false;            // Estado de pausa
bool enPosInicial = false;              // Bandera posición inicial
bool finCarreraTopDetectado = false;    // Límite superior alcanzado
int contadorSubidas = 0;                // Contador de subidas completadas
volatile bool motor2Activo = false;     // Estado motor 2
volatile bool motor3Activo = false;     // Estado motor 3
bool motor2EnInicio = false;            // Motor 2 en posición inicial
bool motor3EnInicio = false;            // Motor 3 en posición inicial

// ================== Funciones de generación de pulsos ==================
// Función de interrupción para motor 1 (alta prioridad)
void IRAM_ATTR togglePulse1() {
  if (!motorActive1) {
    digitalWrite(PUL1, LOW);
    return;
  }
  
  // Detener motor si alcanza límite
  if ((movingRight1 && limitRightPressed1) || (!movingRight1 && limitLeftPressed1)) {
    motorActive1 = false;
    limitRightPressed1 = false;
    limitLeftPressed1 = false;
    digitalWrite(PUL1, LOW);
    return;
  }
  
  // Generar pulso
  digitalWrite(PUL1, !digitalRead(PUL1));
}

// Función de interrupción para motor 2
void togglePulse2() {
  digitalWrite(PUL2, motor2Activo ? !digitalRead(PUL2) : LOW);
}

// Función de interrupción para motor 3
void togglePulse3() {
  digitalWrite(PUL3, motor3Activo ? !digitalRead(PUL3) : LOW);
}

// ================== Configuración inicial de pines ==================
void setupPines() {
  // Motor 1 (horizontal)
  pinMode(PUL1, OUTPUT);
  pinMode(DIR1, OUTPUT);
  pinMode(LIMIT_LEFT1, INPUT);
  pinMode(LIMIT_RIGHT1, INPUT);

  // Motor 2 (rotación)
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

  // Configurar interrupciones para límites motor 1
  attachInterrupt(digitalPinToInterrupt(LIMIT_RIGHT1), []() {
    limitRightPressed1 = true;
  }, RISING);
  
  attachInterrupt(digitalPinToInterrupt(LIMIT_LEFT1), []() {
    limitLeftPressed1 = true;
  }, RISING);
}

// ================== Funciones Motor 4 (Inclinación cámara) ==================
// Convertir posición a ángulo (0-90°)
int getAngleFromPosition(int pos) {
  return map(pos, 0, maxPosition4, 90, 0); // 0 pasos = 90°, maxPosition4 pasos = 0°
}

// Leer valor del potenciómetro
int getPotReading() {
  return analogRead(POT_PIN);
}

// Buscar posición válida al iniciar (calibración)
void buscarPosicionValidaAlInicio() {
  int pot = getPotReading();
  if (pot >= POT_MIN && pot <= POT_MAX) return;

  const int maxIntentos = 150;
  bool direccionSubir = (pot < POT_MIN);
  digitalWrite(DIR4, direccionSubir ? HIGH : LOW);

  for (int i = 0; i < maxIntentos; i++) {
    digitalWrite(PUL4, HIGH);
    delayMicroseconds(speedMicro4);
    digitalWrite(PUL4, LOW);
    delayMicroseconds(speedMicro4);
    
    currentPosition4 += (direccionSubir ? -1 : 1);
    currentPosition4 = constrain(currentPosition4, 0, maxPosition4);
    
    pot = getPotReading();
    if (pot >= POT_MIN && pot <= POT_MAX) break;
    if (currentPosition4 == 0 || currentPosition4 == maxPosition4) break;
  }
  prefs.putInt("pos", currentPosition4);
}

// ================== Funciones de Escaneo ==================
// Reiniciar estados del escaneo
void resetEstados() {
  escaneoActivo = false;
  cancelarEscaneo = false;
  motor2Activo = false;
  motor3Activo = false;
  finCarreraTopDetectado = false;
  pausa = false;
}

// Manejar estado de pausa
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

// Buscar posición inicial de los motores 2 y 3
void buscarPosInicial() {
  // Motor 2 a izquierda (límite físico)
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

  // Motor 3 abajo (límite físico)
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

// Subir motor 3 por un tiempo determinado (1 segundo)
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

// Detener todos los motores (emergencia)
void detenerTodosLosMotores() {
  detenerEmergencia = true;
  escaneoActivo = false;
  pausa = false;
  motorActive1 = false;
  motor2Activo = false;
  motor3Activo = false;
  running4 = false;
}

// Volver a posición inicial después del escaneo
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

// Máquina de estados para el escaneo
void ejecutarEscaneo() {
  static enum { MOVIENDO_DERECHA, SUBIENDO, MOVIENDO_IZQUIERDA, COMPROBANDO } estado = MOVIENDO_DERECHA;
  
  while (escaneoActivo && !finCarreraTopDetectado && !cancelarEscaneo) {
    switch(estado) {
      case MOVIENDO_DERECHA:
        if (!motor2Activo) {
          digitalWrite(DIR2, LOW);
          motor2Activo = true;
        }
        if (digitalRead(LIMIT_RIGHT2) == HIGH) {
          motor2Activo = false;
          estado = SUBIENDO;
        }
        break;
        
      case SUBIENDO:
        subirMotor3PorTiempo();
        estado = MOVIENDO_IZQUIERDA;
        break;
        
      case MOVIENDO_IZQUIERDA:
        if (!motor2Activo) {
          digitalWrite(DIR2, HIGH);
          motor2Activo = true;
        }
        if (digitalRead(LIMIT_LEFT2) == HIGH) {
          motor2Activo = false;
          estado = COMPROBANDO;
        }
        break;
        
      case COMPROBANDO:
        subirMotor3PorTiempo();
        estado = MOVIENDO_DERECHA;
        break;
    }
    
    manejarPausa();
    server.handleClient();
    delay(1);  // Permitir procesamiento
  }

  // Finalización del escaneo
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

// ================== Interfaz Web ==================
const char* html = R"rawliteral(
<!DOCTYPE html><html><head><title>Escaneo</title>
<meta name="viewport" content="width=device-width, initial-scale=1">
<style>
body { font-family: Arial; padding: 20px; }
button, input { font-size: 1.2em; margin: 5px; padding: 10px; }
#contador { font-size: 24px; margin: 20px 0; }
#pausaBtn { background-color: #FFC107; color: black; }
.emergencia { background-color: red; color: white; }
.seccion { border:1px solid #ccc; padding:20px; margin-bottom:20px; }
</style></head><body>

<h1>Sistema de Escaneo 3D</h1>

<div class="seccion">
  <h2>Ancho de objeto (Motor 1)</h2>
  <button onmousedown="fetch('/startRight1')" onmouseup="fetch('/stop1')" ontouchstart="fetch('/startRight1')" ontouchend="fetch('/stop1')">Derecha</button>
  <button onmousedown="fetch('/startLeft1')" onmouseup="fetch('/stop1')" ontouchstart="fetch('/startLeft1')" ontouchend="fetch('/stop1')">Izquierda</button>
</div>

<div class="seccion">
  <h2>Secuencia de Escaneo Automatico</h2>
  <button onclick="fetch('/startScan')">Iniciar Escaneo</button>
  <button id="pausaBtn" onclick="togglePausa()">Pausar</button>
  <button onclick="fetch('/cancelScan')">Cancelar</button>
  <button onclick="fetch('/emergencia')" class="emergencia">Parada Emergencia</button>
  <div id="contador">Subidas completadas: 0</div>
</div>

<div class="seccion">
  <h2>Control de Camara</h2>
  <label>Angulo de inclinacion (0° a 90°): <span id="stepVal">45</span>°</label><br>
  <input type="range" min="0" max="90" value="45" id="stepsSlider" onchange="handleSlider()"><br>
  <div>Lectura del potenciometro: <span id="potValue">---</span></div>
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

// Actualizacion periodica de la interfaz
setInterval(() => {
  // Contador de subidas
  fetch('/contador').then(r => r.text()).then(t => {
    document.getElementById('contador').innerText = "Subidas completadas: " + t;
  });
  
  // Angulo actual
  fetch("/get").then(res => res.json()).then(data => {
    document.getElementById("stepVal").innerText = data.angle;
    if (!document.getElementById("stepsSlider").matches(':focus')) {
      document.getElementById("stepsSlider").value = data.angle;
    }
  });
  
  // Valor del potenciometro
  fetch("/pot").then(res => res.text()).then(val => {
    document.getElementById("potValue").innerText = val;
  });
}, 500);
</script></body></html>
)rawliteral";

// ================== Configuración Inicial ==================
void setup() {
  Serial.begin(115200);
  delay(1000);
  
  // Inicializar pines e interrupciones
  setupPines();

  // Configurar temporizadores para pulsos de motores
  motorTicker1.attach_us(30, togglePulse1);    // Motor 1 - Alta velocidad
  motorTicker2.attach_us(1050, togglePulse2);  // Motor 2 - Baja velocidad
  motorTicker3.attach_us(100, togglePulse3);   // Motor 3 - Velocidad media

  // Configurar almacenamiento persistente para motor 4
  prefs.begin("motor4", false);
  currentPosition4 = prefs.getInt("pos", 0);

  // Iniciar red WiFi
  WiFi.softAP(ssid, password);
  Serial.println("IP del sistema: " + WiFi.softAPIP().toString());

  // Calibración inicial de motores
  buscarPosicionValidaAlInicio(); // Motor 4
  buscarPosInicial();             // Motores 2 y 3

  // ================== Configuración de rutas web ==================
  // Motor 1 - Movimiento horizontal
  server.on("/startRight1", []() {
    motorActive1 = true;
    movingRight1 = true;
    digitalWrite(DIR1, LOW);
    server.send(200, "text/plain", "Movimiento a derecha iniciado");
  });

  server.on("/startLeft1", []() {
    motorActive1 = true;
    movingRight1 = false;
    digitalWrite(DIR1, HIGH);
    server.send(200, "text/plain", "Movimiento a izquierda iniciado");
  });

  server.on("/stop1", []() {
    motorActive1 = false;
    server.send(200, "text/plain", "Motor 1 detenido");
  });

  // Control del escaneo automático
  server.on("/startScan", []() {
    if (!escaneoActivo) {
      detenerEmergencia = false;
      cancelarEscaneo = false;
      pausa = false;
      contadorSubidas = 0;
      volverAInicio();
      escaneoActivo = true;
      server.send(200, "text/plain", "Escaneo automatico iniciado");
    } else {
      server.send(200, "text/plain", "Escaneo ya esta en progreso");
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
    server.send(200, "text/plain", "PARADA DE EMERGENCIA: Todos los motores detenidos");
  });

  server.on("/pausa", []() {
    pausa = !pausa;
    server.send(200, "text/plain", pausa ? "Escaneo en pausa" : "Escaneo reanudado");
  });

  server.on("/contador", []() {
    server.send(200, "text/plain", String(contadorSubidas));
  });

  // Motor 4 - Control de inclinación
  server.on("/", []() {
    server.send(200, "text/html", html);
  });

  server.on("/get", []() {
    int angle = getAngleFromPosition(currentPosition4);
    server.send(200, "application/json", "{\"angle\":" + String(angle) + "}");
  });

  server.on("/set", []() {
    if (!server.hasArg("angle")) {
      server.send(400, "text/plain", "Falta parametro 'angle'");
      return;
    }
    
    int angle = constrain(server.arg("angle").toInt(), 0, 90);
    int target = map(angle, 90, 0, 0, maxPosition4);
    
    if (target != currentPosition4 && !running4 && !motorBusy4) {
      direction4 = (target > currentPosition4) ? 1 : 0;
      stepsToMove4 = abs(target - currentPosition4);
      running4 = true;
      digitalWrite(DIR4, direction4 ? LOW : HIGH);
    }
    
    server.send(200, "text/plain", "OK");
  });

  server.on("/pot", []() {
    server.send(200, "text/plain", String(getPotReading()));
  });

  // Iniciar servidor web
  server.begin();
  Serial.println("Servidor web iniciado");
}

// ================== Bucle Principal ==================
void loop() {
  // Manejar clientes web
  server.handleClient();

  // Control Motor 4
  if (running4 && !motorBusy4 && !detenerEmergencia) {
    motorBusy4 = true;
    
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

  // Ejecutar escaneo si está activo
  if (escaneoActivo && !detenerEmergencia) {
    ejecutarEscaneo();
  }
}