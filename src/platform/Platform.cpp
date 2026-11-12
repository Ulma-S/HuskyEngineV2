// Platform.h の実装。ここでは SDL を直接呼んでよい。
#include "platform/Platform.h"

#include "platform/MemoryHooks.h"

#include <SDL3/SDL.h>

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
            default:                  return Key::Unknown;
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

    bool Initialize()
    {
        // SDL のメモリの確保が MemoryTracker を通っていなければ、知らせる(ヒープの集計から SDL の分が抜けるため)。
        // 差し替えは、ここでは遅すぎる(SDL はもう確保しているかもしれない)ので、main の最初で行う(MemoryHooks.h)。
        if (!AreMemoryHooksInstalled())
        {
            SDL_Log("注意: InstallMemoryHooks が呼ばれていません。SDL のメモリは集計に入りません");
        }

        // SDL の映像(ウィンドウ)まわりを初期化する。イベント処理も一緒に初期化される。
        // SDL3 では成功すると true が返る。
        if (!SDL_Init(SDL_INIT_VIDEO))
        {
            // 例外は使わないので、失敗は戻り値で伝える。理由は SDL_GetError() で取れる。
            SDL_Log("SDL_Init に失敗しました: %s", SDL_GetError());
            return false;
        }
        return true;
    }

    void Shutdown()
    {
        SDL_Quit();
    }

    bool ProcessEvents(std::vector<KeyEvent>& outKeyEvents)
    {
        // 前のフレームの分を消す。clear() は中身を空にするだけで、確保したメモリは残すので、
        // 毎フレーム同じ vector を使い回せば、メモリの確保がほとんど起きない。
        outKeyEvents.clear();

        bool quitRequested = false;

        // SDL_PollEvent は、溜まっているイベントを1つ取り出して true を返す。
        // 溜まっていなければ待たずに false を返すので、空になるまで繰り返す。
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            // 開発ビルドだけ: まず開発用の UI に渡す。UI がキーボードを使っているなら、キーを押した入力はゲームに渡さない。
            bool uiWantsKeyboard = false;
#if HUSKY_DEVELOPMENT
            if (g_nativeEventHook != nullptr)
            {
                uiWantsKeyboard = g_nativeEventHook(&event);
            }
#endif
            if (uiWantsKeyboard && event.type == SDL_EVENT_KEY_DOWN)
            {
                continue;
            }

            switch (event.type)
            {
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
                outKeyEvents.push_back(keyEvent); // push_back は C# の List.Add にあたる
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
