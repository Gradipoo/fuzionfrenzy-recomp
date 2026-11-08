// The run queue key is guest priority, guest thread id, then ready sequence.
#include "det/run_queue.h"

#include <cstdio>
#include <vector>

static int g_fails = 0;

static void Expect(bool ok, const char* what) {
    if (ok) return;
    std::fprintf(stderr, "FAIL %s\n", what);
    g_fails++;
}

static uint32_t Pop(det::RunQueue& q) {
    det::RunEntry e;
    return q.Pop(e) ? e.id : 0;
}

int main() {
    {
        det::RunQueue q;
        q.Push({0, 44, 1});
        q.Push({0, 40, 2});
        q.Push({0, 28, 3});
        Expect(Pop(q) == 28, "id order 1");
        Expect(Pop(q) == 40, "id order 2");
        Expect(Pop(q) == 44, "id order 3");
        Expect(Pop(q) == 0, "id order empty");
    }
    {
        det::RunQueue q;
        q.Push({2, 4, 1});
        q.Push({0, 9, 2});
        q.Push({1, 7, 3});
        Expect(Pop(q) == 9, "priority order 1");
        Expect(Pop(q) == 7, "priority order 2");
        Expect(Pop(q) == 4, "priority order 3");
    }
    {
        det::RunQueue q;
        q.Push({0, 7, 3});
        q.Push({0, 7, 1});
        q.Push({0, 7, 2});
        Expect(Pop(q) == 7, "ready tie 1");
        Expect(Pop(q) == 7, "ready tie 2");
        Expect(Pop(q) == 7, "ready tie 3");
        Expect(Pop(q) == 0, "ready tie empty");
    }
    {
        // A repeated fixed sequence must produce the same order every time.
        std::vector<uint32_t> first;
        for (int run = 0; run < 1000; run++) {
            det::RunQueue q;
            q.Push({0, 44, 1});
            q.Push({1, 36, 2});
            q.Push({0, 40, 3});
            q.Push({0, 28, 4});
            q.Push({1, 20, 5});
            std::vector<uint32_t> got;
            det::RunEntry e;
            while (q.Pop(e)) got.push_back(e.id);
            if (run == 0) first = got;
            else Expect(got == first, "repeat order stable");
        }
        Expect(first == std::vector<uint32_t>({28, 40, 44, 20, 36}), "fixed order");
    }
    if (g_fails) {
        std::fprintf(stderr, "test_run_queue: %d failure(s)\n", g_fails);
        return 1;
    }
    std::printf("test_run_queue: PASS\n");
    return 0;
}
