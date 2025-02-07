#include "DemoApp.h"

#if !defined(__ANDROID__)
#include <SDL2/SDL.h>
#include <SDL2/SDL_keyboard.h>
#include <SDL2/SDL_video.h>
#endif

#include <Sys/Time_.h>
#include <SW/SW.h>

#include <SDL2/SDL_events.h>

#include <cassert>
#include <cfloat>

#ifdef _WIN32
#include <renderdoc/renderdoc_app.h>
#endif

#include <DemoLib/eng/GameBase.h>
#include <DemoLib/Viewer.h>

#pragma warning(disable : 4996)

namespace {
DemoApp *g_app = nullptr;
#ifdef _WIN32
RENDERDOC_API_1_1_2 *rdoc_api = NULL;
#endif
} // namespace

extern "C" {
// Enable High Performance Graphics while using Integrated Graphics
DLL_EXPORT int32_t NvOptimusEnablement = 0x00000001;     // Nvidia
DLL_EXPORT int AmdPowerXpressRequestHighPerformance = 1; // AMD

#ifdef _WIN32
DLL_IMPORT int __stdcall SetProcessDPIAware();
#endif
}

#ifdef _WIN32
namespace Ray {
namespace Vk {
extern RENDERDOC_DevicePointer rdoc_device;
} // namespace Vk
} // namespace Ray
#endif

DemoApp::DemoApp() : quit_(false) { g_app = this; }

DemoApp::~DemoApp() = default;

int DemoApp::Init(int w, int h, const AppParams &app_params, bool nogpu, bool nohwrt, bool nobindless,
                  bool nocompression) {
#if !defined(__ANDROID__)
#ifdef _WIN32
    int dpi_result = SetProcessDPIAware();
    (void)dpi_result;
#endif

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        const char *s = SDL_GetError();
        fprintf(stderr, "%s\n", s);
        return -1;
    }

    window_ = SDL_CreateWindow("View", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, w, h,
                               SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!window_) {
        const char *s = SDL_GetError();
        fprintf(stderr, "%s\n", s);
        return -1;
    }

    renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer_) {
        const char *s = SDL_GetError();
        fprintf(stderr, "%s\n", s);
        return -1;
    }
    texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, w, h);
    if (!texture_) {
        const char *s = SDL_GetError();
        fprintf(stderr, "%s\n", s);
        return -1;
    }
#endif

    char envarg[] = "MVK_CONFIG_FULL_IMAGE_VIEW_SWIZZLE=1";
    putenv(envarg);

#if !defined(NDEBUG) && defined(_WIN32)
    _controlfp(_EM_INEXACT | _EM_UNDERFLOW | _EM_OVERFLOW, _MCW_EM);
#endif

    try {
        CreateViewer(w, h, app_params, nogpu, nohwrt, nobindless, nocompression);
    } catch (std::exception &e) {
        fprintf(stderr, "%s", e.what());
        return -1;
    }

    return 0;
}

void DemoApp::Destroy() {
    viewer_.reset();

#if !defined(__ANDROID__)
    SDL_DestroyTexture(texture_);
    SDL_DestroyRenderer(renderer_);
    SDL_DestroyWindow(window_);
    SDL_Quit();
#endif
}

void DemoApp::Frame() {
#ifdef _WIN32
    if (capture_frame_ && rdoc_api) {
        rdoc_api->StartFrameCapture(Ray::Vk::rdoc_device, NULL);
    }
#endif
    viewer_->Frame();
#ifdef _WIN32
    if (capture_frame_ && rdoc_api) {
        const uint32_t ret = rdoc_api->EndFrameCapture(Ray::Vk::rdoc_device, NULL);
        assert(ret == 1);
    }
#endif
    capture_frame_ = false;
}

