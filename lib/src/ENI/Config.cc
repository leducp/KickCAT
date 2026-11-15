#include "kickcat/ENI/Config.h"

namespace kickcat::ENI
{
    bool Validate::matches(uint8_t const* reply, std::size_t reply_size) const
    {
        if (reply_size < data.size())
        {
            return false;
        }

        for (std::size_t i = 0; i < data.size(); ++i)
        {
            uint8_t byte_mask = 0xFF;
            if (i < mask.size())
            {
                byte_mask = mask[i];
            }
            if ((reply[i] & byte_mask) != data[i])   // ETG.2100: the mask applies to the reply only
            {
                return false;
            }
        }
        return true;
    }
}
