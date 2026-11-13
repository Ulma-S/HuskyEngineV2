// 移動のギズモ(RS-4。Unity の移動ツールにあたる)。選んだアクターの位置に、編集中だけ出す。
//   - x の矢印(赤、右向き): つかむと x だけ動く
//   - y の矢印(緑、上向き): つかむと y だけ動く
//   - 真ん中の四角(黄色): つかむと x と y を一気に動かせる
// ズームしても、画面でいつも同じ大きさ(ピクセルで決める)。マウスを乗せた部品・つかんでいる部品は明るい黄色にする。
// 線はグリッドと同じく、細長い白いスプライトとして作り、シーンより手前に描く(Application::SetAfterSceneDrawCallback)。
//
// ここの関数は ImGui も GPU も使わないので、テストできる。画面の座標は、マウスと同じ決まり(左上が (0, 0)、下が +y)。
// 動作スレッド: どこでもよい
#pragma once

#if HUSKY_DEVELOPMENT

#include "core/Vector3.h"
#include "engine/Camera.h"
#include "render/RenderDevice.h"

#include <vector>

namespace husky::tools
{
    // ギズモの部品
    enum class GizmoHandle
    {
        None,   // どれでもない
        X,      // x の矢印
        Y,      // y の矢印
        Center, // 真ん中の四角
    };

    // 大きさ(画面のピクセル)
    namespace GizmoSize
    {
        constexpr float ArrowLength = 80.0f; // 矢印の軸の長さ(真ん中から先端の三角の付け根まで)
        constexpr float Thickness = 2.0f;    // 矢印の軸の太さ
        constexpr float HeadSize = 10.0f;    // 矢印の先(45 度回した四角)の一辺
        constexpr float CenterSize = 16.0f;  // 真ん中の四角の一辺
        constexpr float PickWidth = 6.0f;    // 矢印をつかめる幅(軸から上下・左右にこれだけ)
    }

    // 画面の点 (mouseX, mouseY) が、画面の (originX, originY) にあるギズモのどの部品の上か。
    // 真ん中の四角を先に調べる(矢印の付け根と重なるため)。
    GizmoHandle PickGizmoHandle(float originX, float originY, float mouseX, float mouseY);

    // つかんだ部品に合わせて、動いた先の位置を返す。from はつかむ前の位置、(deltaX, deltaY) はつかんでからマウスが動いた量
    // (ワールドの単位)。X なら x だけ、Y なら y だけ、Center なら両方に足す。z はそのまま。
    // snap が 0 より大きければ、動かした軸の値を snap の倍数(グリッドの線の上)に丸める(スナップ。動かしていない軸はそのまま)。
    core::Vector3 ApplyGizmoDrag(GizmoHandle handle, const core::Vector3& from, float deltaX, float deltaY, float snap = 0.0f);

    // ワールドの origin にあるギズモの線を、out に作り直す(入れ物は使い回す)。大きさはカメラの zoom から、画面で一定にする。
    // highlighted の部品は明るい黄色にする(マウスを乗せている・つかんでいる部品)。
    void BuildGizmoSprites(const engine::CameraView& view, const core::Vector3& origin, GizmoHandle highlighted,
                           std::vector<render::Sprite>& out);
}

#endif
