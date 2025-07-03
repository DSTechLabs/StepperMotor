//==========================================================
//
//   FILE   : StepperMotor.cpp
//
//  PROJECT : Stepper Motor (Digital Only)
//
//    NOTES : This firmware is used to operate a single digital bipolar stepper driver/motor
//            from an MCU development board such as an Arduino or ESP32.
//            It incorporates both soft and hard limits (by specifying limit switch pins).
//
//   AUTHOR : Bill Daniels (bill@dstechlabs.com)
//            See LICENSE.md
//
//==========================================================

#include <Arduino.h>
#include <string.h>
#include "StepperMotor.h"

//==========================================================
//  Constructor
//==========================================================
StepperMotor::StepperMotor (int enablePin, int directionPin, int stepPin, int llSwitchPin, int ulSwitchPin)
{
  // Save the specified pins for the both driver types
  EnablePin    = enablePin;
  DirectionPin = directionPin;
  StepPin      = stepPin;
  LLSwitchPin  = llSwitchPin;
  ULSwitchPin  = ulSwitchPin;

  // Set pin modes
  pinMode (EnablePin   , OUTPUT);
  pinMode (DirectionPin, OUTPUT);
  pinMode (StepPin     , OUTPUT);

  if (LLSwitchPin >= 0)
    pinMode (LLSwitchPin, INPUT_PULLUP);

  if (ULSwitchPin >= 0)
    pinMode (ULSwitchPin, INPUT_PULLUP);

  // Initialize pins
  digitalWrite (EnablePin   , HIGH);  // HIGH = Off (disabled)
  digitalWrite (DirectionPin, LOW);
  digitalWrite (StepPin     , LOW);

	// Set initial motor state and step position/timing
	Homed             = false;
  State             = MS_DISABLED;
  StepIncrement     = 1L;
  AbsolutePosition  = 0L;
  DeltaPosition     = 0L;
  TargetPosition    = 0L;
  LowerLimit        = -2000000000L;
  UpperLimit        =  2000000000L;
  RampSteps         = 0L;
  RampDownStep      = 0L;
  Velocity          = 0L;
  VelocityIncrement = RampScale * 5L;  // Default ramp scale of 5
  NextPosition      = 0L;
  NextStepMicros    = -1L;
  MaxVelocity       = 0;
  TargetOrSteps     = 0;
  TotalSteps        = 0;
  StepCount         = 0;
}

//=========================================================
//  Run:
//  Must be called inside your loop function with no delay.
//=========================================================
RunReturn StepperMotor::Run ()
{
  // Is the motor RUNNING and is it time for it to step
  if (Homed && (State == MS_RUNNING) && (micros() >= NextStepMicros))
  {
    // Is the motor at the target position?
    if (AbsolutePosition == TargetPosition)
    {
      // Yes, stop motor and indicate completion
      State = MS_ENABLED;
      return RUN_COMPLETE;
    }

    // No, so continue motion
    NextPosition = AbsolutePosition + StepIncrement;  // +1 for clockwise rotations, -1 for counter-clockwise

    // Check next position against range limits
    // If out of range, stop motor and return range error
    if (NextPosition < LowerLimit)
    {
      State = MS_ENABLED;
      return RANGE_ERROR_LOWER;
    }

    if (NextPosition > UpperLimit)
    {
      State = MS_ENABLED;
      return RANGE_ERROR_UPPER;
    }

    // Perform a single step
    doStep ();

    // Set current position
    AbsolutePosition = NextPosition;
    DeltaPosition   += StepIncrement;

    // Check limit switches, if specified
    if ((LLSwitchPin >= 0) && digitalRead (LLSwitchPin) == LOW)
    {
      // Lower limit switch triggered
      State = MS_ENABLED;
      return LIMIT_SWITCH_LOWER;
    }

    if ((ULSwitchPin >= 0) && digitalRead (ULSwitchPin) == LOW)
    {
      // Upper limit switch triggered
      State = MS_ENABLED;
      return LIMIT_SWITCH_UPPER;
    }

    // Adjust velocity if ramping
    StepCount = abs(DeltaPosition);
    if (StepCount <= RampSteps)
    {
      // Ramping up
      Velocity += VelocityIncrement;
    }
    else if (StepCount > RampDownStep)
    {
      // Ramping down
      Velocity -= VelocityIncrement;
    }

    // Set time for next step
    if (Velocity > 0L)
      NextStepMicros += 1000000L / Velocity;
  }

  return OKAY;
}

