// 移動・回転・拡大縮小のギズモ(RS-4 / RS-5。Unity の移動ツール・回転ツール・拡大縮小ツールにあたる)。
// 選んだアクターの位置に、編集中だけ出す。どれを出すかは道具(GizmoTool)で決め、シーンビューで W / E / R キーで切り替える。
//   - 移動(W): x の矢印(赤)は x だけ、y の矢印(緑)は y だけ、真ん中の四角は x と y を一気に動かす。矢印はワールドの向き
//   - 回転(E): 輪(青)をつかんで、ギズモの真ん中のまわりに回すと、z 軸まわりに回る(反時計回りが +)
//   - 拡大縮小(R): x の軸(赤)は scale.x だけ、y の軸(緑)は scale.y だけ、真ん中の四角は両方を同じ割合で変える。
//     拡大縮小はアクターのローカルの向きにかかるので、軸はアクターと一緒に回して描く(Unity と同じ)
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
    // 道具(どのギズモを出すか)
    enum class GizmoTool
    {
        Move,   // 移動(W)
        Rotate, // 回転(E)
        Scale,  // 拡大縮小(R)
    };

    // ギズモの部品
    enum class GizmoHandle
    {
        None,   // どれでもない
        X,      // x の矢印・軸
        Y,      // y の矢印・軸
        Center, // 真ん中の四角
        Ring,   // 回転の輪
    };

    // 大きさ(画面のピクセル)
    namespace GizmoSize
    {
        constexpr float ArrowLength = 80.0f; // 矢印の軸の長さ(真ん中から先まで)
        constexpr float Thickness = 2.0f;    // 線の太さ
        constexpr float HeadSize = 10.0f;    // 矢印の先(移動は 45 度回した菱形、拡大縮小は四角)の一辺
        constexpr float CenterSize = 16.0f;  // 真ん中の四角の一辺
        constexpr float PickWidth = 6.0f;    // 線をつかめる幅(線から、これだけ離れていてもつかめる)
        constexpr float RingRadius = 70.0f;  // 回転の輪の半径
        constexpr int RingSegments = 48;     // 回転の輪を作る短い線の数
    }

    // スナップの刻み(Ctrl を押している間)。移動はグリッドの間隔(ChooseGridSpacing)を使う
    namespace GizmoSnap
    {
        constexpr float RotationDegrees = 15.0f; // 回転は 15 度ずつ(Unity の既定と同じ)
        constexpr float Scale = 0.1f;            // 拡大縮小は 0.1 ずつ
    }

    // 画面の点 (mouseX, mouseY) が、画面の (originX, originY) にある道具 tool のギズモのどの部品の上か。
    // rotationDegrees はアクターの回転(拡大縮小の軸を回して調べるため。ほかの道具では使わない)。
    // 移動と拡大縮小は、真ん中の四角を先に調べる(軸の付け根と重なるため)。
    GizmoHandle PickGizmoHandle(GizmoTool tool, float originX, float originY, float mouseX, float mouseY,
                                float rotationDegrees = 0.0f);

    // 移動: つかんだ部品に合わせて、動いた先の位置を返す。from はつかむ前の位置、(deltaX, deltaY) はつかんでからマウスが
    // 動いた量(ワールドの単位)。X なら x だけ、Y なら y だけ、Center なら両方に足す。z はそのまま。
    // snap が 0 より大きければ、動かした軸の値を snap の倍数(グリッドの線の上)に丸める(スナップ。動かしていない軸はそのまま)。
    core::Vector3 ApplyGizmoDrag(GizmoHandle handle, const core::Vector3& from, float deltaX, float deltaY, float snap = 0.0f);

    // 回転: ギズモの真ん中(画面の origin)から見たマウスの向きが、from の点から to の点へ動く間に何度回ったか(-180〜+180)。
    // 画面の y は下が + なので、反時計回りが + になるように裏返して計算する。
    // シーンビューは、これを毎フレーム足していく(つかんだ時からの角度を直接求めると、半周を越えたところで -180 へ飛ぶため)。
    float MouseAngleDelta(float originX, float originY, float fromX, float fromY, float toX, float toY);

    // 回転: つかむ前の角度 fromDegrees に、回した角度 deltaDegrees を足す。snapDegrees が 0 より大きければ、その倍数に丸める。
    float ApplyRotateDrag(float fromDegrees, float deltaDegrees, float snapDegrees = 0.0f);

    // 拡大縮小: つかんでからマウスが動いた量(画面のピクセル)を、アクターのローカルの向き(rotationDegrees だけ回した軸)に
    // 直し、軸の長さ(ArrowLength)だけ引っ張ると 2 倍になる割合で scale に掛ける。X なら x だけ、Y なら y だけ、
    // Center なら両方(右か上へ引っ張ると大きくなる)。z はそのまま。snap が 0 より大きければ、変えた軸をその倍数に丸める。
    core::Vector3 ApplyScaleDrag(GizmoHandle handle, const core::Vector3& fromScale, float rotationDegrees, float startX,
                                 float startY, float mouseX, float mouseY, float snap = 0.0f);

    // ワールドの origin にある道具 tool のギズモの線を、out に作り直す(入れ物は使い回す)。大きさはカメラの zoom から、
    // 画面で一定にする。rotationDegrees はアクターの回転(拡大縮小の軸を回す)。highlighted の部品は明るい黄色にする。
    void BuildGizmoSprites(GizmoTool tool, const engine::CameraView& view, const core::Vector3& origin, float rotationDegrees,
                           GizmoHandle highlighted, std::vector<render::Sprite>& out);

    // 道具の名前("Move (W)" など。画面の隅に出す)。
    const char* GetGizmoToolName(GizmoTool tool);
}

#endif
