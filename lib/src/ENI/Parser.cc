#include <stdexcept>

#include "kickcat/ENI/Parser.h"
#include "kickcat/utils/xml.h"

using namespace tinyxml2;
using namespace kickcat::xml;

namespace kickcat::ENI
{
    namespace
    {
        void checkDatagramSize(XMLElement const* elem, std::size_t size)
        {
            if (size > MAX_ETHERCAT_PAYLOAD_SIZE)
            {
                fail(elem, "datagram data of " + std::to_string(size) + " bytes does not fit in a frame");
            }
        }

        // Checked, not kept: the master sends its own addresses and EtherCAT frames.
        void checkFrameInfo(XMLElement* info)
        {
            for (char const* name : {"Destination", "Source"})
            {
                XMLElement* mac = require(info, name);
                if (hexBinary(mac).size() != 6)
                {
                    fail(mac, "MAC address must be 6 bytes");
                }
            }

            // In network order (88a4) as ETG.2100 writes it, or byte-swapped (a488) as TwinCAT does.
            if (XMLElement* ether_type = info->FirstChildElement("EtherType"))
            {
                std::vector<uint8_t> raw = hexBinary(ether_type);
                bool network = (raw == std::vector<uint8_t>{0x88, 0xa4});
                bool swapped = (raw == std::vector<uint8_t>{0xa4, 0x88});
                if (not network and not swapped)
                {
                    fail(ether_type, "EtherType must be EtherCAT (88a4)");
                }
            }
        }

        uint16_t parseIndex(XMLElement* elem)
        {
            return numberOf<uint16_t>(elem);
        }

        Transitions parseTransitions(XMLElement* parent)
        {
            Transitions out = 0;
            for (XMLElement* t = parent->FirstChildElement("Transition"); t != nullptr; t = t->NextSiblingElement("Transition"))
            {
                try
                {
                    out |= transition::fromString(textOf(t));
                }
                catch (std::invalid_argument const& e)
                {
                    fail(t, e.what());
                }
            }
            return out;
        }

        uint32_t parseAddress(XMLElement* node, Command cmd)
        {
            XMLElement* addr = node->FirstChildElement("Addr");
            XMLElement* ado_node = node->FirstChildElement("Ado");
            bool logical = (cmd == Command::LRD) or (cmd == Command::LWR) or (cmd == Command::LRW);
            if (logical)
            {
                if (ado_node != nullptr or node->FirstChildElement("Adp") != nullptr)
                {
                    fail(node, "a logical command is addressed by <Addr>, not <Adp>/<Ado>");
                }
                if (addr == nullptr)
                {
                    fail(node, "missing <Addr> for a logical command");
                }
                return numberOf<uint32_t>(addr, BIT_PATTERN);
            }

            if (addr != nullptr)
            {
                fail(node, "<Addr> is only for logical commands (LRD, LWR, LRW)");
            }
            if (ado_node == nullptr)
            {
                fail(node, "missing <Ado>");
            }
            uint16_t ado = numberOf<uint16_t>(ado_node);
            uint16_t adp = optionalNumber<uint16_t>(node, "Adp", BIT_PATTERN).value_or(0);
            return createAddress(adp, ado);
        }

        Command parseCommand(XMLElement* node)
        {
            XMLElement* elem = require(node, "Cmd");
            uint8_t raw = numberOf<uint8_t>(elem);
            if (raw > static_cast<uint8_t>(Command::FRMW))
            {
                fail(elem, "unknown command " + std::to_string(raw));
            }
            return static_cast<Command>(raw);
        }

        // True when the reply is compared (EQ), false when it is not (NONE).
        bool comparesReply(XMLElement const* elem)
        {
            std::optional<std::string> raw = attributeOf(elem, "Type");
            if (not raw)
            {
                return true;
            }
            std::string_view text = *raw;
            if (text == "EQ")
            {
                return true;
            }
            if (text == "NONE")
            {
                return false;
            }
            if (text == "NOT_EQ" or text == "EQ_OR_G" or text == "EQ_OR_L" or text == "G" or text == "L")
            {
                fail(elem, std::string{"unsupported Validate type '"} + *raw + "', only EQ and NONE are");
            }
            fail(elem, std::string{"unknown Validate type '"} + *raw + "'");
        }

