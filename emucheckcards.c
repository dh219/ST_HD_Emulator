/* 
 * ATARI ST HDC Emulator
 *
 * File:    emucheckcards.c
 * Author:  Steve Bradford
 * Created: November 2022
 * 
 * 
 */

#include "emu.h"

#define INSERTED        false
#define NOT_INSERTED    true


extern DRIVES drv[];


extern void emumount ( int );
extern void emuunmount ( int );


void checkSDcards ( void )
{
    static uint64_t elapsed = 0;
    DRIVES *pdrv;


    if ( (time_us_64 () - elapsed) > 1000000 )  /* only check once per second */
    {
        for ( int d = 0; d < MAX_DRIVES; d++ )
        {
            pdrv = &drv [d];
            //printf ( "%s prevState = %d, ejected = %d, mounted = %d, cdgpio = %d\n", __func__, pdrv->prevState, pdrv->ejected, pdrv->pSD->mounted, pdrv->pSD->card_detect_gpio );
            /* check to see if card has been removed then re-inserted */
            if ( pdrv->prevState != gpio_get (pdrv->pSD->card_detect_gpio) )
            {
                pdrv->prevState = gpio_get (pdrv->pSD->card_detect_gpio);

                if ( pdrv->ejected )
                    pdrv->ejected = false;    /* reset ejected flag */
            }

            else if ( gpio_get (pdrv->pSD->card_detect_gpio) == INSERTED )
            {
                if ( ! pdrv->pSD->mounted && ! pdrv->ejected )
                    emumount (d);
            }

            else if ( gpio_get (pdrv->pSD->card_detect_gpio) == NOT_INSERTED )
            {
                if ( pdrv->pSD->mounted )
                    emuunmount (d);
            }
        }

        elapsed = time_us_64 ();
    }
}