//=== startRotation =======================================

void StepperMotor::startRotation ()
{
  // Determine number of steps in ramp and set starting speed
  if (VelocityIncrement == 0L)
  {
    // Immediate full speed, no ramping
    RampSteps = 0L;
    Velocity  = MaxVelocity;  // Start at full velocity
  }
  else
  {
    // Ramp up
    RampSteps = MaxVelocity / VelocityIncrement;
    if (RampSteps == 0L)
      Velocity = MaxVelocity;  // Start at slow value
    else
      Velocity = 0L;  // Start from a stand-still
  }

  // Set on what step to start ramping down
  if (TotalSteps > 2L * RampSteps)
    RampDownStep = TotalSteps - RampSteps;  // Normal trapezoid velocity
  else
    RampDownStep = RampSteps = TotalSteps / 2L;  // Stunted triangle velocity

  // Set Direction
  if (TargetPosition >= AbsolutePosition)
  {
    StepIncrement = 1L;
    digitalWrite (DirectionPin, LOW);
  }
  else if (TargetPosition < AbsolutePosition)
  {
    StepIncrement = -1L;
    digitalWrite (DirectionPin, HIGH);
  }

  // Start rotation
  DeltaPosition  = 0L;
  NextStepMicros = micros() + 10L;  // Direction must be set 10-microseconds before stepping
  State          = MS_RUNNING;
}

//=== doStep ==============================================

void StepperMotor::doStep ()
{
  // Perform a single step
  digitalWrite (StepPin, HIGH);
  delayMicroseconds (PULSE_WIDTH);
  digitalWrite (StepPin, LOW);


  // // For fast stepping on the Arduino NANO:
  // PORTD |= B00010000;   // Turn on Pulse pin (Arduino NANO Digital Pin 4)
  // delayMicroseconds (PULSE_WIDTH);
  // PORTD &= ~B00010000;  // Turn off Pulse pin


}

//=== Enable ==============================================

void StepperMotor::Enable ()
{
  // Enable motor driver
  digitalWrite (EnablePin, LOW);
  State = MS_ENABLED;

  // Also, set the current position as HOME
  SetHomePosition ();
}

//=== Disable =============================================

void StepperMotor::Disable ()
{
  // Disable motor driver
  digitalWrite (EnablePin, HIGH);

  State = MS_DISABLED;
  Homed = false;  // When motor is free to move, the HOME position is lost
}

//=== FindHome ============================================

void StepperMotor::FindHome ()
{
  // Seek counter-clockwise to lower limit switch
  // Then back-off until switch releases and set
  // new HOME position

  if (LLSwitchPin >= 0)
  {
    Enable ();

    // Find limit switch
    digitalWrite (DirectionPin, HIGH);
    while (digitalRead (LLSwitchPin) == HIGH)
    {
      doStep ();
      delay (5L);
    }

    // Back off slowly
    digitalWrite (DirectionPin, LOW);
    while (digitalRead (LLSwitchPin) == LOW)
    {
      doStep ();
      delay (50L);
    }

    // A few more steps
    for (int i=0; i<10; i++)
      doStep ();

    SetHomePosition ();
  }
}

//=== SetHomePosition =====================================

void StepperMotor::SetHomePosition ()
{
  // Only if Enabled
  if (State == MS_ENABLED)
  {
    // Set current position as HOME position
    AbsolutePosition = 0L;
    DeltaPosition    = 0L;
    Homed            = true;
  }
}

//=== SetLowerLimit =======================================

void StepperMotor::SetLowerLimit (long lowerLimit)
{
  // Must be <= 0 and <= UpperLimit
  if (lowerLimit <= 0 && lowerLimit <= UpperLimit)
    LowerLimit = lowerLimit;
}

//=== SetUpperLimit =======================================

void StepperMotor::SetUpperLimit (long upperLimit)
{
  // Must be >= 0 and >= LowerLimit
  if (upperLimit >= 0 && upperLimit >= LowerLimit)
    UpperLimit = upperLimit;
}

