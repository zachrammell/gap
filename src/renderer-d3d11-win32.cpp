#include "renderer.h"

#include "assets.h"
#include "cmd-buffer.h"
#include "config.h"
#include "constants.h"
#include "feed.h"
#include "util.h"

#define NOMINMAX
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <windowsx.h>

#include <d3d11.h>
#include <d3dcompiler.h>

#pragma comment(lib, "d3d11")
#pragma comment(lib, "d3dcompiler")

//#define DEBUG_D3D11

// The main thing to remember about the D3D11 renderer is that, by default, D3D11's coordinate system starts (0, 0) at the upper-left of the screen.
// In fred, we implicitly define the coordinate (0, 0) to be the bottom-left.  This implies that we must adjust the viewports created in the D3D11
// renderer to start at the bottom-left.  This is why there's some odd conversion happening to the viewport TopLeftY values.
namespace Render
{
    namespace
    {
        struct D3D11WindowData
        {
            ID3D11Device*             d3d_device;
            ID3D11DeviceContext*      d3d_device_context;
            IDXGISwapChain*           swap_chain;
            ID3D11Texture2D*          render_target_fb;
            ID3D11RenderTargetView*   render_target_view;
        };

        struct D3D11Uniforms
        {
            Vec2f resolution;
            float time;
            float camera_coord_factor;
            Vec2f camera_scale;
            Vec2f camera_pos;
            Vec2f custom_vec2_value1;
            Vec2f custom_vec2_value2;
            Vec2f custom_vec2_value3;
        };

        struct D3DFramebuffer
        {
            ID3D11Texture2D*          rt_tex;
            ID3D11RenderTargetView*   rt_view;
            ID3D11ShaderResourceView* rt_srv;
            ScreenDimensions          size;
        };

        using BlurFramebuffers = D3DFramebuffer[2];

        struct D3DTextureData
        {
            ID3D11Texture2D*          tex;
            ID3D11ShaderResourceView* tex_view;
        };

        struct D3DVertexBufferData
        {
            ID3D11Buffer* vert_buf;
            ID3D11Buffer* idx_buf;
        };

        struct D3DEntity
        {
            D3DEntity* next;
            D3DEntity* prev;
            union
            {
                D3DTextureData tex;
                D3DVertexBufferData buffers;
            };
            uint64_t gen;
        };

        struct D3DEntityList
        {
            D3DEntity* first;
            D3DEntity* last;
            uint64_t count;
        };

        struct D3D11RenderData
        {
            ID3D11VertexShader*         vert_shaders[count_of<VertShader>];
            ID3D11InputLayout*          input_layouts[count_of<VertShader>];
            ID3D11PixelShader*          px_shaders[count_of<FragShader>];
            ID3D11Buffer*               cbuffer;
            ID3D11Buffer*               scratch_vert_buffer_64kb;
            ID3D11Buffer*               scratch_idx_buffer_64kb;
            ID3D11Buffer*               wnd_blur_vert_buffer_1kb;
            ID3D11Buffer*               wnd_blur_idx_buffer_1kb;
            ID3D11RasterizerState*      raster_state;
            ID3D11BlendState*           blend_modes[count_of<BlendingMode>];
            ID3D11BlendState*           no_blend;
            ID3D11DepthStencilState*    noop_stencil;
            ID3D11SamplerState*         linear_sampler;
            D3DFramebuffer              staging_fb;
            BlurFramebuffers            blur_fbs;
            Arena::Arena*               render_arena;
            D3DEntity*                  entity_free_lst;
            D3DEntityList               tex_list;
            D3DEntityList               flush_buffer_list;
            uint64_t                    entity_gen;
            D3D11Uniforms               uniforms;
            ScreenDimensions            screen_size;
            VertShader                  vert_shader;
            FragShader                  px_shader;
            int                         requested_frames;
        };

        D3D11WindowData impl_d3d_wnd_data;
        D3D11RenderData impl_d3d_rend_data;

        D3D11WindowData* d3d_wind_backend_data()
        {
            return &impl_d3d_wnd_data;
        }

        D3D11RenderData* d3d_render_backend_data()
        {
            return &impl_d3d_rend_data;
        }

        D3DEntity* push_render_entity(D3D11RenderData* data, D3DEntityList* lst)
        {
            D3DEntity* node = data->entity_free_lst;
            if (node != nullptr)
            {
                SLLStackPop(data->entity_free_lst);
                zero_bytes(node);
            }
            else
            {
                node = Arena::push_array<D3DEntity>(data->render_arena, 1);
            }
            node->gen = data->entity_gen++;
            DLLPushBack(lst->first, lst->last, node);
            ++lst->count;
            return node;
        }

        void release_render_entity(D3D11RenderData* data, D3DEntityList* lst, D3DEntity* e)
        {
            DLLRemove(lst->first, lst->last, e);
            --lst->count;
            SLLStackPush(data->entity_free_lst, e);
        }

        BasicTexture to_basic_texture(D3DEntity* e)
        {
            return BasicTexture{ reinterpret_cast<PrimitiveType<BasicTexture>>(e) };
        }

        D3DEntity* basic_texture_d3d(BasicTexture tex)
        {
            return reinterpret_cast<D3DEntity*>(tex);
        }

        void populate_render_target(D3D11WindowData* data)
        {
            data->swap_chain->GetBuffer(0, IID_PPV_ARGS(&data->render_target_fb));
            data->d3d_device->CreateRenderTargetView(data->render_target_fb, nullptr, &data->render_target_view);
        }

        void populate_staging_framebuffer(D3D11WindowData* wnd_data, D3D11RenderData* rend_data)
        {
            // Create the core staging framebuffer.
            D3D11_TEXTURE2D_DESC color_desc = {};
            wnd_data->render_target_fb->GetDesc(&color_desc);
            color_desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

            D3D11_RENDER_TARGET_VIEW_DESC rtv_desc = {};
            rtv_desc.Format        = color_desc.Format;
            rtv_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;

            D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
            srv_desc.Format                    = color_desc.Format;
            srv_desc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
            srv_desc.Texture2D.MipLevels       = UINT(-1);

            wnd_data->d3d_device->CreateTexture2D(&color_desc, nullptr, &rend_data->staging_fb.rt_tex);
            wnd_data->d3d_device->CreateRenderTargetView(rend_data->staging_fb.rt_tex, &rtv_desc, &rend_data->staging_fb.rt_view);
            wnd_data->d3d_device->CreateShaderResourceView(rend_data->staging_fb.rt_tex, &srv_desc, &rend_data->staging_fb.rt_srv);
            rend_data->staging_fb.size = ScreenDimensions{ Width(color_desc.Width), Height(color_desc.Height) };
        }

