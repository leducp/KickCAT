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

TEST(ENIParser, io_master)
{
    ENI::Config config = ENI::loadFile("kickcat_eni_test_io.xml");

    ENI::Master const& master = config.master;
    EXPECT_EQ(master.name, "KickCAT test master");

    ASSERT_EQ(master.init_cmds.size(), 3);

    ENI::InitCmd const& count = master.init_cmds[0];
    EXPECT_EQ(count.transitions, transition::IP);
    EXPECT_TRUE(count.before_slave);
    EXPECT_EQ(count.comment, "read slave count");
    EXPECT_EQ(count.frame_requirement, ENI::requirement::CYCLE);
    EXPECT_EQ(count.cmd, Command::BRD);
    EXPECT_EQ(count.data, (std::vector<uint8_t>{0, 0}));
    EXPECT_FALSE(count.expected_wkc.has_value());
    EXPECT_EQ(count.retries, 0);

    ENI::InitCmd const& clear = master.init_cmds[1];
    EXPECT_TRUE(clear.before_slave);
    EXPECT_EQ(clear.frame_requirement, ENI::requirement::FRAME);
    EXPECT_EQ(clear.cmd, Command::BWR);
    EXPECT_EQ(clear.address, createAddress(0, 0x0600));
    EXPECT_EQ(clear.data.size(), 256);
    EXPECT_EQ(clear.retries, 3);

    ENI::InitCmd const& check = master.init_cmds[2];
    EXPECT_FALSE(check.before_slave);
    EXPECT_EQ(check.transitions, transition::PS);
    EXPECT_EQ(check.frame_requirement, ENI::requirement::NONE);
    EXPECT_EQ(check.expected_wkc, 3);
    ASSERT_TRUE(check.validate.has_value());
    EXPECT_EQ(check.validate->data, (std::vector<uint8_t>{0x04, 0x00}));
    EXPECT_EQ(check.validate->mask, (std::vector<uint8_t>{0x1f, 0x00}));
    EXPECT_EQ(check.validate->timeout, 2000ms);
}

