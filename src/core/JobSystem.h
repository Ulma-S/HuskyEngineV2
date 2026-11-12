// ジョブシステム: 処理を小さな「仕事(ジョブ)」に分けて積み、後でまとめて(将来は複数のスレッドで)実行する仕組み。
// C# の Task.Run と Task.Wait、Unity の Job System の Schedule と Complete にあたる。
//
// 使い方:
//   JobSystem jobs;                                   // 今はワーカー 0 本(同期実行)だけ
//   JobCounter counter;                               // 完了の印。使う側が持つ(ローカル変数やメンバー)
//   jobs.Schedule([&] { DecodeImage(...); }, counter); // 積む(この時点では、まだ実行しない)
//   jobs.Schedule([&] { DecodeSound(...); }, counter); // 同じ印に、いくつでも積める
//   jobs.Wait(counter);                               // 印のジョブがすべて終わるまで待つ(待ちながら、自分でも実行する)
//
// 決まり:
//   - ジョブの中で、GC 管理のオブジェクト(アクターやコンポーネントなど)を触らない。触ってよいのは、ただのデータだけ。
//     (将来、ジョブはゲームスレッド以外で動く。CLAUDE.md: GC 管理オブジェクトはゲームスレッド専用)
//   - JobCounter は、そのジョブがすべて終わる(Wait が戻る)まで壊さない。ジョブは終わったときに印を書き換えるため。
//   - ジョブの中から、さらにジョブを積んでよい(同じ印に積めば、Wait はその分も待つ)。
//
// 同期実行(ワーカー 0 本)のとき:
//   Schedule は積むだけで、Wait(か RunPendingJobs)を呼んだスレッドが、積んだ順にジョブを実行する。
//   実行の順番がいつも同じなので、テストの結果が毎回同じになり、不具合を「スレッドのせいか」切り分けられる。
//   ワーカーを足したとき(J-2)も、使う側のコード(Schedule と Wait)は変わらない。
//
// 動作スレッド: 今はゲームスレッドのみ(J-2 で、Schedule と Wait をどのスレッドからも呼べるようにする)。
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional> // std::function
#include <vector>

namespace husky::core
{
    // ジョブの完了の印。積んだジョブの「残りの数」を数える。0 になったら、積んだジョブがすべて終わった。
    // std::atomic の整数1つだけなので、メモリを確保しない。複数のスレッドから同時に足し引きしても壊れない。
    class JobCounter final
    {
    public:
        JobCounter() = default;
        // 残りのジョブがある(まだ誰かがこの印を書き換える)のに壊すのは誤り。開発中に気づけるように止める。
        ~JobCounter();

        // コピーも移動も禁止する。積んだジョブは、この印の「場所」(アドレス)を覚えているため。
        JobCounter(const JobCounter&) = delete;
        JobCounter& operator=(const JobCounter&) = delete;

        // 積んだジョブが、すべて終わったか(1つも積んでいなければ true)。
        bool IsDone() const { return m_remaining.load(std::memory_order_acquire) == 0; }

    private:
        friend class JobSystem; // 数を足し引きするのは JobSystem だけ

        std::atomic<std::int32_t> m_remaining{0};
    };

    class JobSystem final
    {
    public:
        // ジョブの中身。引数も戻り値も無い関数(ラムダ式など)。
        // std::function は、中身(ラムダ式が持つ変数)が小さければ自分の中にしまい、大きいとヒープから確保する。
        // 毎フレーム積むジョブは、持つ変数を少なく(ポインタ数個まで)しておくと、確保が起きない。
        using Job = std::function<void()>;

        JobSystem() = default;
        // 積んだまま実行されていないジョブがあれば、壊す前にすべて実行する(印を書き換え忘れないため)。
        ~JobSystem();

        JobSystem(const JobSystem&) = delete;
        JobSystem& operator=(const JobSystem&) = delete;

        // ジョブを積む。終わったら counter の残りの数を 1 減らす。この時点では、まだ実行しない。
        void Schedule(Job job, JobCounter& counter);

        // counter のジョブがすべて終わるまで待つ。待っている間、積まれているジョブを、積んだ順に自分で実行する
        // (counter と関係のないジョブも、先に積まれていれば実行する。順番を飛ばさないため)。
        void Wait(JobCounter& counter);

        // 積まれているジョブを、すべて実行する(実行中に積まれたものも)。戻り値: 実行した数。
        std::size_t RunPendingJobs();

        // まだ実行していないジョブの数。確認とテスト用。
        std::size_t GetPendingCount() const { return m_queue.size() - m_head; }

    private:
        struct Entry
        {
            Job job;
            JobCounter* counter = nullptr;
        };

        // 行列の先頭のジョブを1つ取り出して実行する。戻り値: 実行したら true(行列が空なら false)。
        bool RunOneJob();

        // ジョブの行列。vector を「先頭の位置(m_head)」付きで使い、空になったら中身だけ消して最初から使い直す。
        // こうすると、一度大きくなった後は、積むたびのメモリの確保が起きない(std::deque は塊ごとに確保と解放を繰り返す)。
        std::vector<Entry> m_queue;
        std::size_t m_head = 0;
    };
}
