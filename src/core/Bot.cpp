#include "core/Bot.h"

#include <Windows.h>

#include <cctype>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <format>

#include "baritone/Settings.h"
#include "baritone/utils/RayTrace.h"
#include "baritone/world/BlockStateInterface.h"
#include "command/Commands.h"
#include "core/Chat.h"
#include "core/Notify.h"
#include "core/DevPipe.h"
#include "core/Log.h"
#include "core/Perf.h"
#include "core/Paths.h"
#include "core/Watchdog.h"
#include "hooks/GameHooks.h"
#include "platform/PathRenderer.h"
#include "platform/WorldTracker.h"
#include "sdk/Game.h"


using BInput = baritone::Input;

Bot& Bot::get() {
    static Bot instance;
    return instance;
}

bool Bot::start() {
    baritone_ = std::make_unique<baritone::Baritone>(player_, [](const std::string& msg, bool error) {
        if (error) chat::error(msg);
        else chat::info(msg);
    });
    baritone::Baritone::setNotifier([](const std::string& msg, bool error) { notify::show("Baritone", msg, error); });
    int loaded = baritone::loadSettings((paths::dataDir() / "settings.txt").string());
    if (loaded > 0) logger::info("{} settings loaded from settings.txt", loaded);
    command::registerAll(commands_, *this);
    if (!gamehooks::install()) return false;
    devpipe::start();
    watchdog::start();
    chat::info("Loaded. Type #help for a list of commands.");
    return true;
}

void Bot::stop() {
    {
        std::lock_guard lock(diagnosticMutex_);
        diagnosticsStopped_ = true;
        if (diagnosticJob_.valid()) diagnosticJob_.wait();
    }
    watchdog::stop();
    devpipe::stop();
    notify::shutdown();
    cancelAll();
    taskWriter_.wait();
    schematicLoad_.wait();
    updateFocusOverride(false);
    platform::PathRenderer::get().shutdown();
    if (baritone_) baritone_->shutdown();
}

int Bot::loadSettings() {
    return baritone::loadSettings((paths::dataDir() / "settings.txt").string());
}

void Bot::saveSettings() {
    baritone::saveSettings((paths::dataDir() / "settings.txt").string());
}

void Bot::cancelAll() {
    if (!dispatchingTask_) { tasks_.pause(); saveTasks(); }
    if (!dispatchingTask_ && baritone_) baritone_->survival().setEnabled(false);
    survey_.clear(); surveyReport_ = false;
    quarryActive_ = false;
    quarryChecking_ = false; quarryVerification_.clear();
    quarryUnloading_ = false;
    cancelSchematicLoad();
    stopClickSelection();
    player_.cancelDisconnectRequest();
    walkTicks_ = 0;
    jumpTicks_ = 0;
    testAction_.reset();
    manualLook_.reset();
    if (baritone_) baritone_->cancelEverything();
    releaseControl();
}

void Bot::releaseControl() {
    look_.clear();
    input_.clear();
    input_.setControlling(false);
    if (player_.valid()) player_.stopBreaking();
    controlling_ = false;
}

namespace {

bool gameWindowInForeground() {
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
}

}  // namespace

void Bot::updateFocusOverride(bool botControlling) {
    auto* client = sdk::ClientInstance::get();
    if (!client || !client->minecraftGame()) return;
    bool& grabbed = client->cursorGrabbedFlag();
    bool force = botControlling && baritone::Settings::get().allowUnfocusedMovement && !gameWindowInForeground();
    if (force) {
        if (!savedCursorGrabbed_) savedCursorGrabbed_ = grabbed;
        grabbed = true;
    } else if (savedCursorGrabbed_) {
        // back to the real state; if the window is in front again the game manages it
        if (!gameWindowInForeground()) grabbed = *savedCursorGrabbed_;
        savedCursorGrabbed_.reset();
    }
}

void Bot::startClickSelection() {
    cancelAll();
    clickSelection_.start();
    clickSelectionStartedAt_ = GetTickCount64();
    gamehooks::requestClick(0);
    gamehooks::autoUseWhileTargeting(0);
    chat::info("Click mode: aim with the crosshair. Left click: go there; right click: go above; hold left and aim elsewhere: select a box. Escape cancels.");
}

