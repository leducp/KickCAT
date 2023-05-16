//********************************************************************************************
//                                                                                           *
// AB&T Tecnologie Informatiche - Ivrea Italy                                                *
// http://www.bausano.net                                                                    *
// https://www.ethercat.org/en/products/791FFAA126AD43859920EA64384AD4FD.htm                 *
//                                                                                           *
//********************************************************************************************

//********************************************************************************************
//                                                                                           *
// This software is distributed as an example, "AS IS", in the hope that it could            *
// be useful, WITHOUT ANY WARRANTY of any kind, express or implied, included, but            *
// not limited,  to the warranties of merchantability, fitness for a particular              *
// purpose, and non infringiment. In no event shall the authors be liable for any            *
// claim, damages or other liability, arising from, or in connection with this software.     *
//                                                                                           *
//********************************************************************************************



//---- AB&T EasyCAT shield application example V.2_0 -------------------------------------------



#include "EasyCAT.h"                // EasyCAT library to interface the LAN9252
#include <SPI.h>                    // SPI library


EasyCAT EASYCAT;                    // EasyCAT istantiation

                                    // The constructor allow us to choose the pin used for the EasyCAT SPI chip select
                                    // Without any parameter pin 9 will be used

                                    // We can choose between:
                                    // 8, 9, 10, A5, 6, 7

                                    // On the EasyCAT board the SPI chip select is selected through a bank of jumpers

                                    // (The EasyCAT board REV_A allows only pins 8, 9, 10 through 0 ohm resistors)

 //EasyCAT EASYCAT(8);              // example:
                                    // pin 8 will be used as SPI chip select
                                    // The chip select chosen by the firmware must match the setting on the board


//---- pins declaration ------------------------------------------------------------------------------

const int Ana0 = A0;                // analog input 0
const int Ana1 = A1;                // analog input 1

const int BitOut0 = A2;             // digital output bit 0
const int BitOut1 = A3;             // digital output bit 1
const int BitOut2 = A4;             // digital output bit 2
const int BitOut3 = A5;             // digital output bit 3

const int BitIn0 = 3;               // digital input  bit 0
const int BitIn1 = 5;               // digital input  bit 1
const int BitIn2 = 6;               // digital input  bit 2
const int BitIn3 = 7;               // digital input  bit 3



//---- global variables ---------------------------------------------------------------------------


UWORD ContaUp;                      // used for sawthoot test generation
UWORD ContaDown;                    //

unsigned long Millis = 0;
unsigned long PreviousMillis = 0;
unsigned long PreviousSaw = 0;
unsigned long PreviousCycle = 0;


//---- setup ---------------------------------------------------------------------------------------

void setup()
{
  Serial.begin(9600);                                             // serial line initialization
                                                                  //(used only for debug)

  Serial.print ("\nEasyCAT - Generic EtherCAT slave\n");          // print the banner

  pinMode(BitOut0, OUTPUT);                                       // digital output pins setting
  pinMode(BitOut1, OUTPUT);                                       //
  pinMode(BitOut2, OUTPUT);                                       //
  pinMode(BitOut3, OUTPUT);                                       //

  pinMode(BitIn0, INPUT_PULLUP);                                  // digital input pins setting
  pinMode(BitIn1, INPUT_PULLUP);                                  //
  pinMode(BitIn2, INPUT_PULLUP);                                  //
  pinMode(BitIn3, INPUT_PULLUP);                                  //

  ContaUp.Word = 0x0000;                                          //
  ContaDown.Word = 0x0000;                                        //

                                                                  //---- initialize the EasyCAT board -----

  if (EASYCAT.Init() == true)                                     // initialization
  {                                                               // succesfully completed
    Serial.print ("initialized");                                 //
  }                                                               //

  else                                                            // initialization failed
  {                                                               // the EasyCAT board was not recognized
    Serial.print ("initialization failed");                       //
                                                                  // The most common reason is that the SPI
                                                                  // chip select choosen on the board doesn't
                                                                  // match the one choosen by the firmware

    pinMode(13, OUTPUT);                                          // stay in loop for ever
                                                                  // with the Arduino led blinking
    while(1)                                                      //
    {                                                             //
      digitalWrite (13, LOW);                                     //
      delay(500);                                                 //
      digitalWrite (13, HIGH);                                    //
      delay(500);                                                 //
    }                                                             //
  }

  PreviousMillis = millis();
}

//---- user application ------------------------------------------------------------------------------

void Application ()

