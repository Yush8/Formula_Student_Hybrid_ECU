/*
 * console.c
 *
 *  Created on: 10 Jun 2026
 *      Author: Yusha
 */


#include "console.h"
#include "usbd_cdc_if.h"   /* CDC_Read(), CDC_Transmit_FS(), USBD_BUSY */
#include "params.h"        /* the parameter store this console drives */
#include "main.h"          /* HAL_GetTick() */
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

/* ------------------------------------------------------------------ */
/* Line buffer                                                        */
/* ------------------------------------------------------------------ */
static char     line[96];
static uint16_t line_len = 0;

/* ------------------------------------------------------------------ */
/* Output helpers (busy-safe TX)                                      */
/* ------------------------------------------------------------------ */
static void Console_Write(const uint8_t *buf, uint16_t len)
{
    uint32_t t0 = HAL_GetTick();
    while (CDC_Transmit_FS((uint8_t *)buf, len) == USBD_BUSY) {
        if ((HAL_GetTick() - t0) > 10) return;   /* no host reading -> drop */
    }
}

static void Console_Print(const char *s)
{
    Console_Write((const uint8_t *)s, (uint16_t)strlen(s));
}

static void Console_Printf(const char *fmt, ...)
{
    char buf[128];
    va_list ap;
    va_start(ap, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (len > 0) {
        uint16_t out = (len < (int)sizeof(buf)) ? (uint16_t)len
                                                : (uint16_t)(sizeof(buf) - 1);
        Console_Write((const uint8_t *)buf, out);
    }
}

/* ------------------------------------------------------------------ */
/* Command dispatch  (all parameter work delegates to params.c)        */
/* ------------------------------------------------------------------ */
static void process_line(char *s)
{
    char *cmd = strtok(s, " ");
    if (!cmd) return;

    if (strcmp(cmd, "help") == 0 || strcmp(cmd, "?") == 0) {
        Console_Print("commands:\r\n"
                      "  list             show all parameters\r\n"
                      "  get <name>       read one\r\n"
                      "  set <name> <val> change one (RAM, live)\r\n"
                      "  save             write current values to flash\r\n"
                      "  defaults         reset to built-in defaults (RAM)\r\n"
                      "  ping             link check\r\n");

    } else if (strcmp(cmd, "ping") == 0) {
        Console_Print("pong\r\n");

    } else if (strcmp(cmd, "list") == 0) {
        char name[24], val[24];
        for (uint32_t i = 0; i < Params_Count(); i++) {
            Params_Describe(i, name, sizeof(name), val, sizeof(val));
            Console_Printf("%s = %s\r\n", name, val);
        }

    } else if (strcmp(cmd, "get") == 0) {
        char *name = strtok(NULL, " ");
        char val[24];
        if (!name) { Console_Print("usage: get <name>\r\n"); return; }
        if (Params_GetFormatted(name, val, sizeof(val)) == 0)
            Console_Printf("%s = %s\r\n", name, val);
        else
            Console_Printf("unknown parameter: %s\r\n", name);

    } else if (strcmp(cmd, "set") == 0) {
        char *name = strtok(NULL, " ");
        char *val  = strtok(NULL, " ");
        char applied[24];
        if (!name || !val) { Console_Print("usage: set <name> <value>\r\n"); return; }
        switch (Params_SetFromString(name, val, applied, sizeof(applied))) {
            case PARAM_OK:       Console_Printf("ok: %s = %s\r\n", name, applied);          break;
            case PARAM_CLAMPED:  Console_Printf("ok (clamped): %s = %s\r\n", name, applied); break;
            case PARAM_UNKNOWN:  Console_Printf("unknown parameter: %s\r\n", name);          break;
            case PARAM_BADVALUE: Console_Print("bad value\r\n");                             break;
        }

    } else if (strcmp(cmd, "save") == 0) {
        if (Params_Save() == 0) Console_Print("saved to flash\r\n");
        else                    Console_Print("save FAILED\r\n");

    } else if (strcmp(cmd, "defaults") == 0) {
        Params_LoadDefaults();
        Console_Print("defaults loaded (use 'save' to keep them)\r\n");

    } else {
        Console_Printf("unknown command: %s  (try 'help')\r\n", cmd);
    }
}

/* ------------------------------------------------------------------ */
/* Public API                                                         */
/* ------------------------------------------------------------------ */
void Console_Init(void)
{
    line_len = 0;
    Console_Print("\r\nHCU V2 console ready. Type 'help'.\r\n> ");
}

void Console_Poll(void)
{
    static char last_eol = 0;
    uint8_t  rx[64];
    uint16_t n = CDC_Read(rx, sizeof(rx));

    for (uint16_t i = 0; i < n; i++) {
        uint8_t c = rx[i];

        if (c == '\r' || c == '\n') {
            /* swallow the partner of a CR+LF / LF+CR pair */
            if ((c == '\n' && last_eol == '\r') ||
                (c == '\r' && last_eol == '\n')) {
                last_eol = 0;
                continue;
            }
            last_eol = c;

            Console_Print("\r\n");
            line[line_len] = '\0';
            if (line_len > 0) process_line(line);
            line_len = 0;
            Console_Print("> ");
        } else {
            last_eol = 0;

            if (c == 0x08 || c == 0x7F) {            /* backspace / DEL */
                if (line_len > 0) {
                    line_len--;
                    Console_Print("\b \b");
                }
            } else if (c >= 0x20 && c < 0x7F) {      /* printable: echo + store */
                if (line_len < sizeof(line) - 1) {
                    line[line_len++] = (char)c;
                    Console_Write(&c, 1);
                }
            }
        }
    }
}