TEST(ENIParser, io_slaves)
{
    ENI::Config config = ENI::loadFile("kickcat_eni_test_io.xml");
    ASSERT_EQ(config.slaves.size(), 3);

    ENI::Slave const& coupler = config.slaves[0];
    EXPECT_EQ(coupler.info.name, "Term 1 (EK1100)");
    EXPECT_EQ(coupler.info.phys_addr, 1001);
    EXPECT_EQ(coupler.info.auto_inc_addr, 0);
    EXPECT_EQ(coupler.info.vendor_id, 2u);
    EXPECT_EQ(coupler.info.product_code, 0x044c2c52u);
    EXPECT_EQ(coupler.info.revision_no, 0x00110000u);
    EXPECT_EQ(coupler.info.serial_no, 0u);
    EXPECT_FALSE(coupler.mailbox.has_value());
    EXPECT_TRUE(coupler.previous_ports.empty());
    ASSERT_EQ(coupler.init_cmds.size(), 2);
    EXPECT_EQ(coupler.init_cmds[0].transitions, transition::PI | transition::BI | transition::SI | transition::OI);
    EXPECT_EQ(coupler.init_cmds[1].cmd, Command::APWR);
    EXPECT_EQ(coupler.init_cmds[1].address, createAddress(0, 0x10));
    EXPECT_EQ(coupler.init_cmds[1].data, (std::vector<uint8_t>{0xe9, 0x03}));
    EXPECT_EQ(coupler.init_cmds[1].expected_wkc, 1);

    ASSERT_TRUE(coupler.dc.has_value());
    EXPECT_TRUE(coupler.dc->potential_reference_clock);
    EXPECT_TRUE(coupler.dc->reference_clock);
    EXPECT_EQ(coupler.dc->cycle_time0, 1ms);
    EXPECT_FALSE(coupler.dc->cycle_time1.has_value());
    EXPECT_EQ(coupler.dc->shift_time, 250us);

    ENI::Slave const& input = config.slaves[1];
    EXPECT_EQ(input.info.auto_inc_addr, 0xFFFF);
    EXPECT_FALSE(input.info.serial_no.has_value());
    ASSERT_EQ(input.process_data.recv.size(), 1);
    EXPECT_TRUE(input.process_data.send.empty());
    EXPECT_EQ(input.process_data.recv[0].bit_start, 0u);
    EXPECT_EQ(input.process_data.recv[0].bit_length, 4u);
    EXPECT_EQ(input.process_data.recv[0].sm_mask, 0);
    ASSERT_EQ(input.init_cmds.size(), 3);
    EXPECT_EQ(input.init_cmds[0].address, createAddress(0xFFFF, 0x10));
    EXPECT_EQ(input.init_cmds[1].cmd, Command::FPWR);
    EXPECT_EQ(input.init_cmds[1].data.size(), 16);
    EXPECT_EQ(input.init_cmds[2].transitions, transition::IP | transition::SP | transition::OP);
    ASSERT_TRUE(input.init_cmds[2].validate.has_value());
    ASSERT_EQ(input.previous_ports.size(), 1);
    EXPECT_TRUE(input.previous_ports[0].selected);
    EXPECT_EQ(input.previous_ports[0].port, 1);
    EXPECT_EQ(input.previous_ports[0].phys_addr, 1001);

    ENI::Slave const& output = config.slaves[2];
    ASSERT_EQ(output.process_data.send.size(), 1);
    EXPECT_EQ(output.process_data.send[0].sm_mask, 1 << 2);
    ASSERT_EQ(output.process_data.sms.size(), 1);
    ENI::SyncManagerSettings const& sm = output.process_data.sms[0];
    EXPECT_EQ(sm.index, 2);
    EXPECT_EQ(sm.type, SyncManager::Output);
    EXPECT_EQ(sm.default_size, 1);
    EXPECT_EQ(sm.min_size, 0);
    EXPECT_EQ(sm.start_address, 0x0f00);
    EXPECT_EQ(sm.control_byte, 0x44);
    EXPECT_TRUE(sm.enable);
    EXPECT_EQ(sm.pdos, (std::vector<uint16_t>{0x1600, 0x1601}));

    ASSERT_EQ(output.process_data.rx_pdos.size(), 1);
    EXPECT_TRUE(output.process_data.tx_pdos.empty());
    ENI::Pdo const& pdo = output.process_data.rx_pdos[0];
    EXPECT_EQ(pdo.index, 0x1600);
    EXPECT_EQ(pdo.name, "Channel 1");
    EXPECT_TRUE(pdo.fixed);
    EXPECT_FALSE(pdo.mandatory);
    EXPECT_EQ(pdo.sm, 2);
    ASSERT_EQ(pdo.entries.size(), 2);
    EXPECT_EQ(pdo.entries[0].index, 0x7000);
    EXPECT_EQ(pdo.entries[0].subindex, 1);
    EXPECT_EQ(pdo.entries[0].bitlen, 1);
    EXPECT_EQ(pdo.entries[0].type, CoE::DataType::BOOLEAN);
    EXPECT_EQ(pdo.entries[1].index, 0);
    EXPECT_EQ(pdo.entries[1].bitlen, 3);
    EXPECT_EQ(pdo.entries[1].type, CoE::DataType::UNKNOWN);

    ASSERT_EQ(output.previous_ports.size(), 2);
    EXPECT_TRUE(output.previous_ports[0].selected);
    EXPECT_FALSE(output.previous_ports[1].selected);
    EXPECT_EQ(output.previous_ports[1].port, 3);
    EXPECT_FALSE(output.previous_ports[1].phys_addr.has_value());
}

