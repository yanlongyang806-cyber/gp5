/*
 * public TransGaming API function definitions
 *
 * Copyright 2012 TransGaming Inc.
 */
#ifndef  __TG_METRIC_H__
# define __TG_METRIC_H__

# ifdef __cplusplus
extern "C" {
# endif

/* Metric types */
enum _TGMetricType
{
    TG_METRIC_INVALID = 0,             /* Invalid metric */

    TG_METRIC_INTERNAL_MIN,            /* Internal metrics */
    TG_METRIC_INTERNAL_MAX = 63,

    TG_METRIC_GAME_LOAD_START = 64,    /* Game loading screen is starting */
    TG_METRIC_GAME_LOAD_END,           /* Game loading screen is closing */
    TG_METRIC_READY_USER_INTERACTION,  /* Game is ready for user interaction */
    TG_METRIC_LEVEL_LOAD_START,        /* Game level loading screen is starting */
    TG_METRIC_LEVEL_LOAD_END,          /* Game level loading screen is closing */
    TG_METRIC_GAME_EXIT_START,         /* Game is starting to shutdown */
    TG_METRIC_GAME_EXIT_END,           /* Game has finished shutdown process */

    TG_METRIC_COUNT                    /* Max metric type (expandable to 256) */
};

typedef BYTE TGMetricType;

/* TGSendMetric()  (NTDLL)
 *
 * Sends a metric to a monitoring agent.
 *
 *  Parameters:
 *      type [in]: The metric type.
 *
 *  Returns:
 *      TRUE on success, FALSE otherwise
 *
 *  Remarks:
 *      This call is only supported on GTTV.
 */
BOOL WINAPI TGSendMetric(TGMetricType type);

/* prototype definitions for the functions in this header */
typedef BOOL (WINAPI *TYPEOF(TGSendMetric))(TGMetricType);


# ifdef __cplusplus
}
# endif

#endif
