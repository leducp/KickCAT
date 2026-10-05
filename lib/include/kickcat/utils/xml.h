#ifndef KICKCAT_UTILS_XML_H
#define KICKCAT_UTILS_XML_H

#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

#include <tinyxml2.h>

// Reading ESI (ETG.2000) and ENI (ETG.2100) documents.
// Errors throw std::invalid_argument naming the element they come from; withContext() lets a caller
// add what it was reading (the document kind, an object...).
namespace kickcat::xml
{
    /// Element names from the root, with the position among same-name siblings when there are several,
    /// e.g. "EtherCATConfig/Config/Slave[1]/InitCmds/InitCmd".
    std::string pathOf(tinyxml2::XMLElement const* elem);

    /// Throw "<what> in <path of where>".
    [[noreturn]] void fail(tinyxml2::XMLElement const* where, std::string const& what);

    /// Run parse(), prefixing the message of the std::invalid_argument it may throw with "<context>: ".
    template<typename F>
    auto withContext(std::string const& context, F&& parse) -> decltype(parse())
    {
        try
        {
            return parse();
        }
        catch (std::invalid_argument const& e)
        {
            throw std::invalid_argument(context + ": " + e.what());
        }
    }

    /// First child called name; throws when there is none.
    tinyxml2::XMLElement* require(tinyxml2::XMLElement* parent, char const* name);

    /// Every text and CDATA child, so that a comment inside a value does not cut it, without the
    /// surrounding XML whitespace.
    std::string textOf(tinyxml2::XMLElement const* elem);

    /// Text of the first child called name; empty when there is none.
    std::string optionalText(tinyxml2::XMLElement const* parent, char const* name);

    /// Text of the first child called name; throws when there is none or when it is empty.
    std::string requireText(tinyxml2::XMLElement* parent, char const* name);

    /// Attribute without the surrounding XML whitespace; nullopt when absent.
    std::optional<std::string> attributeOf(tinyxml2::XMLElement const* elem, char const* name);

    enum Reading : uint8_t
    {
        VALUE,          // the number must fit the type
        BIT_PATTERN,    // also its two's complement: xs:int identifiers write 0xFFFFFFFF as -1, positions -1 as 65535
    };

    /// HexDecValue read from text found in elem.
    int64_t parseInteger(tinyxml2::XMLElement const* elem, std::string const& text);

    template<typename T>
    T narrow(tinyxml2::XMLElement const* elem, int64_t value, Reading reading = VALUE)
    {
        static_assert(sizeof(T) < sizeof(int64_t), "narrow() needs a type narrower than int64_t");
        using U = std::make_unsigned_t<T>;
        int64_t lo = static_cast<int64_t>(std::numeric_limits<T>::min());
        int64_t hi = static_cast<int64_t>(std::numeric_limits<T>::max());
        if (reading == BIT_PATTERN)
        {
            lo = static_cast<int64_t>(std::numeric_limits<std::make_signed_t<T>>::min());
            hi = static_cast<int64_t>(std::numeric_limits<U>::max());
        }
        if ((value < lo) or (value > hi))
        {
            fail(elem, "value " + std::to_string(value) + " out of range [" + std::to_string(lo) + ", " + std::to_string(hi) + "]");
        }
        return static_cast<T>(static_cast<U>(value));
    }

    template<typename T>
    T numberOf(tinyxml2::XMLElement const* elem, Reading reading = VALUE)
    {
        return narrow<T>(elem, parseInteger(elem, textOf(elem)), reading);
    }

    template<typename T>
    T requireNumber(tinyxml2::XMLElement* parent, char const* name, Reading reading = VALUE)
    {
        return numberOf<T>(require(parent, name), reading);
    }

    template<typename T>
    std::optional<T> optionalNumber(tinyxml2::XMLElement const* parent, char const* name, Reading reading = VALUE)
    {
        tinyxml2::XMLElement const* child = parent->FirstChildElement(name);
        if (child == nullptr)
        {
            return std::nullopt;
        }
        return numberOf<T>(child, reading);
    }

    template<typename T>
    std::optional<T> numberAttribute(tinyxml2::XMLElement const* elem, char const* name, Reading reading = VALUE)
    {
        std::optional<std::string> raw = attributeOf(elem, name);
        if (not raw)
        {
            return std::nullopt;
        }
        return withContext(std::string{"@"} + name, [&]()
        {
            return narrow<T>(elem, parseInteger(elem, *raw), reading);
        });
    }

    /// xs:boolean read from text found in elem.
    bool parseBool(tinyxml2::XMLElement const* elem, std::string const& text);

    /// xs:boolean child called name; false when there is none.
    bool optionalBool(tinyxml2::XMLElement const* parent, char const* name);

    /// xs:boolean attribute; false when absent.
    bool boolAttribute(tinyxml2::XMLElement const* elem, char const* name);

    /// xs:hexBinary content of elem.
    std::vector<uint8_t> hexBinary(tinyxml2::XMLElement const* elem);

    /// parse() applied to every child called name, in document order.
    template<typename F>
    auto all(tinyxml2::XMLElement* parent, char const* name, F parse)
    {
        std::vector<decltype(parse(parent))> out;
        for (tinyxml2::XMLElement* child = parent->FirstChildElement(name); child != nullptr; child = child->NextSiblingElement(name))
        {
            out.push_back(parse(child));
        }
        return out;
    }
}

#endif