TEST(ENIParser, io_cyclic_and_process_image)
{
    ENI::Config config = ENI::loadFile("kickcat_eni_test_io.xml");

    ASSERT_EQ(config.cyclic.size(), 1);
    ENI::Cyclic const& cyclic = config.cyclic[0];
    EXPECT_EQ(cyclic.cycle_time, 1000us);
    ASSERT_EQ(cyclic.frames.size(), 1);
    ASSERT_EQ(cyclic.frames[0].cmds.size(), 3);

    ENI::CyclicCmd const& lrd = cyclic.frames[0].cmds[0];
    EXPECT_EQ(lrd.states, State::SAFE_OP | State::OPERATIONAL);
    EXPECT_EQ(lrd.comment, "cyclic cmd");
    EXPECT_EQ(lrd.cmd, Command::LRD);
    EXPECT_EQ(lrd.address, 0x11000u);
    EXPECT_EQ(lrd.data.size(), 1);
    EXPECT_EQ(lrd.expected_wkc, 1);
    EXPECT_EQ(lrd.input_offs, 26u);
    EXPECT_FALSE(lrd.output_offs.has_value());

    ENI::CyclicCmd const& lwr = cyclic.frames[0].cmds[1];
    EXPECT_EQ(lwr.cmd, Command::LWR);
    EXPECT_EQ(lwr.address, 0x12000u);
    EXPECT_FALSE(lwr.input_offs.has_value());
    EXPECT_EQ(lwr.output_offs, 26u);

    ENI::CyclicCmd const& brd = cyclic.frames[0].cmds[2];
    EXPECT_EQ(brd.states, State::PRE_OP | State::SAFE_OP | State::OPERATIONAL);
    EXPECT_EQ(brd.cmd, Command::BRD);
    EXPECT_EQ(brd.address, createAddress(0, 0x130));
    EXPECT_EQ(brd.data.size(), 2);

    ASSERT_TRUE(config.process_image.has_value());
    EXPECT_EQ(config.process_image->inputs.byte_size, 1536u);
    ASSERT_EQ(config.process_image->inputs.variables.size(), 1);
    EXPECT_EQ(config.process_image->inputs.variables[0].name, "Term 2 (EL1014).Channel 1.Input");
    EXPECT_EQ(config.process_image->inputs.variables[0].bit_offs, 208u);
    ASSERT_EQ(config.process_image->outputs.variables.size(), 1);
    EXPECT_EQ(config.process_image->outputs.variables[0].data_type, CoE::DataType::BOOLEAN);
    EXPECT_EQ(config.process_image->outputs.variables[0].bit_size, 1u);
}

