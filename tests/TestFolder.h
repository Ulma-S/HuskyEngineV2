// テストで使う、本物のファイルを置くための一時フォルダ。
// テストの exe のあるフォルダ(build の中)に作り、作るときに中を空にし、壊すときに中身ごと消す(RAII)。
#pragma once

#include "engine/AssetDatabase.h"
#include "platform/FileSystem.h"

#include <doctest/doctest.h>

#include <algorithm> // std::sort
#include <cstdint>
#include <string>
#include <vector>

namespace husky::tests
{
    // 使い方:
    //   TestFolder folder("husky_test_xxx");          // 名前はテストごとに変える(同時に走っても混ざらないように)
    //   folder.WriteAsset("textures/a.png", guid);    // ファイルと .meta を置く
    //   database.Scan(folder.Root());
    class TestFolder
    {
    public:
        explicit TestFolder(const char* name)
            : m_root(::husky::platform::GetExecutableDirectory() + name)
        {
            RemoveAll(); // 前のテストが途中で止まって残っていても、きれいな状態から始める
            ::husky::platform::MakeDirectory(m_root.c_str());
        }
        ~TestFolder() { RemoveAll(); }

        TestFolder(const TestFolder&) = delete;
        TestFolder& operator=(const TestFolder&) = delete;

        const std::string& Root() const { return m_root; }
        std::string PathOf(const std::string& relativePath) const { return m_root + "/" + relativePath; }

        // relativePath にファイルを書く(途中のフォルダも作る)。
        void Write(const std::string& relativePath, const std::string& text) const
        {
            const std::string path = PathOf(relativePath);
            ::husky::platform::MakeDirectory(path.substr(0, path.rfind('/')).c_str());
            REQUIRE(::husky::platform::WriteTextFile(path.c_str(), text));
        }

        // アセットのファイルと、guid を書いた .meta を、組で置く。
        void WriteAsset(const std::string& relativePath, const ::husky::core::Guid& guid) const
        {
            Write(relativePath, "dummy");
            Write(relativePath + ".meta", ::husky::engine::AssetDatabase::MakeMetaText(guid));
        }

        void Remove(const std::string& relativePath) const { ::husky::platform::RemoveFile(PathOf(relativePath).c_str()); }

    private:
        // 中のファイルを消してから、フォルダを深いものから順に消す(中身があるフォルダは消せないため)。
        void RemoveAll() const
        {
            std::vector<std::string> files;
            if (!::husky::platform::ListFilesRecursive(m_root.c_str(), files))
            {
                return; // もともと無い
            }
            std::vector<std::string> folders;
            for (const std::string& file : files)
            {
                ::husky::platform::RemoveFile(PathOf(file).c_str());
                for (std::size_t slash = file.find('/'); slash != std::string::npos; slash = file.find('/', slash + 1))
                {
                    folders.push_back(file.substr(0, slash));
                }
            }
            // 長いパス(深いフォルダ)から消す。
            std::sort(folders.begin(), folders.end(),
                      [](const std::string& a, const std::string& b) { return a.size() > b.size(); });
            for (const std::string& folder : folders)
            {
                ::husky::platform::RemoveFile(PathOf(folder).c_str());
            }
            ::husky::platform::RemoveFile(m_root.c_str());
        }

        std::string m_root;
    };

    // 決まった値の GUID を作る(テストで、どの GUID かを見分けやすくするため)。
    inline ::husky::core::Guid MakeGuid(std::uint64_t high, std::uint64_t low)
    {
        ::husky::core::Guid guid;
        guid.high = high;
        guid.low = low;
        return guid;
    }
}
