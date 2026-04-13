#include "svg.h"

#include <cassert>

#include <memory>

#define NANOSVG_IMPLEMENTATION
#define NANOSVGRAST_IMPLEMENTATION
#ifdef WIN32
#pragma warning(push)
#pragma warning(disable: 4456) // declaration of 'name' hides previous local declaration
#pragma warning(disable: 4244) // '=': conversion from 'double' to 'float', possible loss of data
#pragma warning(disable: 4457) // declaration of 'path' hides function parameter
#pragma warning(disable: 4702) // unreachable code
#endif
#include "nanosvg.h"
#include "nanosvgrast.h"
#ifdef WIN32
#pragma warning(pop)
#endif

#include "config.h"
#include "feed.h"
#include "renderer.h"
#include "gap-strings.h"
#include "util.h"

namespace SVG
{
    namespace
    {
        struct RasterCleanup
        {
            void operator()(NSVGrasterizer* raster)
            {
                if (raster != nullptr)
                {
                    nsvgDeleteRasterizer(raster);
                }
            }
        };

        using RasterPtr = std::unique_ptr<NSVGrasterizer, RasterCleanup>;

        struct ImageCleanup
        {
            void operator()(NSVGimage* image)
            {
                if (image != nullptr)
                {
                    nsvgDelete(image);
                }
            }
        };

        using ImagePtr = std::unique_ptr<NSVGimage, ImageCleanup>;

        constinit RasterPtr rasterizer;
    } // namespace [anon]

    LoadSVGResult load_svg_asset(Assets::AssetID id, Feed::MessageFeed* feed)
    {
        LoadSVGResult result{};
        auto scratch = Arena::scratch_begin(Arena::no_conflicts);
        // Load.
        ImagePtr image{};
        Assets::AssetBuffer ass_buf;
        // Annoyingly, this only accepts a null-terminated string, but we should only be provided null-terminated strings.
        // +1 for null byte.
        ass_buf.len = rep(Assets::asset_length(id));
        ass_buf.buf = Arena::push_array_no_zero<unsigned char>(scratch.arena, ass_buf.len + 1);
        ass_buf.buf[ass_buf.len] = 0;
        if (not Assets::populate_asset(ass_buf, id))
        {
            auto desc = Assets::describe(id);
            char fmt_buf[512];
            String8 msg = fmt_string(fmt_buf, "Unable to load SVG asset: '%S': %S", desc.name, desc.proxy_file);
            feed->queue_error(msg);
            result.tex = Render::BasicTexture::Invalid;
        }

        if (result.tex != Render::BasicTexture::Invalid)
        {
            // Hardcode the DPI for now.
            constexpr int DPI = 96;
            image = ImagePtr{ nsvgParse(reinterpret_cast<char*>(ass_buf.buf), "px", DPI) };
            if (not image)
            {
                auto desc = Assets::describe(id);
                char fmt_buf[512];
                String8 msg = fmt_string(fmt_buf, "Unable to load SVG asset: '%S': %S", desc.name, desc.proxy_file);
                feed->queue_error(msg);
                result.tex = Render::BasicTexture::Invalid;
            }
        }

        if (result.tex != Render::BasicTexture::Invalid)
        {
            if (not rasterizer)
            {
                rasterizer = RasterPtr{ nsvgCreateRasterizer() };
            }

            if (not rasterizer)
            {
                feed->queue_error("Unable to create rasterizer");
                result.tex = Render::BasicTexture::Invalid;
            }
        }

        if (result.tex != Render::BasicTexture::Invalid)
        {
            int w = static_cast<int>(image->width);
            int h = static_cast<int>(image->height);
            float scale = 1.f;
            if (h != 32)
            {
                // Find the scale for which this will be 32.
                scale = 32.f / h;
                h = 32;
                w = static_cast<int>(scale * w);
            }
            uint64_t img_size = w*h*4;
            uint8_t* img = Arena::push_array_no_zero<uint8_t>(scratch.arena, img_size);
            nsvgRasterize(rasterizer.get(), image.get(), 0, 0, scale, img, w, h, w * 4);
            result.size = { .width = Width{ w }, .height = Height{ h } };
            result.tex = Render::create_basic_texture(result.size);
            Render::BasicTextureEntry entry{
                .offset_x = {},
                .offset_y = {},
                .width = result.size.width,
                .height = result.size.height,
                .buffer = img
            };
            Render::submit_basic_texture_data(result.tex, entry);
        }
        Arena::scratch_end(scratch);
        return result;
    }

    LoadSVGResult load_svg_asset(Assets::AssetID id, Feed::MessageFeed* feed, ScreenDimensions dim)
    {
        LoadSVGResult result{};
        auto scratch = Arena::scratch_begin(Arena::no_conflicts);
        // Load.
        ImagePtr image{};
        Assets::AssetBuffer ass_buf;
        // Annoyingly, this only accepts a null-terminated string, but we should only be provided null-terminated strings.
        // +1 for null byte.
        ass_buf.len = rep(Assets::asset_length(id));
        ass_buf.buf = Arena::push_array_no_zero<unsigned char>(scratch.arena, ass_buf.len + 1);
        ass_buf.buf[ass_buf.len] = 0;
        if (not Assets::populate_asset(ass_buf, id))
        {
            auto desc = Assets::describe(id);
            char fmt_buf[512];
            String8 msg = fmt_string(fmt_buf, "Unable to load SVG asset: '%S': %S", desc.name, desc.proxy_file);
            feed->queue_error(msg);
            result.tex = Render::BasicTexture::Invalid;
        }

        if (result.tex != Render::BasicTexture::Invalid)
        {
            // Hardcode the DPI for now.
            constexpr int DPI = 96;
            image = ImagePtr{ nsvgParse(reinterpret_cast<char*>(ass_buf.buf), "px", DPI) };
            if (not image)
            {
                auto desc = Assets::describe(id);
                char fmt_buf[512];
                String8 msg = fmt_string(fmt_buf, "Unable to load SVG asset: '%S': %S", desc.name, desc.proxy_file);
                feed->queue_error(msg);
                result.tex = Render::BasicTexture::Invalid;
            }
        }

        if (result.tex != Render::BasicTexture::Invalid)
        {
            if (not rasterizer)
            {
                rasterizer = RasterPtr{ nsvgCreateRasterizer() };
            }

            if (not rasterizer)
            {
                feed->queue_error("Unable to create rasterizer");
                result.tex = Render::BasicTexture::Invalid;
            }
        }

        if (result.tex != Render::BasicTexture::Invalid)
        {
            int w = static_cast<int>(image->width);
            int h = static_cast<int>(image->height);
            float scale = 1.f;
            if (h != rep(dim.height))
            {
                // Find the scale for which this will be height.
                scale = static_cast<float>(rep(dim.height)) / h;
                h = rep(dim.height);
                w = static_cast<int>(scale * w);
            }
            uint64_t img_size = w*h*4;
            uint8_t* img = Arena::push_array_no_zero<uint8_t>(scratch.arena, img_size);
            nsvgRasterize(rasterizer.get(), image.get(), 0, 0, scale, img, w, h, w * 4);
            result.size = { .width = Width{ w }, .height = Height{ h } };
            result.tex = Render::create_basic_texture(result.size);
            Render::BasicTextureEntry entry{
                .offset_x = {},
                .offset_y = {},
                .width = result.size.width,
                .height = result.size.height,
                .buffer = img
            };
            Render::submit_basic_texture_data(result.tex, entry);
        }
        Arena::scratch_end(scratch);
        return result;
    }
} // namespace SVG