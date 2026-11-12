// ファイルの読み書き。中では SDL3 を使うが、このヘッダには SDL の型を書かない。
//
// パスは UTF-8 の文字列で、区切りは「/」で書く(Windows でも動く。パスを Windows 固有の書き方にしないため)。
// 相対パスは、作業フォルダ(起動のしかたで変わる)が基準になる。exe の隣のファイル(ビルド時にコピーした assets など)を
// 読むときは、GetExecutableDirectory() を前に付ける。
//
// 動作スレッド: どこでもよい(ただし、同じファイルを複数のスレッドから同時に書かないこと)。
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace husky::platform
{
    // exe のあるフォルダ(末尾に「/」などの区切りが付いている)。取れなければ空の文字列。
    std::string GetExecutableDirectory();

    // ファイル全体を読み、outText に入れる。戻り値: 成功したら true。失敗の理由はログに出す。
    bool ReadTextFile(const char* path, std::string& outText);

    // text をファイルに書く(すでにあれば置き換える)。戻り値: 成功したら true。失敗の理由はログに出す。
    // 安全のため、まず「path + .tmp」に書き、書き終えてから path へ名前を変えて置き換える。
    // こうすると、書いている途中でクラッシュしても、元のファイルは壊れずに残る(壊れるのは .tmp だけ)。
    bool WriteTextFile(const char* path, std::string_view text);

    // ファイル(かフォルダ)があれば true。
    bool FileExists(const char* path);

    // ファイルを消す。戻り値: 呼んだ後にファイルが無い状態になっていれば true(もともと無いときも true)。
    // 消せなかった(使用中、権限が無いなど)ときは false。空のフォルダも消せる(中身があるフォルダは消せない)。
    bool RemoveFile(const char* path);

    // フォルダを作る。途中のフォルダが無ければ、それも作る。すでにあれば何もしない。
    // 戻り値: 呼んだ後にフォルダがあれば true。
    // (名前を CreateDirectory にしないのは、Windows のヘッダが同じ名前のマクロを定義していて、ぶつかることがあるため)
    bool MakeDirectory(const char* path);

    // directory の中のファイルを、下のフォルダの中まですべて集め、outPaths に入れる(前の中身は消す)。
    // 入れるのは directory からの相対パスで、区切りは「/」(例: "textures/enemy.png")。フォルダそのものは入れない。
    // 並びは名前の順(OS によって並びが変わらないように、並べ替えてから返す)。
    // 戻り値: フォルダを読めたら true。
    bool ListFilesRecursive(const char* directory, std::vector<std::string>& outPaths);
}
