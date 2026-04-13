#include "renderer.h"

#include <cassert>

#include <algorithm>

#include "assets.h"
#include "cmd-buffer.h"
#include "config.h"
#include "constants.h"
#include "enum-utils.h"
#include "feed.h"
#include "gl-defs.h"
#include "list-helpers.h"
#include "macros.h"
#include "os.h"
#include "scope-guard.h"
#include "scoped-handle.h"
#include "util.h"
#include "vec.h"

//#define DEBUG_GL

namespace Render
{
    namespace Effects
    {
        void standard_window_blur(FrameRenderer* renderer, const ScreenDimensions& screen, const Render::RenderViewport& window_vp);
    } // namespace Effects

    namespace
    {
        // OpenGL init data.
        // Create function pointer typedefs.
#define DAT_GL(name, type) using PF ## name = type;
#include "gl.dat"
#undef DAT_GL
        struct GLBackendData
        {
#define DAT_GL(name, type) PF ## name  _ ## name;
#include "gl.dat"
#undef DAT_GL
        };

        GLBackendData impl_gl_data;

        constexpr GLBackendData* gl_data()
        {
            return &impl_gl_data;
        }

        bool init_gl_data(GLBackendData* data)
        {
            OS::LibraryHandle lib_gl = OS::get_gl_library();
            if (lib_gl == OS::LibraryHandle::Sentinel)
            {
                fprintf(stderr, "ERROR: Could not load OpenGL library.\n");
                return false;
            }
            bool success = true;
#define CHECKED_API(x, fn) if (x == nullptr) { fprintf(stderr, "ERROR: GL function: " #fn " could not be loaded.\n"); success = false; }
#define DAT_GL(name, type) data->_ ## name = reinterpret_cast<type>(OS::get_gl_function(lib_gl, str8_mut(str8_literal(#name)))); CHECKED_API(data->_ ## name, name);
#include "gl.dat"
#undef DAT_GL
            return success;
        }

        // Define all the OpenGl functions (use impl directly to avoid the extra function call).
#define glDeleteShader impl_gl_data._glDeleteShader
#define glCreateShader impl_gl_data._glCreateShader
#define glCompileShader impl_gl_data._glCompileShader
#define glGetShaderiv impl_gl_data._glGetShaderiv
#define glGetShaderInfoLog impl_gl_data._glGetShaderInfoLog
#define glDeleteProgram impl_gl_data._glDeleteProgram
#define glCreateProgram impl_gl_data._glCreateProgram
#define glUseProgram impl_gl_data._glUseProgram
#define glAttachShader impl_gl_data._glAttachShader
#define glLinkProgram impl_gl_data._glLinkProgram
#define glGetProgramiv impl_gl_data._glGetProgramiv
#define glGetProgramInfoLog impl_gl_data._glGetProgramInfoLog
#define glGetUniformLocation impl_gl_data._glGetUniformLocation
#define glTexImage2D impl_gl_data._glTexImage2D
#define glTexParameteri impl_gl_data._glTexParameteri
#define glFramebufferTexture2D impl_gl_data._glFramebufferTexture2D
#define glTexStorage2D impl_gl_data._glTexStorage2D
#define glGenTextures impl_gl_data._glGenTextures
#define glDeleteTextures impl_gl_data._glDeleteTextures
#define glBindTexture impl_gl_data._glBindTexture
#define glBindBuffer impl_gl_data._glBindBuffer
#define glEnableVertexAttribArray impl_gl_data._glEnableVertexAttribArray
#define glVertexAttribPointer impl_gl_data._glVertexAttribPointer
#define glGenVertexArrays impl_gl_data._glGenVertexArrays
#define glBindVertexArray impl_gl_data._glBindVertexArray
#define glGenBuffers impl_gl_data._glGenBuffers
#define glDrawBuffers impl_gl_data._glDrawBuffers
#define glCheckFramebufferStatus impl_gl_data._glCheckFramebufferStatus
#define glGenFramebuffers impl_gl_data._glGenFramebuffers
#define glBindFramebuffer impl_gl_data._glBindFramebuffer
#define glViewport impl_gl_data._glViewport
#define glDeleteFramebuffers impl_gl_data._glDeleteFramebuffers
#define glEnable impl_gl_data._glEnable
#define glDisable impl_gl_data._glDisable
#define glScissor impl_gl_data._glScissor
#define glUniform2f impl_gl_data._glUniform2f
#define glUniform1f impl_gl_data._glUniform1f
#define glActiveTexture impl_gl_data._glActiveTexture
#define glUniform1i impl_gl_data._glUniform1i
#define glBufferSubData impl_gl_data._glBufferSubData
#define glDrawArrays impl_gl_data._glDrawArrays
#define glLineWidth impl_gl_data._glLineWidth
#define glClearColor impl_gl_data._glClearColor
#define glClear impl_gl_data._glClear
#define glBlendFunc impl_gl_data._glBlendFunc
#define glBlendFuncSeparate impl_gl_data._glBlendFuncSeparate
#define glBufferData impl_gl_data._glBufferData
#define glDrawElements impl_gl_data._glDrawElements
#define glPixelStorei impl_gl_data._glPixelStorei
#define glTexSubImage2D impl_gl_data._glTexSubImage2D
#define glGetIntegerv impl_gl_data._glGetIntegerv
#define glShaderSource impl_gl_data._glShaderSource
#define glBlitFramebuffer impl_gl_data._glBlitFramebuffer
#define glIsEnabled impl_gl_data._glIsEnabled
#define glDebugMessageCallback impl_gl_data._glDebugMessageCallback
#define glDeleteBuffers impl_gl_data._glDeleteBuffers

        enum class Framebuffer
        {
            _0,
            Default = _0,
            _1,
            _2,

            // These buffers are never reserved.
            Scratch1 = _1,
            Scratch2 = _2,
            Count
        };

        struct FramebufferIO
        {
            Framebuffer src;
            Framebuffer dest;
        };

        // A texture which is like a framebuffer but more specific to the component.
        enum class RenderTexture : uint64_t { };

        template <typename T, int N>
        constexpr GLsizei gl_size(T(&)[N])
        {
            return GLsizei(N);
        }

        struct ShaderDeleter
        {
            void operator()(GLuint shader_handle) const
            {
                glDeleteShader(shader_handle);
            }
        };

        using ShaderHandle = ScopedHandle<GLuint, ShaderDeleter>;

        enum class ShaderType : GLuint
        {
            Vertex = GL_VERTEX_SHADER,
            Fragment = GL_FRAGMENT_SHADER,
        };

        constexpr const char* stringify(ShaderType type)
        {
            switch (type)
            {
            case ShaderType::Fragment:
                return "Fragment";
            case ShaderType::Vertex:
                return "Vertex";
            }
            return "unknown";
        }

        template <typename Reporter>
        inline ShaderHandle compile_shader(ShaderType type, Assets::AssetBuffer ass_buf, Reporter&& reporter)
        {
            ShaderHandle handle { glCreateShader(rep(type)) };
            if (not handle)
                return { };
            GLint src_len = static_cast<GLint>(ass_buf.len);
            const char* src = reinterpret_cast<const char*>(ass_buf.buf);
            glShaderSource(handle.handle(), 1, &src, &src_len);
            glCompileShader(handle.handle());

            // Check for compilation errors.
            GLint success = GL_FALSE;
            glGetShaderiv(handle.handle(), GL_COMPILE_STATUS, &success);
            if (not success)
            {
                char log[512] = { };
                String8 txt = fmt_string(log, "Unable to compile shader type '%s'", stringify(type));
                reporter(txt);
                GLsizei len = 0;
                glGetShaderInfoLog(handle.handle(), gl_size(log), &len, log);
                txt = str8(log, len);
                reporter(txt);
                return { };
            }
            return handle;
        }

        enum class ProgramHandle : GLuint { };

        struct ProgramDeleter
        {
            void operator()(ProgramHandle handle)
            {
                glDeleteProgram(rep(handle));
            }
        };

        using ScopedProgramHandle = ScopedHandle<ProgramHandle, ProgramDeleter>;

        enum class VertexShaderHandle : GLuint { };
        enum class FragmentShaderHandle : GLuint { };

        inline ScopedProgramHandle attach_and_create_program(VertexShaderHandle vert, FragmentShaderHandle frag)
        {
            GLuint program_handle = glCreateProgram();
            ScopedProgramHandle program { ProgramHandle(program_handle) };
            ScopeGuard g { [] { glUseProgram(0); } };

            glAttachShader(program_handle, rep(vert));
            glAttachShader(program_handle, rep(frag));
            return program;
        }

        template <typename Reporter>
        inline bool link_program(ProgramHandle prog, Reporter&& reporter)
        {
            glLinkProgram(rep(prog));

            GLint success = 0;
            glGetProgramiv(rep(prog), GL_LINK_STATUS, &success);
            if (not success)
            {
                char fmt_buf[1024];
                char log[512] = { };
                GLsizei len = 0;
                glGetProgramInfoLog(rep(prog), gl_size(log), &len, log);
                String8 txt = fmt_string(fmt_buf, "Failed to link shaders: %.*s\n", len, log);
                reporter(txt);
                return false;
            }
            return true;
        }

        enum class UniformHandle : GLint { };