{
  UWORD Analog0;
  UWORD Analog1;

  Millis = millis();                                    // As an example for this application
  if (Millis - PreviousMillis >= 10)                    // we choose a cycle time of 10 mS
  {                                                     //
    PreviousMillis = Millis;                            //

                                                        // --- analog inputs management ---

    Analog0.Word = analogRead(Ana0);                    // read analog input 0
    Analog0.Word >>= 2;                                 // normalize it on 8 bits
    EASYCAT.BufferIn.Byte[0] = Analog0.Byte[0];         // and put the result into
                                                        // input Byte 0

    Analog1.Word = analogRead(Ana1);                    // read analog input 1
    Analog1.Word >>= 2;                                 // normalize it on 8 bits
    EASYCAT.BufferIn.Byte[1] = Analog1.Byte[0];         // and put the result into
                                                        // input Byte 1

                                                        // --- four output bits management ----
                                                        //
    if (EASYCAT.BufferOut.Byte[0] & (1<<0))             // the four output bits are mapped to the
      digitalWrite (BitOut0, HIGH);                     // lower nibble of output Byte 0
    else                                                //
      digitalWrite (BitOut0, LOW);                      // we read each bit and write it
                                                        // to the corrisponding pin
    if (EASYCAT.BufferOut.Byte[0] & (1<<1))             //
      digitalWrite (BitOut1, HIGH);                     //
    else                                                //
      digitalWrite (BitOut1, LOW);                      //
                                                        //
    if (EASYCAT.BufferOut.Byte[0] & (1<<2))             //
      digitalWrite (BitOut2, HIGH);                     //
    else                                                //
      digitalWrite (BitOut2, LOW);                      //
                                                        //
    if (EASYCAT.BufferOut.Byte[0] & (1<<3))             //
      digitalWrite (BitOut3, HIGH);                     //
    else                                                //
      digitalWrite (BitOut3, LOW);                      //

                                                        //--- four input bits management ---
                                                        //
    if (digitalRead(BitIn0))                            // the four input pins are mapped to the
      EASYCAT.BufferIn.Byte[6] |= (1<<0);               // lower nibble of input Byte 6
    else                                                //
      EASYCAT.BufferIn.Byte[6] &= ~(1<<0);              // we read each pin and write it
                                                        // to the corresponding bit
    if (digitalRead(BitIn1))                            //
      EASYCAT.BufferIn.Byte[6] |= (1<<1);               //
    else                                                //
      EASYCAT.BufferIn.Byte[6] &= ~(1<<1);              //
                                                        //
    if (digitalRead(BitIn2))                            //
      EASYCAT.BufferIn.Byte[6] |= (1<<2);               //
    else                                                //
      EASYCAT.BufferIn.Byte[6] &= ~(1<<2);              //
                                                        //
    if (digitalRead(BitIn3))                            //
      EASYCAT.BufferIn.Byte[6] |= (1<<3);               //
    else                                                //
      EASYCAT.BufferIn.Byte[6] &= ~(1<<3);              //

                                                        // --- test sawtooth generation ---
                                                        //
    Millis = millis();                                  // each 100 mS
    if (Millis - PreviousSaw >= 100)                    //
    {                                                   //
      PreviousSaw = Millis;                             //
                                                        //
      ContaUp.Word++;                                   // we increment the variable ContaUp
      ContaDown.Word--;                                 // and decrement ContaDown
    }                                                   //

                                                        // we use these variables to create sawtooth,
                                                        // with different slopes and periods, for
                                                        // test pourpose, in input Bytes 2,3,4,5,30,31

    EASYCAT.BufferIn.Byte[2] = ContaUp.Byte[0];         // slow rising slope
    EASYCAT.BufferIn.Byte[3] = ContaUp.Byte[1];         // extremly slow rising slope

    EASYCAT.BufferIn.Byte[4] = ContaDown.Byte[0];       // slow falling slope
    EASYCAT.BufferIn.Byte[5] = ContaDown.Byte[1];       // extremly slow falling slope

    EASYCAT.BufferIn.Byte[30] = ContaUp.Byte[0] << 2;   // medium speed rising slope
    EASYCAT.BufferIn.Byte[31] = ContaDown.Byte[0] << 2; // medium speed falling slope
  }
}

//---- main loop ----------------------------------------------------------------------------------------

void loop()                                             // In the main loop we must call ciclically the
{                                                       // EasyCAT task and our application
                                                        //
                                                        // This allows the bidirectional exachange of the data
                                                        // between the EtherCAT master and our application
                                                        //
                                                        // The EasyCAT cycle and the Master cycle are asynchronous
                                                        //

  EASYCAT.MainTask();                                   // execute the EasyCAT task

  Application();                                        // user applications
}




