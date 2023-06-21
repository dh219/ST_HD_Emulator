/*
 * ATARI ST HDC Emulator
 * 
 * File:    emuinit.c
 * Author:  Steve Bradford
 * Created: September 2022
 *
 * PICO hardware initialisation
 */

#include <stdio.h>
#include <pico/stdlib.h>
#include "hardware/clocks.h"
#include "hardware/structs/clocks.h"
#include "emuinit.h"
#include "shell/emushell.h"


int EMUinit ( void )
{
    /* initialise cpu clock to xxx MHz. No need, other than to get faster SPI clock */

    /* NOTE setting clock here, has a direct impact on the SPI clock. If it's too high
     * the sd card may not work. 
     */
    /* 125 MHz seems to offer me the best compromise - SPI clock > 31 MHz is too high 
     * for some cards I have 
     */
    set_sys_clock_khz ( 133 * 1000, false );
    
    clock_configure ( clk_peri, 
                      0, 
                      CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS,
                      clock_get_hz ( clk_sys ), 
                      clock_get_hz ( clk_sys ) );
    
    stdio_init_all ();                          /* CMakeLists.txt assigns serial out device - USB / UART */
    sleep_ms ( 5000 );                          /* give time to minicom (or similar) to establish comms */

    /* initialise GPIO pins */
    gpio_init_mask         ( GPIO_MASK );       /* initialise all GPIO pins for this project - initially all inputs */
    gpio_set_dir_masked    ( GPIO_MASK, GPIO_OUTPUTS_MASK );  
    gpio_set_dir_in_masked ( ACSI_DATA_MASK );

#if !PROJECT_HARDWARE
    /* when hardware is built, remove these */
    gpio_pull_up ( IRQ );
    gpio_pull_up ( DRQ );
    gpio_pull_up ( MICROSD_CARD_CS0 );
    gpio_pull_up ( MICROSD_CARD_CS1 );
    gpio_pull_up ( MICROSD_CARD_CD0 );
    gpio_pull_up ( MICROSD_CARD_CD1 );
#endif

    /* OE on the bidirectional data-bus chip and control-bus chip must be kept low on power-up */
    /* so a higher drive strength is needed to compensate - 3.3v / 1kR = 3mA */
    gpio_set_drive_strength ( DATA_BUS_CNTRL,    GPIO_DRIVE_STRENGTH_4MA ); 
    gpio_set_drive_strength ( CONTROL_BUS_CNTRL, GPIO_DRIVE_STRENGTH_4MA ); 

    /* Likewise, IRQ and DRQ must be held high on power-up */ 
    gpio_set_drive_strength ( IRQ, GPIO_DRIVE_STRENGTH_4MA ); 
    gpio_set_drive_strength ( DRQ, GPIO_DRIVE_STRENGTH_4MA ); 

    gpio_put ( DATA_BUS_CNTRL,    HI );
    gpio_put ( CONTROL_BUS_CNTRL, HI );
    gpio_put ( IRQ,               HI );
    gpio_put ( DRQ,               HI );

    return 1;
}

