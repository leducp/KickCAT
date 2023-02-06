#ifndef KICKCAT_CAN_ELMO_ERROR_H
#define KICKCAT_CAN_ELMO_ERROR_H

namespace kickcat
{
    char const* codeToError(uint16_t const& code)
    {
        switch (code)
        {
            // General messages
            case 0x6180 : {return "Fatal CPU error: stack overflow\n";}
            case 0x6200 : {return "User program aborted by an error\n";}
            case 0x6300 : {return "Object mapped to an RPDO returned an error during interpretation or a referenced motion failed to be performed\n";}
            case 0x8110 : {return "CAN message lost (corrupted or overrun\n";}
            case 0x8130 : {return "Heartbeat event\n";}
            case 0x8200 : {return "Protocol error (unrecognized network management [state machine] request\n";}
            case 0x8210 : {return "Attempt to access an unconfigured RPDO\n";}
            case 0xFF01 : {return "Request by user program “emit” function\n";}
            case 0xFF02 : {return "DS 402 IP Underflow \n";}

            //Motor fault
            case 0x3120 : {return "Under-voltage: power supply is shut down or it has too high an output impedance\n";}
            case 0x2340 : {return "Short circuit: motor or its wiring may be defective, or drive is faulty\n";}
            case 0x3310 : {return "Over-voltage: power-supply voltage is too high or servo drive could not absorb kinetic energy while braking a load. A shunt resistor may be required\n";}
            case 0x4310 : {return "Temperature: drive overheating. The environment is too hot or heat removal is not efficient. Could be due to large thermal resistance between drive and its mounting\n";}
            case 0x5280 : {return "ECAM table problem\n";}
            case 0x5281 : {return "Timing Error\n";}
            case 0x5400 : {return "Cannot start motor\n";}
            case 0x5441 : {return "Disabled by Limit switch\n";}
            case 0x6181 : {return "CPU exception: fatal exception\n";}
            case 0x6320 : {return "Cannot start due to inconsistent database\n";}
            case 0x7121 : {return "Motor stuck: motor powered but not moving\n";}
            case 0x7300 : {return "Resolver or Analog Encoder feedback failed\n";}
            case 0x7381 : {return "Two digital Hall sensors changed at once; only one sensor can be changed at a time\n";}
            case 0x8311 : {return "Peak current has been exceeded due to Drive malfunction or Badly-tuned current controller\n";}
            case 0x8380 : {return "Cannot find electrical zero of motor when attempting to start motor with an incremental encoder and no digital Hall sensors. Applied motor current may not suffice for moving motor from its place\n";}
            case 0x8381 : {return "Cannot tune current offsets\n";}
            case 0x8480 : {return "Speed tracking error : exceeded speed error limit, due to Bad tuning of speed controller / Too tight a speed-error tolerance / Inability of motor to accelerate to required speed because line voltage is too low, or motor is not powerful enough\n";}
            case 0x8481 : {return "Speed limit exceeded\n";}
            case 0x8611 : {return "Position tracking error : exceeded position error limit ER[3], due to Bad tuning of position or speed controller / Too tight a position error tolerance / Abnormal motor load, a mechanical limit reached\n";}
            case 0x8680 : {return "Position limit exceeded\n";}
            case 0xFF10 : {return "Cannot start motor\n";}
            
            default : {return "Unknown error\n";}
        }
    }

    char const* registerToError(uint16_t const& reg)
    {
        std::string result = "GENERIC_ERROR";                           // Generic error is mandatory

        // Context can be precised, depending on the state of bits (see CiA DS301 page 53)
        if ((reg & (1U << 1)) == (1U << 1)) {result += " + CURRENT";}
        if ((reg & (1U << 2)) == (1U << 2)) {result += " + VOLTAGE";}
        if ((reg & (1U << 3)) == (1U << 3)) {result += " + TEMPERATURE";}
        if ((reg & (1U << 4)) == (1U << 4)) {result += " + COMMUNICATION_ERROR";}
        if ((reg & (1U << 5)) == (1U << 5)) {result += " + DEVICE_SPECIFIC";}
        if ((reg & (1U << 7)) == (1U << 7)) {result += " + MANUFACTURER_SPECIFIC";}
        
        result += "\n";
        return result.c_str();
    }
}

#endif
