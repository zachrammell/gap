#include "renderer.h"

#define NOMINMAX
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <windowsx.h>

#include <GL/GL.h>

#pragma comment(lib, "opengl32")

namespace Render
{
    namespace
    {
        // OpenGL-specific window data.
        using PFwglSwapIntervalEXT = BOOL(WINAPI *)(int);
        using PFwglCreateContextAttribsARB = HGLRC(WINAPI *)(HDC, HGLRC, const int *);

        struct WGLWindowData
        {
            HGLRC                        gl_rc;
            HDC                          dc_handle;
            PFwglSwapIntervalEXT         wglSwapIntervalEXT;
            PFwglCreateContextAttribsARB wglCreateContextAttribsARB;
        };

        WGLWindowData impl_gl_wnd_data;

        WGLWindowData* gl_wnd_data()
        {
            return &impl_gl_wnd_data;
        }

        bool create_wgl_device(HWND hWnd, WGLWindowData* data)
        {
            PROF_SCOPE();

            HDC dc_handle = ::GetDC(hWnd);
            PIXELFORMATDESCRIPTOR pfd = { 0 };
            pfd.nSize = sizeof(pfd);
            pfd.nVersion = 1;
            pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
            pfd.iPixelType = PFD_TYPE_RGBA;
            pfd.cColorBits = 32;

            const int pf = ::ChoosePixelFormat(dc_handle, &pfd);
            if (pf == 0)
                return false;
            if (::SetPixelFormat(dc_handle, pf, &pfd) == FALSE)
                return false;
            ::ReleaseDC(hWnd, dc_handle);

            data->dc_handle = ::GetDC(hWnd);
            if (data->gl_rc == nullptr)
            {
                data->gl_rc = wglCreateContext(data->dc_handle);
            }
            return true;
        }

        void init_wgl_extensions(WGLWindowData* data)
        {
            PROF_SCOPE();

            // Try to pull extensions in.
            data->wglSwapIntervalEXT = reinterpret_cast<PFwglSwapIntervalEXT>(wglGetProcAddress("wglSwapIntervalEXT"));

            // Attempt to init the versioned GL context.
            data->wglCreateContextAttribsARB = reinterpret_cast<PFwglCreateContextAttribsARB>(wglGetProcAddress("wglCreateContextAttribsARB"));
            if (data->wglCreateContextAttribsARB != nullptr)
            {
                int attrs[15]; // Max 14 plus terminator.
                int attr = 0;

#define WGL_CONTEXT_MAJOR_VERSION_ARB          0x2091
#define WGL_CONTEXT_MINOR_VERSION_ARB          0x2092

                // TODO: Make this configurable.
                attrs[attr++] = WGL_CONTEXT_MAJOR_VERSION_ARB;
                attrs[attr++] = 3;
                attrs[attr++] = WGL_CONTEXT_MINOR_VERSION_ARB;
                attrs[attr++] = 2;

                // Terminate.
                attrs[attr++] = 0;
                auto new_rc = data->wglCreateContextAttribsARB(data->dc_handle, data->gl_rc, attrs);
                wglDeleteContext(data->gl_rc);
                // Re-initialize with the new context.
                data->gl_rc = new_rc;
                wglMakeCurrent(data->dc_handle, data->gl_rc);
            }

            // Try to enable v-sync.
            if (data->wglSwapIntervalEXT)
            {
                if (data->wglSwapIntervalEXT(-1) != TRUE)
                {
                    fprintf(stderr, "Failed to enable adaptive v-sync, trying default v-sync...\n");
                    data->wglSwapIntervalEXT(1);
                }
            }
            else
            {
                fprintf(stderr, "Cannot enable v-sync.  'wglSwapIntervalEXT' is missing from OpenGL\n");
            }
        }

        void cleanup_wgl_device(HWND hWnd, WGLWindowData* data)
        {
            wglMakeCurrent(nullptr, nullptr);
            ::ReleaseDC(hWnd, data->dc_handle);
            wglDeleteContext(data->gl_rc);
        }
    } // namespace [anon]

    // OS interaction.
    bool os_init_renderer_window(OS::OSWindow wind)
    {
        // Based on os-win32.cpp.
        HWND os_wnd = reinterpret_cast<HWND>(wind);
        return create_wgl_device(os_wnd, gl_wnd_data());
    }

    void os_select_renderer(OS::OSWindow)
    {
        WGLWindowData* gl_data = gl_wnd_data();
        wglMakeCurrent(gl_data->dc_handle, gl_data->gl_rc);
        init_wgl_extensions(gl_data);
    }

    void os_destroy_renderer_window(OS::OSWindow wind)
    {
        // Based on os-win32.cpp.
        HWND os_wnd = reinterpret_cast<HWND>(wind);
        cleanup_wgl_device(os_wnd, gl_wnd_data());
    }

    void os_swap_buffers(OS::OSWindow)
    {
        SwapBuffers(gl_wnd_data()->dc_handle);
    }
} // namespace Render