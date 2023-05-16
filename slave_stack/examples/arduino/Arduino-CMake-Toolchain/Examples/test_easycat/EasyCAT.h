
#ifndef ABT_EasyCAT_H
#define ABT_EasyCAT_H

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



//---- EasyCAT library V_2_0 ------------------------------------------------------------	


// This library has been tested with Arduino IDE 1.8.5
// https://www.arduino.cc
 
 

//--- V_2.0 -----
//
// All the code moved in the .h file:
// this seems to be the only way to allow the library configuration
// through "#defines" in the ino. file
//
// New customization capability through the Easy Configurator tool 
// Fixed SPI optimization for SAMD


//--- V_1.5 ---
//
// Added support for 16+16, 32+32, 64+64 and 128+128 I/O bytes, through definition of 
// BYTE_NUM constant, in "EasyCAT.h"
// Added support for any number of bytes transferred, between 0 and 128, through definition 
// of CUST_BYTE_NUM_IN and CUST_BYTE_NUM_OUT constants, in "EasyCAT.h"
// Added support for DC and SM synchronization
// Aborted any possible pending transfer before accessing the process ram fifos
// Cleared all output bytes correctly, in the event of watchdog
// Fixed chip reset procedure
// Added parameter "Len" in function "SPIWriteRegisterIndirect"
// At init, print out chip identification, revision and number of bytes transferred


//--- V_1.4 ---
//
// The MainTask function now return the state of the 
// Ethercat State machine and of the Wachdog
// Now the SPI chip select is initialized High inside the constructor


//--- V_1.3 --- 
// Replaced delay(100) in Init() function with a wait loop 


//--- V_1.2 --- 
// SPI chip select is configured by the application, setting a constructor parameter.
// If no chip select is declared in the constructor, pin 9 will be used as default.
// Code cleaning.
// Comments in english.
// TODO: fast SPI transfert for DUE board


//--- V_1.1 --- 
// First official release.
// SPI chip select is configured editing the library ".h" file.


#include <Arduino.h> 
#include <SPI.h> 


//------ SPI configuration parameters --------------------------------------------

#define SPI_fast_transfer                 // enable fast SPI transfer (default)
#define SPI_fast_SS                       // enable fast SPI chip select management (default)
 
 
#if (!defined BYTE_NUM && !defined CUSTOM)// if BYTE_NUM is not declared in the .ino file 
  #define BYTE_NUM 32                     // set it to the 32+32 byte default 
#endif                                    // this is for compatibility with old applications 
 
 
//-- the preprocessor calculates the parameters necessary to transfer out data --- 

                                                    // define TOT_BYTE_NUM_OUT as the
                                                    // total number of byte we have to
                                                    // transfer in output
                                                    // this must match the ESI XML
                                                    //
#ifdef  BYTE_NUM                                    // in Standard Mode
  #define TOT_BYTE_NUM_OUT  BYTE_NUM                // 16, 32, 64 or 128
                                                    //
#else                                               // in Custom Mode  
  #define TOT_BYTE_NUM_OUT  CUST_BYTE_NUM_OUT       // any number between 0 and 128  
#endif                                              //


#if TOT_BYTE_NUM_OUT > 64                           // if we have more then  64 bytes
                                                    // we have to split the transfer in two
                                                        
  #define SEC_BYTE_NUM_OUT  (TOT_BYTE_NUM_OUT - 64) // number of bytes of the second transfer
                                        
  #if ((SEC_BYTE_NUM_OUT & 0x03) != 0x00)           // number of bytes of the second transfer
                                                    // rounded to long
    #define SEC_BYTE_NUM_ROUND_OUT  ((SEC_BYTE_NUM_OUT | 0x03) + 1)  
  #else                                             // 
    #define SEC_BYTE_NUM_ROUND_OUT  SEC_BYTE_NUM_OUT//
  #endif                                            //

  #define FST_BYTE_NUM_OUT  64                      // number of bytes of the first transfer     
  #define FST_BYTE_NUM_ROUND_OUT  64                // number of bytes of the first transfer
                                                    // rounded to 4 (long)

#else                                               // if we have max 64 bytes we transfer
                                                    // them in just one round
                                                        
  #define FST_BYTE_NUM_OUT  TOT_BYTE_NUM_OUT        // number of bytes of the first and only transfer 
  
  #if ((FST_BYTE_NUM_OUT & 0x03) != 0x00)           // number of bytes of the first and only transfer  
                                                    // rounded to 4 (long)   
    #define FST_BYTE_NUM_ROUND_OUT ((FST_BYTE_NUM_OUT | 0x03) + 1)
  #else                                             //
    #define FST_BYTE_NUM_ROUND_OUT  FST_BYTE_NUM_OUT//   
  #endif                                            //

  #define SEC_BYTE_NUM_OUT  0                       // we don't use the second round
  #define SEC_BYTE_NUM_ROUND_OUT  0                 //
  
#endif


//-- the preprocessor calculates the parameters necessary to transfer in data --- 
 
                                                    // define TOT_BYTE_NUM_IN as the
                                                    // total number of byte we have to
                                                    // transfer in input
                                                    // this must match the ESI XML
                                                    //     
#ifdef  BYTE_NUM                                    // in Standard Mode
  #define TOT_BYTE_NUM_IN  BYTE_NUM                 // 16, 32, 64 or 128
                                                    //
#else                                               // in Custom Mode
  #define TOT_BYTE_NUM_IN  CUST_BYTE_NUM_IN         // any number between 0 and 128  
