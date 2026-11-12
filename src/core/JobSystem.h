// ジョブシステム: 処理を小さな「仕事(ジョブ)」に分けて積み、ワーカースレッド(と、待っているスレッド)で実行する仕組み。
// C# の Task.Run と Task.Wait、Unity の Job System の Schedule と Complete にあたる。
//
// 使い方:
//   JobSystem jobs(3);                                // ワーカースレッドを 3 本作る(0 本なら同期実行)
//   JobCounter counter;                               // 完了の印。使う側が持つ(ローカル変数やメンバー)
//   jobs.Schedule([&] { DecodeImage(...); }, counter); // 積む(空いているワーカーが、すぐに実行し始める)
//   jobs.Schedule([&] { DecodeSound(...); }, counter); // 同じ印に、いくつでも積める
//   jobs.Wait(counter);                               // 印のジョブがすべて終わるまで待つ(待ちながら、自分でも実行する)
//
// 決まり:
//   - ジョブの中で、GC 管理のオブジェクト(アクターやコンポーネントなど)を触らない。触ってよいのは、ただのデータだけ。
//     (ジョブはワーカースレッドで動く。CLAUDE.md: GC 管理オブジェクトはゲームスレッド専用)
//   - 2つのジョブが、同じデータを同時に書き換えない(鍵が無いので、データ競合になる)。ジョブごとに書く場所を分ける。
//   - JobCounter は、そのジョブがすべて終わる(Wait が戻る)まで壊さない。ジョブは終わったときに印を書き換えるため。
//   - ジョブの中から、さらにジョブを積んでよい(同じ印に積めば、Wait はその分も待つ)。
//
// スレッドの間で共有するもの(データ競合を起こさないために、これだけに絞っている):
//   - ジョブの行列 … m_mutex(鍵)で守る。行列を触るのは、鍵を持っている間だけ
//   - 完了の印    … std::atomic の整数
//   ジョブそのものは、鍵を外してから実行する(実行中に鍵を持ち続けると、ほかのスレッドが行列を触れず、並列にならない)。
//
// ワーカー 0 本(同期実行)のとき:
//   Schedule は積むだけで、Wait(か RunPendingJobs)を呼んだスレッドが、積んだ順にジョブを実行する。
//   実行の順番がいつも同じなので、テストの結果が毎回同じになり、不具合を「スレッドのせいか」切り分けられる。
//
// 動作スレッド: Schedule と Wait は、どのスレッドからも呼んでよい(ジョブの中からも)。作る・壊すのはゲームスレッド。
#pragma once

#include <atomic>
#include <condition_variable> // std::condition_variable: 「条件が整うまで眠る」「整ったら起こす」ための仕組み
#include <cstddef>
#include <cstdint>
#include <functional> // std::function
#include <mutex>      // std::mutex: 鍵(C# の lock の中身)
#include <thread>     // std::thread
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
        // true が返ったら、ジョブが書いたデータは、このスレッドから読んでよい(acquire。JobSystem.cpp を参照)。
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

        // 行列に、最初から確保しておく場所の数(ジョブ 1 件は約 70 バイトなので、約 70 KB)。
        // これを超えて同時に積まれると、そのときだけ vector が大きくなって確保が起きる(動きは正しいまま)。
        static constexpr std::size_t InitialQueueCapacity = 1024;

        // workerCount: ワーカースレッドの数。0 なら同期実行(スレッドを作らない)。
        // ワーカーが全員動き出すまで待ってから戻る(戻ったら、すぐにジョブを実行できる)。
        explicit JobSystem(std::size_t workerCount = 0);
        // ワーカーに「終わり」を伝え、積まれていたジョブをすべて実行し終えてから、ワーカーの終わりを待つ(join)。
        ~JobSystem();

        JobSystem(const JobSystem&) = delete;
        JobSystem& operator=(const JobSystem&) = delete;

        // ゲームに使うワーカーの数の目安: CPU のコアの数 - 1(1 本はゲームスレッドの分)。少なくとも 1。
        static std::size_t RecommendedWorkerCount();

        std::size_t GetWorkerCount() const { return m_workers.size(); }

        // ジョブを積む。終わったら counter の残りの数を 1 減らす。眠っているワーカーがいれば、1本起こす。
        void Schedule(Job job, JobCounter& counter);

        // counter のジョブがすべて終わるまで待つ。行列にジョブがあれば、積んだ順に自分でも実行する
        // (counter と関係のないジョブも、先に積まれていれば実行する)。行列が空なら、ほかのスレッドが終えるのを眠って待つ。
        void Wait(JobCounter& counter);

        // 積まれているジョブを、行列が空になるまで、このスレッドで実行する(実行中に積まれたものも)。戻り値: 実行した数。
        // ワーカーがいるときは、ワーカーが実行中のジョブは待たない(待つなら Wait を使う)。
        std::size_t RunPendingJobs();

        // まだ誰も実行し始めていないジョブの数。確認とテスト用(ワーカーがいると、読んだ直後にも変わる)。
        std::size_t GetPendingCount() const;

    private:
        struct Entry
        {
            Job job;
            JobCounter* counter = nullptr;
        };

        // 行列の先頭のジョブを1つ取り出して実行する。戻り値: 実行したら true(行列が空なら false)。
        bool RunOneJob();
        // 鍵を持った状態で、行列の先頭を取り出す。空なら false。
        bool PopLocked(Entry& outEntry);
        // ジョブを実行し、印を 1 減らす。0 になったら、待っているスレッドを起こす(鍵は持たずに呼ぶ)。
        void Execute(Entry& entry);
        // ワーカースレッドの中身: ジョブを取り出しては実行し、無ければ眠る。終わりを伝えられ、行列が空になったら戻る。
        void WorkerLoop();

        // ---- ここから下(行列、終わりの印、動き出したワーカーの数)は、m_mutex を持っている間だけ触る ----
        // ジョブの行列。vector を「先頭の位置(m_head)」付きで使い、空になったら中身だけ消して最初から使い直す。
        // こうすると、一度大きくなった後は、積むたびのメモリの確保が起きない(std::deque は塊ごとに確保と解放を繰り返す)。
        std::vector<Entry> m_queue;
        std::size_t m_head = 0;
        bool m_stopping = false; // 壊すときに true にする(ワーカーに終わりを伝える)
        std::size_t m_startedWorkerCount = 0; // 動き出したワーカーの数(コンストラクタが、全員そろうまで待つ)
        // mutable: const の関数(GetPendingCount)の中でも、鍵をかけられるようにする。
        mutable std::mutex m_mutex;
        // ---- ここまで ----

        std::condition_variable m_jobAvailable; // 「ジョブが積まれた / 終わりを伝えた」でワーカーを起こす
        std::condition_variable m_jobFinished;  // 「どれかの印が 0 になった」「ワーカーが動き出した」で、眠っているスレッドを起こす
        std::vector<std::thread> m_workers;      // 最後に宣言する(ほかのメンバーができてから、スレッドを動かし始めるため)
    };
}
