#include <bx/math.h>
#include <bx/bx.h>
#include <bx/mutex.h>
#include <bx/thread.h>
#include <bgfx/bgfx.h>
#include <bgfx/platform.h>
#include <bgfx/embedded_shader.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>

#include "vs_cubes.bin.h"
#include "fs_cubes.bin.h"

static bgfx::PlatformData sdlPlatformData(SDL_Window* _window)
{
    SDL_SysWMinfo wmi;
    SDL_VERSION(&wmi.version);        
    bgfx::PlatformData platformData;
    platformData.ndt = nullptr;
    platformData.nwh = nullptr;
    platformData.context = nullptr;
    platformData.backBuffer = nullptr;
    platformData.backBufferDS = nullptr;
    if (!SDL_GetWindowWMInfo(_window, &wmi))
    {        
        return platformData;
    }
#	if BX_PLATFORM_LINUX || BX_PLATFORM_BSD
#		if ENTRY_CONFIG_USE_WAYLAND
    wl_egl_window *win_impl = (wl_egl_window*)SDL_GetWindowData(_window, "wl_egl_window");
    if(!win_impl)
    {
        int width, height;
        SDL_GetWindowSize(_window, &width, &height);
        struct wl_surface* surface = wmi.info.wl.surface;
        if(!surface)
        {
            return platformData;
        }
        win_impl = wl_egl_window_create(surface, width, height);
        SDL_SetWindowData(_window, "wl_egl_window", win_impl);
    }
    platformData.nwh = (void*)(uintptr_t)win_impl;
#		else
    platformData.nwh = (void*)wmi.info.x11.window;
    platformData.ndt = wmi.info.x11.display;
#		endif
#	elif BX_PLATFORM_OSX || BX_PLATFORM_IOS
    platformData.nwh = wmi.info.cocoa.window;
#	elif BX_PLATFORM_WINDOWS
    platformData.nwh = wmi.info.win.window;
#   elif BX_PLATFORM_ANDROID
    platformData.nwh = wmi.info.android.window;
#	endif // BX_PLATFORM_
    return platformData;
}

struct PosColorVertex
{
    float m_x;
    float m_y;
    float m_z;
    uint32_t m_abgr;

    static void init()
    {
        ms_layout
        .begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0,   4, bgfx::AttribType::Uint8, true)
        .end();
    };

    static bgfx::VertexLayout ms_layout;
};

bgfx::VertexLayout PosColorVertex::ms_layout;

static PosColorVertex s_cubeVertices[] =
{
    {-1.0f,  1.0f,  1.0f, 0xff000000 },
    { 1.0f,  1.0f,  1.0f, 0xff0000ff },
    {-1.0f, -1.0f,  1.0f, 0xff00ff00 },
    { 1.0f, -1.0f,  1.0f, 0xff00ffff },
    {-1.0f,  1.0f, -1.0f, 0xffff0000 },
    { 1.0f,  1.0f, -1.0f, 0xffff00ff },
    {-1.0f, -1.0f, -1.0f, 0xffffff00 },
    { 1.0f, -1.0f, -1.0f, 0xffffffff },
};

static const uint16_t s_cubeTriList[] =
{
    0, 1, 2, // 0
    1, 3, 2,
    4, 6, 5, // 2
    5, 6, 7,
    0, 2, 4, // 4
    4, 2, 6,
    1, 5, 3, // 6
    5, 7, 3,
    0, 4, 1, // 8
    4, 5, 1,
    2, 3, 6, // 10
    6, 3, 7,
};

bgfx::VertexBufferHandle m_vbh;
bgfx::IndexBufferHandle m_ibh;

bgfx::ProgramHandle m_program;

