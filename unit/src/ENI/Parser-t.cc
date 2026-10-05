#include <gtest/gtest.h>

#include "kickcat/ENI/Parser.h"

using namespace kickcat;

namespace
{
    std::string wrapConfig(std::string const& body)
    {
        return "<EtherCATConfig><Config><Master><Info><Name>m</Name>"
               "<Destination>ffffffffffff</Destination><Source>000000000000</Source></Info></Master>"
            + body
            + "</Config></EtherCATConfig>";
    }

    std::string wrapSlave(std::string const& slave_body)
    {
        return wrapConfig("<Slave><Info><Name>s</Name><VendorId>1</VendorId><ProductCode>2</ProductCode><RevisionNo>3</RevisionNo></Info>"
            + slave_body + "</Slave>");
    }

    std::string wrapInitCmd(std::string const& cmd_body)
    {
        return wrapSlave("<InitCmds><InitCmd>" + cmd_body + "</InitCmd></InitCmds>");
    }

    void expectInvalid(std::string const& xml, std::string const& fragment)
    {
        try
        {
            (void) ENI::loadString(xml);
            FAIL() << "expected std::invalid_argument containing '" << fragment << "'";
        }
        catch (std::invalid_argument const& e)
        {
            EXPECT_NE(std::string{e.what()}.find(fragment), std::string::npos) << e.what();
        }
    }
}

TEST(ENIParser, load_errors)
{
    ASSERT_THROW((void) ENI::loadFile("does_not_exist.xml"), std::invalid_argument);
    ASSERT_THROW((void) ENI::loadString(""),                 std::invalid_argument);
    expectInvalid("<EtherCATInfo/>", "<EtherCATConfig>");
    expectInvalid("<EtherCATConfig/>", "missing mandatory <Config>");
    expectInvalid("<EtherCATConfig><Config/></EtherCATConfig>", "missing mandatory <Master>");
}

TEST(ENIParser, mailbox_slave)
{
    ENI::Config config = ENI::loadFile("kickcat_eni_test_mailbox.xml");

    EXPECT_TRUE(config.master.init_cmds.empty());
    EXPECT_TRUE(config.cyclic.empty());
    EXPECT_FALSE(config.process_image.has_value());

    ASSERT_EQ(config.slaves.size(), 1);
    ENI::Slave const& slave = config.slaves[0];
    EXPECT_EQ(slave.info.name, "Box 1 (BK1120)");
    EXPECT_FALSE(slave.info.auto_inc_addr.has_value());
    EXPECT_FALSE(slave.info.serial_no.has_value());

    ASSERT_TRUE(slave.mailbox.has_value());
    ENI::Mailbox const& mbx = *slave.mailbox;
    EXPECT_EQ(mbx.send.start, 6144);
    EXPECT_EQ(mbx.send.length, 264);
    EXPECT_EQ(mbx.recv.start, 7168);
    EXPECT_EQ(mbx.recv.length, 264);
    EXPECT_EQ(mbx.poll_time, 20ms);
    EXPECT_EQ(mbx.status_bit_addr, 0u);
    ASSERT_TRUE(mbx.bootstrap_send.has_value());
    EXPECT_EQ(mbx.bootstrap_send->start, 0x1000);
    EXPECT_EQ(mbx.bootstrap_recv->start, 0x1200);
    EXPECT_EQ(mbx.bootstrap_recv->length, 0x200);
    EXPECT_EQ(mbx.protocols, eeprom::MailboxProtocol::EoE | eeprom::MailboxProtocol::CoE | eeprom::MailboxProtocol::FoE);
    EXPECT_EQ(mbx.unsupported_init_cmds, 1u);

    ASSERT_EQ(mbx.coe_init_cmds.size(), 3);
    ENI::CoEInitCmd const& ip = mbx.coe_init_cmds[0];
    EXPECT_EQ(ip.transitions, transition::PS);
    EXPECT_EQ(ip.comment, "IP address");
    EXPECT_EQ(ip.timeout, 0ms);
    EXPECT_EQ(ip.ccs, 1);
    EXPECT_EQ(ip.index, 0x1111);
    EXPECT_EQ(ip.subindex, 1);
    EXPECT_EQ(ip.data, (std::vector<uint8_t>{0xe9, 0x03, 0xfe, 0xa9}));
    EXPECT_FALSE(ip.complete_access);
    EXPECT_FALSE(ip.fixed);
    EXPECT_FALSE(ip.disabled);

    ENI::CoEInitCmd const& assign = mbx.coe_init_cmds[1];
    EXPECT_EQ(assign.transitions, transition::PS | transition::IP);
    EXPECT_EQ(assign.timeout, 500ms);
    EXPECT_EQ(assign.index, 0x1c12);
    EXPECT_TRUE(assign.complete_access);
    EXPECT_TRUE(assign.fixed);
    EXPECT_FALSE(assign.disabled);

    ENI::CoEInitCmd const& read = mbx.coe_init_cmds[2];
    EXPECT_EQ(read.ccs, 2);
    EXPECT_TRUE(read.data.empty());
    EXPECT_TRUE(read.disabled);

    ASSERT_EQ(slave.init_cmds.size(), 2);
    EXPECT_EQ(slave.init_cmds[0].transitions, transition::IP | transition::IB);
    EXPECT_EQ(slave.init_cmds[0].data.size(), 8);
    ENI::InitCmd const& nop = slave.init_cmds[1];
    EXPECT_EQ(nop.transitions, transition::II);
    EXPECT_EQ(nop.cmd, Command::NOP);
    EXPECT_EQ(nop.address, createAddress(0, 0));
    EXPECT_TRUE(nop.data.empty());
    EXPECT_EQ(nop.timeout, 50ms);
    EXPECT_FALSE(nop.validate.has_value());
}

