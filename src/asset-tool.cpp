#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#include "arena.h"
#include "gap-core.h"
#include "os.h"
#include "util.h"

#define MINIZ_NO_STDIO
#define MINIZ_NO_INFLATE_APIS
#include "miniz.h"

HumanFileSize to_human_readable_file_size(Arena::Arena* arena, OS::FileLength size)
{
    HumanFileSize result{};
    int order = 0;
    double mantissa = static_cast<double>(size);
    for (; mantissa >= 1024.; mantissa /= 1024., ++order);
    char c = "BKMGTPE"[order];
    // We shouldn't get strings longer than this.
    char fmt_buf[100];
    String8 file_size = str8_empty;
    if (order == 0)
    {
        file_size = fmt_string(fmt_buf, "%dB", int(size));
    }
    else
    {
        file_size = fmt_string(fmt_buf, "%.1f%cB", float(mantissa), c);
    }
    result.string = str8_copy(arena, file_size);
    result.mag = static_cast<FileSizeMagnitude>(order);
    return result;
}

bool read_entire_file(Arena::Arena* arena, String8* buf, String8 file_path)
{
    auto file = OS::open_file(file_path, OS::FileAccess::Read | OS::FileAccess::ShareRead);
    if (file == OS::FileHandle::Sentinel)
        return false;
    auto file_size = OS::file_length(file);
    auto read_len = OS::read_file(arena, buf, file, file_size);
    bool success = read_len == file_size;
    OS::close_file(file);
    return success;
}

struct CompressInput
{
    String8 asset_name;
    String8 base_path;
    String8 asset_file;
    Arena::Arena* arena;
    OS::FileHandle out_file;
    OS::FileOffset write_off;
};

struct CompressResult
{
    uint64_t cmp_size;
    uint64_t uncmp_size;
    OS::FileOffset new_off;
    bool success = false;
};

