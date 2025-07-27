#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <Ticker.h>

// ================== CONFIGURACIÓN DE RED ==================
const char* ssid = "Escaner";       // Nombre del punto de acceso WiFi
const char* password = "12345678";  // Contraseña del WiFi

// ================== DEFINICIÓN DE PINES ==================
// Motor 1 (Horizontal)
#define PUL1 23       // Pin de pulso (STEP)
#define DIR1 27       // Pin de dirección
#define LIMIT_LEFT1 18  // Fin de carrera izquierdo
#define LIMIT_RIGHT1 13 // Fin de carrera derecho

// Motor 2 (Rotación)
#define PUL2 26       // Pin de pulso (STEP)
#define DIR2 25       // Pin de dirección
#define LIMIT_LEFT2 14  // Fin de carrera izquierdo
#define LIMIT_RIGHT2 12 // Fin de carrera derecho

// Motor 3 (Vertical)
#define PUL3 33       // Pin de pulso (STEP)
#define DIR3 32       // Pin de dirección
#define LIMIT_BOTTOM3 19 // Fin de carrera inferior
#define LIMIT_TOP3 5     // Fin de carrera superior

// Motor 4 (Inclinación de cámara)
#define PUL4 21       // Pin de pulso (STEP)
#define DIR4 22       // Pin de dirección
#define POT_PIN 39    // Pin del potenciómetro (ADC)

// ================== VARIABLES GLOBALES ==================
Preferences prefs;          // Almacenamiento no volátil (EEPROM)
WebServer server(80);       // Servidor web en puerto 80
Ticker motorTicker1, motorTicker2, motorTicker3, motorTicker4; // Temporizadores para pulsos

// ================== VARIABLES MOTOR 1 (HORIZONTAL) ==================
volatile bool motorActive1 = false;     // Indica si el motor está activo
volatile bool movingRight1 = true;      // Dirección actual (true = derecha)
volatile bool limitRightPressed1 = false; // Bandera de límite derecho alcanzado
volatile bool limitLeftPressed1 = false;  // Bandera de límite izquierdo alcanzado

// ================== VARIABLES MOTOR 4 (INCLINACIÓN CÁMARA) ==================
volatile bool motorActive4 = false;     // Indica si el motor está activo
volatile bool movingUp4 = true;         // Dirección actual (true = subir, false = bajar)
const int POT_MIN = 1400;               // Valor mínimo del potenciómetro (0°)
const int POT_MAX = 2000;               // Valor máximo del potenciómetro (90°)
const int speedMicro4 = 800;            // Velocidad del motor (microsegundos entre pasos)

// ================== VARIABLES ESCANEO AUTOMÁTICO ==================
volatile bool escaneoActivo = false;    // Indica si el escaneo está en curso
volatile bool cancelarEscaneo = false;  // Solicitud de cancelación
volatile bool detenerEmergencia = false; // Parada de emergencia activa
volatile bool pausa = false;            // Escaneo en pausa
bool enPosInicial = false;              // Indica si los motores están en posición inicial
bool finCarreraTopDetectado = false;    // Límite superior alcanzado
int contadorSubidas = 0;                // Cuenta las subidas completadas
volatile bool motor2Activo = false;     // Estado del motor 2 (rotación)
volatile bool motor3Activo = false;     // Estado del motor 3 (vertical)
bool motor2EnInicio = false;            // Motor 2 en posición inicial
bool motor3EnInicio = false;            // Motor 3 en posición inicial

// ================== FUNCIONES DE INTERRUPCIÓN PARA PULSOS ==================
// Motor 1 (Horizontal)
void IRAM_ATTR togglePulse1() {
  if (!motorActive1) {
    digitalWrite(PUL1, LOW);
    return;
  }
  
  // Detiene el motor si alcanza un límite
  if ((movingRight1 && limitRightPressed1) || (!movingRight1 && limitLeftPressed1)) {
    motorActive1 = false;
    limitRightPressed1 = false;
    limitLeftPressed1 = false;
    digitalWrite(PUL1, LOW);
    return;
  }
  
  // Genera el pulso (alterna HIGH/LOW)
  digitalWrite(PUL1, !digitalRead(PUL1));
}

// Motor 4 (Inclinación cámara)
void IRAM_ATTR togglePulse4() {
  if (!motorActive4) {
    digitalWrite(PUL4, LOW);
    return;
  }
  
  // Lee el potenciómetro y verifica límites
  int potValue = analogRead(POT_PIN);
  if ((movingUp4 && potValue >= POT_MAX) || (!movingUp4 && potValue <= POT_MIN)) {
    motorActive4 = false;
    digitalWrite(PUL4, LOW);
    return;
  }
  
  // Genera el pulso (alterna HIGH/LOW)
  digitalWrite(PUL4, !digitalRead(PUL4));
}

