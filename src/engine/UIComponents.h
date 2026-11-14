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
#include "core/ObjectRef.h"
#include "engine/Actor.h"
#include "engine/Input.h"
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
    class World;

    // 文字の左右の寄せ(TextLayout.h の TextAlign)を、プロパティにできるように名前で登録する。
    HUSKY_ENUM_DECLARE(TextAlign) // 中身は .cpp(HR-1。DLL の中に1つだけ作るため)

    // 文字の上下の寄せ(四角の中のどこに文字のかたまりを置くか)。
    enum class UIVerticalAlign
    {
        Top,
        Middle,
        Bottom,
    };
    HUSKY_ENUM_DECLARE(UIVerticalAlign) // 中身は .cpp(HR-1。DLL の中に1つだけ作るため)

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

    enum class UIButtonState
    {
        Normal,  // 何もしていない
        Hovered, // マウスが乗っている
        Pressed, // 押していて、マウスがまだボタンの上にある
    };

    // 押せるボタン(Unity の Button)。同じ UIActor の四角が当たりになる。
    //   - 見た目: 状態に合わせた色(normalColor / hoveredColor / pressedColor)を、同じアクターの UIImageComponent の
    //     color に掛けて描く(値は書き換えない)。interactable が false なら disabledColor で、押せない。
    //   - 押したとき(左ボタンで押して、ボタンの上で離した): 先に、このアクターと全コンポーネントの OnUIClick(コードで受ける)、
    //     次に、target(+ targetComponent)の、引数の無いメソッド method をリフレクションで呼ぶ(インスペクタで選べる)。
    //     2026-11-14 にユーザーが「両方」を選んだ。
    // マウスを押したときにいちばん手前の UI がボタンなら、そのフレームからマウスはゲームに渡さない(Input::ConsumeMouse)。
    // 再生中だけ動く(編集中のクリックはエディタが使う)。
    class UIButtonComponent : public Component
    {
        HUSKY_OBJECT(UIButtonComponent, Component)
    public:
        bool interactable = true;
        render::Color normalColor = {1.0f, 1.0f, 1.0f, 1.0f};
        render::Color hoveredColor = {0.85f, 0.85f, 0.85f, 1.0f};
        render::Color pressedColor = {0.65f, 0.65f, 0.65f, 1.0f};
        render::Color disabledColor = {0.5f, 0.5f, 0.5f, 0.6f};
        // 押したときに呼ぶ先(何も指さなければ呼ばない)。targetComponent が空ならアクターのメソッド、
        // 型の名前なら、そのアクターの最初のその型のコンポーネントのメソッド。
        core::ObjectRef<Actor> target;
        std::string targetComponent;
        std::string method;

        UIButtonState GetState() const { return m_state; }
        // 今の状態の色(interactable でなければ disabledColor)。
        const render::Color& GetTintColor() const;

        HUSKY_REFLECTION_BEGIN()
            HUSKY_PROPERTY(interactable)
            HUSKY_PROPERTY(normalColor)
            HUSKY_PROPERTY(hoveredColor)
            HUSKY_PROPERTY(pressedColor)
            HUSKY_PROPERTY(disabledColor)
            HUSKY_PROPERTY(target)
            HUSKY_PROPERTY(targetComponent)
            HUSKY_PROPERTY(method)
        HUSKY_REFLECTION_END()

    private:
        friend class UIEventSystem;
        UIButtonState m_state = UIButtonState::Normal; // 保存しない
    };

    // UI の入力(UI-6b): マウスの下のいちばん手前の UI を調べ、ボタンの状態を進め、押されたボタンを呼ぶ。
    // Application が持ち、再生中のフレームだけ、入力を受けた後・シーンの更新(Tick)の前に Update を呼ぶ
    // (ゲームのコードが同じフレームで「UI が使ったマウス」を見ないように)。当たりは前のフレームの配置(LayoutCanvases)で調べる。
    // 当たりになるのは、Canvas の下の UIActor のうち UIImageComponent か UIButtonComponent を持つもの(文字だけのものは当たらない)。
    class UIEventSystem
    {
    public:
        void Update(World& scene, Input& input);
        // 全部のボタンを Normal に戻し、押しているボタンを忘れる(編集中になったときなど)。
        void Reset(World& scene);
        // マウスの下のいちばん手前の当たり(無ければ null)。テストと確かめ用。
        core::ObjectHandle GetHovered() const { return m_hovered; }

    private:
        core::ObjectHandle m_hovered; // マウスの下のいちばん手前の当たり(弱い参照)
        core::ObjectHandle m_pressed; // 押しているボタンのアクター(弱い参照。離すまで覚える)
    };

    // 押されたボタンの呼び先を呼ぶ(OnUIClick を届けてから、target の method)。UIEventSystem が使う。テストからも呼べる。
    void ClickUIButton(Actor& buttonActor, UIButtonComponent& button);

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
    void DrawUI(World& scene, render::RenderDevice& renderDevice, AssetLoader& loader, TextLayoutResult& layout);
}
