#include "statistics/statistics.h"

PomodoroStepKind Pomodoro_GetStepKind(int index) {
    if (index < 0) return POMODORO_STEP_INVALID;
    return (index % 2) == 0 ? POMODORO_STEP_FOCUS : POMODORO_STEP_BREAK;
}
