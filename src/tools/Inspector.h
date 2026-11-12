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

namespace husky::tools
{
    // 型 type のプロパティを、container(その型のオブジェクトか構造体の先頭のアドレス)について、編集できる形で並べる。
    // 値は Property を通して読み書きする(オフセットを直接触らない)。ハンドルは、指している相手の名前を表示するだけ。
    // registry: ハンドルが指す相手を引くのに使う。
    // assets: アセットの参照(AssetRef)を選ぶときの、アセットの一覧。nullptr なら、GUID を表示するだけで選べない。
    // 戻り値: このフレームで、どれかの値が書き換えられたら true。
    bool DrawProperties(const core::TypeInfo& type, void* container, core::ObjectRegistry& registry,
                        const engine::AssetDatabase* assets = nullptr);

    class SceneInspector
    {
    public:
        // 「Scene」の窓を作る。上にアクターの一覧、下に選んだアクターのプロパティとコンポーネントを出す。
        void Draw(engine::Application& app);

        // 選ぶアクターを決める(テストや、ほかの道具から使う)。どこも指さないハンドルなら、選ばない。
        void Select(core::ObjectHandle actor) { m_selected = actor; }
        core::ObjectHandle GetSelected() const { return m_selected; }

    private:
        // 選んでいるアクター。弱い参照(Destroy されたら、引くと nullptr になるので、選んでいないのと同じに扱う)。
        core::ObjectHandle m_selected;
    };
}

#endif
