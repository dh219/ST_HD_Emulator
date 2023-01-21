/* 
 * ATARI ST HDC Emulator
 *
 * File:    emushell.c
 * Author:  Steve Bradford
 * Created: 1st Nov 2022
 * 
 * Shell 
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pico/stdlib.h>
#include "pico.h"
#include "hardware/pll.h"
#include "hardware/clocks.h"
#include "hardware/structs/pll.h"
#include "hardware/structs/clocks.h"
#include "hardware/vreg.h"
#include "../emu.h"
#include "emushell.h"


int CPUCLK;

uint spi_set_baudrate_fast (spi_inst_t *spi, uint baudrate);


/* 
 * cpu clock
 * Minimum clock is 3 * 38 = 114 MHz MHz for this project
 * 
 * cpu clock speeds may seem odd, but they are calculated based on known good SPI
 * clock speeds
 * Gigastone    8GB max 41 MHz
 * Sandisk     16GB max 38 MHz
 * 
 * Need to implement automatic SPI clock setting
 */
bool cpuFreq ( char *input )
{	
    /*                     100            138            152       164       184       190            228       246            266       276            304      */
    int speeds [] = { 100, (4 * 25), 125, (3 * 46), 150, (4 * 38), (4 * 41), (4 * 46), (5 * 38), 200, (6 * 38), (6 * 41), 250, (7 * 38), (6 * 46), 300, (8 * 38) };
    int f;
    int currentClock = clock_get_hz ( clk_sys );

    
    /* just report speeds */
    if ( *input == 0 )
    {
        printf ( "CPU clock is running at %d MHz\n", currentClock / 1000 / 1000 );
        printf ( "SPI clock is running at %d MHz\n", spi_get_baudrate ( spi0 ) / 1000000 );

        return BAD;
    }

    f = atoi ( input );

    if ( f < speeds [0] || f > speeds [ (sizeof (speeds) / 4) - 1] )
    {
        printf ( "\tCPU clock is running at %d MHz\n", clock_get_hz ( clk_sys ) / 1000 / 1000 );

        return BAD;
    }
                                                
    for ( int i = 0; i < (sizeof (speeds) / 4); i++ )               /* set cpu clock to nearest (next highest) valid frequency */
    {
        if ( f <= speeds [i]  )
        {
            CPUCLK = speeds [i];
            break;
        }
    }

    if ( CPUCLK >= 300 )
        vreg_set_voltage ( VREG_VOLTAGE_1_25 ); /* overclock cpu voltage - norm is 1.10v */

    else if ( CPUCLK > 200 )
        vreg_set_voltage ( VREG_VOLTAGE_1_15 ); /* overclock cpu voltage - norm is 1.10v */

    else 
        vreg_set_voltage ( VREG_VOLTAGE_1_10 ); /* overclock cpu voltage - norm is 1.10v */

    sleep_ms (1);

    bool clock = set_sys_clock_khz ( CPUCLK * 1000, false ); /* set PICO clock speed */
    sleep_ms (100);

    if ( ! clock ) 
    {
        printf ( "\nUnable to set cpu clock to %d\n", CPUCLK );

        CPUCLK = currentClock;
    }

    else
    {
        /* I am struggling to sort the peripheral bus clock speed */
        /* I have had to create a copy of the "spi_set_baudrate" to get the */
        /* fastest SPI, which is now 24 MHz */

        /* At power-on the SPI is half the cpu clock, which = 62 MHz */
        /* and that gives us an SPI clock of 31 MHz. As soon as I change */
        /* the cpu clock speed, the peripheral clock changes to 48 MHz */
        /* which means the best SPI clock is 24 MHz */
/*
        clock_configure ( clk_peri, 
                        0, 
                        CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS, 
                        clock_get_hz ( clk_sys ), 
                        133000000 );
*/
        spi_set_baudrate_fast ( spi0, 0 );
    }

    printf ( "\tCPU now running at %d MHz\n", clock_get_hz ( clk_sys ) / 1000000 );
    printf ( "\tSPI clock is running at %d MHz\n", spi_get_baudrate ( spi0 ) / 1000000 );

    return GOOD;
}


/* orinal spi_set_baudrate () is in pico-sdk/src/rp2_common/hardware_spi/spi.c */

/* 
 * I am trying to optimise SPI bus clock - this is of course dependent upon the sd-card max speed 
 * best I have got is 41 MHz using Gigastone 8GB card 
 */
uint spi_set_baudrate_fast ( spi_inst_t *spi, uint baudrate ) 
{
    uint freq_in = clock_get_hz ( clk_peri );
    uint prescale, postdiv;

    /* divide by 4 or by 8 - them's the options */
    prescale = 2;
    postdiv  = 2;

    if ( freq_in > 200000000 )
        prescale = 4;
        
    /* cpu clock needs to be > 166 MHz to attain these higher SPI clocks */
    if ( (freq_in / prescale) < 41000000 )
        postdiv = 1;

    spi_get_hw(spi)->cpsr = prescale;
    hw_write_masked(&spi_get_hw(spi)->cr0, (postdiv - 1) << SPI_SSPCR0_SCR_LSB, SPI_SSPCR0_SCR_BITS);

   
    return freq_in / (prescale * postdiv);
}