CompressResult compress_asset(CompressInput in)
{
    CompressResult result{};
    String8 path = OS::combine_paths(in.arena, in.base_path, in.asset_file);
    auto file = OS::open_file(path, OS::FileAccess::Read | OS::FileAccess::ShareRead);
    if (file == OS::FileHandle::Sentinel)
    {
        fprintf(stderr, "ERROR: Cannot open file '%s'\n", path.str);
        result.success = false;
        return result;
    }
    auto file_size = OS::file_length(file);
    String8 file_buf = str8_empty;
    auto read_len = OS::read_file(in.arena, &file_buf, file, file_size);
    result.uncmp_size = rep(read_len);
    const bool success = read_len == file_size;
    OS::close_file(file);
    if (not success)
    {
        fprintf(stderr, "ERROR: Failed to read entire file '%s'\n", path.str);
        result.success = false;
        return result;
    }

    uLong cmp_len = compressBound(uLong(read_len));
    uLong uncompress_len = uLong(read_len);

    // Buffer for compression.
    mz_uint8* cmp_stream = Arena::push_array_no_zero<mz_uint8>(in.arena, cmp_len);
    int cmp_status = compress(cmp_stream, &cmp_len, reinterpret_cast<const unsigned char*>(file_buf.str), uncompress_len);
    if (cmp_status != Z_OK)
    {
        fprintf(stderr, "compress() failed!\n");
        result.success = false;
        return result;
    }
    // Generate the array.
    // Let's have a 4K buffer to do this.
    constexpr size_t buf_size = KB(4);
    char* const buf = Arena::push_array_no_zero<char>(in.arena, buf_size);
    String8 arr_base = fmt_string(buf, buf_size, "CompressedAsset asset_%S={%lu,%lu,{", in.asset_name, uncompress_len, cmp_len);
    OS::FileOffset off = in.write_off;
    auto written = OS::write_file(in.out_file, off, arr_base);
    if (rep(written) != arr_base.size)
    {
        result.success = false;
        fprintf(stderr, "ERROR: Failed to write to output file for asset '%s'\n", in.asset_name.str);
        return result;
    }
    off = extend(off, rep(written));

    // We'll write in chunks of 4K bytes.
    size_t current_len = 0;
    for (size_t i = 0; i < cmp_len; ++i)
    {
        // If we need to flush the buffer, do it now and start appending again.
        // We're doing a conservative estimate because the digit cannot be more than 255 (3 chars)
        // plus a comma character.
        constexpr int bump_amount_est = 5;
        if (current_len + bump_amount_est >= buf_size)
        {
            written = OS::write_file(in.out_file, off, str8(buf, current_len));
            if (rep(written) != current_len)
            {
                result.success = false;
                fprintf(stderr, "ERROR: Failed to write to output file for asset '%s'\n", in.asset_name.str);
                return result;
            }
            off = extend(off, rep(written));
            current_len = 0;
        }

        // Append this digit.
        unsigned char c = cmp_stream[i];
        // Just use all 3 slots to make it easy.
        char* dest = buf + current_len;
        dest[0] = ' ';
        dest[1] = ' ';
        dest[2] = '0';
        int idx = 2;
        unsigned char d = 0;
        while (c != 0)
        {
            d = c % 10;
            dest[idx] = '0' + d;
            c /= 10;
            --idx;
        }
        dest[3] = ',';
        current_len += 4;
    }

    if (current_len != 0)
    {
        written = OS::write_file(in.out_file, off, str8(buf, current_len));
        if (rep(written) != current_len)
        {
            result.success = false;
            fprintf(stderr, "ERROR: Failed to write to output file for asset '%s'\n", in.asset_name.str);
            return result;
        }
        off = extend(off, rep(written));
    }

    // Close the array.
    constexpr String8View closer = str8_literal("}};\n");
    written = OS::write_file(in.out_file, off, str8_mut(closer));
    if (rep(written) != closer.size)
    {
        result.success = false;
        fprintf(stderr, "ERROR: Failed to write to output file for asset '%s'\n", in.asset_name.str);
        return result;
    }
    off = extend(off, rep(written));
    double r = 1. - double(cmp_len) / double(uncompress_len);
    printf("Compressed from '%s' '%lu' -> '%lu' bytes (%.2f%%)\n", in.asset_file.str, uncompress_len, cmp_len, float(r * 100.));
    result.success = true;
    result.cmp_size = cmp_len;
    result.new_off = off;
    return result;
}

