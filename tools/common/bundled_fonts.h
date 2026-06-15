#ifndef KICKCAT_TOOLS_COMMON_BUNDLED_FONTS_H
#define KICKCAT_TOOLS_COMMON_BUNDLED_FONTS_H

namespace kickcat::gui
{
    struct BundledFont
    {
        char const*          name;
        unsigned char const* data;
        unsigned int         size;
    };

    // Fonts embedded at build time from tools/common/assets/fonts/*.ttf.
    // Returns the table and writes its length to *count.
    BundledFont const* bundledFonts(int* count);
}

#endif