#endif                                              //


#if TOT_BYTE_NUM_IN > 64                            // if we have more then  64 bytes
                                                    // we have to split the transfer in two
                                                        
  #define SEC_BYTE_NUM_IN  (TOT_BYTE_NUM_IN - 64)   // number of bytes of the second transfer
 
  #if ((SEC_BYTE_NUM_IN & 0x03) != 0x00)            // number of bytes of the second transfer 
                                                    // rounded to 4 (long)          
    #define SEC_BYTE_NUM_ROUND_IN  ((SEC_BYTE_NUM_IN | 0x03) + 1)  
  #else                                             //
    #define SEC_BYTE_NUM_ROUND_IN  SEC_BYTE_NUM_IN  //
  #endif                                            //

  #define FST_BYTE_NUM_IN  64                       // number of bytes of the first transfer     
  #define FST_BYTE_NUM_ROUND_IN  64                 // number of bytes of the first transfer
                                                    // rounded to 4 (long)

#else                                               // if we have max 64 bytes we transfer
                                                    // them in just one round
                                                        
  #define FST_BYTE_NUM_IN  TOT_BYTE_NUM_IN          // number of bytes of the first and only transfer  

  #if ((FST_BYTE_NUM_IN & 0x03) != 0x00)            // number of bytes of the first and only transfer
                                                    // rounded to 4 (long)
    #define FST_BYTE_NUM_ROUND_IN ((FST_BYTE_NUM_IN | 0x03) + 1)
  #else                                             //
    #define FST_BYTE_NUM_ROUND_IN  FST_BYTE_NUM_IN  // 
  #endif                                            //

  #define SEC_BYTE_NUM_IN  0                        // we don't use the second round
  #define SEC_BYTE_NUM_ROUND_IN  0                  //

#endif
 
//---------------------------------------------------------------------------------

//----------------- sanity check -------------------------------------------------------                                 
      

#ifdef BYTE_NUM                     // STANDARD MODE and CUSTOM MODE
                                    // cannot be defined at the same time
  #ifdef CUST_BYTE_NUM_OUT 
    #error "BYTE_NUM and CUST_BYTE_NUM_OUT cannot be defined at the same time !!!!"
    #error "define them correctly in file EasyCAT.h"
    #endif
  
  #ifdef CUST_BYTE_NUM_IN 
    #error "BYTE_NUM and CUST_BYTE_NUM_IN cannot be defined at the same time !!!!"
    #error "define them correctly in file EasyCAT.h"
  #endif
#endif 
      
#ifdef BYTE_NUM                     //--- for BYTE_NUM we accept only 16  32  64  128 --
                                  
  #if ((BYTE_NUM !=16) && (BYTE_NUM !=32) && (BYTE_NUM !=64)  && (BYTE_NUM !=128))
    #error "BYTE_NUM must be 16, 32, 64 or 128 !!! define it correctly in file EasyCAT.h"
  #endif 
  
#else
                                   //--- CUST_BYTE_NUM_OUT and CUST_BYTE_NUM_IN --------
                                   //    must be max 128
  #if (CUST_BYTE_NUM_OUT > 128)
    #error "CUST_BYTE_NUM_OUT must be max 128 !!! define it correctly in file EasyCAT.h"
  #endif 
  
  #if (CUST_BYTE_NUM_IN > 128)
    #error "CUST_BYTE_NUM_IN must be max 128 !!! define it correctly in file EasyCAT.h"
  #endif 
  
#endif 

  
//*************************************************************************************************


//---- LAN9252 registers --------------------------------------------------------------------------

                                            //---- access to EtherCAT registers -------------------

#define ECAT_CSR_DATA           0x0300      // EtherCAT CSR Interface Data Register
#define ECAT_CSR_CMD            0x0304      // EtherCAT CSR Interface Command Register


                                            //---- access to EtherCAT process RAM ----------------- 

#define ECAT_PRAM_RD_ADDR_LEN   0x0308      // EtherCAT Process RAM Read Address and Length Register
#define ECAT_PRAM_RD_CMD        0x030C      // EtherCAT Process RAM Read Command Register
#define ECAT_PRAM_WR_ADDR_LEN   0x0310      // EtherCAT Process RAM Write Address and Length Register 
#define ECAT_PRAM_WR_CMD        0x0314      // EtherCAT Process RAM Write Command Register

#define ECAT_PRAM_RD_DATA       0x0000      // EtherCAT Process RAM Read Data FIFO
#define ECAT_PRAM_WR_DATA       0x0020      // EtherCAT Process RAM Write Data FIFO

                                            //---- EtherCAT registers -----------------------------

#define AL_CONTROL              0x0120      // AL control                                             
#define AL_STATUS               0x0130      // AL status
#define AL_STATUS_CODE          0x0134      // AL status code
#define AL_EVENT                0x0220      // AL event request
#define AL_EVENT_MASK           0x0204      // AL event interrupt mask

#define WDOG_STATUS             0x0440      // watch dog status

#define SM0_BASE                0x0800      // SM0 base address (output)
#define SM1_BASE                0x0808      // SM1 base address (input) 


                                            //---- LAN9252 registers ------------------------------    

#define HW_CFG                  0x0074      // hardware configuration register
#define BYTE_TEST               0x0064      // byte order test register
#define RESET_CTL               0x01F8      // reset register       
#define ID_REV                  0x0050      // chip ID and revision
#define IRQ_CFG                 0x0054      // interrupt configuration
#define INT_EN                  0x005C      // interrupt enable