//=== SetRamp =============================================

void StepperMotor::SetRamp (int ramp)
{
  // Must be 0..9
  if (ramp >= 0 && ramp <= 9)
  {
    // Set velocity slope (increment)
    if (ramp == 0)
      VelocityIncrement = 0L;  // constant full velocity
    else
      VelocityIncrement = RampScale * (10L - (long)ramp);
  }
}

//=== RotateAbsolute ======================================

void StepperMotor::RotateAbsolute (long newPosition, int stepsPerSecond)
{
  TargetPosition = newPosition;  // Set Absolute Position
  MaxVelocity    = stepsPerSecond;
  TotalSteps     = abs(TargetPosition - AbsolutePosition);

  startRotation ();
}

//==== RotateRelative =====================================

void StepperMotor::RotateRelative (long numSteps, int stepsPerSecond)
{
  // If numSteps is positive (> 0) then motor rotates clockwise, else counter-clockwise
  if (numSteps != 0)
  {
    TargetPosition = AbsolutePosition + numSteps;
    MaxVelocity    = stepsPerSecond;
    TotalSteps     = abs(numSteps);

    startRotation ();
  }
}

//=== RotateToHome ========================================

void StepperMotor::RotateToHome ()
{
  MaxVelocity    = HOMING_SPEED;
  TargetPosition = 0L;  // HOME position
  TotalSteps     = abs(AbsolutePosition);

  startRotation ();
}

//=== RotateToLowerLimit ==================================

void StepperMotor::RotateToLowerLimit ()
{
  MaxVelocity    = HOMING_SPEED;
  TargetPosition = LowerLimit;
  TotalSteps     = abs(AbsolutePosition - LowerLimit);

  startRotation ();
}

//=== RototaeToUpperLimit =================================

void StepperMotor::RotateToUpperLimit ()
{
  MaxVelocity    = HOMING_SPEED;
  TargetPosition = UpperLimit;
  TotalSteps     = abs(AbsolutePosition - UpperLimit);

  startRotation ();
}

//=== EStop ===============================================

void StepperMotor::EStop ()
{
  // Emergency Stop
  // ((( Requires Re-Enable of motor )))
	digitalWrite (StepPin  , LOW );   // Pulse Off
	digitalWrite (EnablePin, HIGH);   // Disengage

  State = MS_ESTOPPED;
  Homed = false;
  TargetPosition = AbsolutePosition;
}

//=== IsHomed =============================================

bool StepperMotor::IsHomed ()
{
  // Return homed state
  return Homed;
}

//=== GetState ============================================

MotorState StepperMotor::GetState ()
{
  // Return current motor state
  return State;
}

//=== GetAbsolutePosition =================================

long StepperMotor::GetAbsolutePosition ()
{
  // Return current position from HOME
  return AbsolutePosition;
}

//=== GetRelativePosition =================================

long StepperMotor::GetRelativePosition ()
{
  // Return number of steps moved from last position
  return DeltaPosition;
}

//=== GetLowerLimit =======================================

long StepperMotor::GetLowerLimit ()
{
  return LowerLimit;
}

//=== GetUpperLimit =======================================

long StepperMotor::GetUpperLimit ()
{
  return UpperLimit;
}

//=== GetRemainingTime ====================================

unsigned long StepperMotor::GetRemainingTime ()
{
  // Return remaining time (in milliseconds) for motion to complete
  if (State != MS_RUNNING)
    return 0L;

  unsigned long numSteps = abs (AbsolutePosition - TargetPosition);
  unsigned long remTime  = 1000L * numSteps / MaxVelocity + 500L;  // +500 for ramping

  return remTime;
}

//=== GetVersion ==========================================

const char * StepperMotor::GetVersion ()
{
  return version;
}

//=== BlinkLED ============================================

void StepperMotor::BlinkLED (int LEDpin)
{
  pinMode(LEDpin, OUTPUT);

  // Blink onboard LED
  for (int i=0; i<10; i++)
  {
    digitalWrite (LEDpin, HIGH);
    delay (20);
    digitalWrite (LEDpin, LOW);
    delay (80);
  }
}


//=========================================================
//  ExecuteCommand
//=========================================================

