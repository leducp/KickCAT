#include <algorithm>
#include <cctype>
#include <cstring>
#include <limits>
#include <stdexcept>

#include <tinyxml2.h>

#include "kickcat/ENI/Parser.h"
#include "kickcat/utils/xsd.h"

using namespace tinyxml2;

namespace kickcat::ENI
{
    namespace
    {
        std::string pathOf(XMLElement const* elem)
        {
            std::string path;
            for (XMLNode const* node = elem; node != nullptr and node->ToElement() != nullptr; node = node->Parent())
            {
                XMLElement const* e = node->ToElement();
                std::string part = e->Name();

                int index = 0;
                bool has_siblings = false;
                for (XMLElement const* s = e->PreviousSiblingElement(e->Name()); s != nullptr; s = s->PreviousSiblingElement(e->Name()))
                {
                    ++index;
                    has_siblings = true;
                }
                if (e->NextSiblingElement(e->Name()) != nullptr)
                {
                    has_siblings = true;
                }
                if (has_siblings)
                {
                    part += "[" + std::to_string(index) + "]";
                }

                if (path.empty())
                {
                    path = part;
                }
                else
                {
                    path = part + "/" + path;
                }
            }
            return path;
        }

        [[noreturn]] void fail(XMLElement const* where, std::string const& what)
        {
            throw std::invalid_argument("ENI: " + what + " in " + pathOf(where));
        }

        XMLElement* require(XMLElement* parent, char const* name)
        {
            XMLElement* child = parent->FirstChildElement(name);
            if (child == nullptr)
            {
                fail(parent, std::string{"missing mandatory <"} + name + ">");
            }
            return child;
        }

        std::string trim(std::string text)
        {
            auto not_space = [](unsigned char c) { return std::isspace(c) == 0; };
            text.erase(text.begin(), std::find_if(text.begin(), text.end(), not_space));
            text.erase(std::find_if(text.rbegin(), text.rend(), not_space).base(), text.end());
            return text;
        }

        // Every text and CDATA child: a comment inside a value must not cut it.
        std::string textOf(XMLElement const* elem)
        {
            std::string out;
            for (XMLNode const* node = elem->FirstChild(); node != nullptr; node = node->NextSibling())
            {
                if (XMLText const* text = node->ToText())
                {
                    out += text->Value();
                }
            }
            return trim(out);
        }

        std::optional<std::string> attributeOf(XMLElement const* elem, char const* name)
        {
            char const* raw = elem->Attribute(name);
            if (raw == nullptr)
            {
                return std::nullopt;
            }
            return trim(raw);
        }

        std::string optionalText(XMLElement* parent, char const* name)
        {
            XMLElement* child = parent->FirstChildElement(name);
            if (child == nullptr)
            {
                return {};
            }
            return textOf(child);
        }

        int64_t parseInteger(XMLElement const* elem, std::string const& text)
        {
            try
            {
                return xsd::parseHexDec(text);
            }
            catch (std::invalid_argument const& e)
            {
                fail(elem, e.what());
            }
        }

        template<typename T>
        T narrow(XMLElement const* elem, int64_t value)
        {
            static_assert(sizeof(T) < sizeof(int64_t), "narrow() needs a type narrower than int64_t");
            int64_t lo = static_cast<int64_t>(std::numeric_limits<T>::min());
            int64_t hi = static_cast<int64_t>(std::numeric_limits<T>::max());
            if (value < lo or value > hi)
            {
                fail(elem, "value " + std::to_string(value) + " out of range [" + std::to_string(lo) + ", " + std::to_string(hi) + "]");
            }
            return static_cast<T>(value);
        }

        // For identifiers and addresses only: the XSD declares them xs:int, so a value with the top
        // bit set is written negative (RevisionNo, Addr), and auto-increment positions as -1 or 65535.
        template<typename T>
        T narrowBits(XMLElement const* elem, int64_t value)
        {
            using U = std::make_unsigned_t<T>;
            static_assert(sizeof(U) < sizeof(int64_t), "narrowBits() needs a type narrower than int64_t");
            int64_t lo = static_cast<int64_t>(std::numeric_limits<std::make_signed_t<T>>::min());
            int64_t hi = static_cast<int64_t>(std::numeric_limits<U>::max());
            if (value < lo or value > hi)
            {
                fail(elem, "value " + std::to_string(value) + " out of range for a " + std::to_string(std::numeric_limits<U>::digits) + "-bit field");
            }
            return static_cast<T>(static_cast<U>(value));
        }