//---- LAN9252 flags ------------------------------------------------------------------------------

#define ECAT_CSR_BUSY     0x80
#define PRAM_ABORT        0x40000000
#define PRAM_BUSY         0x80
#define PRAM_AVAIL        0x01
#define READY             0x08
#define DIGITAL_RST       0x00000001


//---- EtherCAT flags -----------------------------------------------------------------------------

#define ALEVENT_CONTROL         0x0001
#define ALEVENT_SM              0x0010
 
 
//----- state machine ------------------------------------------------------------

#define ESM_INIT                  0x01          // state machine control
#define ESM_PREOP                 0x02          // (state request)
#define ESM_BOOT                  0x03          // 
#define ESM_SAFEOP                0x04          // safe-operational
#define ESM_OP                    0x08          // operational
    
    
//--- ESC commands --------------------------------------------------------------------------------

#define ESC_WRITE 		   0x80
#define ESC_READ 		     0xC0


//---- SPI ----------------------------------------------------------------------------------------

#define COMM_SPI_READ    0x03
#define COMM_SPI_WRITE   0x02

#define DUMMY_BYTE       0xFF


#if defined(ARDUINO_ARCH_AVR)  
  #define SpiSpeed         8000000
  
#elif defined (ARDUINO_ARCH_SAM)
  #define SpiSpeed        14000000 
  
#elif defined (ARDUINO_ARCH_SAMD)
  #define SpiSpeed        12000000   
  
#else  
  #define SpiSpeed        8000000    
#endif


//---- typedef ------------------------------------------------------------------------------------

typedef union
{
    unsigned short  Word;
    unsigned char   Byte[2];
} UWORD;

typedef union
{
    unsigned long   Long;
    unsigned short  Word[2];
    unsigned char   Byte[4];
} ULONG;


#ifdef BYTE_NUM                               // Input/Output buffers for Standard Mode    
                                            
  typedef struct								              //-- output buffer -----------------
  {											                      //			
    uint8_t  Byte [BYTE_NUM];                 //    
  } PROCBUFFER_OUT;							              //
                                            
  typedef struct                              //-- input buffer ------------------
  {											                      //
    uint8_t  Byte [BYTE_NUM];                 //     
  } PROCBUFFER_IN;                            //

#endif


typedef enum  
{
  ASYNC,
  DC_SYNC,
  SM_SYNC
}SyncMode;


//-------------------------------------------------------------------------------------------------
 
class EasyCAT 
{
  public:                                       
    EasyCAT();                              // default constructor
    EasyCAT(unsigned char SPI_CHIP_SELECT); 
    EasyCAT(SyncMode Sync);              
    EasyCAT(unsigned char SPI_CHIP_SELECT, SyncMode Sync);  
  
    unsigned char MainTask();               // EtherCAT main task
                                            // must be called cyclically by the application 
    
    bool Init();                            // EasyCAT board initialization 
    
    PROCBUFFER_OUT BufferOut;               // output process data buffer 
    PROCBUFFER_IN BufferIn;                 // input process data buffer    
  
  private:
    void SPIWriteRegisterDirect(unsigned short Address, unsigned long DataOut);
    unsigned long SPIReadRegisterDirect(unsigned short Address, unsigned char Len);
    
    void SPIWriteRegisterIndirect(unsigned long  DataOut, unsigned short Address, unsigned char Len);
    unsigned long SPIReadRegisterIndirect(unsigned short Address, unsigned char Len); 
    
    void SPIReadProcRamFifo();    
    void SPIWriteProcRamFifo();  
    
    unsigned char SCS; 
    
    SyncMode Sync_;

    #if defined(ARDUINO_ARCH_SAM)    
      Pio* pPort_SCS; 
      uint32_t Bit_SCS;      
    #endif

    #if defined(ARDUINO_ARCH_SAMD)    
      EPortType Port_SCS;
      uint32_t Bit_SCS;       
    #endif    
    
    #if defined(ARDUINO_ARCH_AVR)    
      unsigned char Mask_SCS;
      unsigned char Port_SCS;
      volatile uint8_t* pPort_SCS;       
    #endif        
    
 
 //----- fast SPI chip select management ----------------------------------------------------------
 
    #if defined SPI_fast_SS 

    
      #if defined(ARDUINO_ARCH_AVR)                   // -- AVR architecture (Uno - Mega) ---------
        #define SCS_Low_macro      *pPort_SCS &= ~(Mask_SCS);
        #define SCS_High_macro     *pPort_SCS |=  (Mask_SCS);       
  
      #elif defined(ARDUINO_ARCH_SAMD)                //--- SAMD architecture (Zero) --------------
        #define SCS_Low_macro      PORT->Group[Port_SCS].OUTCLR.reg = (1<<Bit_SCS);
        #define SCS_High_macro     PORT->Group[Port_SCS].OUTSET.reg = (1<<Bit_SCS);
            
      #elif defined(ARDUINO_ARCH_SAM)                 //---- SAM architecture (Due) ---------------
      	#define SCS_Low_macro      pPort_SCS->PIO_CODR = Bit_SCS;
        #define SCS_High_macro     pPort_SCS->PIO_SODR = Bit_SCS;

      #else                                    //-- standard management for others architectures -- 
        #define SCS_Low_macro      digitalWrite(SCS, LOW);
        #define SCS_High_macro     digitalWrite(SCS, HIGH);        

