// シーン(SC-2b、2026-11-14): アクターをまとめて読む・下ろす入れ物。位置を持たないので、transform の計算はかからない。
// Unity の「管理用の空のゲームオブジェクト」の代わり。UE のサブレベル、Godot の(位置の無い)Node に近い。
//
// 決まり(ユーザーの決定。CLAUDE.md「オブジェクトモデル」):
//   - シーンは木になる。ワールド(World)はルートシーンを必ず1つ持ち、ルートシーンは下ろせない。
//   - アクターはどれか1つのシーンに入る(Actor::GetScene)。置き場所を指定しないで置いたアクターはルートシーンに入る。
//   - 親子の階層はシーンをまたげない(SetParent はエラーで断る)。
//   - 同じシーン(同じ名前)は同時に1つだけ読める。
//   - シーンを下ろすと、中のアクターと子のシーンもまとめて消える。
//
// 使い方(ゲームスレッド。読む・下ろすのはフレームの途中ではなく、Application が物理の後にまとめて行う):
//   world->LoadScene("title");            // シーンの一覧から名前で探して、ルートシーンの下に読む
//   world->UnloadScene("title");          // 下ろす(中のアクターは Destroy)
//   Scene* title = world->FindScene("title");
#pragma once

#include "core/Guid.h"
#include "core/Object.h"

#include <string>
#include <vector>

namespace husky::engine
{
    // 動作スレッド: ゲームスレッドのみ
    class Scene : public core::Object
    {
        HUSKY_OBJECT(Scene, core::Object)
    public:
        // シーンの名前(シーンの一覧の名前 = ファイル名から拡張子を除いたもの)。ルートシーンは読んだシーンの名前か空。
        std::string name;
        // 元のシーンのアセット(.scene)の GUID。ファイルから作っていなければ空。
        core::Guid asset;
        // 子のシーン(強い参照。シーンを生かす向きは ワールド → ルートシーン → 子のシーン)。
        std::vector<core::ObjectHandle> children;

        // シーンのファイルに書く子のシーンの記録(SC-3)。ファイルの "scenes": [{"scene": GUID, "load": true}]。
        // loadOnOpen が true のものは、このシーンを開いた・読んだときに一緒に読む(false は記録だけ。ゲームが LoadScene で読む)。
        struct ChildSceneEntry
        {
            core::Guid scene;
            bool loadOnOpen = true;
        };
        std::vector<ChildSceneEntry> childScenes;

        // 親のシーン。ルートシーンなら nullptr。
        Scene* GetParent() const;
        bool IsRoot() const { return m_parent.IsNull(); }
        // other がこのシーンの祖先なら true(自分自身も true)。
        bool IsSelfOrDescendantOf(const Scene& other) const;

        HUSKY_REFLECTION_BEGIN()
            HUSKY_PROPERTY(name)
            HUSKY_PROPERTY(asset)
            HUSKY_PROPERTY(children)
        HUSKY_REFLECTION_END()

    private:
        friend class World; // 親子をつなぐのは World だけ

        // 親のシーン(弱い参照。プロパティにしない)。
        core::ObjectHandle m_parent;
    };
}
