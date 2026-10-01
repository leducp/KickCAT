#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>

#include "kickcat/ENI/Sequencer.h"
#include "kickcat/debug.h"

namespace kickcat::ENI
{
    namespace
    {
        bool isAlControlWrite(InitCmd const& cmd, State target)
        {
            bool is_write = (cmd.cmd == Command::APWR) or (cmd.cmd == Command::FPWR) or (cmd.cmd == Command::BWR);
            if (not is_write or (cmd.ado != reg::AL_CONTROL) or cmd.data.empty())
            {
                return false;
            }
            return (cmd.data[0] & State::MASK_STATE) == target;
        }

        State nextStateUp(State current)
        {
            switch (current)
            {
                case State::INIT:    { return State::PRE_OP;      }
                case State::PRE_OP:  { return State::SAFE_OP;     }
                case State::SAFE_OP: { return State::OPERATIONAL; }
                default:             { return State::INVALID;     }
            }
        }

        bool isRequestable(State state)
        {
            return (state == State::INIT) or (state == State::PRE_OP) or (state == State::SAFE_OP) or (state == State::OPERATIONAL);
        }

        std::runtime_error slaveError(std::string const& name, std::string const& what)
        {
            return std::runtime_error("ENI: " + name + ": " + what);
        }

        std::string hex(uint32_t value)
        {
            char buffer[16];
            std::snprintf(buffer, sizeof(buffer), "0x%x", value);
            return buffer;
        }
    }


    Sequencer::Sequencer(Bus& bus, std::shared_ptr<AbstractLink> link, Config const& config)
        : bus_(bus)
        , link_(std::move(link))
        , config_(config)
    {
        std::size_t const count = config_.slaves.size();
        eni_of_position_.assign(count, count);
        position_of_eni_.assign(count, count);
        for (std::size_t i = 0; i < count; ++i)
        {
            std::size_t position = i;
            if (config_.slaves[i].info.auto_inc_addr)
            {
                position = static_cast<uint16_t>(0 - *config_.slaves[i].info.auto_inc_addr);
            }
            if ((position >= count) or (eni_of_position_[position] != count))
            {
                throw std::invalid_argument("ENI: " + config_.slaves[i].info.name + ": AutoIncAddr does not match a unique bus position");
            }
            eni_of_position_[position] = i;
            position_of_eni_[i] = position;
        }

    }


    ENI::Slave const& Sequencer::slaveAt(std::size_t position) const
    {
        return config_.slaves.at(eni_of_position_.at(position));
    }


    void Sequencer::requestState(State target, std::function<void()> const& background)
    {
        if (not isRequestable(target))
        {
            throw std::invalid_argument("ENI: only INIT, PRE_OP, SAFE_OP and OPERATIONAL can be requested");
        }
        if (not detected_)
        {
            detect();
        }

        if (target < state_)
        {
            transition(state_, target, background);
            return;
        }
        while (state_ < target)
        {
            transition(state_, nextStateUp(state_), background);
        }
    }


    void Sequencer::detect()
    {
        int32_t count = bus_.detectSlaves();
        if (static_cast<std::size_t>(count) != config_.slaves.size())
        {
            throw std::runtime_error("ENI: found " + std::to_string(count) + " slave(s), the ENI describes " + std::to_string(config_.slaves.size()));
        }
        detected_ = true;
    }


