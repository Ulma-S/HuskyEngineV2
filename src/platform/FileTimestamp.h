// 1つのファイルの更新日時を、何度も軽く読むための窓口(HR-5。ゲームの DLL がビルドし直されたかを見張るのに使う)。
//
// パスは SetPath で1回だけ渡し、中で OS 向けの形に変えて持つ。Read は毎回それを使うので、メモリを確保しない
// (0.5 秒ごとに読んでも、毎秒の確保の数が増えない)。中身は C++17 の std::filesystem(OS ごとの違いは標準ライブラリが吸収する)。
//
// 動作スレッド: どこでもよい(1つの FileTimestamp を同時に複数のスレッドから使わないこと)。
#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace husky::platform
{
    class FileTimestamp
    {
    public:
        FileTimestamp();
        ~FileTimestamp();
        FileTimestamp(const FileTimestamp&) = delete;
        FileTimestamp& operator=(const FileTimestamp&) = delete;

        // 見るファイル(UTF-8 のパス)。
        void SetPath(const std::string& utf8Path);
        const std::string& GetPath() const { return m_utf8Path; }

        // 更新日時を out に書く(単位は決めない。同じファイルの前の値と比べるためだけに使う)。
        // ファイルが無い・読めないときは false。
        bool Read(std::int64_t& out) const;

    private:
        struct Impl; // std::filesystem::path を持つ(ヘッダに <filesystem> を出さないため)
        std::unique_ptr<Impl> m_impl;
        std::string m_utf8Path;
    };
}
