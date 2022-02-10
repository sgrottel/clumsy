#pragma once
#include <stdlib.h>
#include <Windows.h>
#include <stdint.h>

#include "iup.h"
#include "common.h"

//---------------------------------------------------------------------
// rate stats
//---------------------------------------------------------------------
typedef struct {
	int32_t initialized;
	uint32_t oldest_index;
	uint32_t oldest_ts;
	int64_t accumulated_count;
	int32_t sample_num;
	int window_size;
	float scale;
	uint32_t *array_sum;
	uint32_t *array_sample;
} CRateStats;


extern CRateStats* crate_stats_new(int window_size, float scale);

extern void crate_stats_delete(CRateStats *rate);

extern void crate_stats_reset(CRateStats *rate);

// call when packet arrives, count is the packet size in bytes
extern void crate_stats_update(CRateStats *rate, int32_t count, uint32_t now_ts);

// calculate rate
extern int32_t crate_stats_calculate(CRateStats *rate, uint32_t now_ts);
