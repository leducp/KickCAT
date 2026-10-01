#include <limits>
#include <stdexcept>
#include <string>

#include "kickcat/utils/xsd.h"

namespace kickcat::xsd
{
    namespace
    {
        bool isSpace(char c)
        {
            return (c == ' ') or (c == '\t') or (c == '\n') or (c == '\r');
        }

        // -1 when c is not a digit of base
        int digitValue(char c, int base)
        {
            int value = -1;
            if ((c >= '0') and (c <= '9'))
            {
                value = c - '0';
            }
            else if ((c >= 'a') and (c <= 'f'))
            {
                value = c - 'a' + 10;
            }
            else if ((c >= 'A') and (c <= 'F'))
            {
                value = c - 'A' + 10;
            }
            if (value >= base)
            {
                return -1;
            }
            return value;
        }

        std::string_view trim(std::string_view text)
        {
            while (not text.empty() and isSpace(text.front()))
            {
                text.remove_prefix(1);
            }
            while (not text.empty() and isSpace(text.back()))
            {
                text.remove_suffix(1);
            }
            return text;
        }
    }

    int64_t parseHexDec(std::string_view raw)
    {
        std::string_view text = trim(raw);
        std::string const quoted = "'" + std::string{text} + "'";

        std::string_view digits = text;
        int base = 10;
        bool negative = false;
        if ((text.rfind("#x", 0) == 0) or (text.rfind("0x", 0) == 0) or (text.rfind("0X", 0) == 0))
        {
            digits = text.substr(2);
            base = 16;
        }
        else if (not text.empty() and ((text[0] == '+') or (text[0] == '-')))
        {
            negative = (text[0] == '-');
            digits = text.substr(1);
        }
        if (digits.empty())
        {
            throw std::invalid_argument("invalid numeric value " + quoted);
        }

        constexpr uint64_t MAX = std::numeric_limits<uint64_t>::max();
        uint64_t value = 0;
        for (char c : digits)
        {
            int digit = digitValue(c, base);
            if (digit < 0)
            {
                throw std::invalid_argument("invalid numeric value " + quoted);
            }
            if (value > (MAX - static_cast<uint64_t>(digit)) / static_cast<uint64_t>(base))
            {
                throw std::invalid_argument("numeric value " + quoted + " is out of range for 64 bits");
            }
            value = value * static_cast<uint64_t>(base) + static_cast<uint64_t>(digit);
        }

        if (not negative)
        {
            return static_cast<int64_t>(value);
        }
        constexpr uint64_t MIN_MAGNITUDE = uint64_t{1} << 63;
        if (value > MIN_MAGNITUDE)
        {
            throw std::invalid_argument("numeric value " + quoted + " is out of range for 64 bits");
        }
        return static_cast<int64_t>(0 - value);
    }

    bool parseBoolean(std::string_view raw)
    {
        std::string_view text = trim(raw);
        if ((text == "1") or (text == "true"))
        {
            return true;
        }
        if ((text == "0") or (text == "false"))
        {
            return false;
        }
        throw std::invalid_argument("invalid xs:boolean '" + std::string{text} + "'");
    }

    std::vector<uint8_t> parseHexBinary(std::string_view text)
    {
        std::string field;
        field.reserve(text.size());
        for (char c : text)
        {
            if (not isSpace(c))
            {
                field.push_back(c);
            }
        }
        if (field.size() % 2 != 0)
        {
            throw std::invalid_argument("hexBinary has odd length (" + std::to_string(field.size()) + " chars)");
        }

        std::vector<uint8_t> data;
        data.reserve(field.size() / 2);
        for (std::size_t i = 0; i < field.size(); i += 2)
        {
            int high = digitValue(field[i], 16);
            int low  = digitValue(field[i + 1], 16);
            if ((high < 0) or (low < 0))
            {
                throw std::invalid_argument("hexBinary contains non-hex pair '" + field.substr(i, 2) + "' at byte " + std::to_string(i / 2));
            }
            data.push_back(static_cast<uint8_t>((high << 4) | low));
        }
        return data;
    }
}