        enum Reading : uint8_t
        {
            VALUE,
            BIT_PATTERN,
        };

        template<typename T>
        T numberOf(XMLElement const* elem, Reading reading = VALUE)
        {
            int64_t value = parseInteger(elem, textOf(elem));
            if (reading == BIT_PATTERN)
            {
                return narrowBits<T>(elem, value);
            }
            return narrow<T>(elem, value);
        }

        template<typename T>
        T requireNumber(XMLElement* parent, char const* name, Reading reading = VALUE)
        {
            return numberOf<T>(require(parent, name), reading);
        }

        template<typename T>
        std::optional<T> optionalNumber(XMLElement* parent, char const* name, Reading reading = VALUE)
        {
            XMLElement* child = parent->FirstChildElement(name);
            if (child == nullptr)
            {
                return std::nullopt;
            }
            return numberOf<T>(child, reading);
        }

        bool parseBool(XMLElement const* elem, std::string const& raw)
        {
            try
            {
                return xsd::parseBoolean(raw);
            }
            catch (std::invalid_argument const& e)
            {
                fail(elem, e.what());
            }
        }

        bool optionalBool(XMLElement* parent, char const* name)
        {
            XMLElement* child = parent->FirstChildElement(name);
            if (child == nullptr)
            {
                return false;
            }
            return parseBool(child, textOf(child));
        }

        bool boolAttribute(XMLElement const* elem, char const* name)
        {
            std::optional<std::string> raw = attributeOf(elem, name);
            if (not raw)
            {
                return false;
            }
            return parseBool(elem, *raw);
        }

        void checkDatagramSize(XMLElement const* elem, std::size_t size)
        {
            if (size > MAX_ETHERCAT_PAYLOAD_SIZE)
            {
                fail(elem, "datagram data of " + std::to_string(size) + " bytes does not fit in a frame");
            }
        }

        std::vector<uint8_t> hexBinary(XMLElement const* elem)
        {
            try
            {
                return xsd::parseHexBinary(textOf(elem));
            }
            catch (std::invalid_argument const& e)
            {
                fail(elem, e.what());
            }
        }

        std::array<uint8_t, 6> macAddress(XMLElement* parent, char const* name)
        {
            XMLElement* elem = require(parent, name);
            std::vector<uint8_t> raw = hexBinary(elem);
            if (raw.size() != 6)
            {
                fail(elem, "MAC address must be 6 bytes");
            }
            std::array<uint8_t, 6> mac;
            std::copy(raw.begin(), raw.end(), mac.begin());
            return mac;
        }

