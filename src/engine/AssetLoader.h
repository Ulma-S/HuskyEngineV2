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
// プレハブ(LoadPrefab)と ScriptableData(LoadScriptableData)は、まだ読み込み中の記録の型(PendingLoad の子)と
// Start〜 / Finish〜 の組で書いてある(AL-3 で AssetKind に移す)。
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
#include "platform/Image.h"
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

        // GPU を付ける(nullptr で外す)。付けると、GPU のテクスチャを持っていない読み込み済みのアセットを、読み直す。
        // 外すと、すべての GPU のテクスチャを手放す。RenderDevice を壊す前に、必ず nullptr で外すこと。
        void SetRenderDevice(render::RenderDevice* device);

        // 音の層を付ける(nullptr で外す)。付いていれば、読んだ音の波を預ける(SoundAsset::GetSound)。
        // 付いていない(テストなど)ときは、読んで長さだけを覚える。Application がコンストラクタで付ける。
        void SetAudioDevice(audio::AudioDevice* audio) { m_audio = audio; }

        // 付いている GPU と音の層(読み方が、結果を箱に書くときに使う)。付いていなければ nullptr。
        render::RenderDevice* GetRenderDevice() const { return m_device; }
        audio::AudioDevice* GetAudioDevice() const { return m_audio; }

        // ---- 種類の登録と、共通の入口(AL-1) ----

        // アセットの種類の読み方を登録する(持ち主はローダになる)。同じ箱の型の種類が先にあれば、登録せずにエラーを出す。
        // 読み込みを始める前に登録すること(エンジンの種類は Application がコンストラクタで登録する)。
        void RegisterKind(std::unique_ptr<AssetKind> kind);
        // assetType(箱の型。その子でもよい)の読み方。登録されていなければ nullptr。
        const AssetKind* FindKind(const core::TypeInfo& assetType) const;

        // assetType のアセットの読み込みを始め、箱を指すハンドル(弱い参照)を返す(ふつうは「読み込み中」)。
        // 同じ GUID の箱がまだ生きていれば、それを返す。種類が登録されていない・メモリが足りないときは、何も指さないハンドル。
        core::ObjectHandle Load(const core::TypeInfo& assetType, const core::Guid& guid);
        // アセットの参照が指す箱を返す(assetType として。テンプレートを使わない入口)。何も指していなければ nullptr。
        // まだ読んでいない、または指す GUID が変わったときは、ここで読み始め、箱のハンドルを reference.asset に書く。
        Asset* ResolveReference(core::AssetReference& reference, const core::TypeInfo& assetType);

        // AssetRef<T>(T は Asset の子)の入口。中身は ResolveReference。
        template <typename T, std::enable_if_t<std::is_base_of_v<Asset, T>, int> = 0>
        T* Resolve(core::AssetRef<T>& reference)
        {
            // 型は ResolveReference が IsA で確かめてあるので、static_cast で子の型に変えてよい。
            return static_cast<T*>(ResolveReference(reference, T::StaticType()));
        }

        // キャッシュにある(まだ回収されていない)assetType の箱の数。確認とテスト用。
        std::size_t GetLoadedCount(const core::TypeInfo& assetType) const;

        // 毎フレーム1回、ゲームスレッドで呼ぶ。読み終わったジョブの結果をアセットに書く(GPU に送る)。
        // ワーカーが 0 本(同期実行)のときは、積まれている読み込みのジョブも、ここで実行する。
        void Update();

        // 読み込み中のものが、すべて終わるまで待ち、結果をアセットに書く。テストや、読み込み画面などで使う。
        void WaitForAllLoads();

        // ---- 古い名前の入口(AL-4 で外す) ----
        // 中身は共通の Load と、レジストリの Resolve。AssetRef<T> は、上の共通の Resolve(テンプレート)が受ける。

        core::ObjectHandle LoadTexture(const core::Guid& guid) { return Load(TextureAsset::StaticType(), guid); }
        TextureAsset* ResolveTexture(core::ObjectHandle handle) const { return m_registry.Resolve<TextureAsset>(handle); }
        std::size_t GetLoadedTextureCount() const { return GetLoadedCount(TextureAsset::StaticType()); }

        core::ObjectHandle LoadSound(const core::Guid& guid) { return Load(SoundAsset::StaticType(), guid); }
        SoundAsset* ResolveSound(core::ObjectHandle handle) const { return m_registry.Resolve<SoundAsset>(handle); }
        std::size_t GetLoadedSoundCount() const { return GetLoadedCount(SoundAsset::StaticType()); }

        core::ObjectHandle LoadFont(const core::Guid& guid) { return Load(FontAsset::StaticType(), guid); }
        FontAsset* ResolveFont(core::ObjectHandle handle) const { return m_registry.Resolve<FontAsset>(handle); }
        std::size_t GetLoadedFontCount() const { return GetLoadedCount(FontAsset::StaticType()); }

        core::ObjectHandle LoadSpriteSheet(const core::Guid& guid) { return Load(SpriteSheetAsset::StaticType(), guid); }
        SpriteSheetAsset* ResolveSpriteSheet(core::ObjectHandle handle) const
        {
            return m_registry.Resolve<SpriteSheetAsset>(handle);
        }
        std::size_t GetLoadedSpriteSheetCount() const { return GetLoadedCount(SpriteSheetAsset::StaticType()); }

        // ---- プレハブ ----

        // プレハブ(.prefab)の読み込みを始め、PrefabAsset を指すハンドル(弱い参照)を返す。
        // ジョブがファイルを読んで JSON を値の木にし、Update で値をアセットに移す。
        core::ObjectHandle LoadPrefab(const core::Guid& guid);
        PrefabAsset* ResolvePrefab(core::ObjectHandle handle) const;
        // テクスチャの Resolve と同じ(まだ読んでいなければ読み始め、reference.asset に書く)。
        PrefabAsset* Resolve(core::AssetRef<PrefabAsset>& reference);
        // ファイルを書き直したプレハブを、新しいアセットとして読み直す(読み込んだアセットは変わらないデータなので、その場では書き換えない)。
        // 次からの LoadPrefab は新しいアセットを返す。古いアセットは、指している人(置いたものの prefab.asset)がいる間は残る。
        core::ObjectHandle ReloadPrefab(const core::Guid& guid);
        // guid のプレハブを入れ子に持つ、読み込んだプレハブ(その外側も)を読み直す(HIER。中のプレハブを直したら、外側の開いた形も
        // 作り直すため)。読み直したものの数を返す。読み終わるのを待つのは呼ぶ側(WaitForAllLoads)。
        std::size_t ReloadPrefabsNesting(const core::Guid& guid);

        // ---- ScriptableData(.sdata。ScriptableData.h) ----

        // .sdata の読み込みを始め、箱(ScriptableDataAsset)を指すハンドル(弱い参照)を返す。
        // ジョブがファイルを読んで JSON を値の木にし、Update でファイルの "type" の名前から中身を作り、値を入れる。
        // 型が登録簿に無い・ScriptableData の子でない・値の木の形が違うときは「失敗」にし、理由をログに出す。
        core::ObjectHandle LoadScriptableData(const core::Guid& guid);
        ScriptableDataAsset* ResolveScriptableDataAsset(core::ObjectHandle handle) const;
        // 箱の中身。読み終わっていなければ nullptr。
        const ScriptableData* GetScriptableData(const ScriptableDataAsset& asset) const;

        // 参照が指す中身を、expected の型(か、その子)として返す。読み終わっていない・失敗・型が違うなら nullptr。
        // テクスチャの Resolve と同じく、まだ読んでいない・GUID が変わったときは読み始め、reference.asset に箱を書く。
        // 型が違うときは、箱ごとに1回だけ警告を出す。テンプレートを使わない入口(スクリプトからも同じ形で呼べる)。
        const ScriptableData* ResolveScriptableData(core::AssetReference& reference, const core::TypeInfo& expected);

        // ファイルを書き直した .sdata を、新しい箱として読み直す(読み込んだデータは変わらないので、その場では書き換えない)。
        // 古い箱を指している AssetRef は、次の Resolve で新しい箱に乗り換える(新しい箱が読み終わるまでは古い中身のまま)。
        core::ObjectHandle ReloadScriptableData(const core::Guid& guid);

        // 中身の型が types のどれか(か、その子)の ScriptableData を、キャッシュから外して中身を手放す(HR-3)。
        // ゲームの DLL を外す前に、DLL の中の型のオブジェクトを残さないため。古い箱には読み直しと同じ印を付けるので、
        // 指している AssetRef は次の Resolve で GUID から読み直す(型を登録し直した後なら、新しい型で作られる)。
        // 戻り値: 外した数。
        std::size_t ForgetScriptableData(const std::vector<const core::TypeInfo*>& types);

        // 読み込んだ中身が、読み込んだときの値から変わっていないかを確かめる(SD-2。開発ビルドだけ。製品では何もせず 0)。
        // 中身には読むだけの印が付き、書く入口は断るが、C++ の生のポインタ(const_cast など)からの書き込みは止められない。
        // そこで読み込んだときの値を控えておき、ここで比べる。変わっていたら、ファイル・型・プロパティの名前つきでエラーを出し、
        // 変わったプロパティの数を返す(同じ書き換えを2回出さないように、控えは今の値にする)。
        // Application がフレームの終わりに1秒に1回、エディタが Stop のときに呼ぶ。
        std::size_t CheckScriptableDataUnchanged();

        // AssetRef<T>(T は ScriptableData の子)の入口。中身は ResolveScriptableData。
        // enable_if: T が ScriptableData の子のときだけ、この関数があることにする(テクスチャなどの Resolve と混ざらない)。
        template <typename T, std::enable_if_t<std::is_base_of_v<ScriptableData, T>, int> = 0>
        const T* Resolve(core::AssetRef<T>& reference)
        {
            // 型は ResolveScriptableData が IsA で確かめてあるので、static_cast で子の型に変えてよい(RTTI は使わない)。
            return static_cast<const T*>(ResolveScriptableData(reference, T::StaticType()));
        }

        // ---- 共通 ----

        // relativePath のファイルを、assetType(TextureAsset など)のアセットとして読めるか(拡張子で見分ける)。
        // インスペクタが、選べるアセットを絞り込むのに使う。
        static bool CanLoadAs(const core::TypeInfo& assetType, std::string_view relativePath);

        // キャッシュにある(まだ回収されていない)プレハブのアセットの数。確認とテスト用。
        std::size_t GetLoadedPrefabCount() const { return m_prefabs.size(); }
        // キャッシュにある(まだ回収されていない)ScriptableData の箱の数。確認とテスト用。
        std::size_t GetLoadedScriptableDataCount() const { return m_scriptableData.size(); }

        // 読み込み中(ジョブが終わっていないか、結果をまだアセットに書いていない)の数。確認とテスト用。
        std::size_t GetPendingLoadCount() const { return m_pending.size(); }

    private:
        friend class PrefabAsset;      // 回収されるときに OnPrefabDestroyed を呼べるようにする
        friend class ScriptableDataAsset; // 回収されるときに OnScriptableDataDestroyed を呼べるようにする
        friend class Asset;               // 回収されるときに OnAssetDestroyed / ReleaseAssetData を呼べるようにする

        // 読み込み中の記録(どの種類のアセットにも共通の部分)。ジョブとゲームスレッドの受け渡し場所。
        // アセットの種類ごとに、この型を継承して「ジョブが書く結果」を足し、Finish(結果をアセットに書く)を用意する。
        // 新しい種類のアセットを足すときは、記録の型を1つ足すだけで、Update や待ち方の仕組みはそのまま使える。
        //
        // ジョブが書くのは、子の型で足した結果だけ。ゲームスレッドは counter が「終わった」になってから読む
        // (JobCounter の release / acquire で、ジョブが書いた値が見える)。それまで、どちらも触らない。
        struct PendingLoad
        {
            virtual ~PendingLoad() = default; // 子の型を、親のポインタ(unique_ptr<PendingLoad>)で壊すため
            // 結果をアセットに書く(ゲームスレッド。counter が終わってから呼ぶ)。
            virtual void Finish(AssetLoader& loader) = 0;

            core::ObjectHandle asset; // 結果を書く先(弱い参照。読み込み中に回収されたら、結果は捨てる)
            core::Guid guid;          // 書く先が、途中で別のアセットに替わっていないかの確かめ用
            std::string path;         // 読むファイル(ゲームスレッドが、積む前に表で引いておく。表はジョブから触らない)
            core::JobCounter counter; // ジョブの完了の印
        };

        struct PendingPrefabLoad final : PendingLoad
        {
            void Finish(AssetLoader& loader) override { loader.FinishPrefab(*this); }
            ArchiveValue data;                 // ジョブが書く: JSON を読んだ値の木
            std::vector<std::string> warnings; // ジョブが書く: 読めなかった理由(Finish がログに出す)
            bool succeeded = false;            // ジョブが書く: 読めたか
        };

        struct PendingScriptableDataLoad final : PendingLoad
        {
            void Finish(AssetLoader& loader) override { loader.FinishScriptableData(*this); }
            ArchiveValue data;                 // ジョブが書く: JSON を読んだ値の木({"type", "properties"})
            std::vector<std::string> warnings; // ジョブが書く: 読めなかった理由(Finish がログに出す)
            bool succeeded = false;            // ジョブが書く: 読めたか
        };

        // 登録した種類(AssetKind)の読み込み中の記録。中身(load)は種類が作り、ジョブは load の 1段目・2段目だけを動かす。
        struct PendingKindLoad final : PendingLoad
        {
            void Finish(AssetLoader& loader) override { loader.FinishKindLoad(*this); }
            std::unique_ptr<AssetLoad> load;
            const AssetKind* kind = nullptr;
        };

        // 読むファイルのパスを表で引く。無ければ空(呼んだ側が、その場で「失敗」にする)。
        std::string FindPathOrLog(const core::Guid& guid) const;
        // 記録を m_pending に入れてから、job をジョブとして積む(記録の場所は動かない)。
        template <typename Record, typename Job>
        void Schedule(std::unique_ptr<Record> record, Job&& job);

        void StartPrefabLoad(PrefabAsset& asset);
        void FinishPrefab(PendingPrefabLoad& load);
        void OnPrefabDestroyed(PrefabAsset& asset);

        void StartScriptableDataLoad(ScriptableDataAsset& asset);
        void FinishScriptableData(PendingScriptableDataLoad& load);
        void OnScriptableDataDestroyed(ScriptableDataAsset& asset);

        // 登録した種類の共通の流れ。
        struct KindEntry
        {
            std::unique_ptr<AssetKind> kind;
            std::unordered_map<core::Guid, core::ObjectHandle> cache; // GUID → 箱(弱い参照のキャッシュ)
        };
        KindEntry* FindKindEntry(const core::TypeInfo& assetType);
        const KindEntry* FindKindEntry(const core::TypeInfo& assetType) const;
        void StartKindLoad(Asset& asset);
        void FinishKindLoad(PendingKindLoad& pending);
        void ReleaseAssetData(Asset& asset);  // 箱の子のデストラクタから(種類の片付け)
        void OnAssetDestroyed(Asset& asset);  // 箱のデストラクタから(キャッシュから外す)

        const AssetDatabase& m_database;
        core::ObjectRegistry& m_registry;
        const core::TypeRegistry& m_types; // ScriptableData の中身を、ファイルの型の名前から作るため
        core::JobSystem& m_jobs;
        render::RenderDevice* m_device = nullptr; // 借りているだけ(持ち主は Application::Run)
        audio::AudioDevice* m_audio = nullptr;    // 借りているだけ(持ち主は Application)

        // GUID → アセット(弱い参照のキャッシュ)。アセットが回収されると、そのデストラクタから外される。
        std::unordered_map<core::Guid, core::ObjectHandle> m_prefabs;
        std::unordered_map<core::Guid, core::ObjectHandle> m_scriptableData; // 箱(ScriptableDataAsset)を指す

        // 読み込み中の記録。ジョブがアドレスを覚えているので、記録そのものは動かないように unique_ptr で持つ
        // (vector が大きくなって中身が引っ越しても、記録の場所は変わらない)。
        std::vector<std::unique_ptr<PendingLoad>> m_pending;

        // 登録した種類(数は少ないので、探すときは順に見る)。
        std::vector<KindEntry> m_kinds;

        // 入れ子のプレハブが読み終わるのを待っているプレハブ(HIER)。待っている間、入れ子のプレハブは根にして回収させない。
        struct WaitingPrefab
        {
            core::ObjectHandle asset;
            std::vector<core::Guid> nested;           // 入れ子のプレハブの GUID
            std::vector<core::ObjectHandle> nestedAssets; // その読み込み(根に入れている)
        };
        std::vector<WaitingPrefab> m_waitingPrefabs;
        // 待っているプレハブのうち、入れ子が読み終わったものを開いて読み終わりにする。1つでも進めば true。
        bool UpdateWaitingPrefabs();
        // from(待っているプレハブ)が、入れ子をたどって target を待っているか(輪の見つけ方)。
        bool WaitsOn(const core::Guid& from, const core::Guid& target, int depth) const;
        // 待ちの記録を外す(根も外す)。
        void DropWaiting(std::size_t index);
    };
}