// Motor 2 (Rotación)
void togglePulse2() {
  digitalWrite(PUL2, motor2Activo ? !digitalRead(PUL2) : LOW);
}

// Motor 3 (Vertical)
void togglePulse3() {
  digitalWrite(PUL3, motor3Activo ? !digitalRead(PUL3) : LOW);
}

// ================== CONFIGURACIÓN INICIAL DE PINES ==================
void setupPines() {
  // Motor 1 (Horizontal)
  pinMode(PUL1, OUTPUT);
  pinMode(DIR1, OUTPUT);
  pinMode(LIMIT_LEFT1, INPUT);
  pinMode(LIMIT_RIGHT1, INPUT);

  // Motor 2 (Rotación)
  pinMode(PUL2, OUTPUT);
  pinMode(DIR2, OUTPUT);
  pinMode(LIMIT_LEFT2, INPUT);
  pinMode(LIMIT_RIGHT2, INPUT);

  // Motor 3 (Vertical)
  pinMode(PUL3, OUTPUT);
  pinMode(DIR3, OUTPUT);
  pinMode(LIMIT_BOTTOM3, INPUT);
  pinMode(LIMIT_TOP3, INPUT);

  // Motor 4 (Inclinación cámara)
  pinMode(PUL4, OUTPUT);
  pinMode(DIR4, OUTPUT);
  pinMode(POT_PIN, INPUT);

  // Configura interrupciones para los límites del motor 1
  attachInterrupt(digitalPinToInterrupt(LIMIT_RIGHT1), []() {
    limitRightPressed1 = true;
  }, RISING);
  
  attachInterrupt(digitalPinToInterrupt(LIMIT_LEFT1), []() {
    limitLeftPressed1 = true;
  }, RISING);
}

// ================== FUNCIONES PARA EL MOTOR 4 (CÁMARA) ==================
// Obtiene el ángulo actual basado en el potenciómetro
int getCurrentAngle() {
  int pot = analogRead(POT_PIN);
  return map(pot, POT_MIN, POT_MAX, 0, 90); // Mapea a 0°-90°
}

// Verifica si el potenciómetro está en rango válido
bool potInRange() {
  int pot = analogRead(POT_PIN);
  return (pot >= POT_MIN && pot <= POT_MAX);
}

// ================== FUNCIONES PARA EL ESCANEO AUTOMÁTICO ==================
// Reinicia los estados del escaneo
void resetEstados() {
  escaneoActivo = false;
  cancelarEscaneo = false;
  motor2Activo = false;
  motor3Activo = false;
  finCarreraTopDetectado = false;
  pausa = false;
}

// Maneja la pausa durante el escaneo
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

// Mueve los motores 2 y 3 a su posición inicial
void buscarPosInicial() {
  // Motor 2 hacia la izquierda (límite físico)
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

  // Motor 3 hacia abajo (límite físico)
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

// Sube el motor 3 durante 1 segundo (o hasta tope)
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

// Detiene todos los motores (emergencia)
void detenerTodosLosMotores() {
  detenerEmergencia = true;
  escaneoActivo = false;
  pausa = false;
  motorActive1 = false;
  motor2Activo = false;
  motor3Activo = false;
  motorActive4 = false;
}

// Vuelve a la posición inicial después del escaneo
void volverAInicio() {
  // Motor 3 hacia abajo
  digitalWrite(DIR3, LOW);
  motor3Activo = true;
  while (digitalRead(LIMIT_BOTTOM3) == LOW && !escaneoActivo) {
    manejarPausa();
    delay(1);
    server.handleClient();
  }
  motor3Activo = false;
  delay(200);

  // Motor 2 hacia la izquierda
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

// Ejecuta el escaneo automático
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
    delay(1);
  }

  // Finaliza el escaneo si se alcanza el tope superior
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

// ================== INTERFAZ WEB (HTML + JavaScript) ==================
const char* html = R"rawliteral(
<!DOCTYPE html><html><head><title>Escaneo 3D</title>
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
  <h2>Secuencia de Escaneo Automático</h2>
  <button onclick="fetch('/startScan')">Iniciar Escaneo</button>
  <button id="pausaBtn" onclick="togglePausa()">Pausar</button>
  <button onclick="fetch('/cancelScan')">Cancelar</button>
  <button onclick="fetch('/emergencia')" class="emergencia">Parada Emergencia</button>
  <div id="contador">Subidas completadas: 0</div>
</div>

<div class="seccion">
  <h2>Control de Cámara (Motor 4)</h2>
  <button onmousedown="fetch('/startUp4')" onmouseup="fetch('/stop4')" ontouchstart="fetch('/startUp4')" ontouchend="fetch('/stop4')">Subir Cámara</button>
  <button onmousedown="fetch('/startDown4')" onmouseup="fetch('/stop4')" ontouchstart="fetch('/startDown4')" ontouchend="fetch('/stop4')">Bajar Cámara</button>
  <div>Lectura del potenciómetro: <span id="potValue">---</span></div>
  <div>Ángulo actual: <span id="currentAngle">---</span>°</div>
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

// Actualiza la interfaz cada 500ms
setInterval(() => {
  // Contador de subidas
  fetch('/contador').then(r => r.text()).then(t => {
    document.getElementById('contador').innerText = "Subidas completadas: " + t;
  });
  
  // Ángulo y potenciómetro
  fetch("/currentAngle").then(res => res.text()).then(angle => {
    document.getElementById("currentAngle").innerText = angle;
  });
  
  fetch("/pot").then(res => res.text()).then(val => {
    document.getElementById("potValue").innerText = val;
  });
}, 500);
</script></body></html>
)rawliteral";

