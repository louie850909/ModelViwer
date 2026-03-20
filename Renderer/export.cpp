#include "pch.h"
#include "Renderer.h"
#include <thread>
#include <atomic>

static Renderer          g_renderer;
static std::thread       g_renderThread;
static std::atomic<bool> g_running{ false };
static std::atomic<bool> g_resizePending{ false };
static int g_newW = 0, g_newH = 0;

extern "C" {

    __declspec(dllexport) bool Renderer_Init(IUnknown* panelUnknown, int width, int height) {
        if (!g_renderer.Init(panelUnknown, width, height)) return false;
        g_running = true;
        g_renderThread = std::thread([]() {
            while (g_running) {
                if (g_resizePending.exchange(false))
                    g_renderer.Resize(g_newW, g_newH);
                g_renderer.RenderFrame();
            }
            });
        return true;
    }

    __declspec(dllexport) void Renderer_Resize(int width, int height) {
        g_newW = width; g_newH = height;
        g_resizePending = true;
    }

    __declspec(dllexport) void Renderer_Shutdown() {
        g_running = false;
        if (g_renderThread.joinable()) g_renderThread.join();
        g_renderer.Shutdown();
    }

} // extern "C"