        void parseDatagram(XMLElement* node, Datagram& datagram)
        {
            datagram.comment = optionalText(node, "Comment");
            datagram.cmd = parseCommand(node);
            datagram.address = parseAddress(node, datagram.cmd);

            XMLElement* data = node->FirstChildElement("Data");
            XMLElement* length = node->FirstChildElement("DataLength");
            if (data != nullptr and length != nullptr)
            {
                fail(node, "<Data> cannot be combined with <DataLength>");
            }
            if (data != nullptr)
            {
                datagram.data = hexBinary(data);
                checkDatagramSize(data, datagram.data.size());
            }
            if (length != nullptr)
            {
                uint16_t size = numberOf<uint16_t>(length);
                checkDatagramSize(length, size);
                datagram.data.resize(size, 0);
            }
            datagram.expected_wkc = optionalNumber<uint16_t>(node, "Cnt");
        }

        InitCmd parseInitCmd(XMLElement* node)
        {
            InitCmd cmd;
            parseDatagram(node, cmd);
            cmd.transitions  = parseTransitions(node);
            cmd.before_slave = optionalBool(node, "BeforeSlave");

            if (XMLElement* req = node->FirstChildElement("Requires"))
            {
                std::string text = textOf(req);
                if (text == "frame")
                {
                    cmd.frame_requirement = requirement::FRAME;
                }
                else if (text == "cycle")
                {
                    cmd.frame_requirement = requirement::CYCLE;
                }
                else
                {
                    fail(req, "unknown Requires '" + text + "'");
                }
            }

            cmd.retries = optionalNumber<uint16_t>(node, "Retries").value_or(0);

            if (XMLElement* validate = node->FirstChildElement("Validate"))
            {
                Validate v;
                v.data = hexBinary(require(validate, "Data"));
                if (XMLElement* mask = validate->FirstChildElement("DataMask"))
                {
                    v.mask = hexBinary(mask);
                    if (v.mask.size() != v.data.size())
                    {
                        fail(mask, "DataMask size differs from Data size");
                    }
                }
                if (v.data.size() > cmd.data.size())
                {
                    fail(validate, "Validate Data is longer than the command data");
                }
                v.timeout = milliseconds{requireNumber<uint32_t>(validate, "Timeout")};
                if (comparesReply(validate))
                {
                    cmd.validate = std::move(v);
                }
                else
                {
                    cmd.timeout = v.timeout;
                }
            }
            else
            {
                cmd.timeout = milliseconds{optionalNumber<uint32_t>(node, "Timeout").value_or(0)};
            }
            return cmd;
        }

        std::vector<InitCmd> parseInitCmds(XMLElement* parent)
        {
            XMLElement* list = parent->FirstChildElement("InitCmds");
            if (list == nullptr)
            {
                return {};
            }
            return all(list, "InitCmd", parseInitCmd);
        }

        CoEInitCmd parseCoEInitCmd(XMLElement* node)
        {
            CoEInitCmd cmd;
            cmd.transitions = parseTransitions(node);
            if (cmd.transitions == 0)
            {
                fail(node, "missing mandatory <Transition>");
            }
            cmd.comment         = optionalText(node, "Comment");
            cmd.timeout         = milliseconds{requireNumber<uint32_t>(node, "Timeout")};
            cmd.ccs             = requireNumber<uint8_t>(node, "Ccs");
            cmd.index           = requireNumber<uint16_t>(node, "Index");
            cmd.subindex        = requireNumber<uint8_t>(node, "SubIndex");
            cmd.disabled        = optionalBool(node, "Disabled");
            cmd.fixed           = boolAttribute(node, "Fixed");
            cmd.complete_access = boolAttribute(node, "CompleteAccess");
            if (XMLElement* data = node->FirstChildElement("Data"))
            {
                cmd.data = hexBinary(data);
            }
            return cmd;
        }