int gap_main_entry(int, char**)
{
    // The structure of the resulting array will be:
    // struct CompressedAsset { uint64_t len; uint64_t cmp_len; unsigned char arr[]; };
    // CompressedAsset asset1[] = { 0, 0, {0,2,3,66,6,...} };
    // CompressedAsset asset2[] = { 0, 0, {0,2,3,66,6,...} };
    // CompressedAsset* all_assets[] = { &asset1, &asset2 };
    Arena::Arena* arena = Arena::alloc(Arena::default_params);
    uint64_t uncmp_total_sizes{};
    uint64_t cmp_total_sizes{};
    String8 exe_path = str8_empty;
    auto err = OS::exe_path(arena, &exe_path);
    if (err != OS::Error::None)
    {
        fprintf(stderr, "ERROR: %s\n", OS::error_text(err).str);
        return 1;
    }
    // Create output folder.
    constexpr String8View generated_folder = str8_literal("gen");
    String8 path_gen = OS::combine_paths(arena, exe_path, str8_mut(generated_folder));
    err = OS::create_directory(path_gen);
    if (err != OS::Error::None)
    {
        fprintf(stderr, "ERROR: Failed to create directory '%s': %s\n", path_gen.str, OS::error_text(err).str);
        return 1;
    }
    // Open the output file.
    constexpr String8View output_filename = str8_literal("gen" PATH_SEP "gen-assets.h");
    String8 path = OS::combine_paths(arena, exe_path, str8_mut(output_filename));
    OS::FileHandle out_file = OS::open_file(path, OS::FileAccess::Write | OS::FileAccess::ShareWrite);
    if (out_file == OS::FileHandle::Sentinel)
    {
        fprintf(stderr, "ERROR: Failed to open file '%s'\n", path.str);
        return 1;
    }

    CompressInput input{};
    input.base_path = exe_path;
    input.arena = arena;
    input.out_file = out_file;
    input.write_off = {};

    constexpr String8View cmp_asset_def = str8_literal("struct CompressedAsset { uint64_t len; uint64_t cmp_len; unsigned char arr[]; };\n");
    auto written = OS::write_file(out_file, input.write_off, str8_mut(cmp_asset_def));
    if (rep(written) != cmp_asset_def.size)
    {
        fprintf(stderr, "ERROR: Failed to write to output file for combined asset\n");
        OS::close_file(out_file);
        return 1;
    }
    input.write_off = extend(input.write_off, rep(written));

#define DAT_ASSET(e, path) input.asset_name = str8_mut(str8_literal(#e));   \
                           input.asset_file = str8_mut(str8_literal(path)); \
{                                                                           \
    auto scratch = Arena::temp_begin(input.arena);                          \
    auto r = compress_asset(input);                                         \
    if (not r.success) { OS::close_file(out_file); return 1; }              \
    uncmp_total_sizes += r.uncmp_size; cmp_total_sizes += r.cmp_size;       \
    Arena::temp_end(scratch);                                               \
    input.write_off = r.new_off; }
#include "assets.dat"
#undef DAT_ASSET

    // Define the array to index into each asset.
    constexpr String8View asset_array = str8_literal(
    "const CompressedAsset* all_assets[]={"
#define DAT_ASSET(e, path) "&asset_" #e ","
#include "assets.dat"
#undef DAT_ASSET
    "};");

    written = OS::write_file(out_file, input.write_off, str8_mut(asset_array));
    if (rep(written) != asset_array.size)
    {
        fprintf(stderr, "ERROR: Failed to write to output file for combined asset\n");
        OS::close_file(out_file);
        return 1;
    }

    // Finalize the file.
    OS::close_file(out_file);

    HumanFileSize readable_sz{};
    readable_sz = to_human_readable_file_size(arena, OS::FileLength{ uncmp_total_sizes });
    printf("Uncompressed sizes: %" PRId64 "(%s)\n", uncmp_total_sizes, readable_sz.string.str);
    readable_sz = to_human_readable_file_size(arena, OS::FileLength{ cmp_total_sizes });
    printf("Compressed sizes: %" PRId64 "(%s)\n", cmp_total_sizes, readable_sz.string.str);
    double r = 1. - double(cmp_total_sizes) / double(uncmp_total_sizes);
    printf("Ratio: %.2f%%\n", float(r * 100.));
    return 0;
}

// --- IMPLEMENTATION ------------------------------------------
// For global frame updates e.g. via an event loop.
struct RenderCoreData;
void update_frame(RenderCoreData*) { }

#include "arena.cpp"
#include "gap-strings.cpp"
#include "os-common.cpp"
#include "thread-ctx.cpp"

// Third-party.
#define STB_SPRINTF_IMPLEMENTATION
#include <stb_sprintf.h>
// Undef so it can be included below.
#undef STB_SPRINTF_IMPLEMENTATION

SUPPRESS_IF_CONSTEXPR_SUGGEST_WARNING();
#include "miniz.c"
ENABLE_IF_CONSTEXPR_SUGGEST_WARNING();
#undef MINIZ_NO_STDIO
#undef MINIZ_NO_INFLATE_APIS

// Platform-specific.
#ifdef WIN32
// Windows goes last.
#include "os-win32.cpp"
#else // ^^^ WIN32 ^^^ / vvv !WIN32 vvv
#include "os-linux.cpp"
#endif // WIN32

#ifdef BUILD_PROFILED
#include "TracyClient.cpp"
#endif // BUILD_PROFILED