TEST(ENIParser, hexdec_and_signed_values)
{
    ENI::Config config = ENI::loadString(wrapInitCmd("<Cmd>#x2</Cmd><Adp>-1</Adp><Ado>0x0120</Ado><Data>0100</Data>"));
    ENI::InitCmd const& cmd = config.slaves.at(0).init_cmds.at(0);
    EXPECT_EQ(cmd.cmd, Command::APWR);
    EXPECT_EQ(cmd.address, createAddress(0xFFFF, 0x0120));
    EXPECT_EQ(cmd.transitions, 0);
}

TEST(ENIParser, negative_values)
{
    expectInvalid(wrapInitCmd("<Cmd>1</Cmd><Ado>0</Ado><DataLength>-1</DataLength>"), "value -1 out of range [0, 65535]");
    expectInvalid(wrapInitCmd("<Cmd>1</Cmd><Ado>0</Ado><Data>00</Data><Cnt>-1</Cnt>"), "out of range");
    expectInvalid(wrapInitCmd("<Cmd>1</Cmd><Ado>-16</Ado>"), "out of range");
    expectInvalid(wrapSlave("<DC><CycleTime0>-1000000</CycleTime0></DC>"), "out of range");

    // xs:int identifiers and addresses written with the top bit set
    ENI::Config config = ENI::loadString(wrapConfig(
        "<Slave><Info><Name>s</Name><AutoIncAddr>-1</AutoIncAddr><VendorId>2</VendorId><ProductCode>-1</ProductCode>"
        "<RevisionNo>-2147483647</RevisionNo></Info><DC><ShiftTime>-250000</ShiftTime></DC>"
        "<InitCmds><InitCmd><Cmd>10</Cmd><Addr>-65536</Addr><DataLength>1</DataLength></InitCmd></InitCmds></Slave>"));
    ENI::Slave const& s = config.slaves.at(0);
    EXPECT_EQ(s.info.auto_inc_addr, 0xFFFF);
    EXPECT_EQ(s.info.product_code, 0xFFFFFFFFu);
    EXPECT_EQ(s.info.revision_no, 0x80000001u);
    EXPECT_EQ(s.dc->shift_time, -250us);
    EXPECT_EQ(s.init_cmds.at(0).address, 0xFFFF0000u);
}

