#include "kickcat/utils/xml.h"
#include "kickcat/utils/xsd.h"

using namespace tinyxml2;

namespace kickcat::xml
{
    std::string pathOf(XMLElement const* elem)
    {
        std::string path;
        for (XMLNode const* node = elem; (node != nullptr) and (node->ToElement() != nullptr); node = node->Parent())
        {
            XMLElement const* e = node->ToElement();
            std::string part = e->Name();

            int index = 0;
            for (XMLElement const* s = e->PreviousSiblingElement(e->Name()); s != nullptr; s = s->PreviousSiblingElement(e->Name()))
            {
                ++index;
            }
            if ((index > 0) or (e->NextSiblingElement(e->Name()) != nullptr))
            {
                part += "[" + std::to_string(index) + "]";
            }

            if (path.empty())
            {
                path = part;
            }
            else
            {
                path = part + "/" + path;
            }
        }
        return path;
    }

    void fail(XMLElement const* where, std::string const& what)
    {
        throw std::invalid_argument(what + " in " + pathOf(where));
    }

    XMLElement* require(XMLElement* parent, char const* name)
    {
        XMLElement* child = parent->FirstChildElement(name);
        if (child == nullptr)
        {
            fail(parent, std::string{"missing mandatory <"} + name + ">");
        }
        return child;
    }

    std::string textOf(XMLElement const* elem)
    {
        std::string out;
        for (XMLNode const* node = elem->FirstChild(); node != nullptr; node = node->NextSibling())
        {
            if (XMLText const* text = node->ToText())
            {
                out += text->Value();
            }
        }
        return std::string{xsd::trim(out)};
    }

    std::string optionalText(XMLElement const* parent, char const* name)
    {
        XMLElement const* child = parent->FirstChildElement(name);
        if (child == nullptr)
        {
            return {};
        }
        return textOf(child);
    }

    std::string requireText(XMLElement* parent, char const* name)
    {
        XMLElement* child = require(parent, name);
        std::string text = textOf(child);
        if (text.empty())
        {
            fail(child, std::string{"empty <"} + name + ">");
        }
        return text;
    }

    std::optional<std::string> attributeOf(XMLElement const* elem, char const* name)
    {
        char const* raw = elem->Attribute(name);
        if (raw == nullptr)
        {
            return std::nullopt;
        }
        return std::string{xsd::trim(raw)};
    }

    int64_t parseInteger(XMLElement const* elem, std::string const& text)
    {
        try
        {
            return xsd::parseHexDec(text);
        }
        catch (std::invalid_argument const& e)
        {
            fail(elem, e.what());
        }
    }

    bool parseBool(XMLElement const* elem, std::string const& text)
    {
        try
        {
            return xsd::parseBoolean(text);
        }
        catch (std::invalid_argument const& e)
        {
            fail(elem, e.what());
        }
    }

    bool optionalBool(XMLElement const* parent, char const* name)
    {
        XMLElement const* child = parent->FirstChildElement(name);
        if (child == nullptr)
        {
            return false;
        }
        return parseBool(child, textOf(child));
    }

    bool boolAttribute(XMLElement const* elem, char const* name)
    {
        std::optional<std::string> raw = attributeOf(elem, name);
        if (not raw)
        {
            return false;
        }
        return withContext(std::string{"@"} + name, [&]()
        {
            return parseBool(elem, *raw);
        });
    }

    std::vector<uint8_t> hexBinary(XMLElement const* elem)
    {
        try
        {
            return xsd::parseHexBinary(textOf(elem));
        }
        catch (std::invalid_argument const& e)
        {
            fail(elem, e.what());
        }
    }
}
