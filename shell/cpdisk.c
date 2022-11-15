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
#include "sd_card.h"
#include "diskio.h"

/* project specific includes */
#include "ff.h"
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

        if ( drv [srcdsk].mounted )
        {
            if ( drv [dstdsk].mounted )
            {
                /* start copy */
                printf ( "Copying %s to %s\n", src, dst );

                tx = drv [srcdsk].diskSize / 131072;   /* number of transfers using 128k buffer */
                start = time_us_64 ();

                for ( int n = 0; n < tx; n++ )
                {
                    if ( ( e = sd_read_blocks ( drv [srcdsk].pSD, DMAbuffer, n << 8, 256 )) == SD_BLOCK_DEVICE_ERROR_NONE )
                    {
                        if ( ( e = sd_write_blocks ( drv [dstdsk].pSD, DMAbuffer, n << 8, 256 )) == SD_BLOCK_DEVICE_ERROR_NONE )
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

                printf ( "\n" );
                printf ( "Copy completed in %u seconds\n", (uint32_t)((time_us_64 () - start) / 1000000) );
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