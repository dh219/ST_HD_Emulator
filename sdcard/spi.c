/* spi.c
Copyright 2021 Carl John Kugler III

Licensed under the Apache License, Version 2.0 (the License); you may not use
this file except in compliance with the License. You may obtain a copy of the
License at

   http://www.apache.org/licenses/LICENSE-2.0
Unless required by applicable law or agreed to in writing, software distributed
under the License is distributed on an AS IS BASIS, WITHOUT WARRANTIES OR
CONDITIONS OF ANY KIND, either express or implied. See the License for the
specific language governing permissions and limitations under the License.
*/

#include <stdio.h>
#include <stdbool.h>
#include "pico/stdlib.h"
#include "spi.h"
#include "../emu.h"

#if USEDMA
static bool irqChannel1 = false;
static bool irqShared = true;

void spi_irq_handler(spi_t *pSPI) {
    if (irqChannel1) {
        if (dma_hw->ints1 & 1u << pSPI->rx_dma) {  // Ours?
            dma_hw->ints1 = 1u << pSPI->rx_dma;    // clear it
            dma_channel_is_busy(pSPI->rx_dma);
        }
    } else {
        if (dma_hw->ints0 & 1u << pSPI->rx_dma) {  // Ours?
            dma_hw->ints0 = 1u << pSPI->rx_dma;    // clear it
            dma_channel_is_busy(pSPI->rx_dma);
        }
    }
   
}

void set_spi_dma_irq_channel(bool useChannel1, bool shared) {
    irqChannel1 = useChannel1;
    irqShared = shared;
}
#endif


bool my_spi_init(spi_t *pSPI) {

    if (!pSPI->initialized) {

        /* Configure component */
        // Enable SPI at 400 kHz and connect to GPIOs
        spi_init(pSPI->hw_inst, 400 * 1000);
        spi_set_format(pSPI->hw_inst, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);

        //gpio_set_function(pSPI->miso_gpio, GPIO_FUNC_SPI);
        //gpio_set_function(pSPI->mosi_gpio, GPIO_FUNC_SPI);
        //gpio_set_function(pSPI->sck_gpio, GPIO_FUNC_SPI);

        // SD cards' DO MUST be pulled up.
        //gpio_pull_up(pSPI->miso_gpio);

#if USEDMA
        // Grab some unused dma channels
        pSPI->tx_dma = dma_claim_unused_channel(true);
        pSPI->rx_dma = dma_claim_unused_channel(true);

        pSPI->tx_dma_cfg = dma_channel_get_default_config(pSPI->tx_dma);
        pSPI->rx_dma_cfg = dma_channel_get_default_config(pSPI->rx_dma);
        channel_config_set_transfer_data_size(&pSPI->tx_dma_cfg, DMA_SIZE_8);
        channel_config_set_transfer_data_size(&pSPI->rx_dma_cfg, DMA_SIZE_8);

        // We set the outbound DMA to transfer from a memory buffer to the SPI
        // transmit FIFO paced by the SPI TX FIFO DREQ The default is for the
        // read address to increment every element (in this case 1 byte -
        // DMA_SIZE_8) and for the write address to remain unchanged.
        channel_config_set_dreq(&pSPI->tx_dma_cfg, spi_get_index(pSPI->hw_inst)
                                                       ? DREQ_SPI1_TX
                                                       : DREQ_SPI0_TX);
        channel_config_set_write_increment(&pSPI->tx_dma_cfg, false);

        // We set the inbound DMA to transfer from the SPI receive FIFO to a
        // memory buffer paced by the SPI RX FIFO DREQ We coinfigure the read
        // address to remain unchanged for each element, but the write address
        // to increment (so data is written throughout the buffer)
        channel_config_set_dreq(&pSPI->rx_dma_cfg, spi_get_index(pSPI->hw_inst)
                                                       ? DREQ_SPI1_RX
                                                       : DREQ_SPI0_RX);
        channel_config_set_read_increment(&pSPI->rx_dma_cfg, false);

        /* Theory: we only need an interrupt on rx complete,
        since if rx is complete, tx must also be complete. */

        // Configure the processor to run dma_handler() when DMA IRQ 0/1 is
        // asserted:
        int irq = irqChannel1 ? DMA_IRQ_1 : DMA_IRQ_0;
        if (irqShared) {
            irq_add_shared_handler(
                irq, pSPI->dma_isr,
                PICO_SHARED_IRQ_HANDLER_DEFAULT_ORDER_PRIORITY);
        } else {
            irq_set_exclusive_handler(irq, pSPI->dma_isr);
        }

        // Tell the DMA to raise IRQ line 0/1 when the channel finishes a block
        if (irqChannel1) {
            dma_channel_set_irq1_enabled(pSPI->rx_dma, true);
        } else {
            dma_channel_set_irq0_enabled(pSPI->rx_dma, true);
        }
        irq_set_enabled(irq, true);
#endif
        pSPI->initialized = true;
    }

    return true;
}



