#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "baritone/utils/BlockPos.h"

namespace baritone {

using BlockList = std::vector<std::string>;  // block names, "minecraft:" added when missing
using Vec3i = BlockPos;                       // "x,y,z"
// Plain strings (block property names...): kept as typed.
struct StringList : std::vector<std::string> {
    using std::vector<std::string>::vector;
};
// java.awt.Color, written "r,g,b" like Baritone's SettingsUtil.
struct Color {
    int r = 0, g = 0, b = 0;
    bool operator==(const Color&) const = default;
};

// Same names, defaults and meaning as Baritone's Settings.java, so #set works the same way.
// (type, name, default, description)
#define BARITONE_SETTINGS(X)                                                                                          \
    X(bool, allowBreak, true, "Allow Baritone to break blocks")                                                       \
    X(BlockList, allowBreakAnyway, BlockList(), "Blocks that may be broken even when allowBreak is false")                     \
    X(bool, allowSprint, true, "Allow Baritone to sprint")                                                            \
    X(bool, allowPlace, true, "Allow Baritone to place blocks")                                                       \
    X(bool, allowInventory, false, "Allow Baritone to move items in your inventory to your hotbar")                   \
    X(int, ticksBetweenInventoryMoves, 1, "Ticks to wait between two inventory moves")                                \
    X(bool, inventoryMoveOnlyIfStationary, false, "Only move items while the player is standing still")               \
    X(BlockList, acceptableThrowawayItems, (BlockList{"minecraft:dirt", "minecraft:cobblestone", "minecraft:netherrack", \
                                                      "minecraft:stone", "minecraft:cobbled_deepslate"}),             \
      "Blocks that may be placed to build paths")                                                                     \
    X(bool, allowParkour, true, "Allow parkour jumps (Bedrock port: on by default)")                                      \
    X(bool, backfill, false, "Fill in blocks broken while pathing once they are behind (not with allowParkour)")      \
    X(bool, allowParkourPlace, false, "Allow placing a block at the end of a parkour jump")                           \
    X(bool, allowParkourAscend, true, "Allow parkour jumps that land one block higher")                               \
    X(bool, allowDiagonalDescend, false, "Allow descending diagonally (more reliable on singleplayer)")               \
    X(bool, allowDiagonalAscend, false, "Allow ascending diagonally")                                                 \
    X(bool, allowDownward, true, "Allow mining the block under the player to go down")                                \
    X(bool, allowJumpAt256, false, "Allow jumping at the build limit")                                                \
    X(bool, allowVines, false, "Allow walking on vines")                                                              \
    X(bool, allowWalkOnBottomSlab, true, "Allow walking on bottom slabs")                                             \
    X(bool, allowWaterBucketFall, true, "Allow falling further than maxFallHeightNoWater with a water bucket")        \
    X(bool, assumeWalkOnWater, false, "Assume the player can walk on water (jesus)")                                  \
    X(bool, assumeWalkOnLava, false, "Assume the player can walk on still lava")                                      \
    X(bool, allowFallIntoLava, false, "Allow falling into lava (fire resistance)")                                    \
    X(bool, assumeExternalAutoTool, false, "Do not switch tools: another mod does it")                                \
    X(bool, autoTool, true, "Switch to the best tool when breaking blocks")                                           \
    X(double, blockPlacementPenalty, 20.0, "Extra cost of placing a block, in ticks")                                 \
    X(double, blockBreakAdditionalPenalty, 2.0, "Extra cost of breaking a block, in ticks")                           \
    X(double, jumpPenalty, 2.0, "Extra cost of a jump, in ticks")                                                     \
    X(double, walkOnWaterOnePenalty, 3.0, "Extra cost of walking on water")                                           \
    X(int, maxFallHeightNoWater, 3, "Highest fall without a water bucket")                                            \
    X(int, maxFallHeightBucket, 20, "Highest fall with a water bucket")                                               \
    X(bool, minimumImprovementRepropagation, true, "Ignore improvements smaller than 0.01 ticks in A*")               \
    X(double, costHeuristic, 3.563, "Heuristic multiplier (higher = faster but worse paths)")                         \
    X(int, pathingMaxChunkBorderFetch, 50, "Unloaded chunk borders crossed before giving up")                         \
    X(double, backtrackCostFavoringCoefficient, 0.5, "Cost multiplier for nodes of the previous path")                \
    X(bool, avoidance, false, "Avoid mobs and mob spawners")                                                          \
    X(double, mobSpawnerAvoidanceCoefficient, 2.0, "Cost multiplier near mob spawners")                               \
    X(int, mobSpawnerAvoidanceRadius, 16, "Radius around mob spawners")                                               \
    X(double, mobAvoidanceCoefficient, 1.5, "Cost multiplier near mobs")                                              \
    X(int, mobAvoidanceRadius, 8, "Radius around mobs")                                                               \
    X(int, primaryTimeoutMS, 500, "Path calculation time before using the best path so far")                          \
    X(int, failureTimeoutMS, 2000, "Path calculation time before giving up")                                          \
    X(int, planAheadPrimaryTimeoutMS, 4000, "Same as primaryTimeoutMS for planning the next segment")                 \
    X(int, planAheadFailureTimeoutMS, 5000, "Same as failureTimeoutMS for planning the next segment")                 \
    X(bool, slowPath, false, "Slow down the calculation to watch it")                                                 \
    X(int, slowPathTimeDelayMS, 100, "Delay between nodes with slowPath")                                             \
    X(int, slowPathTimeoutMS, 40000, "Timeout with slowPath")                                                         \
    X(bool, cutoffAtLoadBoundary, false, "Cut the path where it leaves loaded chunks")                                \
    X(double, maxCostIncrease, 10.0, "Cancel the path if a movement became this much more expensive")                 \
    X(int, costVerificationLookahead, 5, "Movements ahead that are re-checked every tick")                            \
    X(double, pathCutoffFactor, 0.9, "Fraction of a segment executed before switching to the next")                   \
    X(int, pathCutoffMinimumLength, 30, "Segments shorter than this are not cut")                                     \
    X(int, planningTickLookahead, 150, "Ticks before the end of a segment when the next one is planned")              \
    X(int, pathHistoryCutoffAmount, 50, "Path positions dropped when the history is too long")                        \
    X(int, maxPathHistoryLength, 300, "Path positions kept before dropping old ones")                                 \
    X(int, movementTimeoutTicks, 100, "Extra ticks allowed for a movement before cancelling")                         \
    X(BlockList, blocksToAvoid, (BlockList{"minecraft:trip_wire"}), "Blocks the path never walks through")                                            \
    X(BlockList, blocksToDisallowBreaking, BlockList(), "Blocks that are never broken")                                        \
    X(BlockList, blocksToAvoidBreaking, (BlockList{"minecraft:crafting_table", "minecraft:furnace", "minecraft:lit_furnace",  \
                                          "minecraft:chest", "minecraft:trapped_chest"}),                             \
      "Blocks that are broken only if really needed")                                                                 \
    X(double, avoidBreakingMultiplier, 0.1, "Multiplies the break speed of blocksToAvoidBreaking (above 1: encourage breaking)")                       \
    X(bool, avoidUpdatingFallingBlocks, true, "Do not break blocks next to unsupported sand/gravel")                  \
    X(bool, strictLiquidCheck, false, "Never break blocks next to any liquid")                                        \
    X(bool, sprintAscends, true, "Sprint while ascending")                                                            \
    X(bool, overshootTraverse, true, "Combine consecutive traverses when possible")                                   \
    X(bool, pauseMiningForFallingBlocks, true, "Wait for falling blocks before mining")                               \
    X(bool, freeLook, true, "Move without moving the camera (where possible)")                                        \
    X(bool, antiCheatCompatibility, true, "Look where the bot acts, for servers with anticheat")                      \
    X(double, blockReachDistance, 4.5, "Reach distance for breaking/placing")                                         \
    X(int, rightClickSpeed, 4, "Ticks between right clicks")                                                          \
    X(bool, sprintInWater, true, "Sprint (swim) in water")                                                            \
    X(bool, walkWhileBreaking, true, "Keep walking while breaking a block in the way")                                \
    X(int, mineGoalUpdateInterval, 5, "Ticks between ore position updates when mining")                               \
    X(int, mineMaxOreLocationsCount, 64, "Ore locations considered when mining")                                      \
    X(bool, legitMine, false, "Only mine ores that have been seen")                                                   \
    X(int, legitMineYLevel, -59, "Y level to branch mine at when no ore is known (-59: diamonds; other ores use their own level)") \
    X(bool, blacklistClosestOnFailure, true, "When no path is found to the closest target, blacklist it")             \
    X(bool, exploreForBlocks, true, "Explore when no matching block is known")                                        \
    X(bool, mineScanDroppedItems, true, "Pick up the items dropped by mined blocks")                                  \
    X(bool, forceInternalMining, true, "Path into the ore block itself (more efficient)")                             \
    X(bool, internalMiningAirException, true, "Treat air next to an ore as part of the ore for goals")               \
    X(int, mineDropLoiterDurationMSThanksLouca, 250, "Time to wait for drops after mining a block")                   \
    X(int, maxScanChunkRadius, 8, "Chunks around the player scanned for blocks (#mine, #goto <bloc>)")               \
    X(int, maxCachedWorldScanCount, 10, "Tracked blocks taken from the long-distance cache per search")              \
    X(bool, chunkCaching, true, "Remember every chunk in 2 bits per block on disk for long-distance pathing")        \
    X(bool, pruneRegionsFromRAM, true, "Unload cached regions far from the player (they stay on disk)")              \
    X(int, axisHeight, 120, "Height of the highways for #axis")                                                       \
    X(int, followRadius, 3, "Distance kept from the followed entity")                                                 \
    X(int, followOffsetDistance, 0, "Follow at this distance in followOffsetDirection")                               \
    X(double, followOffsetDirection, 0.0, "Direction (yaw) of followOffsetDistance")                                 \
    X(int, followTargetMaxDistance, 0, "Ignore entities further than this (0 = no limit)")                           \
    X(int, exploreChunkSetMinimumSize, 10, "Explore: minimum chunk set size")                                         \
    X(bool, renderPath, true, "Render the current path")                                                              \
    X(bool, renderGoal, true, "Render the goal")                                                                      \
    X(bool, renderSelectionBoxes, true, "Render the boxes of blocks being broken/placed")                             \
    X(bool, renderSelection, true, "Render the #sel selection and the schematic being built")                         \
    X(bool, renderSelectionCorners, true, "Render the #sel pos1/pos2 corners")                                        \
    X(bool, buildIgnoreExisting, false, "Build: treat every non-air block as correct, only place in air")             \
    X(BlockList, buildIgnoreBlocks, BlockList(), "Build: blocks that are always correct where they are")              \
    X(BlockList, okIfAir, BlockList(), "Build: blocks the schematic wants that may stay air")                         \
    X(bool, buildIgnoreDirection, false, "Build: accept blocks facing any direction")                                 \
    X(BlockList, buildSkipBlocks, BlockList(), "Build: blocks of the schematic that are not built")                   \
    X(BlockList, buildSubstitutes, BlockList(), "Build: from>to, place `to` when there is no `from`")                 \
    X(BlockList, buildValidSubstitutes, BlockList(), "Build: from>to, `to` is accepted where `from` is wanted")       \
    X(bool, buildInLayers, false, "Build one layer at a time")                                                        \
    X(bool, layerOrder, false, "Build layers from the top down")                                                      \
    X(int, layerHeight, 1, "Height of a layer for buildInLayers")                                                     \
    X(int, startAtLayer, 0, "First layer to build for buildInLayers")                                                 \
    X(bool, mapArtMode, false, "Build: only the top block of each column matters")                                    \
    X(bool, buildOnlySelection, false, "Build only inside the #sel selections")                                       \
    X(double, elytraFireworkSpeed, 1.2, "Elytra: use a firework below this speed (blocks per tick)")                  \
    X(bool, elytraConserveFireworks, false, "Elytra: use fireworks only when slower and well clear of obstacles")     \
    X(bool, elytraAutoJump, false, "Elytra: take off from the ground by itself")                                      \
    X(int, elytraMinFireworksBeforeLanding, 5, "Elytra: land when fewer fireworks are left")                          \
    X(bool, elytraAllowEmergencyLand, true, "Elytra: land when out of fireworks instead of falling")                  \
    X(double, elytraMinimumAvoidance, 0.2, "Elytra: extra room kept around the player when checking lines")           \
    X(bool, renderElytraPath, true, "Render the elytra flight path")                                                  \
    X(Vec3i, buildRepeat, Vec3i(), "Build: offset to move the schematic by when it is done (0,0,0 = no repeat)")      \
    X(int, buildRepeatCount, -1, "Build: how many times to repeat (-1 = forever)")                                    \
    X(bool, chatDebug, false, "Print debug messages in the chat")                                                     \
    X(bool, allowUnfocusedMovement, true, "Bedrock: keep moving while the game window is in the background")          \
    X(bool, doDeathWaypoints, true, "Save a death waypoint when the player dies")                                     \
    X(bool, doBedWaypoints, true, "Save a bed waypoint when the player sleeps")                                       \
    X(bool, logMovement, false, "Dev: log the movement state every tick in log.txt")                                  \
    X(bool, chatControl, true, "Accept commands typed in the chat") \
    /* ---- added for parity with Baritone 1.21 (same names and defaults) ---- */ \
    X(bool, allowPlaceInFluidsSource, true, "Allow Baritone to place blocks in fluid source blocks") \
    X(bool, allowPlaceInFluidsFlow, true, "Allow Baritone to place blocks in flowing fluid") \
    X(bool, assumeStep, false, "Assume step functionality; don't jump on an Ascend") \
    X(bool, assumeSafeWalk, false, "Assume safe walk functionality; don't sneak on a backplace traverse") \
    X(bool, allowJumpAtBuildLimit, false, "Allow parkour jumps when standing at the maximum height") \
    X(bool, allowWalkOnMagmaBlocks, false, "Walk on magma blocks, sneaking") \
    X(StringList, buildIgnoreProperties, StringList(), "Block properties (states) the builder ignores") \
    X(bool, considerPotionEffects, true, "Adjust break costs for Haste and Mining Fatigue") \
    X(double, randomLooking113, 2.0, "How many degrees to randomize the yaw every tick (1.13+ only)") \
    X(int, blockBreakSpeed, 6, "Ticks between breaking a block and starting the next one (creative)") \
    X(double, randomLooking, 0.01, "How many degrees to randomize the pitch and yaw every tick") \
    X(bool, rightClickContainerOnArrival, true, "Right click a container once a #goto towards it arrives") \
    X(bool, enterPortal, true, "Walk all the way into a nether portal on #goto portal") \
    X(int, pathingMapDefaultSize, 1024, "Initial size of the pathing node map") \
    X(double, pathingMapLoadFactor, 0.75, "Load factor of the pathing node map") \
    X(bool, allowOvershootDiagonalDescend, true, "Sprint through a descend followed by a diagonal") \
    X(bool, simplifyUnloadedYCoord, true, "A block goal in unloaded chunks is pathed to as an X/Z goal") \
    X(bool, repackOnAnyBlockChange, true, "Repack the whole chunk in the cache whenever a block changes") \
    X(int, chunkPackerQueueMaxSize, 2000, "Maximum number of chunks waiting to be packed into the cache") \
    X(bool, logAsToast, false, "Show messages as a popup (Windows notification) instead of the chat") \
    X(int, toastTimer, 5000, "How long a popup message stays, in milliseconds") \
    X(bool, chatControlAnyway, false, "Accept commands typed in the chat even when chatControl is off") \
    X(bool, renderPathAsLine, false, "Render the path as a line") \
    X(bool, renderGoalAnimated, true, "Animate the goal box") \
    X(bool, renderGoalIgnoreDepth, true, "Render the goal through blocks") \
    X(bool, renderGoalXZBeacon, false, "Render X/Z goals as a beacon beam") \
    X(bool, renderSelectionBoxesIgnoreDepth, true, "Render the blocks to break/place/walk into through blocks") \
    X(bool, renderPathIgnoreDepth, true, "Render the path through blocks") \
    X(double, pathRenderLineWidthPixels, 5.0, "Line width of the path, in pixels") \
    X(double, goalRenderLineWidthPixels, 3.0, "Line width of the goal, in pixels") \
    X(bool, fadePath, false, "Fade the path out from 20 movements ahead, hide it from 30") \
    X(bool, blockFreeLook, false, "Break and place blocks without forcing the camera rotation") \
    X(bool, elytraFreeLook, true, "Fly with an elytra without forcing the camera rotation") \
    X(bool, smoothLook, false, "Camera yaw = average of the last smoothLookTicks rotations") \
    X(bool, elytraSmoothLook, false, "smoothLook while flying with an elytra") \
    X(int, smoothLookTicks, 5, "Ticks averaged by smoothLook") \
    X(bool, remainWithExistingLookDirection, true, "Keep the current look direction as often as possible") \
    X(bool, pathThroughCachedOnly, false, "Path only through cached chunks (never turn this on)") \
    X(bool, renderCachedChunks, false, "Render cached chunks as semi-transparent") \
    X(double, cachedChunksOpacity, 0.5, "Opacity of renderCachedChunks") \
    X(bool, prefixControl, true, "Run commands typed with the prefix") \
    X(std::string, prefix, "#", "The command prefix for chat control") \
    X(bool, shortBaritonePrefix, false, "Use [B] instead of [Baritone] in chat messages") \
    X(bool, useMessageTag, false, "Mark chat messages with a tag instead of a prefix") \
    X(bool, echoCommands, true, "Echo commands to the chat when they are run") \
    X(bool, censorCoordinates, false, "Censor coordinates in goals and block positions") \
    X(bool, censorRanCommands, false, "Censor the arguments of commands echoed to the chat") \
    X(bool, itemSaver, false, "Stop using tools just before they break") \
    X(int, itemSaverThreshold, 10, "Durability left on tools with itemSaver") \
    X(bool, preferSilkTouch, false, "Always prefer silk touch tools") \
    X(bool, splicePath, true, "Splice the next segment onto the current path") \
    X(int, minYLevelWhileMining, 0, "Minimum y level to mine at (0 = off)") \
    X(int, maxYLevelWhileMining, 2031, "Maximum y level to mine at") \
    X(bool, allowOnlyExposedOres, false, "Only mine ores next to air (ore obfuscators)") \
    X(int, allowOnlyExposedOresDistance, 1, "Distance searched for air with allowOnlyExposedOres") \
    X(int, worldExploringChunkOffset, 0, "Explore: offset the closest unloaded chunk by this much") \
    X(int, exploreMaintainY, 64, "Explore: y level to keep (-1 = off)") \
    X(bool, replantCrops, true, "Farm: replant crops") \
    X(bool, replantNetherWart, false, "Farm: replant nether wart") \
    X(bool, farmUsingSelection, false, "Farm only inside the current selection") \
    X(int, farmMaxScanSize, 256, "Farm: maximum number of blocks scanned") \
    X(bool, extendCacheOnThreshold, false, "Also scan the loaded world when the cache gives few blocks") \
    X(bool, skipFailedLayers, false, "Build: skip a layer that can't be built") \
    X(bool, buildRepeatSneaky, true, "Build: don't announce each repetition") \
    X(bool, breakFromAbove, false, "Build: allow standing above a block while breaking it") \
    X(bool, goalBreakFromAbove, false, "Build: also aim for the blocks above the ones to break") \
    X(bool, okIfWater, false, "Build: don't correct blocks that are currently water") \
    X(int, incorrectSize, 100, "Build: maximum number of incorrect blocks kept") \
    X(double, breakCorrectBlockPenaltyMultiplier, 10.0, "Build: cost multiplier for breaking a correct block") \
    X(double, placeIncorrectBlockPenaltyMultiplier, 2.0, "Build: cost multiplier for placing a wrong block") \
    X(bool, schematicOrientationX, false, "Build: the highest X is the origin") \
    X(bool, schematicOrientationY, false, "Build: the highest Y is the origin") \
    X(bool, schematicOrientationZ, false, "Build: the highest Z is the origin") \
    X(std::string, buildSchematicRotation, "NONE", "Build: NONE, CLOCKWISE_90, CLOCKWISE_180, COUNTERCLOCKWISE_90") \
    X(std::string, buildSchematicMirror, "NONE", "Build: NONE, LEFT_RIGHT, FRONT_BACK") \
    X(std::string, schematicFallbackExtension, "schematic", "Build: extension tried when none is given") \
    X(int, builderTickScanRadius, 5, "Build: radius rescanned every tick") \
    X(bool, distanceTrim, true, "Build: forget incorrect blocks too far away") \
    X(bool, cancelOnGoalInvalidation, true, "Cancel the path when its goal changes and it no longer ends in it") \
    X(bool, disconnectOnArrival, false, "Leave the world when arriving at the goal") \
    X(bool, legitMineIncludeDiagonals, false, "legitMine: also see ores diagonal to known ores") \
    X(bool, disableCompletionCheck, false, "Explore: don't check whether the filter is complete") \
    X(int64_t, cachedChunksExpirySeconds, -1, "Delete cached chunks older than this (-1 = never)") \
    X(bool, verboseCommandExceptions, false, "Log every command error with details") \
    X(double, yLevelBoxSize, 15.0, "Size of the box rendered for a Y level goal") \
    X(Color, colorCurrentPath, (Color{255, 0, 0}), "Color of the current path") \
    X(Color, colorNextPath, (Color{255, 0, 255}), "Color of the next path") \
    X(Color, colorBlocksToBreak, (Color{255, 0, 0}), "Color of the blocks to break") \
    X(Color, colorBlocksToPlace, (Color{0, 255, 0}), "Color of the blocks to place") \
    X(Color, colorBlocksToWalkInto, (Color{255, 0, 255}), "Color of the blocks to walk into") \
    X(Color, colorBestPathSoFar, (Color{0, 0, 255}), "Color of the best path so far") \
    X(Color, colorMostRecentConsidered, (Color{0, 255, 255}), "Color of the most recently considered path") \
    X(Color, colorGoalBox, (Color{0, 255, 0}), "Color of the goal") \
    X(Color, colorInvertedGoalBox, (Color{255, 0, 0}), "Color of an inverted goal") \
    X(Color, colorSelection, (Color{0, 255, 255}), "Color of the selections") \
    X(Color, colorSelectionPos1, (Color{0, 0, 0}), "Color of the selection pos1") \
    X(Color, colorSelectionPos2, (Color{255, 200, 0}), "Color of the selection pos2") \
    X(double, selectionOpacity, 0.5, "Opacity of the selections") \
    X(double, selectionLineWidth, 2.0, "Line width of the selections, in pixels") \
    X(bool, renderSelectionIgnoreDepth, true, "Render the selections through blocks") \
    X(bool, useSwordToMine, true, "Use a sword to mine (cobwebs, bamboo...)") \
    X(bool, desktopNotifications, false, "Windows notifications") \
    X(bool, notificationOnPathComplete, true, "Notify when a path is complete") \
    X(bool, notificationOnFarmFail, true, "Notify when farming fails") \
    X(bool, notificationOnBuildFinished, true, "Notify when a build is finished") \
    X(bool, notificationOnExploreFinished, true, "Notify when exploring is finished") \
    X(bool, notificationOnMineFail, true, "Notify when mining fails") \
    X(int, elytraSimulationTicks, 20, "Elytra: ticks of flight simulated without a firework") \
    X(int, elytraPitchRange, 25, "Elytra: maximum pitch deviation from the line of sight to the target") \
    X(int, elytraFireworkSetbackUseDelay, 15, "Elytra: ticks after a server setback before using a firework") \
    X(bool, elytraRenderRaytraces, false, "Elytra: render the raytraces") \
    X(bool, elytraRenderHitboxRaytraces, false, "Elytra: render the hitbox raytraces") \
    X(bool, elytraRenderSimulation, true, "Elytra: render the best simulated flight") \
    X(int64_t, elytraNetherSeed, 146008555100680, "Elytra: nether seed for terrain prediction (not on Bedrock)") \
    X(bool, elytraPredictTerrain, false, "Elytra: predict nether terrain from the seed (not on Bedrock)") \
    X(bool, elytraAutoSwap, true, "Elytra: swap a worn out elytra for another one") \
    X(int, elytraMinimumDurability, 5, "Elytra: durability under which it is swapped") \
    X(int64_t, elytraTimeBetweenCacheCullSecs, 180, "Elytra: seconds between culls of far chunks") \
    X(int, elytraCacheCullDistance, 5000, "Elytra: chunks further than this are culled") \
    X(bool, elytraAllowLandOnNetherFortress, false, "Elytra: nether bricks are a valid landing block") \
    X(bool, elytraTermsAccepted, false, "Elytra: the user read the elytra warnings") \
    X(bool, elytraChatSpam, false, "Elytra: verbose chat logging") \
    X(bool, elytraCustomAllocator, true, "Elytra: custom allocator (no effect on Bedrock)") \
    X(bool, elytraAllowTightSpaces, false, "Elytra: allow flying in tight spaces") \
    X(bool, elytraAllowAboveRoof, false, "Elytra: allow flying above the nether roof") \
    X(bool, elytraUseCache, true, "Elytra: use the chunk cache for the flight path") \
    X(bool, elytraAllowAboveBuildLimit, true, "Elytra: fly above the build limit on long trips") \
    X(int, elytraLongDistanceThreshold, 500, "Elytra: trips longer than this may fly above the build limit")

struct Settings {
#define BARITONE_SETTING_FIELD(type, name, def, desc) type name = def;
    BARITONE_SETTINGS(BARITONE_SETTING_FIELD)
#undef BARITONE_SETTING_FIELD

    // Live settings, changed by #set on the game thread. Path calculations copy what
    // they need when they start.
    static Settings& get();
};

using SettingPtr = std::variant<bool*, int*, int64_t*, double*, BlockList*, StringList*, Vec3i*, std::string*, Color*>;

struct SettingInfo {
    std::string_view name;
    std::string_view description;
    SettingPtr ptr;
    SettingPtr defaultValue;
};

// Every setting, for #set / #modified / #reset and the settings file.
std::span<const SettingInfo> settingsTable();
const SettingInfo* findSetting(std::string_view name);  // case-insensitive
std::string settingValueToString(const SettingPtr& ptr);
// Throws std::invalid_argument with a readable message.
void parseSettingValue(const SettingInfo& info, std::string_view text);
bool isModified(const SettingInfo& info);
void resetSetting(const SettingInfo& info);

// settings.txt like Baritone's: one "name value" line per modified setting.
void saveSettings(const std::string& path);
// Returns the number of settings read; unknown or invalid lines are skipped.
int loadSettings(const std::string& path);

}  // namespace baritone
