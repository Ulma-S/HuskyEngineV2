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

#include "engine/AssetLoader.h"
#include "engine/PixelRect.h"
#include "engine/SpriteSheetAsset.h"
#include "engine/Scene.h"
#include "engine/RenderReflection.h" // render::Color をプロパティにするための登録
#include "render/RenderDevice.h"

#include <cstdint>

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

        // 貼るテクスチャ(アセットの参照)。何も指さないか、読めなかったときは、
        // Application の既定のテクスチャ(DrawSprites の defaultTexture)を貼る。
        // 保存すると GUID の文字列になる。このコンポーネントがシーンにいる間は、テクスチャも GC に回収されない。
        core::AssetRef<TextureAsset> texture;
        // テクスチャのどの範囲を貼るか(ピクセル単位。画像の左上が (0, 0))。大きさが 0 なら、画像の全体を貼る。
        // 1 枚の画像に並べた絵(スプライトシート)から、1 つを切り出すのに使う。既定のテクスチャを貼るときは使わない。
        PixelRect region;

        // スプライトシートから切り出して貼るとき: スプライトシートと、その中の切り出しの ID。
        // sheet を指していれば、texture と region より優先して、「スプライトシートの画像の、切り出しの範囲」を貼る。
        // sprite が見つからない(0、または消された切り出し)ときは、スプライトシートの画像の全体を貼る。
        core::AssetRef<SpriteSheetAsset> sheet;
        std::int32_t sprite = 0; // 切り出しの ID(SpriteSlices.h)。インスペクタでは名前で選べる

        HUSKY_REFLECTION_BEGIN()
            HUSKY_PROPERTY(width)
            HUSKY_PROPERTY(height)
            HUSKY_PROPERTY(color)   // 構造体(RenderReflection.h で登録)。保存すると {"r": .., "g": .., "b": .., "a": ..} になる
            HUSKY_PROPERTY(texture) // アセットの参照。保存すると "3f2a9c1e-..." のような GUID の文字列になる
            HUSKY_PROPERTY(region)  // 構造体(PixelRect.h で登録)
            HUSKY_PROPERTY(sheet)   // アセットの参照(スプライトシート)
            HUSKY_PROPERTY(sprite)  // 切り出しの ID(整数)
        HUSKY_REFLECTION_END()
    };

    // シーンの生きているアクターのうち、SpriteComponent を持つものについて、
    // 描くスプライト(render::Sprite)を作り、function(sprite, spriteComponent) を一覧の順に呼ぶ。
    // sprite のテクスチャは空のまま渡す(アセットから描画層のテクスチャを引くのは、DrawSprites の役目)。
    // 1つのアクターが SpriteComponent を複数持っていても、使うのは最初の1つだけ。
    // 描画(DrawSprites)とテストの両方で使うために、「何を描くか」の部分だけを取り出している。
    // 動作スレッド: ゲームスレッドのみ
    template <typename Function>
    void ForEachSprite(Scene& scene, Function&& function)
    {
        scene.ForEachActor([&](Actor& actor) {
            SpriteComponent* spriteComponent = actor.GetComponent<SpriteComponent>();
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
            function(sprite, *spriteComponent);
        });
    }

    // ピクセル単位の範囲 region を、大きさ textureWidth x textureHeight の画像の UV(0〜1)にして、sprite に書く。
    // region が空(大きさ 0)か、画像の大きさが分からない(0)なら、画像の全体(0〜1)にする。
    // 動作スレッド: どこでもよい
    void ApplyRegion(const PixelRect& region, int textureWidth, int textureHeight, render::Sprite& sprite);

    // コンポーネントが貼る「テクスチャのアセット」と「その中の範囲(ピクセル)」を決める。GPU は使わない(テストできる)。
    //   1. sheet を指していれば: スプライトシートの画像と、切り出しの範囲(見つからなければ空 = 全体)
    //   2. そうでなく texture を指していれば: その画像と region
    // 戻り値: 読み終わったテクスチャのアセット。指していない、読み込み中、失敗なら nullptr(呼んだ側は既定のテクスチャを使う)。
    // まだ読んでいないアセットは、ここで読み始める(loader.Resolve)ので、コンポーネントを書き換えることがある。
    // 動作スレッド: ゲームスレッドのみ
    const TextureAsset* ChooseSpriteTexture(SpriteComponent& component, AssetLoader& loader, PixelRect& outRegion);

    // シーンのスプライトを、一覧の順に renderDevice.DrawSprite で積む(先に積んだものが奥)。
    // テクスチャと範囲は ChooseSpriteTexture で決める。指していない、もう無い、読めていないときは、
    // defaultTexture(これもアセット)を貼る。それも無ければ、白い板になる。
    // まだ読んでいないテクスチャは、ここで読む(loader.Resolve)ので、コンポーネントを書き換えることがある。
    // renderDevice.BeginFrame と EndFrame の間で呼ぶ。
    // 動作スレッド: ゲームスレッドのみ
    void DrawSprites(Scene& scene, render::RenderDevice& renderDevice, AssetLoader& loader,
                     core::ObjectHandle defaultTexture);
}
