include(FetchContent)

# Hooking
FetchContent_Declare(minhook
    GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
    GIT_TAG v1.3.4
    GIT_SHALLOW ON)

# Signature scanning (SIMD)
FetchContent_Declare(libhat
    GIT_REPOSITORY https://github.com/BasedInc/libhat.git
    GIT_TAG v0.6.0
    GIT_SHALLOW ON)

# The game's ECS. Must be the same revision as the one Minecraft is built with,
# otherwise registry/storage layouts do not match.
FetchContent_Declare(entt
    GIT_REPOSITORY https://github.com/skypjack/entt.git
    GIT_TAG fe8d7d78c4823e8a66a050bf86f5c6318cf76ce7)

set(LIBHAT_TESTING OFF CACHE BOOL "" FORCE)
set(LIBHAT_EXAMPLES OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(minhook libhat entt)
