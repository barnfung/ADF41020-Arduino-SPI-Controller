#include <SoftwareSerial.h>

//############################################################
// To Do / Roadmap
//############################################################
/*
CURRENT VERSION: Implementing serial interface with Matlab for
communication of frequency vector. (Serial.available in loop)

NEXT VERSION: Add support for 3-bit ADC for control signals

FUTURE VERSIONs:replace Serial.available in loop by interrupt
*/

//############################################################
// SPI Serial Interface for the ADF41020
//############################################################
/*
We have a 14 Bit R Counter, 19 Bit N Counter, and a
21 Bit Function Latch. All are sent to a 24 bit shift register,
the 2 LSBs dictate the final destination. The serial order of
data is Function, R (send once), then N repeating. There is
no SS or MISO, only CLK, MOSI, LE.

The 3 signals that must connect to the ADF41020:
SPI MOSI: Pin 11
SPI CLK: PIN 13
LE: Pin 7

Note there is no SS or MISO. I also added another signal:
SI: Pin 6

This is an indicator that goes high before eqch frequency
sweep. It is not needed by the ADF41020 and is only used
for external syncing purposes.
*/

// For SPI Communications
#include <SPI.h>

#define DWELL_TIME 1000 // Must be smaller than 16383
// The time spent at each frequency (which is the delay between 24 bit serial data being sent)
// in microseconds

#define SWEEP_PAUSE 0 // Must be smaller than 16383
// Delay between frequency sweeps
// in microseconds

#define PULSE_DURATION 500 // Must be smaller than 16383
// The duration of pulses in all control signals
// in microseconds
// Must be shorter than DWELL_TIME and SWEEP_PAUSE

#define LONG_DELAY 100 // Must be smaller than 16383
// The additional delay for dwell time and sweep pause to
// slow down the sweep for manual observation
// in milliseconds

#define DO_LONG_DELAY false
// if true, then LONG_DELAY is added to DWELL_TIME and SWEEP_PAUSE

//############################################################
// PRECALCULATED SPI DATA //
//############################################################

// RF Input Frequency in MHz
#define RFInputFrequency 100

// PFD Frequency in MHz
#define PFDFrequency 1250

// Vector of frequencies in MHz
int freqVec [] = {
10415,
10460,
10505,
10550,
10595,
10640,
10685,
10730,
10775,
10820,
10865,
10910,
10955,
11000,
11045,
11090,
11135,
11180,
11225,
11270,
11315,
11360,
11405,
11450,
11495,
11540,
11585,
11630,
11675,
11720,
11765,
11810,
11855,
11900,
11945,
11990,
12035,
12080,
12125,
12170,
12215,
12260,
12305,
12350,
12395,
12440,
12485,
12530,
12575,
12620,
12665,
12710,
12755,
12800,
12845,
12890,
12935,
12980,
13025,
13070,
};

//############################################################
// N Counter Arrays
//############################################################

// Least significant byte of N Counter to be populated by CalcRegisters
byte N0 [sizeof(freqVec)/sizeof(int)] = {};

// Middle Byte of N Counter to be populated by CalcRegisters
byte N1 [sizeof(freqVec)/sizeof(int)] = {};

// Precalculated byte arrays for N Counter, this is always 0x00
byte N2 = 0x00; // MSBs

//############################################################
// R Latch Arrays
//############################################################
// At this point we're assuming constant Rcounters throughout the sweep
// if this is not the case, we log to the serial monitor an error
// and stop the code execution

// RCounter bytes
byte R2; // MSBs
byte R1;
byte R0; // LSBs

// Least significant byte of the R Latch to be populated by CalcRegisters
byte R0A [sizeof(freqVec)/sizeof(int)] = {};

// Middle Byte of R Latch to be populated by CalcRegisters
byte R1A [sizeof(freqVec)/sizeof(int)] = {};

// Most significant byte of R Latch to be populated by CalcRegisters
byte R2A [sizeof(freqVec)/sizeof(int)] = {};

//#############################################################
// Function Latch Arrays
//#############################################################
// At this point we're assuming constant Function Latches throughout the sweep
// if this is not the case, we log to the serial monitor an error
// and stop the code execution

// Function Latch bytes
byte F2; // MSBs
byte F1; 
byte F0;// LSBs

// Least significant byte of the Function Latch to be populated by CalcRegisters
byte F0A [sizeof(freqVec)/sizeof(int)] = {};

// Middle Byte of Function Latch to be populated by CalcRegisters
byte F1A [sizeof(freqVec)/sizeof(int)] = {};

// Most significant byte of Function Latch to be populated by CalcRegisters
byte F2A [sizeof(freqVec)/sizeof(int)] = {};

//############################################################
// SETUP //
//############################################################
// Setup function, only run at the beginning