        MailboxArea parseMailboxArea(XMLElement* node)
        {
            MailboxArea area;
            area.start  = requireNumber<uint16_t>(node, "Start");
            area.length = requireNumber<uint16_t>(node, "Length");
            return area;
        }

        void parseProtocol(XMLElement* node, Mailbox& mbx)
        {
            std::string text = textOf(node);
            if      (text == "AoE") { mbx.protocols |= eeprom::MailboxProtocol::AoE; }
            else if (text == "EoE") { mbx.protocols |= eeprom::MailboxProtocol::EoE; }
            else if (text == "CoE") { mbx.protocols |= eeprom::MailboxProtocol::CoE; }
            else if (text == "FoE") { mbx.protocols |= eeprom::MailboxProtocol::FoE; }
            else if (text == "SoE") { mbx.protocols |= eeprom::MailboxProtocol::SoE; }
            else if (text == "VoE") { mbx.protocols |= eeprom::MailboxProtocol::VoE; }
            else
            {
                fail(node, "unknown mailbox protocol '" + text + "'");
            }
        }

        std::optional<Mailbox> parseMailbox(XMLElement* slave)
        {
            XMLElement* node = slave->FirstChildElement("Mailbox");
            if (node == nullptr)
            {
                return std::nullopt;
            }

            Mailbox mbx;
            mbx.send = parseMailboxArea(require(node, "Send"));
            XMLElement* recv = require(node, "Recv");
            mbx.recv = parseMailboxArea(recv);
            if (std::optional<uint32_t> poll = optionalNumber<uint32_t>(recv, "PollTime"))
            {
                mbx.poll_time = milliseconds{*poll};
            }
            mbx.status_bit_addr = optionalNumber<uint32_t>(recv, "StatusBitAddr");

            if (XMLElement* boot = node->FirstChildElement("BootStrap"))
            {
                mbx.bootstrap_send = parseMailboxArea(require(boot, "Send"));
                mbx.bootstrap_recv = parseMailboxArea(require(boot, "Recv"));
            }

            for (XMLElement* p = node->FirstChildElement("Protocol"); p != nullptr; p = p->NextSiblingElement("Protocol"))
            {
                parseProtocol(p, mbx);
            }

            if (XMLElement* coe = node->FirstChildElement("CoE"))
            {
                if (XMLElement* list = coe->FirstChildElement("InitCmds"))
                {
                    mbx.coe_init_cmds = all(list, "InitCmd", parseCoEInitCmd);
                }
            }

            for (char const* name : {"SoE", "AoE", "EoE", "FoE", "VoE"})
            {
                XMLElement* block = node->FirstChildElement(name);
                if (block == nullptr)
                {
                    continue;
                }
                XMLElement* list = block->FirstChildElement("InitCmds");
                if (list == nullptr)
                {
                    continue;
                }
                for (XMLElement* ic = list->FirstChildElement("InitCmd"); ic != nullptr; ic = ic->NextSiblingElement("InitCmd"))
                {
                    ++mbx.unsupported_init_cmds;
                }
            }
            return mbx;
        }

        uint16_t parseSmMask(XMLElement const* node)
        {
            uint16_t mask = 0;
            for (int i = 0; i < 16; ++i)
            {
                std::string attr = "Sm" + std::to_string(i);
                if (boolAttribute(node, attr.c_str()))
                {
                    mask |= static_cast<uint16_t>(1 << i);
                }
            }
            return mask;
        }

        ProcessDataRange parseRange(XMLElement* node)
        {
            ProcessDataRange range;
            range.bit_start  = requireNumber<uint32_t>(node, "BitStart");
            range.bit_length = requireNumber<uint32_t>(node, "BitLength");
            range.sm_mask    = parseSmMask(node);
            return range;
        }

