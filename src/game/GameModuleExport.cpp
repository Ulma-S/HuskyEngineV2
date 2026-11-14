// ゲームの DLL(HuskyGame.dll)の入口(HR-2)。DLL を作るときだけコンパイルする(テスト・CLI・製品の exe には入れない)。
// exe は "HuskyGetGameModule" という名前でこの関数を探して呼び、ゲームの関数の表を受け取る(engine/GameModule.h)。
#include "engine/GameModule.h"
#include "game/Game.h"

HUSKY_GAME_MODULE_EXPORT bool HuskyGetGameModule(husky::engine::GameModule* out)
{
    if (out == nullptr)
    {
        return false;
    }
    husky::game::FillGameModule(*out);
    return true;
}
