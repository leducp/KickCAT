#include "OS/PikeOS/ErrorCategory.h"

namespace kickcat
{
    std::error_category const& pikeos_category()
    {
        static pikeos_error_category category;
        return category;
    }
}
