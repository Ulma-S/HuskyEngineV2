// テクスチャのアセット。画像ファイル1つを読み込んだもので、GC 管理のオブジェクト(core::Object)。
//
// 作るのは AssetLoader だけ(AssetLoader::LoadTexture)。ゲームは、受け取ったハンドルから引いて使う。
//   core::ObjectHandle handle = loader.LoadTexture(guid);
//   if (TextureAsset* texture = loader.ResolveTexture(handle); texture && texture->IsLoaded()) { ... }
//
// 寿命は GC が決める。どこからも強い参照(プロパティの ObjectHandle、または根)で指されていなければ、
// 次の GC で回収され、そのとき GPU のテクスチャも手放す(解放は描画層が数フレーム遅らせる)。
//
// 動作スレッド: ゲームスレッドのみ
#pragma once

#include "core/Guid.h"
#include "core/Object.h"
#include "render/RenderDevice.h"

#include <cstdint>

namespace husky::engine
{
    class AssetLoader;

    // アセットの読み込みの状態。
    enum class AssetState : std::uint8_t
    {
        Loading, // 読み込み中(ワーカーが読んでいる。終わると AssetLoader::Update が Loaded か Failed にする)
        Loaded,  // 読み込めた
        Failed,  // 読み込めなかった(GUID が表に無い、ファイルが壊れているなど。理由はログに出る)
    };

    class TextureAsset : public core::Object
    {
        HUSKY_OBJECT(TextureAsset, core::Object)
    public:
        // 回収されるとき、GPU のテクスチャを手放すよう、ローダに伝える。
        ~TextureAsset() override;

        const core::Guid& GetGuid() const { return m_guid; }
        AssetState GetState() const { return m_state; }
        bool IsLoaded() const { return m_state == AssetState::Loaded; }

        // 画像の大きさ(ピクセル)。読み込めていなければ 0。
        int GetWidth() const { return m_width; }
        int GetHeight() const { return m_height; }

        // 描画層のテクスチャ。描画の準備ができていない(GPU が無い、テストなど)ときは、何も指さないハンドル。
        render::TextureHandle GetTexture() const { return m_texture; }

    private:
        // 中身を書くのは AssetLoader だけ(friend: private なメンバーを触らせる)。
        friend class AssetLoader;

        core::Guid m_guid;
        AssetState m_state = AssetState::Loading;
        int m_width = 0;
        int m_height = 0;
        render::TextureHandle m_texture;
        AssetLoader* m_loader = nullptr; // 作ったローダ(借りているだけ。ローダはこのオブジェクトより長く生きる)
    };
}
