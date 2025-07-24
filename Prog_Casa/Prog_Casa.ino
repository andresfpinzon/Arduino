#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// Pines
const int boton1 = 5;
const int boton2 = 13;
const int motorHorario = 26;
const int motorAntihorario = 25;

// LCD
LiquidCrystal_I2C lcd(0x27, 16, 2); // Cambia 0x27 si tu pantalla tiene otra dirección I2C

// Variables
int velocidad = 0;
bool boton2Presionado = false;

void setup() {
  // Configuración de pines
  pinMode(boton1, INPUT);
  pinMode(boton2, INPUT);
  pinMode(motorHorario, OUTPUT);
  pinMode(motorAntihorario, OUTPUT);

  // Configuración LCD
  lcd.init();
  lcd.backlight();

  // Inicializar el motor apagado
  analogWrite(motorHorario, 0);
  analogWrite(motorAntihorario, 0);
}

void loop() {
  bool estadoBoton1 = digitalRead(boton1);
  bool estadoBoton2 = digitalRead(boton2);

  // Manejo del pulsador 2: aumentar velocidad de 5 en 5, hasta 255, luego volver a 0
  if (estadoBoton2 && !boton2Presionado) {
    velocidad += 5;
    if (velocidad > 255) {
      velocidad = 0;
    }
    boton2Presionado = true;
  } else if (!estadoBoton2) {
    boton2Presionado = false;
  }

  // Manejo del motor según el estado del botón 1
  if (estadoBoton1) {
    // Giro horario
    analogWrite(motorHorario, velocidad);
    analogWrite(motorAntihorario, 0);

    // Mostrar en pantalla
    lcd.setCursor(0, 0);
    lcd.print("Sentido: Horario  ");
  } else {
    // Giro antihorario
    analogWrite(motorHorario, 0);
    analogWrite(motorAntihorario, velocidad);

    // Mostrar en pantalla
    lcd.setCursor(0, 0);
    lcd.print("Sentido: Antihor  ");
  }

  // Mostrar velocidad en segunda fila
  lcd.setCursor(0, 1);
  lcd.print("Velocidad: ");
  lcd.print(velocidad);
  lcd.print("   ");

  delay(100);
}
