// スプライト1枚の見た目を、アクターに足すコンポーネントと、シーンのスプライトを描画に積む関数。
// Unity の SpriteRenderer にあたる。
//
// 使い方:
//   SpriteComponent* sprite = actor->AddComponent<SpriteComponent>();
//   sprite->width = 32.0f;
//   sprite->height = 32.0f;
//   sprite->color = {1.0f, 0.5f, 0.5f, 1.0f};
//   ...
//   // 毎フレーム、描画のとき
//   if (renderDevice.BeginFrame(clearColor))
//   {
//       DrawSprites(*scene, renderDevice);   // スプライトを持つアクターを、シーンの一覧の順に積む
//       renderDevice.EndFrame();
//   }
#pragma once

#include "engine/Scene.h"
#include "engine/RenderReflection.h" // render::Color をプロパティにするための登録
#include "render/RenderDevice.h"

namespace husky::engine
{
    // 動作スレッド: ゲームスレッドのみ
    class SpriteComponent : public Component
    {
        HUSKY_OBJECT(SpriteComponent, Component)
    public:
        // 位置は持たない。描くときは、持ち主のアクターの位置(x, y)を使う。
        // Tick は使わない(見た目は描画のときに読まれるだけ)。
        float width = 32.0f; // 描く大きさ(ピクセル)
        float height = 32.0f;
        render::Color color = {1.0f, 1.0f, 1.0f, 1.0f}; // テクスチャに掛ける色。白ならテクスチャのまま

        // 貼るテクスチャ。何も指さなければ、Application の既定のテクスチャ(DrawSprites の defaultTexture)を貼る。
        // 【仮】プロパティには登録しない(保存されない)。ハンドルは実行中だけの値で、ファイルに書いても次の起動では意味が無いため。
        // A-4 で、ファイルに書ける AssetRef<Texture>(中身は GUID)に替える。
        render::TextureHandle texture;

        HUSKY_REFLECTION_BEGIN()
            HUSKY_PROPERTY(width)
            HUSKY_PROPERTY(height)
            HUSKY_PROPERTY(color) // 構造体(RenderReflection.h で登録)。保存すると {"r": .., "g": .., "b": .., "a": ..} になる
        HUSKY_REFLECTION_END()
    };

    // シーンの生きているアクターのうち、SpriteComponent を持つものについて、
    // 描くスプライト(render::Sprite)を作り、function(sprite) を一覧の順に呼ぶ。
    // 1つのアクターが SpriteComponent を複数持っていても、使うのは最初の1つだけ。
    // 描画(DrawSprites)とテストの両方で使うために、「何を描くか」の部分だけを取り出している。
    // 動作スレッド: ゲームスレッドのみ
    template <typename Function>
    void ForEachSprite(Scene& scene, Function&& function)
    {
        scene.ForEachActor([&](Actor& actor) {
            const SpriteComponent* spriteComponent = actor.GetComponent<SpriteComponent>();
            if (spriteComponent == nullptr) // 持っていない(GetComponent は Destroy 済みのものも返さない)
            {
                return;
            }
            render::Sprite sprite;
            sprite.x = actor.x;
            sprite.y = actor.y;
            sprite.width = spriteComponent->width;
            sprite.height = spriteComponent->height;
            sprite.color = spriteComponent->color;
            sprite.texture = spriteComponent->texture;
            function(sprite);
        });
    }

    // シーンのスプライトを、一覧の順に renderDevice.DrawSprite で積む(先に積んだものが奥)。
    // テクスチャを指していないスプライトには、defaultTexture を貼る。
    // renderDevice.BeginFrame と EndFrame の間で呼ぶ。
    // 動作スレッド: ゲームスレッドのみ
    void DrawSprites(Scene& scene, render::RenderDevice& renderDevice, render::TextureHandle defaultTexture);
}
