// フォントのアセット(UI-3b)。TTF / OTF のファイル1つ(と .meta)を読み込んだもので、GC 管理のオブジェクト(core::Object)。
// テクスチャや音と同じ形: ファイル + .meta をそのままアセットにし、AssetRef<FontAsset> で指す。
//
// 読み込んだフォント(FontFace)は変わらないデータ。字のアトラス(FontAtlas)は、大きさごとに使うときに作る「描くための
// 控え」で、ファイルには関係しない(テクスチャのアセットが GPU のテクスチャを持つのと同じ扱い。ローダが書いた中身は変えない)。
//
// 寿命は GC が決める。回収されるとき、アトラスの GPU のテクスチャを手放す(ローダに RenderDevice が付いていれば)。
//
// 動作スレッド: ゲームスレッドのみ
#pragma once

#include "engine/Asset.h"
#include "engine/FontAtlas.h"
#include "engine/FontFace.h"

#include <memory>
#include <vector>

namespace husky::engine
{
    class FontAssetKind;

    class FontAsset : public Asset
    {
        HUSKY_OBJECT(FontAsset, Asset)
    public:
        // 回収されるとき、アトラスのテクスチャを手放す(FontAssetKind::Release)。
        ~FontAsset() override;

        // 読んだフォント。読み終わっていない・読めなかったときは nullptr。
        const FontFace* GetFace() const { return m_face.get(); }

        // pixelHeight の字のアトラス。初めて頼まれた大きさなら作る。読み終わっていなければ nullptr。
        // 大きさは丸めずにそのまま比べる(16 と 16.5 は別のアトラス)。UI は決まった大きさをいくつか使う想定。
        FontAtlas* GetAtlas(float pixelHeight);
        std::size_t GetAtlasCount() const { return m_atlases.size(); }

    private:
        friend class FontAssetKind; // 中身を書くのは読み方(FontAsset.cpp)だけ

        std::unique_ptr<FontFace> m_face;
        std::vector<std::unique_ptr<FontAtlas>> m_atlases; // FontAtlas は m_face を借りるので、m_face より後に宣言する
    };

    // フォント(.ttf / .otf)の読み方を作る。ローダに RegisterKind で登録する(AL-2)。
    std::unique_ptr<AssetKind> CreateFontAssetKind();
}