TEST(ENIParser, malformed_init_cmds)
{
    expectInvalid(wrapInitCmd("<Adp>0</Adp><Ado>0</Ado>"),                       "missing mandatory <Cmd>");
    expectInvalid(wrapInitCmd("<Cmd>15</Cmd><Ado>0</Ado>"),                      "unknown command 15");
    expectInvalid(wrapInitCmd("<Cmd>abc</Cmd><Ado>0</Ado>"),                     "invalid numeric value");
    expectInvalid(wrapInitCmd("<Cmd>1</Cmd><Adp>0</Adp>"),                       "missing <Ado>");
    expectInvalid(wrapInitCmd("<Cmd>10</Cmd><DataLength>1</DataLength>"),        "missing <Addr> for a logical command");
    expectInvalid(wrapInitCmd("<Cmd>10</Cmd><Adp>0</Adp><Addr>0</Addr>"),        "not <Adp>/<Ado>");
    expectInvalid(wrapInitCmd("<Cmd>12</Cmd><Ado>0</Ado>"),                      "not <Adp>/<Ado>");
    expectInvalid(wrapInitCmd("<Cmd>4</Cmd><Addr>#x10000</Addr>"),               "<Addr> is only for logical commands");
    expectInvalid(wrapInitCmd("<Cmd>1</Cmd><Ado>0</Ado><Data>00</Data><DataLength>1</DataLength>"), "<DataLength>");
    expectInvalid(wrapInitCmd("<Cmd>1</Cmd><Ado>0</Ado><Data>0</Data>"),         "odd length");
    expectInvalid(wrapInitCmd("<Cmd>1</Cmd><Ado>0</Ado><Data>zz</Data>"),        "non-hex");
    expectInvalid(wrapInitCmd("<Cmd>1</Cmd><Ado>70000</Ado>"),                   "out of range");
    expectInvalid(wrapInitCmd("<Transition>XY</Transition><Cmd>1</Cmd><Ado>0</Ado>"), "unknown Transition 'XY'");
    expectInvalid(wrapInitCmd("<Cmd>1</Cmd><Ado>0</Ado><Requires>always</Requires>"), "unknown Requires");
    expectInvalid(wrapInitCmd("<BeforeSlave>yes</BeforeSlave><Cmd>1</Cmd><Ado>0</Ado>"), "invalid xs:boolean");
    expectInvalid(wrapInitCmd("<Cmd>1</Cmd><Ado>0</Ado><Data>00</Data><Validate><Data>00</Data></Validate>"), "missing mandatory <Timeout>");
    expectInvalid(wrapInitCmd("<Cmd>1</Cmd><Ado>0</Ado><Data>0000</Data><Validate><Data>0000</Data><DataMask>00</DataMask><Timeout>1</Timeout></Validate>"), "DataMask size");
    expectInvalid(wrapInitCmd("<Cmd>1</Cmd><Ado>0</Ado><Data>00</Data><Validate Type=\"GT\"><Data>00</Data><Timeout>1</Timeout></Validate>"), "unknown Validate type");
    expectInvalid(wrapInitCmd("<Cmd>1</Cmd><Ado>0</Ado><Data>00</Data><Validate Type=\"EQ_OR_G\"><Data>00</Data><Timeout>1</Timeout></Validate>"), "unsupported Validate type 'EQ_OR_G'");
    expectInvalid(wrapInitCmd("<Cmd>1</Cmd><Ado>0</Ado><Data>00</Data><Validate><Data>0000</Data><Timeout>1</Timeout></Validate>"), "longer than the command data");
}

TEST(ENIParser, validate_shorter_than_command_data)
{
    ENI::Config config = ENI::loadString(wrapInitCmd("<Cmd>4</Cmd><Ado>304</Ado><DataLength>4</DataLength>"
                                                     "<Validate><Data>0400</Data><DataMask>1f00</DataMask><Timeout>1</Timeout></Validate>"));
    ENI::InitCmd const& cmd = config.slaves.at(0).init_cmds.at(0);
    EXPECT_EQ(cmd.data.size(), 4);
    ASSERT_TRUE(cmd.validate.has_value());
    EXPECT_EQ(cmd.validate->data.size(), 2);
}

TEST(ENIParser, validate_types)
{
    ENI::Config config = ENI::loadString(wrapInitCmd("<Cmd>4</Cmd><Ado>304</Ado><Data>0000</Data>"
                                                     "<Validate Type=\"EQ\" Signed=\"true\"><Data>0400</Data><Timeout>7</Timeout></Validate>"));
    ENI::InitCmd const& eq = config.slaves.at(0).init_cmds.at(0);
    ASSERT_TRUE(eq.validate.has_value());
    EXPECT_EQ(eq.validate->timeout, 7ms);

    config = ENI::loadString(wrapInitCmd("<Cmd>4</Cmd><Ado>304</Ado><Data>0000</Data>"
                                         "<Validate Type=\"NONE\"><Data>0400</Data><Timeout>7</Timeout></Validate>"));
    ENI::InitCmd const& none = config.slaves.at(0).init_cmds.at(0);
    EXPECT_FALSE(none.validate.has_value());
    EXPECT_EQ(none.timeout, 7ms);
}