#if !defined(__ANDROID__)
int DemoApp::Run(int argc, char *argv[]) {
    int w = 640, h = 360;

    AppParams app_params;
    app_params.scene_name = "assets/scenes/mat_test.json";

    bool nogpu = false;
    bool nohwrt = false;
    bool nobindless = false;
    bool nocompression = false;

    for (size_t i = 1; i < argc; i++) {
        if ((strcmp(argv[i], "--width") == 0 || strcmp(argv[i], "-w") == 0) && (++i != argc)) {
            w = int(strtol(argv[i], nullptr, 10));
        } else if ((strcmp(argv[i], "--height") == 0 || strcmp(argv[i], "-h") == 0) && (++i != argc)) {
            h = int(strtol(argv[i], nullptr, 10));
        } else if ((strcmp(argv[i], "--scene") == 0 || strcmp(argv[i], "-s") == 0) && (++i != argc)) {
            app_params.scene_name = argv[i];
        } else if ((strcmp(argv[i], "--reference") == 0 || strcmp(argv[i], "-ref") == 0) && (++i != argc)) {
            app_params.ref_name = argv[i];
        } else if (strcmp(argv[i], "--nogpu") == 0) {
            nogpu = true;
        } else if (strcmp(argv[i], "--nohwrt") == 0) {
            nohwrt = true;
        } else if (strcmp(argv[i], "--nobindless") == 0) {
            nobindless = true;
        } else if (strcmp(argv[i], "--nocompression") == 0) {
            nocompression = true;
        } else if (strcmp(argv[i], "--samples") == 0 && (++i != argc)) {
            app_params.samples = int(strtol(argv[i], nullptr, 10));
        } else if (strcmp(argv[i], "--psnr") == 0 && (++i != argc)) {
            app_params.psnr = strtod(argv[i], nullptr);
        } else if (strcmp(argv[i], "--threshold") == 0 && (++i != argc)) {
            app_params.threshold = int(strtol(argv[i], nullptr, 10));
        } else if (strcmp(argv[i], "--diff_depth") == 0 && (++i != argc)) {
            app_params.diff_depth = int(strtol(argv[i], nullptr, 10));
        } else if (strcmp(argv[i], "--spec_depth") == 0 && (++i != argc)) {
            app_params.spec_depth = int(strtol(argv[i], nullptr, 10));
        } else if (strcmp(argv[i], "--refr_depth") == 0 && (++i != argc)) {
            app_params.refr_depth = int(strtol(argv[i], nullptr, 10));
        } else if (strcmp(argv[i], "--transp_depth") == 0 && (++i != argc)) {
            app_params.transp_depth = int(strtol(argv[i], nullptr, 10));
        } else if (strcmp(argv[i], "--total_depth") == 0 && (++i != argc)) {
            app_params.total_depth = int(strtol(argv[i], nullptr, 10));
        } else if ((strcmp(argv[i], "--device") == 0 || strcmp(argv[i], "-d") == 0) && (++i != argc)) {
            app_params.device_name = argv[i];
        } else if (strcmp(argv[i], "--max_tex_res") == 0 && (++i != argc)) {
            app_params.max_tex_res = int(strtol(argv[i], nullptr, 10));
        } else if (strcmp(argv[i], "--output_exr") == 0) {
            app_params.output_exr = true;
        } else if (strcmp(argv[i], "--output_aux") == 0) {
            app_params.output_aux = true;
        } else if (strcmp(argv[i], "--denoise") == 0) {
            app_params.denoise_after = 1;
        } else if (strcmp(argv[i], "--denoise_after") == 0 && (++i != argc)) {
            app_params.denoise_after = int(strtol(argv[i], nullptr, 10));
        }
    }

    if (Init(w, h, app_params, nogpu, nohwrt, nobindless, nocompression) < 0) {
        return -1;
    }

#ifdef _WIN32
    Sys::DynLib rdoc{"renderdoc.dll"};
    if (rdoc) {
        pRENDERDOC_GetAPI RENDERDOC_GetAPI = (pRENDERDOC_GetAPI)rdoc.GetProcAddress("RENDERDOC_GetAPI");
        const int ret = RENDERDOC_GetAPI(eRENDERDOC_API_Version_1_1_2, (void **)&rdoc_api);
        assert(ret == 1);
    }
#endif

    while (!terminated() && !viewer_->terminated) {
        this->PollEvents();
        this->Frame();

        const void *_pixels = swGetPixelDataRef(swGetCurFramebuffer());
        SDL_UpdateTexture(texture_, NULL, _pixels, viewer_->width * sizeof(Uint32));

        // SDL_RenderClear(renderer_);
        SDL_RenderCopy(renderer_, texture_, NULL, NULL);
        SDL_RenderPresent(renderer_);
    }

    const int return_code = viewer_->return_code;

    this->Destroy();
#endif
    return return_code;
}

bool DemoApp::ConvertToRawButton(int32_t key, InputManager::RawInputButton &button) {
    switch (key) {
    case SDLK_UP:
        button = InputManager::RAW_INPUT_BUTTON_UP;
        break;
    case SDLK_DOWN:
        button = InputManager::RAW_INPUT_BUTTON_DOWN;
        break;
    case SDLK_LEFT:
        button = InputManager::RAW_INPUT_BUTTON_LEFT;
        break;
    case SDLK_RIGHT:
        button = InputManager::RAW_INPUT_BUTTON_RIGHT;
        break;
    case SDLK_ESCAPE:
        button = InputManager::RAW_INPUT_BUTTON_EXIT;
        break;
    case SDLK_TAB:
        button = InputManager::RAW_INPUT_BUTTON_TAB;
        break;
    case SDLK_BACKSPACE:
        button = InputManager::RAW_INPUT_BUTTON_BACKSPACE;
        break;
    case SDLK_LSHIFT:
    case SDLK_RSHIFT:
        button = InputManager::RAW_INPUT_BUTTON_SHIFT;
        break;
    case SDLK_DELETE:
        button = InputManager::RAW_INPUT_BUTTON_DELETE;
        break;
    case SDLK_SPACE:
        button = InputManager::RAW_INPUT_BUTTON_SPACE;
        break;
    default:
        button = InputManager::RAW_INPUT_BUTTON_OTHER;
        break;
    }
    return true;
}

