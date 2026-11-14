// 【開発ビルドだけ】ゲーム用 UI の四角の枠(UI-5b)。Unity のシーンビューで RectTransform の四角が見えるのと同じ。
// 編集中だけ、画面の座標のパス(Application::SetScreenDrawCallback)で、Canvas の下の全部の UIActor の四角を細い灰色の線で、
// 選んでいる UIActor の四角を橙色の線で描く。UIActor は画面の座標で描くので、シーンビューのギズモや枠(ワールド)は出さない。
// 動作スレッド: ゲームスレッド
#pragma once

#if HUSKY_DEVELOPMENT

#include "core/Handle.h"
#include "engine/World.h"
#include "render/RenderDevice.h"

#include <vector>

namespace husky::tools
{
    // 枠の線(細いスプライト)を out に作り直す(GPU を使わないのでテストできる)。行列は画面ちょうどのカメラの前提。
    // selected: 選んでいるアクター(UIActor なら橙色で、ほかより手前に描く)。
    void BuildUIRectOutlines(engine::World& scene, core::ObjectHandle selected, std::vector<render::Sprite>& out);

    class UIRectOverlay final
    {
    public:
        // 枠を作り直して積む。editing が false(再生中など)なら何もしない。
        void Draw(engine::World& scene, core::ObjectHandle selected, bool editing, render::RenderDevice& renderDevice);

    private:
        std::vector<render::Sprite> m_lines; // 使い回す入れ物
    };
}

#endif
