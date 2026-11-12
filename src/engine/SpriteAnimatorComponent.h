// スプライトのアニメーションを再生するコンポーネント。Unity の Animator(スプライトのコマ送りに使うとき)にあたる。
//
// 見た目(何を描くか)は SpriteComponent が持ち、このコンポーネントは「時間で、どの切り出しを描くか」だけを決める。
// 毎フレーム時間を進め、同じアクターの SpriteComponent の sprite(切り出しの ID)を、アニメーションの今のコマに書き換える。
// アニメーションは、SpriteComponent が指しているスプライトシート(sheet)の中から、ID で選ぶ(SpriteSheetData.h)。
//
// スプライト専用。3D の骨のアニメーション(FBX など)は、値の種類(とびとびの ID と、補間する骨の姿勢)も、
// 要る機能(混ぜ合わせ、状態の切り替え)も違うので、作るときは別の仕組みにする(汎用の名前 Animator は空けておく)。
//
// 使い方:
//   SpriteComponent* sprite = actor->AddComponent<SpriteComponent>();
//   sprite->sheet.guid = sheetGuid;                       // 切り出しとアニメーションを持つスプライトシート
//   SpriteAnimatorComponent* animator = actor->AddComponent<SpriteAnimatorComponent>();
//   animator->Play(walkAnimationId);                      // 最初のコマから再生する
//   animator->speed = 2.0f;                               // 2 倍の速さ
//   animator->playing = false;                            // 一時停止(今のコマのまま止まる)
#pragma once

#include "engine/Component.h"

#include <cstdint>

namespace husky::core
{
    class ObjectRegistry;
}

namespace husky::engine
{
    class SpriteComponent;
    class SpriteSheetAsset;

    // 動作スレッド: ゲームスレッドのみ
    class SpriteAnimatorComponent : public Component
    {
        HUSKY_OBJECT(SpriteAnimatorComponent, Component)
    public:
        // 再生するアニメーションの ID(スプライトシートの中のもの)。0 か、見つからない ID なら、何もしない。
        // 別の ID に書き換えると、次の Tick で、そのアニメーションの最初のコマから始まる。インスペクタでは名前で選べる。
        std::int32_t animation = 0;
        bool playing = true; // false の間は時間を進めない(今のコマのまま止まる)
        float speed = 1.0f;  // 再生の速さの倍率。0 以下は止まっているのと同じ(逆再生はしない)

        HUSKY_REFLECTION_BEGIN()
            HUSKY_PROPERTY(animation)
            HUSKY_PROPERTY(playing)
            HUSKY_PROPERTY(speed)
        HUSKY_REFLECTION_END()

        // animationId のアニメーションを、最初のコマから再生する(同じ ID でも、最初からやり直す)。
        void Play(std::int32_t animationId);

        // 今のアニメーションを再生し始めてから進んだ時間(秒。speed を掛けた後、一時停止の間は増えない)。
        // 実行中だけの値なので保存しない。シーンを読み直すと、最初のコマから始まる。
        double GetTime() const { return m_time; }

        // 時間を進めて、SpriteComponent の sprite を今のコマにする。
        // スプライトシートがまだ読めていない間は、時間を進めない(見え始めたときに、最初のコマから再生するため)。
        // スプライトシートを読み始めるのは描画(DrawSprites)なので、このコンポーネントはローダを知らなくてよい。
        void Tick(double dt) override;

        // 見た目の準備なので、全員が動いた後、描く直前に動かす(ほかのコンポーネントが同じフレームに Play しても、そのフレームから効く)。
        std::int32_t GetTickOrder() const override { return TickOrder::PreRender; }

    private:
        double m_time = 0.0;
        // m_time が、どのアニメーションの時間か。animation が書き換えられたことに気づき、時間を 0 に戻すために使う。
        std::int32_t m_timeAnimation = 0;
    };

    // sprite が指しているスプライトシートが、読み終わっていれば返す。指していない、読み込み中、失敗、
    // GUID が書き換えられた後でまだ読み直していない(asset が古い)ときは nullptr。
    // アニメーターとインスペクタの両方で使う。
    // 動作スレッド: ゲームスレッドのみ
    // registry: アセットを引くレジストリ(sprite を作ったもの)。
    const SpriteSheetAsset* FindLoadedSpriteSheet(const SpriteComponent& sprite, core::ObjectRegistry& registry);
}
