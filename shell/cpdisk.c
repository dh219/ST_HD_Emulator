/* 
 * ATARI ST HDC Emulator
 *
 * File:    cpdisk.c
 * Author:  Steve Bradford
 * Created: November 2022
 * 
 * Shell command
 */

#include <stdio.h>
#include <stdbool.h>
#include <ctype.h>
#include <string.h>
#include "../sdcard/sd_card.h"

/* project specific includes */
#include "../emu.h"
#include "emushell.h"


extern uint8_t   DMAbuffer [];
extern DRIVES    drv [];
extern sd_card_t sd_cards [];


/* 
 * arguments can be
 * 0 or 1
 * sd0 or sd1
 * disk0 or disk1
 * drive0 or drive1
 * anything so long as they end in 0 or 1
 */
bool cpdisk ( char *src, char *dst )
{
    bool ret = false;
    int  srcdsk, dstdsk;
    sd_card_t *psrcdrv, *pdstdrv;
    uint64_t start;
    uint32_t duration;
    uint32_t e, r, w;
    uint32_t tx;


    printf ( "SRC disk is %s, DST disk is %s\n", src, dst );
    printf ( "Is this correct [Y/N] " );

    if ( toupper ( getchar () ) == 'Y' )
    {
        printf ( "\n" );

        srcdsk = src [strlen (src) - 1] - '0';
        psrcdrv = drv [srcdsk].pSD;

        dstdsk = dst [strlen (dst) - 1] - '0';
        pdstdrv = drv [dstdsk].pSD;

        if ( psrcdrv->mounted )
        {
            if ( pdstdrv->mounted )
            {
                /* start copy */
                printf ( "Copying %s to %s\n", src, dst );

                tx    = psrcdrv->sectors + 1;   
                start = time_us_64 ();

                for ( uint32_t n = 0; n < tx; n++ )
                {
                    if ( ( e = sd_read_blocks ( psrcdrv, DMAbuffer, n, 1 )) == SD_BLOCK_DEVICE_ERROR_NONE )
                    {
                        if ( ( e = sd_write_blocks ( pdstdrv, DMAbuffer, n, 1 )) == SD_BLOCK_DEVICE_ERROR_NONE )
                        {
                            printf ( " %%%3.1f complete\r", ((float)n / (float)tx) * 100 );
                        }

                        else
                        {
                            printf ( "\ncpdisk: write error %d\n", e );
                        }
                    }

                    else
                    {
                        printf ( "\ncpdisk: read error %d\n", e );
                    }
                }

                duration = (time_us_64 () - start) / 1000000;       /* seconds */

                printf ( "\n" );
                printf ( "Copy completed in %u seconds\n", duration );
                printf ( "%.1f KB/s\n", (float)((psrcdrv->sectors * 512) / 1024 ) / (float)duration );
            }

            else
            {
                printf ( "DST disk %d is not mounted\n", dstdsk );
            }
        }

        else
        {
            printf ( "SRC disk %d is not mounted\n", srcdsk );
        }
    }

    else
    {
        printf ( "\n" );
    }

    return ret;
}