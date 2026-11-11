#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>

namespace det {

// One runnable guest thread. The key is guest state: priority, then thread id,
// then the signal or timer sequence that made this entry ready.
struct RunEntry {
    int priority = 0;
    uint32_t id = 0;
    uint64_t ready = 0;
};

class RunQueue {
  public:
    void Push(const RunEntry& e) {
        auto it = std::lower_bound(q_.begin(), q_.end(), e, Before);
        q_.insert(it, e);
    }

    bool Pop(RunEntry& out) {
        if (q_.empty()) return false;
        out = q_.front();
        q_.erase(q_.begin());
        return true;
    }

    void Clear() { q_.clear(); }
    size_t Size() const { return q_.size(); }

  private:
    static bool Before(const RunEntry& a, const RunEntry& b) {
        if (a.priority != b.priority) return a.priority < b.priority;
        if (a.id != b.id) return a.id < b.id;
        return a.ready < b.ready;
    }

    std::vector<RunEntry> q_;
};

}  // namespace det
