// ゲーム用 UI の部品(UI-6。設計メモ「ゲーム用 UI」)。UIActor(Canvas の下)に付けて使う。
//   - UIImageComponent: 自分の四角(RectTransform)いっぱいに画像を描く(2026-11-14、ユーザーが UI 専用の部品を選んだ)
//   - UITextComponent: 自分の四角の中に文字を並べて描く(UI-4 の LayoutText)
// どちらも画面のパスで描く(DrawUI。Application が DrawSprites(Screen) の後に呼ぶ)。順番はシーンの一覧の順(先が奥)、
// 1体の中では画像 → 文字。
//
// 文字はくっきり描く(2026-11-14、ユーザーが選んだ): Canvas が s 倍に拡大されているときは、字のアトラスを
// 「fontSize × s」ピクセル(整数に丸める)で作り、画面のピクセルで並べる。拡大した絵を引き伸ばさないので、にじまない。
//
// 動作スレッド: ゲームスレッドのみ
#pragma once

#include "core/AssetRef.h"
#include "core/EnumReflection.h"
#include "engine/Component.h"
#include "engine/FontAsset.h"
#include "engine/PixelRect.h"
#include "engine/RenderReflection.h" // render::Color をプロパティにするための登録
#include "engine/SpriteSheetAsset.h"
#include "engine/TextLayout.h"
#include "engine/TextureAsset.h"

#include <cstdint>
#include <string>

namespace husky::engine
{
    class AssetLoader;
    class Scene;

    // 文字の左右の寄せ(TextLayout.h の TextAlign)を、プロパティにできるように名前で登録する。
    HUSKY_ENUM_BEGIN(TextAlign)
        HUSKY_ENUM_VALUE(Left)
        HUSKY_ENUM_VALUE(Center)
        HUSKY_ENUM_VALUE(Right)
    HUSKY_ENUM_END()

    // 文字の上下の寄せ(四角の中のどこに文字のかたまりを置くか)。
    enum class UIVerticalAlign
    {
        Top,
        Middle,
        Bottom,
    };
    HUSKY_ENUM_BEGIN(UIVerticalAlign)
        HUSKY_ENUM_VALUE(Top)
        HUSKY_ENUM_VALUE(Middle)
        HUSKY_ENUM_VALUE(Bottom)
    HUSKY_ENUM_END()

    // 四角いっぱいに画像を描く(Unity の Image)。画像を指していなければ、color の色の板になる。
    // 画像の指し方は SpriteComponent と同じ(sheet + sprite が texture + region より優先)。
    class UIImageComponent : public Component
    {
        HUSKY_OBJECT(UIImageComponent, Component)
    public:
        render::Color color = {1.0f, 1.0f, 1.0f, 1.0f}; // 画像に掛ける色(画像が無ければ、この色の板)
        core::AssetRef<TextureAsset> texture;
        PixelRect region; // texture のどの範囲を貼るか(ピクセル。大きさ 0 なら全体)
        core::AssetRef<SpriteSheetAsset> sheet;
        std::int32_t sprite = 0; // sheet の中の切り出しの ID

        HUSKY_REFLECTION_BEGIN()
            HUSKY_PROPERTY(color)
            HUSKY_PROPERTY(texture)
            HUSKY_PROPERTY(region)
            HUSKY_PROPERTY(sheet)
            HUSKY_PROPERTY(sprite)
        HUSKY_REFLECTION_END()
    };

    // 四角の中に文字を描く(Unity の Text)。四角の幅で折り返し(wrap)、左右と上下に寄せる。はみ出した行も描く。
    class UITextComponent : public Component
    {
        HUSKY_OBJECT(UITextComponent, Component)
    public:
        core::AssetRef<FontAsset> font; // 何も指していなければ描かない
        std::string text;               // UTF-8。"\n" で改行
        float fontSize = 32.0f;         // 字の大きさ(Canvas の単位。FontFace の pixelHeight と同じ決まり)
        render::Color color = {1.0f, 1.0f, 1.0f, 1.0f};
        TextAlign alignment = TextAlign::Left;
        UIVerticalAlign verticalAlignment = UIVerticalAlign::Top;
        bool wrap = true;          // 四角の幅で折り返すか
        float lineSpacing = 1.0f;  // 行の高さに掛ける

        HUSKY_REFLECTION_BEGIN()
            HUSKY_PROPERTY(font)
            HUSKY_PROPERTY(text)
            HUSKY_PROPERTY(fontSize)
            HUSKY_PROPERTY(color)
            HUSKY_PROPERTY(alignment)
            HUSKY_PROPERTY(verticalAlignment)
            HUSKY_PROPERTY(wrap)
            HUSKY_PROPERTY(lineSpacing)
        HUSKY_REFLECTION_END()
    };

    // UIActor の四角の、画面での位置(ピクセル。左下が原点、上が +)。LayoutCanvases の後なら正しい。
    struct UIScreenRect
    {
        float left = 0.0f;
        float bottom = 0.0f;
        float width = 0.0f;
        float height = 0.0f;
        float scale = 1.0f; // Canvas の拡大(画面のピクセル ÷ Canvas の単位)
    };
    class UIActor;
    UIScreenRect ComputeUIScreenRect(const UIActor& actor);

    // 字のアトラスを作る大きさ(くっきり描くため、fontSize × Canvas の拡大を整数に丸めたもの。1 以上)。
    float ComputeUIFontPixelSize(float fontSize, float canvasScale);

    // シーンの Canvas の下の UIActor の、画像と文字を積む(画面ちょうどのカメラの前提)。layout は文字を並べる入れ物(使い回す)。
    // 文字のアトラスへの字の書き足し(Upload)もここで行う。renderDevice.BeginFrame と EndFrame の間で呼ぶ。
    void DrawUI(Scene& scene, render::RenderDevice& renderDevice, AssetLoader& loader, TextLayoutResult& layout);
}