      #endif    
  
  
 //----- standard SPI chip select management ------------------------------------------------------

    #else     
      #define SCS_Low_macro     digitalWrite(SCS, LOW);
      #define SCS_High_macro    digitalWrite(SCS, HIGH);  
    #endif  
     
//-------------------------------------------------------------------------------------------------    
    
    
//----- fast SPI transfer ------------------------------------------------------------------------
    
  #if defined SPI_fast_transfer      

    #if defined(ARDUINO_ARCH_AVR)                     // -- AVR architecture (Uno - Mega) ---------

      inline static void SPI_TransferTx(unsigned char Data) {                             \
                                                            SPDR = Data;                  \
                                                            asm volatile("nop");        
                                                            while (!(SPSR & _BV(SPIF))) ; \
                                                            };               
          
      inline static void SPI_TransferTxLast(unsigned char Data) {                         \
                                                            SPDR = Data;                  \
                                                            asm volatile("nop");                                                                        
                                                            while (!(SPSR & _BV(SPIF))) ; \
                                                            };         
       
      inline static unsigned char SPI_TransferRx(unsigned char Data) {                      \
                                                              SPDR = Data;                  \
                                                              asm volatile("nop");          
                                                              while (!(SPSR & _BV(SPIF))) ; \
                                                              return SPDR; };    
      
    #elif defined(ARDUINO_ARCH_SAMD)                    //--- SAMD architecture (Zero) --------------
 
/*
      inline static void SPI_TransferTx (unsigned char Data){                                   \                                               
                                                    while(SERCOM4->SPI.INTFLAG.bit.DRE == 0){}; \
                                                    SERCOM4->SPI.DATA.bit.DATA = Data;}; 
                                                                                                    
      inline static void SPI_TransferTxLast(unsigned char Data){                                  \
                                                    while(SERCOM4->SPI.INTFLAG.bit.DRE == 0){};   \
                                                    SERCOM4->SPI.DATA.bit.DATA = Data;           \
                                                    while(SERCOM4->SPI.INTFLAG.bit.TXC == 0){}; }; 
                                                                                                                           
      inline static unsigned char SPI_TransferRx (unsigned char Data){                                \
                                                    unsigned char Dummy = SERCOM4->SPI.DATA.bit.DATA; \
                                                    while(SERCOM4->SPI.INTFLAG.bit.DRE == 0){};       \
                                                    SERCOM4->SPI.DATA.bit.DATA = Data;                \
                                                    while(SERCOM4->SPI.INTFLAG.bit.RXC == 0){};       \
                                                    return SERCOM4->SPI.DATA.bit.DATA;};              \
        */
        
      inline static void SPI_TransferTx          (unsigned char Data) {SPI.transfer(Data); };    
      inline static void SPI_TransferTxLast      (unsigned char Data) {SPI.transfer(Data); }; 
      inline static unsigned char SPI_TransferRx (unsigned char Data) {return SPI.transfer(Data); };        
          
        
        
    #elif defined(ARDUINO_ARCH_SAM)                   //---- SAM architecture (Due) ---------------   
                                                      // TODO! currently standard transfer is used  
       
        inline static void SPI_TransferTx          (unsigned char Data) {SPI.transfer(Data); };    
        inline static void SPI_TransferTxLast      (unsigned char Data) {SPI.transfer(Data); }; 
        inline static unsigned char SPI_TransferRx (unsigned char Data) {return SPI.transfer(Data); };        
       
       
    #else                                             //-- standard transfer for others architectures
    
      inline static void SPI_TransferTx            (unsigned char Data) {SPI.transfer(Data); };    
      inline static void SPI_TransferTxLast        (unsigned char Data) {SPI.transfer(Data); }; 
      inline static unsigned char SPI_TransferRx   (unsigned char Data) {return SPI.transfer(Data); }; 
 
    #endif        
      
//---- standard SPI transfer ---------------------------------------------------------------------  

  #else                              
      inline static void SPI_TransferTx          (unsigned char Data) {SPI.transfer(Data); };    
      inline static void SPI_TransferTxLast      (unsigned char Data) {SPI.transfer(Data); }; 
      inline static unsigned char SPI_TransferRx (unsigned char Data) {return SPI.transfer(Data); };         
  #endif       
 
//---------------------------------------------------------------------------------------- 
   
};

//*****************************************************************************
//*****************************************************************************
//*****************************************************************************
//*****************************************************************************  
//*****************************************************************************  
//*****************************************************************************  
  
  
//--------------------------------------------------------------------------------
// This is the code that usually is in the .cpp file.
// This seems to be the only way to allow the library 
// configuration through "#defines" in the ino. file  
//--------------------------------------------------------------------------------  
  
    
//---- constructors --------------------------------------------------------------------------------

EasyCAT::EasyCAT()                              //------- default constructor ---------------------- 
{                                               // 
  Sync_ = ASYNC;                                // if no synchronization mode is declared
                                                // ASYNC is the default
                                                //
  SCS = 9;                                      // if no chip select is declared 
  digitalWrite (SCS, HIGH);                     // pin 9 is the default
}                                               //


EasyCAT::EasyCAT(unsigned char SPI_CHIP_SELECT) //------- SPI_CHIP_SELECT options -----------------
                                                //
                                                // we can choose between:
                                                // 8, 9, 10, A5, 6, 7 
{                                               //                                       
  SCS = SPI_CHIP_SELECT;                        //  initialize chip select  
  digitalWrite (SCS, HIGH);                     //      
}
 

