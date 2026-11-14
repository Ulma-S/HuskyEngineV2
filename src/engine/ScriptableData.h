// ScriptableData: ゲームが作れる「保存できるデータのアセット」(Unity の ScriptableObject にあたる)。SD-1。
//
// ゲームは ScriptableData を継承し、HUSKY_PROPERTY で値を並べる:
//   class BouncerSettings : public engine::ScriptableData
//   {
//       HUSKY_OBJECT(BouncerSettings, engine::ScriptableData)
//   public:
//       float speed = 100.0f;
//       HUSKY_REFLECTION_BEGIN()
//           HUSKY_PROPERTY(speed)
//       HUSKY_REFLECTION_END()
//   };
//   types.Register<BouncerSettings>();   // ファイルの "type" の名前から作れるように、登録簿に入れる
//
// ファイルは .sdata(+ .meta)。中身は JSON で、ObjectSerializer の1つのオブジェクトの形そのもの:
//   { "type": "BouncerSettings", "properties": { "speed": 120 } }
//
// 使う側は、プロパティの AssetRef で指し、ローダで引く:
//   core::AssetRef<BouncerSettings> settings;                  // HUSKY_PROPERTY(settings) で保存され、GC からも守られる
//   const BouncerSettings* s = loader.Resolve(settings);      // 読み終わるまでは nullptr
//
// 読み込んだものは変わらないデータ(読むだけ)なので、const のポインタで渡す(書き込みの検知は SD-2)。
//
// ---- 箱と中身の 2 つのオブジェクトに分ける理由 ----
// どの型を作るかは、ファイルの "type" を読むまで分からない。一方、ローダの Load は読み始めたその場でハンドルを返す決め事。
// そこで、ローダは先に型の決まった「箱」(ScriptableDataAsset。GUID と状態を持つ)を作って返し、
// ファイルを読み終わってから、名前で登録簿から「中身」(ScriptableData の子の型)を作り、箱に持たせる。
// こうすると AssetRef<WeaponData> が、子の型(SwordData)のファイルを指すこともできる。
//
// ---- スクリプト言語でも使える形 ----
// 型は名前で登録簿(TypeRegistry)から作り、値は必ず Property を通して入れる(ObjectSerializer)。
// C++ のテンプレート(AssetRef<T>、Resolve<T>)は表側の便利な入口だけで、中身は「GUID + 期待する型の TypeInfo」で動く
// (AssetLoader::ResolveScriptableData)。将来スクリプトの型を足しても、この形のまま使える。
//
// 動作スレッド: ゲームスレッドのみ
#pragma once

