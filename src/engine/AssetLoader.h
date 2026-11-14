// アセットのローダ。アセットを読み込む処理は、すべてここを通す(読み込みの入口を1か所にする)。
//
// ローダは種類ごとの違いを知らず、共通の流れだけを持つ。種類ごとの違いは AssetKind(Asset.h)に書き、RegisterKind で登録する。
// 読み込みの流れ。重い部分(3、4)は、ジョブとしてワーカースレッドで行う(非同期):
//   Load(箱の型, guid)                                        … ゲームスレッド
//     1. キャッシュ: 同じ GUID の箱がまだ生きていれば、それを返す(同じファイルを2回読まない)
//     2. GUID → パス: AssetDatabase の表で引く。無ければ、その場で「失敗」
//        箱を「読み込み中」で作り(AssetKind::CreateAsset)、記録(AssetKind::CreateLoad)とジョブを積んで、すぐに返す
//   ジョブ                                                     … ワーカースレッド
//     3. 1段目「元のファイルを読む」(AssetLoad::ReadSource。テクスチャなら PNG を CPU のメモリ上の画像にする)
//     4. 2段目「実行用のデータにする」(AssetLoad::Cook。テクスチャは今は素通し)
//   Update(毎フレーム)                                         … ゲームスレッド
//     5. 終わったジョブを見つけ、結果を箱に書き(AssetKind::Finish。テクスチャなら GPU に送る)、「読めた」か「失敗」にする
//
// ジョブは、GC 管理のオブジェクト(箱)を触らない。触るのは「読み込み中の記録」(AssetLoad)のパスと結果(ただのデータ)だけ。
// 箱に結果を書くのは、ゲームスレッドの Update。
// 読み込み中に箱が GC で回収されても、記録は箱を弱い参照で指しているだけなので、終わったときに捨てるだけ。
//
// Load は必ずハンドルを返す。戻ったときは、ふつう「読み込み中」。状態は Asset::GetState で分かる
// (オブジェクト用のメモリを使い切った・種類が登録されていないときだけは、何も指さないハンドルを返す)。
// テストなどで、読み終わるまで待ちたいときは WaitForAllLoads を呼ぶ。
//
// 描画の準備(RenderDevice)は Run の中でしか用意できないので、それより前にも読めるようにしている:
//   - GPU が無いときに読み終わったテクスチャは、ファイルを読めれば「読めた」にし、GPU のテクスチャは作らない
//   - SetRenderDevice で GPU が付いたら、種類が「読み直しが要る」と答えた箱(GPU のテクスチャを持っていないもの)を、もう一度読む
//   - SetRenderDevice(nullptr) で GPU が外れるときは、種類ごとに GPU のものを手放させる(AssetKind::ReleaseRenderResources)
//
// スプライトシートは、読み終わったら中で指している画像(テクスチャ)の読み込みを始める(依存の読み込み)。
// プレハブは、入れ子のプレハブを先に読み、それを待つ間は「読み込み中」のまま(AssetKind の Update / HasWaiting / FailWaiting)。
// ScriptableData は、箱(ScriptableDataAsset)と中身(ScriptableData の子)が別のオブジェクト(AssetKind::GetContentType、
// ResolveContent)。AssetRef<中身の型> は Resolve で const の中身を返す。
//
// 動作スレッド: ゲームスレッドのみ(中で積んだジョブだけが、ワーカースレッドで動く)
#pragma once

#include "core/AssetRef.h"
#include "core/Guid.h"
#include "core/JobSystem.h"
#include "core/ObjectRegistry.h"
#include "core/TypeInfo.h"
#include "core/TypeRegistry.h"
#include "engine/Asset.h"
#include "engine/AssetDatabase.h"
#include "engine/PrefabAsset.h"
#include "engine/ScriptableData.h"
#include "engine/FontAsset.h"
#include "engine/SoundAsset.h"
#include "engine/SpriteSheetAsset.h"
#include "engine/TextureAsset.h"
#include "render/RenderDevice.h"

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits> // std::enable_if_t, std::is_base_of_v
#include <unordered_map>
#include <vector>

namespace husky::engine
{
    class AssetLoader final
    {
    public:
        // database: GUID からパスを引く表。registry: アセットのオブジェクトを作るところ。jobs: 読み込みのジョブを積むところ。
        // types: 型の登録簿(ScriptableData の中身を、ファイルの型の名前から作る)。
        // どれも借りるだけ。このローダが作ったアセットより先に、このローダを壊さないこと(アセットは、回収されるときに
        // ローダを呼ぶため)。jobs は、このローダより長く生きること(壊すときに、読み込み中のジョブを待つため)。
        AssetLoader(const AssetDatabase& database, core::ObjectRegistry& registry, core::JobSystem& jobs,
                    const core::TypeRegistry& types);
        // 読み込み中のジョブが終わるのを待ってから壊れる(ジョブが、この中の記録に書き込むため)。
        // 結果は捨てる(このとき、レジストリはもう壊れていることがあるので、アセットには触らない)。
        ~AssetLoader();