        void populate_blur_framebuffers(D3D11WindowData* wnd_data, D3D11RenderData* rend_data)
        {
            D3D11_TEXTURE2D_DESC color_desc = {};
            wnd_data->render_target_fb->GetDesc(&color_desc);
            // The blur framebuffers only need to be half the size of the primary framebuffer.
            color_desc.Width  /= 2;
            color_desc.Height /= 2;
            color_desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

            D3D11_RENDER_TARGET_VIEW_DESC rtv_desc = {};
            rtv_desc.Format        = color_desc.Format;
            rtv_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;

            D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
            srv_desc.Format                    = color_desc.Format;
            srv_desc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
            srv_desc.Texture2D.MipLevels       = 1;

            for EachIndex(i, std::size(rend_data->blur_fbs))
            {
                wnd_data->d3d_device->CreateTexture2D(&color_desc, nullptr, &rend_data->blur_fbs[i].rt_tex);
                wnd_data->d3d_device->CreateRenderTargetView(rend_data->blur_fbs[i].rt_tex, &rtv_desc, &rend_data->blur_fbs[i].rt_view);
                wnd_data->d3d_device->CreateShaderResourceView(rend_data->blur_fbs[i].rt_tex, &srv_desc, &rend_data->blur_fbs[i].rt_srv);
                rend_data->blur_fbs[i].size = ScreenDimensions{ Width(color_desc.Width), Height(color_desc.Height) };
            }
        }

        bool create_device_d3d(HWND wnd, D3D11WindowData* data, D3D11RenderData* rend_data)
        {
            // Setup swap chain
            DXGI_SWAP_CHAIN_DESC sd = {};
            sd.BufferCount                        = 2;
            sd.BufferDesc.Width                   = 0;
            sd.BufferDesc.Height                  = 0;
            sd.BufferDesc.Format                  = DXGI_FORMAT_R8G8B8A8_UNORM;
            sd.BufferDesc.RefreshRate.Numerator   = 60;
            sd.BufferDesc.RefreshRate.Denominator = 1;
            sd.Flags                              = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
            sd.BufferUsage                        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            sd.OutputWindow                       = wnd;
            sd.SampleDesc.Count                   = 1;
            sd.SampleDesc.Quality                 = 0;
            sd.Windowed                           = TRUE;
            sd.SwapEffect                         = DXGI_SWAP_EFFECT_DISCARD;

            UINT create_device_flags = 0;
#ifdef DEBUG_D3D11
            create_device_flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif // DEBUG_D3D11
            D3D_FEATURE_LEVEL feature_level;
            const D3D_FEATURE_LEVEL feature_levels[2] =
            {
                D3D_FEATURE_LEVEL_11_0,
                D3D_FEATURE_LEVEL_10_0,
            };
            HRESULT res = D3D11CreateDeviceAndSwapChain(
                                nullptr,                        // _In_opt_ IDXGIAdapter* pAdapter
                                D3D_DRIVER_TYPE_HARDWARE,       // D3D_DRIVER_TYPE DriverType
                                nullptr,                        // HMODULE Software
                                create_device_flags,            // UINT Flags
                                feature_levels,                 // _In_reads_opt_( FeatureLevels ) CONST D3D_FEATURE_LEVEL* pFeatureLevels
                                2,                              // UINT FeatureLevels
                                D3D11_SDK_VERSION,              // UINT SDKVersion
                                &sd,                            // _In_opt_ CONST DXGI_SWAP_CHAIN_DESC* pSwapChainDesc
                                &data->swap_chain,              // _COM_Outptr_opt_ IDXGISwapChain** ppSwapChain
                                &data->d3d_device,              // _COM_Outptr_opt_ ID3D11Device** ppDevice
                                &feature_level,                 // _Out_opt_ D3D_FEATURE_LEVEL* pFeatureLevel
                                &data->d3d_device_context);     // _COM_Outptr_opt_ ID3D11DeviceContext** ppImmediateContext
            // Try high-performance WARP software driver if hardware is not available.
            if (res == DXGI_ERROR_UNSUPPORTED)
            {
                res = D3D11CreateDeviceAndSwapChain(
                                nullptr,
                                D3D_DRIVER_TYPE_WARP,
                                nullptr,
                                create_device_flags,
                                feature_levels,
                                2,
                                D3D11_SDK_VERSION,
                                &sd,
                                &data->swap_chain,
                                &data->d3d_device,
                                &feature_level,
                                &data->d3d_device_context);
            }

            if (res != S_OK)
                return false;
            populate_render_target(data);
            populate_staging_framebuffer(data, rend_data);
            populate_blur_framebuffers(data, rend_data);
            return true;
        }

        ENABLE_UNHANDLED_CASE_WARNING();
        constexpr Assets::AssetID builtin_vert_shader_asset(VertShader shader)
        {
            switch (shader)
            {
            case VertShader::CameraTransform:
                return Assets::AssetID::VertTransformHLSL;
            case VertShader::NoTransform:
                return Assets::AssetID::VertNoTransformHLSL;
            case VertShader::OneOneTransform:
                return Assets::AssetID::VertOneOneTransformHLSL;
            case VertShader::Count:
                break;
            }
            return Assets::AssetID::Invalid;
        }

        constexpr Assets::AssetID builtin_frag_shader_asset(FragShader shader)
        {
            switch (shader)
            {
            case FragShader::Image:
                return Assets::AssetID::FragImageHLSL;
            case FragShader::TextSubpixel:
                return Assets::AssetID::FragTextSubpixelHLSL;
            case FragShader::Text:
                return Assets::AssetID::FragTextHLSL;
            case FragShader::BlurHorizVert:
                return Assets::AssetID::FragBlurHorizVertHLSL;
            case FragShader::Count:
                break;
            }
            return Assets::AssetID::Invalid;
        }
        DISABLE_UNHANDLED_CASE_WARNING();

        // Borrowed from RADDBG.
        UINT size_for_buffer(uint64_t requested_size)
        {
            UINT size = UINT(requested_size);
            size += MB(1) - 1;
            size -= size%MB(1);
            return size;
        }

        struct D3DGenBufferInput
        {
            D3D11WindowData* wnd_data;
            D3D11RenderData* rend_data;
            CmdBuffer::VertexBuffer vert_buf;
            CmdBuffer::IndexBuffer idx_buf;
        };

        D3DVertexBufferData gen_cmd_buffer_pair(D3DGenBufferInput in)
        {
            D3DVertexBufferData result = {
                .vert_buf = in.rend_data->scratch_vert_buffer_64kb,
                .idx_buf = in.rend_data->scratch_idx_buffer_64kb,
            };

            bool need_large = (in.vert_buf.count * sizeof(CmdBuffer::DrawVertex)) > KB(64)
                                or (in.idx_buf.count * sizeof(CmdBuffer::Index)) > KB(64);

            if (need_large)
            {
                D3DEntity* buffs = push_render_entity(in.rend_data, &in.rend_data->flush_buffer_list);
                // Vertex buffer.
                {
                    D3D11_BUFFER_DESC desc = {};
                    desc.Usage = D3D11_USAGE_DYNAMIC;
                    desc.ByteWidth = size_for_buffer(in.vert_buf.count * sizeof(CmdBuffer::DrawVertex));
                    desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
                    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
                    in.wnd_data->d3d_device->CreateBuffer(&desc, nullptr, &buffs->buffers.vert_buf);
                }
                // Index buffer.
                {
                    D3D11_BUFFER_DESC desc = {};
                    desc.Usage = D3D11_USAGE_DYNAMIC;
                    desc.ByteWidth = size_for_buffer(in.idx_buf.count * sizeof(CmdBuffer::Index));
                    desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
                    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
                    in.wnd_data->d3d_device->CreateBuffer(&desc, nullptr, &buffs->buffers.idx_buf);
                }
                result = buffs->buffers;
            }

            // Populate data.
            D3D11_MAPPED_SUBRESOURCE vtx_res = {};
            D3D11_MAPPED_SUBRESOURCE idx_res = {};
            in.wnd_data->d3d_device_context->Map(result.vert_buf, 0, D3D11_MAP_WRITE_DISCARD, 0, &vtx_res);
            in.wnd_data->d3d_device_context->Map(result.idx_buf, 0, D3D11_MAP_WRITE_DISCARD, 0, &idx_res);
            memcpy(vtx_res.pData, in.vert_buf.buf, in.vert_buf.count * sizeof(CmdBuffer::DrawVertex));
            memcpy(idx_res.pData, in.idx_buf.buf, in.idx_buf.count * sizeof(CmdBuffer::Index));
            in.wnd_data->d3d_device_context->Unmap(result.vert_buf, 0);
            in.wnd_data->d3d_device_context->Unmap(result.idx_buf, 0);

            return result;
        }

