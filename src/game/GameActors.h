// このゲームのアクター(跳ね回るもの、プレイヤー)。ゲームのコードの一部(src/game。Game.h を参照)。
// 動作スレッド: ゲームスレッドのみ
#pragma once

#include "core/AssetRef.h"
#include "engine/Actor.h"
#include "engine/SoundAsset.h"
#include "game/BouncerSettings.h"
#include "render/RenderDevice.h" // render::Color

#include <cstdint>

namespace husky::game
{
    constexpr float BouncerSize = 32.0f; // 跳ね回るアクターの当たりと見た目の大きさ(テクスチャと同じ。拡大なし)
    constexpr float PlayerSize = 128.0f; // プレイヤーの見た目の大きさ(4 倍に拡大)

    // 当たり判定のグループ(COL-4)。プレハブのコライダーの layer / collisionMask と同じ番号にしておく。
    // 跳ね回るもの同士は当たらない(数が多いので、プレイヤーとだけ調べる)。
    constexpr int PlayerLayer = 0;  // プレイヤー(collisionMask は跳ね回るものと木箱 = (1 << 1) | (1 << 2))
    constexpr int BouncerLayer = 1; // 跳ね回るもの(collisionMask はプレイヤーだけ = 1 << 0)
    // 押せる木箱と、世界の端の壁(PHY-5)。木箱はプレイヤー・木箱・壁と当たる(collisionMask = 1 + 4 + 8 = 13)、壁は木箱とだけ
    constexpr int CrateLayer = 2;
    constexpr int WallLayer = 3;
    constexpr int CrateCount = 8; // 置く木箱の数
    constexpr int WallCount = 4;  // 世界の端の壁の数(上下左右)
    // プレイヤーに触れている間の、跳ね回るものの色の既定(設定のデータ BouncerSettings::touchedColor が読めないときに使う)
    constexpr render::Color TouchedColor = {1.0f, 0.25f, 0.25f, 1.0f};

    // 画面の中を跳ね回るアクター。見た目は SpriteComponent で足す。
    class BouncerActor : public engine::Actor
    {
        HUSKY_OBJECT(BouncerActor, engine::Actor)
    public:
        float vx = 0.0f; // 速度(ピクセル/秒)
        float vy = 0.0f;
        // 全員で共有する設定(SD-4。プレハブで指すので、置いたもの1体ずつの違いとしては保存されない)。触れたときの色を読む。
        core::AssetRef<BouncerSettings> settings;
        // プレイヤーに触れ始めたときに鳴らす音(AUD-4。プレハブで指すので、1体ずつの違いとしては保存されない)。
        core::AssetRef<engine::SoundAsset> touchSound;
        // 今、触れているプレイヤーの数(0 なら元の色)。元の色は savedColor に控える。
        // どちらも再生中だけの状態なので、プロパティにしない(保存しない。アニメーションの経った時間と同じ考え方)
        std::int32_t touching = 0;
        render::Color savedColor;

        // Scene::Tick から毎フレーム呼ばれる。
        // override: 親(Actor)の virtual 関数を上書きしていることを、コンパイラに確かめさせる(C# の override と同じ)。
        // 跳ね返る位置は、見た目(SpriteComponent)の大きさではなく、当たりの大きさ(BouncerSize)で決める。
        // 動きは見た目に頼らない(見た目のコンポーネントを外しても、同じように動く)。
        void Tick(double dt) override;

        // 当たり判定(COL-4): プレイヤーに触れている間は赤くなり、離れたら元の色に戻る。
        // 重なりはプレハブのコライダー(グループ BouncerLayer)で調べられ、エンジンがここを呼ぶ。
        void OnOverlap(const engine::OverlapEvent& event) override;

        HUSKY_REFLECTION_BEGIN()
            HUSKY_PROPERTY(vx)
            HUSKY_PROPERTY(vy)
            HUSKY_PROPERTY(settings)
            HUSKY_PROPERTY(touchSound)
        HUSKY_REFLECTION_END()
    };

    // 矢印キー(または WASD)で動くプレイヤーのアクター。見た目は SpriteComponent で足す。
    // マウスの左ボタンでクリックした所へも歩いていく(UI-1 の例。着くか、キーで動かすとやめる)。
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

    private:
        // クリックで決めた行き先(ワールドの位置)。その場限りの状態なので保存しない(HUSKY_PROPERTY にしない)。
        core::Vector3 m_moveTarget;
        bool m_hasMoveTarget = false;
    };
}