        ENABLE_UNHANDLED_CASE_WARNING();
        constexpr Assets::AssetID builtin_vert_shader_asset(VertShader shader)
        {
            switch (shader)
            {
            case VertShader::CameraTransform:
                return Assets::AssetID::VertTransform;
            case VertShader::NoTransform:
                return Assets::AssetID::VertNoTransform;
            case VertShader::OneOneTransform:
                return Assets::AssetID::VertOneOneTransform;
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
                return Assets::AssetID::FragImage;
            case FragShader::TextSubpixel:
                return Assets::AssetID::FragTextSubpixel;
            case FragShader::Text:
                return Assets::AssetID::FragText;
            case FragShader::BlurHorizVert:
                return Assets::AssetID::FragBlurHorizVert;
            case FragShader::Count:
                break;
            }
            return Assets::AssetID::Invalid;
        }
        DISABLE_UNHANDLED_CASE_WARNING();

        enum class ShaderUniformLocation
        {
            Time,
            Resolution,
            CameraCoordFactor,
            CameraPos,
            CameraScale,
            PreviousPassTexture,
            CustomFloatValue1,
            CustomFloatValue2,
            CustomVec2Value1,
            CustomVec2Value2,
            CustomVec2Value3,
            Count
        };

        struct ShaderUniformInput
        {
            ShaderUniformLocation locus;
            const char* name;
        };

        constexpr ShaderUniformInput uniforms[count_of<ShaderUniformLocation>] {
            { .locus = ShaderUniformLocation::Time,
            .name = "time" },

            { .locus = ShaderUniformLocation::Resolution,
            .name = "resolution" },

            { .locus = ShaderUniformLocation::CameraCoordFactor,
            .name = "camera_coord_factor" },

            { .locus = ShaderUniformLocation::CameraPos,
            .name = "camera_pos" },

            { .locus = ShaderUniformLocation::CameraScale,
            .name = "camera_scale" },

            { .locus = ShaderUniformLocation::PreviousPassTexture,
            .name = "prev_pass_tex" },

            { .locus = ShaderUniformLocation::CustomFloatValue1,
            .name = "custom_float_value1" },

            { .locus = ShaderUniformLocation::CustomFloatValue2,
            .name = "custom_float_value2" },

            { .locus = ShaderUniformLocation::CustomVec2Value1,
            .name = "custom_vec2_value1" },

            { .locus = ShaderUniformLocation::CustomVec2Value2,
            .name = "custom_vec2_value2" },

            { .locus = ShaderUniformLocation::CustomVec2Value3,
            .name = "custom_vec2_value3" },
        };

        static_assert(std::is_sorted(std::begin(uniforms),
                                    std::end(uniforms),
                                    [](const auto& lhs, const auto& rhs)
                                    {
                                        return rep(lhs.locus) < rep(rhs.locus);
                                    }));

        template <typename T, int BindPosition>
        struct VertexBinding
        {
            T data;
            static constexpr int locus = BindPosition;
        };

        enum class VertexBindingLocus
        {
            Position,
            Color,
            UV,
            Cust1,
            Cust2,
            Count
        };

        // We model a vertex as a tuple of (pos, color, uv transformation).
        struct RenderVertex
        {
            VertexBinding<Vec2f, rep(VertexBindingLocus::Position)> pos;
            VertexBinding<Vec4f, rep(VertexBindingLocus::Color)> color;
            VertexBinding<Vec2f, rep(VertexBindingLocus::UV)> uv;
            VertexBinding<float, rep(VertexBindingLocus::Cust1)> cust1;
            VertexBinding<float, rep(VertexBindingLocus::Cust2)> cust2;
        };

        constexpr int vertex_cap = 3 * 3;

        static_assert(vertex_cap % 3 == 0,
            "retain relation that the vertex cap is divisible by 3 since we're rendering triangles.");

        constexpr auto default_reporter = [](String8 s)
        {
            fprintf(stderr, "%.*s\n", int(s.size), s.str);
        };

        template <typename Reporter>
        ShaderHandle compile_shader_asset(Assets::AssetID id, Assets::AssetBuffer ass_buf , ShaderType type, Reporter&& reporter)
        {
            if (not Assets::populate_asset(ass_buf, id))
            {
                char fmt_buf[512];
                auto desc = Assets::describe(id);
                String8 txt = fmt_string(fmt_buf, "Failed to load shader asset: %S", desc.proxy_file);
                reporter(txt);
                return { };
            }
            auto handle = compile_shader(type, ass_buf, reporter);
            if (not handle)
            {
                char fmt_buf[512];
                auto desc = Assets::describe(id);
                String8 txt = fmt_string(fmt_buf, "Failed to compile shader file: %S", desc.proxy_file);
                reporter(txt);
            }
            return handle;
        }

        using UniformsContainer = UniformHandle[count_of<ShaderUniformLocation>];
        void populate_uniform_locations(ProgramHandle program, UniformsContainer* container)
        {
            for (int i = 0; i < count_of<ShaderUniformLocation>; ++i)
            {
                (*container)[i] = UniformHandle{ glGetUniformLocation(rep(program), uniforms[i].name) };
            }
        }

        using ShaderProgramContainer = ScopedProgramHandle[count_of<VertShader>][count_of<FragShader>];

        enum class ColorAttachments : GLint
        {
            Default,
            RGBA = Default,

            Count,
        };

        constexpr GLuint color_attachment_index(ColorAttachments attachment)
        {
            return GL_COLOR_ATTACHMENT0 + rep(attachment);
        }

        enum class InternalTextureFormat : GLuint
        {
            RGBA8 = GL_RGBA8,
            Uint32 = GL_R32UI,
        };

        constexpr InternalTextureFormat internal_texture_format_for_attachment(ColorAttachments attachment)
        {
            switch (attachment)
            {
            case ColorAttachments::RGBA:
                return InternalTextureFormat::RGBA8;
            default:
                assert(not "format not implemented");
                return InternalTextureFormat{ };
            }
        }

        enum class TextureFormat : GLuint
        {
            RGBA = GL_RGBA,
            RedInt = GL_RED_INTEGER,
        };

        constexpr TextureFormat texture_format_for_attachment(ColorAttachments attachment)
        {
            switch (attachment)
            {
            case ColorAttachments::RGBA:
                return TextureFormat::RGBA;
            default:
                assert(not "format not implemented");
                return TextureFormat{ };
            }
        }

        void attach_color_texture(GLuint tex_id, const ScreenDimensions& screen, ColorAttachments attachment)
        {
            glTexImage2D(GL_TEXTURE_2D, 0, rep(internal_texture_format_for_attachment(attachment)), rep(screen.width), rep(screen.height), 0, rep(texture_format_for_attachment(attachment)), GL_UNSIGNED_BYTE, nullptr);

            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

            glFramebufferTexture2D(GL_FRAMEBUFFER, color_attachment_index(attachment), GL_TEXTURE_2D, tex_id, 0);
        }

        enum class DepthTextureFormat : GLuint
        {
            Depth24Stencil8 = GL_DEPTH24_STENCIL8
        };

        enum class DepthAttachment : GLuint
        {
            DepthStencilAttachment = GL_DEPTH_STENCIL_ATTACHMENT
        };

        void attach_depth_texture(GLuint tex_id, DepthTextureFormat format, const ScreenDimensions& screen, DepthAttachment attachment)
        {
            glTexStorage2D(GL_TEXTURE_2D, 1, rep(format), rep(screen.width), rep(screen.height));

            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

            glFramebufferTexture2D(GL_FRAMEBUFFER, rep(attachment), GL_TEXTURE_2D, tex_id, 0);
        }

        template <int N>
        void create_textures(GLuint (&output)[N])
        {
            glGenTextures(N, output);
            glBindTexture(GL_TEXTURE_2D, *output);
        }

        GLuint create_texture()
        {
            GLuint id;
            glGenTextures(1, &id);
            glBindTexture(GL_TEXTURE_2D, id);
            return id;
        }

        // Note: https://registry.khronos.org/OpenGL-Refpages/es2.0/xhtml/glDeleteTextures.xml
        // Which implies that deleting textures with the id of 0 will be a no-op.
        template <int N>
        void delete_textures(GLuint (&output)[N])
        {
            glDeleteTextures(N, output);
        }

        void delete_texture(GLuint id)
        {
            glDeleteTextures(1, &id);
        }

        void bind_texture(GLuint id)
        {
            glBindTexture(GL_TEXTURE_2D, id);
        }

        void bind_basic_texture(BasicTexture tex)
        {
            bind_texture(GLuint(rep(tex)));
        }

        enum class FrameBufferID : GLuint { };

        struct FramebufferData
        {
            FrameBufferID id;
            GLuint attachments[rep(ColorAttachments::Count)];
            GLuint depth_attachment;
            ScreenDimensions size;
            bool dirty = true;
        };

        struct RenderTextureData
        {
            RenderTextureData* next;
            RenderTextureData* prev;
            FramebufferData data;
        };

        struct RenderTextureList
        {
            RenderTextureData* first;
            RenderTextureData* last;
            RenderTextureData* free_lst;
            uint64_t count;
        };

        enum class TextureUnit : GLuint
        {
            Sentinel = sentinel_for<TextureUnit>
        };

        enum class VertexBO : GLuint { };
        enum class ElementBO : GLuint { };

        struct CmdBufferPair
        {
            VertexBO vbo;
            ElementBO ebo;
        };

        struct FlushBuffer
        {
            FlushBuffer* next;
            CmdBufferPair buf_pair;
        };