void setup() {
  
  // Set pints 6 and 7 as outputs without changing the values
  // of any other pins.
  // This is a C command so I can drive the pins
  // faster than using the standard Arduino synatx.
  DDRD = DDRD | B11000000;
  
  // Initialize LE and SI to low
  PORTD = B00000000;
  
  // Initialize Serial Port for debugging
  Serial.begin(9600);
  
  // Populate the NCounter and R Latch arrays
  for(int i =0; i< sizeof(freqVec)/sizeof(int); i++){ 
    calcRegisters(freqVec[i], RFInputFrequency, PFDFrequency, i);
        if(R0 != R0A[i] || R1 != R1A[i] || R2 != R2A[i]){
          Serial.println("RCounter changed during sweep. This is not yet supported. Execution halted");
   
           // Halt execution with infinite pause
           while(true){
             delay(1);
           }
    }
        if(F0 != F0A[i] || F1 != F1A[i] || F2 != F2A[i]){
           Serial.println("Function Latch changed during sweep. This is not yet supported. Execution halted");
    
           // Halt execution with infinite pause
           while(true){
             delay(1);
           }
    }
  }

  // Start the SPI library
  SPI.begin();
  // Set the bit order
  // The AD41020 expects the MSB to come first
  SPI.setBitOrder(MSBFIRST);
  // Set the data mode. Data is sent on the rising edge
  // and the clock is idle when low. This is Mode 0
  SPI.setDataMode(SPI_MODE0);
  // Set the speed we want to transmit at. This divides
  // the 16 MHz system clock. Default is divide by 4.
  SPI.setClockDivider(SPI_CLOCK_DIV2);
  
  // Wait just a bit
  delayer(SWEEP_PAUSE);
  
  // Send 3 bytes of Function Latch Data. This is the same each time
  // So we only send it once during setup
  SPI.transfer(F2);
  SPI.transfer(F1);
  SPI.transfer(F0);
  
  // Now we must set LE high for a bit to push the data out of the shift register
  PORTD = B10000000;
  delayMicroseconds(PULSE_DURATION);
  PORTD = B00000000;
 
  delayer(DWELL_TIME-PULSE_DURATION);
 
  // Send 3 bytes of R Latch Data. This is the same each time
  // So we only send it once during setup
  SPI.transfer(R2);
  SPI.transfer(R1);
  SPI.transfer(R0);
  
  // Now we must set LE high for a bit to push the data out of the shift register
  PORTD = B10000000;
  delayMicroseconds(PULSE_DURATION);
  PORTD = B00000000;
 
  delayer(DWELL_TIME-PULSE_DURATION);
}

//############################################################
// READ IN SERIAL DATA FROM MATLAB
//############################################################

String parseSerial(){
  char inChar=-1; // Where to store the character read
  byte index = 0; // Index into loop; where to store the character
  String command; // String of the command red in from serial
  
  // If there is serial data available, read it in
  if(Serial.available() > 0) {
     inChar = Serial.read(); // Read a character
     command+=inChar; // Store it
     index++; // Increment where to write next
  }
  
}

//############################################################
// POPULATE THE REGISTER ARRAY
//############################################################