// ================== CONFIGURACIÓN INICIAL (SETUP) ==================
void setup() {
  Serial.begin(115200);
  delay(1000);
  
  // Configura pines e interrupciones
  setupPines();

  // Configura temporizadores para generación de pulsos
  motorTicker1.attach_us(30, togglePulse1);    // Motor 1 - Alta velocidad
  motorTicker2.attach_us(1050, togglePulse2);  // Motor 2 - Baja velocidad
  motorTicker3.attach_us(100, togglePulse3);   // Motor 3 - Velocidad media
  motorTicker4.attach_us(speedMicro4, togglePulse4); // Motor 4 - Velocidad configurable

  // Inicia WiFi en modo AP
  WiFi.softAP(ssid, password);
  Serial.println("IP del sistema: " + WiFi.softAPIP().toString());

  // Mueve motores a posición inicial
  buscarPosInicial();

  // ================== RUTAS DEL SERVIDOR WEB ==================
  // Motor 1 (Horizontal)
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

  // Motor 4 (Inclinación cámara)
  server.on("/startUp4", []() {
    if (analogRead(POT_PIN) < POT_MAX) {
      motorActive4 = true;
      movingUp4 = true;
      digitalWrite(DIR4, HIGH);
      server.send(200, "text/plain", "Subiendo cámara");
    } else {
      server.send(200, "text/plain", "Límite superior alcanzado");
    }
  });

  server.on("/startDown4", []() {
    if (analogRead(POT_PIN) > POT_MIN) {
      motorActive4 = true;
      movingUp4 = false;
      digitalWrite(DIR4, LOW);
      server.send(200, "text/plain", "Bajando cámara");
    } else {
      server.send(200, "text/plain", "Límite inferior alcanzado");
    }
  });

  server.on("/stop4", []() {
    motorActive4 = false;
    server.send(200, "text/plain", "Cámara detenida");
  });

  // Escaneo automático
  server.on("/startScan", []() {
    if (!escaneoActivo) {
      detenerEmergencia = false;
      cancelarEscaneo = false;
      pausa = false;
      contadorSubidas = 0;
      volverAInicio();
      escaneoActivo = true;
      server.send(200, "text/plain", "Escaneo automático iniciado");
    } else {
      server.send(200, "text/plain", "Escaneo ya está en progreso");
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

  // Obtener datos del potenciómetro y ángulo
  server.on("/pot", []() {
    server.send(200, "text/plain", String(analogRead(POT_PIN)));
  });

  server.on("/currentAngle", []() {
    server.send(200, "text/plain", String(getCurrentAngle()));
  });

  // Página principal
  server.on("/", []() {
    server.send(200, "text/html", html);
  });

  // Inicia el servidor web
  server.begin();
  Serial.println("Servidor web iniciado");
}

// ================== BUCLE PRINCIPAL (LOOP) ==================
void loop() {
  server.handleClient(); // Maneja peticiones web

  // Ejecuta el escaneo si está activo
  if (escaneoActivo && !detenerEmergencia) {
    ejecutarEscaneo();
  }
}