        SyncManagerSettings parseSm(XMLElement* node, uint8_t index)
        {
            SyncManagerSettings sm;
            sm.index = index;

            XMLElement* type = require(node, "Type");
            try
            {
                SyncManager::fromString(textOf(type), sm.type);
            }
            catch (std::exception const& e)
            {
                fail(type, e.what());
            }

            sm.min_size      = optionalNumber<uint16_t>(node, "MinSize").value_or(0);
            sm.max_size      = optionalNumber<uint16_t>(node, "MaxSize").value_or(0);
            sm.default_size  = optionalNumber<uint16_t>(node, "DefaultSize").value_or(0);
            sm.start_address = requireNumber<uint16_t>(node, "StartAddress");
            sm.control_byte  = requireNumber<uint8_t>(node, "ControlByte");
            XMLElement* enable = require(node, "Enable");
            sm.enable        = parseBool(enable, textOf(enable));
            sm.pdos = all(node, "Pdo", parseIndex);
            return sm;
        }

        CoE::DataType parseDataType(XMLElement* node)
        {
            std::string text = textOf(node);
            std::optional<CoE::DataType> type = CoE::dataTypeFromLabel(text);
            if (not type)
            {
                fail(node, "unknown base data type '" + text + "'");
            }
            return *type;
        }

        CoE::PdoMappingEntry parsePdoEntry(XMLElement* node)
        {
            CoE::PdoMappingEntry entry{};
            entry.index    = requireNumber<uint16_t>(node, "Index");
            entry.subindex = optionalNumber<uint8_t>(node, "SubIndex").value_or(0);
            entry.bitlen   = requireNumber<uint8_t>(node, "BitLen");
            if (entry.index != 0)   // index 0 is a padding gap: no type
            {
                entry.type = parseDataType(require(node, "DataType"));
            }
            return entry;
        }

        Pdo parsePdo(XMLElement* node)
        {
            Pdo pdo;
            pdo.index     = requireNumber<uint16_t>(node, "Index");
            pdo.name      = optionalText(node, "Name");
            pdo.fixed     = boolAttribute(node, "Fixed");
            pdo.mandatory = boolAttribute(node, "Mandatory");
            pdo.sm        = numberAttribute<uint8_t>(node, "Sm");
            pdo.exclude   = all(node, "Exclude", parseIndex);
            pdo.entries   = all(node, "Entry", parsePdoEntry);
            return pdo;
        }

        ProcessData parseProcessData(XMLElement* slave)
        {
            ProcessData pd;
            XMLElement* node = slave->FirstChildElement("ProcessData");
            if (node == nullptr)
            {
                return pd;
            }

            pd.send = all(node, "Send", parseRange);
            pd.recv = all(node, "Recv", parseRange);
            for (int i = 0; i < 16; ++i)
            {
                std::string name = "Sm" + std::to_string(i);
                if (XMLElement* sm = node->FirstChildElement(name.c_str()))
                {
                    pd.sms.push_back(parseSm(sm, static_cast<uint8_t>(i)));
                }
            }
            pd.rx_pdos = all(node, "RxPdo", parsePdo);
            pd.tx_pdos = all(node, "TxPdo", parsePdo);
            return pd;
        }

        SlaveInfo parseSlaveInfo(XMLElement* slave)
        {
            XMLElement* node = require(slave, "Info");
            SlaveInfo info;
            info.name          = optionalText(node, "Name");
            info.phys_addr     = optionalNumber<uint16_t>(node, "PhysAddr");
            info.auto_inc_addr = optionalNumber<uint16_t>(node, "AutoIncAddr", BIT_PATTERN);
            if (XMLElement* identification = node->FirstChildElement("Identification"))
            {
                std::optional<uint16_t> value = numberAttribute<uint16_t>(identification, "Value");
                if (not value)
                {
                    fail(identification, "missing mandatory @Value");
                }
                Identification id;
                id.ado   = requireNumber<uint16_t>(identification, "Ado");
                id.value = *value;
                info.identification = id;
            }
            info.vendor_id     = requireNumber<uint32_t>(node, "VendorId", BIT_PATTERN);
            info.product_code  = requireNumber<uint32_t>(node, "ProductCode", BIT_PATTERN);
            info.revision_no   = requireNumber<uint32_t>(node, "RevisionNo", BIT_PATTERN);
            info.serial_no     = optionalNumber<uint32_t>(node, "SerialNo", BIT_PATTERN);
            return info;
        }