/* 
 * spi_write_read_blocking () is called frequently
 * it needs to be optimised to gain performance
 * 
 * I have created my version plus two other functions, which are doing the same thing, 
 * but broken down for read and write operations. This has improved performance by over 10%
 * 
 * It might be worth looking in to adding dma for long transfers only
 */
#if USEDMA
extern spi_t *spi_get_by_num (size_t num) ;
#endif

static uint8_t rxdummy = 0xa5;
static uint8_t txdummy = SPI_FILL_CHAR;
static const size_t fifo_depth = 8;


int my_spi_read_blocking (spi_inst_t *spi, uint8_t *rx, size_t len)
{
#if USEDMA == 0
    register int rx_remaining = len, tx_remaining = len;


    while ( rx_remaining || tx_remaining ) 
    {
        if ( ( tx_remaining && (spi0_hw->sr & 0x02) ) && ( rx_remaining < (tx_remaining + fifo_depth) ) )
        {
            spi0_hw->dr = SPI_FILL_CHAR;
            tx_remaining--;
        }

        if ( rx_remaining && (spi0_hw->sr & 0x04) )
        {
            *rx++ = spi0_hw->dr & 0xff;
            rx_remaining--;
        }
    }
#else
    spi_t *pSPI = spi_get_by_num (0);

    channel_config_set_read_increment( &pSPI->tx_dma_cfg, false);
    channel_config_set_write_increment( &pSPI->rx_dma_cfg, true);

    dma_hw->ints0 = 1u <<  pSPI->rx_dma;

    dma_channel_configure(pSPI->tx_dma, &pSPI->tx_dma_cfg,
                        &spi_get_hw(pSPI->hw_inst)->dr,  // write address
                        &txdummy,                              // read address
                        len,  // element count (each element is of
                                // size transfer_data_size)
                        false);  // start

    dma_channel_configure(pSPI->rx_dma, &pSPI->rx_dma_cfg,
                        rx,                              // write address
                        &spi_get_hw(pSPI->hw_inst)->dr,  // read address
                        len,  // element count (each element is of
                                // size transfer_data_size)
                        false);  // start

    dma_start_channel_mask((1u << pSPI->tx_dma) | (1u << pSPI->rx_dma));
    dma_channel_wait_for_finish_blocking(pSPI->rx_dma);
#endif

    return len;
}



int my_spi_write_blocking (spi_inst_t *spi, const uint8_t *tx, size_t len)
{
#if USEDMA == 0
    register int rx_remaining = len, tx_remaining = len;


    while ( rx_remaining || tx_remaining ) 
    {
        if ( tx_remaining && (spi0_hw->sr & 0x02) )
        {
            spi0_hw->dr = *tx++;
            tx_remaining--;
        }

        if ( rx_remaining && (spi0_hw->sr & 0x04) )
        {
            rxdummy = spi0_hw->dr;
            rx_remaining--;
        }
    }
#else
    spi_t *pSPI = spi_get_by_num (0);

    channel_config_set_read_increment( &pSPI->tx_dma_cfg, true);
    channel_config_set_write_increment( &pSPI->rx_dma_cfg, false);

    dma_hw->ints0 = 1u <<  pSPI->tx_dma;

    dma_channel_configure(pSPI->tx_dma, &pSPI->tx_dma_cfg,
                        &spi_get_hw(pSPI->hw_inst)->dr,  // write address
                        tx,                              // read address
                        len,  // element count (each element is of
                                // size transfer_data_size)
                        false);  // start

    dma_channel_configure(pSPI->rx_dma, &pSPI->rx_dma_cfg,
                        &rxdummy,                              // write address
                        &spi_get_hw(pSPI->hw_inst)->dr,  // read address
                        len,  // element count (each element is of
                                // size transfer_data_size)
                        false);  // start

    dma_start_channel_mask((1u << pSPI->tx_dma) | (1u << pSPI->rx_dma));

    dma_channel_wait_for_finish_blocking(pSPI->tx_dma);
    dma_channel_wait_for_finish_blocking(pSPI->rx_dma);
#endif
    return len;
}


int my_spi_write_read_blocking_one (spi_inst_t *spi, const uint8_t *tx, uint8_t *rx, size_t len)
{
    while ( ! (spi0_hw->sr & 0x02) ); //&& rx_remaining < tx_remaining + fifo_depth )
    spi0_hw->dr = *tx;


    while ( ! (spi0_hw->sr & 0x04) );
    *rx = spi0_hw->dr & 0xff;
   
    return len;
}


/* [] END OF FILE */
