// プレハブのアセット: 「アクター + コンポーネント + 値」の型紙(Prefab.h)を、ファイル(.prefab、中身は JSON)にしたもの。
// GC 管理のオブジェクト。隣の .meta の GUID で指す(テクスチャやスプライトシートと同じ)。
//
//   assets/prefabs/bouncer.prefab   ← 中身は Prefab::Write の値をそのまま JSON にしたもの(シーンのファイルと同じ形、アクター1つ)
//   assets/prefabs/bouncer.prefab.meta
//
// 持つのは、読んだ値の木(ArchiveValue)だけ。アクターは作らない(作るのは Prefab::Instantiate で、置くたびに新しく作る)。
// 値の木は「ただのデータ」なので、ファイルを読むところから JSON を値の木にするところまで、ジョブ(ワーカースレッド)でできる。
//
// 作るのは AssetLoader だけ(AssetLoader::LoadPrefab)。
// 中のコンポーネントが指すテクスチャやスプライトシートは、ここでは読まない(置いたアクターが描かれるときに読まれる)。
//
// 動作スレッド: ゲームスレッドのみ
#pragma once

#include "core/Guid.h"
#include "core/Object.h"
#include "engine/ArchiveValue.h"
#include "engine/AssetState.h"

namespace husky::engine
{
    class AssetLoader;

    class PrefabAsset : public core::Object
    {
        HUSKY_OBJECT(PrefabAsset, core::Object)
    public:
        // 回収されるとき、ローダのキャッシュから外してもらう。
        ~PrefabAsset() override;

        const core::Guid& GetGuid() const { return m_guid; }
        AssetState GetState() const { return m_state; }
        bool IsLoaded() const { return m_state == AssetState::Loaded; }

        // プレハブの中身(Prefab::Instantiate に渡す値)。読み終わる(IsLoaded)までは Null。
        const ArchiveValue& GetData() const { return m_data; }

    private:
        friend class AssetLoader; // 中身を書くのは AssetLoader だけ

        core::Guid m_guid;
        AssetState m_state = AssetState::Loading;
        ArchiveValue m_data;
        AssetLoader* m_loader = nullptr; // 作ったローダ(借りているだけ)
    };
}
