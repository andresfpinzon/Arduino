//Libraries

//Pins definitions
int pulPin[3]={8,7,10};  // PUL pin drivers
int dirPin[3]={9,6,9};   // DIR pin drivers
int enPin[3]={2,5,8};   // ENA pin drivers
#define limitSw 4


//Declaring variables
uint16_t step[3];   //steps for each motor

char buffer[16];   //maximum expected length 
int len = 0; // the length of the buffer
int steps = 0; //distance in mm from the computer
int speeds = 0; //delay between two steps, received from the computer
int receivedCommand; //character for commands

bool newData, runallowed = false; // booleans for new data from serial, and runallowed flag


void setup() {
    Serial.begin(9600); //initialize the serial transmition at 9600 bauds

    pinMode(limitSw,INPUT_PULLUP);

  //pins as output
 for(int i=0; i<3;i++){ 
    pinMode(pulPin[i], OUTPUT);
    pinMode(dirPin[i], OUTPUT);
    pinMode(enPin[i], OUTPUT);
   
  } 

}

void loop() {


 
  if (runallowed == true )
  {
     
       if(steps>0){
          controlMotor1(0, 1, steps, map(speeds,1,2000,2000,1));
       }
       else{
          controlMotor1(0, 0, steps*-1,map(speeds,1,2000,2000,1));
       }
  }
  else{
    checkSerial();
  }

}

void controlMotor1(int motor, bool dir, int stepM, int speedM){ 
    int i=0;

    digitalWrite(dirPin[motor],dir);
    digitalWrite(enPin[motor],HIGH);  // enable the driver steppers
    step[motor]=stepM;

   while(i<step[motor] && digitalRead(limitSw)==HIGH){ 
      digitalWrite(pulPin[motor], HIGH);
      delayMicroseconds(speedM);
      digitalWrite(pulPin[motor], LOW);
      delayMicroseconds(speedM);

      i++;
    }

    if(digitalRead(limitSw)==LOW){
      Serial.println("Limit Sw activated");
      Serial.println("returning to zero");
      digitalWrite(dirPin[motor],!dir);

        while(i>0){ 
            digitalWrite(pulPin[motor], HIGH);
            delayMicroseconds(speedM);
            digitalWrite(pulPin[motor], LOW);
            delayMicroseconds(speedM);

        i--;
       }
    }


    digitalWrite(enPin[motor],LOW);  // disable the driver steppers
    runallowed = false;

}


/*////////////////////////////////////////////****Method to check the serial data****\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\*/
void checkSerial() //method for receiving the commands
{  
  
 
  if (Serial.available() > 0) //if something comes
  {
    receivedCommand = Serial.read(); // this will read the command character
    Serial.write(receivedCommand); 
    newData = true; //this creates a flag
    buffer[len++] = receivedCommand;  
    //
    // check for overflow
    //
    if (len >= 16)
    {
      // overflow, resetting
      len = 0;
    }
    //
    // check for newline (end of message)
    //
  }
 
  if (newData == true) //if we received something (see above)
  {
    //START - MEASURE
   //  Serial.println("new data"); 

  //  if (receivedCommand == 13) //** to check for newline  '\n' 
    if (receivedCommand == '\n') //to check for newline  '\n'
    { 
      Serial.println(" "); 

          
      int n = sscanf(buffer, "%d %d", &steps, &speeds);
      if (n == 2)
      {
        
          Serial.print("Steps:"); 
          Serial.print(steps);
          Serial.print(" Speed:"); 
          Serial.println(speeds); 

        
       if(steps!=0 && speeds>0){
          runallowed = true; //allow running

             
       } 
    
      }
      else
      {
        Serial.println("ERROR");
      }
      len = 0; // reset buffer counter
 
    }
  }
  //after we went through the above tasks, newData becomes false again, so we are ready to receive new commands again.
  newData = false;
 
}