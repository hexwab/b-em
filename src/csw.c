/*B-em v2.2 by Tom Walker
  CSW cassette support*/
#include <stdio.h>
#include <stdlib.h>
#include <zlib.h>
#include "b-em.h"
#include "sysacia.h"
#include "csw.h"
#include "tape.h"

int csw_toneon=0;

static int csw_intone = 1, csw_indat = 0, csw_datbits = 0, csw_enddat = 0;
static uint8_t *csw_dat = NULL;
static int      csw_point;
static uint8_t  csw_head[0x34];
static int      csw_skip = 0;
static int      csw_loop = 1;
int csw_ena;

static void csw_read_failed(FILE *csw_f, const char *fn)
{
    if (ferror(csw_f))
        log_error("csw: read error on '%s': %s", fn, strerror(errno));
    else
        log_error("csw: premature EOF on '%s'", fn);
}

void csw_load(const char *fn)
{
    FILE *csw_f;
    int end,c;
    uint32_t destlen = 8 * 1024 * 1024;
    uint8_t *tempin;

    /*Allocate buffer*/
    if (!csw_dat) {
        if (!(csw_dat = malloc(8 * 1024 * 1024))) {
            log_error("csw: out of memory reading '%s'", fn);
            return;
        }
    }

    /*Open file and get size*/
    if (!(csw_f = fopen(fn,"rb"))) {
        log_warn("csw: uanble to open CSW file '%s': %s", fn, strerror(errno));
        return;
    }
    fseek(csw_f, -1, SEEK_END);
    end = ftell(csw_f);
    fseek(csw_f, 0, SEEK_SET);

    /*Read header*/
    if (fread(csw_head, 0x34, 1, csw_f) == 1) {
        for (c = 0; c < csw_head[0x23]; c++)
            getc(csw_f);
        /*Allocate temporary memory and read file into memory*/
        end -= ftell(csw_f);
        if ((tempin = malloc(end))) {
            if (fread(tempin, end, 1, csw_f) == 1) {
                fclose(csw_f);
                /*Decompress*/
                uncompress(csw_dat, (unsigned long *)&destlen, tempin, end);
                free(tempin);
                /*Reset data pointer*/
                csw_point = 0;
                acia_dcdhigh(&sysacia);
                tapellatch  = (1000000 / (1200 / 10)) / 64;
                tapelcount  = 0;
                tape_loaded = 1;
                return;
            }
            else {
                csw_read_failed(csw_f, fn);
                free(tempin);
            }
        }
        else
            log_error("csw: out of memory reading '%s'", fn);
    }
    else
        csw_read_failed(csw_f, fn);
    fclose(csw_f);
}

void csw_close()
{
    if (csw_dat) {
        free(csw_dat);
        csw_dat = NULL;
    }
}

int ffound, fdat;
int infilenames;
static void csw_receive(uint8_t val)
{
        csw_toneon--;
        if (infilenames)
        {
                ffound = 1;
                fdat = val;
        }
        else
           acia_receive(&sysacia, val);
}

void csw_poll()
{
        int c;
        uint8_t dat;
        if (!csw_dat) return;

        for (c = 0; c < 10; c++)
        {
                dat = csw_dat[csw_point++];

                if (csw_point >= (8 * 1024 * 1024))
                {
                        csw_point = 0;
                        csw_loop = 1;
                }
                if (csw_skip)
                   csw_skip--;
                else if (csw_intone && dat > 0xD) /*Not in tone any more - data start bit*/
                {
                        csw_point++; /*Skip next half of wave*/
                        if (sysacia_tapespeed) csw_skip = 6;
                        csw_intone = 0;
                        csw_indat = 1;

                        csw_datbits = csw_enddat = 0;
                        acia_dcdlow(&sysacia);
                        return;
                }
                else if (csw_indat && csw_datbits != -1 && csw_datbits != -2)
                {
                        csw_point++; /*Skip next half of wave*/
                        if (sysacia_tapespeed) csw_skip = 6;
                        csw_enddat >>= 1;

                        if (dat <= 0xD)
                        {
                                csw_point += 2;
                                if (sysacia_tapespeed) csw_skip += 6;
                                csw_enddat |= 0x80;
                        }
                        csw_datbits++;
                        if (csw_datbits == 8)
                        {
                                csw_receive(csw_enddat);

                                csw_datbits = -2;
                                return;
                        }
                }
                else if (csw_indat && csw_datbits == -2) /*Deal with stop bit*/
                {
                        csw_point++;
                        if (sysacia_tapespeed) csw_skip = 6;
                        if (dat <= 0xD)
                        {
                                csw_point += 2;
                                if (sysacia_tapespeed) csw_skip += 6;
                        }
                        csw_datbits = -1;
                }
                else if (csw_indat && csw_datbits == -1)
                {
                        if (dat <= 0xD) /*Back in tone again*/
                        {
                                acia_dcdhigh(&sysacia);
                                csw_toneon  = 2;
                                csw_indat   = 0;
                                csw_intone  = 1;
                                csw_datbits = 0;
                                return;
                        }
                        else /*Start bit*/
                        {
                                csw_point++; /*Skip next half of wave*/
                                if (sysacia_tapespeed) csw_skip += 6;
                                csw_datbits = 0;
                                csw_enddat  = 0;
                        }
                }
        }
}

