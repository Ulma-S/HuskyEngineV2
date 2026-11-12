// このゲームのアクター(跳ね回るもの、プレイヤー)。ゲームのコードの一部(src/game。Game.h を参照)。
// 動作スレッド: ゲームスレッドのみ
#pragma once

#include "engine/Actor.h"

namespace husky::game
{
    constexpr float BouncerSize = 32.0f; // 跳ね回るアクターの当たりと見た目の大きさ(テクスチャと同じ。拡大なし)
    constexpr float PlayerSize = 128.0f; // プレイヤーの見た目の大きさ(4 倍に拡大)

    // 画面の中を跳ね回るアクター。見た目は SpriteComponent で足す。
    class BouncerActor : public engine::Actor
    {
        HUSKY_OBJECT(BouncerActor, engine::Actor)
    public:
        float vx = 0.0f; // 速度(ピクセル/秒)
        float vy = 0.0f;

        // Scene::Tick から毎フレーム呼ばれる。
        // override: 親(Actor)の virtual 関数を上書きしていることを、コンパイラに確かめさせる(C# の override と同じ)。
        // 跳ね返る位置は、見た目(SpriteComponent)の大きさではなく、当たりの大きさ(BouncerSize)で決める。
        // 動きは見た目に頼らない(見た目のコンポーネントを外しても、同じように動く)。
        void Tick(double dt) override;

        HUSKY_REFLECTION_BEGIN()
            HUSKY_PROPERTY(vx)
            HUSKY_PROPERTY(vy)
        HUSKY_REFLECTION_END()
    };

    // 矢印キー(または WASD)で動くプレイヤーのアクター。見た目は SpriteComponent で足す。
    class PlayerActor : public engine::Actor
    {
        HUSKY_OBJECT(PlayerActor, engine::Actor)
    public:
        float moveSpeed = 300.0f; // 移動の速さ(ピクセル/秒)

        // 入力は、自分のいるシーンから引く(配られるのを待たない)。
        // シーンにいない、またはシーンに入力が渡されていなければ、動かない。
        void Tick(double dt) override;

        HUSKY_REFLECTION_BEGIN()
            HUSKY_PROPERTY(moveSpeed)
        HUSKY_REFLECTION_END()
    };
}
