#pragma once
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>

namespace thandor {
// A single joined writer owns formatting and disk I/O. Periodic jobs coalesce;
// boundary jobs drain in order. No task reads the live editor or simulation worker.
class AsyncDiagnosticLog {
public:
    using Job = std::function<void(std::ostream&)>;
    struct Stats { uint64_t written{}, coalesced{}, failures{}; double last_write_ms{}, peak_write_ms{}; };
    AsyncDiagnosticLog(const std::filesystem::path& path, std::string header) {
        stream_.open(path,std::ios::out|std::ios::trunc);
        if(!stream_)throw std::runtime_error("Unable to open "+path.string());
        stream_.exceptions(std::ios::badbit|std::ios::failbit);
        stream_<<header;
        writer_=std::jthread([this]{run();});
    }
    ~AsyncDiagnosticLog(){close("writer_destroyed");}
    AsyncDiagnosticLog(const AsyncDiagnosticLog&)=delete;
    AsyncDiagnosticLog& operator=(const AsyncDiagnosticLog&)=delete;
    bool enqueue(Job job,bool boundary=false) {
        Job retired;
        std::unique_lock lock(mutex_);
        if(closing_)return false;
        // Only boundary samples can wait for space. Normal UI samples never wait
        // for formatting and never grow an unbounded immutable snapshot backlog.
        if(boundary)space_.wait(lock,[&]{return closing_||jobs_.size()<4;});
        if(closing_)return false;
        if(!boundary&&!jobs_.empty()&&!jobs_.back().boundary){
            retired=std::move(jobs_.back().job);jobs_.back().job=std::move(job);++stats_.coalesced;
        }else if(!boundary&&jobs_.size()>=4){++stats_.coalesced;return false;}
        else jobs_.push_back({std::move(job),boundary});
        lock.unlock();ready_.notify_one();return true;
    }
    Stats stats()const {std::lock_guard lock(mutex_);return stats_;}
    void close(const std::string& reason) {
        {std::lock_guard lock(mutex_);if(closing_)return;closing_=true;reason_=reason;}
        ready_.notify_one();space_.notify_all();
        if(writer_.joinable())writer_.join();
    }
private:
    struct Entry {Job job;bool boundary{};};
    void run()noexcept {
        for(;;){
            Entry entry;
            {std::unique_lock lock(mutex_);ready_.wait(lock,[&]{return closing_||!jobs_.empty();});
             if(jobs_.empty())break;
             entry=std::move(jobs_.front());jobs_.pop_front();}
            space_.notify_all();
            const auto start=std::chrono::steady_clock::now();
            bool failed=false;
            try{entry.job(stream_);stream_.flush();}catch(...){failed=true;}
            const double elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
            {std::lock_guard lock(mutex_);++stats_.written;stats_.failures+=failed?1U:0U;
             stats_.last_write_ms=elapsed;stats_.peak_write_ms=std::max(stats_.peak_write_ms,elapsed);}
        }
        try{stream_<<"END reason="<<reason_<<'\n';stream_.flush();stream_.close();}catch(...){}
    }
    std::ofstream stream_;
    mutable std::mutex mutex_;
    std::condition_variable ready_,space_;
    std::deque<Entry> jobs_;
    Stats stats_;
    bool closing_{};
    std::string reason_;
    std::jthread writer_;
};
} // namespace thandor