        template<typename F>
        auto all(XMLElement* parent, char const* name, F parse)
        {
            std::vector<decltype(parse(parent))> out;
            for (XMLElement* child = parent->FirstChildElement(name); child != nullptr; child = child->NextSiblingElement(name))
            {
                out.push_back(parse(child));
            }
            return out;
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

        void parseAddress(XMLElement* node, uint16_t& adp, uint16_t& ado, std::optional<uint32_t>& logical)
        {
            XMLElement* addr = node->FirstChildElement("Addr");
            XMLElement* ado_node = node->FirstChildElement("Ado");
            if (addr != nullptr)
            {
                if (ado_node != nullptr or node->FirstChildElement("Adp") != nullptr)
                {
                    fail(node, "<Addr> cannot be combined with <Adp>/<Ado>");
                }
                logical = numberOf<uint32_t>(addr, BIT_PATTERN);
                return;
            }
            if (ado_node == nullptr)
            {
                fail(node, "missing <Ado> or <Addr>");
            }
            ado = numberOf<uint16_t>(ado_node);
            adp = optionalNumber<uint16_t>(node, "Adp", BIT_PATTERN).value_or(0);
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

        compare::Type parseCompare(XMLElement const* elem)
        {
            std::optional<std::string> raw = attributeOf(elem, "Type");
            if (not raw)
            {
                return compare::EQ;
            }
            std::string_view text = *raw;
            if (text == "EQ")      { return compare::EQ;      }
            if (text == "NOT_EQ")  { return compare::NOT_EQ;  }
            if (text == "EQ_OR_G") { return compare::EQ_OR_G; }
            if (text == "EQ_OR_L") { return compare::EQ_OR_L; }
            if (text == "G")       { return compare::G;       }
            if (text == "L")       { return compare::L;       }
            if (text == "NONE")    { return compare::NONE;    }
            fail(elem, std::string{"unknown Validate type '"} + *raw + "'");
        }

        void parseDatagram(XMLElement* node, Datagram& datagram)
        {
            datagram.comment = optionalText(node, "Comment");
            datagram.cmd = parseCommand(node);
            parseAddress(node, datagram.adp, datagram.ado, datagram.logical_address);

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
            datagram.wkc = optionalNumber<uint16_t>(node, "Cnt");
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
                v.data      = hexBinary(require(validate, "Data"));
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
                v.timeout   = milliseconds{requireNumber<uint32_t>(validate, "Timeout")};
                v.type      = parseCompare(validate);
                v.is_signed = boolAttribute(validate, "Signed");
                cmd.validate = std::move(v);
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
            if      (text == "AoE") { mbx.protocols |= protocol::AoE; }
            else if (text == "EoE") { mbx.protocols |= protocol::EoE; }
            else if (text == "CoE") { mbx.protocols |= protocol::CoE; }
            else if (text == "FoE") { mbx.protocols |= protocol::FoE; }
            else if (text == "SoE") { mbx.protocols |= protocol::SoE; }
            else if (text == "VoE") { mbx.protocols |= protocol::VoE; }
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

        SlaveInfo parseSlaveInfo(XMLElement* slave)
        {
            XMLElement* node = require(slave, "Info");
            SlaveInfo info;
            info.name          = optionalText(node, "Name");
            info.phys_addr     = optionalNumber<uint16_t>(node, "PhysAddr");
            info.auto_inc_addr = optionalNumber<uint16_t>(node, "AutoIncAddr", BIT_PATTERN);
            if (XMLElement* identification = node->FirstChildElement("Identification"))
            {
                std::optional<std::string> value = attributeOf(identification, "Value");
                if (not value)
                {
                    fail(identification, "missing mandatory @Value");
                }
                Identification id;
                id.ado   = requireNumber<uint16_t>(identification, "Ado");
                id.value = narrow<uint16_t>(identification, parseInteger(identification, *value));
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
            master.name        = optionalText(info, "Name");
            master.destination = macAddress(info, "Destination");
            master.source      = macAddress(info, "Source");
            if (XMLElement* ether_type = info->FirstChildElement("EtherType"))
            {
                master.ether_type = hexBinary(ether_type);
            }

            if (XMLElement* states = node->FirstChildElement("MailboxStates"))
            {
                MailboxStates ms;
                ms.start_addr = requireNumber<uint32_t>(states, "StartAddr", BIT_PATTERN);
                ms.count      = requireNumber<uint16_t>(states, "Count");
                master.mailbox_states = ms;
            }

            master.init_cmds = parseInitCmds(node);
            return master;
        }

        Config parseDocument(XMLDocument& doc)
        {
            XMLElement* root = doc.FirstChildElement("EtherCATConfig");
            if (root == nullptr)
            {
                throw std::invalid_argument("ENI: root element <EtherCATConfig> not found");
            }
            XMLElement* node = require(root, "Config");

            Config config;
            config.master = parseMaster(require(node, "Master"));
            config.slaves = all(node, "Slave", parseSlave);
            return config;
        }
    }

    Config loadFile(std::string const& path)
    {
        XMLDocument doc;
        if (doc.LoadFile(path.c_str()) != XML_SUCCESS)
        {
            throw std::invalid_argument("ENI: cannot load '" + path + "': " + doc.ErrorStr());
        }
        return parseDocument(doc);
    }

    Config loadString(std::string const& xml)
    {
        XMLDocument doc;
        if (doc.Parse(xml.c_str(), xml.size()) != XML_SUCCESS)
        {
            throw std::invalid_argument(std::string{"ENI: cannot parse document: "} + doc.ErrorStr());
        }
        return parseDocument(doc);
    }
}
