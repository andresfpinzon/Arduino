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

// Estado general
volatile bool escaneoActivo = false;
volatile bool cancelarEscaneo = false;
bool enPosInicial = false;
bool finCarreraTopDetectado = false;
volatile bool detenerEmergencia = false;
volatile bool pausa = false; // Nuevo estado para control de pausa

// Contador de subidas verticales
int contadorSubidas = 0;

// Ticker para generar pulsos
Ticker ticker2, ticker3;

// Control de motores
bool motor2Activo = false;
bool motor3Activo = false;
bool motor2EnInicio = false;
bool motor3EnInicio = false;

// HTML para la interfaz (actualizado con botón de pausa)
const char* html = R"rawliteral(
<!DOCTYPE html><html><head><title>Escaneo</title>
<style>
body { font-family: Arial; padding: 20px; }
button { font-size: 20px; padding: 10px 20px; margin: 10px; }
#contador { font-size: 24px; margin-top: 20px; }
#pausaBtn { background-color: #FFC107; color: black; }
</style>
</head><body>
<h2>Secuencia de Escaneo</h2>
<button onclick="fetch('/start')">Iniciar</button>
<button id="pausaBtn" onclick="togglePausa()">Pausar</button>
<button onclick="fetch('/cancel')">Cancelar</button>
<button onclick="fetch('/emergencia')" style="background-color:red; color:white; padding:10px; margin-top:10px;"> Parada de emergencia
</button>
<div id="contador">Subidas: 0</div>
<script>
let pausado = false;

function togglePausa() {
  fetch('/pausa').then(() => {
    pausado = !pausado;
    document.getElementById('pausaBtn').innerText = pausado ? 'Reanudar' : 'Pausar';
    document.getElementById('pausaBtn').style.backgroundColor = pausado ? '#4CAF50' : '#FFC107';
  });
}

setInterval(() => {
  fetch('/contador').then(r => r.text()).then(t => {
    document.getElementById('contador').innerText = "Subidas: " + t;
  });
}, 500);
</script>
</body></html>
)rawliteral";

// Generadores de pulsos
void togglePulse2() {
  digitalWrite(PUL2, motor2Activo ? !digitalRead(PUL2) : LOW);
}

void togglePulse3() {
  digitalWrite(PUL3, motor3Activo ? !digitalRead(PUL3) : LOW);
}

// Inicializar pines
void setupMotores() {
  pinMode(PUL2, OUTPUT);
  pinMode(DIR2, OUTPUT);
  pinMode(LIMIT_LEFT2, INPUT);
  pinMode(LIMIT_RIGHT2, INPUT);

  pinMode(PUL3, OUTPUT);
  pinMode(DIR3, OUTPUT);
  pinMode(LIMIT_BOTTOM3, INPUT);
  pinMode(LIMIT_TOP3, INPUT);
}

// Reset de estados
void resetEstados() {
  escaneoActivo = false;
  cancelarEscaneo = false;
  motor2Activo = false;
  motor3Activo = false;
  finCarreraTopDetectado = false;
  pausa = false; // Resetear estado de pausa
}

// Manejar estado de pausa (nueva función)
void manejarPausa() {
  if (pausa) {
    bool estadoMotor2 = motor2Activo;
    bool estadoMotor3 = motor3Activo;
    
    // Detener motores
    motor2Activo = false;
    motor3Activo = false;
    
    // Esperar mientras esté en pausa
    while (pausa && !cancelarEscaneo && !detenerEmergencia) {
      delay(10);
      server.handleClient();
    }
    
    // Reanudar motores si no se canceló
    if (!cancelarEscaneo && !detenerEmergencia) {
      motor2Activo = estadoMotor2;
      motor3Activo = estadoMotor3;
    }
  }
}

// Buscar posición inicial al encender
void buscarPosInicial() {
  Serial.println("Buscando posición inicial...");

  // Motor 2 a la izquierda
  digitalWrite(DIR2, HIGH);
  motor2Activo = true;
  while (digitalRead(LIMIT_LEFT2) == LOW && !cancelarEscaneo) {
    manejarPausa(); // Gestionar pausa
    delay(1);
    server.handleClient();
  }
  motor2Activo = false;
  delay(200);
  motor2EnInicio = true;

  // Motor 3 hacia abajo
  digitalWrite(DIR3, LOW);
  motor3Activo = true;
  while (digitalRead(LIMIT_BOTTOM3) == LOW && !cancelarEscaneo) {
    manejarPausa(); // Gestionar pausa
    delay(1);
    server.handleClient();
  }
  motor3Activo = false;
  delay(200);
  motor3EnInicio = true;

  enPosInicial = true;
  Serial.println("Motores en posición inicial.");
}