TEST(ENIParser, data_types)
{
    auto pdo = [](std::string const& entry)
    {
        return wrapSlave("<ProcessData><TxPdo><Index>#x1a00</Index><Name>p</Name><Entry>" + entry + "</Entry></TxPdo></ProcessData>");
    };
    expectInvalid(pdo("<Index>#x6000</Index><BitLen>16</BitLen>"),                               "missing mandatory <DataType>");
    expectInvalid(pdo("<Index>#x6000</Index><BitLen>16</BitLen><DataType>WORD16</DataType>"),    "unknown base data type 'WORD16'");
    expectInvalid(pdo("<Index>#x6000</Index><BitLen>256</BitLen><DataType>UINT</DataType>"),     "out of range");

    ENI::Config config = ENI::loadString(pdo("<Index>0</Index><BitLen>4</BitLen>"));
    EXPECT_EQ(config.slaves.at(0).process_data.tx_pdos.at(0).entries.at(0).type, CoE::DataType::UNKNOWN);

    auto variable = [](std::string const& type)
    {
        return wrapConfig("<ProcessImage><Inputs><ByteSize>1</ByteSize><Variable><Name>v</Name>" + type
                        + "<BitSize>8</BitSize><BitOffs>0</BitOffs></Variable></Inputs></ProcessImage>");
    };
    config = ENI::loadString(variable(""));
    EXPECT_EQ(config.process_image->inputs.variables.at(0).data_type, CoE::DataType::UNKNOWN);
    config = ENI::loadString(variable("<DataType>USINT</DataType>"));
    EXPECT_EQ(config.process_image->inputs.variables.at(0).data_type, CoE::DataType::UNSIGNED8);
    expectInvalid(variable("<DataType>STRING(1)</DataType>"), "unknown base data type");
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
    expectInvalid(wrapSlave("<ProcessData><Recv><BitStart>0</BitStart><BitLength>-8</BitLength></Recv></ProcessData>"), "out of range");
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
    expectInvalid(wrapSlave("<ProcessData><Sm2><Type>Sideways</Type><StartAddress>0</StartAddress><ControlByte>0</ControlByte><Enable>1</Enable></Sm2></ProcessData>"),
                  "ProcessData/Sm2/Type");

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
        "<ProcessData><Sm2><Type>Outputs</Type><StartAddress>#x1800</StartAddress><ControlByte>#x64</ControlByte><Enable>true</Enable></Sm2>"
        "<Sm3><Type>Inputs</Type><StartAddress>#x1c00</StartAddress><ControlByte>#x20</ControlByte><Enable>false</Enable></Sm3></ProcessData>"
        "<DC><CycleTime0>1000000</CycleTime0><CycleTime1>-100000</CycleTime1><ShiftTime>-100000</ShiftTime></DC>"));
    ENI::Slave const& slave = config.slaves.at(0);
    ASSERT_EQ(slave.process_data.sms.size(), 2);
    EXPECT_TRUE(slave.process_data.sms[0].enable);
    EXPECT_FALSE(slave.process_data.sms[1].enable);
    ASSERT_TRUE(slave.dc.has_value());
    EXPECT_EQ(slave.dc->cycle_time1, -100us);

    auto cyclic = [](std::string const& task)
    {
        return wrapConfig("<Cyclic>" + task + "<Frame><Cmd><State>OP</State><Cmd>12</Cmd><Addr>65536</Addr><DataLength>2</DataLength></Cmd></Frame></Cyclic>");
    };
    ENI::Config config_task = ENI::loadString(cyclic("<Priority>1</Priority><TaskId>PlcTask</TaskId>"));
    EXPECT_FALSE(config_task.cyclic.at(0).cycle_time.has_value());
    EXPECT_EQ(config_task.cyclic.at(0).frames.at(0).cmds.size(), 1);
    expectInvalid(cyclic("<Priority>63</Priority>"), "Priority must be within [1, 62] in EtherCATConfig/Config/Cyclic/Priority");
}

TEST(ENIParser, datagram_must_fit_in_a_frame)
{
    constexpr std::size_t MAX_DATA = MAX_ETHERCAT_PAYLOAD_SIZE;
    std::string const fits = "<Cmd>8</Cmd><Ado>0</Ado><DataLength>" + std::to_string(MAX_DATA) + "</DataLength>";
    EXPECT_EQ(ENI::loadString(wrapInitCmd(fits)).slaves.at(0).init_cmds.at(0).data.size(), MAX_DATA);
    expectInvalid(wrapInitCmd("<Cmd>8</Cmd><Ado>0</Ado><DataLength>" + std::to_string(MAX_DATA + 1) + "</DataLength>"), "does not fit in a frame");
    expectInvalid(wrapInitCmd("<Cmd>8</Cmd><Ado>0</Ado><Data>" + std::string(2 * (MAX_DATA + 1), '0') + "</Data>"), "does not fit in a frame");

    expectInvalid(wrapConfig("<Cyclic><Frame><Cmd><State>OP</State><Cmd>12</Cmd><Addr>65536</Addr><DataLength>6000</DataLength></Cmd></Frame></Cyclic>"),
                  "does not fit in a frame");
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
        "<ProcessData><Send Sm2=\" true \"><BitStart>0</BitStart><BitLength>8</BitLength></Send>"
        "<RxPdo Sm=\" 2 \" Fixed=\" 1\"><Index>#x1600</Index><Name>p</Name></RxPdo></ProcessData>"
        "<InitCmds><InitCmd><Cmd>1</Cmd><Ado>0</Ado><Data>00</Data>"
        "<Validate Type=\" NONE \"><Data>00</Data><Timeout>1</Timeout></Validate></InitCmd></InitCmds>"));
    ENI::Slave const& slave = config.slaves.at(0);
    EXPECT_EQ(slave.process_data.send.at(0).sm_mask, 1 << 2);
    EXPECT_EQ(slave.process_data.rx_pdos.at(0).sm, 2);
    EXPECT_TRUE(slave.process_data.rx_pdos.at(0).fixed);
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