#define getcswbyte()            ffound = 0; \
                                while (!ffound && !csw_loop) \
                                { \
                                        csw_poll(); \
                                } \
                                if (csw_loop) break;

static uint8_t ffilename[16];
void csw_findfilenames()
{
        int temp, temps, tempd, tempi, tempsk, tempspd;
        uint8_t tb;
        int c;
        int fsize = 0;
        char s[256];
        uint32_t run, load;
        uint8_t status;
        int skip;
        if (!csw_dat) return;
        temp    = csw_point;
        temps   = csw_indat;
        tempi   = csw_intone;
        tempd   = csw_datbits;
        tempsk  = csw_skip;
        tempspd = sysacia_tapespeed;
        csw_point = 0;

        csw_indat = csw_intone = csw_datbits = csw_skip = 0;
        csw_intone = 1;
        sysacia_tapespeed = 0;

//        gzseek(csw,12,SEEK_SET);
        csw_loop = 0;
        infilenames = 1;
        while (!csw_loop)
        {
//                log_debug("Start\n");
                ffound = 0;
                while (!ffound && !csw_loop)
                {
                        csw_poll();
                }
                if (csw_loop) break;
//                log_debug("FDAT %02X csw_toneon %i\n",fdat,csw_toneon);
                if (fdat == 0x2A && csw_toneon == 1)
                {
                        c = 0;
                        do
                        {
                                ffound = 0;
                                while (!ffound && !csw_loop)
                                {
                                        csw_poll();
                                }
                                if (csw_loop) break;
                                ffilename[c++] = fdat;
                        } while (fdat != 0x0 && c <= 10);
                        if (csw_loop) break;
                        c--;
                        while (c < 13) ffilename[c++] = 32;
                        ffilename[c] = 0;

                                getcswbyte();
                                tb = fdat;
                                getcswbyte();
                                load = tb | (fdat << 8);
                                getcswbyte();
                                tb = fdat;
                                getcswbyte();
                                load |= (tb | (fdat << 8)) << 16;

                                getcswbyte();
                                tb = fdat;
                                getcswbyte();
                                run = tb | (fdat << 8);
                                getcswbyte();
                                tb = fdat;
                                getcswbyte();
                                run |= (tb | (fdat << 8)) << 16;

                                getcswbyte();
                                getcswbyte();

                                getcswbyte();
                                tb = fdat;
                                getcswbyte();
                                skip = tb | (fdat << 8);

                                fsize += skip;

                                getcswbyte();
                                status = fdat;

//log_debug("Got block - %08X %08X %02X\n",load,run,status);
                        if (status & 0x80)
                        {
                                sprintf(s, "%s Size %04X Load %08X Run %08X", ffilename, fsize, load, run);
                                cataddname(s);
                                fsize = 0;
                        }
                        for (c = 0; c < skip + 8; c++)
                        {
                                getcswbyte();
                        }
                }
        }
        infilenames = 0;
        csw_indat   = temps;
        csw_intone  = tempi;
        csw_datbits = tempd;
        csw_skip    = tempsk;
        sysacia_tapespeed = tempspd;
        csw_point   = temp;
        csw_loop = 0;
}

