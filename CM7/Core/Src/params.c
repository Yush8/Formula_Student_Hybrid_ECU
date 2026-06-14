/*
 * params.c
 *
 *  Created on: 11 Jun 2026
 *      Author: Yusha
 */


#include "params.h"
#include "main.h"        /* HAL flash + cache, FLASH_* / SCB_* */
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>

/* ================================================================== */
/* Live values + defaults                                             */
/*                                                                    */
/* Both are GENERATED from params.def - add a parameter there, not    */
/* here. (Same X-macro trick as the CAN / log .def lists.)            */
/* ================================================================== */
Params_t g_params;

static const Params_t PARAMS_DEFAULT = {
#define PARAM_F32(name, def, lo, hi)  .name = def,
#define PARAM_I32(name, def, lo, hi)  .name = def,
#include "params.def"
#undef PARAM_F32
#undef PARAM_I32
};

/* ================================================================== */
/* Descriptor table: name, type, where it lives, and its safe range   */
/* Also generated from params.def, so it can never disagree with the  */
/* struct above.                                                       */
/* ================================================================== */
typedef enum { P_F32, P_I32 } ptype_t;

typedef struct {
    const char *name;
    ptype_t     type;
    void       *ptr;
    float       lo;
    float       hi;
} ParamDesc_t;

static const ParamDesc_t TABLE[] = {
    /*  name (stringized)   type    pointer            lo            hi   */
#define PARAM_F32(name, def, lo, hi)  { #name, P_F32, &g_params.name, (float)(lo), (float)(hi) },
#define PARAM_I32(name, def, lo, hi)  { #name, P_I32, &g_params.name, (float)(lo), (float)(hi) },
#include "params.def"
#undef PARAM_F32
#undef PARAM_I32
};
#define TABLE_COUNT (sizeof(TABLE) / sizeof(TABLE[0]))

/* ================================================================== */
/* Flash layout                                                       */
/*                                                                    */
/* CM7 code runs from bank 1, so we keep the config in BANK 2's last  */
/* sector. The H7 only allows read-while-write across banks, so this  */
/* lets a save erase/program without stalling the running CPU. The    */
/* CM4 stub lives at the start of bank 2 and never reaches sector 7.  */
/* ================================================================== */
#define CONFIG_FLASH_ADDR    0x081E0000UL    /* bank 2, sector 7 start */
#define CONFIG_FLASH_BANK    FLASH_BANK_2
#define CONFIG_FLASH_SECTOR  FLASH_SECTOR_7

#define CONFIG_MAGIC    0x32564348UL         /* tag so we recognise our blob */
#define CONFIG_VERSION  2U                   /* bump only for a SEMANTIC change; */
                                             /* adding/removing a param is caught */
                                             /* automatically by layout_id below. */

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t layout_id;  /* fingerprint of params.def (names+types). A change to  */
                         /* the parameter list changes this, so an old saved blob */
                         /* with a different layout is rejected -> safe defaults.  */
                         /* This is why you NEVER hand-bump a version for a param. */
    Params_t params;
    uint32_t crc;        /* over every byte before this field */
} ConfigBlob_t;

/* ================================================================== */
/* Small helpers                                                      */
/* ================================================================== */

/* newlib-nano printf has no %f, so format floats by hand (3 dp) */
static void f32_to_str(char *out, size_t n, float v)
{
    int neg = (v < 0.0f);
    if (neg) v = -v;
    long whole = (long)v;
    long frac  = (long)((v - (float)whole) * 1000.0f + 0.5f);
    if (frac >= 1000) { whole += 1; frac -= 1000; }
    snprintf(out, n, "%s%ld.%03ld", neg ? "-" : "", whole, frac);
}

static uint32_t crc32(const uint8_t *data, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFFUL;
    for (uint32_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++)
            crc = (crc >> 1) ^ (0xEDB88320UL & (uint32_t)(-(int32_t)(crc & 1U)));
    }
    return ~crc;
}

/* Fingerprint of the current parameter layout: an FNV-1a hash over each
 * parameter's name and type. It depends ONLY on params.def, so adding,
 * removing, renaming or retyping a parameter changes it. Stored in the saved
 * blob and checked on load, so stale flash from a different .def is rejected
 * and the board falls back to defaults - no manual version bump needed. */
static uint32_t params_layout_id(void)
{
    uint32_t h = 2166136261UL;                 /* FNV-1a offset basis */
    for (uint32_t i = 0; i < TABLE_COUNT; i++) {
        for (const char *n = TABLE[i].name; *n; n++) {
            h ^= (uint8_t)*n;
            h *= 16777619UL;
        }
        h ^= (uint8_t)TABLE[i].type;           /* P_F32 vs P_I32 */
        h *= 16777619UL;
        h ^= 0xFFU;                            /* entry separator */
        h *= 16777619UL;
    }
    return h;
}

static const ParamDesc_t *find(const char *name)
{
    for (uint32_t i = 0; i < TABLE_COUNT; i++)
        if (strcmp(name, TABLE[i].name) == 0) return &TABLE[i];
    return NULL;
}

