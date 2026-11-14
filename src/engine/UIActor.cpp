// UIActor.h の実装(UI の配置の計算)。
#include "engine/UIActor.h"

#include "engine/Scene.h"

namespace husky::engine
{
    UIRect ComputeChildRect(const UIRect& parent, const RectTransform& rect)
    {
        // 1. アンカーの四角: 親の四角のうち anchorMin〜anchorMax の部分(1点なら大きさ 0 の四角)
        const float anchorLeft = parent.x + parent.width * rect.anchorMin.x;
        const float anchorBottom = parent.y + parent.height * rect.anchorMin.y;
        const float anchorWidth = parent.width * (rect.anchorMax.x - rect.anchorMin.x);
        const float anchorHeight = parent.height * (rect.anchorMax.y - rect.anchorMin.y);

        // 2. 大きさ: アンカーの四角の大きさ + sizeDelta
        UIRect result;
        result.width = anchorWidth + rect.sizeDelta.x;
        result.height = anchorHeight + rect.sizeDelta.y;

        // 3. pivot の点: アンカーの四角の中の pivot の割合の点 + anchoredPosition
        const float pivotX = anchorLeft + anchorWidth * rect.pivot.x + rect.anchoredPosition.x;
        const float pivotY = anchorBottom + anchorHeight * rect.pivot.y + rect.anchoredPosition.y;

        // 4. 左下の角: pivot の点から、自分の大きさの pivot の割合だけ戻る
        result.x = pivotX - result.width * rect.pivot.x;
        result.y = pivotY - result.height * rect.pivot.y;
        return result;
    }

    // 計算の中身。UIActor の private を触るので、UIActor.h で friend にしてある。
    class UILayout
    {
    public:
        // actor を置く(親が先)。Canvas の下でなければ m_inCanvas を false にする。
        static void Place(UIActor& actor, std::uint64_t pass, float screenWidth, float screenHeight, int depth)
        {
            if (actor.m_layoutPass == pass)
            {
                return; // この回ではもう置いた
            }
            actor.m_layoutPass = pass;
            actor.m_inCanvas = false;

            // Canvas の根: 大きさは画面で決まる。transform は画面の左下からの位置 (0, 0) と拡大
            if (const CanvasComponent* canvas = actor.GetComponent<CanvasComponent>())
            {
                const float referenceWidth = canvas->referenceWidth > 0.0f ? canvas->referenceWidth : 1.0f;
                const float referenceHeight = canvas->referenceHeight > 0.0f ? canvas->referenceHeight : 1.0f;
                const float width = screenWidth > 0.0f ? screenWidth : referenceWidth;
                const float height = screenHeight > 0.0f ? screenHeight : referenceHeight;
                const float scale = height / referenceHeight; // 高さに合わせる
                actor.m_rect = UIRect{0.0f, 0.0f, width / scale, height / scale};
                actor.m_pivotPoint = core::Vector2{0.0f, 0.0f}; // 子の位置の基準は Canvas の左下
                actor.transform.position = core::Vector3{0.0f, 0.0f, actor.transform.position.z};
                actor.transform.rotation = 0.0f;
                actor.transform.scale = core::Vector3{scale, scale, 1.0f};
                actor.m_inCanvas = true;
                return;
            }

            // 親が UIActor なら、親を先に置いてから、親の四角の中に置く
            Actor* parent = actor.GetParent();
            if (parent == nullptr || !parent->IsA<UIActor>() || depth >= Actor::MaxHierarchyDepth)
            {
                return; // Canvas の外の UIActor は置かない
            }
            UIActor& parentUi = static_cast<UIActor&>(*parent);
            Place(parentUi, pass, screenWidth, screenHeight, depth + 1);
            if (!parentUi.m_inCanvas)
            {
                return;
            }
            const RectTransform& rect = actor.rectTransform;
            actor.m_rect = ComputeChildRect(parentUi.m_rect, rect);
            actor.m_pivotPoint = core::Vector2{actor.m_rect.x + actor.m_rect.width * rect.pivot.x,
                                               actor.m_rect.y + actor.m_rect.height * rect.pivot.y};
            // transform は親の pivot の点から、自分の pivot の点へのずれ(親のワールドの行列を掛けると画面のピクセルになる)
            actor.transform.position = core::Vector3{actor.m_pivotPoint.x - parentUi.m_pivotPoint.x,
                                                     actor.m_pivotPoint.y - parentUi.m_pivotPoint.y,
                                                     actor.transform.position.z};
            actor.transform.rotation = 0.0f;
            actor.transform.scale = core::Vector3{1.0f, 1.0f, 1.0f};
            actor.m_inCanvas = true;
        }
    };

    void LayoutCanvases(Scene& scene, float screenWidth, float screenHeight)
    {
        // 回の番号: 同じ回に同じアクターを2度計算しないため(親を先に置くとき、子から何度も親へたどる)。
        static std::uint64_t s_pass = 0;
        ++s_pass;
        const std::uint64_t pass = s_pass;
        scene.ForEachActor([&](Actor& actor) {
            if (actor.IsA<UIActor>())
            {
                UILayout::Place(static_cast<UIActor&>(actor), pass, screenWidth, screenHeight, 0);
            }
        });
    }
}
