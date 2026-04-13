#pragma once

#include <stdint.h>

#include "enum-utils.h"

enum class Width { };
enum class Height { };

struct ScreenDimensions
{
    Width width;
    Height height;

    bool operator==(const ScreenDimensions&) const = default;
};

enum class FPS { };

// For Graphics.
struct OpaqueWindow
{
    void* value;
};

namespace Editor
{
    enum class ID : uint64_t
    {
        Anonymous = sentinel_for<ID>
    };

    enum class Column : uint64_t
    {
        Beginning
    };

    enum class Tabstop { };

    enum class CursorLocus : uint64_t
    {
        Beginning,
        SelectionSentinel = sentinel_for<CursorLocus>
    };

    enum class LineEndingMode : uint8_t
    {
        Auto,
        CRLF,
        LF
    };

    enum class Length : uint64_t { };

    constexpr CursorLocus operator+(CursorLocus a, CursorLocus b)
    {
        return CursorLocus{ rep(a) + rep(b) };
    }

    constexpr CursorLocus operator+(CursorLocus l, PrimitiveType<CursorLocus> off)
    {
        return l + CursorLocus{ off };
    }

    constexpr CursorLocus operator+(CursorLocus l, Length len)
    {
        return l + CursorLocus{ rep(len) };
    }

    constexpr Length distance(CursorLocus first, CursorLocus last)
    {
        return Length{ rep(last) - rep(first) };
    }

    enum class CharOffset : uint64_t
    {
        Sentinel = sentinel_for<CharOffset>
    };

    constexpr CursorLocus seek(CharOffset off, Column c = Column::Beginning)
    {
        return CursorLocus{ rep(off) + rep(c) };
    }

    constexpr CharOffset as_offset(CursorLocus locus)
    {
        return CharOffset{ rep(locus) };
    }

    constexpr CharOffset operator+(CharOffset off, Length len)
    {
        return CharOffset{ rep(off) + rep(len) };
    }

    constexpr Column col(CharOffset first, CharOffset last)
    {
        return Column{ rep(last) - rep(first) };
    }

    constexpr Column col(CharOffset first, CursorLocus cursor)
    {
        return col(first, CharOffset{ rep(cursor) });
    }

    constexpr Length distance(CharOffset first, CharOffset last)
    {
        return Length{ rep(last) - rep(first) };
    }

    constexpr Length operator+(Length lhs, Length rhs)
    {
        return Length{ rep(lhs) + rep(rhs) };
    }

    constexpr Length operator-(Length lhs, Length rhs)
    {
        return Length{ rep(lhs) - rep(rhs) };
    }

    enum class EditSort
    {
        Insert,
        Deletion
    };

    struct GenericEdit
    {
        CharOffset first;
        union
        {
            Length len;      // EditSort::Insertion.
            CharOffset last; // EditSort::Deletion.
        };
        EditSort sort;
    };

    // A set of edits to help move cursors when snapping to nodes.
    struct EditCollection
    {
        GenericEdit* edits;
        uint64_t size;
    };

    // A data structure meant to track edits made to an underlying buffer.
    struct TrackedEdit
    {
        GenericEdit edit;
        TrackedEdit* next;
    };

    struct TrackedEditList
    {
        TrackedEdit* first;
        TrackedEdit* last;
        uint64_t count;
    };

    enum class CursorLine : uint64_t
    {
        IndexBeginning,
        Beginning
    };

    struct SearchResult
    {
        CharOffset first;
        CharOffset last;
        CursorLine line;

        bool operator==(const SearchResult&) const = default;
    };

    constexpr SearchResult nil_result = {
        .first = CharOffset::Sentinel,
        .last = CharOffset::Sentinel
    };

    struct CursorLineRange
    {
        CursorLine first;
        CursorLine last;
    };

    enum class BufferChangeSort : uint8_t
    {
        None,
        Complex,    // e.g. paste op or line movement.
        Delete,     // e.g. backspace or word deletion.
        SimpleChar, // e.g. entering a simple character sequence.
    };

