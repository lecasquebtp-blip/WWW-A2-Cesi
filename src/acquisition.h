#ifndef ACQUISITION_H
#define ACQUISITION_H

#include <stdbool.h>
#include <stddef.h>
#include "cesi_types.h"

typedef struct
{
    bool rtc_error;
    bool gps_error;
    bool sensor_error;
    bool data_error;
} AcqStatus;

void Acquisition_Init(void);
AcqStatus Acquisition_Run(bool with_gps, char *line, size_t len, RTC_DateTime *now);

#endif