        struct FlushBufferList
        {
            FlushBuffer* first;
            FlushBuffer* last;
            uint64_t count;
        };

        template <Enum E>
        PrimitiveType<E>* as_gl_ref(E* x)
        {
            return static_cast<PrimitiveType<E>*>(static_cast<void*>(x));
        }

        class ScopedRenderViewport
        {
        public:
            ScopedRenderViewport(RenderViewport old, FrameRenderer* renderer);
            ~ScopedRenderViewport();

            void apply_viewport(RenderViewport viewport);
            void reset_viewport();
            ScopedRenderViewport sub() const;

            const RenderViewport& current_viewport() const
            {
                return current;
            }

        private:
            RenderViewport current;
            RenderViewport old_viewport;
            FrameRenderer* renderer;
        };

        // Similar to the class above, however it will not adjust resolution and instead trim
        // viewports using scissor rects.
        class ScopedRenderViewportScissor
        {
        public:
            ScopedRenderViewportScissor(RenderViewport old);
            ~ScopedRenderViewportScissor();

            void apply_viewport(RenderViewport viewport);
            void reset_viewport();

            const RenderViewport& current_viewport() const
            {
                return current;
            }

        private:
            RenderViewport current;
            RenderViewport old_viewport;
            bool old_scissor;
        };

        enum class BlurDirection
        {
            Horiz,
            Vert
        };

        constexpr Vec2f blur_flags(BlurDirection dir)
        {
            // Horiz blur is 0.f Vert is 1.f.
            return { static_cast<float>(dir), 0.f };
        }
    } // namespace [anon]

    // Note: This basic renderer always renders 'up', e.g. a y-coordinate will correspond to the bottom
    // of the render target.
    struct FrameRenderer
    {
        void bind_framebuffer(Framebuffer idx);
        // Back to default render buffer.
        void unbind_framebuffer();
        void enable_prev_pass_texture(Framebuffer prev);
        void enable_prev_pass_texture(RenderTexture prev);
        // Note: It is recommended that you unbind the framebuffer first.  We render
        // this with a non-static instance so that we can shaders can be used for
        // possible postprocessing on the resulting framebuffer.
        void render_framebuffer(const ScreenDimensions& screen, Framebuffer src, Vec2f vert_flags);
        void bind_framebuffer_texture(Framebuffer src);
        // Using framebuffer 'src', render that framebuffer to framebuffer 'dest' using the provided fragment shader.
        // Note: This will make the blend mode sticky, be sure to unset it, if necessary.
        void render_framebuffer_layer(FramebufferIO io, FragShader shader, const ScreenDimensions& full_screen);
        // Similar to the above, but it does not clear framebuffer content first.
        void render_framebuffer_layer_noclear(FramebufferIO io, FragShader shader, const ScreenDimensions& full_screen);

        // Functions for creating render textures and rendering them.
        static RenderTexture create_render_texture(const ScreenDimensions& screen);
        static void bind_render_texture(RenderTexture tex);
        static void bind_render_texture_texture(RenderTexture tex);
        void render_render_texture(RenderTexture tex, Vec2f vert_flags);
        void render_framebuffer_to_render_texture(Framebuffer src, RenderTexture dest, FragShader shader, const ScreenDimensions& screen);
        static void update_render_texture(RenderTexture tex, const ScreenDimensions& screen);
        static void delete_render_texture(RenderTexture tex);

        // User interaction.
        void flush();
        void set_shader(FragShader shader);
        void set_shader(VertShader shader);
        ScopedRenderViewport create_viewport(const ScreenDimensions& screen);
        ScopedRenderViewport create_viewport(const RenderViewport& viewport);
        ScopedRenderViewportScissor create_scissor_viewport(const ScreenDimensions& screen);
        ScopedRenderViewportScissor create_scissor_viewport(const RenderViewport& viewport);

        // Rendering.
        void solid_rect(const Vec2f& top_left, const Vec2f& size, const Vec4f& color);
        void strike_rect(const Vec2f& top_left, const Vec2f& size, float thickness, const Vec4f& color);
        void render_image(const Vec2f& pos, const Vec2f& size, const Vec2f& uv_pos, const Vec2f& uv_size, const Vec4f& color, const Vec2f& vert_flags);

        // Various inputs for shaders.
        const Camera& camera() const;
        void camera(const Camera& new_camera);
        void resolution(const Vec2f& res);
        const Vec2f& resolution() const;
        void update_time(float time, float dt);
        float time() const;
        float delta_time() const;
        void custom_float_value1(float value);
        void custom_float_value2(float value);
        void custom_vec2_value1(const Vec2f& value);
        void custom_vec2_value2(const Vec2f& value);
        void custom_vec2_value3(const Vec2f& value);

        // Various buffer operations.
        void reset_current_buffer(const Vec4f& color);
        void apply_blending_mode(BlendingMode mode);

        void gather_vertices();
        void populate_buffer();
        void draw();

        struct Data
        {
            FragShader selected_frag_shader = FragShader::BasicColor;
            VertShader selected_vert_shader = VertShader::CameraTransform;
            UniformsContainer uniforms{};
            Vec2f resolution;
            float time = 0.f;
            float dt = 0.f;
            float custom_float_value1 = 0.f;
            float custom_float_value2 = 0.f;
            Vec2f custom_vec2_value1;
            Vec2f custom_vec2_value2;
            Vec2f custom_vec2_value3;
            TextureUnit previous_texture = TextureUnit::Sentinel;
            Camera camera;
        };

        Data data;
    };

    // Creation.
    FrameRenderer* make_platform_renderer(Arena::Arena* arena)
    {
        FrameRenderer* renderer = Arena::push_array_no_zero<FrameRenderer>(arena, 1);
        *renderer = {};
        return renderer;
    }

    // Interaction.
    void update_resolution(FrameRenderer* renderer, Vec2f new_res)
    {
        renderer->resolution(new_res);
    }

    void update_time(FrameRenderer* renderer, float app_time, float dt)
    {
        renderer->update_time(app_time, dt);
    }

    namespace
    {
        // Global data shared across all renderer instances.
        GLuint vao;
        VertexBO vbo;
        ShaderProgramContainer shader_programs;
        constinit RenderVertex vertices[vertex_cap]{};
        GLsizei vertices_flush_count = 0;
        FramebufferData framebuffer_collection[rep(Framebuffer::Count)];
        Arena::Arena* renderer_arena;
        Arena::Arena* buffer_arena;
        RenderTextureList render_texture_lst;
        FlushBufferList flush_buffer_lst;
        RenderTexture standard_blur_textures[2];
#ifndef NDEBUG
        // This is used to track the implicit dependency by using a single vertex buffer
        // above.  Two different renderers cannot invoke a render function without first
        // calling flush.
        FrameRenderer* current_renderer;
#endif
        int requested_frames;

        void setup_legacy_vertex_buffer()
        {
            glBindBuffer(GL_ARRAY_BUFFER, rep(vbo));
            // Note: the use of 'sizeof(array)' is intentional because we're providing a total buffer size.
            glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_DYNAMIC_DRAW);

            // position
            glEnableVertexAttribArray(rep(VertexBindingLocus::Position));
            glVertexAttribPointer(
                rep(VertexBindingLocus::Position),
                2, // Vec2f
                GL_FLOAT,
                GL_FALSE,
                sizeof(RenderVertex),
                (GLvoid *) __builtin_offsetof(RenderVertex, pos));

            // color
            glEnableVertexAttribArray(rep(VertexBindingLocus::Color));
            glVertexAttribPointer(
                rep(VertexBindingLocus::Color),
                4, // Vec4f
                GL_FLOAT,
                GL_FALSE,
                sizeof(RenderVertex),
                (GLvoid *) __builtin_offsetof(RenderVertex, color));

            // uv
            glEnableVertexAttribArray(rep(VertexBindingLocus::UV));
            glVertexAttribPointer(
                rep(VertexBindingLocus::UV),
                2, // Vec2f
                GL_FLOAT,
                GL_FALSE,
                sizeof(RenderVertex),
                (GLvoid *) __builtin_offsetof(RenderVertex, uv));

            // cust1
            glEnableVertexAttribArray(rep(VertexBindingLocus::Cust1));
            glVertexAttribPointer(
                rep(VertexBindingLocus::Cust1),
                1, // float
                GL_FLOAT,
                GL_FALSE,
                sizeof(RenderVertex),
                (GLvoid *) __builtin_offsetof(RenderVertex, cust1));

            // cust2
            glEnableVertexAttribArray(rep(VertexBindingLocus::Cust2));
            glVertexAttribPointer(
                rep(VertexBindingLocus::Cust2),
                1, // float
                GL_FLOAT,
                GL_FALSE,
                sizeof(RenderVertex),
                (GLvoid *) __builtin_offsetof(RenderVertex, cust2));
        }

        void setup_cmd_list_vertex_buffer(CmdBufferPair buf_pair)
        {
            glBindBuffer(GL_ARRAY_BUFFER, rep(buf_pair.vbo));
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, rep(buf_pair.ebo));

            // position
            glEnableVertexAttribArray(rep(VertexBindingLocus::Position));
            glVertexAttribPointer(
                rep(VertexBindingLocus::Position),
                2, // Vec2f
                GL_FLOAT,
                GL_FALSE,
                sizeof(RenderVertex),
                (GLvoid *) __builtin_offsetof(RenderVertex, pos));

