// アセットのローダ。アセットを読み込む処理は、すべてここを通す(読み込みの入口を1か所にする)。
//
// 読み込みの流れ(テクスチャの場合):
//   LoadTexture(guid)
//     1. キャッシュ: 同じ GUID のアセットがまだ生きていれば、それを返す(同じ画像を2回読まない)
//     2. GUID → パス: AssetDatabase の表で引く。無ければ「失敗」
//     3. 1段目「元のファイルを読む」: PNG を読み、CPU のメモリ上の画像にする
//     4. 2段目「実行用のデータにする」: 今は何もしない(素通し)。
//        将来、GPU 向けの圧縮やミップマップ(縮小版)作りをここに入れ、結果をファイルに残しておけば、
//        製品では1段目と2段目を飛ばして、出来上がったデータを読むだけにできる
//     5. GPU に送る: 描画の準備ができていれば(SetRenderDevice 済み)、テクスチャを作る
//
// Load は必ずハンドルを返す。失敗しても「失敗」の状態のアセットを返すので、呼んだ側は状態を見ればよい
// (オブジェクト用のメモリを使い切ったときだけは、何も指さないハンドルを返す)。今は同期で読むので、
// 戻ったときには「読めた」か「失敗」のどちらかになっている。
//
// 描画の準備(RenderDevice)は Run の中でしか用意できないので、それより前にも読めるようにしている:
//   - GPU が無いときに読んだテクスチャは、ファイルを読めれば「読めた」にし、GPU のテクスチャは作らない
//   - SetRenderDevice で GPU が付いたら、GPU のテクスチャを持っていないアセットを、もう一度読んで GPU に送る
//     (【仮】1回目の画像を取っておけば読み直さずに済むが、メモリを使うので、今は単純に読み直す)
//   - SetRenderDevice(nullptr) で GPU が外れるときは、すべての GPU のテクスチャを手放す
//
// 動作スレッド: ゲームスレッドのみ
#pragma once

#include "core/Guid.h"
#include "core/ObjectRegistry.h"
#include "engine/AssetDatabase.h"
#include "engine/TextureAsset.h"
#include "render/RenderDevice.h"

#include <cstddef>
#include <unordered_map>

namespace husky::engine
{
    class AssetLoader final
    {
    public:
        // database: GUID からパスを引く表。registry: アセットのオブジェクトを作るところ。
        // どちらも借りるだけ。このローダが作ったアセットより先に、このローダを壊さないこと
        // (アセットは、回収されるときにローダを呼ぶため)。
        AssetLoader(const AssetDatabase& database, core::ObjectRegistry& registry);

        AssetLoader(const AssetLoader&) = delete;
        AssetLoader& operator=(const AssetLoader&) = delete;

        // GPU を付ける(nullptr で外す)。付けると、GPU のテクスチャを持っていないアセットを GPU に送り直す。
        // 外すと、すべての GPU のテクスチャを手放す。RenderDevice を壊す前に、必ず nullptr で外すこと。
        void SetRenderDevice(render::RenderDevice* device);

        // テクスチャを読み込み、TextureAsset を指すハンドルを返す(状態は TextureAsset::GetState で分かる)。
        // 返すハンドルは弱い参照。使い続けるなら、根に加えるか、プロパティ(強い参照)で持つこと。
        // どこからも強く指されていないアセットは、次の GC で回収される。
        core::ObjectHandle LoadTexture(const core::Guid& guid);

        // ハンドルが指す TextureAsset を返す。もう無い、またはテクスチャのアセットでなければ nullptr。
        TextureAsset* ResolveTexture(core::ObjectHandle handle) const;

        // キャッシュにある(まだ回収されていない)テクスチャのアセットの数。確認とテスト用。
        std::size_t GetLoadedTextureCount() const { return m_textures.size(); }

    private:
        friend class TextureAsset; // 回収されるときに OnTextureDestroyed を呼べるようにする

        // GUID → パス → 1段目 → 2段目 → GPU、の順に読み、結果を asset に書く。
        void ReadTexture(TextureAsset& asset);
        void OnTextureDestroyed(TextureAsset& asset);

        const AssetDatabase& m_database;
        core::ObjectRegistry& m_registry;
        render::RenderDevice* m_device = nullptr; // 借りているだけ(持ち主は Application::Run)

        // GUID → アセット(弱い参照のキャッシュ)。アセットが回収されると、そのデストラクタから外される。
        std::unordered_map<core::Guid, core::ObjectHandle> m_textures;
    };
}
