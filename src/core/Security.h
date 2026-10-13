#pragma once
#include <Windows.h>

// Security attributes for the dev pipe and the unload event: any authenticated user, any
// integrity level. Without this, tools run from a normal terminal cannot reach a game
// started as administrator.
class OpenSecurity {
public:
    OpenSecurity();
    ~OpenSecurity();
    OpenSecurity(const OpenSecurity&) = delete;
    OpenSecurity& operator=(const OpenSecurity&) = delete;

    SECURITY_ATTRIBUTES* get() { return descriptor_ ? &attributes_ : nullptr; }

private:
    PSECURITY_DESCRIPTOR descriptor_ = nullptr;
    SECURITY_ATTRIBUTES attributes_{};
};
