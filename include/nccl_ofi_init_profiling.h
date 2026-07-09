/*
 * Copyright (c) 2026 Amazon.com, Inc. or its affiliates. All rights reserved.
 */

#ifndef NCCL_OFI_INIT_PROFILING_H_
#define NCCL_OFI_INIT_PROFILING_H_

#include "config.h"

#if HAVE_INIT_PROFILING

#include <time.h>
#include "nccl_ofi_log.h"

#define INIT_PROFILE_BEGIN(name) \
	struct timespec _init_profile_ts_##name; \
	clock_gettime(CLOCK_MONOTONIC, &_init_profile_ts_##name);

#define INIT_PROFILE_END(name) do { \
	struct timespec _init_profile_ts_end_##name; \
	clock_gettime(CLOCK_MONOTONIC, &_init_profile_ts_end_##name); \
	double _ms_##name = (_init_profile_ts_end_##name.tv_sec - _init_profile_ts_##name.tv_sec) * 1000.0 + \
			    (_init_profile_ts_end_##name.tv_nsec - _init_profile_ts_##name.tv_nsec) / 1e6; \
	NCCL_OFI_INFO(NCCL_INIT, "INIT_PROFILE " #name ": %.3f ms", _ms_##name); \
} while (0)

#else

#define INIT_PROFILE_BEGIN(name)
#define INIT_PROFILE_END(name)

#endif /* HAVE_INIT_PROFILING */

#endif /* NCCL_OFI_INIT_PROFILING_H_ */
