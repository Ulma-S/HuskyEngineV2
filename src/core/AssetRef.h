// アセットへの参照。プロパティにでき、保存すると GUID になり、持っている間はアセットを GC から守る。
//
// 使い方(SpriteComponent など):
//   core::AssetRef<engine::TextureAsset> texture;   // メンバーにして、HUSKY_PROPERTY(texture) で登録する
//   texture.guid = someGuid;                         // 指す先を決める(インスペクタやファイルからも決まる)
//   TextureAsset* asset = loader.Resolve(texture);   // 使うとき(まだ読んでいなければ、ここで読む)
//
// 中身は2つ:
//   - guid  … どのアセットか。ファイルに保存されるのはこれだけ(実行中だけの値であるハンドルは保存しない)
//   - asset … 読み込んだアセットを指すハンドル。プロパティとして登録すると、GC はこれを強い参照としてたどるので、
//             この参照を持っている間は、アセットが回収されない。ローダが Resolve のときに書く
// 「ファイルにはずっと変わらない GUID、実行中は速いハンドル」の2つを持つことで、両方の良いところを使う。
//
// T(アセットの型。engine::TextureAsset など)は、型を取り違えないための印と、インスペクタで選べるアセットを
// 絞り込むためだけに使う。中身は T によらず同じ(AssetReference)なので、保存や GC は T を知らずに扱える。
// core は engine の型(TextureAsset)を知らないが、テンプレートなら使う側(engine)が T を決めるので、依存の向きは崩れない。
//
// 動作スレッド: ゲームスレッドのみ(asset を書き換えるのはローダ)
#pragma once

#include "core/Guid.h"
#include "core/Handle.h"

namespace husky::core
{
    class Object;
    using ObjectHandle = Handle<Object>;

    // 型を問わないアセットの参照。保存と GC は、この形で扱う。
    struct AssetReference
    {
        Guid guid;          // 指すアセット(すべて 0 なら、何も指さない)
        ObjectHandle asset; // 読み込んだアセット(実行中だけ。GC がたどる強い参照)

        bool IsNull() const { return guid.IsNull(); }
    };

    // T 型のアセットへの参照。メンバーは足さない(AssetReference と同じ形のまま、型の印だけを付ける)。
    template <typename T>
    struct AssetRef : AssetReference
    {
        using AssetType = T;
    };
}