            // color
            glEnableVertexAttribArray(rep(VertexBindingLocus::Color));
            glVertexAttribPointer(
                rep(VertexBindingLocus::Color),
                4, // Vec4f
                GL_FLOAT,
                GL_FALSE,
                sizeof(RenderVertex),
                (GLvoid *) __builtin_offsetof(RenderVertex, color));

            // uv
            glEnableVertexAttribArray(rep(VertexBindingLocus::UV));
            glVertexAttribPointer(
                rep(VertexBindingLocus::UV),
                2, // Vec2f
                GL_FLOAT,
                GL_FALSE,
                sizeof(RenderVertex),
                (GLvoid *) __builtin_offsetof(RenderVertex, uv));

            // cust1
            glEnableVertexAttribArray(rep(VertexBindingLocus::Cust1));
            glVertexAttribPointer(
                rep(VertexBindingLocus::Cust1),
                1, // float
                GL_FLOAT,
                GL_FALSE,
                sizeof(RenderVertex),
                (GLvoid *) __builtin_offsetof(RenderVertex, cust1));

            // cust2
            glEnableVertexAttribArray(rep(VertexBindingLocus::Cust2));
            glVertexAttribPointer(
                rep(VertexBindingLocus::Cust2),
                1, // float
                GL_FLOAT,
                GL_FALSE,
                sizeof(RenderVertex),
                (GLvoid *) __builtin_offsetof(RenderVertex, cust2));
        }

        RenderTexture create_standard_blur_texture(const ScreenDimensions& screen)
        {
            auto scaled_screen = screen;
            scaled_screen.width = Width{ rep(screen.width) / 2 };
            scaled_screen.height = Height{ rep(screen.height) / 2 };
            return FrameRenderer::create_render_texture(scaled_screen);
        }

        void init_vertex_buffer()
        {
            glGenVertexArrays(1, &vao);
            glBindVertexArray(vao);

            // Create the vertex buffer data binding.
            glGenBuffers(1, as_gl_ref(&vbo));
            setup_legacy_vertex_buffer();
        }

        CmdBufferPair gen_cmd_buffer_pair()
        {
            FlushBuffer* buf = Arena::push_array<FlushBuffer>(buffer_arena, 1);
            glGenBuffers(1, as_gl_ref(&buf->buf_pair.vbo));
            glGenBuffers(1, as_gl_ref(&buf->buf_pair.ebo));
            // Add to the flush list.
            SLLQueuePush(flush_buffer_lst.first, flush_buffer_lst.last, buf);
            return buf->buf_pair;
        }

        void setup_framebuffer_texture_attachments(FramebufferData* data, const ScreenDimensions& screen)
        {
            // Get the last texture so we can restore it after this work.
            GLenum last_active_texture;
            glGetIntegerv(GL_ACTIVE_TEXTURE, (GLint*)&last_active_texture);

            create_textures(data->attachments);
            for (auto i = ColorAttachments{}; i != ColorAttachments::Count; i = extend(i))
            {
                bind_texture(data->attachments[rep(i)]);
                attach_color_texture(data->attachments[rep(i)], screen, i);
            }

            // Attach the single depth texture.
            data->depth_attachment = create_texture();
            bind_texture(data->depth_attachment);
            attach_depth_texture(data->depth_attachment, DepthTextureFormat::Depth24Stencil8, screen, DepthAttachment::DepthStencilAttachment);

            GLenum buffers[rep(ColorAttachments::Count)] = { GL_COLOR_ATTACHMENT0 };
            glDrawBuffers(GLsizei(std::size(buffers)), buffers);

            assert(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);

            // Rebind the old texture.
            bind_texture(last_active_texture);
        }

        void init_framebuffer(FramebufferData* data)
        {
            // Right into the DANGER ZONE!!!
            glGenFramebuffers(1, reinterpret_cast<GLuint*>(&data->id));
            glBindFramebuffer(GL_FRAMEBUFFER, rep(data->id));
            // Bind to the default frame buffer on exit.
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }

        void update_framebuffer_size(FramebufferData* data)
        {
            glBindFramebuffer(GL_FRAMEBUFFER, rep(data->id));

            // Destroy all of the old textures.
            delete_textures(data->attachments);
            delete_texture(data->depth_attachment);

            setup_framebuffer_texture_attachments(data, data->size);
            data->dirty = false;
        }

        void screen_update(const ScreenDimensions& screen)
        {
            glViewport(0, 0, rep(screen.width), rep(screen.height));

            for (auto& fb : framebuffer_collection)
            {
                fb.dirty = true;
                fb.size = screen;
            }
            update_framebuffer_size(&framebuffer_collection[rep(Framebuffer::Default)]);

            for (auto& tex : standard_blur_textures)
            {
                FrameRenderer::delete_render_texture(tex);
                tex = create_standard_blur_texture(screen);
            }
            // Bind to the default frame buffer on exit.
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }

        RenderTexture alloc_render_texture()
        {
            RenderTextureData* tex = render_texture_lst.free_lst;
            if (tex != nullptr)
            {
                SLLStackPop(render_texture_lst.free_lst);
                zero_bytes(tex);
            }
            else
            {
                tex = Arena::push_array<RenderTextureData>(renderer_arena, 1);
            }
            DLLPushFront(render_texture_lst.first, render_texture_lst.last, tex);
            ++render_texture_lst.count;
            RenderTexture handle = RenderTexture{ reinterpret_cast<size_t>(tex) };
            return handle;
        }

        RenderTextureData* render_texture_data(RenderTexture tex)
        {
#ifndef NDEBUG
            // Ensure that the node is in the list.
            RenderTextureData* data = reinterpret_cast<RenderTextureData*>(rep(tex));
            bool found = false;
            for EachNode(n, render_texture_lst.first)
            {
                if (n == data)
                {
                    found = true;
                    break;
                }
            }
            assert(found);
#endif
            return reinterpret_cast<RenderTextureData*>(rep(tex));
        }

        void dealloc_render_texture(RenderTexture tex)
        {
            RenderTextureData* data = render_texture_data(tex);
            DLLRemove(render_texture_lst.first, render_texture_lst.last, data);
            --render_texture_lst.count;
            data->next = data->prev = nullptr;
            SLLStackPush(render_texture_lst.free_lst, data);
        }

        void init_render_texture(RenderTextureData* data, const ScreenDimensions& screen)
        {
            data->data.size = screen;
            init_framebuffer(&data->data);
            update_framebuffer_size(&data->data);
        }

        void update_render_texture(RenderTextureData* data, const ScreenDimensions& screen)
        {
            data->data.size = screen;
            update_framebuffer_size(&data->data);
        }

        void delete_render_texture(RenderTextureData* data)
        {
            delete_textures(data->data.attachments);
            delete_texture(data->data.depth_attachment);
            glDeleteFramebuffers(1, reinterpret_cast<GLuint*>(&data->data.id));
        }

        // Borrowed from RADDBG.
        GLsizeiptr size_for_buffer(GLsizeiptr requested_size)
        {
            uint64_t size = requested_size;
            size += MB(1) - 1;
            size -= size%MB(1);
            return size;
        }

        void populate_buffers(CmdBuffer::VertexBuffer vert_buf, CmdBuffer::IndexBuffer idx_buf)
        {
            constexpr auto draw_sort = GL_DYNAMIC_DRAW;
            const GLsizeiptr vert_buf_size = vert_buf.count * sizeof(CmdBuffer::DrawVertex);
            const GLsizeiptr idx_buf_size = idx_buf.count * sizeof(CmdBuffer::Index);
            glBufferData(GL_ARRAY_BUFFER, size_for_buffer(vert_buf_size), nullptr, draw_sort);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, size_for_buffer(idx_buf_size), nullptr, draw_sort);

            glBufferSubData(GL_ARRAY_BUFFER, 0, vert_buf_size, (const GLvoid*)vert_buf.buf);
            glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, idx_buf_size, (const GLvoid*)idx_buf.buf);
        }

        ScopedRenderViewport::ScopedRenderViewport(RenderViewport old, FrameRenderer* renderer):
            current{ old }, old_viewport{ old }, renderer{ renderer } { }

        ScopedRenderViewport::~ScopedRenderViewport()
        {
            reset_viewport();
        }

        void ScopedRenderViewport::apply_viewport(RenderViewport viewport)
        {
            // This ensures that pixels snap to an even number.
            current = viewport;
            float w = static_cast<float>(current.width);
            if ((rep(current.width) & 1) == 1)
            {
                w += 1.0;
                current.width = extend(current.width);
            }
            float h = static_cast<float>(current.height);
            if ((rep(current.height) & 1) == 1)
            {
                h += 1.0;
                current.height = extend(current.height);
            }
            glViewport(rep(current.offset_x),
                        rep(current.offset_y),
                        rep(current.width),
                        rep(current.height));
            renderer->resolution(Vec2f(w, h));
        }

        void ScopedRenderViewport::reset_viewport()
        {
            apply_viewport(old_viewport);
        }

        ScopedRenderViewport ScopedRenderViewport::sub() const
        {
            return { current, renderer };
        }

        ScopedRenderViewportScissor::ScopedRenderViewportScissor(RenderViewport old):
            current{ old }, old_viewport{ old }, old_scissor{ !!glIsEnabled(GL_SCISSOR_TEST) } { }

        ScopedRenderViewportScissor::~ScopedRenderViewportScissor()
        {
            reset_viewport();
        }

        void ScopedRenderViewportScissor::apply_viewport(RenderViewport viewport)
        {
            current = viewport;
            glEnable(GL_SCISSOR_TEST);
            glViewport(rep(current.offset_x),
                        rep(current.offset_y),
                        // We retain the resolution of the original viewport.
                        rep(old_viewport.width),
                        rep(old_viewport.height));
            // Apply scissor.
            glScissor(rep(current.offset_x),
                        rep(current.offset_y),
                        rep(current.width),
                        rep(current.height));
        }

        void ScopedRenderViewportScissor::reset_viewport()
        {
            apply_viewport(old_viewport);
            if (old_scissor)
            {
                glEnable(GL_SCISSOR_TEST);
            }
            else
            {
                glDisable(GL_SCISSOR_TEST);
            }
        }
    } // namespace [anon]

    // Mostly rendering stuff...
    namespace
    {
        void dummy_cull(FrameRenderer*) { }
        void cull_vertices(FrameRenderer* renderer)
        {
            renderer->flush();
        }

        using Culler = void(*)(FrameRenderer*);

        Culler cullers[] = {
            cull_vertices,
            dummy_cull
        };

        void render_vertex(FrameRenderer* renderer, const RenderVertex& target)
        {
            assert(vertices_flush_count < vertex_cap);
            vertices[vertices_flush_count] = target;
            ++vertices_flush_count;
            // This function is extremely hot, so we need to reduce the number of branches as
            // much as humanly possible.  Below we implement a branchless dispatch table to
            // identify when the vertex count grows large enough to cull.
            // Note: since vertices_flush_count will always be at least '1' at this point, we
            // will only make the following boolean expression 'true' exactly one time, when
            // vertices_flush_count == vertex_cap.
            cullers[bool(vertices_flush_count % vertex_cap)](renderer);
        }

        constexpr Vec2f no_vert_flags = { 0.f, 0.f };

        SUPPRESS_MULTI_LINE_COMMENT_WARNING()
        // 2
        // | \ 
        // 0 - 1
        void render_triangle(FrameRenderer* renderer,
                            const Vec2f& p0, const Vec2f& p1, const Vec2f& p2,
                            const Vec4f& c0, const Vec4f& c1, const Vec4f& c2,
                            const Vec2f& uv0, const Vec2f& uv1, const Vec2f& uv2,
                            const Vec2f& vert_flags)
        {
            render_vertex(renderer, { .pos = p0, .color = c0, .uv = uv0, .cust1 = vert_flags.x, .cust2 = vert_flags.y });
            render_vertex(renderer, { .pos = p1, .color = c1, .uv = uv1, .cust1 = vert_flags.x, .cust2 = vert_flags.y });
            render_vertex(renderer, { .pos = p2, .color = c2, .uv = uv2, .cust1 = vert_flags.x, .cust2 = vert_flags.y });
        }
        ENABLE_MULTI_LINE_COMMENT_WARNING()

        // 2 - 3
        // | \ |
        // 0 - 1
        void render_quad(FrameRenderer* renderer,
                        const Vec2f& p0, const Vec2f& p1, const Vec2f& p2, const Vec2f& p3,
                        const Vec4f& c0, const Vec4f& c1, const Vec4f& c2, const Vec4f& c3,
                        const Vec2f& uv0, const Vec2f& uv1, const Vec2f& uv2, const Vec2f& uv3,
                        const Vec2f& vert_flags)
        {
            render_triangle(renderer, p0, p1, p2, c0, c1, c2, uv0, uv1, uv2, vert_flags);
            render_triangle(renderer, p1, p2, p3, c1, c2, c3, uv1, uv2, uv3, vert_flags);
        }

#ifdef DEBUG_GL
        void gl_debug(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length,
                  const GLchar *message, const void *userParam)
        {
            (void)source,type,id,severity,length,userParam;
            // Print, log, whatever based on the enums and message
            fprintf(stderr, "[OpenGL]: %.*s\n", static_cast<int>(length), message);
        }
#endif // DEBUG_GL
    } // namespace [anon]

    void FrameRenderer::set_shader(FragShader shader)
    {
        data.selected_frag_shader = shader;
        glUseProgram(rep(shader_programs[rep(data.selected_vert_shader)][rep(shader)].handle()));
        populate_uniform_locations(shader_programs[rep(data.selected_vert_shader)][rep(shader)].handle(), &data.uniforms);
        glUniform2f(rep(data.uniforms[rep(ShaderUniformLocation::Resolution)]), data.resolution.x, data.resolution.y);
        glUniform1f(rep(data.uniforms[rep(ShaderUniformLocation::Time)]), data.time);
        glUniform1f(rep(data.uniforms[rep(ShaderUniformLocation::CameraCoordFactor)]), Constants::shader_scale_factor);
        glUniform2f(rep(data.uniforms[rep(ShaderUniformLocation::CameraPos)]), data.camera.pos.x, data.camera.pos.y);
        glUniform2f(rep(data.uniforms[rep(ShaderUniformLocation::CameraScale)]), data.camera.scale.x, data.camera.scale.y);
        glUniform1f(rep(data.uniforms[rep(ShaderUniformLocation::CustomFloatValue1)]), data.custom_float_value1);
        glUniform1f(rep(data.uniforms[rep(ShaderUniformLocation::CustomFloatValue2)]), data.custom_float_value2);
        glUniform2f(rep(data.uniforms[rep(ShaderUniformLocation::CustomVec2Value1)]), data.custom_vec2_value1.x, data.custom_vec2_value1.y);
        glUniform2f(rep(data.uniforms[rep(ShaderUniformLocation::CustomVec2Value2)]), data.custom_vec2_value2.x, data.custom_vec2_value2.y);
        glUniform2f(rep(data.uniforms[rep(ShaderUniformLocation::CustomVec2Value3)]), data.custom_vec2_value3.x, data.custom_vec2_value3.y);
        if (data.previous_texture != TextureUnit::Sentinel)
        {
            // Now we can bind it to texture unit 1.
            glActiveTexture(GL_TEXTURE1);
            bind_texture(rep(data.previous_texture));
            // We also keep texture unit 0 as the active texture unit for future binding since the bind above
            // is a 1-off thing.
            glActiveTexture(GL_TEXTURE0);
            // Set the uniform properly.
            // NOTE: The second parameter is NOT the texture id but rather the unit to which the texture is
            // associated.  In the case of this texture we used GL_TEXTURE1 so the unit is 1.
            glUniform1i(rep(data.uniforms[rep(ShaderUniformLocation::PreviousPassTexture)]), 1);
        }
    }

    void FrameRenderer::set_shader(VertShader shader)
    {
        // Since the vertex shader always requires a fragment shader, we won't bother setting the uniform locations
        // just yet.
        data.selected_vert_shader = shader;
    }

    ScopedRenderViewport FrameRenderer::create_viewport(const ScreenDimensions& screen)
    {
        // Perhaps we should discard the 'screen' argument and simply use glGet to get these properties, but most
        // of the time we know them so we can save the query time.
        return { RenderViewport::basic(screen), this };
    }

    ScopedRenderViewport FrameRenderer::create_viewport(const RenderViewport& viewport)
    {
        // Still possibly use glGet to do this...
        return { viewport, this };
    }

    ScopedRenderViewportScissor FrameRenderer::create_scissor_viewport(const ScreenDimensions& screen)
    {
        // Perhaps we should discard the 'screen' argument and simply use glGet to get these properties, but most
        // of the time we know them so we can save the query time.
        return { RenderViewport::basic(screen) };
    }

    ScopedRenderViewportScissor FrameRenderer::create_scissor_viewport(const RenderViewport& viewport)
    {
        // Still possibly use glGet to do this...
        return { viewport };
    }

    void FrameRenderer::populate_buffer()
    {
        glBufferSubData(GL_ARRAY_BUFFER,
                        0,
                        vertices_flush_count * sizeof(RenderVertex),
                        vertices);
    }

    void FrameRenderer::draw()
    {
        glDrawArrays(GL_TRIANGLES, 0, vertices_flush_count);
    }

    void FrameRenderer::flush()
    {
        // Bind to the legacy buffer.
        populate_buffer();
        draw();
        vertices_flush_count = 0;
#ifndef NDEBUG
        current_renderer = nullptr;
#endif // NDEBUG
    }

    void FrameRenderer::solid_rect(const Vec2f& top_left, const Vec2f& size, const Vec4f& color)
    {
        assert(current_renderer == nullptr or current_renderer == this);
#ifndef NDEBUG
        current_renderer = this;
#endif // NDEBUG
        constexpr Vec2f top_left_uv{-1.f, 1.f};
        constexpr Vec2f bottom_left_uv{-1.f, -1.f};
        constexpr Vec2f top_right_uv{1.f, 1.f};
        constexpr Vec2f bottom_right_uv{1.f, -1.f};
        render_quad(this,
            top_left,
            top_left + Vec2f(size.x, 0),
            top_left + Vec2f(0, size.y),
            top_left + size,
            // Color
            color,
            color,
            color,
            color,
            // UV transform is empty
            top_left_uv,
            top_right_uv,
            bottom_left_uv,
            bottom_right_uv,
            no_vert_flags);
    }

    void FrameRenderer::render_image(const Vec2f& pos, const Vec2f& size, const Vec2f& uv_pos, const Vec2f& uv_size, const Vec4f& color, const Vec2f& vert_flags)
    {
        assert(current_renderer == nullptr or current_renderer == this);
#ifndef NDEBUG
        current_renderer = this;
#endif // NDEBUG
        render_quad(this,
            pos,
            pos + Vec2f(size.x, 0),
            pos + Vec2f(0, size.y),
            pos + size,
            // Color
            color,
            color,
            color,
            color,
            // UV transform
            uv_pos,
            uv_pos + Vec2f(uv_size.x, 0),
            uv_pos + Vec2f(0, uv_size.y),
            uv_pos + uv_size,
            vert_flags);
    }

    void FrameRenderer::strike_rect(const Vec2f& top_left, const Vec2f& size, float thickness, const Vec4f& color)
    {
        auto strike_pos = top_left;
        auto strike_size = size;
        //      A
        //   ----------
        //   |        |
        // D |        | B
        //   |        |
        //   ----------
        //     C
        //
        // A
        strike_size.y = thickness;
        solid_rect(strike_pos, strike_size, color);
        // C
        strike_pos.y = top_left.y + size.y - thickness;
        solid_rect(strike_pos, strike_size, color);
        // D
        strike_pos.y = top_left.y + thickness;
        strike_size.y = size.y - thickness * 2.f;
        strike_size.x = thickness;
        solid_rect(strike_pos, strike_size, color);
        // B
        strike_pos.x = top_left.x + size.x - thickness;
        solid_rect(strike_pos, strike_size, color);
    }

    const Camera& FrameRenderer::camera() const
    {
        return data.camera;
    }

    void FrameRenderer::camera(const Camera& new_camera)
    {
        data.camera = new_camera;
    }

    void FrameRenderer::resolution(const Vec2f& res)
    {
        data.resolution = res;
    }

    const Vec2f& FrameRenderer::resolution() const
    {
        return data.resolution;
    }

    void FrameRenderer::update_time(float time, float dt)
    {
        data.dt = dt;
        data.time = time;
    }

    float FrameRenderer::time() const
    {
        return data.time;
    }

    float FrameRenderer::delta_time() const
    {
        return data.dt;
    }

    void FrameRenderer::custom_float_value1(float value)
    {
        data.custom_float_value1 = value;
    }

    void FrameRenderer::custom_float_value2(float value)
    {
        data.custom_float_value2 = value;
    }

    void FrameRenderer::custom_vec2_value1(const Vec2f& value)
    {
        data.custom_vec2_value1 = value;
    }

    void FrameRenderer::custom_vec2_value2(const Vec2f& value)
    {
        data.custom_vec2_value2 = value;
    }

    void FrameRenderer::custom_vec2_value3(const Vec2f& value)
    {
        data.custom_vec2_value3 = value;
    }

    void FrameRenderer::bind_framebuffer(Framebuffer idx)
    {
        // We should only be binding to other framebuffers.  User 'unbind_framebuffer' to get back
        // to the default render buffer.
        auto& framebuf = framebuffer_collection[rep(idx)];
        // If the framebuffer is dirty, generate the texture.
        if (framebuf.dirty)
        {
            update_framebuffer_size(&framebuf);
        }
        glBindFramebuffer(GL_FRAMEBUFFER, rep(framebuf.id));
    }

    void FrameRenderer::unbind_framebuffer()
    {
        data.previous_texture = TextureUnit::Sentinel;
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void FrameRenderer::enable_prev_pass_texture(Framebuffer prev)
    {
        data.previous_texture = TextureUnit(framebuffer_collection[rep(prev)]
                                    .attachments[rep(ColorAttachments::Default)]);
    }

    void FrameRenderer::enable_prev_pass_texture(RenderTexture prev)
    {
        RenderTextureData* tex_data = render_texture_data(prev);
        data.previous_texture = TextureUnit(tex_data->data.attachments[rep(ColorAttachments::RGBA)]);
    }

    void FrameRenderer::render_framebuffer(const ScreenDimensions& screen, Framebuffer src, Vec2f vert_flags)
    {
        auto& framebuf = framebuffer_collection[rep(src)];
        bind_texture(framebuf.attachments[rep(ColorAttachments::Default)]);
        auto width = rep(screen.width);
        auto height = rep(screen.height);
        render_image(Vec2f(-width + 0.f, -height + 0.f),
                                Vec2f(width * 2.f, height * 2.f),
                                Vec2f(0.f, 0.f),
                                Vec2f(1.f, 1.f),
                                hex_to_vec4f(0xFFFFFFFF),
                                vert_flags);
        flush();
    }

    void FrameRenderer::bind_framebuffer_texture(Framebuffer src)
    {
        auto& framebuf = framebuffer_collection[rep(src)];
        bind_texture(framebuf.attachments[rep(ColorAttachments::Default)]);
    }

    void FrameRenderer::render_framebuffer_layer(FramebufferIO io, FragShader shader, const ScreenDimensions& full_screen)
    {
        bind_framebuffer(io.dest);
        // Clear this framebuffer completely.
        reset_current_buffer(hex_to_vec4f(0xFFFFFFFF));
        // We assume that 'src' has its alpha pre-blended.
        apply_blending_mode(Render::BlendingMode::PremultipliedAlpha);
        set_shader(shader);
        render_framebuffer(full_screen, io.src, no_vert_flags);
    }

    void FrameRenderer::render_framebuffer_layer_noclear(FramebufferIO io, FragShader shader, const ScreenDimensions& full_screen)
    {
        bind_framebuffer(io.dest);
        // We assume that 'src' has its alpha pre-blended.
        apply_blending_mode(Render::BlendingMode::PremultipliedAlpha);
        set_shader(shader);
        render_framebuffer(full_screen, io.src, no_vert_flags);
    }

    // Various buffer operations.
    void FrameRenderer::reset_current_buffer(const Vec4f& color)
    {
        glClearColor(color.x, color.y, color.z, color.a);
        glClear(GL_COLOR_BUFFER_BIT);
    }

    void FrameRenderer::apply_blending_mode(BlendingMode mode)
    {
        switch (mode)
        {
        case BlendingMode::PremultipliedAlpha:
            glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
            break;
        case BlendingMode::SrcAlpha:
            glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
            break;
        case BlendingMode::DualSourceBlend:
            glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC1_COLOR);
            break;
        case BlendingMode::Default:
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            break;
        }
    }

    bool init(const ScreenDimensions& screen)
    {
        PROF_SCOPE();

        GLBackendData* data = gl_data();
        bool result = init_gl_data(data);

#ifdef DEBUG_GL
        glEnable(GL_DEBUG_OUTPUT);
        glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
        glDebugMessageCallback(gl_debug, nullptr);
#endif // DEBUG_GL

        init_vertex_buffer();
        for (auto& framebuf : framebuffer_collection)
        {
            init_framebuffer(&framebuf);
            framebuf.dirty = true;
            framebuf.size = screen;
        }

        // Only init the default framebuffer.
        update_framebuffer_size(&framebuffer_collection[rep(Framebuffer::Default)]);

        // Initialize the render textures for blurring.
        for (auto& tex : standard_blur_textures)
        {
            tex = create_standard_blur_texture(screen);
        }

        ShaderHandle vert_shaders[count_of<VertShader>];
        ShaderHandle frag_shaders[count_of<FragShader>];
        auto scratch = Arena::scratch_begin(Arena::no_conflicts);
        Assets::AssetBuffer ass_buf{};
        for (int v = 0; v != count_of<VertShader>; ++v)
        {
            auto temp_outer = Arena::temp_begin(scratch.arena);
            auto id = builtin_vert_shader_asset(VertShader(v));
            ass_buf.len = rep(Assets::asset_length(id));
            ass_buf.buf = Arena::push_array_no_zero<unsigned char>(temp_outer.arena, ass_buf.len);

            vert_shaders[v] = compile_shader_asset(id, ass_buf, ShaderType::Vertex, default_reporter);
            if (not vert_shaders[v])
            {
                result = false;
                break;
            }
            Arena::temp_end(temp_outer);
        }

        for (int f = 0; f != count_of<FragShader>;++f)
        {
            auto temp_inner = Arena::temp_begin(scratch.arena);
            auto id = builtin_frag_shader_asset(FragShader(f));
            ass_buf.len = rep(Assets::asset_length(id));
            ass_buf.buf = Arena::push_array_no_zero<unsigned char>(temp_inner.arena, ass_buf.len);

            frag_shaders[f] = compile_shader_asset(id, ass_buf, ShaderType::Fragment, default_reporter);
            if (not frag_shaders[f])
            {
                result = false;
                break;
            }
            Arena::temp_end(temp_inner);
        }

        if (result)
        {
            for (int v = 0; v != count_of<VertShader>; ++v)
            {
                for (int f = 0; f != count_of<FragShader>;++f)
                {
                    shader_programs[v][f] = attach_and_create_program(
                        VertexShaderHandle{ vert_shaders[v].handle() },
                        FragmentShaderHandle{ frag_shaders[f].handle() });
                    if (!link_program(shader_programs[v][f].handle(), default_reporter))
                    {
                        result = false;
                        break;
                    }
                }
            }
        }
        Arena::scratch_end(scratch);
        return result;
    }

    void reload_shaders(Feed::MessageFeed* feed)
    {
        auto reporter = [&](String8 s)
        {
            feed->queue_error(s);
        };
        auto scratch = Arena::scratch_begin(Arena::no_conflicts);
        Assets::AssetBuffer ass_buf{};
        // Populate the new programs into a temporary container that we can move from later. If
        // shader compilation or linking fails, we leave this function before the new programs are
        // populated.
        ShaderProgramContainer new_programs;
        bool usable = true;
        // TODO: Rethink how reloading shaders will interact when builtin assets are here.
        for (int v = 0; v != count_of<VertShader>; ++v)
        {
            auto temp_outer = Arena::temp_begin(scratch.arena);
            auto id = builtin_vert_shader_asset(VertShader(v));
            ass_buf.len = rep(Assets::asset_length(id));
            ass_buf.buf = Arena::push_array_no_zero<unsigned char>(temp_outer.arena, ass_buf.len);

            auto vert_handle = compile_shader_asset(id, ass_buf, ShaderType::Vertex, reporter);
            if (not vert_handle)
            {
                usable = false;
                break;
            }
            for (int f = 0; f != count_of<FragShader>;++f)
            {
                auto temp_inner = Arena::temp_begin(scratch.arena);
                id = builtin_frag_shader_asset(FragShader(f));
                ass_buf.len = rep(Assets::asset_length(id));
                ass_buf.buf = Arena::push_array_no_zero<unsigned char>(temp_inner.arena, ass_buf.len);

                auto frag_handle = compile_shader_asset(id, ass_buf, ShaderType::Fragment, reporter);
                if (not frag_handle)
                {
                    usable = false;
                    break;
                }
                new_programs[v][f] = attach_and_create_program(
                    VertexShaderHandle{ vert_handle.handle() },
                    FragmentShaderHandle{ frag_handle.handle() });
                if (not link_program(new_programs[v][f].handle(), reporter))
                {
                    usable = false;
                    break;
                }
                Arena::temp_end(temp_inner);
            }
            if (not usable)
                break;
            Arena::temp_end(temp_outer);
        }

        Arena::scratch_end(scratch);

        if (not usable)
            return;

        // Success!  Let's move them all over.
        for (int v = 0; v != count_of<VertShader>; ++v)
        {
            std::move(std::begin(new_programs[v]), std::end(new_programs[v]), std::begin(shader_programs[v]));
        }
        feed->queue_info("Shaders reloaded.");
    }

    void draw_cmd_list(FrameRenderer* renderer, CmdBuffer::CmdList* cmd_list)
    {
        PROF_SCOPE();

        CmdBufferPair cmd_buffers = gen_cmd_buffer_pair();
        setup_cmd_list_vertex_buffer(cmd_buffers);

        // Setup the primary framebuffer.
        renderer->bind_framebuffer(Render::Framebuffer::Default);
        glEnable(GL_BLEND);
        renderer->apply_blending_mode(Render::BlendingMode::Default);

        const Vec4f bg = Config::diff_colors().background;
        renderer->reset_current_buffer(bg);

#if 0
        constexpr auto draw_sort = GL_DYNAMIC_DRAW;
#endif

        for EachNode(lst_n, cmd_list->draw_list.first)
        {
            CmdBuffer::DrawList* lst = lst_n->lst;
            populate_buffers(lst->vert_buf, lst->idx_buf);
#if 0
            const GLsizeiptr vert_buf_size = lst->vert_buf.count * sizeof(CmdBuffer::DrawVertex);
            const GLsizeiptr idx_buf_size = lst->idx_buf.count * sizeof(CmdBuffer::Index);
            glBufferData(GL_ARRAY_BUFFER, size_for_buffer(vert_buf_size), (const GLvoid*)lst->vert_buf.buf, draw_sort);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, size_for_buffer(idx_buf_size), (const GLvoid*)lst->idx_buf.buf, draw_sort);
#endif
            glEnable(GL_SCISSOR_TEST);
            // It's possible that the screen resolution updates via a command below.
            auto res = lst->screen;
            for EachNode(cmd_n, lst->cmd_buf.first)
            {
                const CmdBuffer::DrawCmd& cmd = cmd_n->cmd;
                switch (cmd.sort)
                {
                case CmdBuffer::CmdSort::Standard:
                    {
                        auto& std_data = cmd.std_data;
                        glViewport(rep(cmd.clip_rect.offset_x),
                                    rep(cmd.clip_rect.offset_y),
                                    rep(res.width),
                                    rep(res.height));

                        glScissor(rep(cmd.clip_rect.offset_x),
                                    rep(cmd.clip_rect.offset_y),
                                    rep(cmd.clip_rect.width),
                                    rep(cmd.clip_rect.height));
                        bind_basic_texture(std_data.tex);
                        renderer->apply_blending_mode(std_data.blend);
                        renderer->set_shader(std_data.vert);
                        renderer->set_shader(std_data.frag);
                        // If this changes, we need to adjust the element size argument to glDrawElements.
                        static_assert(sizeof(CmdBuffer::Index) == 4);
                        glDrawElements(GL_TRIANGLES,
                                        static_cast<GLsizei>(std_data.idx_count),
                                        GL_UNSIGNED_INT,
                                        reinterpret_cast<void*>(static_cast<intptr_t>(rep(std_data.idx_off) * sizeof(CmdBuffer::Index))));
                    }
                    break;
                case CmdBuffer::CmdSort::Blur:
                    // TODO: Split this out into multiple commands?
                    setup_legacy_vertex_buffer();
                    Effects::standard_window_blur(renderer, lst->screen, UI::convert(cmd.clip_rect));
                    // Rebind to original buffers.
                    setup_cmd_list_vertex_buffer(cmd_buffers);
#if 0
                    glBufferData(GL_ARRAY_BUFFER, vert_buf_size, (const GLvoid*)lst->vert_buf.buf, draw_sort);
                    glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx_buf_size, (const GLvoid*)lst->idx_buf.buf, draw_sort);
#endif
                    break;
                case CmdBuffer::CmdSort::CameraUpdate:
                    {
                        renderer->camera(cmd.camera_up);
                    }
                    break;
                case CmdBuffer::CmdSort::ResolutionUpdate:
                    {
                        auto& res_up = cmd.res_up;
                        res = res_up;
                        renderer->resolution(Vec2f(rep(res_up.width) + 0.f, rep(res_up.height) + 0.f));
                    }
                    break;
                }
            }
            glDisable(GL_SCISSOR_TEST);

            // Reset the viewport as well.
            glViewport(0, 0, rep(lst->screen.width), rep(lst->screen.height));
        }

        setup_legacy_vertex_buffer();
    }

    // Functions for creating render textures and rendering them.
    RenderTexture FrameRenderer::create_render_texture(const ScreenDimensions& screen)
    {
        // Allocate a new texture.
        RenderTexture tex = alloc_render_texture();
        RenderTextureData* tex_data = render_texture_data(tex);

        // Setup.
        init_render_texture(tex_data, screen);

        return tex;
    }

    void FrameRenderer::bind_render_texture(RenderTexture tex)
    {
        RenderTextureData* tex_data = render_texture_data(tex);
        glBindFramebuffer(GL_FRAMEBUFFER, rep(tex_data->data.id));
    }

    void FrameRenderer::bind_render_texture_texture(RenderTexture tex)
    {
        RenderTextureData* tex_data = render_texture_data(tex);
        bind_texture(tex_data->data.attachments[rep(ColorAttachments::Default)]);
    }

    void FrameRenderer::render_render_texture(RenderTexture tex, Vec2f vert_flags)
    {
        RenderTextureData* tex_data = render_texture_data(tex);
        bind_texture(tex_data->data.attachments[rep(ColorAttachments::Default)]);
        auto width = rep(tex_data->data.size.width);
        auto height = rep(tex_data->data.size.height);
        render_image(Vec2f(0.f, 0.f),
                                Vec2f(width + 0.f, height + 0.f),
                                Vec2f(0.f, 0.f),
                                Vec2f(1.f, 1.f),
                                hex_to_vec4f(0xFFFFFFFF),
                                vert_flags);
        flush();
    }

    void FrameRenderer::render_framebuffer_to_render_texture(Framebuffer src, RenderTexture dest, FragShader shader, const ScreenDimensions& screen)
    {
        // Bind to the texture.
        bind_render_texture(dest);
        // Note: In order to draw the alpha layer properly for this text, we first need to draw it as though the alpha layer
        // were premultiplied itself (e.g. GL_ONE for alpha): https://stackoverflow.com/a/18497511.  We then draw the fully
        // premultiplied version in 'render_editor_text_texture'.
        apply_blending_mode(Render::BlendingMode::PremultipliedAlpha);
        reset_current_buffer(hex_to_vec4f(0x00000000));

        set_shader(Render::VertShader::OneOneTransform);
        set_shader(shader);
        // Setup the image we're going to sample from.
        bind_framebuffer_texture(src);
        float width = static_cast<float>(rep(screen.width));
        float height = static_cast<float>(rep(screen.height));
        // Note: we always use the size of the framebuffer and rely on the fact that OpenGL
        // will chop samples for us.
        render_image(Vec2f(0.f, 0.f),
                                Vec2f(width, height),
                                Vec2f(0.f, 0.f),
                                Vec2f(1.f, 1.f),
                                hex_to_vec4f(0xFFFFFFFF),
                                no_vert_flags);
        flush();
    }

    void FrameRenderer::update_render_texture(RenderTexture tex, const ScreenDimensions& screen)
    {
        RenderTextureData* tex_data = render_texture_data(tex);
        ::Render::update_render_texture(tex_data, screen);
    }

    void FrameRenderer::delete_render_texture(RenderTexture tex)
    {
        RenderTextureData* tex_data = render_texture_data(tex);
        ::Render::delete_render_texture(tex_data);
        dealloc_render_texture(tex);
    }

    // APIs for general frame requests.
    void request_frames()
    {
        requested_frames = 4;
    }

    int frames_remaining()
    {
        return requested_frames;
    }

    void consume_frame()
    {
        if (requested_frames > 0)
        {
            --requested_frames;
        }
    }

    // APIs for initialization/debugging.
    bool init_renderer_arenas()
    {
        renderer_arena = Arena::alloc(Arena::default_params);
        buffer_arena = Arena::alloc(Arena::default_params);
        return true;
    }

    void display_renderer_version()
    {
        GLint major = 0;
        GLint minor = 0;
        glGetIntegerv(GL_MAJOR_VERSION, &major);
        glGetIntegerv(GL_MINOR_VERSION, &minor);
        printf("OpenGL version %d.%d\n", major, minor);
    }

    void apply_framebuffer(Render::FrameRenderer* renderer, const ScreenDimensions& screen)
    {
        PROF_SCOPE();

        auto& def_framebuf = framebuffer_collection[rep(Framebuffer::Default)];

        glBindFramebuffer(GL_READ_FRAMEBUFFER, rep(def_framebuf.id));

        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);

        glBlitFramebuffer(  0,
                            0,
                            rep(screen.width),
                            rep(screen.height),
                            0,
                            0,
                            rep(screen.width),
                            rep(screen.height),
                            GL_COLOR_BUFFER_BIT,
                            GL_NEAREST);
        renderer->unbind_framebuffer();
    }

    void window_end_frame(OS::OSWindow window)
    {
        for EachNode(n, flush_buffer_lst.first)
        {
            glDeleteBuffers(1, as_gl_ref(&n->buf_pair.vbo));
            glDeleteBuffers(1, as_gl_ref(&n->buf_pair.ebo));
        }
        flush_buffer_lst = {};
        Arena::clear(buffer_arena);

        OS::swap_buffers(window);
    }

    // Resize-related functionality.
    void screen_resize(const ScreenDimensions& screen)
    {
        screen_update(screen);
    }

    // Functions for creating basic textures and manipulating them.
    BasicTexture create_basic_texture(const ScreenDimensions& size)
    {
        auto tex = BasicTexture{ create_texture() };
        bind_basic_texture(tex);

        // Attribute the texture.
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        // Set alignment.
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

        // Generate.
        glTexImage2D(
            GL_TEXTURE_2D,
            0,
            rep(internal_texture_format_for_attachment(ColorAttachments::RGBA)),
            rep(size.width),
            rep(size.height),
            0,
            rep(texture_format_for_attachment(ColorAttachments::RGBA)),
            GL_UNSIGNED_BYTE,
            nullptr);

        return tex;
    }

    void delete_basic_texture(BasicTexture tex)
    {
        delete_texture(GLuint(rep(tex)));
    }

    void submit_basic_texture_data(BasicTexture tex, BasicTextureEntry entry)
    {
        bind_basic_texture(tex);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexSubImage2D(
            GL_TEXTURE_2D,
            0,
            rep(entry.offset_x),
            rep(entry.offset_y),
            rep(entry.width),
            rep(entry.height),
            rep(texture_format_for_attachment(ColorAttachments::RGBA)),
            GL_UNSIGNED_BYTE,
            entry.buffer);
    }

    // Functions for creating glyph cache textures, binding, and manipulating them.
    BasicTexture create_glyph_texture(const ScreenDimensions& dim)
    {
        // Hardcode this for now.
        glActiveTexture(GL_TEXTURE0);
        auto handle = BasicTexture{ create_texture() };
        bind_basic_texture(handle);

        // Attribute the texture.
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        // Set alignment.
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

        // Generate.
        glTexImage2D(
                GL_TEXTURE_2D,
                0,
                rep(internal_texture_format_for_attachment(ColorAttachments::RGBA)),
                rep(dim.width),
                rep(dim.height),
                0,
                rep(texture_format_for_attachment(ColorAttachments::RGBA)),
                GL_UNSIGNED_BYTE,
                nullptr);
        return handle;
    }

    void submit_glyph_data(BasicTexture tex, GlyphEntry entry)
    {
        bind_basic_texture(tex);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexSubImage2D(
            GL_TEXTURE_2D,
            0,
            rep(entry.offset_x),
            rep(entry.offset_y),
            rep(entry.width),
            rep(entry.height),
            GL_RGBA,
            GL_UNSIGNED_BYTE,
            entry.buffer);
    }
} // namespace Render