        AssetLoader(const AssetLoader&) = delete;
        AssetLoader& operator=(const AssetLoader&) = delete;

        // GPU を付ける(nullptr で外す)。付けると、種類が「読み直しが要る」と答えた読み込み済みのアセットを、読み直す。
        // 外すと、種類ごとに GPU のものを手放させる。RenderDevice を壊す前に、必ず nullptr で外すこと。
        void SetRenderDevice(render::RenderDevice* device);

        // 音の層を付ける(nullptr で外す)。付いていれば、読んだ音の波を預ける(SoundAsset::GetSound)。
        // 付いていない(テストなど)ときは、読んで長さだけを覚える。Application がコンストラクタで付ける。
        void SetAudioDevice(audio::AudioDevice* audio) { m_audio = audio; }

        // 読み方が使う道具(借りているもの)。GPU と音の層は、付いていなければ nullptr。
        render::RenderDevice* GetRenderDevice() const { return m_device; }
        audio::AudioDevice* GetAudioDevice() const { return m_audio; }
        core::ObjectRegistry& GetRegistry() const { return m_registry; }
        const core::TypeRegistry& GetTypes() const { return m_types; }
        const AssetDatabase& GetDatabase() const { return m_database; }

        // ---- 種類の登録と、共通の入口(AL-1〜3) ----

        // アセットの種類の読み方を登録する(持ち主はローダになる)。同じ箱の型の種類が先にあれば、登録せずにエラーを出す。
        // 読み込みを始める前に登録すること(エンジンの種類は Application がコンストラクタで登録する)。
        void RegisterKind(std::unique_ptr<AssetKind> kind);
        // type の読み方。type は箱の型か、中身の型(AssetKind::GetContentType)。その子でもよい。登録されていなければ nullptr。
        const AssetKind* FindKind(const core::TypeInfo& type) const;
        // relativePath のファイルを、type(箱か中身の型)のアセットとして読めるか(種類に聞く。拡張子で見分ける)。
        // インスペクタが、選べるアセットを絞り込むのに使う。種類が登録されていなければ false。
        bool CanLoadAs(const core::TypeInfo& type, std::string_view relativePath) const;

        // type(箱か中身の型)のアセットの読み込みを始め、箱を指すハンドル(弱い参照)を返す(ふつうは「読み込み中」)。
        // 同じ GUID の箱がまだ生きていれば、それを返す。種類が登録されていない・メモリが足りないときは、何も指さないハンドル。
        core::ObjectHandle Load(const core::TypeInfo& type, const core::Guid& guid);
        // アセットの参照が指す箱を返す(type として。テンプレートを使わない入口)。何も指していなければ nullptr。
        // まだ読んでいない、または指す GUID が変わったときは、ここで読み始め、箱のハンドルを reference.asset に書く。
        Asset* ResolveReference(core::AssetReference& reference, const core::TypeInfo& type);
        // 箱と中身が別の種類(ScriptableData)で、参照が指す中身を expected の型(か、その子)として返す。
        // 読み終わっていない・失敗・型が違うなら nullptr。テンプレートを使わない入口(スクリプトからも同じ形で呼べる)。
        const core::Object* ResolveContent(core::AssetReference& reference, const core::TypeInfo& expected);

        // 型を名前で書く入口(C++ から使うとき。中身は上の関数): loader.Load<TextureAsset>(guid)、
        // loader.Resolve<TextureAsset>(handle)(ハンドルが指す箱。もう無い・型が違うなら nullptr)。
        template <typename T>
        core::ObjectHandle Load(const core::Guid& guid)
        {
            return Load(T::StaticType(), guid);
        }
        template <typename T>
        T* Resolve(core::ObjectHandle handle) const
        {
            return m_registry.Resolve<T>(handle);
        }

        // AssetRef<T>(T は Asset の子 = 箱)の入口。中身は ResolveReference。
        template <typename T, std::enable_if_t<std::is_base_of_v<Asset, T>, int> = 0>
        T* Resolve(core::AssetRef<T>& reference)
        {
            // 型は ResolveReference が IsA で確かめてあるので、static_cast で子の型に変えてよい。
            return static_cast<T*>(ResolveReference(reference, T::StaticType()));
        }
        // AssetRef<T>(T が箱でない Object の子 = 中身。ScriptableData の子など)の入口。中身は ResolveContent。
        // 読み込んだ中身は変わらないデータなので const で返す。
        template <typename T,
                  std::enable_if_t<!std::is_base_of_v<Asset, T> && std::is_base_of_v<core::Object, T>, int> = 0>
        const T* Resolve(core::AssetRef<T>& reference)
        {
            // 型は種類の ResolveContent が IsA で確かめてあるので、static_cast で子の型に変えてよい(RTTI は使わない)。
            return static_cast<const T*>(ResolveContent(reference, T::StaticType()));
        }