EasyCAT::EasyCAT(SyncMode Sync)                 //-------Synchronization options ---------------------- 
                                                //   
                                                // we can choose between:
                                                // ASYNC   = free running i.e. no synchronization
                                                //           between master and slave (default)   
                                                //
                                                // DC_SYNC = interrupt is generated from the
                                                //           Distributed clock unit
                                                //
                                                // SM_SYNC = interrupt is generated from the
                                                //           Syncronizatiom manager 2 
{                                               //
  Sync_ = Sync;                                 //                                           
                                                //                                        
  SCS = 9;                                      // default chip select is pin 9 
  digitalWrite (SCS, HIGH);                     //      
}                                              

                                                //-- Synchronization and chip select options -----  
EasyCAT::EasyCAT(unsigned char SPI_CHIP_SELECT, SyncMode Sync) 

{                                               //
  Sync_ = Sync;                                 //  
                                                //    
  SCS = SPI_CHIP_SELECT;                        //  
  digitalWrite (SCS, HIGH);                     //      
}                                               //  

  
//---- EasyCAT board initialization ---------------------------------------------------------------

bool EasyCAT::Init()
{
  #define Tout 1000
  
  ULONG TempLong;
  unsigned short i;
  
  SPI.begin();

  #if defined(ARDUINO_ARCH_SAMD)                          // calculate the microcontroller port and
    Bit_SCS = g_APinDescription[SCS].ulPin;               // pin for fast SPI chip select management     
    Port_SCS = g_APinDescription[SCS].ulPort;             //                                           
  #endif                                                  //  
                                                          //  
  #if defined(ARDUINO_ARCH_SAM)                           //    
    Bit_SCS = g_APinDescription[SCS].ulPin;               //    
    pPort_SCS = g_APinDescription[SCS].pPort;             //  
  #endif                                                  //
                                                          //
  #if defined(ARDUINO_ARCH_AVR)                           //
    Mask_SCS = (digitalPinToBitMask(SCS));                //  
    Port_SCS = digitalPinToPort(SCS);                     //
    pPort_SCS = portOutputRegister(Port_SCS);             //     
  #endif                                                  //
    
  digitalWrite(SCS, HIGH);
  pinMode(SCS, OUTPUT);   
  
  
  delay(100);     
                                                          // set SPI parameters
  SPI.beginTransaction(SPISettings(SpiSpeed, MSBFIRST, SPI_MODE0)); 
  
  SPIWriteRegisterDirect (RESET_CTL, DIGITAL_RST);        // LAN9252 reset 
   
  i = 0;                                                  // reset timeout 
  do                                                      // wait for reset to complete
  {                                                       //
    i++;                                                  //
    TempLong.Long = SPIReadRegisterDirect (RESET_CTL, 4); //
  }while (((TempLong.Byte[0] & 0x01) != 0x00) && (i != Tout));    
                                                          //                                                       
  if (i == Tout)                                          // time out expired      
  {                                                       //
    SPI.endTransaction();                                 //                        
    SPI.end();                                            //      
    return false;                                         // initialization failed  
  }                                                         
  
  i = 0;                                                  // reset timeout  
  do                                                      // check the Byte Order Test Register
  {                                                       //
    i++;                                                  //      
    TempLong.Long = SPIReadRegisterDirect (BYTE_TEST, 4); //
  }while ((TempLong.Long != 0x87654321) && (i != Tout));  //    
                                                          //                                                            
  if (i == Tout)                                          // time out expired      
  {                                                       //
    SPI.endTransaction();                                 //                        
    SPI.end();                                            //   
    return false;                                         // initialization failed  
  }            
  
  i = 0;                                                  // reset timeout  
  do                                                      // check the Ready flag
  {                                                       //
    i++;                                                  //    
    TempLong.Long = SPIReadRegisterDirect (HW_CFG, 4);    //
  }while (((TempLong.Byte[3] & READY) == 0) && (i != Tout));//
                                                          //
  if (i == Tout)                                          // time out expired      
  {                                                       //
    SPI.endTransaction();                                 //                        
    SPI.end();                                            //
    return false;                                         // initialization failed  
  }            
  
  
#ifdef BYTE_NUM
  Serial.println (F("STANDARD MODE")); 
#else
  Serial.println (F("CUSTOM MODE")); 
#endif

  Serial.print (TOT_BYTE_NUM_OUT);  
  Serial.println (F(" Byte Out"));    
  Serial.print (TOT_BYTE_NUM_IN);  
  Serial.println (F(" Byte In"));   

  Serial.print (F("Sync = "));    
                                                          
  if ((Sync_ == DC_SYNC) || (Sync_ == SM_SYNC))           //--- if requested, enable --------   
  {                                                       //--- interrupt generation -------- 
  
    if (Sync_ == DC_SYNC)
    {                                                     // enable interrupt from SYNC 0
      SPIWriteRegisterIndirect (0x00000004, AL_EVENT_MASK, 4);  
                                                          // in AL event mask register, and disable 
                                                          // other interrupt sources    
      Serial.println(F("DC_SYNC"));                                                      
    }                                                       
                                                                                                         
    else
    {                                                     
                                                          // enable interrupt from SM 0 event 
                                                          // (output synchronization manager)
      SPIWriteRegisterIndirect (0x00000100, AL_EVENT_MASK, 4);     
                                                          // in AL event mask register, and disable 
                                                          // other interrupt sources 
      Serial.println(F("SM_SYNC"));    
    }    
                                                         
    SPIWriteRegisterDirect (IRQ_CFG, 0x00000111);         // set LAN9252 interrupt pin driver  
                                                          // as push-pull active high
                                                          // (On the EasyCAT shield board the IRQ pin
                                                          // is inverted by a mosfet, so Arduino                                                        
                                                          // receives an active low signal)
                                                                        
    SPIWriteRegisterDirect (INT_EN, 0x00000001);          // enable LAN9252 interrupt      
  } 

  else
  {
    Serial.println(F("ASYNC"));
  }
  
  TempLong.Long = SPIReadRegisterDirect (ID_REV, 4);      // read the chip identification 
  Serial.print (F("Detected chip "));                     // and revision, and print it
  Serial.print (TempLong.Word[1], HEX);                   // out on the serial line
  Serial.print (F("  Rev "));                             //    
  Serial.println (TempLong.Word[0]);                      //  
  
  #ifdef DEB                                              // debug     
    Serial.println (F("\nBytes in OUT "));  
    
    Serial.println (TOT_BYTE_NUM_OUT);                      
    #ifdef CUSTOM
      Serial.println (TOT_BYTE_NUM_ROUND_OUT); 
    #else
      Serial.println (TOT_BYTE_NUM_OUT);     
    #endif
    Serial.println (FST_BYTE_NUM_OUT);                    
    Serial.println (FST_BYTE_NUM_ROUND_OUT);                       
    Serial.println (SEC_BYTE_NUM_OUT);                    
    Serial.println (SEC_BYTE_NUM_ROUND_OUT);                       
              
    Serial.println (F("\nBytes in IN "));  
    Serial.println (TOT_BYTE_NUM_IN);    
    #ifdef CUSTOM
      Serial.println (TOT_BYTE_NUM_ROUND_IN); 
    #else
      Serial.println (TOT_BYTE_NUM_IN);     
    #endif
    Serial.println (FST_BYTE_NUM_IN);                    
    Serial.println (FST_BYTE_NUM_ROUND_IN);                       
    Serial.println (SEC_BYTE_NUM_IN);                    
    Serial.println (SEC_BYTE_NUM_ROUND_IN);                       
    Serial.println ();                                
  #endif
    
  SPI.endTransaction();                               //
  return true;                                        // initalization completed   
}  


