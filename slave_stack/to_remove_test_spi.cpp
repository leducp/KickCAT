
#include <SPI.h>

// LAN9252 Commands:
unsigned short CMD_SPI_READ = 0x03;
unsigned short CMD_SPI_WRITE = 0x02;
unsigned short BYTE_TEST = 0x64;

unsigned char DUMMY_BYTE = 0xFF;


unsigned int SCS = 9; //default pin for chip select.

SPISettings settings(8000000, MSBFIRST, SPI_MODE0);


// LAN9252 registers
unsigned short PRAM_READ_LEN = 0x308;
unsigned short RESET_CTL     = 0x01F8;      // reset register
unsigned short ECAT_CSR_DATA = 0x0300;      // EtherCAT CSR Interface Data Register
unsigned short ECAT_CSR_CMD  = 0x0304;      // EtherCAT CSR Interface Command Register


// LAN9252 flags
unsigned short ECAT_CSR_BUSY = 0x80;
unsigned PRAM_ABORT    = 0x40000000;
unsigned short PRAM_BUSY     = 0x80;
unsigned short PRAM_AVAIL    = 0x01;
unsigned short READY         = 0x08;
unsigned short DIGITAL_RST   = 0x00000001;



//--- ESC commands --------------------------------------------------------------------------------

unsigned short ESC_WRITE = 0x80;
unsigned short ESC_READ  = 0xC0;


//---- access to EtherCAT process RAM -----------------

unsigned short ECAT_PRAM_RD_ADDR_LEN =  0x0308;      // EtherCAT Process RAM Read Address and Length Register
unsigned short ECAT_PRAM_RD_CMD      =  0x030C;      // EtherCAT Process RAM Read Command Register
unsigned short ECAT_PRAM_WR_ADDR_LEN =  0x0310;      // EtherCAT Process RAM Write Address and Length Register
unsigned short ECAT_PRAM_WR_CMD      =  0x0314;      // EtherCAT Process RAM Write Command Register

unsigned short ECAT_PRAM_RD_DATA     =  0x0000;      // EtherCAT Process RAM Read Data FIFO
unsigned short ECAT_PRAM_WR_DATA     =  0x0020;      // EtherCAT Process RAM Write Data FIFO

// Ethercat registers

unsigned short AL_CONTROL          =   0x0120;      // AL control
unsigned short AL_STATUS           =   0x0130;      // AL status
unsigned short AL_STATUS_CODE      =   0x0134;      // AL status code
unsigned short AL_EVENT            =   0x0220;      // AL event request
unsigned short AL_EVENT_MASK       =   0x0204;      // AL event interrupt mask

unsigned short WDOG_STATUS         =   0x0440;      // watch dog status

unsigned short SM0_BASE            =   0x0800;      // SM0 base address (output)
unsigned short SM1_BASE            =   0x0808;      // SM1 base address (input)


constexpr uint16_t EEPROM_CONFIG  = 0x500;
constexpr uint16_t EEPROM_PDI     = 0x501;
constexpr uint16_t EEPROM_CONTROL = 0x502; // 2 bytes
constexpr uint16_t EEPROM_ADDRESS = 0x504; // 4 bytes
constexpr uint16_t EEPROM_DATA    = 0x508; // 8 bytes

//----- state machine ------------------------------------------------------------

unsigned short ESM_INIT   =               0x01;          // state machine control
unsigned short ESM_PREOP  =               0x02;          // (state request)
unsigned short ESM_BOOT   =               0x03;          //
unsigned short ESM_SAFEOP =               0x04;          // safe-operational
unsigned short ESM_OP     =               0x08;          // operational



// EEPROM content
constexpr uint16_t ESC_INFO            = 0x00;
constexpr uint16_t ESC_PDI_CONTROL     = 0;
constexpr uint16_t ESC_PDI_CONFIG      = 1;
constexpr uint16_t ESC_SYNC_IMPULSE    = 2;
constexpr uint16_t ESC_PDI_CONFIG_2    = 3;

constexpr uint16_t ESC_STATION_ALIAS   = 0x04;
constexpr uint16_t ESC_CRC             = 0x07;

constexpr uint16_t VENDOR_ID           = 0x8;
constexpr uint16_t PRODUCT_CODE        = 0xA;
constexpr uint16_t REVISION_NUMBER     = 0xC;
constexpr uint16_t SERIAL_NUMBER       = 0xE;

