// JobSystem.h の実装。
//
// 鍵(m_mutex)と、2つの condition_variable の使い方:
//   - 行列(m_queue、m_head)と m_stopping は、鍵を持っている間だけ触る。
//   - ワーカーは、行列が空なら m_jobAvailable で眠る。Schedule が1本起こす。
//   - Wait は、行列が空で印も終わっていなければ m_jobFinished で眠る。印を 0 にしたスレッドが、全員を起こす。
//   condition_variable::wait(lock, 条件) は「条件が偽なら、鍵を外して眠り、起こされたら鍵をかけ直して条件を見直す」を
//   繰り返す。鍵を外すのと眠るのが一度に行われるので、「眠る直前に起こされて、起こされたことに気づかない」ことが無い。
//
// 完了の印のメモリの順序:
//   ジョブが終わったら、印を release で 1 減らす。印を読む側(IsDone)は acquire で読む。
//   この組み合わせで、「ジョブが書いたデータ」は、「印が 0 になったのを見たスレッド」から必ず見える。
//   (release: それより前の書き込みを、先に済ませてから印を書く。acquire: 印を読んでから、後の読み込みをする)
#include "core/JobSystem.h"

#include <cassert> // assert: Debug ビルドだけ、条件が偽ならその場で止める(Release では何もしない)
#include <utility> // std::move

namespace husky::core
{
    JobCounter::~JobCounter()
    {
        // 残りがあるまま壊すと、後で終わったジョブが、もう無いこの印を書き換えてメモリを壊す。
        assert(m_remaining.load(std::memory_order_acquire) == 0 && "ジョブが終わる前に JobCounter を壊しています");
    }

    JobSystem::JobSystem(std::size_t workerCount)
    {
        // 行列の場所を、最初にまとめて確保しておく。ワーカーが積むそばから実行するので、行列がいつ一番長くなるかは
        // 毎回変わる。後で初めて長くなったときに vector が大きくなり、ゲームの途中で確保が起きるのを避けるため。
        m_queue.reserve(InitialQueueCapacity);
        m_workers.reserve(workerCount);
        for (std::size_t i = 0; i < workerCount; ++i)
        {
            // std::thread を作ると、すぐに新しいスレッドで WorkerLoop が動き始める。
            // [this]: このオブジェクトのメンバー関数を、新しいスレッドから呼ぶ。
            m_workers.emplace_back([this] { WorkerLoop(); });
        }

        // ワーカーが全員動き出すまで待つ。
        // std::thread を作っても、OS がスレッドを実際に動かし始めるのは少し後で、そのとき OS や C++ の実行時ライブラリが
        // スレッドごとのデータ(thread_local の変数など)を用意する(メモリの確保も起きる)。
        // ここで待っておくと、「作り終わったら、ワーカーはすぐ働ける」「後から、起動の分の確保が紛れ込まない」と言える。
        std::unique_lock<std::mutex> lock(m_mutex);
        m_jobFinished.wait(lock, [this] { return m_startedWorkerCount == m_workers.size(); });
    }

    JobSystem::~JobSystem()
    {
        // 1. ワーカーに終わりを伝え、眠っているワーカーを全員起こす。
        {
            // lock_guard: 作ったときに鍵をかけ、この { } を抜けるときに自動で外す(RAII。C# の lock 文と同じ)。
            std::lock_guard<std::mutex> lock(m_mutex);
            m_stopping = true;
        }
        m_jobAvailable.notify_all();

        // 2. ワーカーは、行列を空にしてから戻る。全員が戻るのを待つ(join)。
        //    C++ では、std::thread を join せずに壊すと、プログラムが止まる(std::terminate)。
        for (std::thread& worker : m_workers)
        {
            worker.join();
        }

        // 3. ワーカー 0 本のときは、残りをここで実行する(印を書き換え忘れないため)。
        RunPendingJobs();
    }

    std::size_t JobSystem::RecommendedWorkerCount()
    {
        // hardware_concurrency: 同時に動かせるスレッドの数(論理コアの数)。分からなければ 0。
        const unsigned int cores = std::thread::hardware_concurrency();
        return cores > 1 ? cores - 1 : 1;
    }