        // ---- キャッシュ(読み方と、種類だけの操作(ReloadPrefab など)が使う) ----

        // キャッシュにある(まだ回収されていない)type の箱の数。確認とテスト用。
        std::size_t GetLoadedCount(const core::TypeInfo& type) const;
        template <typename T>
        std::size_t GetLoadedCount() const
        {
            return GetLoadedCount(T::StaticType());
        }
        // キャッシュにある guid の箱(無ければ何も指さないハンドル)。読み込みは始めない。
        core::ObjectHandle FindCached(const core::TypeInfo& type, const core::Guid& guid) const;
        // guid をキャッシュから外す(箱そのものは、指している人がいる間は残る)。次の Load は新しい箱を作って読む(読み直し)。
        void ForgetCached(const core::TypeInfo& type, const core::Guid& guid);
        // type のキャッシュの (GUID, 箱のハンドル) をすべて回す。回している間に Load / ForgetCached を呼ばないこと
        // (キャッシュが変わるため。外したいものは先に集める)。
        template <typename Fn>
        void ForEachCached(const core::TypeInfo& type, Fn&& fn) const
        {
            if (const KindEntry* entry = FindKindEntry(type))
            {
                for (const auto& cached : entry->cache)
                {
                    fn(cached.first, cached.second);
                }
            }
        }

        // 毎フレーム1回、ゲームスレッドで呼ぶ。読み終わったジョブの結果をアセットに書く(GPU に送る)。
        // ワーカーが 0 本(同期実行)のときは、積まれている読み込みのジョブも、ここで実行する。
        void Update();

        // 読み込み中のもの(ほかのアセットを待っているものも)が、すべて終わるまで待ち、結果をアセットに書く。
        // テストや、読み込み画面などで使う。
        void WaitForAllLoads();

        // 読み込み中(ジョブが終わっていないか、結果をまだアセットに書いていない)の数。確認とテスト用。
        std::size_t GetPendingLoadCount() const { return m_pending.size(); }

    private:
        friend class Asset; // 回収されるときに OnAssetDestroyed / ReleaseAssetData を呼べるようにする

        // 登録した種類と、そのキャッシュ。
        struct KindEntry
        {
            std::unique_ptr<AssetKind> kind;
            std::unordered_map<core::Guid, core::ObjectHandle> cache; // GUID → 箱(弱い参照のキャッシュ)
        };

        // 読み込み中の記録(ローダの側)。ジョブとゲームスレッドの受け渡し場所。
        // ジョブが書くのは load の中(種類が足した結果と succeeded)だけ。ゲームスレッドは counter が「終わった」になってから読む
        // (JobCounter の release / acquire で、ジョブが書いた値が見える)。それまで、どちらも触らない。
        struct PendingLoad
        {
            core::ObjectHandle asset;        // 結果を書く先(弱い参照。読み込み中に回収されたら、結果は捨てる)
            core::Guid guid;                 // 書く先が、途中で別のアセットに替わっていないかの確かめ用
            AssetKind* kind = nullptr;       // 読み方
            std::unique_ptr<AssetLoad> load; // 種類が作った記録(パスと結果)
            core::JobCounter counter;        // ジョブの完了の印
        };

        KindEntry* FindKindEntry(const core::TypeInfo& type);
        const KindEntry* FindKindEntry(const core::TypeInfo& type) const;
        // 読むファイルのパスを表で引く。無ければ空(呼んだ側が、その場で「失敗」にする)。
        std::string FindPathOrLog(const core::Guid& guid) const;
        void StartLoad(Asset& asset);
        void FinishLoad(PendingLoad& pending);
        void ReleaseAssetData(Asset& asset); // 箱の子のデストラクタから(種類の片付け)
        void OnAssetDestroyed(Asset& asset); // 箱のデストラクタから(キャッシュから外す)
        // 種類の待ちを進める(AssetKind::Update)。1つでも進めば true。
        bool UpdateKinds();

        const AssetDatabase& m_database;
        core::ObjectRegistry& m_registry;
        const core::TypeRegistry& m_types; // ScriptableData の中身を、ファイルの型の名前から作るため
        core::JobSystem& m_jobs;
        render::RenderDevice* m_device = nullptr; // 借りているだけ(持ち主は Application::Run)
        audio::AudioDevice* m_audio = nullptr;    // 借りているだけ(持ち主は Application)

        // 読み込み中の記録。ジョブがアドレスを覚えているので、記録そのものは動かないように unique_ptr で持つ
        // (vector が大きくなって中身が引っ越しても、記録の場所は変わらない)。
        std::vector<std::unique_ptr<PendingLoad>> m_pending;

        // 登録した種類(数は少ないので、探すときは順に見る)。
        std::vector<KindEntry> m_kinds;
    };
}
