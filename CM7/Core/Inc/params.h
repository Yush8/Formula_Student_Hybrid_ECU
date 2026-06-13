/*
 * params.h
 *
 *  Created on: 11 Jun 2026
 *      Author: Yusha
 */

#ifndef PARAMS_H
#define PARAMS_H

#include <stdint.h>
#include <stddef.h>

/*
 * Parameter store: the single source of truth for tunable values.
 *   - lives in RAM as g_params (read by the bridge, written by the console)
 *   - loads from / saves to internal flash, integrity-checked with a CRC
 *   - out-of-range values are clamped to safe limits on load and on set
 *
 * To add a parameter: add a field to Params_t, a default in PARAMS_DEFAULT,
 * and a row in the table (all in params.c). The console and flash pick it up
 * automatically.
 *
 * NOTE: safety-critical limits (AIR / precharge thresholds, fault trips) do
 * NOT belong here. Keep those as const in firmware so that no console command
 * or edited flash image can ever move them.
 */

typedef struct {
    float   kp;
    float   ki;
    int32_t torque_limit;
    int32_t Parameter_1;
} Params_t;

extern Params_t g_params;   /* the live values */

typedef enum {
    PARAM_OK = 0,
    PARAM_UNKNOWN,    /* no parameter with that name */
    PARAM_BADVALUE,   /* could not parse the value */
    PARAM_CLAMPED     /* accepted, but limited to the allowed range */
} ParamStatus_t;

void          Params_Init(void);          /* flash -> g_params, or defaults if invalid */
void          Params_LoadDefaults(void);  /* g_params = built-in defaults (RAM only)   */
int           Params_Save(void);          /* g_params -> flash; 0 = ok, -1 = fail       */

uint32_t      Params_Count(void);
int           Params_Describe(uint32_t index, char *name, size_t name_sz,
                              char *value, size_t value_sz);              /* 0 ok, -1 bad index */
int           Params_GetFormatted(const char *name, char *value, size_t value_sz); /* 0 ok, -1 unknown */
ParamStatus_t Params_SetFromString(const char *name, const char *valstr,
                                    char *applied, size_t applied_sz);

#endif /* PARAMS_H */
