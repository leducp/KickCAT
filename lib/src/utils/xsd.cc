#include <cctype>
#include <cstdlib>
#include <stdexcept>
#include <string>

#include "kickcat/utils/xsd.h"

namespace kickcat::xsd
{
    namespace
    {
        bool isSpace(char c)
        {
            return std::isspace(static_cast<unsigned char>(c)) != 0;
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
        if ((text.rfind("#x", 0) == 0) or (text.rfind("0x", 0) == 0) or (text.rfind("0X", 0) == 0))
        {
            digits = text.substr(2);
            base = 16;
        }
        else if (not text.empty() and ((text[0] == '+') or (text[0] == '-')))
        {
            digits = text.substr(1);
        }

        bool valid = not digits.empty();
        for (char c : digits)
        {
            if (base == 16)
            {
                valid = valid and (std::isxdigit(static_cast<unsigned char>(c)) != 0);
            }
            else
            {
                valid = valid and (std::isdigit(static_cast<unsigned char>(c)) != 0);
            }
        }
        if (not valid)
        {
            throw std::invalid_argument("invalid numeric value " + quoted);
        }

        std::string const number{digits};
        try
        {
            if ((base == 10) and (text[0] == '-'))
            {
                return std::stoll(std::string{text}, nullptr, 10);
            }
            return static_cast<int64_t>(std::stoull(number, nullptr, base));
        }
        catch (std::out_of_range const&)
        {
            throw std::invalid_argument("numeric value " + quoted + " is out of range for 64 bits");
        }
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
            if ((std::isxdigit(static_cast<unsigned char>(field[i])) == 0) or (std::isxdigit(static_cast<unsigned char>(field[i + 1])) == 0))
            {
                throw std::invalid_argument("hexBinary contains non-hex pair '" + field.substr(i, 2) + "' at byte " + std::to_string(i / 2));
            }
            char pair[3] = {field[i], field[i + 1], '\0'};
            data.push_back(static_cast<uint8_t>(std::strtoul(pair, nullptr, 16)));
        }
        return data;
    }
}
