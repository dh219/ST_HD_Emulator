/* 
 * ATARI ST HDC Emulator
 *
 * File:    sdtest.c
 * Author:  Steve Bradford
 * Created: December 2022
 * 
 * Shell command
 * 
 * SD Card speed test
 * 
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





uint32_t readTest ( sd_card_t *psrcdrv, int testNumber, int testSectors )
{
    uint64_t start;
    uint32_t duration;
    uint32_t testLBA;
    int      e;
    uint32_t testBytes = testSectors * 512;


    printf ( "Test %d: Read speed - %d %s (%d %s)\n", 
        testNumber, 
        testSectors, 
        testSectors == 1 ? "sector" : "sectors", 
        testBytes < 1000 ? testBytes : (testBytes < 1000000 ? testBytes / 1000 : testBytes / 1000000),
        testBytes < 1000 ? "B" : (testBytes < 1000000 ? "KB" : "MB") );

    testLBA = psrcdrv->sectors - 100000;     /* run tests outside FAT system - ie. raw */
    printf ( "Test LBA = %u\n", testLBA );
    start   = time_us_64 ();

    for ( uint32_t n = 0; n < testSectors; n++ )
    {
        if ( ( e = sd_read_blocks ( psrcdrv, DMAbuffer, testLBA + n, 1 )) != SD_BLOCK_DEVICE_ERROR_NONE )
        {
            printf ( "\nTest %d: read error %d\n", testNumber, e );

            break;
        }
    }

    duration = (time_us_64 () - start);       /* microseconds */

    printf ( "Test %d: completed in %u microseconds\n", testNumber, duration );
    printf ( "Test %d: read speed %.1f KB/s\n\n", testNumber, ((float)( (float)testBytes / 1024.0 ) / (float)duration ) * 1000000.0 );

    return duration;
}





/* 
 * arguments can be
 * 0 or 1
 * sd0 or sd1
 * disk0 or disk1
 * drive0 or drive1
 * anything so long as they end in 0 or 1
 */
bool sdtest ( char *src )
{
    bool ret = false;
    int  srcdsk;
    sd_card_t *psrcdrv;
    uint32_t duration;
    uint32_t testSectors;


    printf ( "Test disk is %s\n", src );
    printf ( "Is this correct [Y/N] " );

    if ( toupper ( getchar () ) == 'Y' )
    {
        printf ( "\n" );

        srcdsk = src [strlen (src) - 1] - '0';
        psrcdrv = drv [srcdsk].pSD;

        if ( psrcdrv->mounted )
        {            
            /* start tests */
            printf ( "Testing SD Card %s\n", src );

            testSectors = 1;
            duration = readTest ( psrcdrv, 1, testSectors );

            testSectors = 10;
            duration = readTest ( psrcdrv, 2, testSectors );

            testSectors = 100;
            duration = readTest ( psrcdrv, 3, testSectors );

            testSectors = 1000;
            duration = readTest ( psrcdrv, 4, testSectors );

            testSectors = 10000;
            duration = readTest ( psrcdrv, 5, testSectors );
        }

        else
        {
            printf ( "Test disk %d is not mounted\n", srcdsk );
        }
    }

    else
    {
        printf ( "\n" );
    }

    return ret;
}