TEST(ENIParser, malformed_slave_and_master)
{
    expectInvalid(wrapSlave("<PreviousPort><Port>E</Port></PreviousPort>"), "port must be A, B, C or D");
    expectInvalid(wrapSlave("<Mailbox><Send><Start>0</Start><Length>1</Length></Send></Mailbox>"), "missing mandatory <Recv>");
    expectInvalid(wrapSlave("<Mailbox><Send><Start>0</Start><Length>1</Length></Send><Recv><Start>0</Start><Length>1</Length></Recv>"
                            "<Protocol>XoE</Protocol></Mailbox>"), "unknown mailbox protocol");
    expectInvalid(wrapSlave("<Mailbox><Send><Start>0</Start><Length>1</Length></Send><Recv><Start>0</Start><Length>1</Length></Recv>"
                            "<CoE><InitCmds><InitCmd><Timeout>0</Timeout><Ccs>1</Ccs><Index>0</Index><SubIndex>0</SubIndex></InitCmd></InitCmds></CoE></Mailbox>"),
                  "missing mandatory <Transition>");

    expectInvalid("<EtherCATConfig><Config><Master><Info><Destination>ffff</Destination><Source>000000000000</Source></Info></Master></Config></EtherCATConfig>",
                  "MAC address must be 6 bytes");
    expectInvalid("<EtherCATConfig><Config><Master><Info><Destination>ffffffffffff</Destination><Source>0000</Source></Info></Master></Config></EtherCATConfig>",
                  "MAC address must be 6 bytes in EtherCATConfig/Config/Master/Info/Source");
    expectInvalid("<EtherCATConfig><Config><Master><Info><Destination>ffffffffffff</Destination><Source>000000000000</Source>"
                  "<EtherType>0800</EtherType></Info></Master></Config></EtherCATConfig>",
                  "EtherType must be EtherCAT (88a4)");
    expectInvalid("<EtherCATConfig><Config><Master><Info><Destination>ffffffffffff</Destination><Source>000000000000</Source></Info>"
                  "<MailboxStates><StartAddr>#x9000000</StartAddr></MailboxStates></Master></Config></EtherCATConfig>",
                  "missing mandatory <Count> in EtherCATConfig/Config/Master/MailboxStates");
    expectInvalid("<EtherCATConfig><Config><Master><Info><Destination>ffffffffffff</Destination><Source>000000000000</Source></Info></Master>"
                  "<Slave><Info><Name>s</Name><VendorId>1</VendorId><ProductCode>2</ProductCode></Info></Slave></Config></EtherCATConfig>",
                  "missing mandatory <RevisionNo>");
}

TEST(ENIParser, error_path_names_the_element)
{
    std::string const good = "<Slave><Info><Name>a</Name><VendorId>1</VendorId><ProductCode>2</ProductCode><RevisionNo>3</RevisionNo></Info></Slave>";
    std::string const bad  = "<Slave><Info><Name>b</Name><VendorId>1</VendorId><ProductCode>2</ProductCode><RevisionNo>3</RevisionNo></Info>"
                             "<InitCmds><InitCmd><Cmd>1</Cmd></InitCmd></InitCmds></Slave>";
    expectInvalid(wrapConfig(good + bad), "EtherCATConfig/Config/Slave[1]/InitCmds/InitCmd");
}


TEST(ENIParser, xsd_types)
{
    ENI::Config config = ENI::loadString(wrapSlave(
        "<DC><CycleTime0>1000000</CycleTime0><CycleTime1>-100000</CycleTime1><ShiftTime>-100000</ShiftTime></DC>"));
    ENI::Slave const& slave = config.slaves.at(0);
    ASSERT_TRUE(slave.dc.has_value());
    EXPECT_EQ(slave.dc->cycle_time1, -100us);
}