        void swap_shader(D3D11WindowData* wnd_data, D3D11RenderData* rend_data, VertShader vert, FragShader px)
        {
            rend_data->vert_shader = vert;
            rend_data->px_shader = px;
            ID3D11DeviceContext* ctx = wnd_data->d3d_device_context;
            // Setup the shader uniforms.
            {
                D3D11_MAPPED_SUBRESOURCE res;
                if (ctx->Map(rend_data->cbuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &res) == S_OK)
                {
                    D3D11Uniforms* cbuffer = static_cast<D3D11Uniforms*>(res.pData);
                    memcpy(cbuffer, &rend_data->uniforms, sizeof(D3D11Uniforms));
                    ctx->Unmap(rend_data->cbuffer, 0);
                }
            }
            // Setup input layout.
            ctx->IASetInputLayout(rend_data->input_layouts[rep(vert)]);

            // Setup shaders.
            ctx->VSSetShader(rend_data->vert_shaders[rep(vert)], nullptr, 0);
            ctx->VSSetConstantBuffers(0, 1, &rend_data->cbuffer);
            ctx->PSSetShader(rend_data->px_shaders[rep(px)], nullptr, 0);
            ctx->PSSetConstantBuffers(0, 1, &rend_data->cbuffer);
            ctx->PSSetSamplers(0, 1, &rend_data->linear_sampler);
            //ctx->GSSetShader(nullptr, nullptr, 0);
            //ctx->HSSetShader(nullptr, nullptr, 0);
            //ctx->DSSetShader(nullptr, nullptr, 0);
            //ctx->CSSetShader(nullptr, nullptr, 0);
        }

        // These render functions are highly specialized just for applying our ad-hoc window blur.
        struct WindowQuadInput
        {
            CmdBuffer::DrawVertex* off_vert;
            CmdBuffer::Index* off_idx;
            CmdBuffer::Index start_idx;
            Vec2f a;
            Vec2f c;
            Vec4f color;
            Vec2f uv_a;
            Vec2f uv_c;
            Vec2f vert_flags;
        };

        // 0 - 1  |  a - b
        // | \ |  |  | \ |
        // 3 - 2  |  d - c
        void window_quad(const WindowQuadInput& in)
        {
            Vec2f b{ in.c.x, in.a.y };
            Vec2f d{ in.a.x, in.c.y };
            Vec2f uv_b{ in.uv_c.x, in.uv_a.y };
            Vec2f uv_d{ in.uv_a.x, in.uv_c.y };

            in.off_vert[0] = { .pos = in.a, .color = in.color, .uv = in.uv_a, .cust1 = in.vert_flags.x, .cust2 = in.vert_flags.y };
            in.off_vert[1] = { .pos = b,    .color = in.color, .uv = uv_b,    .cust1 = in.vert_flags.x, .cust2 = in.vert_flags.y };
            in.off_vert[2] = { .pos = in.c, .color = in.color, .uv = in.uv_c, .cust1 = in.vert_flags.x, .cust2 = in.vert_flags.y };
            in.off_vert[3] = { .pos = d,    .color = in.color, .uv = uv_d,    .cust1 = in.vert_flags.x, .cust2 = in.vert_flags.y };

            CmdBuffer::Index idx = in.start_idx;
            in.off_idx[0] = idx;
            in.off_idx[1] = extend(idx, 1);
            in.off_idx[2] = extend(idx, 2);

            in.off_idx[3] = idx;
            in.off_idx[4] = extend(idx, 2);
            in.off_idx[5] = extend(idx, 3);
        }

        void populate_buffers_for_window_blur(D3D11WindowData* wnd_data,
                                                D3D11RenderData* rend_data,
                                                ScreenDimensions screen)
        {
            // We need a total of 3 quads for all 3 stages.
            constexpr int quad_count = 3;
            // Each quad is 4 verts.
            constexpr int vert_count = quad_count * 4;
            // Each quad requires 6 indices.
            constexpr int idx_count = quad_count * 6;

            CmdBuffer::DrawVertex verts[vert_count];
            CmdBuffer::Index idxs[idx_count];

            WindowQuadInput quad_in = {};
            quad_in.c.x = rep(rend_data->blur_fbs[0].size.width) + 0.f;
            quad_in.c.y = rep(rend_data->blur_fbs[0].size.height) + 0.f;
            quad_in.color = hex_to_vec4f(0xFFFFFFFF);
            quad_in.uv_a = { 0.f, 1.f };
            quad_in.uv_c = { 1.f, 0.f };

            // Create quads for #1.
            quad_in.off_vert = verts;
            quad_in.off_idx = idxs;
            quad_in.start_idx = CmdBuffer::Index{};
            quad_in.vert_flags.x = 1.f; // Blur vert.
            window_quad(quad_in);

            // Create quads for #2.
            quad_in.off_vert += 4;
            quad_in.off_idx += 6;
            quad_in.start_idx = extend(quad_in.start_idx, 4);
            quad_in.vert_flags.x = 0.f; // Blur horiz.
            window_quad(quad_in);

            // Create quads for #3.
            quad_in.c.x = rep(screen.width) + 0.f;
            quad_in.c.y = rep(screen.height) + 0.f;
            quad_in.off_vert += 4;
            quad_in.off_idx += 6;
            quad_in.start_idx = extend(quad_in.start_idx, 4);
            window_quad(quad_in);

            // Now map it all to our scratch buffers.
            static_assert(sizeof(verts) < KB(1) and sizeof(idxs) < KB(1));
            D3D11_MAPPED_SUBRESOURCE vtx_res = {};
            D3D11_MAPPED_SUBRESOURCE idx_res = {};
            wnd_data->d3d_device_context->Map(rend_data->wnd_blur_vert_buffer_1kb, 0, D3D11_MAP_WRITE_DISCARD, 0, &vtx_res);
            wnd_data->d3d_device_context->Map(rend_data->wnd_blur_idx_buffer_1kb, 0, D3D11_MAP_WRITE_DISCARD, 0, &idx_res);
            memcpy(vtx_res.pData, verts, vert_count * sizeof(CmdBuffer::DrawVertex));
            memcpy(idx_res.pData, idxs, idx_count * sizeof(CmdBuffer::Index));
            wnd_data->d3d_device_context->Unmap(rend_data->wnd_blur_vert_buffer_1kb, 0);
            wnd_data->d3d_device_context->Unmap(rend_data->wnd_blur_idx_buffer_1kb, 0);
        }

