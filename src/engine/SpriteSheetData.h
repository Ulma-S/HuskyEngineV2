// スプライトシートの中身: 切り出し(1 枚の画像の中の名前付きの範囲)と、アニメーション(切り出しを時間で切り替える並び)。
// スプライトシートのファイル(.spritesheet、JSON)に書く。読むのは SpriteSheetAsset(ローダ)。
//
//   assets/sprites/shapes.spritesheet:
//   {
//     "texture": "79c3b0af-c595-475c-96a9-6925bdf98dd7",
//     "sprites": [
//       { "id": 2029980675, "name": "circle",  "x": 0,  "y": 0,  "width": 32, "height": 32 },
//       { "id": 1917386538, "name": "diamond", "x": 32, "y": 32, "width": 32, "height": 32 }
//     ],
//     "animations": [
//       { "id": 5551234, "name": "cycle", "frames": [2029980675, 1917386538], "frameSeconds": 0.25, "loop": true }
//     ]
//   }
//
// 切り出しとアニメーションは、どちらも「スプライトシートの GUID + ID」で指す(Unity の fileID と同じ考え方)。
//   - ID は、一度振ったら変えない。名前は表示用で、変えても参照は切れない(GUID とパスの関係と同じ)。
//   - ID は、同じスプライトシートの、同じ種類(切り出し / アニメーション)の中で重ならなければよい。0 は「指さない」に使う。
//   - 乱数で振る(連番にしない): 2 人が別々に足しても、同じ番号がぶつかりにくいため。
//
// 手で書く形と、実行時に読む形:
//   人(や AI)が書くときは、ID を書かなくてよく、アニメーションのコマは切り出しの「名前」で書いてよい:
//     "animations": [ { "name": "cycle", "frames": ["circle", "diamond"], "frameSeconds": 0.25 } ]
//   開発ビルドが起動時に、ID を振り、コマの名前を ID に書き換えて、ファイルに書き戻す(NormalizeSpriteSheet。エディタの役目)。
//   実行時(製品)に読むのは、ID だけ。名前のままのコマは、読むときに注意を出して飛ばす。
//
// 動作スレッド: どこでもよい(ただのデータと関数。スプライトシートを読むジョブの中でも使う)
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
        std::int32_t id = 0; // スプライトシートの中で重ならない ID(0 は「指さない」)
        std::string name;    // 表示用の名前(インスペクタで選ぶときに出す)
        PixelRect rect;      // 画像の中の範囲(ピクセル)
    };

    // アニメーション1つ: 切り出しを、決まった時間ごとに順に切り替える。
    struct SpriteAnimation
    {
        std::int32_t id = 0;              // スプライトシートの中で重ならない ID(0 は「指さない」)
        std::string name;                 // 表示用の名前
        std::vector<std::int32_t> frames; // コマ(切り出しの ID)の並び。空にはならない(空なら読むときに飛ばす)
        float frameSeconds = 0.1f;        // 1 コマの時間(秒)。0 より大きい
        bool loop = true;                 // 最後のコマの後、最初に戻るか(false なら最後のコマで止まる)
    };

    // スプライトシートの値の木(JSON を読んだもの)から、切り出しの一覧を読む。"sprites" が無ければ、空の一覧。
    // 形がおかしい切り出し(数が無い、ID が無いか 0、大きさが 0 以下、ID が重なる)は飛ばし、理由を outWarnings に足す
    // (outWarnings が nullptr なら足さない)。
    std::vector<SpriteSlice> ReadSpriteSlices(const ArchiveValue& sheet, std::vector<std::string>* outWarnings = nullptr);

    // アニメーションの一覧を読む。"animations" が無ければ、空の一覧。
    // 形がおかしいもの(ID が無いか 0、重なる、コマが無い、1 コマの時間が 0 以下)は飛ばす。
    // 名前のままのコマ(開発ビルドでまだ整えていない)と、slices に無い切り出しを指すコマは、注意を出す
    // (名前のままのコマは飛ばす。無い切り出しのコマは残す。描くときは、見つからない切り出しと同じ扱いになる)。
    std::vector<SpriteAnimation> ReadSpriteAnimations(const ArchiveValue& sheet, const std::vector<SpriteSlice>& slices,
                                                      std::vector<std::string>* outWarnings = nullptr);

    // 再生を始めてから time 秒たったときのコマ(切り出しの ID)を返す。
    // 繰り返すなら最初に戻り、繰り返さないなら最後のコマで止まる。コマが無ければ 0。time が負なら最初のコマ。
    std::int32_t SampleAnimationFrame(const SpriteAnimation& animation, double time);

#if HUSKY_DEVELOPMENT
    // 【開発ビルドだけ】スプライトシートの値の木を整える(エディタの役目なので、製品にはコードごと入らない):
    //   1. ID が無い(か 0 の、ほかと重なる)切り出しとアニメーションに、新しい ID を振る
    //   2. アニメーションのコマが切り出しの名前で書かれていたら、その切り出しの ID に書き換える(見つからない名前はそのまま)
    // 戻り値: 直した数(0 なら木は変えていない)。
    std::size_t NormalizeSpriteSheet(ArchiveValue& sheet);

    // 【開発ビルドだけ】rootDirectory の中のすべての .spritesheet について NormalizeSpriteSheet を行い、直したら書き戻す。
    // mirrorDirectory が空でなければ、書き戻したファイルを、同じ相対パスでそこにも書く(exe の隣の assets など)。
    // 戻り値: 書き戻したファイルの数。
    std::size_t NormalizeSpriteSheetsInFolder(const std::string& rootDirectory, const std::string& mirrorDirectory = {});
#endif
}
