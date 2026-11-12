// スプライトの切り出し方: 1 枚の画像の中の「名前付きの範囲」の一覧。テクスチャの .meta に書く(インポートの設定)。
//
//   shapes.png.meta:
//   {
//     "guid": "…",
//     "sprites": [
//       { "id": 1841231977, "name": "circle",  "x": 0,  "y": 0,  "width": 32, "height": 32 },
//       { "id": 520771344,  "name": "diamond", "x": 32, "y": 32, "width": 32, "height": 32 }
//     ]
//   }
//
// 切り出しは「画像の GUID + 切り出しの ID」で指す(Unity の fileID と同じ考え方)。
//   - ID は、一度振ったら変えない。名前は表示用で、変えても参照は切れない(GUID とパスの関係と同じ)。
//   - ID は、同じ画像の中で重ならなければよい(画像どうしは GUID で区別できる)。0 は「指さない」に使うので振らない。
//   - 乱数で振る(連番にしない): 2 人が別々に切り出しを足しても、同じ番号がぶつかりにくいため。
//   - 手で書き足した切り出しには ID が無い。開発ビルドが起動時に振って、.meta に書き戻す(AssignMissingSliceIds)。
//
// 動作スレッド: どこでもよい(ただのデータと関数。テクスチャを読むジョブの中でも使う)
#pragma once

#include "engine/ArchiveValue.h"
#include "engine/PixelRect.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace husky::engine
{
    // 切り出し1つ。
    struct SpriteSlice
    {
        std::int32_t id = 0; // 画像の中で重ならない ID(0 は「指さない」)
        std::string name;    // 表示用の名前(インスペクタで選ぶときに出す)
        PixelRect rect;      // 画像の中の範囲(ピクセル)
    };

    // .meta の値の木(JSON を読んだもの)から、切り出しの一覧を読む。"sprites" が無ければ、空の一覧。
    // 形がおかしい切り出し(数が無い、ID が無いか 0、大きさが 0 以下、ID が重なる)は飛ばし、理由を outWarnings に足す
    // (outWarnings が nullptr なら足さない)。
    std::vector<SpriteSlice> ReadSpriteSlices(const ArchiveValue& meta, std::vector<std::string>* outWarnings = nullptr);

#if HUSKY_DEVELOPMENT
    // 【開発ビルドだけ】.meta の値の木の、ID が無い(か 0 の、ほかと重なる)切り出しに、新しい ID を振る。
    // ID を振るのはエディタの役目なので、製品(Release)にはコードごと入らない。
    // 戻り値: 振った数(0 なら木は変えていない)。
    std::size_t AssignMissingSliceIds(ArchiveValue& meta);

    // 【開発ビルドだけ】rootDirectory の中のすべての .meta について AssignMissingSliceIds を行い、振ったら書き戻す。
    // mirrorDirectory が空でなければ、書き戻した .meta を、同じ相対パスでそこにも書く(exe の隣の assets など)。
    // 戻り値: 書き戻した .meta の数。
    std::size_t AssignMissingSliceIdsInFolder(const std::string& rootDirectory, const std::string& mirrorDirectory = {});
#endif
}