constexpr uint16_t BOOTSTRAP_MAILBOX   = 0x14;
constexpr uint16_t STANDARD_MAILBOX    = 0x18;
constexpr uint16_t RECV_MBO_OFFSET     = 0;
constexpr uint16_t RECV_MBO_SIZE       = 1;
constexpr uint16_t SEND_MBO_OFFSET     = 2;
constexpr uint16_t SEND_MBO_SIZE       = 3;
constexpr uint16_t MAILBOX_PROTOCOL    = 0x1C;

constexpr uint16_t EEPROM_SIZE         = 0x3E;
constexpr uint16_t EEPROM_VERSION      = 0x3F;

constexpr uint16_t START_CATEGORY      = 0x40;



enum EepromCommand : uint16_t
{
    NOP    = 0x0000,  // clear error bits
    READ   = 0x0100,
    WRITE  = 0x0201,
    RELOAD = 0x0300
};





typedef union
{
    unsigned long   Long;
    unsigned short  Word[2];
    unsigned char   Byte[4];
} ULONG;

typedef union
{
    unsigned short  Word;
    unsigned char   Byte[2];
} UWORD;

uint8_t const PDO_SIZE = 32;
uint8_t  BufferOut [PDO_SIZE];
uint8_t  BufferIn [PDO_SIZE];

unsigned long SPIReadRegisterDirect (unsigned short Address, unsigned char Len)

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

  digitalWrite(SCS, LOW);                                             // SPI chip select enable

  SPI.transfer(CMD_SPI_READ);
  SPI.transfer(Addr.Byte[1]);                             // address of the register
  SPI.transfer(Addr.Byte[0]);                            // to read, MsByte first

  for (i=0; i<Len; i++)                                     // read the requested number of bytes
  {                                                         // LsByte first
    Result.Byte[i] = SPI.transfer(DUMMY_BYTE);
  }

  digitalWrite(SCS, HIGH);                                            // SPI chip select disable

  return Result.Long;                                       // return the result
}


void SPIWriteRegisterDirect (unsigned short Address, unsigned long DataOut)
                                                   // Address = register to write
                                                   // DataOut = data to write
{
  ULONG Data;
  UWORD Addr;
  Addr.Word = Address;
  Data.Long = DataOut;

  digitalWrite(SCS, LOW);                                             // SPI chip select enable
  SPI.transfer(CMD_SPI_WRITE);
  SPI.transfer(Addr.Byte[1]);
  SPI.transfer(Addr.Byte[0]);

  SPI.transfer(Data.Byte[0]);
  SPI.transfer(Data.Byte[1]);
  SPI.transfer(Data.Byte[2]);
  SPI.transfer(Data.Byte[3]);


  digitalWrite(SCS, HIGH);                                            // SPI chip select disable
}


