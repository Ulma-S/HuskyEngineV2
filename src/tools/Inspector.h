// シーンのインスペクタ(ImGui): アクターの一覧から1つを選び、そのアクターとコンポーネントのプロパティを表示・編集する。
// プロパティの画面は、リフレクション(TypeInfo と Property)から自動で作る。クラスごとに画面のコードを書かなくてよい。
// 製品(Release)には入らない(#if HUSKY_DEVELOPMENT)。
//
// 使い方(main.cpp):
//   tools::SceneInspector inspector;                       // 選んでいるアクターを覚えておくので、毎フレーム作り直さない
//   app.SetDevUiCallback([&inspector](engine::Application& a) { inspector.Draw(a); });
//
// 動作スレッド: ゲームスレッドのみ。
#pragma once

#if HUSKY_DEVELOPMENT

#include "core/ObjectRegistry.h"
#include "core/TypeInfo.h"
#include "engine/Application.h"
#include "tools/PropertyEditRecorder.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility> // std::pair
#include <vector>

namespace husky::tools
{
    // 型 type のプロパティを、container(その型のオブジェクトか構造体の先頭のアドレス)について、編集できる形で並べる。
    // 値は Property を通して読み書きする(オフセットを直接触らない)。ハンドルは、指している相手の名前を表示するだけ。
    // registry: ハンドルが指す相手を引くのに使う。
    // assets: アセットの参照(AssetRef)を選ぶときの、ローダ(アセットの一覧と、どのファイルがその型として読めるか。AL-3)。
    //         nullptr なら、GUID を表示するだけで選べない。
    // 戻り値: このフレームで、どれかの値が書き換えられたら true。
    bool DrawProperties(const core::TypeInfo& type, void* container, core::ObjectRegistry& registry,
                        const engine::AssetLoader* assets = nullptr);

    // プロパティ1つの表示を差し替える関数の型。引数は DrawProperties と同じ(container はプロパティの持ち主の先頭)。
    // 戻り値: 値を書き換えたら true。
    using PropertyDrawer = bool (*)(const core::Property& property, void* container, core::ObjectRegistry& registry,
                                    const engine::AssetLoader* assets);

    // type の propertyName のプロパティを、標準の部品ではなく drawer で表示するようにする(親の型のプロパティも指定できる)。
    // ふつうはリフレクションから自動で表示すれば足りるが、「整数の ID を、名前の一覧から選ばせる」のように、
    // 型だけでは分からない見せ方をしたいときに使う。最初から SpriteComponent::sprite(切り出しの ID)と
    // SpriteAnimatorComponent::animation(アニメーションの ID)が登録されている。
    // 戻り値: そのプロパティが見つかれば true。同じプロパティにもう一度登録すると置き換える。
    bool RegisterPropertyDrawer(const core::TypeInfo& type, const char* propertyName, PropertyDrawer drawer);

    class SceneInspector
    {
    public:
        // 「World」の窓を作る。上にアクターの一覧、下に選んだアクターのプロパティとコンポーネントを出す。
        // commands があれば、値の書き換えは property.set を通す(Undo できる。ドラッグは手を離したときに1件)。
        // nullptr なら、その場で書き換えるだけ(テストなど)。
        void Draw(engine::Application& app, EditorCommandRegistry* commands = nullptr);

        // 選ぶアクターを決める(テストや、ほかの道具から使う)。どこも指さないハンドルなら、選ばない。
        void Select(core::ObjectHandle actor) { m_selected = actor; }
        core::ObjectHandle GetSelected() const { return m_selected; }
        // 選んでいるアクターの、プレハブとの違いの数(印の数。確認とテスト用)。
        std::size_t GetOverrideCount() const { return m_overrides.size(); }