void DemoApp::PollEvents() {
    SDL_Event e = {};
    InputManager::RawInputButton button;
    InputManager::Event evt = {};
    while (SDL_PollEvent(&e)) {
        evt.type = InputManager::RAW_INPUT_NONE;
        switch (e.type) {
        case SDL_KEYDOWN: {
            if (e.key.keysym.sym == SDLK_ESCAPE) {
                quit_ = true;
                return;
            } else if (e.key.keysym.sym == SDLK_F12) {
                capture_frame_ = true;
                return;
            } /*else if (e.key.keysym.sym == SDLK_TAB) {
            bool is_fullscreen = bool(SDL_GetWindowFlags(window_) & SDL_WINDOW_FULLSCREEN);
            SDL_SetWindowFullscreen(window_, is_fullscreen ? 0 : SDL_WINDOW_FULLSCREEN);
            return;
        }*/ else if (ConvertToRawButton(e.key.keysym.sym, button)) {
                evt.type = InputManager::RAW_INPUT_KEY_DOWN;
                evt.key = button;
                evt.raw_key = e.key.keysym.sym;
            }
        } break;
        case SDL_KEYUP:
            if (ConvertToRawButton(e.key.keysym.sym, button)) {
                evt.type = InputManager::RAW_INPUT_KEY_UP;
                evt.key = button;
                evt.raw_key = e.key.keysym.sym;
            }
            break;
        case SDL_FINGERDOWN:
            evt.type = e.tfinger.fingerId == 0 ? InputManager::RAW_INPUT_P1_DOWN : InputManager::RAW_INPUT_P2_DOWN;
            evt.point.x = e.tfinger.x * float(viewer_->width);
            evt.point.y = e.tfinger.y * float(viewer_->height);
            break;
        case SDL_MOUSEBUTTONDOWN:
            evt.type = InputManager::RAW_INPUT_P1_DOWN;
            evt.point.x = float(e.motion.x);
            evt.point.y = float(e.motion.y);
            break;
        case SDL_FINGERUP:
            evt.type = e.tfinger.fingerId == 0 ? InputManager::RAW_INPUT_P1_UP : InputManager::RAW_INPUT_P2_UP;
            evt.point.x = e.tfinger.x * float(viewer_->width);
            evt.point.y = e.tfinger.y * float(viewer_->height);
            break;
        case SDL_MOUSEBUTTONUP:
            evt.type = InputManager::RAW_INPUT_P1_UP;
            evt.point.x = float(e.motion.x);
            evt.point.y = float(e.motion.y);
            break;
        case SDL_QUIT: {
            quit_ = true;
            return;
        }
        case SDL_FINGERMOTION:
            evt.type = e.tfinger.fingerId == 0 ? InputManager::RAW_INPUT_P1_MOVE : InputManager::RAW_INPUT_P2_MOVE;
            evt.point.x = e.tfinger.x * float(viewer_->width);
            evt.point.y = e.tfinger.y * float(viewer_->height);
            evt.move.dx = e.tfinger.dx * float(viewer_->width);
            evt.move.dy = e.tfinger.dy * float(viewer_->height);
            break;
        case SDL_MOUSEMOTION:
            evt.type = InputManager::RAW_INPUT_P1_MOVE;
            evt.point.x = float(e.motion.x);
            evt.point.y = float(e.motion.y);
            evt.move.dx = float(e.motion.xrel);
            evt.move.dy = float(e.motion.yrel);
            break;
        case SDL_MOUSEWHEEL:
            evt.type = InputManager::RAW_INPUT_MOUSE_WHEEL;
            evt.move.dx = float(e.wheel.x);
            evt.move.dy = float(e.wheel.y);
            break;
        case SDL_WINDOWEVENT:
            if (e.window.event == SDL_WINDOWEVENT_RESIZED || e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                evt.type = InputManager::RAW_INPUT_RESIZE;
                evt.point.x = float(e.window.data1);
                evt.point.y = float(e.window.data2);

                viewer_->Resize(e.window.data1, e.window.data2);

                SDL_RenderPresent(renderer_);
                SDL_DestroyTexture(texture_);
                texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                             e.window.data1, e.window.data2);
            }
            break;
        default:
            return;
        }
        if (evt.type != InputManager::RAW_INPUT_NONE) {
            evt.time_stamp = Sys::GetTimeMs() - (SDL_GetTicks() - e.common.timestamp);
            p_input_manager_->AddRawInputEvent(evt);
        }
    }
}

void DemoApp::CreateViewer(int w, int h, const AppParams &app_params, const bool nogpu, const bool nohwrt,
                           const bool nobindless, const bool nocompression) {
    if (viewer_) {
        w = viewer_->width;
        h = viewer_->height;
    }

    viewer_ = {};
    viewer_ = std::make_unique<Viewer>(w, h, "./", app_params, nogpu ? 0 : (nohwrt ? 1 : 2), nobindless, nocompression);
    p_input_manager_ = viewer_->GetComponent<InputManager>(INPUT_MANAGER_KEY);
}