#include "core/Guid.h"
#include "core/Object.h"
#include "core/TypeInfo.h"
#include "engine/ArchiveValue.h"
#include "engine/Asset.h"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace husky::engine
{
    class ScriptableDataAssetKind;

    // ゲームが継承するデータの基底クラス。中身(値)は子の型の HUSKY_PROPERTY だけ。
    // 型の登録簿には入れない(値を持たないので、ファイルから作る意味が無い)。
    class ScriptableData : public core::Object
    {
        HUSKY_OBJECT(ScriptableData, core::Object)
    public:
        // 読み込んだファイルの GUID(ローダが作ったものだけ。コードで作ったものは null)。エラーの表示などに使う。
        const core::Guid& GetAssetGuid() const { return m_assetGuid; }

    private:
        friend class ScriptableDataAssetKind; // 書くのは読み方(ScriptableData.cpp)だけ
        core::Guid m_assetGuid;
    };

    // ローダが作る「箱」。GUID、読み込みの状態、中身を指すハンドルを持つ。作るのは読み方(ScriptableDataAssetKind)だけ。
    // 中身は箱のプロパティ(強い参照)なので、箱が生きている間は中身も回収されない。
    class ScriptableDataAsset : public Asset
    {
        HUSKY_OBJECT(ScriptableDataAsset, Asset)
    public:
        // 回収されるとき、ローダのキャッシュから外れる(Asset のデストラクタ)。
        ~ScriptableDataAsset() override;

        // 中身(ScriptableData の子)を指すハンドル。読み終わる(IsLoaded)までは null。引くのは GetScriptableData。
        core::ObjectHandle GetDataHandle() const { return m_data; }

    private:
        friend class ScriptableDataAssetKind; // 中身を書くのは読み方だけ

        core::ObjectHandle m_data;                 // 中身(プロパティとして登録するので、GC がたどる強い参照)
        const core::TypeInfo* m_warnedType = nullptr; // 「期待する型と違う」をもう出した型(毎フレーム同じ警告を出さないため)
        // ファイルを書き直して読み直した(ReloadScriptableData)後の古い箱なら true。
        // 指している AssetRef は、次の Resolve で新しい箱に乗り換える(新しい箱が読み終わるまでは、古い中身を返す)。
        bool m_superseded = false;
        // 読み込んだときの中身の値の控え(SD-2。開発ビルドだけ書く。製品では空のまま)。
        // メンバーそのものは #if で外さない(定義が構成で違うと、ターゲットごとにクラスの形が食い違うおそれがあるため)。
        ArchiveValue m_snapshot;

    public:
        HUSKY_REFLECTION_BEGIN()
            HUSKY_PROPERTY(m_data)
        HUSKY_REFLECTION_END()
    };

    // ScriptableData を .sdata の形({"type", "properties"})にする。中身は ObjectSerializer::Write。
    // ほかのオブジェクト(アクターなど)を指すハンドルのプロパティは書かない(ファイルの外の番号は意味が無い)。
    // 書かなかったものは outWarnings に名前を残す(渡したときだけ)。AssetRef は GUID で書かれる。
    ArchiveValue WriteScriptableData(const ScriptableData& data, std::vector<std::string>* outWarnings = nullptr);

    // ---- 読み方と、ScriptableData だけの操作(AL-3。中は AssetLoader の共通の入口を使う) ----

    // .sdata の読み方を作る。ローダに RegisterKind で登録する。箱の型は ScriptableDataAsset、中身の型は ScriptableData。
    // ジョブがファイルを読んで JSON を値の木にし、Finish でファイルの "type" の名前から中身を作り、値を入れる。
    // 型が登録簿に無い・ScriptableData の子でない・値の木の形が違うときは「失敗」にし、理由をログに出す。
    std::unique_ptr<AssetKind> CreateScriptableDataAssetKind();

    // 箱の中身。読み終わっていなければ nullptr。
    const ScriptableData* GetScriptableData(const AssetLoader& loader, const ScriptableDataAsset& asset);

    // ファイルを書き直した .sdata を、新しい箱として読み直す(読み込んだデータは変わらないので、その場では書き換えない)。
    // 古い箱を指している AssetRef は、次の Resolve で新しい箱に乗り換える(新しい箱が読み終わるまでは古い中身のまま)。
    core::ObjectHandle ReloadScriptableData(AssetLoader& loader, const core::Guid& guid);

    // 中身の型が types のどれか(か、その子)の ScriptableData を、キャッシュから外して中身を手放す(HR-3)。
    // ゲームの DLL を外す前に、DLL の中の型のオブジェクトを残さないため。古い箱には読み直しと同じ印を付けるので、
    // 指している AssetRef は次の Resolve で GUID から読み直す(型を登録し直した後なら、新しい型で作られる)。
    // 戻り値: 外した数。
    std::size_t ForgetScriptableData(AssetLoader& loader, const std::vector<const core::TypeInfo*>& types);

    // 読み込んだ中身が、読み込んだときの値から変わっていないかを確かめる(SD-2。開発ビルドだけ。製品では何もせず 0)。
    // 中身には読むだけの印が付き、書く入口は断るが、C++ の生のポインタ(const_cast など)からの書き込みは止められない。
    // そこで読み込んだときの値を控えておき、ここで比べる。変わっていたら、ファイル・型・プロパティの名前つきでエラーを出し、
    // 変わったプロパティの数を返す(同じ書き換えを2回出さないように、控えは今の値にする)。
    // Application がフレームの終わりに1秒に1回、エディタが Stop のときに呼ぶ。
    std::size_t CheckScriptableDataUnchanged(AssetLoader& loader);
}