void calcRegisters(float RFOutFreqBox, float RefFreqBox, float PFDFreqBox, int i){
  
  // Initialize parameters for the sweep
  int PrescalerBoxIndex = 1;
  int ChargePumpSetting1SelectedIndex = 3;
  int ChargePumpSetting2SelectedIndex = 3;
  int ChargePumpGainBoxSelectedIndex = 0;
  int ChargePump3StateBoxSelectedIndex = 0;
  int FastLockBoxSelectedIndex = 0;
  int TimeoutBoxSelectedIndex = 0;
  int PhaseDetectorPolarityBoxSelectedIndex = 0;
  int CounterResetBoxSelectedIndex = 0;
  int LockDetectPrecisionBoxSelectedIndex = 0;
  int PowerDownBoxSelectedIndex = 0;
  int ABPWBoxSelectedIndex=0;
  int SyncBoxSelectedIndex=0;
  int DelayBoxSelectedIndex=0;
  int MuxoutBoxSelectedIndex=0;
  int TestmodesBoxSelectedIndex=1;
  float Reg[3] = {};
  // end params
 
  float RFout = RFOutFreqBox;
  float REFin = RefFreqBox;
  float PFDFreq = PFDFreqBox;
  
  RFout = RFout/4;
   
  //Calculate P, R, N, B, & A values for calculating register
  int P = (int)pow(2,PrescalerBoxIndex) * 8;
  int R = (int)(REFin*1000/PFDFreq);
  int N = (int)(RFout*1000/PFDFreq);
  int B = (int)(N/P);
  int A = (int)(N-(B*P));
 
  //Cast relevant integer values to bytes
  byte Prescaler = (byte)PrescalerBoxIndex;
  byte CPsetting1 = (byte)ChargePumpSetting1SelectedIndex;
  byte CPsetting2 = (byte)ChargePumpSetting2SelectedIndex;
  byte CPGain = (byte)ChargePumpGainBoxSelectedIndex;
  byte CP3state = (byte)ChargePump3StateBoxSelectedIndex;
  byte Fastlock = (byte)FastLockBoxSelectedIndex;
  
  if (Fastlock==2) Fastlock++;
  
  //Cast more relevant integer values to bytes
  byte Timeout = (byte)TimeoutBoxSelectedIndex;
  byte PDPolarity = (byte)PhaseDetectorPolarityBoxSelectedIndex;
  byte CounterReset = (byte)CounterResetBoxSelectedIndex;
  byte LDP = (byte)LockDetectPrecisionBoxSelectedIndex;
  byte Powerdown = (byte)PowerDownBoxSelectedIndex;
  
  if (Powerdown==2) Powerdown++;
  
  //More relevant integer values to bytes
  byte ABPW = (byte)ABPWBoxSelectedIndex;
  byte Sync = (byte)SyncBoxSelectedIndex;
  byte Delay = (byte)DelayBoxSelectedIndex;
  byte Muxout = (byte)MuxoutBoxSelectedIndex;
  byte Testmodes = (byte)TestmodesBoxSelectedIndex;
  
  // Calculate the register values
  Reg[0] = ( pow(2,23)+pow(2, 20) + Testmodes*pow(2,16) + (R & 0x3FFF) * pow(2,2) );
  Reg[1] = ( CPGain * pow(2,21)+(B&0x1FFF)*pow(2,8)+(A&0x3F)*pow(2,2)+1);
  Reg[2] = ( Prescaler * pow(2,22)+CPsetting2*pow(2,18)+CPsetting1*pow(2,15)+Timeout*pow(2,11)+Fastlock*pow(2,9)+CP3state*pow(2,8)+PDPolarity*pow(2,7)+Muxout*pow(2,4)+Powerdown*pow(2,3)+CounterReset*pow(2,2)+2);
  
  // N0 is the low byte
  N0[i] = (byte)lowByte((long)Reg[1]);
    
  // Shift the value returned by calcRegisters by 8 bits to get the N1, middle byte
  N1[i] = (byte)lowByte((long)Reg[1] >> 8);
  
  // If this is the first iteration, the RCounter and Function Latch should be set
  if( i == 0){
     R0 = (byte)(lowByte((long)Reg[0])); // The LSB of the R Latch
     R1 = (byte)(lowByte((long)Reg[0] >> 8)); // The middle byte of the R Latch
     R2 = (byte)(lowByte((long)Reg[0] >> 16)); // The MSB of the R Latch

     F0 = (byte)(lowByte((long)Reg[2])); // The LSB of the Function Latch
     F1 = (byte)(lowByte((long)Reg[2] >> 8)); // The middle byte of the Function Latch
     F2 = (byte)(lowByte((long)Reg[2] >> 16)); // The MSB of the Function Latch
  }
  
  // R0A is the low byte of the R Latch
  R0A[i] = (byte)(lowByte((long)Reg[0]));
  
  // R1A is the middle byte of the R Latch
  R1A[i] = (byte)(lowByte((long)Reg[0] >> 8));
  
  // R1A is the high byte of the R Latch
  R2A[i] = (byte)(lowByte((long)Reg[0] >> 16));
    
}
  
//############################################################
// DELAY FUNCTION
//############################################################

void delayer(int delval ){
    if( DO_LONG_DELAY){
      delay(LONG_DELAY);
    }  
    delayMicroseconds(delval);
}

//############################################################
// WRITE 3 BYTES OVER SPI //
//############################################################
void SPIwrite24bitRegister(byte b23to16, byte b15to8, byte b7to0, boolean NewFrame) {
  
  // Transfer the first byte
  // This also serves as a timer for how long we keep
  // LE and SI high
  SPI.transfer(b23to16);
  SPI.transfer(b15to8);
  SPI.transfer(b7to0);
  
  // If NewFrame is true, we are at the beginning of a new set of
  // data, and both SI and LE must be high
  if(NewFrame)
    PORTD = B11000000;
  // Otherwise, we are mid-data transmit, and only LE must be high
  else
    PORTD = B10000000;
    
  delayMicroseconds(PULSE_DURATION);
  // After one byte transmission, set both LE and SI back low
  PORTD = B00000000;
  
  delayer(DWELL_TIME-PULSE_DURATION);
}

//############################################################
// MAIN LOOP //
//############################################################
void loop() {

  // Loop for the number of values in our frequency sweep
  for(int i = 0; i < sizeof(N0); i++) {
   
    if(i==0)
        SPIwrite24bitRegister(N2, N1[i], N0[i],true);
    else
        SPIwrite24bitRegister(N2, N1[i], N0[i],false);
  }
  
  SPIwrite24bitRegister(N2, N1[0], N0[0], false);
  delayer(SWEEP_PAUSE);
}