unsigned long SPIReadRegisterIndirect (unsigned short Address, unsigned char Len)

                                                   // Address = register to read
                                                   // Len = number of bytes to read (1,2,3,4)
                                                   //
                                                   // a long is returned but only the requested bytes
                                                   // are meaningful, starting from LsByte
{
  ULONG TempLong;
  do                                                          // Check the ECS is not busy.
  {                                                           //
    TempLong.Long = SPIReadRegisterDirect (ECAT_CSR_CMD, 4);  //
  }                                                           //
  while (TempLong.Byte[3] & ECAT_CSR_BUSY);                   //

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


void  SPIWriteRegisterIndirect (unsigned long DataOut, unsigned short Address, unsigned char Len)

                                                   // Address = register to write
                                                   // DataOut = data to write
                                                   // Len = 1,2 or 4 bytes.
{
  ULONG TempLong;
  do                                                          // Check the ECS is not busy.
  {                                                           //
    TempLong.Long = SPIReadRegisterDirect (ECAT_CSR_CMD, 4);  //
  }                                                           //
  while (TempLong.Byte[3] & ECAT_CSR_BUSY);                   //



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

void SPIReadProcRamFifo()    // read data from the output process ram, through the fifo
                                      //
                                      // these are the bytes received from the EtherCAT master and
                                      // that will be use by our application to write the outputs
{
  ULONG TempLong;
  unsigned char i;



  SPIWriteRegisterDirect (ECAT_PRAM_RD_CMD, PRAM_ABORT);        // abort any possible pending transfer

  SPIWriteRegisterDirect (ECAT_PRAM_RD_ADDR_LEN, (0x00001000 | (((uint32_t)PDO_SIZE) << 16)));
                                                                  // the high word is the num of bytes
                                                                  // to read 0xTOT_BYTE_NUM_OUT----
                                                                  // the low word is the output process
                                                                  // ram offset 0x----1000

  SPIWriteRegisterDirect (ECAT_PRAM_RD_CMD, 0x80000000);        // start command

                                                //------- one round is enough if we have ----
                                                //------- to transfer up to 64 bytes --------
  do                                                            // wait for the data to be
  {                                                             // transferred from the output
    TempLong.Long = SPIReadRegisterDirect (ECAT_PRAM_RD_CMD, 2); // process ram to the read fifo
  }
  // Number of available 32 bits in PRAM_RD_DATA
  while (TempLong.Byte[1] != PDO_SIZE / 4);     //Will read by chunk of 8 bits (sizeof(DUMMY_BYTE), for PDO_size, and  PRAM_RD_DATA is 32 bits. So each count is equivalent to 4 read available in the data.

  digitalWrite(SCS, LOW);                                                 // enable SPI chip select

  SPI.transfer(CMD_SPI_READ);                                // SPI read command
  SPI.transfer(0x00);                                         // address of the read fifo MsByte first
  SPI.transfer(0x00);

  for (i=0; i< PDO_SIZE; i++)                     // transfer the data
  {                                                             //
    BufferOut[i] = SPI.transfer(DUMMY_BYTE);             //
  }                                                             //

  digitalWrite(SCS, HIGH);                                              // disable SPI chip select
}


//---- write to the process ram fifo --------------------------------------------------------------

void SPIWriteProcRamFifo()    // write data to the input process ram, through the fifo
                                       //
                                       // these are the bytes that we have read from the inputs of our
                                       // application and that will be sent to the EtherCAT master
{
  ULONG TempLong;
  unsigned char i;

  SPIWriteRegisterDirect (ECAT_PRAM_WR_CMD, PRAM_ABORT);        // abort any possible pending transfer


  SPIWriteRegisterDirect (ECAT_PRAM_WR_ADDR_LEN, (0x00001200 | (((uint32_t)PDO_SIZE) << 16)));
                                                                // the high word is the num of bytes
                                                                // to write 0xTOT_BYTE_NUM_IN----
                                                                // the low word is the input process
                                                                // ram offset  0x----1200

  SPIWriteRegisterDirect (ECAT_PRAM_WR_CMD, 0x80000000);        // start command


                                              //------- one round is enough if we have ----
                                              //------- to transfer up to 64 bytes --------

  do                                                            // check that the fifo has
  {                                                             // enough free space
    TempLong.Long = SPIReadRegisterDirect (ECAT_PRAM_WR_CMD, 2); //
  }                                                             //
  while (TempLong.Byte[1] <   (PDO_SIZE/4));

  digitalWrite(SCS, LOW);                                       // enable SPI chip select

  SPI.transfer(CMD_SPI_WRITE);                               // SPI write command
  SPI.transfer(0x00);                                         // address of the write fifo
  SPI.transfer(0x20);                                         // MsByte first

  for (i=0; i< (PDO_SIZE); i++)               // transfer the data
  {                                                             //
    SPI.transfer(BufferIn[i]);                          //
  }                                                             //

  digitalWrite(SCS, HIGH);                                      // disable SPI chip select

}

void setup() {

    Serial.begin(9600);
    Serial.print("\n Hello : \n");

    SPI.begin();

    digitalWrite (SCS, HIGH);
    pinMode(SCS, OUTPUT);
    delay(100);

    SPI.beginTransaction(settings);

    SPIWriteRegisterDirect (RESET_CTL, DIGITAL_RST);

    ULONG tmpLong;
    tmpLong.Long = 2;
    ULONG tmpLong2;
    tmpLong2.Long = 2;

    unsigned short i = 0;
    unsigned short timeout = 1000;
    do
    {
      i++;
      tmpLong.Long = SPIReadRegisterDirect(BYTE_TEST, 4);
    }while ((tmpLong.Long != 0x87654321) && (i != timeout));

    if (i == timeout)
    {
      Serial.println("Timeout get byte test");
      SPI.endTransaction();
      SPI.end();
    }



  // Test eeprom read:
  uint16_t eeprom_control = SPIReadRegisterIndirect (EEPROM_CONTROL, 2);
  Serial.print("eeprom_control value: ");
  Serial.println(eeprom_control);

  uint16_t eeprom_config = SPIReadRegisterIndirect (EEPROM_CONFIG, 1);
  Serial.print("eeprom_config value: ");
  Serial.println(eeprom_config);
/*
  uint16_t eeprom_pdi = SPIReadRegisterIndirect (EEPROM_PDI, 1);
  Serial.print("EEPROM_PDI value: ");
  Serial.println(eeprom_pdi);
*/
  SPIWriteRegisterIndirect(256, EEPROM_CONTROL, 2);

  eeprom_control = SPIReadRegisterIndirect (EEPROM_CONTROL, 2);
  Serial.print("EEPROM_CONTROL value after: 0x");
  Serial.println(eeprom_control, HEX);




    SPI.endTransaction();
}


void mainTask()
{
  bool WatchDog = false;
  bool Operational = false;
  unsigned char i;
  ULONG TempLong;
                                                            // set SPI parameters
  SPI.beginTransaction(settings);

  TempLong.Long = SPIReadRegisterIndirect (WDOG_STATUS, 1); // read watchdog status
  if ((TempLong.Byte[0] & 0x01) == 0x01)                    //
    WatchDog = false;                                           // set/reset the corrisponding flag
  else                                                      //
    WatchDog = true;                                           //

  // Serial.print("Watdog: ");
  // Serial.println(WatchDog);

  TempLong.Long = SPIReadRegisterIndirect (AL_STATUS, 1);   // read the EtherCAT State Machine status
  unsigned char Status = TempLong.Byte[0] & 0x0F;                         // to see if we are in operational state
  Operational = (Status == ESM_OP);                                     //

                                                            //--- process data transfert ----------

  if (WatchDog | !Operational)                              // if watchdog is active or we are
  {                                                         // not in operational state, reset
    for (i=0; i < PDO_SIZE ; i++)                          // the output buffer
    {                                                       //
      BufferOut[i] = 0;                                //
    }
  }

  else
  {
    SPIReadProcRamFifo();                                   // otherwise transfer process data from
  }                                                         // the EtherCAT core to the output buffer

  SPIWriteProcRamFifo();                                    // we always transfer process data from
                                                            // the input buffer to the EtherCAT core


  SPI.endTransaction();
}


uint8_t counter = 0;


bool is_written = false;

void loop() {
  // put your main code here, to run repeatedly:

  BufferIn[0] = counter;
  counter++;
  mainTask();

  //Serial.print("Buffer out 0 : ");
  //Serial.println(BufferOut[0]);

  uint16_t eeprom_config = SPIReadRegisterIndirect (EEPROM_CONFIG, 1);
  Serial.print("eeprom_config value: ");
  Serial.println(eeprom_config);

  if ((eeprom_config == 1) and not is_written)
  {
     Serial.println("Write request !");
     is_written = true;
     uint16_t vendor_id_address = 0x8;
     SPIWriteRegisterIndirect(vendor_id_address, EEPROM_ADDRESS, 2);
     SPIWriteRegisterIndirect(EepromCommand::READ, EEPROM_CONTROL, 2);

  }


  uint16_t eeprom_control = SPIReadRegisterIndirect (EEPROM_CONTROL, 2);
  Serial.print("EEPROM_CONTROL value after: 0x");
  Serial.println(eeprom_control, HEX);

  uint32_t address = SPIReadRegisterIndirect (EEPROM_ADDRESS, 4);
  Serial.print("address: 0x");
  Serial.println(address, HEX);

  uint32_t vendor_id = SPIReadRegisterIndirect (EEPROM_DATA, 4);
  Serial.print("data: 0x");
  Serial.println(vendor_id, HEX);

 /* SPIWriteRegisterIndirect(256, EEPROM_CONTROL, 2);
  uint16_t eeprom_control = SPIReadRegisterIndirect (EEPROM_CONTROL, 2);
  Serial.print("EEPROM_CONTROL value after: ");
  Serial.println(eeprom_control);

  */

  delayMicroseconds(100);
}