    // Tells us whether or not a property is coming from a config value or something else.
    enum class PropertyOriginFromConfig : bool
    {
        No,
        Yes
    };
} // namespace Editor

namespace Glyph
{
    enum class FontSize { };

    enum class Tabstop { };

    enum class SpecialGlyph
    {
        Whitespace,
        CarriageReturn,
        ArrowDown,
        ArrowUp,
        ArrowRight,
        Tab = ArrowRight,
        ArrowLeft,
        Search,
        Replace,
        Reset = Replace,
        IncompleteCRLF,
        Warning = IncompleteCRLF,
        Checkmark,
        X,
        Copy,
        Terminal,
        Share,
        Wrench,
        Trash,
        Anchor,
        Filter,
        CircleQuestion,
        SaveDisk,
        Plus,
        Minus,
        CaseSensitive,
        WholeWord,
        Regex,

        Invalid,
        Count = Invalid
    };
} // namespace Glyph

namespace Render
{
    enum class FragShader : uint8_t
    {
        Image,
        TextSubpixel,
        Text,
        BasicColor = Text, // These two use the same shader so we can batch them.
        BlurHorizVert,
        Count
    };

    enum class VertShader : uint8_t
    {
        CameraTransform,
        NoTransform,
        OneOneTransform,
        Count
    };

    enum class BasicTexture : uint64_t
    {
        Invalid = sentinel_for<BasicTexture>
    };

    // Note: The general strategy for rendering to a framebuffer and rendering that result to another if this
    // framebuffer has alpha channels is to:
    // 1. Render to the framebuffer with default blending enabled.
    // 2. Bind the dest framebuffer.
    // 3. Apply the pre-multiplied alpha blending (as the src framebuffer had its alpha blended once already).
    // 4. Render the src framebuffer to the dest.
    // 5. Reset the blending mode.
    // Advice taken from: https://stackoverflow.com/questions/2171085/opengl-blending-with-previous-contents-of-framebuffer.
    enum class BlendingMode : uint8_t
    {
        PremultipliedAlpha, // GL_ONE, GL_ONE_MINUS_SRC_ALPHA
        SrcAlpha,           // GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA
        DualSourceBlend,    // GL_ONE, GL_ONE_MINUS_SRC1_COLOR
        Default,            // GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA
        Count
    };
} // namespace Render

namespace CmdBuffer
{
    enum class OffsetX : int { };
    enum class OffsetY : int { };

    struct ClipRect
    {
        OffsetX offset_x;
        OffsetY offset_y;
        Width width;
        Height height;

        static constexpr ClipRect basic(const ScreenDimensions& screen)
        {
            return { .offset_x = OffsetX{ },
                        .offset_y = OffsetY{ },
                        .width = screen.width,
                        .height = screen.height };
        }
    };

    enum class VertexOffset : uint32_t { };
    enum class IndexOffset : uint32_t { };
    enum class Cardinality : uint32_t { };

    struct DrawList;
} // namespace CmdBuffer

namespace OS
{
    enum class KeyMods : uint8_t
    {
        None    = 0,
        Shift   = 1u << 0,
        Alt     = 1u << 1,
        Ctrl    = 1u << 2,
        AnyDown = Shift | Alt | Ctrl,
    };

    enum class EventSort
    {
        Quit,
        Release,
        Press,
        MouseMove,
        Scroll,
        Text,
        FocusLost,
        FileDrop,
        GapThreadWakeup,
        Count
    };