void Bot::stopClickSelection() {
    clickSelection_.cancel();
    clickCaptureStarted_ = false;
    clickTarget_.reset();
    gamehooks::captureSelectionClicks(false);
}

void Bot::updateClickSelection() {
    if (!clickSelection_.active()) return;
    auto* client = sdk::ClientInstance::get();
    auto world = platform::WorldTracker::get().snapshot();
    if (!client || !player_.valid() || !world ||
        (gameWindowInForeground() && (GetAsyncKeyState(VK_ESCAPE) & 0x8000))) {
        stopClickSelection();
        chat::info("Click mode cancelled");
        return;
    }
    // Wait until the command's chat screen closes. Do not consume clicks on
    // chat, menus or other apps; once active, opening a menu cancels the mode.
    const bool ready = client->cursorGrabbedFlag() && gameWindowInForeground();
    if (!clickCaptureStarted_) {
        if (!ready) {
            if (GetTickCount64() - clickSelectionStartedAt_ > 30000) {
                stopClickSelection();
                chat::info("Click mode cancelled");
            }
            return;
        }
        gamehooks::captureSelectionClicks(true);
        clickCaptureStarted_ = true;
    } else if (!ready) {
        stopClickSelection();
        chat::info("Click mode cancelled");
        return;
    }
    baritone::BlockStateInterface bsi(world);
    auto hit = baritone::rayTrace(bsi, player_.eyePosition(), player_.rotation(), 512);
    clickTarget_ = hit ? std::make_optional(hit->pos) : std::nullopt;
    while (auto event = gamehooks::pollSelectionClick()) {
        auto result = clickSelection_.event(event->button, event->pressed, clickTarget_);
        if (!result) continue;
        stopClickSelection();
        if (result->kind == baritone::ClickSelection::Result::Box) {
            baritone_->selections().clear();
            selectionPos1_.reset();
            baritone_->selections().add({result->first, result->second});
            chat::info(std::format("Selection made: {} to {}. Use #help sel for building commands.", result->first, result->second));
        } else if (result->kind == baritone::ClickSelection::Result::Goal) {
            auto goal = std::make_shared<baritone::GoalBlock>(result->first);
            baritone_->customGoal().setGoalAndPath(goal);
            chat::info("Goal: " + goal->toString());
        } else {
            chat::info("No block selected");
        }
        break;
    }
}

void Bot::walkFor(int ticks, bool sprint) {
    walkTicks_ = ticks;
    walkSprint_ = sprint;
}

void Bot::loadSchematicAsync(std::string file, baritone::BlockPos origin, bool preview) {
    if (schematicLoad_.running()) throw std::invalid_argument("A schematic is still loading; wait for it to finish or use #stop to discard it");
    if (!player_.valid()) throw std::invalid_argument("Enter a world first");
    cancelAll();
    loadingSchematicName_ = std::filesystem::path(file).filename().string();
    loadingSchematicFile_ = file;
    loadingPlacement_.reset();
    loadingSchematicOrigin_ = origin;
    loadingSchematicPreview_ = preview;
    loadingSchematicGeneration_ = worldGeneration_;
    lastLoadFailure_.clear();
    schematicLoad_.start(std::move(file));
    chat::info("Loading " + loadingSchematicName_ + " in the background. Wait for Loaded before editing or building; #stop discards the result");
}

void Bot::selectPlacement(const std::string& key) {
    prepareTasks();
    if (schematicLoading()) throw std::invalid_argument("Wait for the schematic load to finish");
    auto preset = placements_.entry(key).preset;
    cancelAll();
    if (placements_.select(key, schematics_)) return;
    loadSchematicAsync(preset.file, preset.origin, true);
    loadingPlacement_ = std::move(preset);
}