const char * StepperMotor::ExecuteCommand (const char *packet)
{
  char  command[3];
  int   ramp, velocity;
  long  limit, targetOrNumSteps;

  // Initialize return string
  ecReturnString[0] = 0;

  // Command string must be at least 2 chars
  if (strlen(packet) < 2)
  {
    strcpy (ecReturnString, "Bad command");
    return ecReturnString;
  }

  // Set 2-Char Command and parse all commands
  strncpy (command, packet, 2);
  command[2] = 0;

  //=======================================================
  //  Emergency Stop (ESTOP)
  //  I check this first for quick processing.
  //  When an E-Stop is called, you must re-Enable the
  //  driver for motion to resume.
  //=======================================================
  if (strcmp (command, "ES") == 0)
    EStop();

  //=======================================================
  //  Enable / Disable
  //=======================================================
  else if (strcmp (command, "EN") == 0)
    Enable ();
  else if (strcmp (command, "DI") == 0)
    Disable ();

  //=======================================================
  //  Find HOME, Set HOME Position, LOWER and UPPER Limits
  //=======================================================
  else if (strcmp (command, "FH") == 0)
    FindHome ();
  else if (strcmp (command, "SH") == 0)
    SetHomePosition ();
  else if (strcmp (command, "SL") == 0 || strcmp (command, "SU") == 0)
  {
    // Check for value
    if (strlen (packet) < 3)
      strcpy (ecReturnString, "Missing limit value");
    else
    {
      limit = strtol (packet+2, NULL, 10);

      if (strcmp (command, "SL") == 0)
        SetLowerLimit (limit);
      else
        SetUpperLimit (limit);
    }
  }

  //=======================================================
  //  Set Velocity Ramp Factor
  //=======================================================
  else if (strcmp (command, "SR") == 0)
  {
    // Check for value
    if (strlen (packet) != 3)
      strcpy (ecReturnString, "Missing ramp value 0-9");
    else
    {
      ramp = atoi (packet+2);

      // Check specified ramp value
      if (ramp >= 0 && ramp <= 9)
        SetRamp (ramp);
    }
  }

  //=======================================================
  //  Rotate Commands
  //=======================================================
  else if (strcmp (command, "RH") == 0)
    RotateToHome ();
  else if (strcmp (command, "RL") == 0)
    RotateToLowerLimit ();
  else if (strcmp (command, "RU") == 0)
    RotateToUpperLimit ();
  else if (strcmp (command, "RA") == 0 || strcmp (command, "RR") == 0)
  {
    // Rotate command must be at least 7 chars
    if (strlen (packet) < 7)
      strcpy (ecReturnString, "Bad command");
    else
    {
      // Parse max velocity and target/numSteps
      char velString[5];  // Velocity is 4-chars 0001..9999
      strncpy (velString, packet+2, 4);
      velString[4] = 0;
      velocity = strtol (velString, NULL, 10);
      targetOrNumSteps = strtol (packet+6, NULL, 10);  // Target position or number of steps is remainder of packet

      if (strcmp (command, "RA") == 0)
        RotateAbsolute (targetOrNumSteps, velocity);
      else
        RotateRelative (targetOrNumSteps, velocity);
    }
  }

  //=======================================================
  //  Query Commands and Blink
  //=======================================================
  else if (strcmp (command, "GA") == 0)
    ltoa (GetAbsolutePosition (), ecReturnString, 10);
  else if (strcmp (command, "GR") == 0)
    ltoa (GetRelativePosition (), ecReturnString, 10);
  else if (strcmp (command, "GL") == 0)
    ltoa(GetLowerLimit (), ecReturnString, 10);
  else if (strcmp (command, "GU") == 0)
    ltoa (GetUpperLimit (), ecReturnString, 10);
  else if (strcmp (command, "GT") == 0)
    ltoa (GetRemainingTime (), ecReturnString, 10);
  else if (strcmp (command, "GV") == 0)
    return GetVersion();
  else if (strcmp (command, "BL") == 0)
  {
    // Parse pin number: BLpin
    int pin = atoi (packet+2);
    BlinkLED (pin);
  }
  else
    strcpy (ecReturnString, "Unknown command");

  return ecReturnString;
}
