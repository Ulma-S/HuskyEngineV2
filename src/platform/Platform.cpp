// Platform.h の実装。ここでは SDL を直接呼んでよい。
#include "platform/Platform.h"
#include "platform/PlatformLog.h"

#include "platform/MemoryHooks.h"

#include <SDL3/SDL.h>

#ifdef _WIN32
// Windows の API(コンソールの文字コードの設定)。使ってよいのは platform の中だけ。
// WIN32_LEAN_AND_MEAN: あまり使わない部分を読み込まない。NOMINMAX: min / max というマクロを作らせない(std::min と衝突するため)。
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace husky::platform
{
    // 無名 namespace: この中のものは、この .cpp の中からしか見えない(C# の private static に近い)。
    namespace
    {
        // SDL のスキャンコード(キーの物理的な位置)を、エンジンの Key に変換する。
        Key ToKey(SDL_Scancode scancode)
        {
            switch (scancode)
            {
            case SDL_SCANCODE_W:      return Key::W;
            case SDL_SCANCODE_A:      return Key::A;
            case SDL_SCANCODE_S:      return Key::S;
            case SDL_SCANCODE_D:      return Key::D;
            case SDL_SCANCODE_UP:     return Key::Up;
            case SDL_SCANCODE_DOWN:   return Key::Down;
            case SDL_SCANCODE_LEFT:   return Key::Left;
            case SDL_SCANCODE_RIGHT:  return Key::Right;
            case SDL_SCANCODE_SPACE:  return Key::Space;
            case SDL_SCANCODE_RETURN: return Key::Enter;
            case SDL_SCANCODE_ESCAPE: return Key::Escape;
            case SDL_SCANCODE_F5:     return Key::F5;
            case SDL_SCANCODE_Z:      return Key::Z;
            case SDL_SCANCODE_Y:      return Key::Y;
            case SDL_SCANCODE_LCTRL:  return Key::LeftCtrl;
            case SDL_SCANCODE_RCTRL:  return Key::RightCtrl;
            default:                  return Key::Unknown;
            }
        }

        // SDL のマウスのボタンの番号を、エンジンの MouseButton に変換する。対応しないボタン(横のボタンなど)なら false。
        bool ToMouseButton(Uint8 sdlButton, MouseButton& outButton)
        {
            switch (sdlButton)
            {
            case SDL_BUTTON_LEFT:   outButton = MouseButton::Left;   return true;
            case SDL_BUTTON_RIGHT:  outButton = MouseButton::Right;  return true;
            case SDL_BUTTON_MIDDLE: outButton = MouseButton::Middle; return true;
            default:                return false;
            }
        }

#if HUSKY_DEVELOPMENT
        // 開発用の UI にイベントを渡す関数(描画層が SetNativeEventHook で登録する)。
        NativeEventHook g_nativeEventHook = nullptr;
#endif
    }

#if HUSKY_DEVELOPMENT
    void SetNativeEventHook(NativeEventHook hook)
    {
        g_nativeEventHook = hook;
    }
#endif

    namespace
    {
        // SDL のログの受け口(SDL_SetLogOutputFunction に渡す)。SDL の重要度をエンジンのレベルに読み替える。
        void SDLCALL ForwardSdlLog(void* /*userdata*/, int /*category*/, SDL_LogPriority priority, const char* message)
        {
            core::LogLevel level = core::LogLevel::Info;
            if (priority >= SDL_LOG_PRIORITY_ERROR)
                level = core::LogLevel::Error;
            else if (priority == SDL_LOG_PRIORITY_WARN)
                level = core::LogLevel::Warning;
            else if (priority <= SDL_LOG_PRIORITY_DEBUG)
                level = core::LogLevel::Verbose;
            static const int channel = core::Log::Get().RegisterChannel("Engine.Platform.SDL");
            core::Log::Get().WriteText(channel, level, message);
        }
    }

    void ForwardSdlLogToEngine()
    {
        SDL_SetLogOutputFunction(ForwardSdlLog, nullptr);
    }

    bool Initialize()
    {
        ForwardSdlLogToEngine();

        // SDL のメモリの確保が MemoryTracker を通っていなければ、知らせる(ヒープの集計から SDL の分が抜けるため)。
        // 差し替えは、ここでは遅すぎる(SDL はもう確保しているかもしれない)ので、main の最初で行う(MemoryHooks.h)。
        if (!AreMemoryHooksInstalled())
        {
            HUSKY_LOG(PlatformLog::MemoryHooksMissing);
        }

        // SDL の映像(ウィンドウ)まわりを初期化する。イベント処理も一緒に初期化される。
        // SDL3 では成功すると true が返る。
        if (!SDL_Init(SDL_INIT_VIDEO))
        {
            // 例外は使わないので、失敗は戻り値で伝える。理由は SDL_GetError() で取れる。
            HUSKY_LOG(PlatformLog::SdlCallFailed, "SDL_Init", SDL_GetError());
            return false;
        }
        return true;
    }

    void Shutdown()
    {
        SDL_Quit();
    }

    void EnableUtf8ConsoleOutput()
    {
#ifdef _WIN32
        SetConsoleOutputCP(CP_UTF8); // コンソールが無い(ファイルへ書き出している)ときは、失敗しても困らない
#endif
    }

    bool ProcessEvents(InputEvents& outEvents)
    {
        // 前のフレームの分を消す。clear() は中身を空にするだけで、確保したメモリは残すので、
        // 毎フレーム同じ入れ物を使い回せば、メモリの確保がほとんど起きない。
        outEvents.Clear();

        bool quitRequested = false;

        // SDL_PollEvent は、溜まっているイベントを1つ取り出して true を返す。
        // 溜まっていなければ待たずに false を返すので、空になるまで繰り返す。
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            // 開発ビルドだけ: まず開発用の UI に渡す。UI が使っているなら、押した入力はゲームに渡さない
            // (キーボードならキーを押した入力、マウスならボタンを押した入力とホイール)。
            // 離した入力とマウスの動きは渡す(ゲームの中で押したままの状態が残らないように、位置が古くならないように)。
            NativeEventCapture capture;
#if HUSKY_DEVELOPMENT
            if (g_nativeEventHook != nullptr)
            {
                capture = g_nativeEventHook(&event);
            }
#endif
            if (capture.keyboard && event.type == SDL_EVENT_KEY_DOWN)
            {
                continue;
            }
            if (capture.mouse && (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_WHEEL))
            {
                continue;
            }

            switch (event.type)
            {
            case SDL_EVENT_MOUSE_MOTION:
            {
                MouseEvent mouseEvent;
                mouseEvent.type = MouseEventType::Move;
                mouseEvent.x = event.motion.x;
                mouseEvent.y = event.motion.y;
                outEvents.mouse.push_back(mouseEvent);
                break;
            }

            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP:
            {
                MouseEvent mouseEvent;
                if (!ToMouseButton(event.button.button, mouseEvent.button))
                {
                    break;
                }
                mouseEvent.type = (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) ? MouseEventType::ButtonDown : MouseEventType::ButtonUp;
                mouseEvent.x = event.button.x;
                mouseEvent.y = event.button.y;
                outEvents.mouse.push_back(mouseEvent);
                break;
            }

            case SDL_EVENT_MOUSE_WHEEL:
            {
                MouseEvent mouseEvent;
                mouseEvent.type = MouseEventType::Wheel;
                mouseEvent.x = event.wheel.mouse_x;
                mouseEvent.y = event.wheel.mouse_y;
                // OS の設定で向きが逆(「ナチュラル」なスクロール)なら、SDL は FLIPPED を付けて送ってくる。
                // ゲームからはいつも「奥へ回すと +」に見えるようにそろえる。
                mouseEvent.wheel = (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) ? -event.wheel.y : event.wheel.y;
                outEvents.mouse.push_back(mouseEvent);
                break;
            }
            case SDL_EVENT_QUIT:
                // 最後のウィンドウが閉じられたときや、OS から終了を求められたときに届く。
                quitRequested = true;
                break;

            case SDL_EVENT_KEY_DOWN:
            case SDL_EVENT_KEY_UP:
            {
                // キーを押しっぱなしにすると、OS が「押された」を繰り返し送ってくる(キーリピート)。
                // ゲームでは「押した瞬間」を知りたいので、繰り返し分は捨てる。
                if (event.key.repeat)
                {
                    break;
                }
                const Key key = ToKey(event.key.scancode);
                if (key == Key::Unknown)
                {
                    break;
                }
                KeyEvent keyEvent;
                keyEvent.key = key;
                keyEvent.isDown = (event.type == SDL_EVENT_KEY_DOWN);
                outEvents.keys.push_back(keyEvent); // push_back は C# の List.Add にあたる
                break;
            }

            default:
                break;
            }
        }
        return quitRequested;
    }

    std::uint64_t GetTimeNanoseconds()
    {
        // SDL_Init からの経過時間(ナノ秒)。中では OS の高精度タイマーを使っている。
        return SDL_GetTicksNS();
    }
}
