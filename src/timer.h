
#ifndef _TIMER_H_INCLUDED_
#define _TIMER_H_INCLUDED_

#include <ctime>
#include <chrono>
#include <thread>

class Timer
{
public:
    Timer() : _start(0) {}
    ~Timer() {}

    /**
     * @brief Reset the timer.
     */
    void reset() {
        _start = clock();
    }

    /**
     * @brief Return the number of milliseconds since the timer was either
     * started or reset.
     */
    unsigned long get() {
        return (clock() - _start) * 1000L / CLOCKS_PER_SEC;
    }

    /**
     * @brief Wait for the value of the timer to reach \p limit.
     * @param limit     Timer limit (in milliseconds)
     */
    void wait(unsigned long limit) {
        unsigned long now = get();
        if (now < limit)
            std::this_thread::sleep_for(std::chrono::milliseconds(limit - now));
    }

private:
    clock_t _start;
};

#endif /* _TIMER_H_INCLUDED_ */
