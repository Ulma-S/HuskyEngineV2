// JobSystem.h の実装(J-1: ワーカー 0 本の同期実行)。
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

    JobSystem::~JobSystem()
    {
        RunPendingJobs();
    }

    void JobSystem::Schedule(Job job, JobCounter& counter)
    {
        // 先に数を増やしてから積む(積んだ直後に誰かが Wait しても、「まだ終わっていない」と分かるように)。
        counter.m_remaining.fetch_add(1, std::memory_order_relaxed);
        m_queue.push_back(Entry{std::move(job), &counter});
    }

    void JobSystem::Wait(JobCounter& counter)
    {
        while (!counter.IsDone())
        {
            if (!RunOneJob())
            {
                // 行列が空なのに終わっていない: 同期実行では起きないはず(積んでいないのに数だけ増えた)。
                // 永遠に回らないように抜ける。ワーカーがいるとき(J-2)は、ここで、ほかのスレッドの完了を待つ。
                break;
            }
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

    bool JobSystem::RunOneJob()
    {
        if (m_head == m_queue.size())
        {
            return false;
        }
        // ジョブを行列から取り出してから実行する。ジョブの中で Schedule されると、vector が大きくなって
        // 中身が引っ越すことがあり、行列の中を指したまま実行すると、引っ越し前の場所を触ってしまうため。
        Entry entry = std::move(m_queue[m_head]);
        ++m_head;

        entry.job();

        // 終わったことを印に書く。release: ジョブが書いた結果が、印を読んだ側(acquire)から必ず見えるようにする
        // (今は1つのスレッドだけなので効き目は無いが、J-2 でワーカーが書いた結果を、ゲームスレッドが読むときに要る)。
        entry.counter->m_remaining.fetch_sub(1, std::memory_order_release);

        // 行列が空になったら、中身だけ消して先頭から使い直す(確保したメモリは残るので、次に積むときに確保しない)。
        if (m_head == m_queue.size())
        {
            m_queue.clear();
            m_head = 0;
        }
        return true;
    }
}