    void Sequencer::transition(State from, State to, std::function<void()> const& background)
    {
        transition::Type t = transition::between(from, to);
        if (t == transition::NONE)
        {
            throw std::invalid_argument(std::string{"ENI: no transition from "} + toString(from) + " to " + toString(to));
        }
        bus_info("ENI transition %s\n", transition::toString(t));

        for (auto const& cmd : config_.master.init_cmds)
        {
            if ((cmd.transitions & t) and cmd.before_slave)
            {
                execute("master", cmd, background);
            }
        }

        // IP CoE needs the mailbox, so it runs once PRE_OP is reached.
        std::vector<std::size_t> split(config_.slaves.size());
        for (std::size_t i = 0; i < config_.slaves.size(); ++i)
        {
            std::vector<InitCmd> const& cmds = config_.slaves[i].init_cmds;
            split[i] = cmds.size();
            for (std::size_t c = 0; c < cmds.size(); ++c)
            {
                if ((cmds[c].transitions & t) and isAlControlWrite(cmds[c], to))
                {
                    split[i] = c;
                    break;
                }
            }
            for (std::size_t c = 0; c < split[i]; ++c)
            {
                if (cmds[c].transitions & t)
                {
                    execute(config_.slaves[i].info.name, cmds[c], background);
                }
            }
        }

        if (t == transition::IP)
        {
            attachSlaves();
        }
        else
        {
            sendCoE(t, background);
        }

        for (std::size_t i = 0; i < config_.slaves.size(); ++i)
        {
            std::vector<InitCmd> const& cmds = config_.slaves[i].init_cmds;
            for (std::size_t c = split[i]; c < cmds.size(); ++c)
            {
                if (cmds[c].transitions & t)
                {
                    execute(config_.slaves[i].info.name, cmds[c], background);
                }
            }
        }

        if (t == transition::IP)
        {
            sendCoE(t, background);
        }

        for (auto const& cmd : config_.master.init_cmds)
        {
            if ((cmd.transitions & t) and not cmd.before_slave)
            {
                execute("master", cmd, background);
            }
        }

        state_ = to;
    }


    void Sequencer::execute(std::string const& who, InitCmd const& cmd, std::function<void()> const& background)
    {
        nanoseconds start = now();
        int32_t attempts = cmd.retries + 1;
        while (true)
        {
            std::vector<uint8_t> reply(cmd.data.size());
            uint16_t wkc = 0;
            bool received = false;
            auto process = [&](DatagramHeader const*, uint8_t const* data, uint16_t answer_wkc)
            {
                std::memcpy(reply.data(), data, reply.size());
                wkc = answer_wkc;
                received = true;
                return DatagramState::OK;
            };
            auto error = [](DatagramState const&) {};

            link_->addDatagram(cmd.cmd, cmd.address(), cmd.data.data(), static_cast<uint16_t>(cmd.data.size()), process, error);
            link_->processDatagrams();

            if (not received or (cmd.wkc and (wkc != *cmd.wkc)))
            {
                --attempts;
                if (attempts <= 0)
                {
                    throw slaveError(who, "'" + cmd.comment + "' failed (working counter " + std::to_string(wkc) + ")");
                }
                continue;
            }

            if (not cmd.validate or cmd.validate->matches(reply.data(), reply.size()))
            {
                return;
            }
            if (elapsed_time(start) > cmd.validate->timeout)
            {
                throw slaveError(who, "'" + cmd.comment + "' validate timeout");
            }
            background();
            sleep(1ms);
        }
    }


    void Sequencer::sendCoE(transition::Type t, std::function<void()> const& background)
    {
        for (std::size_t i = 0; i < config_.slaves.size(); ++i)
        {
            ENI::Slave const& eni = config_.slaves[i];
            if (not eni.mailbox)
            {
                continue;
            }
            kickcat::Slave& slave = bus_.slaves().at(position_of_eni_[i]);
            for (auto const& cmd : eni.mailbox->coe_init_cmds)
            {
                if (not (cmd.transitions & t) or cmd.disabled)
                {
                    continue;
                }

                nanoseconds timeout = cmd.timeout;
                if (timeout == 0ns)
                {
                    timeout = 1s;
                }
                Bus::Access access = Bus::Access::PARTIAL;
                if (cmd.complete_access)
                {
                    access = Bus::Access::COMPLETE;
                }

                if (cmd.ccs == 1)
                {
                    bus_.writeSDO(slave, cmd.index, cmd.subindex, access, cmd.data.data(), static_cast<uint32_t>(cmd.data.size()), timeout);
                }
                else
                {
                    std::vector<uint8_t> buffer(slave.mailbox.send_size);
                    uint32_t size = static_cast<uint32_t>(buffer.size());
                    bus_.readSDO(slave, cmd.index, cmd.subindex, access, buffer.data(), &size, timeout);
                }
                background();
            }
        }
    }


    std::vector<uint8_t> Sequencer::read(std::size_t position, uint16_t ado, uint16_t size)
    {
        std::vector<uint8_t> out(size);
        auto process = [&out](DatagramHeader const*, uint8_t const* data, uint16_t wkc)
        {
            if (wkc != 1)
            {
                return DatagramState::INVALID_WKC;
            }
            std::memcpy(out.data(), data, out.size());
            return DatagramState::OK;
        };
        auto error = [](DatagramState const& state)
        {
            THROW_ERROR_DATAGRAM("ENI: register read back failed", state);
        };
        link_->addDatagram(Command::FPRD, createAddress(bus_.slaves().at(position).address, ado), nullptr, size, process, error);
        link_->processDatagrams();
        return out;
    }


