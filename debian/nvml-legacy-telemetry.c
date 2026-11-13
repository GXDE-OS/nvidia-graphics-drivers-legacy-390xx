/*
 * NVML telemetry compatibility shim for the 390xx legacy driver (amd64).
 *
 * The 390.157 NVML library rejects most telemetry queries on pre-Kepler and
 * non-Titan GeForce products: nvmlDeviceGetUtilizationRates() and
 * nvmlDeviceGetClockInfo() return NVML_ERROR_NOT_SUPPORTED even though the
 * kernel driver does have the data.  The decision is made from a cached
 * capability field in the opaque nvmlDevice_t object, which is initialised
 * on the first telemetry query.
 *
 * This DSO is installed as libnvidia-ml.so.1.  It carries a DT_NEEDED entry
 * for the unmodified vendor library, so every NVML symbol except the two
 * intercepted below is resolved by the vendor DSO.  When a query is rejected
 * as "not supported", the shim clears the capability gate (after the vendor
 * library has populated its cache) and retries the query once.
 *
 * The field offset is private ABI of the amd64 390.157 NVML binary.  Do not
 * use this shim with any other driver version or architecture.  Devices that
 * already support these queries are never modified, because the shim only
 * acts after a NVML_ERROR_NOT_SUPPORTED result.
 *
 * Set NVML_LEGACY_TELEMETRY=0 in the environment to disable the workaround.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define NVML_ERROR_NOT_SUPPORTED            3

/* Offsets into the amd64 390.157 nvmlDevice_t object, in uint32_t units. */
#define NVML_DEVICE_CAPABILITY_U32_INDEX    356
#define NVML_DEVICE_CAPABILITY_TELEMETRY    6

typedef int (*nvml_utilization_fn)(void *, void *);
typedef int (*nvml_clock_fn)(void *, int, unsigned int *);

static int telemetry_disabled(void)
{
    static int checked;
    static int disabled;

    if (!checked) {
        const char *env = getenv("NVML_LEGACY_TELEMETRY");
        disabled = (env != NULL && strcmp(env, "0") == 0);
        checked = 1;
    }
    return disabled;
}

static void clear_capability_gate(void *device)
{
    ((uint32_t *)device)[NVML_DEVICE_CAPABILITY_U32_INDEX] =
        NVML_DEVICE_CAPABILITY_TELEMETRY;
}

int nvmlDeviceGetUtilizationRates(void *device, void *utilization)
{
    static nvml_utilization_fn real;
    int ret;

    if (!real)
        real = (nvml_utilization_fn)
            dlsym(RTLD_NEXT, "nvmlDeviceGetUtilizationRates");
    if (!real)
        return NVML_ERROR_NOT_SUPPORTED;

    ret = real(device, utilization);
    if (ret == NVML_ERROR_NOT_SUPPORTED && !telemetry_disabled()) {
        clear_capability_gate(device);
        ret = real(device, utilization);
    }
    return ret;
}

int nvmlDeviceGetClockInfo(void *device, int type, unsigned int *clock)
{
    static nvml_clock_fn real;
    int ret;

    if (!real)
        real = (nvml_clock_fn)dlsym(RTLD_NEXT, "nvmlDeviceGetClockInfo");
    if (!real)
        return NVML_ERROR_NOT_SUPPORTED;

    ret = real(device, type, clock);
    if (ret == NVML_ERROR_NOT_SUPPORTED && !telemetry_disabled()) {
        clear_capability_gate(device);
        ret = real(device, type, clock);
    }
    return ret;
}
