// アセットの共通の土台(AL-1)。
//
// ローダ(AssetLoader)は、アセットの種類ごとの違いを知らずに、共通の流れだけを持つ:
//   キャッシュ(同じ GUID は同じ箱)→ 箱を「読み込み中」で作る → ジョブを積む → Update で結果を箱に書く → 待つ。
// 種類ごとの違い(どの箱を作るか、どの拡張子を読めるか、ジョブで何をするか、結果をどう箱に書くか、回収のときの片付け)は、
// AssetKind の子に書き、ローダに RegisterKind で登録する。新しい種類を足すときは、ローダを直さずに済む。
//
// 3つの部品:
//   - Asset     … 箱(GC 管理のオブジェクト)。GUID・状態・作ったローダ・種類を持つ。TextureAsset などはこれを継承する。
//   - AssetLoad … 読み込み中の記録(ただのデータ)。ジョブ(ワーカースレッド)が 1段目・2段目を行い、結果を自分の中に書く。
//                 GC 管理のものは触らない。種類ごとに継承して、結果のメンバーを足す。
//   - AssetKind … 1種類の読み方。上の2つを作り、ゲームスレッドで結果を箱に書く。
//
// 動作スレッド: Asset と AssetKind はゲームスレッドのみ。AssetLoad の ReadSource / Cook だけはワーカースレッド。
#pragma once

#include "core/Guid.h"
#include "core/Object.h"
#include "core/TypeInfo.h"
#include "engine/AssetState.h"

#include <memory>
#include <string>
#include <string_view>

namespace husky::core
{
    class ObjectRegistry;
}

namespace husky::engine
{
    class AssetKind;
    class AssetLoader;

    // アセットの箱の共通の土台。直接は作らない(種類ごとの子の型を、AssetKind::CreateAsset が作る)。
    class Asset : public core::Object
    {
        HUSKY_OBJECT(Asset, core::Object)
    public:
        // 回収されるとき、ローダのキャッシュから外れる(AssetLoader.cpp)。
        ~Asset() override;

        const core::Guid& GetGuid() const { return m_guid; }
        AssetState GetState() const { return m_state; }
        bool IsLoaded() const { return m_state == AssetState::Loaded; }
        // この箱の読み方。ローダが作った箱でなければ nullptr。
        const AssetKind* GetKind() const { return m_kind; }

    protected:
        // 種類ごとの片付け(AssetKind::Release)を呼ぶ。子の型のデストラクタの最初で呼ぶこと。
        // C++ では、基底(Asset)のデストラクタが動くときには、子の型のメンバーはもう壊れている。
        // そのため、子のメンバー(音の番号、GPU のテクスチャなど)を片付けるのは、子のデストラクタからにする。
        void ReleaseLoadedData();

    private:
        friend class AssetLoader; // 書くのはローダだけ

        core::Guid m_guid;
        AssetState m_state = AssetState::Loading;
        AssetLoader* m_loader = nullptr;   // 作ったローダ(借りているだけ)
        const AssetKind* m_kind = nullptr; // 読み方(ローダが持つ。借りているだけ)
        bool m_released = false;           // ReleaseLoadedData を呼んだか(2回片付けない)
    };

    // 読み込み中の記録。ジョブとゲームスレッドの受け渡し場所。種類ごとに継承し、ジョブが書く結果を足す。
    // ジョブが書くのは、子の型で足した結果と succeeded だけ。ゲームスレッドは、ジョブが終わってから読む。
    struct AssetLoad
    {
        virtual ~AssetLoad() = default; // 子の型を、親のポインタ(unique_ptr<AssetLoad>)で壊すため

        // 1段目「元のファイルを読む」(ワーカースレッド)。path を読み、結果を自分の中に書く。読めなければ false。
        virtual bool ReadSource() = 0;
        // 2段目「実行用のデータにする」(ワーカースレッド。1段目が読めたときだけ)。既定は素通し。
        virtual bool Cook() { return true; }

        core::Guid guid;        // ゲームスレッドが積む前に書く: 読むアセット
        std::string path;       // ゲームスレッドが積む前に書く: 読むファイル(表はジョブから触らない)
        bool succeeded = false; // ジョブが書く: 1段目と2段目が通ったか
    };

    // 1種類のアセットの読み方。ローダが持ち(RegisterKind)、ローダより先には壊れない。
    // 中は状態を持たない(持つなら、ゲームスレッドだけで触る)。
    class AssetKind
    {
    public:
        virtual ~AssetKind() = default;

        // 箱の型(AssetRef<T> の T。Load(型, GUID) はこれで種類を探す)。
        virtual const core::TypeInfo& GetAssetType() const = 0;
        // ログに出す名前("音" など)。文字列の定数。
        virtual const char* GetDisplayName() const = 0;
        // relativePath のファイルを、この種類として読めるか(拡張子で見分ける)。
        virtual bool CanLoad(std::string_view relativePath) const = 0;

        // 箱を作る(中身は空。GUID などはローダが書く)。メモリが足りなければ nullptr。
        virtual Asset* CreateAsset(core::ObjectRegistry& registry) const = 0;
        // 読み込み中の記録を作る(ゲームスレッド。guid と path はローダが書く)。
        virtual std::unique_ptr<AssetLoad> CreateLoad(const Asset& asset) const = 0;
        // ジョブが終わった結果を箱に書く(ゲームスレッド)。読めたら true(ローダが「読めた」にする)、だめなら false(「失敗」)。
        // ジョブが失敗した(load.succeeded が false)ときも呼ぶので、理由をログに出すのはここ。
        // 箱がもう無い・作り直された・読み込み中でないときは、ローダが呼ばずに捨てる。
        virtual bool Finish(AssetLoader& loader, Asset& asset, AssetLoad& load) const = 0;
        // 箱が回収されるときの片付け(子の型のデストラクタから、ReleaseLoadedData を通して呼ばれる)。
        virtual void Release(AssetLoader& loader, Asset& asset) const
        {
            (void)loader;
            (void)asset;
        }
    };
}
