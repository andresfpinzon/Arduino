#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>

const char* ssid = "prueba";
const char* password = "12345678";

const int pulPin = 21;
const int dirPin = 22;

WebServer server(80);
Preferences prefs;

volatile int stepsToMove = 0;
volatile int direction = 1;
volatile bool running = false;
volatile bool motorBusy = false;

const int speedMicro = 800;
const int maxPosition = 100; // Equivalente a 90° (100 pasos)
int currentPosition = 0;

// HTML
const char htmlPage[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>Control Motor</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body { font-family: Arial; padding: 20px; }
    label { font-weight: bold; }
  </style>
</head>
<body>
  <h2>Control Cámara - Escáner 3D</h2>

  <label>Ángulo (0 a 90): <span id="stepVal">45</span></label><br>
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
  fetch("/get")
    .then(res => res.json())
    .then(data => {
      document.getElementById("stepVal").innerText = data.angle;
      document.getElementById("stepsSlider").value = data.angle;
    });
}, 1000);
</script>
</body>
</html>
)rawliteral";

// Función para mover el motor a 0 pasos (posición inicial)
void moveToZero() {
  Serial.println("Moviendo a posición 0...");
  digitalWrite(dirPin, LOW); // Dirección hacia 0°

  for (int i = 0; i < currentPosition; i++) {
    digitalWrite(pulPin, HIGH);
    delayMicroseconds(speedMicro);
    digitalWrite(pulPin, LOW);
    delayMicroseconds(speedMicro);
  }

  currentPosition = 0;
  prefs.putInt("pos", currentPosition);
  Serial.println("Posición reiniciada a 0 pasos.");
}

void setup() {
  Serial.begin(115200);
  pinMode(pulPin, OUTPUT);
  pinMode(dirPin, OUTPUT);

  prefs.begin("motor", false);
  currentPosition = prefs.getInt("pos", 0); 
  moveToZero(); 

  // Access Point
  WiFi.softAP(ssid, password);
  IPAddress IP = WiFi.softAPIP();
  Serial.print("Red WiFi creada. Conéctate a: ");
  Serial.println(ssid);
  Serial.print("IP del ESP32: ");
  Serial.println(IP);

  server.on("/", []() {
    server.send_P(200, "text/html", htmlPage);
  });

  server.on("/get", []() {
    int angle = map(currentPosition, 0, maxPosition, 0, 90);
    server.send(200, "application/json", "{\"angle\":" + String(angle) + "}");
  });

  server.on("/set", []() {
    if (!server.hasArg("angle")) return;

    int angle = constrain(server.arg("angle").toInt(), 0, 90); // Validar entrada
    int targetPosition = map(angle, 0, 90, 0, maxPosition);    // Convertir a pasos

    if (targetPosition == currentPosition) return; // Ya está ahí

    if (!motorBusy && !running) {
      stepsToMove = abs(targetPosition - currentPosition);
      direction = (targetPosition > currentPosition) ? 1 : 0;
      running = true;
    }
  });

  server.begin();
}

void loop() {
  server.handleClient();

  if (running && !motorBusy) {
    motorBusy = true;
    digitalWrite(dirPin, direction ? HIGH : LOW);

    for (int i = 0; i < stepsToMove; i++) {
      digitalWrite(pulPin, HIGH);
      delayMicroseconds(speedMicro);
      digitalWrite(pulPin, LOW);
      delayMicroseconds(speedMicro);
    }

    int newPosition = currentPosition + (direction ? stepsToMove : -stepsToMove);
    newPosition = constrain(newPosition, 0, maxPosition);

    if (newPosition != currentPosition) {
      currentPosition = newPosition;
      prefs.putInt("pos", currentPosition);
    }

    stepsToMove = 0;
    running = false;
    motorBusy = false;
  }
}

