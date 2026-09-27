#pragma once
#include <atomic>

class Renderer {
public:
    ~Renderer() = default;
    Renderer(const Renderer&) = delete;
    Renderer(Renderer&&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    Renderer& operator=(Renderer&&) = delete;

    static bool Init();
    static void Destroy();
    static void Thread(int frame_limit = 0);

    static bool IsOpen();
    static bool IsFocused();
private:
    Renderer() {};

    static Renderer& GetInstance()
    {
        static Renderer i{};
        return i;
    }

    bool InitImpl();
    void ThreadImpl(int frame_limit);
    void DestroyImpl();

    void Render();
    bool HandleState();
    bool HandleWindowOrder();
private:
    std::atomic<bool> isRunning{true};
    std::atomic<bool> isOpen{false};

    std::atomic<bool> isFocused{false};
};
