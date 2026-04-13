#include "utf-8.h"

#include <cassert>

#include <bit>

#include "constants.h"
#include "enum-utils.h"

namespace UTF8
{
    namespace
    {
        constexpr Offset offset_of(ptrdiff_t diff)
        {
            return Offset{ static_cast<size_t>(diff) };
        }

        using UTF8Char = unsigned char;

        constexpr uint8_t ascii_end = 0x80;
    } // namespace [anon]

    CodepointResult next_codepoint(std::string_view input, Offset start)
    {
        if (rep(start) >= input.size())
            return invalid;
        auto first = input.data() + rep(start);
        auto last = input.data() + input.size();
        if (first == last)
            return invalid;
        // Wikipedia tells us where each codepoint starts/ends.
        // https://en.wikipedia.org/wiki/UTF-8
        // ASCII will never have byte 1 set: 0xxxxxxx
        if ((*first & ascii_end) == 0)
            return { .codepoint = static_cast<UTF8Char>(*first), .first = start, .last = extend(start) };

        // We're not in this range of UTF-8.  We need to compute the number of trailing
        // bytes to append to our sequence based on the number of active bits in the first
        // part of the the leading byte.
        // U+0080   U+07FF         110xxxxx    10xxxxxx
        // U+0800   U+FFFF         1110xxxx    10xxxxxx    10xxxxxx
        // U+10000  [b]U+10FFFF    11110xxx    10xxxxxx    10xxxxxx    10xxxxxx
        auto count = std::countl_one(static_cast<UTF8Char>(*first));
        // The count - 1 tells us how many bytes to consume after this.
        if (count - 1 <= 0)
            return invalid;
        count -= 1;
        // Cannot go past the end.
        if (rep(start) + count >= input.size())
            return invalid;
        Codepoint result { };
        // Chop the higher bits.
        UTF8Char c = *first;
        // Note: +1 because we want to chop the top-most UTF-8 bit as well.
        c <<= count + 1;
        c >>= count + 1;
        result = c;
        for (int i = 0; i < count; ++i)
        {
            c = *++first;
            // Shift the result down to accommodate.
            result <<= 6;
            // We only want to take the lower 6 bits of: 10xxxxxx.
            result |= c & 0x3f;
        }
        // Add an extra +1 because 'count' is inclusive.
        auto ending = Offset(rep(start) + count + 1);
        return { .codepoint = result, .first = start, .last = ending };
    }

    bool non_ascii_codepoint(unsigned char c)
    {
        return (c & ascii_end) != 0;
    }

    bool trailing_codepoint_byte(unsigned char c)
    {
        // Using our table:
        // U+0080   U+07FF         110xxxxx    10xxxxxx
        // U+0800   U+FFFF         1110xxxx    10xxxxxx    10xxxxxx
        // U+10000  [b]U+10FFFF    11110xxx    10xxxxxx    10xxxxxx    10xxxxxx
        //
        // We can observe that a non-leading UTF-8 byte will always be of the form
        // 10xxxxxx (specifically that the top bit is set and the next is not), so
        // we can mask of everything after testing the high bit to see if this is UTF-8
        // and then test if bit 7 (1-indexed) is not set.
        // Note: the 'non_ascii_codepoint' is exactly the test we want for the first part
        // due to evaluating bit 8 (the high bit) only.
        if (not non_ascii_codepoint(c))
            return false;
        // Test if bit 7 is set, if not then this is a trailing byte.
        return (c & 0x40) == 0;
    }

    bool ascii_codepoint(Codepoint cp)
    {
        return cp < ascii_end;
    }

    size_t codepoint_count(std::string_view input, Offset start)
    {
        CodepointWalker walker{ input, start };
        size_t count = 0;
        while (not walker.exhausted())
        {
            walker.next();
            ++count;
        }
        return count;
    }

    // Encoding.
    // Note: This returns a view over the input, so be sure that the output lives as long as the input.
    // Note: Borrowed from RADDBG.
    EncodeOutput utf8_encode(EncodeInput* in, Codepoint cp)
    {
        using namespace Constants;

        uint32_t len = 0;
        if (cp <= 0x7F)
        {
            in->buf[0] = static_cast<uint8_t>(cp);
            len = 1;
        }
        else if (cp <= 0x7FF)
        {
            in->buf[0] = (bitmask2 << 6) | ((cp >> 6) & bitmask5);
            in->buf[1] = bit8 | (cp & bitmask6);
            len = 2;
        }
        else if (cp <= 0xFFFF)
        {
            in->buf[0] = (bitmask3 << 5) | ((cp >> 12) & bitmask4);
            in->buf[1] = bit8 | ((cp >> 6) & bitmask6);
            in->buf[2] = bit8 | ( cp       & bitmask6);
            len = 3;
        }
        else if (cp <= 0x10FFFF)
        {
            in->buf[0] = (bitmask4 << 4) | ((cp >> 18) & bitmask3);
            in->buf[1] = bit8 | ((cp >> 12) & bitmask6);
            in->buf[2] = bit8 | ((cp >>  6) & bitmask6);
            in->buf[3] = bit8 | ( cp        & bitmask6);
            len = 4;
        }
        else
        {
            in->buf[0] = '?';
            len = 1;
        }
        return { reinterpret_cast<const char*>(in->buf), len };
    }

    CodepointWalker::CodepointWalker(std::string_view text, Offset start):
        text{ text }, current{ start } { }

    Codepoint CodepointWalker::next()
    {
        return next_result().codepoint;
    }

    CodepointResult CodepointWalker::next_result()
    {
        auto result = UTF8::next_codepoint(text, current);
        if (result == UTF8::invalid)
        {
            // Just advance.
            current = extend(current);
        }
        else
        {
            current = result.last;
        }
        return result;
    }

    bool CodepointWalker::exhausted() const
    {
        return rep(current) == text.size();
    }
} // namespace UTF8