    private:
        // 選んでいるアクター。弱い参照(Destroy されたら、引くと nullptr になるので、選んでいないのと同じに扱う)。
        core::ObjectHandle m_selected;
        // 値の書き換えを、property.set の1件にまとめる記録係(ドラッグ中の前の値を覚えておく)。
        PropertyEditRecorder m_recorder;

        // 選んでいるアクターの、プレハブとの違いを作り直す(印を付けるため。Prefab::FindOverrides)。
        void RefreshOverrides(const engine::Actor& actor, const core::TypeRegistry& types);
        // プレハブとの違いの(コンポーネントの型の名前、道筋)。アクター自身の値なら、型の名前は空。
        std::vector<std::pair<std::string, std::string>> m_overrides;
        core::ObjectHandle m_overridesFor; // m_overrides を作ったアクター
        int m_overridesAge = 0;            // 作ってから経ったフレームの数(30 で作り直す)
        // 置いた後に足したコンポーネントの、アクターの components の中の位置(PF-7b、7d。見出しの下に "(added)" と出す)
        std::vector<std::size_t> m_addedComponentIndexes;
        // 置いた後に外したコンポーネントの(型の名前、プレハブの中の ID の文字列)。1つずつ Revert ボタンを付ける(PF-7d)
        std::vector<std::pair<std::string, std::string>> m_removedComponents;
        // 置いた後に足した子の(名前、guid)と、消した子の(名前、プレハブの中の ID)。ボタンでコマンドを呼ぶ(HIER)
        std::vector<std::pair<std::string, std::string>> m_addedChildren;
        std::vector<std::pair<std::string, std::string>> m_removedChildren;
        // 選んでいるアクターが入れ子のプレハブから来たものなら、その元のプレハブ(HIER)
        core::Guid m_nestedSource;

        // ---- アクターの木(HIER-3)----
        // 一覧を親子の木で描く。毎フレーム、シーンの一覧から作り直す(入れ物は使い回し、温まった後は確保しない)。
        void BuildTreeRows(engine::World& scene, core::ObjectRegistry& registry);
        // i 番目(シーンの一覧の位置)のアクターと、開いていればその子孫を、行に足す。
        void AppendTreeRows(int i, int depth);
        // 親を付け替える(編集中は actor.setParent を通して Undo できる。使えなければ、その場で付け替えるだけ)。
        void RequestSetParent(engine::Application& app, EditorCommandRegistry* commands, core::ObjectHandle actor,
                              core::ObjectHandle parent);
        // 開いているアクター(ハンドルの場所と世代をつないだ数)。開いたり閉じたりしたときだけ変わる
        std::vector<std::uint64_t> m_expanded;
        bool IsExpanded(core::ObjectHandle actor) const;
        void SetExpanded(core::ObjectHandle actor, bool expanded);
        std::vector<int> m_rowOfSlot;    // ハンドルの場所(index)→ シーンの一覧の位置(-1 は無い)
        std::vector<int> m_firstChild;   // シーンの一覧の位置 → 最初の子の位置(-1 は無い)
        std::vector<int> m_nextSibling;  // シーンの一覧の位置 → 次の兄弟の位置(-1 は無い)
        std::vector<char> m_isRoot;      // シーンの一覧の位置 → 親が無い(か、親が一覧にいない)か
        std::vector<std::pair<int, int>> m_treeRows; // 描く行(シーンの一覧の位置、深さ)。位置が負ならシーンの見出し(SC-4)
        std::vector<const engine::Scene*> m_sceneRows; // 見出しの行のシーン(-1 - 位置 で引く。描く間だけ使う)
        std::vector<std::pair<const engine::Scene*, int>> m_sceneStack; // シーンの木をたどる作業用(深さつき)
        core::ObjectHandle m_revealedFor;            // 先祖を開いて見えるようにした選択(選び直したら、もう一度開く)
        engine::World* m_sceneForRows = nullptr;     // 行を作っている間だけ使う(AppendTreeRows がアクターのハンドルを引くため)
    };
}

#endif
