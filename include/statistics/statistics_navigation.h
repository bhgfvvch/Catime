#ifndef CATIME_STATISTICS_NAVIGATION_H
#define CATIME_STATISTICS_NAVIGATION_H

#include "statistics/statistics_types.h"

void StatisticsNavigation_Move(StatisticsRangeKind range,
                               SYSTEMTIME* anchor, int direction);
BOOL StatisticsNavigation_CanMoveNext(StatisticsRangeKind range,
                                      const SYSTEMTIME* anchor,
                                      const SYSTEMTIME* today);

#endif