//---- EtherCAT task ------------------------------------------------------------------------------

unsigned char EasyCAT::MainTask()                    // must be called cyclically by the application

{
  bool WatchDog = 0;
  bool Operational = 0; 
  unsigned char i;
  ULONG TempLong; 
  unsigned char Status;  
                                                            // set SPI parameters
  SPI.beginTransaction(SPISettings(SpiSpeed, MSBFIRST, SPI_MODE0)); 
 
  TempLong.Long = SPIReadRegisterIndirect (WDOG_STATUS, 1); // read watchdog status
  if ((TempLong.Byte[0] & 0x01) == 0x01)                    //
    WatchDog = 0;                                           // set/reset the corrisponding flag
  else                                                      //
    WatchDog = 1;                                           //
    
  TempLong.Long = SPIReadRegisterIndirect (AL_STATUS, 1);   // read the EtherCAT State Machine status
  Status = TempLong.Byte[0] & 0x0F;                         // to see if we are in operational state
  if (Status == ESM_OP)                                     // 
    Operational = 1;                                        //
  else                                                      // set/reset the corrisponding flag
    Operational = 0;                                        //    


                                                            //--- process data transfert ----------
                                                            //                                                        
  if (WatchDog | !Operational)                              // if watchdog is active or we are 
  {                                                         // not in operational state, reset 
    for (i=0; i < TOT_BYTE_NUM_OUT ; i++)                   // the output buffer
    {                                                       //
      BufferOut.Byte[i] = 0;                                //
    }                                                       //
    
  #ifdef DEB                                                // debug
    if (!Operational)                                       //
      Serial.println("Not operational");                    //
    if (WatchDog)                                           //    
      Serial.println("WatchDog");                           //  
  #endif                                                    //
  }
  
  else                                                      
  {                                                         
    SPIReadProcRamFifo();                                   // otherwise transfer process data from 
  }                                                         // the EtherCAT core to the output buffer  
                 
  SPIWriteProcRamFifo();                                    // we always transfer process data from
                                                            // the input buffer to the EtherCAT core  
                                                            
  SPI.endTransaction();                                     //

  if (WatchDog)                                             // return the status of the State Machine      
  {                                                         // and of the watchdog
    Status |= 0x80;                                         //
  }                                                         //
  return Status;                                            //     
}

    
//---- read a directly addressable registers  -----------------------------------------------------

unsigned long EasyCAT::SPIReadRegisterDirect (unsigned short Address, unsigned char Len)

                                                   // Address = register to read
                                                   // Len = number of bytes to read (1,2,3,4)
                                                   //
                                                   // a long is returned but only the requested bytes
                                                   // are meaningful, starting from LsByte                                                 
{
  ULONG Result; 
  UWORD Addr;
  Addr.Word = Address; 
  unsigned char i; 
  
  SCS_Low_macro                                             // SPI chip select enable

  SPI_TransferTx(COMM_SPI_READ);                            // SPI read command
  SPI_TransferTx(Addr.Byte[1]);                             // address of the register
  SPI_TransferTxLast(Addr.Byte[0]);                         // to read, MsByte first
 
  for (i=0; i<Len; i++)                                     // read the requested number of bytes
  {                                                         // LsByte first 
    Result.Byte[i] = SPI_TransferRx(DUMMY_BYTE);            //
  }                                                         //    
  
  SCS_High_macro                                            // SPI chip select disable 
 
  return Result.Long;                                       // return the result
}