        struct WindowBlurInput
        {
            D3D11WindowData* wnd_data;
            D3D11RenderData* rend_data;
            ScreenDimensions screen;
            CmdBuffer::ClipRect window_clip;
        };

        void apply_standard_window_blur(const WindowBlurInput& in)
        {
            // The strategy is this:
            // 1. Render the current framebuffer to our blur buffer A (vert blur).
            // 2. Render framebuffer A -> B (horiz blur).
            // 3. Render B -> primary frame buffer.
            // Note: Steps 1-3 require premultiplied alpha blending mode.
            populate_buffers_for_window_blur(in.wnd_data, in.rend_data, in.screen);

            ID3D11DeviceContext* ctx = in.wnd_data->d3d_device_context;
            // Vert + index buffers.
            UINT stride = sizeof(CmdBuffer::DrawVertex);
            UINT offset = 0;
            ctx->IASetVertexBuffers(0, 1, &in.rend_data->wnd_blur_vert_buffer_1kb, &stride, &offset);
            ctx->IASetIndexBuffer(in.rend_data->wnd_blur_idx_buffer_1kb, DXGI_FORMAT_R32_UINT, 0);
            ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

            // Setup #1.
            ctx->OMSetRenderTargets(1, &in.rend_data->blur_fbs[0].rt_view, nullptr);

            // Setup viewport.
            D3D11_VIEWPORT vp = {};
            vp.Width = static_cast<float>(rep(in.rend_data->blur_fbs[0].size.width));
            vp.Height = static_cast<float>(rep(in.rend_data->blur_fbs[0].size.height));
            vp.MinDepth = 0.f;
            vp.MaxDepth = 1.f;
            vp.TopLeftX = 0;
            vp.TopLeftY = 0;
            ctx->RSSetViewports(1, &vp);

            // Setup scissor.
            D3D11_RECT r = {
                .left = 0,
                .top = 0,
                .right = rep(in.rend_data->blur_fbs[0].size.width),
                .bottom = rep(in.rend_data->blur_fbs[0].size.height),
            };
            ctx->RSSetScissorRects(1, &r);

            // Set shaders.
            // Update uniform values.
            in.rend_data->uniforms.custom_vec2_value1.x = .03f; // GLOW_FALLOFF.
            in.rend_data->uniforms.custom_vec2_value1.y = 4.f; // TAPS.
            Vec2f old_resolution = in.rend_data->uniforms.resolution;
            in.rend_data->uniforms.custom_vec2_value2 = old_resolution; // original_resolution.
            in.rend_data->uniforms.resolution = Vec2f(vp.Width, vp.Height);
            swap_shader(in.wnd_data, in.rend_data, VertShader::OneOneTransform, FragShader::BlurHorizVert);

            // Setup blend.
            constexpr float blend_fact[4] = {};
            ctx->OMSetBlendState(in.rend_data->no_blend, blend_fact, 0xffffffff);
            ctx->OMSetDepthStencilState(in.rend_data->noop_stencil, 0);

            // Bind the main framebuffer as our input texture.
            ctx->PSSetShaderResources(0, 1, &in.rend_data->staging_fb.rt_srv);
            // Draw #1.
            ctx->DrawIndexed(6, 0, 0);

            // Setup #2.
            ctx->OMSetRenderTargets(1, &in.rend_data->blur_fbs[1].rt_view, nullptr);
            ctx->OMSetBlendState(in.rend_data->no_blend, blend_fact, 0xffffffff);
            ctx->PSSetShaderResources(0, 1, &in.rend_data->blur_fbs[0].rt_srv);
            // We can reuse everything else above.
            // Draw #2.
            ctx->DrawIndexed(6, 6, 0);

            // Setup #3.
            // Finally, blit back to main framebuffer.
            ctx->OMSetRenderTargets(1, &in.rend_data->staging_fb.rt_view, nullptr);

            // Setup viewport.
            vp = {};
            vp.Width = static_cast<float>(rep(in.screen.width));
            vp.Height = static_cast<float>(rep(in.screen.height));
            vp.MinDepth = 0.f;
            vp.MaxDepth = 1.f;
            vp.TopLeftX = 0;
            vp.TopLeftY = 0;
            ctx->RSSetViewports(1, &vp);

            // Setup scissor.
            r = {
                .left = rep(in.window_clip.offset_x),
                .top = rep(in.screen.height) - (rep(in.window_clip.height) + rep(in.window_clip.offset_y)),
                .right = rep(in.window_clip.offset_x) + rep(in.window_clip.width),
                .bottom = rep(in.screen.height) - rep(in.window_clip.offset_y),
            };
            ctx->RSSetScissorRects(1, &r);

            // Set shaders.
            // Restore old resolution uniform.
            in.rend_data->uniforms.resolution = old_resolution;
            swap_shader(in.wnd_data, in.rend_data, VertShader::OneOneTransform, FragShader::Image);

            // Setup blend.
            ctx->OMSetBlendState(in.rend_data->no_blend, blend_fact, 0xffffffff);

            ctx->PSSetShaderResources(0, 1, &in.rend_data->blur_fbs[1].rt_srv);
            // Draw #3.
            ctx->DrawIndexed(6, 12, 0);
        }
    } // namespace [anon]

    struct FrameRenderer { };

    // OS interaction.
    bool os_init_renderer_window(OS::OSWindow wind)
    {
        HWND os_wnd = reinterpret_cast<HWND>(wind);
        if (not create_device_d3d(os_wnd, d3d_wind_backend_data(), d3d_render_backend_data()))
            return false;
        return true;
    }

    void os_select_renderer(OS::OSWindow)
    {
        // No-op in DX11.
    }

    void os_destroy_renderer_window(OS::OSWindow)
    {
        D3D11WindowData* data = d3d_wind_backend_data();
        data->d3d_device->Release();
        data->d3d_device_context->Release();
        data->render_target_view->Release();
        data->swap_chain->Release();
        data->d3d_device_context->Release();
    }

    void os_swap_buffers(OS::OSWindow)
    {
        D3D11WindowData* data = d3d_wind_backend_data();
        data->swap_chain->Present(1, 0); // With vsync.
        data->d3d_device_context->ClearState();
    }

    // Creation.
    FrameRenderer* make_platform_renderer(Arena::Arena* arena)
    {
        return Arena::push_array<FrameRenderer>(arena, 1);
    }

    // Interaction.
    void update_resolution(FrameRenderer*, Vec2f new_res)
    {
        D3D11RenderData* rend_data = d3d_render_backend_data();
        rend_data->uniforms.resolution = new_res;
    }

    void update_time(FrameRenderer*, float app_time, float /*dt*/)
    {
        D3D11RenderData* rend_data = d3d_render_backend_data();
        rend_data->uniforms.time = app_time;
    }