TEST(ENIParser, datagram_must_fit_in_a_frame)
{
    constexpr std::size_t MAX_DATA = MAX_ETHERCAT_PAYLOAD_SIZE;
    std::string const fits = "<Cmd>8</Cmd><Ado>0</Ado><DataLength>" + std::to_string(MAX_DATA) + "</DataLength>";
    EXPECT_EQ(ENI::loadString(wrapInitCmd(fits)).slaves.at(0).init_cmds.at(0).data.size(), MAX_DATA);
    expectInvalid(wrapInitCmd("<Cmd>8</Cmd><Ado>0</Ado><DataLength>" + std::to_string(MAX_DATA + 1) + "</DataLength>"), "does not fit in a frame");
    expectInvalid(wrapInitCmd("<Cmd>8</Cmd><Ado>0</Ado><Data>" + std::string(2 * (MAX_DATA + 1), '0') + "</Data>"), "does not fit in a frame");
}

TEST(ENIParser, strict_numbers)
{
    expectInvalid(wrapInitCmd("<Cmd>1</Cmd><Ado>#x0x120</Ado>"), "invalid numeric value");
    expectInvalid(wrapInitCmd("<Cmd>1</Cmd><Ado>0x0x120</Ado>"), "invalid numeric value");
    expectInvalid(wrapInitCmd("<Cmd>1</Cmd><Ado>#x 12</Ado>"),   "invalid numeric value");
    expectInvalid(wrapInitCmd("<Cmd>1</Cmd><Adp>#x-1</Adp><Ado>0</Ado>"), "invalid numeric value");
    expectInvalid(wrapInitCmd("<Cmd>1</Cmd><Ado>1 2</Ado>"),     "invalid numeric value");
    expectInvalid(wrapInitCmd("<Cmd>1</Cmd><Ado>#x</Ado>"),      "invalid numeric value");
    expectInvalid(wrapInitCmd("<Cmd>1</Cmd><Ado>-</Ado>"),       "invalid numeric value");
    expectInvalid(wrapInitCmd("<Cmd>1</Cmd><Ado>#xffffffffffffffffff</Ado>"), "out of range");

    ENI::Config config = ENI::loadString(wrapInitCmd("<Cmd>+2</Cmd><Adp>-1</Adp><Ado> #x120 </Ado>"));
    ENI::InitCmd const& cmd = config.slaves.at(0).init_cmds.at(0);
    EXPECT_EQ(cmd.cmd, Command::APWR);
    EXPECT_EQ(cmd.address, createAddress(0xFFFF, 0x120));
}

TEST(ENIParser, text_spans_comments_and_cdata)
{
    ENI::Config config = ENI::loadString(wrapInitCmd("<Cmd>2</Cmd><Ado>#x120</Ado><Data>01<!-- split -->02<![CDATA[03]]></Data>"));
    ENI::InitCmd const& cmd = config.slaves.at(0).init_cmds.at(0);
    EXPECT_EQ(cmd.data, (std::vector<uint8_t>{0x01, 0x02, 0x03}));
}

TEST(ENIParser, attributes_are_trimmed)
{
    ENI::Config config = ENI::loadString(wrapSlave(
        "<InitCmds><InitCmd><Cmd>1</Cmd><Ado>0</Ado><Data>00</Data>"
        "<Validate Type=\" NONE \"><Data>00</Data><Timeout>1</Timeout></Validate></InitCmd></InitCmds>"));
    ENI::Slave const& slave = config.slaves.at(0);
    EXPECT_FALSE(slave.init_cmds.at(0).validate.has_value());
}

TEST(ENIParser, identification)
{
    std::string const slave = wrapConfig(
        "<Slave><Info><Name>s</Name><PhysAddr>1001</PhysAddr><AutoIncAddr>0</AutoIncAddr>"
        "<Identification Value=\"#x0159\"><Ado>#x0012</Ado></Identification>"
        "<VendorId>2</VendorId><ProductCode>3</ProductCode><RevisionNo>4</RevisionNo></Info></Slave>");
    ENI::Config config = ENI::loadString(slave);
    ENI::SlaveInfo const& info = config.slaves.at(0).info;
    ASSERT_TRUE(info.identification.has_value());
    EXPECT_EQ(info.identification->ado, 0x0012);
    EXPECT_EQ(info.identification->value, 0x0159);

    std::string missing = slave;
    missing.replace(missing.find(" Value=\"#x0159\""), std::string{" Value=\"#x0159\""}.size(), "");
    expectInvalid(missing, "missing mandatory @Value");
}
