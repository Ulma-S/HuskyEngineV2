// アセットのローダ。アセットを読み込む処理は、すべてここを通す(読み込みの入口を1か所にする)。
//
// 読み込みの流れ(テクスチャの場合)。重い部分(3、4)は、ジョブとしてワーカースレッドで行う(非同期):
//   LoadTexture(guid)                                         … ゲームスレッド
//     1. キャッシュ: 同じ GUID のアセットがまだ生きていれば、それを返す(同じ画像を2回読まない)
//     2. GUID → パス: AssetDatabase の表で引く。無ければ、その場で「失敗」
//        アセットを「読み込み中」で作り、ジョブを積んで、すぐに返す
//   ジョブ                                                     … ワーカースレッド
//     3. 1段目「元のファイルを読む」: PNG を読み、CPU のメモリ上の画像にする
//     4. 2段目「実行用のデータにする」: 今は何もしない(素通し)。
//        将来、GPU 向けの圧縮やミップマップ(縮小版)作りをここに入れ、結果をファイルに残しておけば、
//        製品では1段目と2段目を飛ばして、出来上がったデータを読むだけにできる
//   Update(毎フレーム)                                         … ゲームスレッド
//     5. 終わったジョブを見つけ、GPU に送り(描画の準備ができていれば)、「読めた」か「失敗」にする
//
// ジョブは、GC 管理のオブジェクト(TextureAsset)を触らない。触るのは「読み込み中の記録」(PendingTextureLoad)の
// パスと画像(ただのデータ)だけ。アセットに結果を書くのは、ゲームスレッドの Update。
// 読み込み中にアセットが GC で回収されても、記録はアセットを弱い参照で指しているだけなので、終わったときに捨てるだけ。
//
// Load は必ずハンドルを返す。戻ったときは、ふつう「読み込み中」。状態は TextureAsset::GetState で分かる
// (オブジェクト用のメモリを使い切ったときだけは、何も指さないハンドルを返す)。
// テストなどで、読み終わるまで待ちたいときは WaitForAllLoads を呼ぶ。
//
// 描画の準備(RenderDevice)は Run の中でしか用意できないので、それより前にも読めるようにしている:
//   - GPU が無いときに読み終わったテクスチャは、ファイルを読めれば「読めた」にし、GPU のテクスチャは作らない
//   - SetRenderDevice で GPU が付いたら、GPU のテクスチャを持っていないアセットを、もう一度読む(「読み込み中」に戻る)
//     (【仮】1回目の画像を取っておけば読み直さずに済むが、メモリを使うので、今は単純に読み直す)
//   - SetRenderDevice(nullptr) で GPU が外れるときは、すべての GPU のテクスチャを手放す
//
// スプライトシート(LoadSpriteSheet)も同じ流れ。ジョブが .spritesheet(JSON)を読み、Update で結果を書く。
// 書き終わったら、中で指している画像(テクスチャ)の読み込みを始める(依存の読み込み)。
//
// アセットの種類を足すときは、読み込み中の記録の型(PendingLoad の子)と、Start〜 / Finish〜 の組を足す。
//
// 動作スレッド: ゲームスレッドのみ(中で積んだジョブだけが、ワーカースレッドで動く)
#pragma once