static void format_value(const ParamDesc_t *d, char *out, size_t n)
{
    if (d->type == P_I32) snprintf(out, n, "%ld", (long)*(int32_t *)d->ptr);
    else                  f32_to_str(out, n, *(float *)d->ptr);
}

/* clamp one entry to its declared range; returns 1 if it had to move it */
static int clamp_one(const ParamDesc_t *d)
{
    int clamped = 0;
    if (d->type == P_F32) {
        float *p = (float *)d->ptr;
        if (*p < d->lo) { *p = d->lo; clamped = 1; }
        if (*p > d->hi) { *p = d->hi; clamped = 1; }
    } else {
        int32_t *p = (int32_t *)d->ptr;
        if (*p < (int32_t)d->lo) { *p = (int32_t)d->lo; clamped = 1; }
        if (*p > (int32_t)d->hi) { *p = (int32_t)d->hi; clamped = 1; }
    }
    return clamped;
}

static void clamp_all(void)
{
    for (uint32_t i = 0; i < TABLE_COUNT; i++) clamp_one(&TABLE[i]);
}

/* ================================================================== */
/* Flash access                                                       */
/* ================================================================== */

static void flash_read_blob(ConfigBlob_t *b)
{
    /* flash is memory-mapped, so reading is just a copy */
    memcpy(b, (const void *)CONFIG_FLASH_ADDR, sizeof(*b));
}

static int flash_write_blob(const ConfigBlob_t *b)
{
    /* H7 programs one 32-byte "flash word" at a time, so pad to a multiple */
    static uint8_t buf[((sizeof(ConfigBlob_t) + 31) / 32) * 32]
        __attribute__((aligned(32)));
    memset(buf, 0xFF, sizeof(buf));
    memcpy(buf, b, sizeof(*b));

    HAL_FLASH_Unlock();

    FLASH_EraseInitTypeDef er = {0};
    er.TypeErase = FLASH_TYPEERASE_SECTORS;
    er.Banks     = CONFIG_FLASH_BANK;
    er.Sector    = CONFIG_FLASH_SECTOR;
    er.NbSectors = 1;
    uint32_t sector_error = 0;
    HAL_StatusTypeDef st = HAL_FLASHEx_Erase(&er, &sector_error);

    for (uint32_t i = 0; (st == HAL_OK) && (i < sizeof(buf)); i += 32) {
        st = HAL_FLASH_Program(FLASH_TYPEPROGRAM_FLASHWORD,
                               CONFIG_FLASH_ADDR + i,
                               (uint32_t)(uintptr_t)&buf[i]);
    }

    HAL_FLASH_Lock();

    return (st == HAL_OK) ? 0 : -1;
}

/* ================================================================== */
/* Public API                                                         */
/* ================================================================== */

void Params_LoadDefaults(void)
{
    g_params = PARAMS_DEFAULT;
}

void Params_Init(void)
{
    ConfigBlob_t b;
    flash_read_blob(&b);

    if (b.magic     == CONFIG_MAGIC &&
        b.version   == CONFIG_VERSION &&
        b.layout_id == params_layout_id() &&
        b.crc       == crc32((const uint8_t *)&b, offsetof(ConfigBlob_t, crc))) {
        g_params = b.params;        /* a valid saved config */
    } else {
        Params_LoadDefaults();      /* blank / corrupt / old version / changed .def */
    }
    clamp_all();                    /* never trust stored values blindly */
}

int Params_Save(void)
{
    ConfigBlob_t b;
    b.magic     = CONFIG_MAGIC;
    b.version   = CONFIG_VERSION;
    b.layout_id = params_layout_id();
    b.params    = g_params;
    b.crc       = crc32((const uint8_t *)&b, offsetof(ConfigBlob_t, crc));
    return flash_write_blob(&b);
}

uint32_t Params_Count(void) { return TABLE_COUNT; }

int Params_Describe(uint32_t index, char *name, size_t name_sz,
                    char *value, size_t value_sz)
{
    if (index >= TABLE_COUNT) return -1;
    snprintf(name, name_sz, "%s", TABLE[index].name);
    format_value(&TABLE[index], value, value_sz);
    return 0;
}

int Params_GetFormatted(const char *name, char *value, size_t value_sz)
{
    const ParamDesc_t *d = find(name);
    if (!d) return -1;
    format_value(d, value, value_sz);
    return 0;
}

ParamStatus_t Params_SetFromString(const char *name, const char *valstr,
                                   char *applied, size_t applied_sz)
{
    const ParamDesc_t *d = find(name);
    if (!d) return PARAM_UNKNOWN;

    char *end;
    if (d->type == P_I32) {
        long v = strtol(valstr, &end, 0);     /* base 0 -> accepts 0x.. too */
        if (end == valstr) return PARAM_BADVALUE;
        *(int32_t *)d->ptr = (int32_t)v;
    } else {
        float v = strtof(valstr, &end);
        if (end == valstr) return PARAM_BADVALUE;
        *(float *)d->ptr = v;
    }

    int clamped = clamp_one(d);
    if (applied) format_value(d, applied, applied_sz);
    return clamped ? PARAM_CLAMPED : PARAM_OK;
}
