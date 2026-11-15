#ifndef KICKCAT_ENI_PARSER_H
#define KICKCAT_ENI_PARSER_H

#include <string>

#include "kickcat/ENI/Config.h"

namespace kickcat::ENI
{
    // Throw std::invalid_argument on malformed input, with the offending element path.
    Config loadFile(std::string const& path);
    Config loadString(std::string const& xml);
}

#endif
