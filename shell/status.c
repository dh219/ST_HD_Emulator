/* 
 * ATARI ST HDC Emulator
 *
 * File:    status.c
 * Author:  Steve Bradford
 * Created: 4th Nov 2022
 * 
 * shell commands 
 * Syntax:  status
 * 
 * return true on success
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include "pico/stdlib.h"
#include <pico/util/datetime.h>
#include "hardware/rtc.h"
#include "../emu.h"
#include "emushell.h"

extern DRIVES drv [];


bool status ( void )
{
    DRIVES *pdrv;
    bool ret = BAD;

    printf ( "%s\n", TITLE );

    /* display hard drives connected */
    for ( int i = 0; i < MAX_DRIVES; i++ )
    {
        pdrv = &drv [i];

        //if ( pdrv->mounted )
        //{
            printf ( "%s %s - command packets %d\n", 
                pdrv->pSD->pcName, 
                pdrv->pSD->mounted ? "mounted  " : "unmounted",
                pdrv->packetCount );
        //}
    }

    /* display partitions */
    /* display packet count per drive */
    /* display uptime, cpu clock, spi clock */
    uptime ();
    cpuFreq ( "" );

    return ret;
}