    enum class Key : uint32_t
    {
        Null,
        Esc,
        F1,
        F2,
        F3,
        F4,
        F5,
        F6,
        F7,
        F8,
        F9,
        F10,
        F11,
        F12,
        F13,
        F14,
        F15,
        F16,
        F17,
        F18,
        F19,
        F20,
        F21,
        F22,
        F23,
        F24,
        Tick,
        Minus,
        Equal,
        Backspace,
        Tab,
        LeftBracket,
        RightBracket,
        BackSlash,
        CapsLock,
        Semicolon,
        Quote,
        Return,
        Shift,
        Comma,
        Period,
        Slash,
        Ctrl,
        Alt,
        Space,
        Menu,
        ScrollLock,
        Pause,
        _0,
        _1,
        _2,
        _3,
        _4,
        _5,
        _6,
        _7,
        _8,
        _9,
        Insert,
        Home,
        PageUp,
        Delete,
        End,
        PageDown,
        Up,
        Left,
        Down,
        Right,
        Ex0,
        Ex1,
        Ex2,
        Ex3,
        Ex4,
        Ex5,
        Ex6,
        Ex7,
        Ex8,
        Ex9,
        Ex10,
        Ex11,
        Ex12,
        Ex13,
        Ex14,
        Ex15,
        Ex16,
        Ex17,
        Ex18,
        Ex19,
        Ex20,
        Ex21,
        Ex22,
        Ex23,
        Ex24,
        Ex25,
        Ex26,
        Ex27,
        Ex28,
        A,
        B,
        C,
        D,
        E,
        F,
        G,
        H,
        I,
        J,
        K,
        L,
        M,
        N,
        O,
        P,
        Q,
        R,
        S,
        T,
        U,
        V,
        W,
        X,
        Y,
        Z,
        Ex29,
        NumLock,
        NumSlash,
        NumStar,
        NumMinus,
        NumPlus,
        NumPeriod,
        Num0,
        Num1,
        Num2,
        Num3,
        Num4,
        Num5,
        Num6,
        Num7,
        Num8,
        Num9,
        LeftMouseButton,
        MiddleMouseButton,
        RightMouseButton,
        Count
    };

    // Alpha numerics are positioned correctly.
    static_assert(rep(Key::A) == 'a');
    static_assert(rep(Key::Z) == 'z');

    static_assert(rep(Key::_0) == '0');
    static_assert(rep(Key::_9) == '9');

    enum class CursorStyle
    {
        Default,
        IBeam,
        Select,
        UpDownArrow,
        LeftRightArrow,
        SouthEastArrow,  // Arrow pointing South East.
        SouthWestArrow,  // Arrow pointing South West.
        SizeAll,
        Count
    };

    enum class Hz : uint32_t
    {
        Default = 60
    };

    enum class DPI : uint32_t { };

    // Largely borrowed from RADDBG.
    enum class WeekDay : uint32_t
    {
        Sun,
        Mon,
        Tue,
        Wed,
        Thu,
        Fri,
        Sat,
        Count
    };

    enum class Month : uint32_t
    {
        Jan,
        Feb,
        Mar,
        Apr,
        May,
        Jun,
        Jul,
        Aug,
        Sep,
        Oct,
        Nov,
        Dec,
        Count
    };

    struct DateTime
    {
        uint16_t micro_sec; // [0,999]
        uint16_t msec;      // [0,999]
        uint16_t sec;       // [0,60]
        uint16_t min;       // [0,59]
        uint16_t hour;      // [0,24]
        uint16_t day;       // [0,30]
        union
        {
            WeekDay week_day;
            uint32_t wday;
        };
        union
        {
            Month month;
            uint32_t mon;
        };
        uint32_t year; // 1 = 1 CE, 0 = 1 BC
    };

    enum class DenseTime : uint64_t { };
} // namespace OS

namespace Hotkeys
{
    enum class Hotkey : uint32_t
    {
        None,
#define DAT_CAT_START(e, grp, cat, desc) e ## _Start,
#define DAT_CMD(cmd, key, mods, desc) cmd,
#define DAT_CAT_END(e, grp) e ## _End,
#include "hotkeys.dat"
#undef DAT_CAT_START
#undef DAT_CMD
        Count
    };
#undef DAT_CAT_END

    enum class CustomHotkeyGroup : uint32_t
    {
        GLB,
        ED,
        FILE_EXP,
        FIND,
        SMCLI,
        Count
    };

    enum class CustomHotkeyID : uint64_t
    {
        None = 0
    };
} // namespace Hotkeys