namespace Render::Effects
{
    void standard_window_blur(FrameRenderer* renderer, const ScreenDimensions& screen, const Render::RenderViewport& window_vp)
    {
        auto vp = renderer->create_scissor_viewport(screen);
        vp.apply_viewport(RenderViewport::basic(screen));
        // The render below is very similar to the one above, except that we're not clearing the source framebuffer (default)
        // and instead we render the result as a clipped rect directly to the output.
        renderer->set_shader(Render::VertShader::OneOneTransform);

        RenderTexture tex_a = standard_blur_textures[0];
        RenderTexture tex_b = standard_blur_textures[1];
        RenderTextureData* data = render_texture_data(tex_a);

        FrameRenderer::bind_render_texture(tex_a);

        // Blur vert.
        // Values for the shader.
        renderer->custom_float_value1(0.03f); // GLOW_FALLOFF.
        renderer->custom_float_value2(4.0); // TAPS.
        renderer->reset_current_buffer(hex_to_vec4f(0x000000FF));
        renderer->apply_blending_mode(Render::BlendingMode::PremultipliedAlpha);
        renderer->set_shader(Render::FragShader::BlurHorizVert);
        renderer->bind_framebuffer_texture(Render::Framebuffer::Default);
        auto width = rep(data->data.size.width);
        auto height = rep(data->data.size.height);
        Vec2f uv_start = 0.f;
        Vec2f uv_end = 1.f;
        Vec2f size{ width + 0.f, height + 0.f };
        renderer->render_image({}, size, uv_start, uv_end, hex_to_vec4f(0xFFFFFFFF), blur_flags(BlurDirection::Vert));
        renderer->flush();

        // Blur horiz.
        // Values for the shader.
        FrameRenderer::bind_render_texture(tex_b);
        renderer->custom_float_value1(0.03f); // GLOW_FALLOFF.
        renderer->custom_float_value2(4.0); // TAPS.
        renderer->reset_current_buffer(hex_to_vec4f(0x000000FF));
        renderer->apply_blending_mode(Render::BlendingMode::PremultipliedAlpha);
        renderer->set_shader(Render::FragShader::BlurHorizVert);
        renderer->render_render_texture(tex_a, blur_flags(BlurDirection::Horiz));

        // Blit back to scratch buffer.
        FrameRenderer::bind_render_texture_texture(tex_b);
        renderer->bind_framebuffer(Render::Framebuffer::Default);

        vp.apply_viewport(window_vp);

        renderer->set_shader(Render::FragShader::Image);
        Vec2f bg_size{ rep(screen.width) + 0.f, rep(screen.height) + 0.f };
        Vec2f bg_pos{ 0.f, 0.f };
        uv_start = { rep(window_vp.offset_x) / bg_size.x, rep(window_vp.offset_y) / bg_size.y };
        uv_end = { 1.f };
        renderer->render_image(bg_pos, bg_size, uv_start, uv_end, hex_to_vec4f(0xFFFFFFFF), no_vert_flags);
        renderer->flush();

        // Reset renderer state.
        renderer->apply_blending_mode(Render::BlendingMode::Default);
        renderer->bind_framebuffer(Render::Framebuffer::Default);
    }
} // namespace Render::Effects

