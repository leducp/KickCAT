#ifndef DIAG_CONTAINER
#define DIAG_CONTAINER

#include "protocol.h"
#include "Time.h"
#include "KickCAT.h"

#include <vector>

using namespace kickcat;

// Slave errors
enum class ecatCommand
{
    //General
    BROADCAST_READ,
    BROADCAST_WRITE,
    GET_CURRENT_STATE,
    //Init
    RESET_SLAVES,
    SET_ADDRESSES,
    REQUEST_ECAT_INIT,
    WAIT_ECAT_INIT,
    //Eeprom
    REQUEST_EEPROM,
    READ_EEPROM,
    //IO
    CONFIGURE_MAILBOXES,
    CHECK_MAILBOXES,
    PROCESS_MAILBOXES,
    RXMAPPING,
    TXMAPPING,
    IOMAPPING,
    //PreOP
    REQUEST_ECAT_PREOP,
    WAIT_ECAT_PREOP,
    //SafeOP
    REQUEST_ECAT_SAFEOP,
    WAIT_ECAT_SAFEOP,
    //Op
    VALIDATE_MAPPING,
    REQUEST_ECAT_OP,
    WAIT_ECAT_OP,
    //Logical
    SEND_LOGICAL_READ,
    SEND_LOGICAL_WRITE,
    //Routine
    ROUTINE_GENERAL,
    ROUTINE_LOGICALREAD,
    ROUTINE_LOGICALWRITE,
    ROUTINE_ERRORCOUNTERS,
    ROUTINE_MAILREADCHECK,
    ROUTINE_MAILREAD,
    //Debug
    SEND_GET_REGISTER,
    SEND_WRITE_REGISTER,
    SEND_GET_DLSTATUS,
    REFRESH_ERRORCOUNTERS
};

class ErrorFrame
{
public:
    nanoseconds timestamp;
    ecatCommand command;
    bool broadcast;
    DatagramState state;

    ErrorFrame() = default;
    ErrorFrame(nanoseconds ts, ecatCommand cmd, bool brdcst, DatagramState st) : timestamp(std::move(ts)), command(std::move(cmd)), broadcast(std::move(brdcst)), state(std::move(st)) {};
};

class Emergency
{
public:
    nanoseconds timestamp;
    uint16_t errorCode;

    Emergency() = default;
    Emergency(nanoseconds ts, uint16_t err) : timestamp(std::move(ts)), errorCode(std::move(err)) {}
};


class SlaveErrorContainer
{
public:
    uint8_t ALStatus;
    uint16_t ALStatusCode;
    reg::DLStatus DLStatus;
    ErrorCounters CRCErrorCounters;
    uint16_t CANState;
    std::vector<ErrorFrame> ErroredFrames;
    std::vector<Emergency> Emergencies;

    SlaveErrorContainer() = default;
    SlaveErrorContainer(uint8_t alstat, uint16_t alstatcode, reg::DLStatus dlstat, ErrorCounters errcount, uint16_t canstat, std::vector<ErrorFrame> errfram, std::vector<Emergency> emgs)
        :   ALStatus(std::move(alstat)),
            ALStatusCode(std::move(alstatcode)),
            DLStatus(std::move(dlstat)),
            CRCErrorCounters(std::move(errcount)),
            CANState(std::move(canstat)),
            ErroredFrames(std::move(errfram)),
            Emergencies(std::move(emgs))
            {}
};

//Process Errors
enum class processState
{
    DEFAULT_OK,                                 //default state
    SOCKET_ERROR,                               //Cannot open socket
    NO_SLAVE_DETECTED,                          //No slave is detected initially
    SLAVE_DETECTION_OK,
    UNEXPECTED_TOPOLOGY,                        //topology does not match
    TOPOLOGY_OK,

    RESET_SLAVES_FAILED,
    RESET_SLAVES_OK,

    ECAT_INIT_TIMEOUT,                          //EtherCAT init has reached timeout
    ECAT_INIT_INVALID_STATE_TRANSITION,         //Cannot go to init, because transition is invalid
    ECAT_INIT_FAILED,                           //WKC invalid on operation to go init
    ECAT_INIT_OK,

    ECAT_FETCH_EEPROM_TIMEOUT,                  //FetchEeprom has reached timeout
    ECAT_FETCH_EEPROM_FAILED,                   //WKC invalid for fetching eeprom
    ECAT_FETCH_EEPROM_OK,
    ECAT_CONFIGURE_MAILBOXES_FAILED,            //WKC invalid for configureMailboxes
    ECAT_CONFIGURE_MAILBOXES_OK,

    ECAT_PREOP_TIMEOUT,
    ECAT_PREOP_INVALID_STATE_TRANSITION,
    ECAT_PREOP_FAILED,
    ECAT_PREOP_OK,

    ECAT_IO_CHECKMAILBOXES_FAILED,
    ECAT_IO_RXMAPPING_FAILED,
    ECAT_IO_RXMAPPING_TIMEOUT,
    ECAT_IO_TXMAPPING_FAILED,
    ECAT_IO_TXMAPPING_TIMEOUT,
    ECAT_IO_IOMAPPING_BUFFERTOOSMALL,
    ECAT_IO_IOMAPPING_ACCESS_ERROR,
    ECAT_IO_IOMAPPING_TIMEOUT,
    ECAT_IO_IOMAPPING_FAILED,
    ECAT_IO_OK,

    ECAT_SAFEOP_TIMEOUT,
    ECAT_SAFEOP_INVALID_STATE_TRANSITION,
    ECAT_SAFEOP_FAILED,
    ECAT_SAFEOP_OK,

    ECAT_VALIDATE_MAPPING_FAILED,

    ECAT_OP_TIMEOUT,
    ECAT_OP_INVALID_STATE_TRANSITION,
    ECAT_OP_FAILED,
    ECAT_OP_OK,

    CAN_TURNALLON_FAILED,
    CAN_TURNALLON_TIMEOUT,
    CAN_TURNALLON_OK,

    CAN_TURNALLOFF_FAILED,
    CAN_TURNALLOFF_TIMEOUT,
    CAN_TURNALLOFF_OK,

    ROUTINE_FAILED,

    CAN_STEP_INIT_FAILED,
    CAN_STEP_INIT_TIMEOUT,
    CAN_STEP_INIT_OK,

    UNKNOWN_ERROR
};

class ErrorProcess
{
public:
    nanoseconds timestamp;
    processState stateProcess;

    ErrorProcess() = default;
    ErrorProcess(nanoseconds ts, processState st) : timestamp(std::move(ts)), stateProcess(std::move(st)) {};
};

class ErrorGeneral
{
public:
    nanoseconds timestamp;
    std::string msg;

    ErrorGeneral() = default;
    ErrorGeneral(nanoseconds ts, std::string m) : timestamp(std::move(ts)), msg(std::move(m)) {};
};

#endif
