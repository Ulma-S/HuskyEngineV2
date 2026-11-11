// フリーリスト型のヒープアロケータ(ファーストフィット)。
//
// 作るときに決まった大きさのメモリの塊を1つもらい、その中を切り分けて貸し出す。
//   - Allocate: 空きブロックのリストを先頭から見て、最初に収まったブロックを使う(ファーストフィット)。
//               大きすぎるブロックは必要な分だけ切り出し、残りを空きとして戻す(分割)。
//   - Free    : ブロックを空きリストに戻し、メモリ上ですぐ隣も空きなら1つにくっつける(結合)。
//
// ブロックの形:
//   [ヘッダ 16バイト | 中身(貸し出す部分) ...]
//   ヘッダには、ブロック全体の大きさと「使用中 / 空き」の目印を書く。
//   空きブロックでは、中身の先頭に「次の空きブロックの場所」を書いて、空きをリストにつなぐ。
//
// 動作スレッド: ゲームスレッドのみ(複数のスレッドから同時に使ってはいけない)。
#pragma once

#include <cstddef>
#include <memory>
#include <new>     // placement new
#include <utility> // std::forward

namespace husky::core
{
    class FreeListAllocator final
    {
    public:
        // 対応するアラインメントの上限(バイト)。すべてのブロックをこの倍数にそろえる。
        static constexpr std::size_t MaxAlignment = 16;

        // capacityBytes: 管理するメモリの塊の大きさ。16 の倍数に切り捨てて使う。
        explicit FreeListAllocator(std::size_t capacityBytes);

        FreeListAllocator(const FreeListAllocator&) = delete;
        FreeListAllocator& operator=(const FreeListAllocator&) = delete;

        // size バイトを確保し、その先頭のアドレスを返す。足りなければ nullptr。
        // alignment: 返すアドレスが何の倍数であるべきか。MaxAlignment(16)より大きければ nullptr。
        // size が 0 のときも、1 バイト分として扱って有効なアドレスを返す。
        void* Allocate(std::size_t size, std::size_t alignment = MaxAlignment);

        // Allocate で返したアドレスを返却する。
        // 戻り値: 返却できたら true。nullptr、このアロケータのものでないアドレス、二重解放なら false(何もしない)。
        bool Free(void* pointer);

        // T 型のオブジェクトを、このアロケータのメモリの上に作る(確保 + コンストラクタ)。足りなければ nullptr。
        // C# の new T(args) にあたる。args は T のコンストラクタにそのまま渡す。
        //
        // 中でやっていること:
        //   1. Allocate でメモリを借りる(ただのバイト列)
        //   2. placement new で、その上でコンストラクタだけを呼ぶ(メモリの確保はしない)
        // placement new は、CLAUDE.md で、この New の中だけに許可している。
        template <typename T, typename... Args>
        T* New(Args&&... args)
        {
            static_assert(alignof(T) <= MaxAlignment, "アラインメントが大きすぎます");
            void* memory = Allocate(sizeof(T), alignof(T));
            if (memory == nullptr)
            {
                return nullptr;
            }
            return new (memory) T(std::forward<Args>(args)...);
        }

        // New で作ったオブジェクトを破棄する(デストラクタ + 返却)。nullptr なら何もしない。
        //
        // 中でやっていること:
        //   1. デストラクタを明示的に呼ぶ(メンバーが持つメモリの解放など、オブジェクトの後片付け)
        //   2. Free でメモリをアロケータに返す
        // デストラクタの明示的な呼び出しは、CLAUDE.md で、この Delete の中だけに許可している。
        //
        // 基底クラスのポインタで渡してもよい(T のデストラクタが virtual なら、実際の型のデストラクタが呼ばれる)。
        // ただし、object のアドレスが New で借りたメモリの先頭と一致している必要がある。
        // 継承が1本道(単一継承)なら一致する。複数のクラスを継承した型を、基底のポインタで渡してはいけない。
        // 戻り値: 返却できたら true。
        template <typename T>
        bool Delete(T* object)
        {
            if (object == nullptr)
            {
                return false;
            }
            object->~T();
            return Free(object); // T* は void* に自動で変換される(アドレスはそのまま)
        }

        // 管理しているメモリの大きさ(バイト)。
        std::size_t GetCapacity() const { return m_capacity; }

        // 貸し出し中のブロックの合計(ヘッダを含む、バイト)。
        std::size_t GetUsedBytes() const { return m_usedBytes; }

        // 空きブロックの数。多いほど細切れ(断片化)になっている。
        std::size_t GetFreeBlockCount() const;

        // 一番大きな空きブロックで、貸し出せる中身の大きさ(バイト)。これより大きい Allocate は失敗する。
        std::size_t GetLargestFreeSize() const;

    private:
        struct BlockHeader;

        // 空きブロックの「次の空きブロック」を読み書きする(中身の先頭に書いてある)。
        static BlockHeader*& NextFree(BlockHeader* block);

        // pointer が、このアロケータが貸し出したブロックの中身の先頭なら、そのヘッダを返す。違えば nullptr。
        BlockHeader* FindAllocatedHeader(void* pointer) const;

        std::unique_ptr<std::byte[]> m_memory; // メモリの塊。持ち主はこのアロケータ
        std::size_t m_capacity = 0;
        std::size_t m_usedBytes = 0;
        BlockHeader* m_freeList = nullptr;     // 空きブロックのリストの先頭(アドレスの小さい順に並べる)
    };
}
