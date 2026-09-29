#include <stdexcept>
#include <string>

#include "kickcat/ENI/Config.h"

namespace kickcat::ENI
{
    namespace transition
    {
        namespace
        {
            struct Entry
            {
                Type transition;
                char const* name;
                State from;
                State to;
            };

            constexpr Entry TABLE[] =
            {
                {IP, "IP", INIT,        PRE_OP     },
                {PS, "PS", PRE_OP,      SAFE_OP    },
                {PI, "PI", PRE_OP,      INIT       },
                {SP, "SP", SAFE_OP,     PRE_OP     },
                {SO, "SO", SAFE_OP,     OPERATIONAL},
                {SI, "SI", SAFE_OP,     INIT       },
                {OS, "OS", OPERATIONAL, SAFE_OP    },
                {OP, "OP", OPERATIONAL, PRE_OP     },
                {OI, "OI", OPERATIONAL, INIT       },
                {IB, "IB", INIT,        BOOT       },
                {BI, "BI", BOOT,        INIT       },
                {II, "II", INIT,        INIT       },
                {PP, "PP", PRE_OP,      PRE_OP     },
                {SS, "SS", SAFE_OP,     SAFE_OP    },
                {PO, "PO", PRE_OP,      OPERATIONAL},
            };
        }

        char const* toString(Type t)
        {
            if (t == NONE)
            {
                return "NONE";
            }
            for (auto const& entry : TABLE)
            {
                if (entry.transition == t)
                {
                    return entry.name;
                }
            }
            return "unknown";
        }

        Type fromString(std::string_view text)
        {
            for (auto const& entry : TABLE)
            {
                if (text == entry.name)
                {
                    return entry.transition;
                }
            }
            throw std::invalid_argument("ENI: unknown Transition '" + std::string{text} + "'");
        }

        Type between(State from, State to)
        {
            for (auto const& entry : TABLE)
            {
                if ((entry.from == from) and (entry.to == to))
                {
                    return entry.transition;
                }
            }
            return NONE;
        }
    }

    namespace state
    {
        Type from(State s)
        {
            switch (s & State::MASK_STATE)
            {
                case State::INIT:        { return INIT;   }
                case State::PRE_OP:      { return PREOP;  }
                case State::SAFE_OP:     { return SAFEOP; }
                case State::OPERATIONAL: { return OP;     }
                default:                 { return NONE;   }
            }
        }
    }

    bool Validate::matches(uint8_t const* reply, std::size_t reply_size) const
    {
        if (reply_size < data.size())
        {
            return false;
        }

        // The most significant differing byte decides; only it carries the sign.
        int order = 0;
        std::size_t i = data.size();
        while (i > 0)
        {
            --i;
            uint8_t byte_mask = 0xFF;
            if (i < mask.size())
            {
                byte_mask = mask[i];
            }
            uint8_t got      = reply[i] & byte_mask;   // ETG.2100: the mask applies to the reply only
            uint8_t expected = data[i];
            if (got == expected)
            {
                continue;
            }

            bool greater = got > expected;
            if (is_signed and (i == data.size() - 1))
            {
                greater = static_cast<int8_t>(got) > static_cast<int8_t>(expected);
            }
            if (greater)
            {
                order = 1;
            }
            else
            {
                order = -1;
            }
            break;
        }

        switch (type)
        {
            case compare::EQ:      { return order == 0; }
            case compare::NOT_EQ:  { return order != 0; }
            case compare::EQ_OR_G: { return order >= 0; }
            case compare::EQ_OR_L: { return order <= 0; }
            case compare::G:       { return order >  0; }
            case compare::L:       { return order <  0; }
            case compare::NONE:    { return true; }
            default:               { return false; }
        }
    }

    uint32_t Datagram::address() const
    {
        if (logical_address)
        {
            return *logical_address;
        }
        return createAddress(adp, ado);
    }
}