//---- write a directly addressable registers  ----------------------------------------------------

void EasyCAT::SPIWriteRegisterDirect (unsigned short Address, unsigned long DataOut)

                                                   // Address = register to write
                                                   // DataOut = data to write
{ 
  ULONG Data; 
  UWORD Addr;
  Addr.Word = Address;
  Data.Long = DataOut;    

  
  SCS_Low_macro                                             // SPI chip select enable  
  
  SPI_TransferTx(COMM_SPI_WRITE);                           // SPI write command
  SPI_TransferTx(Addr.Byte[1]);                             // address of the register
  SPI_TransferTx(Addr.Byte[0]);                             // to write MsByte first

  SPI_TransferTx(Data.Byte[0]);                             // data to write 
  SPI_TransferTx(Data.Byte[1]);                             // LsByte first
  SPI_TransferTx(Data.Byte[2]);                             //
  SPI_TransferTxLast(Data.Byte[3]);                         //
 
  SCS_High_macro                                            // SPI chip select enable   
}


//---- read an indirectly addressable registers  --------------------------------------------------

unsigned long EasyCAT::SPIReadRegisterIndirect (unsigned short Address, unsigned char Len)

                                                   // Address = register to read
                                                   // Len = number of bytes to read (1,2,3,4)
                                                   //
                                                   // a long is returned but only the requested bytes
                                                   // are meaningful, starting from LsByte                                                  
{
  ULONG TempLong;
  UWORD Addr;
  Addr.Word = Address;
                                                            // compose the command
                                                            //
  TempLong.Byte[0] = Addr.Byte[0];                          // address of the register
  TempLong.Byte[1] = Addr.Byte[1];                          // to read, LsByte first
  TempLong.Byte[2] = Len;                                   // number of bytes to read
  TempLong.Byte[3] = ESC_READ;                              // ESC read 

  SPIWriteRegisterDirect (ECAT_CSR_CMD, TempLong.Long);     // write the command

  do
  {                                                         // wait for command execution
    TempLong.Long = SPIReadRegisterDirect(ECAT_CSR_CMD,4);  //
  }                                                         //
  while(TempLong.Byte[3] & ECAT_CSR_BUSY);                  //
                                                             
                                                              
  TempLong.Long = SPIReadRegisterDirect(ECAT_CSR_DATA,Len); // read the requested register
  return TempLong.Long;                                     //
}


//---- write an indirectly addressable registers  -------------------------------------------------

void  EasyCAT::SPIWriteRegisterIndirect (unsigned long DataOut, unsigned short Address, unsigned char Len)

                                                   // Address = register to write
                                                   // DataOut = data to write                                                    
{
  ULONG TempLong;
  UWORD Addr;
  Addr.Word = Address;

  
  SPIWriteRegisterDirect (ECAT_CSR_DATA, DataOut);            // write the data

                                                              // compose the command
                                                              //                                
  TempLong.Byte[0] = Addr.Byte[0];                            // address of the register  
  TempLong.Byte[1] = Addr.Byte[1];                            // to write, LsByte first
  TempLong.Byte[2] = Len;                                     // number of bytes to write
  TempLong.Byte[3] = ESC_WRITE;                               // ESC write

  SPIWriteRegisterDirect (ECAT_CSR_CMD, TempLong.Long);       // write the command

  do                                                          // wait for command execution
  {                                                           //
    TempLong.Long = SPIReadRegisterDirect (ECAT_CSR_CMD, 4);  //  
  }                                                           //  
  while (TempLong.Byte[3] & ECAT_CSR_BUSY);                   //
}


//---- read from process ram fifo ----------------------------------------------------------------

