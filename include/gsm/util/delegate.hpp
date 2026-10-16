#pragma once
#include <cstdint>

namespace gsm
{

template <typename... Args> class Delegate
{
  public:
    using Fn = void (*)(void* ctx, Args...);

    constexpr Delegate() = default;
    constexpr Delegate(Fn fn, void* ctx) : fn_(fn), ctx_(ctx) {}

    void operator()(Args... args) const
    {
        if (fn_) fn_(ctx_, args...);
    }
    explicit operator bool() const { return fn_ != nullptr; }
    void     clear()
    {
        fn_  = nullptr;
        ctx_ = nullptr;
    }

    template <typename T, void (T::*M)(Args...)> static Delegate bind(T* obj)
    {
        return Delegate([](void* ctx, Args... a) { (static_cast<T*>(ctx)->*M)(a...); }, obj);
    }

  private:
    Fn    fn_  = nullptr;
    void* ctx_ = nullptr;
};

} // namespace gsm