void Bot::runDiagnosticAsync(std::function<void()> task) {
    std::lock_guard lock(diagnosticMutex_);
    if (diagnosticsStopped_) throw std::invalid_argument("The bot is unloading");
    if (diagnosticJob_.valid() && diagnosticJob_.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        throw std::invalid_argument("A diagnostic calculation is already running; wait for its result");
    diagnosticJob_ = std::async(std::launch::async, [task = std::move(task)] {
        try { task(); }
        catch (const std::exception& error) { chat::error(std::string("Diagnostic calculation failed: ") + error.what()); }
    });
}

void Bot::startSchematicSurvey(bool materials) {
    if (schematicLoading()) throw std::invalid_argument("Wait for the schematic to finish loading");
    if (!player_.valid() || !schematics_.loaded()) throw std::invalid_argument("Enter a world and load a schematic first");
    survey_.start(schematics_.schematic(), schematics_.origin(), schematics_.revision());
    surveyMaterials_ = materials; surveyReport_ = true;
    chat::info("Comparing the placement with loaded blocks in the background across ticks");
}

std::filesystem::path Bot::worldDataPath(const std::string& category, bool perDimension) const {
    if (worldName_.empty() || !player_.valid()) throw std::invalid_argument("Enter a world first");
    uint64_t hash = 14695981039346656037ull;
    for (unsigned char c : worldName_) { hash ^= c; hash *= 1099511628211ull; }
    const auto dimension = platform::WorldTracker::get().dimension();
    return paths::dataDir() / category / (perDimension ? std::format("{:016x}_dim{}.txt", hash, dimension) : std::format("{:016x}.txt", hash));
}
void Bot::applyProtection(std::shared_ptr<const baritone::ProtectionPolicy> policy) {
    auto world = platform::WorldTracker::get().snapshot();
    if (!world || !player_.valid()) throw std::invalid_argument("Enter a world first");
    policy->save(worldDataPath("protection"));
    cancelAll();
    world->setProtection(std::move(policy));
}

void Bot::pollSchematicLoad() {
    auto result = schematicLoad_.poll();
    if (!result) return;
    if (perf::enabled()) perf::record(perf::Metric::BackgroundLoad, result->milliseconds);
    if (result->cancelled || loadingSchematicGeneration_ != worldGeneration_ || !player_.valid()) return;
    if (!result->error.empty()) {
        lastLoadFailure_ = result->error;
        chat::error(std::format("Could not read {}: {}", loadingSchematicName_, result->error));
        return;
    }
    const auto size = result->schematic->size();
    const auto warnings = result->schematic->warnings();
    try {
        if (loadingSchematicPreview_) {
            baritone::SchematicWorkspace next;
            next.load(loadingSchematicName_, std::move(result->schematic), loadingSchematicOrigin_, loadingSchematicFile_);
            if (loadingPlacement_) loadingPlacement_->apply(next);
            schematics_ = std::move(next);
        }
        else
            baritone_->builder().build(loadingSchematicName_, std::move(result->schematic), loadingSchematicOrigin_, true);
    } catch (const std::exception& error) {
        lastLoadFailure_ = error.what();
        chat::error(std::format("Could not place {}: {}", loadingSchematicName_, error.what()));
        return;
    }
    chat::info(std::format("Loaded {} ({}x{}x{}), origin {}", loadingSchematicName_, size.x, size.y, size.z, loadingSchematicOrigin_));
    for (const auto& warning : warnings) chat::error("Schematic note: " + warning);
    if (loadingSchematicPreview_) {
        chat::info("Preview: cyan = missing, red = wrong block, orange = remove, gray = unloaded. Matching blocks are hidden");
        chat::info("Preview shows up to 512 differences within 16 blocks; the blue outline covers the full placement");
        chat::info("Use #schematic move/rotate/mirror/layer, #schematic materials, then #schematic build");
    }
}

void Bot::onFrame() {
    perf::Scope timing(perf::Metric::BotFrame);
    watchdog::heartbeat();
    auto* client = sdk::ClientInstance::get();
    auto* player = client ? client->localPlayer() : nullptr;
    if (player != lastPlayer_) {
        ++worldGeneration_;
        cancelSchematicLoad();
        if (player) logger::info("Entered a world (LocalPlayer {})", static_cast<void*>(player));
        else logger::info("Left the world");
        devpipe::setStatus(player ? "connection=Loading world\n" : "connection=Connected; enter a world\n");
        lastPlayer_ = player;
        {
            player_.bind(nullptr, nullptr);  // first: the old LocalPlayer is gone
            player_.setLevel(nullptr);
            worldName_.clear();
            protectionKey_.clear();
            baritone_->survival().resetWorld();
            schematics_.clear();
            placements_.clear();
            placementsFile_.clear();
            schematicPreview_.clear();
            schematicDimension_.reset();
            platform::PathRenderer::get().clear();
            cancelAll();
            platform::WorldTracker::get().reset();
        }
    }
    while (auto line = devpipe::poll()) commands_.execute(*line);
    if (auto error = taskWriter_.poll()) chat::error("Task checkpoint error: " + *error);
    pollSchematicLoad();
    if (player) {
        updateClickSelection();
        chat::flush();
        updateFocusOverride(input_.controlling());
        if (player_.takeDisconnectRequest()) {
            // Never tear down a world from inside its level tick. This native
            // asynchronous entry point schedules the normal save/leave flow.
            cancelAll();
            logger::info("Disconnecting after arrival");
            client->requestLeaveGameAsync();
        }
    }
}

void Bot::onTick(void* level) {
    perf::Scope timing(perf::Metric::BotTick);
    ++ticks_;
    level_ = level;
    auto* client = sdk::ClientInstance::get();
    auto* player = client ? client->localPlayer() : nullptr;
    auto* region = client ? client->region() : nullptr;
    if (!player || !region) {
        player_.bind(nullptr, nullptr);
        return;
    }
    player_.bind(player, region);
    if (player_.inventoryScreenPending()) {
        // Pause movement/clicks while our temporary inventory screen is active.
        releaseControl();
        return;
    }
    look_.tick();  // new random look offsets (Java LookBehavior.onTick IN)
    player_.setLevel(static_cast<sdk::Level*>(level));
    if (worldName_.empty() && level) {
        // waypoints are stored per world
        std::string name = static_cast<sdk::Level*>(level)->name();
        worldName_ = name.empty() ? "world" : name;
        std::string file;
        for (char c : worldName_) file += (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_') ? c : '_';
        if (name.empty()) file = "monde";  // Preserve the existing cache/waypoint key for unnamed worlds.
        auto dir = paths::dataDir() / "waypoints";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        waypoints_.open((dir / (file + ".txt")).string());
        logger::info("World '{}': waypoints in {}", worldName_, waypoints_.path());
        platform::WorldTracker::get().setWorld(file);
    }

    if (!tickTasks_.empty()) {
        auto tasks = std::move(tickTasks_);
        tickTasks_.clear();
        for (auto& task : tasks) task();
    }

    bool dead = player->isDead();
    if (dead && !wasDead_) onDeath(*player);
    wasDead_ = dead;
    if (dead) return;  // death screen: nothing to drive
    bool sleeping = player->isSleeping();
    if (sleeping && !wasSleeping_) onSleep(*player);
    wasSleeping_ = sleeping;

    auto& tracker = platform::WorldTracker::get();
    { perf::Scope scanTiming(perf::Metric::WorldScan); tracker.tick(*region, *player); }
    sdk::BlockPos feet = sdk::BlockPos::floor(player->feet());
    { perf::Scope refreshTiming(perf::Metric::LocalRefresh); tracker.refreshAround({feet.x, feet.y, feet.z}, 4); }

    if (schematicDimension_ && *schematicDimension_ != tracker.dimension()) {
        baritone_->survival().resetWorld();
        ++worldGeneration_;
        cancelAll();
        schematics_.clear();
        placements_.clear();
        schematicPreview_.clear();
    }
    schematicDimension_ = tracker.dimension();
    const auto protectionFile = worldDataPath("protection");
    if (protectionKey_ != protectionFile.string()) {
        protectionKey_ = protectionFile.string();
        try { tracker.snapshot()->setProtection(std::make_shared<baritone::ProtectionPolicy>(baritone::ProtectionPolicy::load(protectionFile))); }
        catch (const std::exception& error) {
            cancelAll();
            // Fail closed: unreadable configured protections must never silently vanish.
            auto policy = std::make_shared<baritone::ProtectionPolicy>();
            policy->add({"invalid-protection-file", {{INT32_MIN, INT32_MIN, INT32_MIN}, {INT32_MAX, INT32_MAX, INT32_MAX}}, baritone::ProtectionPolicy::Both});
            tracker.snapshot()->setProtection(policy);
            chat::error(std::string("Protected zones could not be loaded; bot actions blocked: ") + error.what());
        }
    }

    if (surveyReport_) {
        if (!schematics_.loaded() || survey_.revision() != schematics_.revision()) {
            survey_.clear(); surveyReport_ = false;
            chat::info("Schematic comparison cancelled because the placement changed");
        } else if (survey_.step(*tracker.snapshot())) {
            surveyReport_ = false;
            const auto& result = survey_.result();
            chat::info(std::format("Verification: {} matching, {} missing, {} wrong, {} extra, {} unknown, {} ignored",
                result.matching, result.missing, result.wrong, result.extra, result.unknown, result.ignored));
            if (surveyMaterials_) {
                auto plan = baritone::planMaterials(result.items, player_, baritone::Settings::get().buildSubstitutes);
                for (const auto& row : plan)
                    chat::info(std::format("{}: need {}, inventory allocated {}, missing {}", row.item, row.required, row.available, row.missing));
                for (const auto& warning : result.warnings) chat::error("Material note: " + warning);
                if (result.unknown) chat::error("Unloaded positions are excluded from material requirements; load those chunks and verify again");
            }
        }
    }

    if (jumpTicks_ > 0) {
        --jumpTicks_;
        input_.set(Key::Jump, jumpTicks_ > 0);
        input_.setControlling(jumpTicks_ > 0);
        return;
    }

    if (walkTicks_ > 0) {
        --walkTicks_;
        input_.set(Key::MoveForward, walkTicks_ > 0);
        input_.set(Key::Sprint, walkSprint_ && walkTicks_ > 0);
        input_.setControlling(walkTicks_ > 0);
        if (walkTicks_ == 0) chat::info(std::format("Finished walking, position {}", player->feet()));
        return;
    }

    if (testAction_) {
        runTestAction();
        return;
    }

    if (!clickSelection_.active()) {
        tickTasks();
        tickQuarry();
        perf::Scope coreTiming(perf::Metric::CoreTick);
        baritone_->tick(tracker.snapshot());
    }
    if (ticks_ % 20 == 0) {
        auto* process = baritone_->control().mostRecentInControl();
        std::string status = std::format("connection=Connected\nversion={}\nworld={}\ndimension={}\nposition={}\nactivity={}\nqueue={}\nqueue_progress={}/{}\nallowBreak={}\nallowPlace={}\n",
            BARITONE_BEDROCK_VERSION, worldName_, tracker.dimension(), baritone_->playerFeet(),
            process ? process->displayName() : "Idle", tasks_.running() ? "Running" : "Paused", tasks_.currentIndex(), tasks_.tasks().size(),
            baritone::Settings::get().allowBreak ? "true" : "false", baritone::Settings::get().allowPlace ? "true" : "false");
        if (schematics_.loaded()) status += std::format("schematic={}\norigin={}\n", schematics_.name(), schematics_.origin());
        if (!baritone_->diagnostics().events().empty()) status += "latest=" + baritone_->diagnostics().events().back().message + "\n";
        devpipe::setStatus(std::move(status));
    }
    const baritone::MovementState& out = baritone_->controls();
    applyOutput(out, !clickSelection_.active() && (out.inputs.any() || out.target.rotation.has_value()));

    auto& pathing = baritone_->pathing();
    platform::PathRenderer::Frame frame;
    frame.path = pathing.currentPathPositions();
    frame.pathPosition = pathing.currentPathPosition();
    frame.next = pathing.nextPathPositions();
    frame.calculating = pathing.calculationInProgress();
    frame.mostRecent = pathing.mostRecentConsidered();
    auto sets = pathing.currentBlockSets();
    frame.toBreak = std::move(sets.toBreak);
    frame.toPlace = std::move(sets.toPlace);
    frame.toWalkInto = std::move(sets.toWalkInto);
    frame.player = player_.position();
    frame.goal = pathing.goal();
    frame.breaking = player_.breakingPos();
    frame.selPos1 = selectionPos1_;
    frame.clickTarget = clickTarget_;
    frame.clickStart = clickSelection_.dragStart();
    frame.selections = selectionBoxes();
    frame.build = baritone_->builder().bounds();
    if (schematics_.loaded() || !placements_.entries().empty()) {
        if (ticks_ % 10 == 0 || schematicPreviewRevision_ != schematics_.revision()) {
            perf::Scope previewTiming(perf::Metric::Preview);
            schematicPreview_ = placements_.preview(*tracker.snapshot(), {feet.x, feet.y, feet.z}, schematics_);
            if (schematicPreviewRevision_ != schematics_.revision())
                logger::debug("Schematic preview: {} difference boxes, origin {}", schematicPreview_.size(), schematics_.origin());
            schematicPreviewRevision_ = schematics_.revision();
        }
        if (schematics_.loaded() && schematics_.previewEnabled()) frame.schematicBounds = schematics_.bounds();
        frame.schematicPreview = schematicPreview_;
    } else {
        schematicPreview_.clear();
    }
    frame.elytraPath = baritone_->elytra().path();
    frame.elytraAim = baritone_->elytra().aim();
    frame.elytraLanding = baritone_->elytra().landing();
    frame.elytraSimulation = baritone_->elytra().simulation();
    frame.elytraRaytraces = baritone_->elytra().raytraces();
    frame.elytraHitboxSamples = baritone_->elytra().hitboxSamples();
    platform::PathRenderer::get().update(std::move(frame));
}

// Baritone's MemoryBehavior.onPlayerDeath: a "death" waypoint where the player died.
void Bot::onDeath(sdk::LocalPlayer& player) {
    cancelAll();
    sdk::Vec3 f = player.feet();
    baritone::BlockPos pos{static_cast<int>(std::floor(f.x)), static_cast<int>(std::floor(f.y + 0.1251)),
                           static_cast<int>(std::floor(f.z))};
    logger::info("Player died at {}", pos);
    if (!baritone::Settings::get().doDeathWaypoints) return;
    baritone::Waypoint wp;
    wp.tag = "death";
    wp.name = "death";
    wp.pos = pos;
    wp.dimension = platform::WorldTracker::get().dimension();
    wp.createdAt = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    waypoints_.add(wp);
    chat::info(std::format("Died at {} {} {}: 'death' waypoint saved (#wp goto death)", pos.x, pos.y, pos.z));
}

std::vector<std::pair<baritone::BlockPos, baritone::BlockPos>> Bot::selectionBoxes() const {
    std::vector<std::pair<baritone::BlockPos, baritone::BlockPos>> out;
    for (const auto& sel : baritone_->selections().all()) out.emplace_back(sel.pos1, sel.pos2);
    return out;
}

// Baritone's MemoryBehavior: a "bed" waypoint on the bed the player uses.
void Bot::onSleep(sdk::LocalPlayer& player) {
    if (!baritone::Settings::get().doBedWaypoints) return;
    sdk::Vec3 f = player.feet();
    baritone::BlockPos feet{static_cast<int>(std::floor(f.x)), static_cast<int>(std::floor(f.y)), static_cast<int>(std::floor(f.z))};
    baritone::BlockPos bed = feet;
    if (auto snap = platform::WorldTracker::get().snapshot()) {
        baritone::BlockStateInterface bsi(snap);
        int best = INT32_MAX;
        for (int dx = -2; dx <= 2; ++dx)
            for (int dy = -1; dy <= 1; ++dy)
                for (int dz = -2; dz <= 2; ++dz) {
                    baritone::BlockPos p = feet.offset(dx, dy, dz);
                    if (bsi.props(p.x, p.y, p.z).name != "minecraft:bed") continue;
                    int d = dx * dx + dy * dy + dz * dz;
                    if (d < best) {
                        best = d;
                        bed = p;
                    }
                }
    }
    baritone::Waypoint wp;
    wp.tag = "bed";
    wp.name = "bed";
    wp.pos = bed;
    wp.dimension = platform::WorldTracker::get().dimension();
    wp.createdAt = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    waypoints_.add(wp);
    chat::info(std::format("Bed at {} {} {}: 'bed' waypoint saved", bed.x, bed.y, bed.z));
}

void Bot::startTestAction(TestAction::Kind kind, const baritone::BlockPos& pos) {
    baritone_->cancelEverything();
    testAction_ = TestAction{kind, pos, 0};
}

// #break / #place: one block action through the same helpers the movements use.
void Bot::runTestAction() {
    using namespace baritone;
    TestAction& t = *testAction_;
    auto snapshot = platform::WorldTracker::get().snapshot();
    platform::WorldTracker::get().refreshAround(t.pos, 1);
    CalculationContext calc(snapshot, baritone_->inventory().abilities(), false);
    ExecContext ctx{player_, calc, baritone_->inventory(), {}};
    MovementState state;
    bool done = false;
    if (t.kind == TestAction::Break) {
        if (ctx.canWalkThrough(t.pos)) {
            done = true;
            chat::info(std::format("Block {} broken in {} ticks", t.pos, t.ticks));
        } else {
            movement_helper::switchToBestToolFor(ctx, ctx.props(t.pos));
            if (auto reach = movement_helper::reachable(ctx, t.pos)) {
                state.setTarget({*reach, true});
                if (ctx.isLookingAt(t.pos) || ctx.playerRotations().isReallyCloseTo(*reach)) {
                    state.setInput(Input::ClickLeft, true);
                }
            } else if (t.ticks == 0) {
                chat::error("Block out of reach");
                done = true;
            }
        }
    } else {
        if (!ctx.props(t.pos).replaceable && ctx.props(t.pos).type != BlockType::Air) {
            done = true;
            chat::info(std::format("Block placed at {}: {} ({} ticks)", t.pos, ctx.props(t.pos).name, t.ticks));
        } else {
            auto r = movement_helper::attemptToPlaceABlock(ctx, state, t.pos, false, false);
            if (r == movement_helper::PlaceResult::ReadyToPlace) state.setInput(Input::ClickRight, true);
            if (r == movement_helper::PlaceResult::NoOption && t.ticks == 0) {
                chat::error("Cannot place here (no block available or no visible face)");
                done = true;
            }
        }
    }
    if (++t.ticks > 400 && !done) {
        chat::error("Test abandoned after 400 ticks");
        done = true;
    }
    if (done) {
        testAction_.reset();
        releaseControl();
        return;
    }
    applyOutput(state, true);
}

void Bot::applyOutput(const baritone::MovementState& out, bool active) {
    // A process may request an inventory move during this very tick.
    if (!active || player_.containerOpen() || player_.inventoryScreenPending()) {
        if (controlling_) releaseControl();
        return;
    }
    controlling_ = true;
    input_.setControlling(true);
    input_.set(Key::MoveForward, out.input(BInput::MoveForward));
    input_.set(Key::MoveBack, out.input(BInput::MoveBack));
    input_.set(Key::MoveLeft, out.input(BInput::MoveLeft));
    input_.set(Key::MoveRight, out.input(BInput::MoveRight));
    input_.set(Key::Jump, out.input(BInput::Jump));
    input_.set(Key::Sneak, out.input(BInput::Sneak));
    input_.set(Key::Sprint, out.input(BInput::Sprint));
    // Creative flight (a double jump while pillaring or parkouring) breaks the movements:
    // like Baritone, stop flying while the bot walks; here with a double tap on jump.
    if (player_.isFlying() && !player_.isGliding()) {
        int phase = flightStopTicks_++ % 8;
        for (Key k : {Key::MoveForward, Key::MoveBack, Key::MoveLeft, Key::MoveRight, Key::Sprint}) input_.set(k, false);
        input_.set(Key::Jump, phase == 0 || phase == 2);
        if (flightStopTicks_ == 1) logger::info("Creative flight detected: stopping it");
    } else {
        flightStopTicks_ = 0;
    }
    if (out.target.rotation) {
        // forceRotations is Java's "blockInteract" for LookBehavior's mode
        baritone::Rotation wanted{out.target.rotation->yaw, std::clamp(out.target.rotation->pitch, -90.f, 90.f)};
        look_.updateTarget(wanted, out.target.forceRotations, player_.isGliding(), player_.rotation());
    }

    // Clicks act on what the player is looking at, like Baritone's BlockBreakHelper /
    // BlockPlaceHelper (with the rotation applied last frame).
    auto snapshot = platform::WorldTracker::get().snapshot();
    baritone::BlockStateInterface bsi(snapshot);
    auto hit = baritone::rayTrace(bsi, player_.eyePosition(), player_.rotation(), player_.blockReachDistance());
    if (baritone::Settings::get().logMovement) {
        static constexpr const char* names[] = {"F", "B", "L", "R", "CL", "CR", "J", "S", "Sp"};
        std::string inputs;
        for (int i = 0; i < static_cast<int>(BInput::Count); ++i)
            if (out.inputs.test(i)) inputs += std::string(names[i]) + " ";
        auto pos = player_.position();
        auto vel = player_.velocity();
        auto rot = player_.rotation();
        logger::debug("[{}] {} | in: {}| tgt {} | rot {:.1f},{:.1f} pos {:.2f},{:.2f},{:.2f} vel {:.2f},{:.2f},{:.2f} ground {} sneak {} hit {} held {} falling {} breakDelay {}",
                      ticks_, baritone_->pathing().debugState(), inputs,
                      out.target.rotation ? std::format("{:.1f},{:.1f}{}", out.target.rotation->yaw,
                                                        out.target.rotation->pitch, out.target.forceRotations ? "!" : "")
                                          : "-",
                      rot.yaw, rot.pitch, pos.x, pos.y, pos.z, vel.x, vel.y, vel.z, player_.onGround(),
                      player_.isSneaking(), hit ? std::format("{} f{}", hit->pos, static_cast<int>(hit->face)) : "-",
                      player_.hotbar(player_.selectedSlot()).name,
                      [this] {
                          std::string out;
                          for (const auto& e : player_.entities())
                              if (e.type == "minecraft:falling_block") out += std::format("{:.2f},{:.2f},{:.2f};", e.position.x, e.position.y, e.position.z);
                          return out.empty() ? std::string("-") : out;
                      }(),
                      breakDelay_);
    }
    if (player_.slotChangePending()) {
        player_.stopBreaking();
        return;  // the server doesn't know about the new held item yet
    }
    // Java BlockBreakHelper: after a block that took several ticks, wait blockBreakSpeed ticks
    // (minus the base delay of 1) before hitting the next one
    if (breakDelay_ > 0) {
        --breakDelay_;
    } else if (out.input(BInput::ClickLeft) && hit) {
        if (player_.startOrContinueBreaking(hit->pos, hit->face)) breakDelay_ = baritone::Settings::get().blockBreakSpeed - 1;
    } else {
        player_.stopBreaking();
    }
    if (rightClickTimer_ > 0) rightClickTimer_--;
    if (out.input(BInput::ClickRight) && !out.input(BInput::ClickLeft) && hit && rightClickTimer_ == 0) {
        player_.useItemOn(hit->pos, hit->face, hit->hit);
        rightClickTimer_ = baritone::Settings::get().rightClickSpeed;
    }
}

void Bot::beforeInputTick(sdk::MoveInputComponent& input) {
    input_.beforeInputTick(input);
}

void Bot::afterInputTick(sdk::MoveInputComponent& input) {
    input_.afterInputTick(input);
}

bool Bot::onChatSend(const std::string& message) {
    return commands_.onChat(message);
}
