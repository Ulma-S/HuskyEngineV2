// アクター: シーンに置かれる、ゲーム上の意味を持つもの(敵、プレイヤー、弾など)の基底クラス。
// 名前と位置だけを持つ薄い入れ物。機能(見た目、体力、AI など)は、後でコンポーネントとして足す。
#pragma once

#include "core/Object.h"

#include <string>

namespace husky::engine
{
    // 動作スレッド: ゲームスレッドのみ
    class Actor : public core::Object
    {
        HUSKY_OBJECT(Actor, core::Object)
    public:
        // 毎フレーム、シーンの更新(Scene::Tick)から呼ばれる。dt はこのフレームの経過時間(秒)。
        // 子クラスで override して、動きを書く。Destroy 済みのアクターでは呼ばれない。
        virtual void Tick(double dt) { (void)dt; }

        std::string name;
        float x = 0.0f; // 位置(ピクセル、左上が原点)
        float y = 0.0f;

        HUSKY_REFLECTION_BEGIN()
            HUSKY_PROPERTY(name)
            HUSKY_PROPERTY(x)
            HUSKY_PROPERTY(y)
        HUSKY_REFLECTION_END()
    };
}