    void JobSystem::Schedule(Job job, JobCounter& counter)
    {
        // 先に数を増やしてから積む(積んだ直後に誰かが Wait しても、「まだ終わっていない」と分かるように)。
        counter.m_remaining.fetch_add(1, std::memory_order_relaxed);
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_queue.push_back(Entry{std::move(job), &counter});
        }
        // 鍵を外してから起こす(起きたワーカーが、すぐに鍵をかけられるように)。
        m_jobAvailable.notify_one();
    }

    void JobSystem::Wait(JobCounter& counter)
    {
        while (!counter.IsDone())
        {
            // 行列にジョブがあれば、待つ代わりに自分で実行する(待つだけのスレッドを作らない)。
            if (RunOneJob())
            {
                continue;
            }
            if (m_workers.empty())
            {
                // ワーカーがいないのに、行列が空で終わっていない: 積んでいないのに数だけ増えた(起きないはず)。
                // ほかに終わらせるスレッドがいないので、永遠に眠らないように抜ける。
                break;
            }
            // 行列は空だが、ワーカーがこの印のジョブを実行中。終わるか、新しいジョブが積まれるまで眠る。
            std::unique_lock<std::mutex> lock(m_mutex); // unique_lock: wait に渡せる鍵(眠る間、外したりかけ直したりできる)
            m_jobFinished.wait(lock, [this, &counter] { return counter.IsDone() || m_head != m_queue.size(); });
        }
    }

    std::size_t JobSystem::RunPendingJobs()
    {
        std::size_t count = 0;
        while (RunOneJob())
        {
            ++count;
        }
        return count;
    }

    std::size_t JobSystem::GetPendingCount() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_queue.size() - m_head;
    }

    bool JobSystem::RunOneJob()
    {
        Entry entry;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (!PopLocked(entry))
            {
                return false;
            }
        }
        Execute(entry); // 鍵を外してから実行する
        return true;
    }

    bool JobSystem::PopLocked(Entry& outEntry)
    {
        if (m_head == m_queue.size())
        {
            return false;
        }
        // ジョブを行列から取り出して(移して)から実行する。行列の中を指したまま実行すると、
        // ジョブの中の Schedule で vector が大きくなって中身が引っ越したとき、引っ越し前の場所を触ってしまうため。
        outEntry = std::move(m_queue[m_head]);
        ++m_head;
        // 行列が空になったら、中身だけ消して先頭から使い直す(確保したメモリは残るので、次に積むときに確保しない)。
        if (m_head == m_queue.size())
        {
            m_queue.clear();
            m_head = 0;
        }
        return true;
    }

    void JobSystem::Execute(Entry& entry)
    {
        entry.job();

        // 終わったことを印に書く(release。ファイルの最初の説明を参照)。fetch_sub は「引く前の値」を返す。
        // 印が 0 になった後は、待っていた側が印を壊してよいので、この後で印に触ってはいけない。
        const std::int32_t before = entry.counter->m_remaining.fetch_sub(1, std::memory_order_release);
        if (before == 1)
        {
            // 鍵をかけてから起こす。Wait が「印を見て、まだなので眠る」の途中(鍵を持っている間)に、
            // ここで起こしてしまい、起こされたことに気づかずに眠り続けるのを防ぐため。
            std::lock_guard<std::mutex> lock(m_mutex);
            m_jobFinished.notify_all();
        }
    }

    void JobSystem::WorkerLoop()
    {
        // 動き出したことを、コンストラクタ(で待っているスレッド)に知らせる。
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            ++m_startedWorkerCount;
        }
        m_jobFinished.notify_all();

        for (;;)
        {
            Entry entry;
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                // ジョブが積まれるか、終わりを伝えられるまで眠る。
                m_jobAvailable.wait(lock, [this] { return m_stopping || m_head != m_queue.size(); });
                if (!PopLocked(entry))
                {
                    return; // 終わりを伝えられ、行列も空になった
                }
            }
            Execute(entry);
        }
    }
}
