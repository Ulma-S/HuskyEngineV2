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
#include "engine/ArchiveValue.h"
#include "engine/AssetState.h"

#include <string>
#include <vector>

namespace husky::engine
{
    class AssetLoader;

    // ゲームが継承するデータの基底クラス。中身(値)は子の型の HUSKY_PROPERTY だけ。
    // 型の登録簿には入れない(値を持たないので、ファイルから作る意味が無い)。
    class ScriptableData : public core::Object
    {
        HUSKY_OBJECT(ScriptableData, core::Object)
    public:
        // 読み込んだファイルの GUID(ローダが作ったものだけ。コードで作ったものは null)。エラーの表示などに使う。
        const core::Guid& GetAssetGuid() const { return m_assetGuid; }

    private:
        friend class AssetLoader; // 書くのはローダだけ
        core::Guid m_assetGuid;
    };

    // ローダが作る「箱」。GUID、読み込みの状態、中身を指すハンドルを持つ。作るのは AssetLoader::LoadScriptableData だけ。
    // 中身は箱のプロパティ(強い参照)なので、箱が生きている間は中身も回収されない。
    class ScriptableDataAsset : public core::Object
    {
        HUSKY_OBJECT(ScriptableDataAsset, core::Object)
    public:
        // 回収されるとき、ローダのキャッシュから外してもらう。
        ~ScriptableDataAsset() override;

        const core::Guid& GetGuid() const { return m_guid; }
        AssetState GetState() const { return m_state; }
        bool IsLoaded() const { return m_state == AssetState::Loaded; }
        // 中身(ScriptableData の子)を指すハンドル。読み終わる(IsLoaded)までは null。引くのは AssetLoader::GetScriptableData。
        core::ObjectHandle GetDataHandle() const { return m_data; }

    private:
        friend class AssetLoader; // 中身を書くのはローダだけ

        core::Guid m_guid;
        AssetState m_state = AssetState::Loading;
        core::ObjectHandle m_data;                 // 中身(プロパティとして登録するので、GC がたどる強い参照)
        AssetLoader* m_loader = nullptr;           // 作ったローダ(借りているだけ)
        const core::TypeInfo* m_warnedType = nullptr; // 「期待する型と違う」をもう出した型(毎フレーム同じ警告を出さないため)
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
}
