#include <WiFi.h>
#include <WebServer.h>

const char* ssid = "ESP_MOTOR";
const char* password = "12345678";

// Pines del motor (ajusta según tu conexión)
#define PUL_PIN 13   // STEP
#define DIR_PIN 12   // DIR
#define ENA_PIN 33   // ENABLE (opcional)

// Configuración del motor
const int pasosPorVuelta = 200;  // Pasos por vuelta completa
const int microsteps = 16;       // Configuración del driver (1, 2, 4, 8, 16...)
const int pasosTotalesPorVuelta = pasosPorVuelta * microsteps;

WebServer server(80);

// Variables de control
volatile bool motorEnMovimiento = false;
int motorDelay = 800; // microsegundos entre pasos

// Página HTML mejorada
const char* htmlPage = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body { 
      font-family: Arial; 
      background: #f0f0f0; 
      text-align: center; 
      padding: 20px;
    }
    .container { 
      background: white; 
      max-width: 500px;
      margin: 0 auto;
      border-radius: 15px; 
      padding: 25px; 
      box-shadow: 0 4px 15px rgba(0,0,0,0.1);
    }
    h2 { color: #2c3e50; }
    input, button, select { 
      font-size: 16px; 
      padding: 12px; 
      margin: 10px;
      width: 90%;
      border: 1px solid #ddd;
      border-radius: 5px;
    }
    button {
      background: #3498db;
      color: white;
      border: none;
      cursor: pointer;
      transition: 0.3s;
    }
    button:hover { background: #2980b9; }
    .status {
      margin: 15px;
      padding: 10px;
      background: #f8f9fa;
      border-radius: 5px;
    }
  </style>
</head>
<body>
  <div class="container">
    <h2>Control de Motor Paso a Paso</h2>
    <p><b>ESP32-WROOM-32</b></p>
    
    <div class="status">
      <p>Configuración: <span id="config">200 pasos/vuelta × 16 microsteps</span></p>
      <p>Pasos/vuelta: <b>3200</b> | Velocidad actual: <span id="currentSpeed">800</span> µs</p>
    </div>
    
    <input type="number" id="steps" placeholder="Pasos (+/-)" min="-10000" max="10000">
    
    <label for="speedRange">Velocidad (100-2000 µs)</label>
    <input type="range" id="speedRange" min="100" max="2000" value="800" step="50" 
           oninput="updateSpeed(this.value)">
    
    <button onclick="moveMotor()">Mover Motor</button>
    <button onclick="stopMotor()" style="background:#e74c3c;">Parada de Emergencia</button>
  </div>

  <script>
    function updateSpeed(val) {
      document.getElementById("currentSpeed").textContent = val;
    }

    function moveMotor() {
      const steps = document.getElementById("steps").value;
      const speed = document.getElementById("speedRange").value;
      if(!steps) return alert("Ingresa cantidad de pasos");
      
      fetch(`/move?steps=${steps}&speed=${speed}`)
        .then(response => response.text())
        .then(data => console.log(data))
        .catch(err => console.error("Error:", err));
    }

    function stopMotor() {
      fetch("/stop")
        .then(response => console.log("Motor detenido"))
        .catch(err => console.error("Error:", err));
    }
  </script>
</body>
</html>
)rawliteral";

// Función mejorada para mover el motor
void moveMotor(int steps, int delayMicros) {
  if(motorEnMovimiento) return;
  
  motorEnMovimiento = true;
  bool dir = steps >= 0;
  steps = abs(steps);

  digitalWrite(DIR_PIN, dir);
  digitalWrite(ENA_PIN, LOW);  // Activar driver

  // Timing optimizado para ESP32
  int halfDelay = delayMicros / 2;
  
  for(int i = 0; i < steps && motorEnMovimiento; i++) {
    digitalWrite(PUL_PIN, HIGH);
    delayMicroseconds(halfDelay);
    digitalWrite(PUL_PIN, LOW);
    delayMicroseconds(halfDelay);
    
    // Pequeña pausa cada 100 pasos para mantener estabilidad
    if(i % 100 == 0) delayMicroseconds(100);
  }

  digitalWrite(ENA_PIN, HIGH); // Desactivar driver
  motorEnMovimiento = false;
}

void stopMotor() {
  motorEnMovimiento = false;
  digitalWrite(ENA_PIN, HIGH); // Desactivar driver inmediatamente
}

void setup() {
  Serial.begin(115200);
  
  // Configuración de pines
  pinMode(PUL_PIN, OUTPUT);
  pinMode(DIR_PIN, OUTPUT);
  pinMode(ENA_PIN, OUTPUT);
  digitalWrite(PUL_PIN, LOW);
  digitalWrite(DIR_PIN, LOW);
  digitalWrite(ENA_PIN, HIGH); // Driver desactivado inicialmente

  // Iniciar AP WiFi
  WiFi.softAP(ssid, password);
  Serial.println("\nAccess Point creado");
  Serial.print("SSID: "); Serial.println(ssid);
  Serial.print("IP: "); Serial.println(WiFi.softAPIP());

  // Configurar rutas del servidor web
  server.on("/", HTTP_GET, []() {
    server.send(200, "text/html", htmlPage);
  });

  server.on("/move", HTTP_GET, []() {
    if(server.hasArg("steps") && server.hasArg("speed")) {
      int steps = server.arg("steps").toInt();
      motorDelay = constrain(server.arg("speed").toInt(), 100, 2000);
      
      Serial.printf("Comando recibido: %d pasos a %d µs\n", steps, motorDelay);
      moveMotor(steps, motorDelay);
      
      server.send(200, "text/plain", "Movimiento completado");
    } else {
      server.send(400, "text/plain", "Parámetros incorrectos");
    }
  });

  server.on("/stop", HTTP_GET, []() {
    stopMotor();
    server.send(200, "text/plain", "Motor detenido");
  });

  server.begin();
  Serial.println("Servidor HTTP iniciado");
}

void loop() {
  server.handleClient();
}