int main(int, char**)
{
    SDL_InitSubSystem(SDL_INIT_VIDEO);

    const int m_width = 800;
    const int m_height = 600;

    SDL_Window* window = nullptr;

    // sdl2
    {

        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);

        window = SDL_CreateWindow(
                     "bgfx SDL2",
                     SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                     m_width, m_height,
                     0
                 );
    }

    // bgfx
    {
        bgfx::setPlatformData(sdlPlatformData(window));

        // prevent creation of a renderer thread
        bgfx::renderFrame();

        bgfx::Init init;
        init.type =
            //bgfx::RendererType::Vulkan;
            bgfx::RendererType::OpenGL;
        init.resolution.width = m_width;
        init.resolution.height = m_height;
        init.resolution.reset = BGFX_RESET_VSYNC;
        bgfx::init(init);
        bgfx::setViewRect(0, 0, 0, uint16_t(m_width), uint16_t(m_height) );
        //bgfx::setScissor()

        bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x000088FF, 1.f, 0);

        static const bgfx::EmbeddedShader s_embeddedShaders[] =
        {
            BGFX_EMBEDDED_SHADER(vs_cubes),
            BGFX_EMBEDDED_SHADER(fs_cubes),

            BGFX_EMBEDDED_SHADER_END()
        };

        bgfx::RendererType::Enum type = bgfx::getRendererType();

        bgfx::ShaderHandle vsh = bgfx::createEmbeddedShader(s_embeddedShaders, type, "vs_cubes");
        bgfx::ShaderHandle fsh = bgfx::createEmbeddedShader(s_embeddedShaders, type, "fs_cubes");

        if (vsh.idx == 65535 || fsh.idx == 65535)
        {
            exit(1);
        }

        // Create program from shaders.
        m_program = bgfx::createProgram(vsh, fsh, true /* destroy shaders when program is destroyed */);

        // Create vertex stream declaration.
        PosColorVertex::init();

        // Create static vertex buffer.
        m_vbh = bgfx::createVertexBuffer(
                    // Static data can be passed with bgfx::makeRef
                    bgfx::makeRef(s_cubeVertices, sizeof(s_cubeVertices) )
                    , PosColorVertex::ms_layout
                );

        // Create static index buffer for triangle list rendering.
        m_ibh = bgfx::createIndexBuffer(
                    // Static data can be passed with bgfx::makeRef
                    bgfx::makeRef(s_cubeTriList, sizeof(s_cubeTriList) )
                );

        const bx::Vec3 at  = { 0.0f, 0.0f,   0.0f };
        const bx::Vec3 eye = { 0.0f, 0.0f, -5.0f };

        // Set view and projection matrix for view 0.
        {
            float view[16];
            bx::mtxLookAt(view, eye, at);

            float proj[16];
            bx::mtxProj(proj, 60.0f, float(m_width)/float(m_height), 0.1f, 100.0f, bgfx::getCaps()->homogeneousDepth);
            bgfx::setViewTransform(0, view, proj);

            // Set view 0 default viewport.
            bgfx::setViewRect(0, 0, 0, uint16_t(m_width), uint16_t(m_height) );
        }
    }

    float time = 0.f;

    while (1)
    {
        // sdl events
        SDL_Event event;
        bool bQuit = false;
        while (SDL_PollEvent(&event))
        {
            switch (event.type)
            {
            case SDL_QUIT:
                bQuit = true;
                break;
            }
        }

        if (bQuit)
        {
            break;
        }

        bgfx::touch(0);

        uint64_t state = 0
                         | BGFX_STATE_WRITE_R
                         | BGFX_STATE_WRITE_G
                         | BGFX_STATE_WRITE_B
                         | BGFX_STATE_WRITE_A
                         | BGFX_STATE_WRITE_Z
                         | BGFX_STATE_DEPTH_TEST_LESS
                         | BGFX_STATE_CULL_CW
                         | BGFX_STATE_MSAA
                         | 0
                         ;

        {
            float mtx[16];
            bx::mtxRotateY(mtx, time);

            // Set model matrix for rendering.
            bgfx::setTransform(mtx);

            // Set vertex and index buffer.
            bgfx::setVertexBuffer(0, m_vbh);
            bgfx::setIndexBuffer(m_ibh);

            // Set render states.
            bgfx::setState(state);

            // Submit primitive for rendering to view 0.
            bgfx::submit(0, m_program);
        }

        bgfx::frame();

        time += 0.05f;
    }

    bgfx::destroy(m_vbh);
    bgfx::destroy(m_ibh);
    bgfx::destroy(m_program);

    bgfx::shutdown();
    SDL_Quit();

    return 0;
}
