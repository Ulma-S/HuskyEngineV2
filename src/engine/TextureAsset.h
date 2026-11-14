// テクスチャのアセット。画像ファイル1つ(と、その .meta の取り込みの設定)を読み込んだもので、GC 管理のオブジェクト(core::Object)。
//
// 今は「画像 + .meta」をそのままテクスチャのアセットとして扱う(1 枚の画像 = 1 つのテクスチャ。Unity と同じ形)。
// 将来、同じ画像を別々の取り込みの設定(圧縮、ミップマップ、色の扱い)で使いたくなったら、画像を包むアセット
// (.texture のようなファイル)に変える余地を残している。そのときも、使う側は GUID(AssetRef<TextureAsset>)で指すので変わらない
// (設計メモ「テクスチャを包むアセットへの移し替え」)。
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

#include "engine/Asset.h"
#include "render/RenderDevice.h"

#include <cstdint>
#include <memory>

namespace husky::engine
{
    class TextureAssetKind;

    class TextureAsset : public Asset
    {
        HUSKY_OBJECT(TextureAsset, Asset)
    public:
        // 回収されるとき、GPU のテクスチャを手放す(TextureAssetKind::Release)。
        ~TextureAsset() override;

        // 画像の大きさ(ピクセル)。読み込めていなければ 0。
        int GetWidth() const { return m_width; }
        int GetHeight() const { return m_height; }

        // 描画層のテクスチャ。描画の準備ができていない(GPU が無い、テストなど)ときは、何も指さないハンドル。
        render::TextureHandle GetTexture() const { return m_texture; }

    private:
        // 中身を書くのは読み方(TextureAsset.cpp)だけ(friend: private なメンバーを触らせる)。
        friend class TextureAssetKind;

        int m_width = 0;
        int m_height = 0;
        render::TextureHandle m_texture;
    };

    // テクスチャ(.png)の読み方を作る。ローダに RegisterKind で登録する(AL-2)。
    std::unique_ptr<AssetKind> CreateTextureAssetKind();
}