#include "core/AssetRef.h"
#include "core/Guid.h"
#include "core/JobSystem.h"
#include "core/ObjectRegistry.h"
#include "core/TypeInfo.h"
#include "engine/AssetDatabase.h"
#include "engine/SpriteSheetAsset.h"
#include "engine/TextureAsset.h"
#include "platform/Image.h"
#include "render/RenderDevice.h"

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace husky::engine
{
    class AssetLoader final
    {
    public:
        // database: GUID からパスを引く表。registry: アセットのオブジェクトを作るところ。jobs: 読み込みのジョブを積むところ。
        // どれも借りるだけ。このローダが作ったアセットより先に、このローダを壊さないこと(アセットは、回収されるときに
        // ローダを呼ぶため)。jobs は、このローダより長く生きること(壊すときに、読み込み中のジョブを待つため)。
        AssetLoader(const AssetDatabase& database, core::ObjectRegistry& registry, core::JobSystem& jobs);
        // 読み込み中のジョブが終わるのを待ってから壊れる(ジョブが、この中の記録に書き込むため)。
        // 結果は捨てる(このとき、レジストリはもう壊れていることがあるので、アセットには触らない)。
        ~AssetLoader();

        AssetLoader(const AssetLoader&) = delete;
        AssetLoader& operator=(const AssetLoader&) = delete;

        // GPU を付ける(nullptr で外す)。付けると、GPU のテクスチャを持っていない読み込み済みのアセットを、読み直す。
        // 外すと、すべての GPU のテクスチャを手放す。RenderDevice を壊す前に、必ず nullptr で外すこと。
        void SetRenderDevice(render::RenderDevice* device);

        // テクスチャの読み込みを始め、TextureAsset を指すハンドルを返す(ふつうは「読み込み中」)。
        // 返すハンドルは弱い参照。使い続けるなら、根に加えるか、プロパティ(強い参照)で持つこと。
        // どこからも強く指されていないアセットは、次の GC で回収される。
        core::ObjectHandle LoadTexture(const core::Guid& guid);

        // 毎フレーム1回、ゲームスレッドで呼ぶ。読み終わったジョブの結果をアセットに書く(GPU に送る)。
        // ワーカーが 0 本(同期実行)のときは、積まれている読み込みのジョブも、ここで実行する。
        void Update();

        // 読み込み中のものが、すべて終わるまで待ち、結果をアセットに書く。テストや、読み込み画面などで使う。
        void WaitForAllLoads();

        // ---- テクスチャ ----

        // ハンドルが指す TextureAsset を返す。もう無い、またはテクスチャのアセットでなければ nullptr。
        TextureAsset* ResolveTexture(core::ObjectHandle handle) const;

        // アセットの参照(プロパティの AssetRef)が指すテクスチャを返す。何も指していなければ nullptr。
        // まだ読んでいない、または指す GUID が変わった(インスペクタで選び直したなど)ときは、ここで読み始め、
        // アセットのハンドルを reference.asset に書く(これで、参照している間は GC に回収されない)。
        // 読み始めたばかりなら「読み込み中」のアセットを返す(状態は IsLoaded で分かる)。
        TextureAsset* Resolve(core::AssetRef<TextureAsset>& reference);

        // ---- スプライトシート ----

        // スプライトシートの読み込みを始め、SpriteSheetAsset を指すハンドル(弱い参照)を返す。
        // 読み終わると、中で指している画像(テクスチャ)の読み込みも始める。
        core::ObjectHandle LoadSpriteSheet(const core::Guid& guid);
        SpriteSheetAsset* ResolveSpriteSheet(core::ObjectHandle handle) const;
        // テクスチャの Resolve と同じ(まだ読んでいなければ読み始め、reference.asset に書く)。
        SpriteSheetAsset* Resolve(core::AssetRef<SpriteSheetAsset>& reference);

        // ---- 共通 ----

        // relativePath のファイルを、assetType(TextureAsset など)のアセットとして読めるか(拡張子で見分ける)。
        // インスペクタが、選べるアセットを絞り込むのに使う。
        static bool CanLoadAs(const core::TypeInfo& assetType, std::string_view relativePath);

        // キャッシュにある(まだ回収されていない)テクスチャのアセットの数。確認とテスト用。
        std::size_t GetLoadedTextureCount() const { return m_textures.size(); }
        // キャッシュにある(まだ回収されていない)スプライトシートのアセットの数。確認とテスト用。
        std::size_t GetLoadedSpriteSheetCount() const { return m_spriteSheets.size(); }

        // 読み込み中(ジョブが終わっていないか、結果をまだアセットに書いていない)の数。確認とテスト用。
        std::size_t GetPendingLoadCount() const { return m_pending.size(); }

    private:
        friend class TextureAsset;     // 回収されるときに OnTextureDestroyed を呼べるようにする
        friend class SpriteSheetAsset; // 回収されるときに OnSpriteSheetDestroyed を呼べるようにする

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

        struct PendingTextureLoad final : PendingLoad
        {
            void Finish(AssetLoader& loader) override { loader.FinishTexture(*this); }
            platform::Image image;  // ジョブが書く: 読んだ画像
            bool succeeded = false; // ジョブが書く: 読めたか
        };

        struct PendingSpriteSheetLoad final : PendingLoad
        {
            void Finish(AssetLoader& loader) override { loader.FinishSpriteSheet(*this); }
            core::Guid texture;                // ジョブが書く: 中で指している画像の GUID
            std::vector<SpriteSlice> slices;   // ジョブが書く: 切り出しの一覧
            std::vector<std::string> warnings; // ジョブが書く: 読んだときの注意(Finish がログに出す)
            bool succeeded = false;            // ジョブが書く: 読めたか
        };

        // 読むファイルのパスを表で引く。無ければ空(呼んだ側が、その場で「失敗」にする)。
        std::string FindPathOrLog(const core::Guid& guid) const;
        // 記録を m_pending に入れてから、job をジョブとして積む(記録の場所は動かない)。
        template <typename Record, typename Job>
        void Schedule(std::unique_ptr<Record> record, Job&& job);

        void StartTextureLoad(TextureAsset& asset);
        void FinishTexture(PendingTextureLoad& load);
        void OnTextureDestroyed(TextureAsset& asset);

        void StartSpriteSheetLoad(SpriteSheetAsset& asset);
        void FinishSpriteSheet(PendingSpriteSheetLoad& load);
        void OnSpriteSheetDestroyed(SpriteSheetAsset& asset);

        const AssetDatabase& m_database;
        core::ObjectRegistry& m_registry;
        core::JobSystem& m_jobs;
        render::RenderDevice* m_device = nullptr; // 借りているだけ(持ち主は Application::Run)

        // GUID → アセット(弱い参照のキャッシュ)。アセットが回収されると、そのデストラクタから外される。
        std::unordered_map<core::Guid, core::ObjectHandle> m_textures;
        std::unordered_map<core::Guid, core::ObjectHandle> m_spriteSheets;

        // 読み込み中の記録。ジョブがアドレスを覚えているので、記録そのものは動かないように unique_ptr で持つ
        // (vector が大きくなって中身が引っ越しても、記録の場所は変わらない)。
        std::vector<std::unique_ptr<PendingLoad>> m_pending;
    };
}
