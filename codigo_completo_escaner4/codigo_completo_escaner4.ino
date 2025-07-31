#include <WiFi.h>
#include <WebServer.h>
#include <Ticker.h>

// Motor 1
#define PUL 23
#define DIR 27
#define LIMIT_LEFT 18
#define LIMIT_RIGHT 13

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

const char* ssid = "11";
const char* password = "12345678";

WebServer server(80);

// Estados para motor 1
bool movingRight1 = true;
bool motorActive1 = false;
bool canChangeDirection1 = true;

// Estados para motor 2
bool movingRight2 = true;
bool motorActive2 = false;
bool canChangeDirection2 = true;

// Estados para motor 3
bool movingRight3 = true;
bool motorActive3 = false;
bool canChangeDirection3 = true;

unsigned long lastDebounceTime1 = 0;
unsigned long lastDebounceTime2 = 0;
unsigned long lastDebounceTime3 = 0;
const unsigned long debounceDelay = 300; // ms

Ticker motorPulseTicker1;
Ticker motorPulseTicker2;
Ticker motorPulseTicker3;

// Tickers de pulso
void togglePulse1() {
  digitalWrite(PUL, motorActive1 ? !digitalRead(PUL) : LOW);
}

void togglePulse2() {
  digitalWrite(PUL2, motorActive2 ? !digitalRead(PUL2) : LOW);
}

void togglePulse3() {
  digitalWrite(PUL3, motorActive3 ? !digitalRead(PUL3) : LOW);
}

// HTML
const char* html = R"rawliteral(
<!DOCTYPE html><html><head><title>Control de Motores</title></head><body>
<h1>Control Motores</h1>

<h2>Motor 1</h2>
<button onclick="location.href='/start1'">Iniciar Motor 1</button>
<button onclick="location.href='/stop1'">Detener Motor 1</button>

<h2>Motor 2</h2>
<button onclick="location.href='/start2'">Iniciar Motor 2</button>
<button onclick="location.href='/stop2'">Detener Motor 2</button>

<h2>Motor 3</h2>
<button onclick="location.href='/start3'">Iniciar Motor 3</button>
<button onclick="location.href='/stop3'">Detener Motor 3</button>

</body></html>
)rawliteral";

void setup() {
  Serial.begin(115200);

  // Pines Motor 1
  pinMode(PUL, OUTPUT);
  pinMode(DIR, OUTPUT);
  pinMode(LIMIT_LEFT, INPUT);
  pinMode(LIMIT_RIGHT, INPUT);

  // Pines Motor 2
  pinMode(PUL2, OUTPUT);
  pinMode(DIR2, OUTPUT);
  pinMode(LIMIT_LEFT2, INPUT);
  pinMode(LIMIT_RIGHT2, INPUT);

  // Pines Motor 3
  pinMode(PUL3, OUTPUT);
  pinMode(DIR3, OUTPUT);
  pinMode(LIMIT_LEFT3, INPUT);
  pinMode(LIMIT_RIGHT3, INPUT);

  // Conexión WiFi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConectado. IP: " + WiFi.localIP().toString());

  // Rutas del servidor
  server.on("/", []() { server.send(200, "text/html", html); });

  server.on("/start1", []() {
    motorActive1 = true;
    server.send(200, "text/html", html);
    Serial.println("Motor 1 INICIADO");
  });
  server.on("/stop1", []() {
    motorActive1 = false;
    server.send(200, "text/html", html);
    Serial.println("Motor 1 DETENIDO");
  });

  server.on("/start2", []() {
    motorActive2 = true;
    server.send(200, "text/html", html);
    Serial.println("Motor 2 INICIADO");
  });
  server.on("/stop2", []() {
    motorActive2 = false;
    server.send(200, "text/html", html);
    Serial.println("Motor 2 DETENIDO");
  });

  server.on("/start3", []() {
    motorActive3 = true;
    server.send(200, "text/html", html);
    Serial.println("Motor 3 INICIADO");
  });
  server.on("/stop3", []() {
    motorActive3 = false;
    server.send(200, "text/html", html);
    Serial.println("Motor 3 DETENIDO");
  });

  server.begin();

  // Iniciar pulsos
  motorPulseTicker1.attach_us(30, togglePulse1);      // Motor 1
  motorPulseTicker2.attach_us(1050, togglePulse2);     // Motor 2
  motorPulseTicker3.attach_us(100, togglePulse3);    // Motor 3
}

void loop() {
  server.handleClient();
  unsigned long now = millis();

  // Motor 1
  if (motorActive1) {
    if (canChangeDirection1) {
      if (movingRight1 && digitalRead(LIMIT_RIGHT) == HIGH) {
        movingRight1 = false;
        digitalWrite(DIR, HIGH);
        canChangeDirection1 = false;
        lastDebounceTime1 = now;
        Serial.println("Motor 1: Cambio a IZQUIERDA");
      }
     
     
      else if (!movingRight1 && digitalRead(LIMIT_LEFT) == HIGH) {
        movingRight1 = true;
        digitalWrite(DIR, LOW);
        canChangeDirection1 = false;
        lastDebounceTime1 = now;
        Serial.println("Motor 1: Cambio a DERECHA");
      }
    } else if (now - lastDebounceTime1 > debounceDelay) {
      canChangeDirection1 = true;
    }
  }

  // Motor 2
  if (motorActive2) {
    if (canChangeDirection2) {
      if (movingRight2 && digitalRead(LIMIT_RIGHT2) == HIGH) {
        movingRight2 = false;
        digitalWrite(DIR2, HIGH);
        canChangeDirection2 = false;
        lastDebounceTime2 = now;
        Serial.println("Motor 2: Cambio a IZQUIERDA");
      } else if (!movingRight2 && digitalRead(LIMIT_LEFT2) == HIGH) {
        movingRight2 = true;
        digitalWrite(DIR2, LOW);
        canChangeDirection2 = false;
        lastDebounceTime2 = now;
        Serial.println("Motor 2: Cambio a DERECHA");
      }
    } else if (now - lastDebounceTime2 > debounceDelay) {
      canChangeDirection2 = true;
    }
  }

  // Motor 3
  if (motorActive3) {
    if (canChangeDirection3) {
      if (movingRight3 && digitalRead(LIMIT_RIGHT3) == HIGH) {
        movingRight3 = false;
        digitalWrite(DIR3, HIGH);
        canChangeDirection3 = false;
        lastDebounceTime3 = now;
        Serial.println("Motor 3: Cambio a IZQUIERDA");
      } else if (!movingRight3 && digitalRead(LIMIT_LEFT3) == HIGH) {
        movingRight3 = true;
        digitalWrite(DIR3, LOW);
        canChangeDirection3 = false;
        lastDebounceTime3 = now;
        Serial.println("Motor 3: Cambio a DERECHA");
      }
    } else if (now - lastDebounceTime3 > debounceDelay) {
      canChangeDirection3 = true;
    }
  }
}
