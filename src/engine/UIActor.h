// ゲーム用 UI のアクター(UI-5a。設計メモ「ゲーム用 UI」)。Unity の RectTransform を持つ GameObject にあたる。
//
// UI の置き方は RectTransform(Unity と同じ5つの値。2026-11-14 にユーザーが選んだ)で決める:
//   - anchorMin / anchorMax: 親の四角のどこに付けるか(親の四角の割合。0 が左・下、1 が右・上)。
//     2つが同じなら「親の1点に付く」、違えば「親の四角の一部に合わせて伸びる」(例: (0, 0)〜(1, 0) で下の辺いっぱい)。
//   - pivot: 自分の四角の中の基準の点(割合)。anchoredPosition はこの点の位置で、回転・拡大の中心にもなる(回転は後回し)。
//   - anchoredPosition: アンカーの点(伸びるときは anchorMin〜anchorMax の四角の中の pivot の割合の点)から、pivot までのずれ。
//   - sizeDelta: 大きさ。アンカーが1点なら幅と高さそのもの、伸びるときは「アンカーの四角より何だけ大きいか」。
// 単位は Canvas の単位(基準の解像度のピクセル。CanvasComponent)、右が +x、上が +y。
//
// transform は計算の結果(IsTransformDriven が true)。LayoutCanvases が毎フレーム、親の四角と RectTransform から
// 自分の四角(GetRect)を求め、transform に「親の pivot から自分の pivot へのずれ」を書く(回転 0、拡大 1)。
// Canvas の根だけは、transform が「画面の左下からの位置と拡大(画面の大きさ ÷ 基準の解像度)」になるので、
// 子のワールドの行列(親 × 自分)がそのまま画面のピクセルになる。
//
// 動作スレッド: ゲームスレッド
#pragma once

#include "core/StructReflection.h"
#include "core/Vector2.h"
#include "engine/Actor.h"
#include "engine/Component.h"

namespace husky::engine
{
    class Scene;

    struct RectTransform
    {
        core::Vector2 anchorMin = {0.5f, 0.5f};
        core::Vector2 anchorMax = {0.5f, 0.5f};
        core::Vector2 pivot = {0.5f, 0.5f};
        core::Vector2 anchoredPosition = {0.0f, 0.0f};
        core::Vector2 sizeDelta = {100.0f, 100.0f};
    };

    HUSKY_STRUCT_BEGIN(RectTransform)
        HUSKY_PROPERTY(anchorMin)
        HUSKY_PROPERTY(anchorMax)
        HUSKY_PROPERTY(pivot)
        HUSKY_PROPERTY(anchoredPosition)
        HUSKY_PROPERTY(sizeDelta)
    HUSKY_STRUCT_END()

    // 四角(Canvas の単位)。x, y が左下の角、右と上が +。
    struct UIRect
    {
        float x = 0.0f;
        float y = 0.0f;
        float width = 0.0f;
        float height = 0.0f;
    };

    // 親の四角 parent の中に、rect で置いたときの自分の四角(Unity の RectTransform と同じ計算)。GPU を使わない。
    UIRect ComputeChildRect(const UIRect& parent, const RectTransform& rect);

    class UIActor : public Actor
    {
        HUSKY_OBJECT(UIActor, Actor)
    public:
        RectTransform rectTransform;

        bool IsTransformDriven() const override { return true; }

        // 最後の LayoutCanvases で求めた自分の四角(Canvas の単位、Canvas の左下が原点)。保存しない。
        const UIRect& GetRect() const { return m_rect; }
        // Canvas の下にあって、最後の LayoutCanvases で置かれたか(Canvas の外の UIActor は置かれず、描かれない)。
        bool IsInCanvas() const { return m_inCanvas; }

        HUSKY_REFLECTION_BEGIN()
            HUSKY_PROPERTY(rectTransform)
        HUSKY_REFLECTION_END()

    private:
        friend void LayoutCanvases(Scene& scene, float screenWidth, float screenHeight);
        friend class UILayout; // 計算の中身(UIActor.cpp)

        UIRect m_rect;
        core::Vector2 m_pivotPoint;    // 自分の pivot の点(Canvas の単位)。子の transform の基準
        std::uint64_t m_layoutPass = 0; // 何回目の LayoutCanvases で置いたか(同じ回に2度計算しないため)
        bool m_inCanvas = false;
    };

    // UI の木の根に付ける(UI-5a)。付けた UIActor が Canvas になり、その子孫の UIActor を置く。
    // 基準の解像度で UI を作り、実際の画面では「画面の高さ ÷ referenceHeight」倍に拡大縮小する(高さに合わせる。
    // 2026-11-14 にユーザーが選んだ)。横長の画面では、Canvas の幅が基準より広くなる(アンカーで右端に付けたものは右端に付く)。
    // Canvas の根の RectTransform は使わない(大きさは画面で決まる)。Canvas のアクターには親を付けない。
    class CanvasComponent : public Component
    {
        HUSKY_OBJECT(CanvasComponent, Component)
    public:
        float referenceWidth = 1280.0f;
        float referenceHeight = 720.0f;

        HUSKY_REFLECTION_BEGIN()
            HUSKY_PROPERTY(referenceWidth)
            HUSKY_PROPERTY(referenceHeight)
        HUSKY_REFLECTION_END()
    };

    // シーンの全部の Canvas の下の UIActor を置く(四角を求め、transform を書く)。毎フレーム1回、描く前に呼ぶ
    // (Application が StepFrame の終わりで呼ぶ。編集中も止めない)。screenWidth / screenHeight は画面のピクセル。
    // 0 以下なら基準の解像度の画面とみなす(テストや CLI)。
    void LayoutCanvases(Scene& scene, float screenWidth, float screenHeight);
}
