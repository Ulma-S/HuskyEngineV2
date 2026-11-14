// シーンのアセット(SC-1)。`.scene`(+ `.meta`)のファイルを読み、中身の値の木(シーンのファイルと同じ形)を持つだけ。
// アクターを作るのは、使う側(Application::LoadScene)。プレハブ(PrefabAsset)と同じく、読み込んだ中身は変わらないデータ。
// GUID(AssetRef<SceneAsset>)で指せるので、ゲームのコードやボタンが「次のシーン」をプロパティとして持てる。
//
// 使い方(ゲームスレッド):
//   core::AssetRef<engine::SceneAsset> next;            // プロパティにすれば、インスペクタやファイルで GUID を書ける
//   const engine::SceneAsset* scene = loader.Resolve(next); // 読み終わるまでは状態が Loading
#pragma once

#include "engine/ArchiveValue.h"
#include "engine/Asset.h"
#include "engine/ScriptableData.h"
#include "core/AssetRef.h"

#include <memory>
#include <vector>

namespace husky::engine
{
    class SceneAssetKind;

    class SceneAsset : public Asset
    {
        HUSKY_OBJECT(SceneAsset, Asset)
    public:
        ~SceneAsset() override;

        // シーンのファイルの中身(SceneSerializer::Read に渡せる形)。読み終わるまでは空。
        const ArchiveValue& GetData() const { return m_data; }

    private:
        friend class SceneAssetKind; // 中身を書くのは読み方(SceneAsset.cpp)だけ

        ArchiveValue m_data;
    };

    // シーンの読み方(拡張子 .scene)。Application のコンストラクタが RegisterKind で登録する。
    std::unique_ptr<AssetKind> CreateSceneAssetKind();

    // シーンの一覧(SC-2b。Unity の Build Settings のシーンの一覧にあたる)。ScriptableData のファイル(.sdata)に書く。
    // ゲームはシーンを名前(ファイル名から拡張子を除いたもの)で指し、エンジンはこの一覧から名前で探して読む。
    // 先頭が最初のシーン。名前が重なっていればエラー(後のものは使わない)。Application::LoadSceneList で読む。
    class SceneList : public ScriptableData
    {
        HUSKY_OBJECT(SceneList, ScriptableData)
    public:
        std::vector<core::AssetRef<SceneAsset>> scenes;

        HUSKY_REFLECTION_BEGIN()
            HUSKY_PROPERTY(scenes)
        HUSKY_REFLECTION_END()
    };
}
