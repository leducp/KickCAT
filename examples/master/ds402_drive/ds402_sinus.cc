#define _USE_MATH_DEFINES
#include <cmath>
#include <iostream>

#include <argparse/argparse.hpp>

#include "kickcat/Bus.h"
#include "kickcat/CoE/CiA/DS402/Drive.h"
#include "kickcat/Link.h"
#include "kickcat/OS/Timer.h"
#include "kickcat/helpers.h"

using namespace kickcat;

int main(int argc, char* argv[])
{
    argparse::ArgumentParser program("ds402_sinus");

    std::string nom_if;
    program.add_argument("-i", "--interface").required().store_into(nom_if)
        .help("primary network interface name");

    std::string red_if;
    program.add_argument("-r", "--redundancy").default_value(std::string{""}).store_into(red_if)
        .help("redundancy network interface name (empty disables redundancy)");

    int slave_index = 0;
    program.add_argument("-s", "--slave").default_value(0).scan<'i', int>().store_into(slave_index)
        .help("0-based position of the motor on the EtherCAT bus");

    double encoder_ticks_per_rev = 1 << 19;
    program.add_argument("--encoder-ticks")
        .default_value(static_cast<double>(1 << 19)).scan<'g', double>().store_into(encoder_ticks_per_rev)
        .help("encoder counts per motor revolution");

    double gear_ratio = 1.0;
    program.add_argument("--gear-ratio").default_value(1.0).scan<'g', double>().store_into(gear_ratio)
        .help("motor revolutions per output-shaft revolution");

    double amplitude_deg = 8.0;
    program.add_argument("--amplitude").default_value(8.0).scan<'g', double>().store_into(amplitude_deg)
        .help("sinusoidal motion amplitude at the output shaft (degrees)");

    double frequency_hz = 0.2;
    program.add_argument("--frequency").default_value(0.2).scan<'g', double>().store_into(frequency_hz)
        .help("sinusoidal motion frequency (Hz)");

    double duration_s = 30.0;
    program.add_argument("--duration").default_value(30.0).scan<'g', double>().store_into(duration_s)
        .help("how long to run the motion (seconds)");

    try
    {
        program.parse_args(argc, argv);
    }
    catch (std::runtime_error const& e)
    {
        std::cerr << e.what() << std::endl << program;
        return 1;
    }

    auto [nominal, redundancy] = createSockets(nom_if, red_if);
    auto report_redundancy = []{ printf("Redundancy activated\n"); };
    auto link = std::make_shared<Link>(nominal, redundancy, report_redundancy);
    link->setTimeout(2ms);
    link->checkRedundancyNeeded();

    Bus bus{link};
    uint8_t io_buffer[2048] = {};

    bus.init();
    bus.requestState(State::PRE_OP);
    bus.waitForState(State::PRE_OP, 3000ms);

    if (slave_index < 0 or static_cast<size_t>(slave_index) >= bus.slaves().size())
    {
        std::cerr << "Slave index " << slave_index << " out of range; bus has "
                  << bus.slaves().size() << " slave(s)." << std::endl;
        return 1;
    }
    Slave& slave = bus.slaves().at(slave_index);
    printf("Driving slave %d (address 0x%04x)\n", slave_index, slave.address);

    CoE::CiA::DS402::Drive drive(bus, slave);
    drive.setUnits({encoder_ticks_per_rev, gear_ratio, 1.0});
    drive.configure(CoE::CiA::DS402::control::POSITION_CYCLIC);
    drive.setInterpolationTimePeriod(1, -3); // 1ms

    bus.createMapping(io_buffer);
    bus.requestState(State::SAFE_OP);
    bus.waitForState(State::SAFE_OP, 1000ms);

    auto bus_error = [](DatagramState const&){ THROW_ERROR("bus error"); };
    bus.processDataRead (bus_error);
    bus.processDataWrite(bus_error);

    drive.attach();

    auto pump_pdo = [&]{
        bus.processDataRead (bus_error);
        bus.processDataWrite(bus_error);
    };
    bus.requestState(State::OPERATIONAL);
    bus.waitForState(State::OPERATIONAL, 100ms, pump_pdo);

    link->setTimeout(500us);
    drive.enable();

    Timer timer{1ms};
    timer.start();

    double const amplitude_rad = amplitude_deg / 180.0 * M_PI;
    int64_t const loop_count   = static_cast<int64_t>(duration_s * 1000.0);

    bool initial_captured = false;
    double initial_rad    = 0.0;
    nanoseconds motion_start = 0ns;

    for (int64_t i = 0; i < loop_count; ++i)
    {
        timer.wait_next_tick();

        try
        {
            bus.sendLogicalRead (bus_error);
            bus.sendLogicalWrite(bus_error);
            bus.finalizeDatagrams();
            bus.processAwaitingFrames();
        }
        catch (std::exception const& e)
        {
            std::cerr << "[" << i << "] " << e.what() << std::endl;
            continue;
        }

        drive.update();

        if (drive.isEnabled() and not initial_captured)
        {
            initial_rad      = drive.actualPosition();
            motion_start     = since_epoch();
            initial_captured = true;
            printf("Motor enabled at %.4f rad; running %.2f Hz sinus, +/- %.2f deg, for %.1f s\n",
                   initial_rad, frequency_hz, amplitude_deg, duration_s);
        }

        if (initial_captured)
        {
            double t     = seconds_f(elapsed_time(motion_start)).count();
            double delta = amplitude_rad * std::sin(2.0 * M_PI * frequency_hz * t);
            drive.setTargetPosition(initial_rad + delta);
        }
    }

    printf("Disabling motor...\n");
    drive.disable();
    for (int i = 0; i < 200; ++i)
    {
        timer.wait_next_tick();
        try
        {
            bus.sendLogicalRead (bus_error);
            bus.sendLogicalWrite(bus_error);
            bus.finalizeDatagrams();
            bus.processAwaitingFrames();
        }
        catch (...) {}
        drive.update();
        if (not drive.isEnabled() and not drive.isFaulted()) { break; }
    }

    return 0;
}
