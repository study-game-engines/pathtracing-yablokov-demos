#pragma once

#include <atomic>

#include "../eng/GameState.h"
#include "../ren/Camera.h"
#include "../ren/Program.h"
#include "../ren/Texture.h"

class GameBase;
class GameStateManager;
class GCursor;
class FontStorage;
class Random;
class Renderer;

namespace Sys {
class ThreadPool;
}

namespace Gui {
class BaseElement;
class BitmapFont;
class Renderer;
}

class GSCPUTest : public GameState {
    GameBase *game_;
    std::weak_ptr<GameStateManager> state_manager_;
    std::shared_ptr<Ren::Context> ctx_;
    std::shared_ptr<Renderer> renderer_;

    std::shared_ptr<Random> random_;

    std::shared_ptr<Gui::Renderer> ui_renderer_;
    std::shared_ptr<Gui::BaseElement> ui_root_;
    std::shared_ptr<Gui::BitmapFont> font_;

    std::shared_ptr<Sys::ThreadPool> threads_;

    enum { eWarmup, eStarted, eFinished } state_ = eWarmup;

    std::atomic_bool warmup_done_ = { false };
    std::atomic_int num_ready_ = { 0 };
    int counter_ = 0;
public:
    explicit GSCPUTest(GameBase *game);
    ~GSCPUTest() override;

    void Enter() override;
    void Exit() override;

    void Draw(uint64_t dt_us) override;

    void Update(uint64_t dt_us) override;

    void HandleInput(const InputManager::Event &evt) override;
};