#include "pch.h"
#include "Renderer.h"
#include "MeshLoader.h"
#include <thread>
#include <atomic>

static Renderer          g_renderer;
static std::thread       g_renderThread;
static std::atomic<bool> g_running{ false };
static std::atomic<bool> g_resizePending{ false };
static std::atomic<int>  g_newW{ 0 }; // 改為 atomic
static std::atomic<int>  g_newH{ 0 }; // 改為 atomic

extern "C" {

    __declspec(dllexport) bool Renderer_Init(IUnknown* panelUnknown, int width, int height) {
        if (!g_renderer.Init(panelUnknown, width, height)) return false;
        g_running = true;
        g_renderThread = std::thread([]() {
            while (g_running) {
                if (g_resizePending.exchange(false))
                    g_renderer.Resize(g_newW, g_newH); // 內部會自己鎖

                g_renderer.RenderFrame(); // 內部會自己鎖
            }
            });
        return true;
    }

    __declspec(dllexport) void Renderer_Resize(int width, int height) {
        g_newW.store(width);
        g_newH.store(height);
        g_resizePending.store(true);
    }

    __declspec(dllexport) void Renderer_Shutdown() {
        g_running = false;
        if (g_renderThread.joinable()) g_renderThread.join();
        g_renderer.Shutdown();
    }

    __declspec(dllexport) bool Renderer_LoadModel(const char* path) {
        // 必須先將字元指標拷貝成 std::string，避免離開此作用域後 path 指標失效
        std::string filePath(path);

        // 啟動背景執行緒處理載入
        std::thread([filePath]() {
            // 1. 這裡包含 Assimp / glTF 的硬碟讀取與解析 (極度耗時)
            auto mesh = MeshLoader::Load(filePath);
            if (mesh) {
                // 2. 這裡處理圖片解碼與 GPU 資源建立
                g_renderer.UploadMeshToGpu(mesh);
            }
            }).detach(); // 放飛執行緒，不阻塞當前呼叫

        return true; // 立即回傳給 C#，UI 不會凍結
    }

    __declspec(dllexport) void Renderer_SetCameraTransform(float px, float py, float pz, float pitch, float yaw) {
        g_renderer.SetCameraTransform(px, py, pz, pitch, yaw);
    }

    __declspec(dllexport) void Renderer_GetStats(int* vertices, int* polygons, int* drawCalls, float* frameTimeMs) {
        if (vertices && polygons && drawCalls && frameTimeMs) {
            g_renderer.GetStats(*vertices, *polygons, *drawCalls, *frameTimeMs);
        }
    }

} // extern "C"