    void Sequencer::attachSlaves()
    {
        std::vector<kickcat::Slave>& slaves = bus_.slaves();
        for (std::size_t position = 0; position < slaves.size(); ++position)
        {
            ENI::Slave const& eni = slaveAt(position);
            if (not eni.info.phys_addr)
            {
                throw std::invalid_argument("ENI: " + eni.info.name + ": no PhysAddr");
            }
            slaves[position].address = *eni.info.phys_addr;
        }

        bus_.fetchEeprom();
        bus_.fetchESC();
        bus_.fetchDL();
        checkIdentification();

        for (std::size_t position = 0; position < slaves.size(); ++position)
        {
            checkIdentity(position);
            checkMailbox(position);
        }

        if (not handlers_attached_)
        {
            for (auto& slave : slaves)
            {
                bus_.addMailboxHandlers(slave);
            }
            handlers_attached_ = true;
        }
    }


    void Sequencer::checkIdentity(std::size_t position)
    {
        ENI::SlaveInfo const& eni = slaveAt(position).info;
        eeprom::InfoEntry const& sii = bus_.slaves()[position].sii.info;
        bool same = (sii.vendor_id == eni.vendor_id) and (sii.product_code == eni.product_code) and (sii.revision_number == eni.revision_no);
        if (eni.serial_no and (*eni.serial_no != 0))
        {
            same = same and (sii.serial_number == *eni.serial_no);
        }
        if (not same)
        {
            throw slaveError(eni.name, "identity differs from the SII (vendor " + hex(sii.vendor_id)
                + ", product " + hex(sii.product_code) + ", revision " + hex(sii.revision_number) + ")");
        }
    }


    void Sequencer::checkMailbox(std::size_t position)
    {
        ENI::Slave const& eni = slaveAt(position);
        kickcat::Slave& slave = bus_.slaves()[position];
        if (not eni.mailbox)
        {
            return;
        }
        if ((eni.mailbox->protocols & ~slave.sii.info.mailbox_protocol) != 0)
        {
            throw slaveError(eni.info.name, "mailbox protocols not supported by the SII");
        }

        // The mailbox the slave runs is the one written to SM0/SM1, whoever wrote it.
        SyncManager::Register sm[2];
        std::memcpy(sm, read(position, reg::SYNC_MANAGER, sizeof(sm)).data(), sizeof(sm));
        bool enabled = (sm[0].activate & 1) and (sm[1].activate & 1) and (sm[0].length > 0) and (sm[1].length > 0);
        bool as_eni  = (sm[0].start_address == eni.mailbox->send.start) and (sm[0].length == eni.mailbox->send.length)
                   and (sm[1].start_address == eni.mailbox->recv.start) and (sm[1].length == eni.mailbox->recv.length);
        if (not enabled or not as_eni)
        {
            throw slaveError(eni.info.name, "SM0/SM1 do not hold the mailbox the ENI describes");
        }

        if ((sm[0].start_address != slave.mailbox.recv_offset) or (sm[0].length != slave.mailbox.recv_size)
            or (sm[1].start_address != slave.mailbox.send_offset) or (sm[1].length != slave.mailbox.send_size))
        {
            bus_info("ENI: %s: mailbox layout differs from the SII default\n", eni.info.name.c_str());
        }
        slave.mailbox.recv_offset = sm[0].start_address;
        slave.mailbox.recv_size   = sm[0].length;
        slave.mailbox.send_offset = sm[1].start_address;
        slave.mailbox.send_size   = sm[1].length;
    }


    void Sequencer::checkIdentification()
    {
        std::vector<kickcat::Slave>& slaves = bus_.slaves();
        for (std::size_t position = 0; position < slaves.size(); ++position)
        {
            ENI::SlaveInfo const& eni = slaveAt(position).info;
            if (not eni.identification)
            {
                continue;
            }

            uint16_t value = 0;
            std::memcpy(&value, read(position, eni.identification->ado, sizeof(value)).data(), sizeof(value));
            if (value != eni.identification->value)
            {
                throw slaveError(eni.name, "explicit identification " + hex(value) + " differs from " + hex(eni.identification->value));
            }
        }
    }
}