void EasyCAT::SPIReadProcRamFifo()    // read data from the output process ram, through the fifo
                                      //        
                                      // these are the bytes received from the EtherCAT master and
                                      // that will be use by our application to write the outputs
{
  ULONG TempLong;
  unsigned char i;
  
  
  #if TOT_BYTE_NUM_OUT > 0

    SPIWriteRegisterDirect (ECAT_PRAM_RD_CMD, PRAM_ABORT);        // abort any possible pending transfer

  
    SPIWriteRegisterDirect (ECAT_PRAM_RD_ADDR_LEN, (0x00001000 | (((uint32_t)TOT_BYTE_NUM_OUT) << 16)));   
                                                                  // the high word is the num of bytes
                                                                  // to read 0xTOT_BYTE_NUM_OUT----
                                                                  // the low word is the output process        
                                                                  // ram offset 0x----1000 

    SPIWriteRegisterDirect (ECAT_PRAM_RD_CMD, 0x80000000);        // start command        
 
 
                                                //------- one round is enough if we have ----
                                                //------- to transfer up to 64 bytes --------
   
    do                                                            // wait for the data to be       
    {                                                             // transferred from the output  
      TempLong.Long = SPIReadRegisterDirect (ECAT_PRAM_RD_CMD,2); // process ram to the read fifo       
    }                                                             //    
    while (TempLong.Byte[1] != (FST_BYTE_NUM_ROUND_OUT/4));       // *CCC* 
  
    SCS_Low_macro                                                 // enable SPI chip select 
  
    SPI_TransferTx(COMM_SPI_READ);                                // SPI read command
    SPI_TransferTx(0x00);                                         // address of the read  
    SPI_TransferTxLast(0x00);                                     // fifo MsByte first
  
    for (i=0; i< FST_BYTE_NUM_ROUND_OUT; i++)                     // transfer the data
    {                                                             //
      BufferOut.Byte[i] = SPI_TransferRx(DUMMY_BYTE);             //
    }                                                             //
    
    SCS_High_macro                                                // disable SPI chip select    
  #endif  

  
  #if SEC_BYTE_NUM_OUT > 0                    //-- if we have to transfer more then 64 bytes --
                                              //-- we must do another round -------------------
                                              //-- to transfer the remainig bytes -------------


    do                                                          // wait for the data to be       
    {                                                           // transferred from the output  
      TempLong.Long = SPIReadRegisterDirect(ECAT_PRAM_RD_CMD,2);// process ram to the read fifo 
    }                                                           //    
    while (TempLong.Byte[1] != SEC_BYTE_NUM_ROUND_OUT/4);       // *CCC*  

    SCS_Low_macro                                               // enable SPI chip select   
    
    SPI_TransferTx(COMM_SPI_READ);                              // SPI read command
    SPI_TransferTx(0x00);                                       // address of the read  
    SPI_TransferTxLast(0x00);                                   // fifo MsByte first
    
    for (i=0; i< (SEC_BYTE_NUM_ROUND_OUT); i++)                 // transfer loop for the remaining 
    {                                                           // bytes
      BufferOut.Byte[i+64] = SPI_TransferRx(DUMMY_BYTE);        // we transfer the second part of
    }                                                           // the buffer, so offset by 64
      
    SCS_High_macro                                              // SPI chip select disable  
  #endif  
}  


//---- write to the process ram fifo --------------------------------------------------------------

void EasyCAT::SPIWriteProcRamFifo()    // write data to the input process ram, through the fifo
                                       //    
                                       // these are the bytes that we have read from the inputs of our                   
                                       // application and that will be sent to the EtherCAT master
{
  ULONG TempLong;
  unsigned char i;
  
  
  #if TOT_BYTE_NUM_IN > 0  
  
    SPIWriteRegisterDirect (ECAT_PRAM_WR_CMD, PRAM_ABORT);        // abort any possible pending transfer
  
 
    SPIWriteRegisterDirect (ECAT_PRAM_WR_ADDR_LEN, (0x00001200 | (((uint32_t)TOT_BYTE_NUM_IN) << 16)));   
                                                                  // the high word is the num of bytes
                                                                  // to write 0xTOT_BYTE_NUM_IN----
                                                                  // the low word is the input process        
                                                                  // ram offset  0x----1200
                                                                                               
    SPIWriteRegisterDirect (ECAT_PRAM_WR_CMD, 0x80000000);        // start command  
  
  
                                                //------- one round is enough if we have ----
                                                //------- to transfer up to 64 bytes --------
    
    do                                                            // check that the fifo has      
    {                                                             // enough free space 
      TempLong.Long = SPIReadRegisterDirect (ECAT_PRAM_WR_CMD,2); //  
    }                                                             //  
    while (TempLong.Byte[1] <   (FST_BYTE_NUM_ROUND_IN/4));       //    *CCC*
  
    SCS_Low_macro                                                 // enable SPI chip select
  
    SPI_TransferTx(COMM_SPI_WRITE);                               // SPI write command
    SPI_TransferTx(0x00);                                         // address of the write fifo 
    SPI_TransferTx(0x20);                                         // MsByte first 

    for (i=0; i< (FST_BYTE_NUM_ROUND_IN - 1 ); i++)               // transfer the data
    {                                                             //
      SPI_TransferTx (BufferIn.Byte[i]);                          //      
    }                                                             //
                                                                  //  
    SPI_TransferTxLast (BufferIn.Byte[i]);                        // one last byte
  
    SCS_High_macro                                                // disable SPI chip select           
  #endif        

  
  #if SEC_BYTE_NUM_IN > 0                     //-- if we have to transfer more then 64 bytes --
                                              //-- we must do another round -------------------
                                              //-- to transfer the remainig bytes -------------

    do                                                          // check that the fifo has     
    {                                                           // enough free space       
      TempLong.Long = SPIReadRegisterDirect(ECAT_PRAM_WR_CMD,2);// 
    }                                                           //  
    while (TempLong.Byte[1] < (SEC_BYTE_NUM_ROUND_IN/4));       //   *CCC*
                             
    SCS_Low_macro                                               // enable SPI chip select
    
    SPI_TransferTx(COMM_SPI_WRITE);                             // SPI write command
    SPI_TransferTx(0x00);                                       // address of the write fifo 
    SPI_TransferTx(0x20);                                       // MsByte first 

    for (i=0; i< (SEC_BYTE_NUM_ROUND_IN - 1); i++)              // transfer loop for the remaining 
    {                                                           // bytes
      SPI_TransferTx (BufferIn.Byte[i+64]);                     // we transfer the second part of
    }                                                           // the buffer, so offset by 64
                                                                //  
    SPI_TransferTxLast (BufferIn.Byte[i+64]);                   // one last byte  

    SCS_High_macro                                              // disable SPI chip select    
  #endif     
}

#endif