// Subir motor 3 durante 1 segundo
void subirMotor3PorTiempo() {
  if (digitalRead(LIMIT_TOP3) == HIGH) {
    finCarreraTopDetectado = true;
    return;
  }

  Serial.println("→ Subiendo motor 3");
  digitalWrite(DIR3, HIGH);
  motor3Activo = true;
  unsigned long start = millis();
  while (millis() - start < 1000 && escaneoActivo && !cancelarEscaneo) {
    manejarPausa(); // Gestionar pausa
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
  Serial.println("EMERGENCIA ACTIVADA: deteniendo motores...");
  detenerEmergencia = true;  
  escaneoActivo = false;
  pausa = false; // Desactivar pausa durante emergencia
  motor2Activo = false;
  motor3Activo = false;
}

// Secuencia de escaneo
void ejecutarEscaneo() {
  Serial.println("→ Iniciando escaneo...");

  while (escaneoActivo && !finCarreraTopDetectado && !cancelarEscaneo) {
    // Mover hacia la derecha
    Serial.println("Motor 2 → Derecha");
    digitalWrite(DIR2, LOW);
    motor2Activo = true;
    while (digitalRead(LIMIT_RIGHT2) == LOW && escaneoActivo && !cancelarEscaneo) {
      manejarPausa(); // Gestionar pausa
      delay(1);
      server.handleClient();
    }
    motor2Activo = false;
    delay(200);
    subirMotor3PorTiempo();  // Subir al llegar al fin de carrera derecho

    // Cancelar si fue solicitado
    if (cancelarEscaneo || finCarreraTopDetectado) break;

    // Mover hacia la izquierda
    Serial.println("Motor 2 ← Izquierda");
    digitalWrite(DIR2, HIGH);
    motor2Activo = true;
    while (digitalRead(LIMIT_LEFT2) == LOW && escaneoActivo && !cancelarEscaneo) {
      manejarPausa(); // Gestionar pausa
      delay(1);
      server.handleClient();
    }
    motor2Activo = false;
    delay(200);
    subirMotor3PorTiempo();  // Subir al llegar al fin de carrera izquierdo
  }

  // Último barrido si se alcanzó tope superior
  if (finCarreraTopDetectado && !cancelarEscaneo) {
    Serial.println("Tope superior alcanzado.");
    delay(2000);  // Esperar 2 segundos

    // Último barrido hacia la derecha
    Serial.println("Último barrido hacia la derecha...");
    digitalWrite(DIR2, LOW);
    motor2Activo = true;
    while (digitalRead(LIMIT_RIGHT2) == LOW && !cancelarEscaneo) {
      manejarPausa(); // Gestionar pausa
      delay(1);
      server.handleClient();
    }
    motor2Activo = false;

    // Finaliza escaneo y vuelve a inicio
    Serial.println("Finalizando escaneo. Regresando a posición inicial...");
    resetEstados();   // Resetear primero
    volverAInicio();  // Volver al inicio
    return;           // Salir de la función
  }
}

// Función para regresar a posición inicial
void volverAInicio() {
  Serial.println("→ Regresando a posición inicial...");

  // Motor 3 hacia abajo
  digitalWrite(DIR3, LOW);
  motor3Activo = true;
  while (digitalRead(LIMIT_BOTTOM3) == LOW && !escaneoActivo) {
    manejarPausa(); // Gestionar pausa
    delay(1);
    server.handleClient();
  }
  motor3Activo = false;
  delay(200);

  // Motor 2 a la izquierda
  digitalWrite(DIR2, HIGH);
  motor2Activo = true;
  while (digitalRead(LIMIT_LEFT2) == LOW && !escaneoActivo) {
    manejarPausa(); // Gestionar pausa
    delay(1);
    server.handleClient();
  }
  motor2Activo = false;

  Serial.println("← Posición inicial alcanzada.");
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

  ticker2.attach_us(1050, togglePulse2);
  ticker3.attach_us(100, togglePulse3);

  server.on("/", []() {
    server.send(200, "text/html", html);
  });

  server.on("/emergencia", []() {
    detenerTodosLosMotores();
    server.send(200, "text/plain", "Emergencia activada. Motores detenidos.");
  });

  server.on("/start", []() {
    detenerEmergencia = false;
    if (!escaneoActivo) {
      cancelarEscaneo = false;
      pausa = false; // Asegurar que no está en pausa
      contadorSubidas = 0;
      volverAInicio();
      escaneoActivo = true;
      server.send(200, "text/plain", "Escaneo iniciado");
    }
  });

  server.on("/cancel", []() {
    cancelarEscaneo = true;
    escaneoActivo = false;
    pausa = false; // Desactivar pausa al cancelar
    motor2Activo = false;
    motor3Activo = false;
    delay(200);
    volverAInicio();
    server.send(200, "text/plain", "Escaneo cancelado y regreso al inicio");
  });

  // Nuevo manejador para pausa/reanudación
  server.on("/pausa", []() {
    pausa = !pausa;
    if (pausa) {
      server.send(200, "text/plain", "PAUSA activada");
    } else {
      server.send(200, "text/plain", "Escaneo REANUDADO");
    }
  });

  server.on("/contador", []() {
    server.send(200, "text/plain", String(contadorSubidas));
  });

  server.begin();
  buscarPosInicial();
}

// Loop principal
void loop() {
  server.handleClient();

  if (escaneoActivo) {
    enPosInicial = false;
    ejecutarEscaneo();
  }
}