#undef glDeleteShader
#undef glCreateShader
#undef glCompileShader
#undef glGetShaderiv
#undef glGetShaderInfoLog
#undef glDeleteProgram
#undef glCreateProgram
#undef glUseProgram
#undef glAttachShader
#undef glLinkProgram
#undef glGetProgramiv
#undef glGetProgramInfoLog
#undef glGetUniformLocation
#undef glTexImage2D
#undef glTexParameteri
#undef glFramebufferTexture2D
#undef glTexStorage2D
#undef glGenTextures
#undef glDeleteTextures
#undef glBindTexture
#undef glBindBuffer
#undef glEnableVertexAttribArray
#undef glVertexAttribPointer
#undef glGenVertexArrays
#undef glBindVertexArray
#undef glGenBuffers
#undef glDrawBuffers
#undef glCheckFramebufferStatus
#undef glBindFramebuffer
#undef glViewport
#undef glDeleteFramebuffers
#undef glEnable
#undef glDisable
#undef glScissor
#undef glUniform2f
#undef glUniform1f
#undef glActiveTexture
#undef glUniform1i
#undef glBufferSubData
#undef glDrawArrays
#undef glLineWidth
#undef glClearColor
#undef glClear
#undef glBlendFunc
#undef glBlendFuncSeparate
#undef glBufferData
#undef glDrawElements
#undef glPixelStorei
#undef glTexSubImage2D
#undef glGetIntegerv
#undef glShaderSource
#undef glBlitFramebuffer
#undef glIsEnabled
#undef glDebugMessageCallback
#undef glDeleteBuffers