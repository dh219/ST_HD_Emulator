/*
 * ATARI ST HDC Emulator
 *
 * File:    emumount.c
 * Author:  Steve Bradford
 * Created: November 2022
 * 
 */

#include <stdio.h>
#include <stdbool.h>
#include "../sdcard/sd_card.h"
//#include "../fatfs/diskio.h"

/* project specific includes */
#include "../emuscsi.h"
#include "../emu.h"
#include "emushell.h"


extern DRIVES drv [];


void emumount ( int disk )
{
    if ( mountRAW ( disk ) == 0 )
    {
        printf ( "\nERROR: no drives mounted\n\n" );
    }
}


void emuunmount ( int disk )
{
    if ( drv [disk].pSD->mounted )
    {
        
        drv [disk].pSD->mounted  = false;
        drv [disk].pSD->m_Status = STA_NOINIT;
        drv [disk].pSD->sectors  = 0;
        drv [disk].packetCount   = 0;
                                                /* card has been unmounted but is still inserted, so eject it */
        if ( gpio_get (drv [disk].pSD->card_detect_gpio) == drv [disk].pSD->card_detected_true )
            drv [disk].ejected   = true;

        printf ( "\n%s unmounted\n", drv [disk].pSD->pcName );
    }
}