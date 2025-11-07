// High-resolution timer RAII helper for Windows
#pragma once

#ifdef _WIN32
#include <Windows.h>
#include <timeapi.h>

// RAII wrapper that raises system timer resolution while in scope.
// Use with care: higher resolution increases power consumption.
class HighResolutionTimer {
public:
    explicit HighResolutionTimer(UINT period_ms = 1) : m_period(period_ms) {
        timeBeginPeriod(m_period);
    }
    ~HighResolutionTimer() {
        timeEndPeriod(m_period);
    }

    HighResolutionTimer(const HighResolutionTimer&) = delete;
    HighResolutionTimer& operator=(const HighResolutionTimer&) = delete;
    HighResolutionTimer(HighResolutionTimer&&) = delete;
    HighResolutionTimer& operator=(HighResolutionTimer&&) = delete;

private:
    UINT m_period;
};
#endif // _WIN32