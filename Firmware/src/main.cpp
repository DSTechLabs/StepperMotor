//=============================================================================
//
//     FILE : main.cpp
//
//  PROJECT : Stepper Motor (Digital Only)
//
//    NOTES : This test app uses the Serial Monitor to
//            receive and execute stepper motor commands.
//            (See StepperMotor.h for details)
//
//   AUTHOR : Bill Daniels (bill@dstechlabs.com)
//            See LICENSE.md
//
//=============================================================================

//--- Includes --------------------------------------------

#include <Arduino.h>
#include "StepperMotor.h"

//--- Defines ---------------------------------------------

#define SERIAL_BAUDRATE       115200L
#define MAX_COMMAND_LENGTH    20       // Update this length to hold your longest command

#define DRIVER_ENABLE_PIN     2        // MCU pins going to stepper driver board
#define DRIVER_DIRECTION_PIN  3
#define DRIVER_STEP_PIN       4

//--- Globals ---------------------------------------------

char        commandString[MAX_COMMAND_LENGTH] = "";  // Command from Serial client (Chrome browser web app)
int         commandLength = 0;
char        nextChar;
const char  *response;

// A StepperMotor object
StepperMotor  *MyStepper;

//--- Declarations -----------------------------------------

bool commandReady ();


//==========================================================
//  setup
//==========================================================
void setup ()
{
  //Initialize serial and wait for port to open:
  Serial.begin (115200);
  while (!Serial);  // wait for serial port to connect

  // Init and enable the motor driver (energize)
  MyStepper = new StepperMotor (DRIVER_ENABLE_PIN, DRIVER_DIRECTION_PIN, DRIVER_STEP_PIN);
  MyStepper->Enable ();

  // Ready for commands
  Serial.print (MyStepper->GetVersion());
  Serial.println (" : ready");
}


//==========================================================
//  loop
//==========================================================
void loop ()
{
  // Keep the motor running by calling Run()
  switch (MyStepper->Run ())
  {
    case RUN_COMPLETE:
      Serial.print ("Run complete, position = ");
      Serial.println (MyStepper->GetAbsolutePosition ());
      break;

    case RANGE_ERROR_LOWER:
      Serial.print ("Lower Range Error, position = ");
      Serial.println (MyStepper->GetAbsolutePosition ());
      break;

    case RANGE_ERROR_UPPER:
      Serial.print ("Upper Range Error, position = ");
      Serial.println (MyStepper->GetAbsolutePosition ());
      break;

    case LIMIT_SWITCH_LOWER:
      Serial.print ("Lower Limit Switch Triggered, position = ");
      Serial.println (MyStepper->GetAbsolutePosition ());
      break;

    case LIMIT_SWITCH_UPPER:
      Serial.print ("Upper Limit Switch Triggered, position = ");
      Serial.println (MyStepper->GetAbsolutePosition ());
      break;
  }

  // Check for any commands from UI app
  if (commandReady ())
  {
    response = MyStepper->ExecuteCommand (commandString);

    // Handle response
    if (strlen (response) > 0)
      Serial.println (response);

    // Reset for a new command
    commandLength = 0;
  }
}

//--- commandReady ----------------------------------------

bool commandReady ()
{
  while (Serial.available ())
  {
    nextChar = (char) Serial.read ();

    if (nextChar != '\r')  // ignore CR's
    {
      if (nextChar == '\n')
      {
        // Command is ready, terminate it
        commandString[commandLength] = 0;
        return true;
      }

      // Add char to end of buffer
      if (commandLength < MAX_COMMAND_LENGTH - 1)
        commandString[commandLength++] = nextChar;
      else  // too long
      {
        Serial.println ("ERROR: Command is too long.");

        // Ignore and start new command
        commandLength = 0;
      }
    }
  }

  return false;
}
