/* 
 * ATARI ST HDC Emulator
 *
 * File:    emusdcard.c
 * Author:  Steve Bradford
 * Created: 1st Nov 2022
 * 
 * Shell 
 */

#include <stdio.h>
#include <pico/stdlib.h>
#include "pico.h"   
#include "hardware/spi.h"
#include "emu.h"





#if USEDMA
void spi0_dma_isr();
#endif

// Hardware Configuration of SPI "objects"
// Note: multiple SD cards can be driven by one SPI if they use different slave
// selects.
spi_t spis[] = {                        // One for each SPI.
    {
        .hw_inst     = spi0,            // SPI instance
        .miso_gpio   = SPI_SDO,         // SDO
        .mosi_gpio   = SPI_SDI,         // SDI
        .sck_gpio    = SPI_CLK,         // CLK

        /* 
         * The choice of SD card does matter 
         * Samsung EVO Plus 24MHz and 50MHz (CPU CLOCK needs to be 200 MHz) tested OK
         */
        .baud_rate   = 50 * MHZ,
                                        // Following attributes are dynamically assigned
#if USEDMA
        .dma_isr     = spi0_dma_isr,
#endif
        .initialized = false,           // initialized flag
    }
};

// Hardware Configuration of the SD Card "objects"
sd_card_t sd_cards[] = {         // One for each SD card
    {
        .pcName             = "sd0:",   // Name used to mount device - refer to ffconf.h
        .spi                = &spis[0], // Pointer to the SPI driving this card
        .ss_gpio            = MICROSD_CARD_CS0, // The SPI slave select GPIO for this SD card
        .card_detect_gpio   = MICROSD_CARD_CD0,        // Card detect
        .card_detected_true = false,       // What the GPIO read returns when a card is
                                        // present. Use -1 if there is no card detect.
        .use_card_detect    = true,
                                        // Following attributes are dynamically assigned
        .m_Status           = STA_NOINIT,
        .sectors            = 0,
        .card_type          = 0,
    },

    {
        .pcName             = "sd1:",   // Name used to mount device - refer to ffconf.h
        .spi                = &spis[0], // Pointer to the SPI driving this card
        .ss_gpio            = MICROSD_CARD_CS1, // The SPI slave select GPIO for this SD card
        .card_detect_gpio   = MICROSD_CARD_CD1,        // Card detect
        .card_detected_true = false,      // What the GPIO read returns when a card is
                                        // present. Use -1 if there is no card detect.
        .use_card_detect    = true,
                                        // Following attributes are dynamically assigned
        .m_Status           = STA_NOINIT,
        .sectors            = 0,
        .card_type          = 0,
    }
};


#if USEDMA
void spi0_dma_isr() { spi_irq_handler(&spis[0]); }
#endif


/* ********************************************************************** */

size_t sd_get_num() { return count_of(sd_cards); }

sd_card_t *sd_get_by_num(size_t num) {
    if (num <= sd_get_num()) {
        return &sd_cards[num];
    } else {
        return NULL;
    }
}

size_t spi_get_num() { return count_of(spis); }

spi_t *spi_get_by_num(size_t num) {
    if (num <= sd_get_num()) {
        return &spis[num];
    } else {
        return NULL;
    }
}


/* ------------------------------------------------------------------------- */

/* the following needs to be included for ff_conf.h when using multiple partitions */

/*
PARTITION VolToPart [FF_VOLUMES] = {
    {0, 0},    
    {0, 1},    
    {0, 2},     
    {0, 3},    
                
    {1, 0},
    {1, 1},
    {1, 2},
    {1, 3}
};
*/