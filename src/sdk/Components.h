#pragma once
#include <array>
#include <cstdint>
#include <string>

#include "sdk/Entity.h"
#include "sdk/Math.h"

namespace sdk {

struct StateVectorComponent : GameComponent {
    static constexpr uint32_t kTypeHash = 0x1B5D5238;
    Vec3 pos;
    Vec3 posOld;
    Vec3 velocity;
};

struct AABBShapeComponent : GameComponent {
    static constexpr uint32_t kTypeHash = 0xBAC1B3CF;
    AABB box;
    Vec2 size;  // width, height
};

struct ActorRotationComponent : GameComponent {
    static constexpr uint32_t kTypeHash = 0x75DF36B7;
    Vec2 rotation;  // x = pitch, y = yaw
    Vec2 rotationOld;
};

struct ActorTypeComponent : GameComponent {
    static constexpr uint32_t kTypeHash = 0x4F6BA419;
    uint32_t type;
};

struct RuntimeIDComponent : GameComponent {
    static constexpr uint32_t kTypeHash = 0xFC0DBBB5;
    uint64_t runtimeId;
};

struct ActorDefinitionIdentifier {
    std::string nameSpace;
    std::string identifier;
    std::string initEvent;
    std::string fullName;
    struct {
        uint64_t hash;
        std::string str;
        const void* lastMatch;
    } canonicalName;
};

struct ActorDefinitionIdentifierComponent : GameComponent {
    static constexpr uint32_t kTypeHash = 0xDEB6534F;
    ActorDefinitionIdentifier identifier;
};

// Raw key state of the movement keys, before the input system turns it into a move vector.
struct MoveInputState {
    bool sneakDown : 1;
    bool sneakToggleDown : 1;
    bool wantDownSlow : 1;
    bool wantUpSlow : 1;
    bool blockSelectDown : 1;
    bool ascendBlock : 1;
    bool descendBlock : 1;
    bool jumpDown : 1;
    bool sprintDown : 1;
    bool upLeft : 1;
    bool upRight : 1;
    bool downLeft : 1;
    bool downRight : 1;
    bool up : 1;
    bool down : 1;
    bool left : 1;
    bool right : 1;
    bool ascend : 1;
    bool descend : 1;
    bool changeHeight : 1;
    bool lookCenter : 1;
    bool sneakInputCurrentlyDown : 1;
    bool sneakInputWasReleased : 1;
    bool sneakInputWasPressed : 1;
    bool jumpInputWasReleased : 1;
    bool jumpInputWasPressed : 1;
    bool jumpInputCurrentlyDown : 1;
    Vec2 analogMoveVector;
    uint8_t lookSlightDir;
    uint8_t lookNormalDir;
    uint8_t lookSmoothDir;
};
static_assert(sizeof(MoveInputState) == 0x10);

struct MoveInputComponent : GameComponent {
    static constexpr uint32_t kTypeHash = 0x018B1887;
    MoveInputState inputState;
    MoveInputState rawInputState;
    int8_t holdAutoJumpInWaterTicks;
    Vec2 move;
    Vec2 lookDelta;
    Vec2 interactDir;
    Vec3 displacement;
    Vec3 displacementDelta;
    Vec3 cameraOrientation;
    bool sneaking : 1;
    bool sprinting : 1;
    bool wantUp : 1;
    bool wantDown : 1;
    bool jumping : 1;
    bool autoJumpingInWater : 1;
    bool moveInputStateLocked : 1;
    bool persistSneak : 1;
    bool autoJumpEnabled : 1;
    bool isCameraRelativeMovementEnabled : 1;
    bool isRotControlledByMoveDirection : 1;
    std::array<bool, 2> isPaddling;
};

}  // namespace sdk
