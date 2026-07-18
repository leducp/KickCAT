#include "kickcat/simulation/SimulatedSlave.h"

#include <filesystem>
#include <fstream>
#include <numeric>
#include <optional>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include "kickcat/CoE/OD.h"
#include "kickcat/ESC/EmulatedESC.h"
#include "kickcat/ESI/Parser.h"
#include "kickcat/ESI/SIIBuilder.h"
#include "kickcat/SIIParser.h"

namespace kickcat::sim
{
    using json = nlohmann::json;
    namespace fs = std::filesystem;

    void configureDeviceDictionary(SimulatedSlave& sim, ESI::Device& device)
    {
        if (not device.dictionary.empty())
        {
            sim.dictionary = std::make_unique<CoE::Dictionary>(std::move(device.dictionary));
            sim.slave->setDictionary(sim.dictionary.get());

            // A mailboxless terminal (e.g. a digital I/O like EL1004) still gets its OD,
            // but only a device that declares a CoE mailbox is reachable by SDO.
            const bool esi_coe_advertised = (device.mailbox and device.mailbox->coe);
            if (esi_coe_advertised)
            {
                sim.mailbox = std::make_unique<mailbox::response::Mailbox>(sim.esc.get(), 1024);
                sim.mailbox->enableCoE(*sim.dictionary);
                sim.slave->setMailbox(sim.mailbox.get());
            }
        }
    }

    SimulatedSlave buildSlave(fs::path const& config_path)
    {
        fs::path config_dir = config_path.parent_path();

        std::ifstream f(config_path);
        if (not f.is_open())
        {
            throw std::runtime_error("Failed to open config file: " + config_path.string());
        }
        json config;
        try
        {
            f >> config;
        }
        catch (const json::parse_error& e)
        {
            throw std::runtime_error("Failed to parse JSON in " + config_path.string() + ": " + e.what());
        }

        SimulatedSlave sim;
        sim.esc = std::make_unique<EmulatedESC>();
        sim.pdo   = std::make_unique<PDO>(sim.esc.get());
        sim.slave = std::make_unique<slave::Slave>(sim.esc.get(), sim.pdo.get());

        if (config.contains("esi"))
        {
            // Build the EEPROM image (and CoE dictionary) from a selected ESI device.
            fs::path esi_full_path = config_dir / config["esi"].get<std::string>();
            if (not fs::exists(esi_full_path))
            {
                throw std::runtime_error("ESI file not found: " + esi_full_path.string());
            }
            ESI::DeviceFilter filter;
            if (config.contains("device_type"))  { filter.type         = config["device_type"].get<std::string>(); }
            if (config.contains("product_code")) { filter.product_code = config["product_code"].get<uint32_t>();    }
            if (config.contains("revision_no"))  { filter.revision_no  = config["revision_no"].get<uint32_t>();     }

            try
            {
                ESI::Parser parser;
                ESI::Device device = parser.loadDevice(esi_full_path.string(), filter);
                CoE::materializeStorage(device.dictionary);
                sim.esc->loadEeprom(ESI::buildEepromImage(device));
                configureDeviceDictionary(sim, device);
            }
            catch (std::exception const& e)
            {
                throw std::runtime_error("Failed to build EEPROM from ESI " + esi_full_path.string() + ": " + e.what());
            }
        }
        else if (config.contains("eeprom"))
        {
            fs::path eeprom_full_path = config_dir / config["eeprom"].get<std::string>();
            if (not fs::exists(eeprom_full_path))
            {
                throw std::runtime_error("EEPROM file not found: " + eeprom_full_path.string());
            }
            std::vector<uint8_t> eeprom_image = loadBinaryFile(eeprom_full_path);
            sim.esc->loadEeprom(eeprom_image);

            if (config.contains("coe_xml"))
            {
                fs::path coe_xml_full_path = config_dir / config["coe_xml"].get<std::string>();
                if (not fs::exists(coe_xml_full_path))
                {
                    throw std::runtime_error("CoE XML file not found: " + coe_xml_full_path.string());
                }
                eeprom::SII sii;
                sii.parse(eeprom_image);
                uint32_t revision_no = sii.info.revision_number;
                uint32_t product_code = sii.info.product_code;

                ESI::DeviceFilter filter;
                filter.revision_no = revision_no;
                filter.product_code = product_code;
                ESI::Parser parser;
                ESI::Device device = parser.loadDevice(coe_xml_full_path.string(), filter);
                CoE::materializeStorage(device.dictionary);
                configureDeviceDictionary(sim, device);
            }
        }
        else
        {
            throw std::runtime_error("Config file " + config_path.string() + " missing 'eeprom' or 'esi' field");
        }

        sim.input.resize(PDO_MAX_SIZE);
        std::iota(sim.input.begin(), sim.input.end(), 0);
        sim.output.assign(PDO_MAX_SIZE, 0xFF);
        sim.pdo->setInput(sim.input.data(), PDO_MAX_SIZE);
        sim.pdo->setOutput(sim.output.data(), PDO_MAX_SIZE);

        // Optional device behaviour (e.g. a CiA-402 motor plant), selected by the
        // config -- the factory is the only place that names a concrete device.
        sim.device = makeDeviceApp(*sim.slave, config);

        return sim;
    }
}