    // Initialize global data for all renderer instances.
    bool init(const ScreenDimensions& screen)
    {
        PROF_SCOPE();
        D3D11WindowData* wnd_data = d3d_wind_backend_data();
        D3D11RenderData* rend_data = d3d_render_backend_data();
        auto scratch = Arena::scratch_begin(Arena::no_conflicts);
        bool good = true;
        // Setup the default uniform values.
        {
            rend_data->uniforms.camera_coord_factor = Constants::shader_scale_factor;
            rend_data->uniforms.camera_scale = 3.f;
        }
        // Compile shaders.
        Assets::AssetBuffer ass_buf{};
        // Vertex shaders.
        {
            constexpr D3D11_INPUT_ELEMENT_DESC layout_desc[] =
            {
                { "POS",   0, DXGI_FORMAT_R32G32_FLOAT,       0, 0,                            D3D11_INPUT_PER_VERTEX_DATA, 0 },
                { "COL",   0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
                { "TEX",   0, DXGI_FORMAT_R32G32_FLOAT,       0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
                { "CUST",  0, DXGI_FORMAT_R32_FLOAT,          0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
                { "CUST",  1, DXGI_FORMAT_R32_FLOAT,          0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            };

            for EachIndex(i, count_of<VertShader>)
            {
                if (not good)
                    break;
                auto tmp = Arena::temp_begin(scratch.arena);
                auto id = builtin_vert_shader_asset(VertShader(i));
                ass_buf.len = rep(Assets::asset_length(id));
                ass_buf.buf = Arena::push_array_no_zero<uint8_t>(tmp.arena, ass_buf.len);
                if (not Assets::populate_asset(ass_buf, id))
                {
                    Assets::AssetDescription desc = Assets::describe(id);
                    String8 msg = str8_fmt(scratch.arena, "Failed to load shader asset: '%S'", desc.proxy_file);
                    fprintf(stderr, "%.*s\n", static_cast<int>(msg.size), msg.str);
                    good = false;
                }

                ID3DBlob* vert_src_blob = nullptr;
                ID3DBlob* vert_errors_blob = nullptr;
                if (good)
                {
                    if (FAILED(D3DCompile(ass_buf.buf,
                                            ass_buf.len,
                                            nullptr,
                                            nullptr,
                                            nullptr,
                                            "main",
                                            "vs_4_0",
                                            0,
                                            0,
                                            &vert_src_blob,
                                            &vert_errors_blob)))
                    {
                        String8 err_txt = str8(static_cast<char*>(vert_errors_blob->GetBufferPointer()), vert_errors_blob->GetBufferSize());
                        Assets::AssetDescription desc = Assets::describe(id);
                        String8 msg = str8_fmt(scratch.arena, "Failed to compile shader file '%S': %S", desc.proxy_file, err_txt);
                        fprintf(stderr, "%.*s\n", static_cast<int>(msg.size), msg.str);
                        good = false;
                    }
                }

                if (good)
                {
                    if (wnd_data->d3d_device->CreateVertexShader(
                                                vert_src_blob->GetBufferPointer(),
                                                vert_src_blob->GetBufferSize(),
                                                nullptr,
                                                &rend_data->vert_shaders[i]) != S_OK)
                    {
                        Assets::AssetDescription desc = Assets::describe(id);
                        String8 msg = str8_fmt(scratch.arena, "CreateVertexShader failed for '%S'", desc.proxy_file);
                        fprintf(stderr, "%.*s\n", static_cast<int>(msg.size), msg.str);
                        vert_src_blob->Release();
                        good = false;
                    }
                }

                if (good)
                {
                    HRESULT error = wnd_data->d3d_device->CreateInputLayout(
                                                layout_desc,
                                                std::size(layout_desc),
                                                vert_src_blob->GetBufferPointer(),
                                                vert_src_blob->GetBufferSize(),
                                                &rend_data->input_layouts[i]);
                    if (error != S_OK)
                    {
                        Assets::AssetDescription desc = Assets::describe(id);
                        String8 msg = str8_fmt(scratch.arena, "CreateInputLayout failed for '%S'", desc.proxy_file);
                        fprintf(stderr, "%.*s\n", static_cast<int>(msg.size), msg.str);
                        vert_src_blob->Release();
                        good = false;
                    }
                    vert_src_blob->Release();
                }
                Arena::temp_end(tmp);
            }
        }

        // Pixel shaders.
        if (good)
        {
            for EachIndex(i, count_of<FragShader>)
            {
                if (not good)
                    break;
                auto tmp = Arena::temp_begin(scratch.arena);
                auto id = builtin_frag_shader_asset(FragShader(i));
                ass_buf.len = rep(Assets::asset_length(id));
                ass_buf.buf = Arena::push_array_no_zero<uint8_t>(tmp.arena, ass_buf.len);
                if (not Assets::populate_asset(ass_buf, id))
                {
                    Assets::AssetDescription desc = Assets::describe(id);
                    String8 msg = str8_fmt(scratch.arena, "Failed to load shader asset: '%S'", desc.proxy_file);
                    fprintf(stderr, "%.*s\n", static_cast<int>(msg.size), msg.str);
                    good = false;
                }

                ID3DBlob* px_src_blob = nullptr;
                ID3DBlob* px_errors_blob = nullptr;
                if (good)
                {
                    if (FAILED(D3DCompile(ass_buf.buf,
                                            ass_buf.len,
                                            nullptr,
                                            nullptr,
                                            nullptr,
                                            "main",
                                            "ps_4_0",
                                            0,
                                            0,
                                            &px_src_blob,
                                            &px_errors_blob)))
                    {
                        String8 err_txt = str8(static_cast<char*>(px_errors_blob->GetBufferPointer()), px_errors_blob->GetBufferSize());
                        Assets::AssetDescription desc = Assets::describe(id);
                        String8 msg = str8_fmt(scratch.arena, "Failed to compile shader file '%S': %S", desc.proxy_file, err_txt);
                        fprintf(stderr, "%.*s\n", static_cast<int>(msg.size), msg.str);
                        good = false;
                    }
                }

                if (good)
                {
                    if (wnd_data->d3d_device->CreatePixelShader(
                                                px_src_blob->GetBufferPointer(),
                                                px_src_blob->GetBufferSize(),
                                                nullptr,
                                                &rend_data->px_shaders[i]) != S_OK)
                    {
                        Assets::AssetDescription desc = Assets::describe(id);
                        String8 msg = str8_fmt(scratch.arena, "CreatePixelShader failed for '%S'", desc.proxy_file);
                        fprintf(stderr, "%.*s\n", static_cast<int>(msg.size), msg.str);
                        px_src_blob->Release();
                        good = false;
                    }
                    px_src_blob->Release();
                }
            }
        }

        // Setup uniform buffer.
        if (good)
        {
            D3D11_BUFFER_DESC desc = {};
            desc.ByteWidth = sizeof(D3D11Uniforms);
            desc.ByteWidth += 0xf;
            desc.ByteWidth -= desc.ByteWidth % 0x10;
            desc.Usage = D3D11_USAGE_DYNAMIC;
            // https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ns-d3d11-d3d11_buffer_desc#remarks
            // Since we specified this flag, the byte width must be a multiple of 16.
            desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            HRESULT error = wnd_data->d3d_device->CreateBuffer(&desc, nullptr, &rend_data->cbuffer);
            good = error == S_OK;
        }

        // Setup rasterizer state.
        if (good)
        {
            D3D11_RASTERIZER_DESC desc = {};
            desc.FillMode = D3D11_FILL_SOLID;
            desc.CullMode = D3D11_CULL_NONE;
            desc.ScissorEnable = true;
            HRESULT error = wnd_data->d3d_device->CreateRasterizerState(&desc, &rend_data->raster_state);
            good = error == S_OK;
        }

        // Setup blend modes.
        if (good)
        {
            for EachIndex(i, count_of<BlendingMode>)
            {
                D3D11_BLEND_DESC desc = {};
                desc.AlphaToCoverageEnable = false;
                desc.RenderTarget[0].BlendEnable           = true;
                desc.RenderTarget[0].BlendOp               = D3D11_BLEND_OP_ADD;
                desc.RenderTarget[0].BlendOpAlpha          = D3D11_BLEND_OP_ADD;
                desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

                ENABLE_UNHANDLED_CASE_WARNING();
                switch (BlendingMode(i))
                {
                case BlendingMode::PremultipliedAlpha:
                    desc.RenderTarget[0].SrcBlend       = D3D11_BLEND_ONE;
                    desc.RenderTarget[0].SrcBlendAlpha  = D3D11_BLEND_ONE;
                    desc.RenderTarget[0].DestBlend      = D3D11_BLEND_INV_SRC_ALPHA;
                    desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
                    break;
                case BlendingMode::SrcAlpha:
                    desc.RenderTarget[0].SrcBlend       = D3D11_BLEND_SRC_ALPHA;
                    desc.RenderTarget[0].SrcBlendAlpha  = D3D11_BLEND_SRC_ALPHA;
                    desc.RenderTarget[0].DestBlend      = D3D11_BLEND_INV_SRC_ALPHA;
                    desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
                    break;
                case BlendingMode::DualSourceBlend:
                    desc.RenderTarget[0].SrcBlend       = D3D11_BLEND_ONE;
                    desc.RenderTarget[0].SrcBlendAlpha  = D3D11_BLEND_ONE;
                    desc.RenderTarget[0].DestBlend      = D3D11_BLEND_INV_SRC1_COLOR;
                    desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
                    break;
                case BlendingMode::Default:
                    desc.RenderTarget[0].SrcBlend       = D3D11_BLEND_SRC_ALPHA;
                    desc.RenderTarget[0].SrcBlendAlpha  = D3D11_BLEND_ONE;
                    desc.RenderTarget[0].DestBlend      = D3D11_BLEND_INV_SRC_ALPHA;
                    desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
                    break;
                case BlendingMode::Count:
                    // Fatal.
                    break;
                }
                DISABLE_UNHANDLED_CASE_WARNING();
                HRESULT error = wnd_data->d3d_device->CreateBlendState(&desc, &rend_data->blend_modes[i]);
                good = error == S_OK;
            }

            if (good)
            {
                D3D11_BLEND_DESC desc = {};
                desc.RenderTarget[0].BlendEnable = false;
                desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
                HRESULT error = wnd_data->d3d_device->CreateBlendState(&desc, &rend_data->no_blend);
                good = error == S_OK;
            }
        }

        // Create depth-stencil.
        if (good)
        {
            D3D11_DEPTH_STENCIL_DESC desc = {};
            desc.DepthEnable = false;
            desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
            desc.DepthFunc = D3D11_COMPARISON_LESS;
            desc.StencilEnable = false;
            HRESULT error = wnd_data->d3d_device->CreateDepthStencilState(&desc, &rend_data->noop_stencil);
            good = error == S_OK;
        }

        // Create samplers.
        if (good)
        {
            D3D11_SAMPLER_DESC desc = {};
            desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
            desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
            desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
            desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
            desc.MipLODBias = 0.f;
            desc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
            desc.MinLOD = 0.f;
            desc.MaxLOD = 0.f;
            HRESULT error = wnd_data->d3d_device->CreateSamplerState(&desc, &rend_data->linear_sampler);
            good = error == S_OK;
        }

        // Setup scratch buffers.
        if (good)
        {
            // Vertex buffer.
            {
                D3D11_BUFFER_DESC desc = {};
                desc.Usage = D3D11_USAGE_DYNAMIC;
                desc.ByteWidth = KB(64);
                desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
                desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
                HRESULT error = wnd_data->d3d_device->CreateBuffer(&desc, nullptr, &rend_data->scratch_vert_buffer_64kb);
                good = error == S_OK;
            }
            // Index buffer.
            if (good)
            {
                D3D11_BUFFER_DESC desc = {};
                desc.Usage = D3D11_USAGE_DYNAMIC;
                desc.ByteWidth = KB(64);
                desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
                desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
                HRESULT error = wnd_data->d3d_device->CreateBuffer(&desc, nullptr, &rend_data->scratch_idx_buffer_64kb);
                good = error == S_OK;
            }
            // Vertex buffer (window blur).
            if (good)
            {
                D3D11_BUFFER_DESC desc = {};
                desc.Usage = D3D11_USAGE_DYNAMIC;
                desc.ByteWidth = KB(1);
                desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
                desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
                HRESULT error = wnd_data->d3d_device->CreateBuffer(&desc, nullptr, &rend_data->wnd_blur_vert_buffer_1kb);
                good = error == S_OK;
            }
            // Index buffer (window blur).
            if (good)
            {
                D3D11_BUFFER_DESC desc = {};
                desc.Usage = D3D11_USAGE_DYNAMIC;
                desc.ByteWidth = KB(1);
                desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
                desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
                HRESULT error = wnd_data->d3d_device->CreateBuffer(&desc, nullptr, &rend_data->wnd_blur_idx_buffer_1kb);
                good = error == S_OK;
            }
        }

        // Size our swap chain.
        if (good)
        {
            screen_resize(screen);
        }
        Arena::scratch_end(scratch);
        return good;
    }

    // Reloads all shaders for every renderer instance.
    void reload_shaders(Feed::MessageFeed* feed)
    {
        // TODO.
        feed->queue_info("Shaders reloaded.");
    }

    // Helper functions.
    void draw_cmd_list(FrameRenderer*, CmdBuffer::CmdList* cmd_lst)
    {
        PROF_SCOPE();

        D3D11WindowData* wnd_data = d3d_wind_backend_data();
        D3D11RenderData* rend_data = d3d_render_backend_data();
        ID3D11DeviceContext* ctx = wnd_data->d3d_device_context;

        // Setup primary viewport.
        D3D11_VIEWPORT vp = {};
        vp.Width    = static_cast<float>(rep(rend_data->screen_size.width));
        vp.Height   = static_cast<float>(rep(rend_data->screen_size.height));
        vp.MinDepth = 0.f;
        vp.MaxDepth = 1.f;
        vp.TopLeftX = 0;
        vp.TopLeftY = 0;
        ctx->RSSetViewports(1, &vp);

        // Clear the background.
        const Vec4f& bg = Config::diff_colors().background;
        ctx->OMSetRenderTargets(1, &rend_data->staging_fb.rt_view, nullptr);
        ctx->ClearRenderTargetView(rend_data->staging_fb.rt_view, bg.xyza);
        // Prepare raster.
        ctx->RSSetState(rend_data->raster_state);

        for EachIndex(i, count_of<CmdBuffer::DrawListLayer>)
        {
            CmdBuffer::DrawListCollection* lst_c = &cmd_lst->draw_list[i];
            for EachNode(lst_n, lst_c->first)
            {
                CmdBuffer::DrawList* lst = lst_n->lst;

                // Obtain vert and index buffers.
                D3DGenBufferInput buf_in = {
                    .wnd_data  = wnd_data,
                    .rend_data = rend_data,
                    .vert_buf  = lst->vert_buf,
                    .idx_buf   = lst->idx_buf,
                };
                D3DVertexBufferData buffers = gen_cmd_buffer_pair(buf_in);
                // Vert and index buffers.
                UINT stride = sizeof(CmdBuffer::DrawVertex);
                UINT offset = 0;
                ctx->IASetVertexBuffers(0, 1, &buffers.vert_buf, &stride, &offset);
                ctx->IASetIndexBuffer(buffers.idx_buf, DXGI_FORMAT_R32_UINT, 0);
                ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

                // It's possible that the screen resolution changes in each draw.
                ScreenDimensions res = lst->screen;
                for EachNode(cmd_n, lst->cmd_buf.first)
                {
                    const CmdBuffer::DrawCmd* cmd = &cmd_n->cmd;
                    switch (cmd->sort)
                    {
                    case CmdBuffer::CmdSort::Standard:
                        {
                            const CmdBuffer::StandardData* std_data = &cmd->std_data;
                            // Setup viewport.
                            vp.Width    = static_cast<float>(rep(res.width));
                            vp.Height   = static_cast<float>(rep(res.height));
                            vp.MinDepth = 0.f;
                            vp.MaxDepth = 1.f;
                            vp.TopLeftX = static_cast<float>(rep(cmd->clip_rect.offset_x));
                            vp.TopLeftY = rep(rend_data->screen_size.height) - static_cast<float>(rep(cmd->clip_rect.offset_y)) - rep(res.height);
                            ctx->RSSetViewports(1, &vp);

                            // Setup scissor.
                            D3D11_RECT r = {
                                .left   = rep(cmd->clip_rect.offset_x),
                                .top    = rep(rend_data->screen_size.height) - (rep(cmd->clip_rect.height) + rep(cmd->clip_rect.offset_y)),
                                .right  = rep(cmd->clip_rect.offset_x) + rep(cmd->clip_rect.width),
                                .bottom = rep(rend_data->screen_size.height) - rep(cmd->clip_rect.offset_y),
                            };
                            ctx->RSSetScissorRects(1, &r);

                            // Set shaders.
                            swap_shader(wnd_data, rend_data, std_data->vert, std_data->frag);

                            // Setup blend.
                            constexpr float blend_fact[4] = {};
                            ctx->OMSetBlendState(rend_data->blend_modes[rep(std_data->blend)], blend_fact, 0xffffffff);
                            ctx->OMSetDepthStencilState(rend_data->noop_stencil, 0);

                            // Bind texture.
                            D3DEntity* tex = basic_texture_d3d(std_data->tex);
                            ctx->PSSetShaderResources(0, 1, &tex->tex.tex_view);
                            // Note: Our indices are absolute offsets into the vertex buffer so we don't need to add an offset for the base vertex value.
                            ctx->DrawIndexed(rep(std_data->idx_count), rep(std_data->idx_off), 0);
                        }
                        break;
                    case CmdBuffer::CmdSort::Blur:
                        {
                            WindowBlurInput in = {
                                .wnd_data    = wnd_data,
                                .rend_data   = rend_data,
                                .screen      = rend_data->screen_size,
                                .window_clip = cmd->clip_rect
                            };
                            apply_standard_window_blur(in);
                            // Restore the standard buffers.
                            stride = sizeof(CmdBuffer::DrawVertex);
                            offset = 0;
                            ctx->IASetVertexBuffers(0, 1, &buffers.vert_buf, &stride, &offset);
                            ctx->IASetIndexBuffer(buffers.idx_buf, DXGI_FORMAT_R32_UINT, 0);
                            ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                        }
                        break;
                    case CmdBuffer::CmdSort::CameraUpdate:
                        {
                            const Camera* camera                    = &cmd->camera_up;
                            rend_data->uniforms.camera_pos          = camera->pos;
                            rend_data->uniforms.camera_scale        = camera->scale;
                            rend_data->uniforms.camera_coord_factor = Constants::shader_scale_factor;
                        }
                        break;
                    case CmdBuffer::CmdSort::ResolutionUpdate:
                        {
                            rend_data->uniforms.resolution = { rep(cmd->res_up.width) + 0.f, rep(cmd->res_up.height) + 0.f };
                            res = cmd->res_up;
                        }
                        break;
                    }
                }
            }
        }
    }

    void apply_framebuffer(Render::FrameRenderer*, const ScreenDimensions& screen)
    {
        D3D11WindowData* wnd_data = d3d_wind_backend_data();
        D3D11RenderData* rend_data = d3d_render_backend_data();
        ID3D11DeviceContext* ctx = wnd_data->d3d_device_context;

        // Just write as a simple quad.
        CmdBuffer::DrawVertex verts[4];
        CmdBuffer::Index idxs[6];

        WindowQuadInput quad_in = {};
        quad_in.c.x = rep(screen.width) + 0.f;
        quad_in.c.y = rep(screen.height) + 0.f;
        quad_in.color = hex_to_vec4f(0xFFFFFFFF);
        quad_in.uv_a = { 0.f, 1.f };
        quad_in.uv_c = { 1.f, 0.f };

        quad_in.off_vert = verts;
        quad_in.off_idx = idxs;
        quad_in.start_idx = CmdBuffer::Index{};
        window_quad(quad_in);

        // Now map it all to our scratch buffers.
        D3D11_MAPPED_SUBRESOURCE vtx_res = {};
        D3D11_MAPPED_SUBRESOURCE idx_res = {};
        wnd_data->d3d_device_context->Map(rend_data->wnd_blur_vert_buffer_1kb, 0, D3D11_MAP_WRITE_DISCARD, 0, &vtx_res);
        wnd_data->d3d_device_context->Map(rend_data->wnd_blur_idx_buffer_1kb, 0, D3D11_MAP_WRITE_DISCARD, 0, &idx_res);
        memcpy(vtx_res.pData, verts, 4 * sizeof(CmdBuffer::DrawVertex));
        memcpy(idx_res.pData, idxs, 6 * sizeof(CmdBuffer::Index));
        wnd_data->d3d_device_context->Unmap(rend_data->wnd_blur_vert_buffer_1kb, 0);
        wnd_data->d3d_device_context->Unmap(rend_data->wnd_blur_idx_buffer_1kb, 0);

        // Set the primary render target.
        ctx->OMSetRenderTargets(1, &wnd_data->render_target_view, nullptr);

        // Vert + index buffers.
        UINT stride = sizeof(CmdBuffer::DrawVertex);
        UINT offset = 0;
        ctx->IASetVertexBuffers(0, 1, &rend_data->wnd_blur_vert_buffer_1kb, &stride, &offset);
        ctx->IASetIndexBuffer(rend_data->wnd_blur_idx_buffer_1kb, DXGI_FORMAT_R32_UINT, 0);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        // Setup viewport.
        D3D11_VIEWPORT vp = {};
        vp.Width = static_cast<float>(rep(screen.width));
        vp.Height = static_cast<float>(rep(screen.height));
        vp.MinDepth = 0.f;
        vp.MaxDepth = 1.f;
        vp.TopLeftX = 0;
        vp.TopLeftY = 0;
        ctx->RSSetViewports(1, &vp);

        // Setup scissor.
        D3D11_RECT r = {
            .left = 0,
            .top = 0,
            .right = rep(screen.width),
            .bottom = rep(screen.height),
        };
        ctx->RSSetScissorRects(1, &r);

        // Set shaders.
        swap_shader(wnd_data, rend_data, VertShader::OneOneTransform, FragShader::Image);

        // Setup blend.
        constexpr float blend_fact[4] = {};
        ctx->OMSetBlendState(rend_data->no_blend, blend_fact, 0xffffffff);
        ctx->OMSetDepthStencilState(rend_data->noop_stencil, 0);

        // Bind the staging framebuffer as our input texture.
        ctx->PSSetShaderResources(0, 1, &rend_data->staging_fb.rt_srv);
        ctx->DrawIndexed(6, 0, 0);
    }

    void window_end_frame(OS::OSWindow window)
    {
        D3D11RenderData* rend_data = d3d_render_backend_data();
        while (rend_data->flush_buffer_list.first != nullptr)
        {
            D3DEntity* n = rend_data->flush_buffer_list.first;
            n->buffers.vert_buf->Release();
            n->buffers.idx_buf->Release();
            release_render_entity(rend_data, &rend_data->flush_buffer_list, n);
        }
        rend_data->flush_buffer_list = {};
        OS::swap_buffers(window);
    }

    // Resize-related functionality.
    void screen_resize(const ScreenDimensions& screen)
    {
        D3D11WindowData* wnd_data = d3d_wind_backend_data();
        D3D11RenderData* rend_data = d3d_render_backend_data();
        // Destroy the old render target.
        wnd_data->render_target_view->Release();
        wnd_data->render_target_fb->Release();
        // Destory staging framebuffer.
        rend_data->staging_fb.rt_srv->Release();
        rend_data->staging_fb.rt_tex->Release();
        rend_data->staging_fb.rt_view->Release();
        // Destroy old blur framebuffers.
        for EachIndex(i, std::size(rend_data->blur_fbs))
        {
            rend_data->blur_fbs[i].rt_srv->Release();
            rend_data->blur_fbs[i].rt_tex->Release();
            rend_data->blur_fbs[i].rt_view->Release();
        }
        wnd_data->swap_chain->ResizeBuffers(0, rep(screen.width), rep(screen.height), DXGI_FORMAT_UNKNOWN, 0);
        rend_data->screen_size = screen;
        populate_render_target(wnd_data);
        populate_staging_framebuffer(wnd_data, rend_data);
        populate_blur_framebuffers(wnd_data, rend_data);
    }

    // Functions for creating basic textures and manipulating them.
    BasicTexture create_basic_texture(const ScreenDimensions& size)
    {
        D3D11WindowData* wnd_data = d3d_wind_backend_data();
        D3D11RenderData* rend_data = d3d_render_backend_data();

        // Allocate the return texture.
        D3DEntity* tex = push_render_entity(rend_data, &rend_data->tex_list);

        // Create texture
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = rep(size.width);
        desc.Height = rep(size.height);
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        desc.CPUAccessFlags = 0;
        wnd_data->d3d_device->CreateTexture2D(&desc, nullptr, &tex->tex.tex);

        // Create texture view
        D3D11_SHADER_RESOURCE_VIEW_DESC shad_res_desc = {};
        shad_res_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        shad_res_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        shad_res_desc.Texture2D.MipLevels = desc.MipLevels;
        shad_res_desc.Texture2D.MostDetailedMip = 0;
        wnd_data->d3d_device->CreateShaderResourceView(tex->tex.tex, &shad_res_desc, &tex->tex.tex_view);

        return to_basic_texture(tex);
    }

    void delete_basic_texture(BasicTexture tex)
    {
        D3D11RenderData* data = d3d_render_backend_data();
        D3DEntity* entity_tex = basic_texture_d3d(tex);
        entity_tex->tex.tex_view->Release();
        entity_tex->tex.tex->Release();
        release_render_entity(data, &data->tex_list, entity_tex);
    }

    void submit_basic_texture_data(BasicTexture tex, BasicTextureEntry entry)
    {
        D3D11WindowData* wnd_data = d3d_wind_backend_data();
        D3DEntity* entity_tex = basic_texture_d3d(tex);
        D3D11_BOX box = {
            .left   = UINT(rep(entry.offset_x)),
            .top    = UINT(rep(entry.offset_y)),
            .front  = 0,
            .right  = UINT(rep(entry.offset_x) + rep(entry.width)),
            .bottom = UINT(rep(entry.offset_y) + rep(entry.height)),
            .back   = 1
        };
        wnd_data->d3d_device_context->UpdateSubresource(
            entity_tex->tex.tex,
            0,
            &box,
            entry.buffer,
            rep(entry.width) * 4, // Pitch (width * RGBA).
            0);
    }

    // Functions for creating glyph cache textures, binding, and manipulating them.
    BasicTexture create_glyph_texture(const ScreenDimensions& dim)
    {
        D3D11WindowData* wnd_data = d3d_wind_backend_data();
        D3D11RenderData* rend_data = d3d_render_backend_data();

        // Allocate the return texture.
        D3DEntity* tex = push_render_entity(rend_data, &rend_data->tex_list);

        // Create texture
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = rep(dim.width);
        desc.Height = rep(dim.height);
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        desc.CPUAccessFlags = 0;
        HRESULT error = wnd_data->d3d_device->CreateTexture2D(&desc, nullptr, &tex->tex.tex);

        // Create texture view
        D3D11_SHADER_RESOURCE_VIEW_DESC shad_res_desc = {};
        shad_res_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        shad_res_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        shad_res_desc.Texture2D.MipLevels = desc.MipLevels;
        shad_res_desc.Texture2D.MostDetailedMip = 0;
        error = wnd_data->d3d_device->CreateShaderResourceView(tex->tex.tex, &shad_res_desc, &tex->tex.tex_view);

        return to_basic_texture(tex);
    }

    void submit_glyph_data(BasicTexture tex, GlyphEntry entry)
    {
        D3D11WindowData* wnd_data = d3d_wind_backend_data();
        D3DEntity* entity_tex = basic_texture_d3d(tex);
        D3D11_BOX box = {
            .left   = UINT(rep(entry.offset_x)),
            .top    = UINT(rep(entry.offset_y)),
            .front  = 0,
            .right  = UINT(rep(entry.offset_x) + rep(entry.width)),
            .bottom = UINT(rep(entry.offset_y) + rep(entry.height)),
            .back   = 1
        };
        wnd_data->d3d_device_context->UpdateSubresource(
            entity_tex->tex.tex,
            0,
            &box,
            entry.buffer,
            rep(entry.width) * 4, // Pitch.
            0);
    }

    // APIs for general frame requests.
    void request_frames()
    {
        D3D11RenderData* data = d3d_render_backend_data();
        data->requested_frames = 4;
    }

    int frames_remaining()
    {
        D3D11RenderData* data = d3d_render_backend_data();
        return data->requested_frames;
    }

    void consume_frame()
    {
        D3D11RenderData* data = d3d_render_backend_data();
        if (data->requested_frames > 0)
        {
            --data->requested_frames;
        }
    }

    // APIs for initialization/debugging.
    bool init_renderer_arenas()
    {
        D3D11RenderData* data = d3d_render_backend_data();
        data->render_arena = Arena::alloc(Arena::default_params);
        return true;
    }

    void display_renderer_version()
    {
        printf("D3D11\n");
    }
} // namespace Render