        std::vector<PreviousPort> parsePreviousPorts(XMLElement* slave)
        {
            std::vector<PreviousPort> out;
            for (XMLElement* node = slave->FirstChildElement("PreviousPort"); node != nullptr; node = node->NextSiblingElement("PreviousPort"))
            {
                PreviousPort prev;
                prev.selected = boolAttribute(node, "Selected");

                XMLElement* port = require(node, "Port");
                std::string letter = textOf(port);
                if (letter.size() != 1 or letter[0] < 'A' or letter[0] > 'D')
                {
                    fail(port, "port must be A, B, C or D");
                }
                prev.port = static_cast<uint8_t>(letter[0] - 'A');
                prev.phys_addr = optionalNumber<uint16_t>(node, "PhysAddr");
                out.push_back(prev);
            }
            return out;
        }

        std::optional<Dc> parseDc(XMLElement* slave)
        {
            XMLElement* node = slave->FirstChildElement("DC");
            if (node == nullptr)
            {
                return std::nullopt;
            }

            auto time = [&](char const* name, bool is_signed) -> std::optional<nanoseconds>
            {
                if (is_signed)
                {
                    std::optional<int32_t> shift = optionalNumber<int32_t>(node, name);
                    if (not shift)
                    {
                        return std::nullopt;
                    }
                    return nanoseconds{*shift};
                }
                std::optional<uint32_t> cycle = optionalNumber<uint32_t>(node, name);
                if (not cycle)
                {
                    return std::nullopt;
                }
                return nanoseconds{*cycle};
            };

            Dc dc;
            dc.potential_reference_clock = optionalBool(node, "PotentialReferenceClock");
            dc.reference_clock           = optionalBool(node, "ReferenceClock");
            dc.cycle_time0               = time("CycleTime0", false);
            dc.cycle_time1               = time("CycleTime1", true);   // Sync1 - Sync0 cycle + Sync0 shift: may be negative
            dc.shift_time                = time("ShiftTime",  true);
            return dc;
        }

        Slave parseSlave(XMLElement* node)
        {
            Slave slave;
            slave.info           = parseSlaveInfo(node);
            slave.process_data   = parseProcessData(node);
            slave.mailbox        = parseMailbox(node);
            slave.init_cmds      = parseInitCmds(node);
            slave.previous_ports = parsePreviousPorts(node);
            slave.dc             = parseDc(node);
            return slave;
        }

        Master parseMaster(XMLElement* node)
        {
            Master master;
            XMLElement* info = require(node, "Info");
            master.name = optionalText(info, "Name");
            checkFrameInfo(info);

            // Checked, not kept: mailboxes are polled per slave, not through the ENI status FMMUs.
            if (XMLElement* states = node->FirstChildElement("MailboxStates"))
            {
                (void) requireNumber<uint32_t>(states, "StartAddr", BIT_PATTERN);
                (void) requireNumber<uint16_t>(states, "Count");
            }

            master.init_cmds = parseInitCmds(node);
            return master;
        }

        uint8_t parseStates(XMLElement* node)
        {
            uint8_t out = 0;
            for (XMLElement* s = node->FirstChildElement("State"); s != nullptr; s = s->NextSiblingElement("State"))
            {
                std::string text = textOf(s);
                if      (text == "INIT")   { out |= State::INIT;        }
                else if (text == "PREOP")  { out |= State::PRE_OP;      }
                else if (text == "SAFEOP") { out |= State::SAFE_OP;     }
                else if (text == "OP")     { out |= State::OPERATIONAL; }
                else
                {
                    fail(s, "unknown State '" + text + "'");
                }
            }
            return out;
        }

