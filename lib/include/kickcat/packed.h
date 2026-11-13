#ifndef KICKCAT_PACKED_H
#define KICKCAT_PACKED_H

// Portable declaration of a packed struct (no padding between the members, alignment of 1):
//
//   KICKCAT_PACKED_BEGIN
//   struct Foo
//   {
//       ...
//   } KICKCAT_PACKED_END;
//
// MSVC does not know __attribute__((__packed__)): the struct is enclosed in #pragma pack instead.
#if defined(_MSC_VER) && !defined(__clang__)
    #define KICKCAT_PACKED_BEGIN __pragma(pack(push, 1))
    #define KICKCAT_PACKED_END   __pragma(pack(pop))
#else
    #define KICKCAT_PACKED_BEGIN
    #define KICKCAT_PACKED_END   __attribute__((__packed__))
#endif

#endif
