#ifndef KICKAT_PIKEOS_ERROR_CATEGORY_H
#define KICKAT_PIKEOS_ERROR_CATEGORY_H

#include "kickcat/Error.h"

namespace kickcat
{
    class pikeos_error_category : public std::error_category
    {
        char const* name() const noexcept override
        {
            return "PikeOS";
        }

        std::string message(int condition) const override
        {
            return p4_strerror(condition);
        }
    };
    std::error_category const& pikeos_category();

    #define THROW_PIKEOS_ERROR(code, msg)     (throw std::system_error(code, kickcat::pikeos_category(), LOCATION(": " msg)))
}

#endif
