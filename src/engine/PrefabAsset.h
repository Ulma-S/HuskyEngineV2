// プレハブのアセット: 「アクター + コンポーネント + 値」の型紙(Prefab.h)を、ファイル(.prefab、中身は JSON)にしたもの。
// GC 管理のオブジェクト。隣の .meta の GUID で指す(テクスチャやスプライトシートと同じ)。
//
//   assets/prefabs/bouncer.prefab   ← 中身は Prefab::Write の値をそのまま JSON にしたもの(シーンのファイルと同じ形、アクター1つ)
//   assets/prefabs/bouncer.prefab.meta
//
// 持つのは、読んだ値の木(ArchiveValue)だけ。アクターは作らない(作るのは Prefab::Instantiate で、置くたびに新しく作る)。
// 値の木は「ただのデータ」なので、ファイルを読むところから JSON を値の木にするところまで、ジョブ(ワーカースレッド)でできる。
//
// 作るのは読み方(PrefabAssetKind)だけ。読むのは loader.Load<PrefabAsset>(guid) か Resolve(AssetRef<PrefabAsset>&)。
// 中のコンポーネントが指すテクスチャやスプライトシートは、ここでは読まない(置いたアクターが描かれるときに読まれる)。
//
// 動作スレッド: ゲームスレッドのみ
#pragma once

#include "engine/ArchiveValue.h"
#include "engine/Asset.h"

#include <cstddef>
#include <memory>

namespace husky::engine
{
    class PrefabAssetKind;

    class PrefabAsset : public Asset
    {
        HUSKY_OBJECT(PrefabAsset, Asset)
    public:
        // 回収されるとき、ローダのキャッシュから外れる(Asset のデストラクタ)。
        ~PrefabAsset() override;

        // プレハブの中身(Prefab::Instantiate に渡す値)。読み終わる(IsLoaded)までは Null。
        // 入れ子のプレハブ(別のプレハブから置いたもの)は開いた形: 全部の値を持ち、元のプレハブとのつながりは無く、
        // ID はこのプレハブの中の ID(HIER)。置く・比べる・子の出し入れは、この形を相手にする。
        const ArchiveValue& GetData() const { return m_data; }
        // ファイルに書いてあるとおりの値(入れ子のプレハブは、元のプレハブとの違いだけ)。Apply でファイルを書き直すときに使う。
        // 入れ子が無ければ GetData と同じ。
        const ArchiveValue& GetRawData() const { return m_raw; }

    private:
        friend class PrefabAssetKind; // 中身を書くのは読み方(PrefabAsset.cpp)だけ

        ArchiveValue m_data;
        ArchiveValue m_raw;
    };

    // プレハブ(.prefab)の読み方を作る。ローダに RegisterKind で登録する(AL-3)。
    // 入れ子のプレハブがあれば、先にそれを読み、読み終わってから開いた形を作る(待っている間は「読み込み中」)。
    std::unique_ptr<AssetKind> CreatePrefabAssetKind();

    // ファイルを書き直したプレハブを、新しいアセットとして読み直す(読み込んだアセットは変わらないデータなので、その場では書き換えない)。
    // 次からの Load は新しいアセットを返す。古いアセットは、指している人(置いたものの prefab.asset)がいる間は残る。
    core::ObjectHandle ReloadPrefab(AssetLoader& loader, const core::Guid& guid);
    // guid のプレハブを入れ子に持つ、読み込んだプレハブ(その外側も)を読み直す(中のプレハブを直したら、外側の開いた形も
    // 作り直すため)。読み直したものの数を返す。読み終わるのを待つのは呼ぶ側(WaitForAllLoads)。
    std::size_t ReloadPrefabsNesting(AssetLoader& loader, const core::Guid& guid);
}
