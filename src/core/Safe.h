#pragma once
#include <exception>
#include <string>
#include <string_view>

// Runs bot code called from a game hook. A C++ exception or an access violation (e.g. a
// wrong offset after a game update) is logged and disables the bot instead of crashing
// the game.
namespace safe {

template <typename Fn>
void run(std::string_view where, Fn&& fn);

bool faulted();
void resetFault();

namespace detail {
using Thunk = void (*)(void*);
// Returns the SEH code, 0 if no fault. Implemented without C++ objects so __try is allowed.
unsigned long invokeSeh(Thunk thunk, void* ctx, void** faultAddress);
void reportSeh(std::string_view where, unsigned long code, void* address);
void reportException(std::string_view where, const char* what);
}  // namespace detail

template <typename Fn>
void run(std::string_view where, Fn&& fn) {
    if (faulted()) return;
    struct Ctx {
        Fn* fn;
        std::string error;
        bool hasError = false;
    } ctx{&fn};

    auto thunk = [](void* p) {
        auto* c = static_cast<Ctx*>(p);
        try {
            (*c->fn)();
        } catch (const std::exception& e) {
            c->error = e.what();
            c->hasError = true;
        } catch (...) {
            c->error = "unknown exception";
            c->hasError = true;
        }
    };
    void* faultAddress = nullptr;
    unsigned long code = detail::invokeSeh(thunk, &ctx, &faultAddress);
    if (code) detail::reportSeh(where, code, faultAddress);
    else if (ctx.hasError) detail::reportException(where, ctx.error.c_str());
}

}  // namespace safe