        CyclicCmd parseCyclicCmd(XMLElement* node)
        {
            CyclicCmd cmd;
            parseDatagram(node, cmd);
            cmd.states      = parseStates(node);
            cmd.input_offs  = optionalNumber<uint32_t>(node, "InputOffs");
            cmd.output_offs = optionalNumber<uint32_t>(node, "OutputOffs");
            return cmd;
        }

        Cyclic parseCyclic(XMLElement* node)
        {
            Cyclic cyclic;
            cyclic.comment = optionalText(node, "Comment");
            if (std::optional<uint32_t> cycle = optionalNumber<uint32_t>(node, "CycleTime"))
            {
                cyclic.cycle_time = microseconds{*cycle};
            }
            // Priority and TaskId schedule the master's own tasks: the application owns them.
            if (std::optional<uint8_t> priority = optionalNumber<uint8_t>(node, "Priority"))
            {
                if ((*priority < 1) or (*priority > 62))
                {
                    fail(node->FirstChildElement("Priority"), "Priority must be within [1, 62]");
                }
            }

            for (XMLElement* f = node->FirstChildElement("Frame"); f != nullptr; f = f->NextSiblingElement("Frame"))
            {
                Frame frame;
                frame.comment = optionalText(f, "Comment");
                frame.cmds = all(f, "Cmd", parseCyclicCmd);
                cyclic.frames.push_back(std::move(frame));
            }
            return cyclic;
        }

        ProcessImageArea parseImageArea(XMLElement* node)
        {
            ProcessImageArea area;
            if (node == nullptr)
            {
                return area;
            }
            area.byte_size = requireNumber<uint32_t>(node, "ByteSize");
            for (XMLElement* v = node->FirstChildElement("Variable"); v != nullptr; v = v->NextSiblingElement("Variable"))
            {
                Variable var;
                var.name      = textOf(require(v, "Name"));
                if (XMLElement* type = v->FirstChildElement("DataType"))
                {
                    var.data_type = parseDataType(type);
                }
                var.bit_size  = requireNumber<uint32_t>(v, "BitSize");
                var.bit_offs  = requireNumber<uint32_t>(v, "BitOffs");
                area.variables.push_back(std::move(var));
            }
            return area;
        }

        Config parseDocument(XMLDocument& doc)
        {
            XMLElement* root = doc.FirstChildElement("EtherCATConfig");
            if (root == nullptr)
            {
                throw std::invalid_argument("root element <EtherCATConfig> not found");
            }
            XMLElement* node = require(root, "Config");

            Config config;
            config.master = parseMaster(require(node, "Master"));
            config.slaves = all(node, "Slave", parseSlave);
            config.cyclic = all(node, "Cyclic", parseCyclic);
            if (XMLElement* image = node->FirstChildElement("ProcessImage"))
            {
                ProcessImage pi;
                pi.inputs  = parseImageArea(image->FirstChildElement("Inputs"));
                pi.outputs = parseImageArea(image->FirstChildElement("Outputs"));
                config.process_image = std::move(pi);
            }
            return config;
        }
    }

    Config loadFile(std::string const& path)
    {
        return withContext("ENI", [&]()
        {
            XMLDocument doc;
            if (doc.LoadFile(path.c_str()) != XML_SUCCESS)
            {
                throw std::invalid_argument("cannot load '" + path + "': " + doc.ErrorStr());
            }
            return parseDocument(doc);
        });
    }

    Config loadString(std::string const& xml)
    {
        return withContext("ENI", [&]()
        {
            XMLDocument doc;
            if (doc.Parse(xml.c_str(), xml.size()) != XML_SUCCESS)
            {
                throw std::invalid_argument(std::string{"cannot parse document: "} + doc.ErrorStr());
            }
            return parseDocument(doc);
        });
    }
}
