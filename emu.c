/*
 * ATARI ST HDC Emulator
 *
 * File:    emu.c
 * Author:  Steve Bradford
 * Created: September 2022
 * 
 * 
 * Using Raspberry PI PICO RP2040
 *
 * 
 * This Emulator is designed to be controller 0.
 * Targets 0 and 1 are assigned to SD0 and SD1.
 * Target 6 is used by the ICD RTC
 * 
 * The cpu clock is altered at initialisation to give us optimal SPI speed
 * of 33 MHz. This yields a transfer rate of just over 1 MB/s. NOTE, I 
 * have only used current 8GB/16GB/32GB micro-sd cards. I do not think 
 * older cards will work.
 * 
 * I have tested using ICD driver, and PP driver
 * FreeMintOS works okay
 * EmuTOS works okay
 * 
 * 
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pico/stdlib.h>
#include <pico/util/datetime.h>
#include "pico.h"
#include "hardware/rtc.h"
#include "pico/multicore.h"
#include "hardware/clocks.h"

/* project specific includes */
#include "emuerror.h"
#include "emu.h"
#include "emucmd.h"
#include "emurtc.h"
#include "shell/emushell.h"
#include "emuinit.h"



/* prototypes */
static int     getCMD   ( CommandDescriptorBlock * );
static void    doStatus ( uint8_t );
static int     rawRD    ( CommandDescriptorBlock *, DRIVES * );
static int     rawWR    ( CommandDescriptorBlock *, DRIVES * );
static uint8_t rdDMA    ( uint8_t *, uint32_t );
static uint8_t wrDMA    ( uint8_t *, uint32_t );

/* 64 bytes */
uint8_t INQUIRY_DATA [] =
{
	0,                                          /* device type 0 = direct access device */
	0x80,                                       /* device type qualifier (removable) */
	2,                                          /* ACSI/SCSI version */
	2,                                          /* reserved */
	59,                                         /* length of the following data */
	0, 0, 0,                                    /* Vendor specific data */
	'B','B','a','N','-','R','P','P',            /* T10 Vendor ID    8 chars */
	' ','H','D','C',' ',' ','S','D',            /* Product ID       8 chars */
	'#',' ','#','#','#',' ','G','B',            /* Product Revision 8 chars */
	'1','.','x','x','0','0','0','0',            /* Firmware Revision (4) / Drive Serial Number (4) */
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,            /* Vendor Unique - Empty */
    0,0,0,0,0,0,0,0                             /* pad to 64 bytes - ATARI FIFO needs multiples of 16 bytes */
};


uint8_t  DMAbuffer  [512 * 256];                /* max transfer size 128 KB */
DRIVES   drv        [MAX_DRIVES]; //[MAX_ID + 1];               /* IDs 0 - 6 */

uint32_t gStatus;                               /* used for printing - copy of CDB */
CommandDescriptorBlock gCDB;                    /* used for printing - copy of CDB */

#ifdef DEBUG
volatile bool       VERBOSE;                    /* enable / disable debug print */
#endif

volatile uint32_t   intState;                   /* interrupt state save/restore */
volatile bool       gotRST;
volatile bool       doPrint;
volatile datetime_t gdttm;                      /* global date time structure - update once per minute */


/*
 * run on second core
 * 
 */

void __not_in_flash_func (core1Entry) (void)
{
    CommandDescriptorBlock sCMD;


    memset ( &sCMD, 0, sizeof (CommandDescriptorBlock) );

    gpio_set_dir_in_masked (ACSI_DATA_MASK);
    
    IRQ_HI ();
    DRQ_HI ();
    dataBus (DISABLE);
    disableInterrupts ();

    for ( int i = 0; i < MAX_DRIVES; i++ )
    {
        drv [i].locked      = false;
        //drv [i].mounted     = false;
        drv [i].packetCount = 0;
        //drv [i].partTotal   = 0;
        //drv [i].raw         = false;
        drv [i].ejected     = false;
        drv [i].prevState   = true;
    }

    emudate ( "", 0 );                          /* initialise ICD RTC date/time array */
    emutime ( "", 0 );

    gpio_put ( CONTROL_BUS_CNTRL, LO );         /* enable control bus so we can listen for commands */

    /* monitor ACSI bus */
    while ( 1 ) 
    {
        /* 
         * ATARI holds RST low for 12us 
         *
         */
        
        if ( gpio_get ( RST ) == LO )
        {
            dataBus (DISABLE);
            IRQ_HI ();
            DRQ_HI ();
            
            gpio_set_dir_in_masked (ACSI_DATA_MASK); 
#if DEBUG
            printf ( "RESET\n" );
#endif            
            waitRST ();                         /* wait for RST to go high */
        }
        
        /* ============= */
        /* command phase */
        /* ============= */

        /* 
         * check GPIO for new command packet - A1 and RW = low
         * 
         * time critical, code MUST be kept to the minimum, otherwise, can miss bytes
         * interrupts should be disabled for the duration of the CDB building at least
         * 
         */
        
        if ( newCMD () ) 
        { 
            dataBus (ENABLE);

            waitCS(LO);                          /* wait for CS low */
            sCMD.b [0] = rdDataBus ();          /* data is available to read now - byte 0 */
            waitCS(HI);

            /* check this command is for this controller 
             * better to do in hardware with a switch selection ? */
            if ( sCMD.DEVICE.target == TARGET0 || sCMD.DEVICE.target == TARGET1 )
            {
                sCMD.cmdLength = 6;

                IRQ_LO ();                      /* assert IRQ to tell ATARI to continue */

                waitA1 ();                      /* subsequent command bytes are read whilst A1 is high */
                waitCS (LO); 
                IRQ_HI ();  
                sCMD.b [1] = rdDataBus ();      /* byte 2 - check for special/extended commands */
                waitCS(HI);
                                                /* we have a maximum of 12us to assert IRQ, starting....... now */

                if ( sCMD.DEVICE.opcode == 0x1f ) /* ACSI commands use 5 bits, highest ACSI command is 0x1f */
                {
                    sCMD.cmdLength = 7;         /* 6 + 1 */

                    if ( sCMD.b [1] >= 0xa0 )
                    {
                        sCMD.cmdLength = 13;    /* 12 + 1 */
                    }

                    else if ( sCMD.b [1] >= 0x80 )
                    {
                        sCMD.cmdLength = 17;    /* 16 + 1 */
                    }

                    else if ( sCMD.b [1] >= 0x20 )
                    {
                        sCMD.cmdLength = 11;    /* 10 + 1 */
                    }
                }

                IRQ_LO ();                      /* assert IRQ */

                for ( int d = 2; d < sCMD.cmdLength; d++ )
                {       
                    waitCS (LO);                  /* ATARI tells us another byte is ready */
                    IRQ_HI ();  
                    sCMD.b [d] = rdDataBus ();  /* bytes 2-n */
                    waitCS(HI);   
                                                /* the last command byte does NOT assert IRQ */
                    if ( d == (sCMD.cmdLength - 1) )
                        break;
                                      
                    IRQ_LO ();                  /* tell ATARI we are ready for next byte */
                }        

                dataBus (DISABLE);

                getCMD ( &sCMD );               /* process ACSI/SCSI command */
            }
            else                                /* ignore command, it wasn't for us */
            {                                   /* ATARI will timeout */
                IRQ_HI ();
                DRQ_HI ();
                dataBus (DISABLE);              /* data-bus trisate */
            }

            memset ( &sCMD, 0, sizeof (CommandDescriptorBlock) );   /* clear the command block */

        } /* end of command */
    }
    
    return;
}






/* 
 * use cpu core 0 to print command status
 * optimise to reduce overheads 
 */

static inline void printCmd ( CommandDescriptorBlock *CDB, int status ) 
{    
    char cmdString [16];
    char debugLine [80];
    
    switch ( CDB->DEVICE.opcode )
    {
        case TEST_UNIT_READY:
            sprintf ( cmdString, "TEST_UNIT_READY" );
            break;
        case REQUEST_SENSE:
            sprintf ( cmdString, "REQUEST_SENSE  " );
            break;
        case FORMAT_UNIT:
            sprintf ( cmdString, "FORMAT_UNIT    " );
            break;
        case CMD_READ:
            sprintf ( cmdString, "CMD_READ       " );
            break;
        case CMD_WRITE:
            sprintf ( cmdString, "CMD_WRITE      " );
            break;
        case CMD_SEEK:
            sprintf ( cmdString, "CMD_SEEK       " );
            break;
        case VENDOR_SPECIFIC:
            sprintf ( cmdString, "VENDOR_SPECIFIC" );
            break;
        case CMD_INQUIRY:
            sprintf ( cmdString, "CMD_INQUIRY    " );
            break;
        case MODE_SELECT:
            sprintf ( cmdString, "MODE_SELECT    " );
            break;
        case MODE_SENSE:
            sprintf ( cmdString, "MODE_SENSE     " );
            break;
        case START_STOP:
            sprintf ( cmdString, "START_STOP     " );
            break;
        case MEDIA_REMOVE:
            sprintf ( cmdString, "MEDIA_REMOVE   " );
            break;
        case EXTENDED_CMD:
            sprintf ( cmdString, "EXTENDED_CMD   " );
            break;
        default:
            sprintf ( cmdString, "UNKNOWN_COMMAND" );
            break;
    }
   
    for ( int i = 0; i < CDB->cmdLength; i++ )
    {
        if ( i == 2 && CDB->cmdLength == 6 )
            sprintf ( debugLine, "%s tgt=%d lun=%d (0x%02x:%02x:%02x:", 
                    cmdString, 
                    CDB->DEVICE.target, 
                    CDB->BYTE1.lun,
                    CDB->DEVICE.opcode,
                    CDB->b [1],
                    CDB->b [2]
                    );
        
        else if ( i == 2 && CDB->cmdLength > 6 )
            sprintf ( debugLine, "%s tgt=%d lun=%d (0x%02x:%02x:%02x:", 
                    cmdString, 
                    CDB->DEVICE.target,
                    CDB->b [2] >> 5,
                    CDB->DEVICE.opcode,
                    CDB->b [1],
                    CDB->b [2]
                    );
        
        if ( i < 3 )
            continue;

        sprintf ( debugLine + strlen (debugLine), "%02x:", CDB->b [i] );
    }

    sprintf ( debugLine + strlen (debugLine), "\b \b) (%02x)\n", status );
    printf ( debugLine );                       /* one printf to stdio */
}



int main ( void )
{
    datetime_t dt;
    char datetime_buf [80];


    EMUinit ();                                 /* MUST do first */
    rtc_init ();
    buildDateTime ();                           /* use compilation date/time to initialise RTC */

    printf ( "%s\n", TITLE );
    printf ( "Controller ID %d\n", CONTROLLER_ID );
    printf ( "Hardware initialised\n" );
    printf ( "RTC Jumper is %s\n", gpio_get (RTC_ENABLED) ? "ON" : "OFF" );
    printf ( "CPU clock is running at %d MHz\n\n", clock_get_hz (clk_sys) / 1000000 );

    printf ( "ACSI HDC Emulator Running ... press ESC for shell\n\n" );
    

#ifdef DEBUG
    VERBOSE = true;
#endif
    doPrint = false;                            /* flag to show if debug print is needed */

#if ICD_RTC && DEBUG_ICDRTC
    doPrintICDRTCbuff = false;
#endif

    sleep_ms (1000);

    multicore_launch_core1 ( core1Entry );      /* one whole cpu dedicated to decoding commands etc. */

    gpio_put ( ONBOARD_LED, HI );               /* show we are initialised and running */

    mountRAW (0);                               /* initialise disks */
    mountRAW (1);
    
    /* 
     * do something in core 0 :) 
     */
    while ( true ) 
    {
#if !DEBUG
        printf ( "\033[2J" );

        status ();

        rtc_get_datetime (&dt);
        datetime_to_str  ( datetime_buf, sizeof (datetime_buf), &dt );
        printf ( "\n%s\n", datetime_buf );

        sleep_ms (1000);
#else
        /* do the debug print on this core to reduce overheads on other core */
        if ( doPrint && VERBOSE )
        {
            doPrint = false;

            printCmd ( &gCDB, gStatus );
        }
#endif

#if ICD_RTC && DEBUG_ICDRTC
        if ( doPrintICDRTCbuff )
        {
            doPrintICDRTCbuff = false;

            printICDRTCbuff ();
        }
#endif

        /* enter shell on ESC key */
        if ( getchar_timeout_us (0) == 0x1b )
        {
            doShell ();                         /* all the time we are in the shell, we are not checking for sd card removal/insertion */
        }

        checkSDcards ();                        /* check sd cards insertion state */
    }

    return 0;
}



static inline int __not_in_flash_func (getCMD) ( CommandDescriptorBlock *CDB )
{  
    register uint8_t LUN;
    register uint8_t TARGET;
    DRIVES  *pdrv;
   

    /* ============= */
    /* data phase    */
    /* ============= */

    TARGET = CDB->DEVICE.target;

    if ( CDB->cmdLength == 6 )
        LUN = CDB->BYTE1.lun; 

    else
        LUN = CDB-> b [2] >> 5;

    pdrv    = &drv [TARGET - CONTROLLER_ID];    /* max of 2 disks 0, 1 */

    pdrv->packetCount += 1;                     /* increment packet counter for this drive - just for stats */

    switch ( CDB->DEVICE.opcode ) {

        case TEST_UNIT_READY:        
            /* 
                * 0x00 No error 
                * 0x04 Device not ready 
                * 0x30 Controller self test failed
                * 
                * The check condition bit (1) should be set if error
                * 
                * NOTE status byte bits 5-7 = controller number
                */
            pdrv->status = pdrv->pSD->mounted ? ERR_DRV_NO_ERROR : CHECK_CONDITION;

            if ( pdrv->status )
            {
                pdrv->lastError.SK   = SCSI_SK_NOT_READY;
                pdrv->lastError.ASC  = SCSI_ASC_MEDIUM_NOT_PRESENT;
                pdrv->lastError.ASCQ = SCSI_ASCQ_NO_ADDITIONAL_SENSE_INFORMATION;
            }

            else
            {
                pdrv->lastError.SK   = SCSI_SK_NO_SENSE;
                pdrv->lastError.ASC  = SCSI_ASC_NO_ADDITIONAL_SENSE;
                pdrv->lastError.ASCQ = SCSI_ASCQ_NO_ADDITIONAL_SENSE_INFORMATION;
            }

            break;

        /* SCSI SPC-5 */
        case REQUEST_SENSE:
            memset ( DMAbuffer, 0, CDB->len );

            /* should the VALID bit be set ??? = 0x80 | CDB->b [1] */
            if ( CDB->b [1] & 0x01 )            /* check descriptor bit 0 = fixed format sense data, 1 = descriptor format sense data */
                DMAbuffer [0] = 0x72;           /* Response Code (byte 0) = Current Information DESCRIPTOR */  

            else
                DMAbuffer [0] = 0x70;           /* Response Code (byte 0) = Current Information FIXED */   

            DMAbuffer [1] = 0;                  /* Reserved */
            DMAbuffer [2] = 0;                  /* FILEMARK, EOM, ILI, SDAT_OVFL, bits 0-3 sense key */
            DMAbuffer [3] = 0;                  /* Information field - bytes 3 (MSB), 4, 5, 6 (LSB) */
            DMAbuffer [7] = 12;                 /* additional sense length (n-7) */

           
            /* sense data */
            /* 0 - descriptor type
             *     0x00 information sense, 
             *     0x01 command specific, 
             *     0x02 sense key, 
             *     0x03 field replacable unit, 
             *     0x0a another progress indication, 
             *     0x0c foreward sense, 
             *     0x0e device designation, 
             *     0x0f microcode activation, 
             *     0x80-0xff vendor specific
             */

            /* Information Sense */
            DMAbuffer [8]  = 0;                 /* byte 0 descriptor type */
            DMAbuffer [9]  = 0x0a;              /* byte 1 additional length */
            DMAbuffer [10] = 0x80;              /* byte 2 reserved - set valid bit if FIXED */
            DMAbuffer [11] = 0;                 /* byte 3 reserved */
            DMAbuffer [12] = pdrv->lastError.ASC;  /* Additional Sense Code */
            DMAbuffer [13] = pdrv->lastError.ASCQ; /* Additional Sense Code Qualifier */
            DMAbuffer [14] = 0;                 /* Field replacable unit code */
            DMAbuffer [15] = 0;                 /* sense key specific information 15, 16, 17 */
            DMAbuffer [16] = 0;
            DMAbuffer [17] = 0;
            DMAbuffer [18] = 0;                 /* additional sense bytes 18..n */
            DMAbuffer [19] = 0;               
                
            switch ( pdrv->lastStatus )
            {
                case ERR_DRV_NO_ERROR: 
                    DMAbuffer [2] |= SCSI_SK_NO_SENSE; 
                    break;
                case ERR_CMD_INVALID_CMD:
                case ERR_CMD_INVALID_ADD:
                case ERR_CMD_INVALID_ARG:
                case ERR_CMD_INVALID_DRV:
                    DMAbuffer [2] |= SCSI_SK_ILLEGAL_REQUEST; 
                    break;

                default: 
                    DMAbuffer [2] |= SCSI_SK_HARDWARE_ERROR; 
                    break;
            }
               
            pdrv->status = ERR_DRV_NO_ERROR;

            wrDMA ( DMAbuffer, 20 );            /* need to do doStatus () ASAP after a wrDMA () */

            break;

        case FORMAT_UNIT:
            pdrv->status = ERR_DRV_NO_ERROR;
           
            if ( ! pdrv->pSD->mounted )
            {
                pdrv->lastError.status = SCSI_ERR_INV_LUN;
                pdrv->status           = ERR_CMD_INVALID_DRV;
            }

            else
            {
                sd_erase ( pdrv->pSD );
            }

            break;

        case CMD_READ:
            if ( ! pdrv->pSD->mounted || LUN != 0 )
            {
                pdrv->lastError.SK     = SCSI_SK_NOT_READY;
                pdrv->lastError.ASC    = SCSI_ASC_MEDIUM_NOT_PRESENT;
                pdrv->lastError.ASCQ   = SCSI_ASCQ_NO_ADDITIONAL_SENSE_INFORMATION;
                pdrv->status           = ERR_DRV_DRV_NOT_READY;
            }

            else 
            {
                pdrv->lba = (uint32_t)CDB->BYTE1.msbLBA << 16 | (uint32_t)CDB->mid << 8 | CDB->lsb;
               
                rawRD ( CDB, pdrv );
            }
        
            break;

        case CMD_WRITE:            

            if ( ! pdrv->pSD->mounted || LUN != 0 ) 
            {
                pdrv->lastError.SK     = SCSI_SK_NOT_READY;
                pdrv->lastError.ASC    = SCSI_ASC_MEDIUM_NOT_PRESENT;
                pdrv->lastError.ASCQ   = SCSI_ASCQ_NO_ADDITIONAL_SENSE_INFORMATION;
                pdrv->status           = ERR_DRV_DRV_NOT_READY;
            }
            else 
            {
                pdrv->lba = (uint32_t)CDB->BYTE1.msbLBA << 16 | (uint32_t)CDB->mid << 8 | CDB->lsb;
                rawWR ( CDB, pdrv );
            }

            break;

        /* don't need this command */
        case CMD_SEEK:
            pdrv->lastError.status = SCSI_ERR_OK;
            pdrv->status           = ERR_DRV_NO_ERROR;

            pdrv->lba  = (uint32_t)CDB->BYTE1.msbLBA << 16;
            pdrv->lba |= (uint32_t)CDB->mid << 8;
            pdrv->lba |= CDB->lsb;
            
            break;


        case CMD_INQUIRY:
            pdrv->lastError.status = SCSI_ERR_OK; 
            pdrv->status           = ERR_DRV_NO_ERROR;
               
            memcpy ( DMAbuffer, INQUIRY_DATA, sizeof (INQUIRY_DATA) );

            {
                char num [9];
                int diskSize = (float)((float)(pdrv->pSD->sectors * 512) / 1000.0 / 1000.0 / 1000.0) + 0.5;

                sprintf ( num, "%3d", diskSize );                

                DMAbuffer [24] = '0' + (TARGET - CONTROLLER_ID);  /* alter the disk number in product revision field */
                DMAbuffer [26] = num [0];
                DMAbuffer [27] = num [1];
                DMAbuffer [28] = num [2];
                DMAbuffer [29] = ' ';
                DMAbuffer [30] = 'G';
                DMAbuffer [31] = 'B';

                memcpy ( &DMAbuffer [32], VERSION, 4 );         /* include firmware revision */

                DMAbuffer [39] = '0' + (TARGET - CONTROLLER_ID);/* alter drive serial number */
                DMAbuffer [0] = 0x00;       /* device connected and is a direct access block device */

                if ( LUN != 0 )
                    DMAbuffer [0] = 0x7f;       /* this is causing me problems ??? hangs bus */
            }

            if ( CDB->len < 64 )
                DMAbuffer [4] = CDB->len - 5;
                
            wrDMA ( DMAbuffer, CDB->len );

            break;

        /* 
         * non-ACSI, 
         * SCSI commands or vendor specific ... 
         */
        default:

            switch ( CDB->DEVICE.opcode )
            {
                case START_STOP:
                    pdrv->status         = CHECK_CONDITION;
                    pdrv->lastError.SK   = SCSI_SK_NOT_READY;
                    pdrv->lastError.ASC  = SCSI_ASC_LUN_NOT_READY;
                    pdrv->lastError.ASCQ = SCSI_ASCQ_NO_ADDITIONAL_SENSE_INFORMATION;

                    break;

                case READ_BLOCK_LIMITS:
                    pdrv->status = ERR_DRV_NO_ERROR;

                    break;

                /* ICD RTC ??? */
                case 0x13:
                    pdrv->status = ERR_DRV_NO_ERROR;

                    break;

                case MODE_SELECT:
                    pdrv->status = ERR_DRV_NO_ERROR;

                    memset ( DMAbuffer, 0, CDB->len );

                    /* what to do here? */

                    break;

                /* SCSI commands */
                // 0 OPERATION CODE (1Ah)
                // 1 DBD (disable block descriptors) bit: bit 7 to 4 RES, bit3 DBD, bit 2 to 0 RES. --> DBD (disable block descriptors) bit.
                // 2 PC (Page Control) field: First two bit (PC, Page Control) field, bit 5 to 0 PAGE CODE.
                // 3 SUBPAGE CODE: The PAGE CODE and SUBPAGE CODE fields specify which mode pages and subpages to return (see table 349)
                // 4 ALLOCATION LENGTH: The ALLOCATION LENGTH field is defined in 2.2.6.
                // 5 CONTROL:
                //
                // Error
                // send Status 2
                // Success
                case MODE_SENSE:
                    pdrv->status = ERR_DRV_NO_ERROR;

                    /* get sub-command */
                    switch ( CDB->b [2] ) {

                        /* MODE SENSE 0x00: */
                        case 0x00:
                            modeSense0 ( DMAbuffer, pdrv, TARGET );

                            wrDMA ( DMAbuffer, 16 );

                            break;

                        /* MODE SENSE 0x04: Rigid disk geometry */
                        case 0x04:
                            modeSense4 ( DMAbuffer, pdrv, TARGET );

                            wrDMA ( DMAbuffer, 24 );

                            break;

                        /* MODE SENSE 0x3f: Return all pages */
                        case 0x3f:
                            memset ( DMAbuffer, 0, 44 );

                            DMAbuffer [0] = 44;
                            DMAbuffer [1] = 0;
                            DMAbuffer [2] = 0;
                            DMAbuffer [3] = 0;

                            modeSense4 ( DMAbuffer + 4, pdrv, TARGET );
                            modeSense0 ( DMAbuffer + 28, pdrv, TARGET );

                            wrDMA ( DMAbuffer, 44 );

                            break;

                        case 0x40:
                            /* page control bits 7 and 6 */
                            /* 0x40 = page control 01 = changeable parameter values */

                        case 0x48:
                            /* page control bits 7 and 6 */
                            /* page code bits 5, 4, 3, 2, 1, 0 */
                            /* 0x48 = */

                            pdrv->lastError.SK   = SCSI_SK_ILLEGAL_REQUEST;
                            pdrv->lastError.ASC  = SCSI_ASC_INVALID_FIELD_IN_CDB;
                            pdrv->lastError.ASCQ = SCSI_ASCQ_NO_ADDITIONAL_SENSE_INFORMATION;
                            pdrv->status         = CHECK_CONDITION;

                            break;

                        default:

                            printf ( "MODE_SENSE: unsupported mode sense 0x%02x\n", CDB->b [2] );
                            pdrv->status = ERR_CMD_INVALID_ARG;
                            break;
                    }

                    break;

                /* TODO Prevent / Allow media removal */
                case MEDIA_REMOVE:
                    pdrv->status = ERR_DRV_NO_ERROR;

                    //if ( CDB->b [4] & 0x01 == 0x00 ) /* 0 = unlocked, 1 = locked */
                        //emuunmount (TARGET);
                    pdrv->locked = CDB-> b [4] & 0x01;              /* 0 = unlocked, 1 = locked */

                    break;

                /* extended commands */
                case 0x1f:
                    
                    /* decode extended command */
                    switch ( CDB->b [1] )
                    {
                        /* A2STM image */
                        case 0x0c:
                            //a2stmPtr = a2setup_boot_bin;
                            //a2stmLen = a2setup_boot_bin_len;

                            //wrDMA ( a2stmPtr, a2stmLen );

                            break;

                        /* A2STM image */
                        case 0x0d:
                            //a2stmPtr = a2stdrv_boot_bin;
                            //a2stmLen = a2stdrv_boot_bin_len;

                            //wrDMA ( a2stmPtr, a2stmLen );

                            break;

                        /* 0x12 SCSI Inquiry */
                        case 0x12:       
                            pdrv->lastError.status = SCSI_ERR_OK;      /* SCSI error code */
                            pdrv->status           = ERR_DRV_NO_ERROR;
                            
                            memcpy ( DMAbuffer, INQUIRY_DATA, sizeof (INQUIRY_DATA) );
                          
                            DMAbuffer [0] = 0x00;       /* device connected and is a direct access block device */

                            if ( LUN != 0 )
                                DMAbuffer [0] = 0x7f;       /* this is causing me problems ??? hangs bus */

                            if ( CDB->len < sizeof (INQUIRY_DATA) )
                                DMAbuffer [4] = CDB->len - 5;

                            wrDMA ( DMAbuffer, CDB->len );

                            break;

                        /* Ultrasatan RTC */
                        case 0x20:
                            printf ( "Ultrasatan RTC request\n" );
                            break;

                        /* 0x25 SCSI Read Capacity */
                        case SCSI_OP_READ_CAPACITY:
                            pdrv->lastError.status = SCSI_ERR_OK;          /* SCSI error code */
                            pdrv->status           = ERR_DRV_NO_ERROR;

                            memset ( DMAbuffer, 0, 16 );

                            if ( pdrv->pSD->mounted )
                            {
                                uint32_t sectors = pdrv->pSD->sectors - 1;

                                DMAbuffer [0] = (sectors >> 24) & 0xff;
                                DMAbuffer [1] = (sectors >> 16) & 0xff;
                                DMAbuffer [2] = (sectors >> 8)  & 0xff;
                                DMAbuffer [3] =  sectors        & 0xff;
                                DMAbuffer [4] = 0x00;
                                DMAbuffer [5] = 0x00;
                                DMAbuffer [6] = 0x02;               /* always 512 bytes */
                                DMAbuffer [7] = 0x00;
                            }

                            wrDMA ( DMAbuffer, 16 );

                            break;

                        /* 0x28 SCSI Read */
                        case SCSI_OP_READ:
                            if ( pdrv->pSD->mounted && LUN == 0 )
                            {
                                pdrv->lba =  (uint32_t)CDB->b [3] << 24 | 
                                             (uint32_t)CDB->b [4] << 16 | 
                                             (uint32_t)CDB->b [5] << 8  | 
                                             (uint32_t)CDB->b [6];

                                pdrv->len =  (uint32_t)CDB->b [8] << 8  | CDB->b [9];

                                rawRD ( CDB, pdrv );
                            } 

                            else
                            {
                                pdrv->lastError.SK     = SCSI_SK_NOT_READY;
                                pdrv->lastError.ASC    = SCSI_ASC_MEDIUM_NOT_PRESENT;
                                pdrv->lastError.ASCQ   = SCSI_ASCQ_NO_ADDITIONAL_SENSE_INFORMATION;
                                pdrv->status           = ERR_DRV_DRV_NOT_READY;
                            }              
                           
                            break;

                        /* 0x2a SCSI WRITE */
                        case SCSI_OP_WRITE:
                            if ( pdrv->pSD->mounted && LUN == 0 )
                            {
                                pdrv->lba = (uint32_t)CDB->b [3] << 24 | 
                                            (uint32_t)CDB->b [4] << 16 | 
                                            (uint32_t)CDB->b [5] << 8  | 
                                            (uint32_t)CDB->b [6];

                                pdrv->len = (uint32_t)CDB->b [8] << 8  | CDB->b [9];

                                rawWR ( CDB, pdrv );
                            }

                            else
                            {
                                pdrv->lastError.SK     = SCSI_SK_NOT_READY;
                                pdrv->lastError.ASC    = SCSI_ASC_MEDIUM_NOT_PRESENT;
                                pdrv->lastError.ASCQ   = SCSI_ASCQ_NO_ADDITIONAL_SENSE_INFORMATION;
                                pdrv->status           = ERR_DRV_DRV_NOT_READY;
                            }

                            break;

                        /* 0x3b SCSI Write Buffer - diags only */
                        case SCSI_OP_WRITE_BUFFER:
                            printf ( "requested SCSI_OP_WRITE_BUFFER\n" );
                            break;

                        /* 0x3c SCSI Read Buffer - diags only */
                        case SCSI_OP_READ_BUFFER:
                            printf ( "requested SCSI_OP_READ_BUFFER\n" );
                            break;

                        case SCSI_OP_REPORT_LUNS:
                            /* always report 0 */
                            printf ( "requested Report Luns\n" );

                            break;

                        /* unknown opcode */
                        default:
                            pdrv->status = 0xff;
                            printf ( "unknown extended command 0x%02x\n", CDB->b [1] );

                            break;
                    }

                    break;

                default:
                    pdrv->status = 0xff;
                    printf ( "unknown command 0x%02x\n", CDB->b [0] );

                    break;
            }

            break;
    }
    
    /* ============= */
    /* status phase  */
    /* ============= */

#if DEBUG 
    if (VERBOSE)
    {
        memcpy ( &gCDB, CDB, sizeof (CommandDescriptorBlock) );
        gStatus = ( pdrv->status );
        doPrint = true;                         /* tell other CPU to print debug info */
    }
#endif 
    
    pdrv->lastStatus = pdrv->status;

    doStatus ( pdrv->status );                  /* end of command - send status byte to ATARI */

    return 1;
}



/* return command status (one byte) to ATARI */
static inline void __not_in_flash_func (doStatus) ( uint8_t status )
{ 
    IRQ_LO ();                                  /* completes DMA (if any) */
    waitRW (LO);                                /* make sure RW is high = READ */
    dataBus (ENABLE);
    gpio_set_dir_out_masked (ACSI_DATA_MASK);

    waitCS (LO);
    IRQ_HI ();
    wrDataBus ( status );
    waitRW (HI);
    
    gpio_set_dir_in_masked (ACSI_DATA_MASK);
    dataBus (DISABLE);
}



/*
 * from perspective of peripheral
 * read DMA means the ATARI is doing a DMA write to us
 */
static inline uint8_t __not_in_flash_func (rdDMA) ( uint8_t *ptr, uint32_t length ) 
{
    register int d;
    register int rdDelay;

    /* DMA transfers - read */
    /* Peripheral asserts DRQ, ATARI responds with ACK */
    /* RW stays low (WRITE) for duration */
    /* at end assert IRQ to complete transfer - done in doStatus () */
    
    waitWR ();                                  /* make sure RW is low (WRITE) */
    
    gpio_set_dir_in_masked (ACSI_DATA_MASK);
    
    /* do the transfer as fast as we can - the ATARI is handling the handshaking */
    /* seems to be okay below 200 MHz before small delay is needed between bytes */
    for ( d = 0; d < length; d++ ) {

        DRQ_LO ();
        waitACK (HI);
        *ptr++ = rdDataBus ();
        DRQ_HI ();
        waitACK (LO);

        /* 
         * apparently have to wait a minimum of 240ns before next DRQ LO 
         * however, in reality, at least on my ATARI 520 STFM, I getaway with ~50ns 
         * NOTE this is with CPU @ 150 MHz and SPI @ 37 MHz 
         */
        /* 7 for SPI @ 37MHz */
        /* */
        for ( rdDelay = 7; rdDelay; rdDelay-- )
            Nop ();
            
    }
    
    return ERR_DRV_NO_ERROR;
}


/*
 * from perspective of peripheral
 * write DMA means the ATARI is doing a DMA read
 */
static inline uint8_t __not_in_flash_func (wrDMA) ( uint8_t *ptr, uint32_t length ) 
{
    register uint32_t d = length;
    register uint32_t wrDelay;

    /* DMA transfers - write */
    /* Peripheral asserts DRQ, ATARI responds with ACK */
    /* RW stays high (READ) for duration */
    /* at end assert IRQ - done in doStatus () */
    
    waitRD ();
    
    gpio_set_dir_out_masked (ACSI_DATA_MASK);   /* make data gpio pins outputs */
    
    /* do the transfer as fast as we can - the ATARI is handling the handshaking */
    /* seems to be okay below 200 MHz    */
    for ( ; d; d-- ) {

        DRQ_LO ();
        waitACK (HI);
        wrDataBus ( *ptr++ );
        DRQ_HI ();
        waitACK (LO);

        /* NOTE calculated with a 150 MHz CPU clock */
        /* hold data for at least 50ns - reducing this delay DOES NOT increase read speed */
        for ( wrDelay = 7; wrDelay; wrDelay-- )
            Nop ();
    }
    
    gpio_set_dir_in_masked (ACSI_DATA_MASK);    /* make data gpio pins inputs */

    return ERR_DRV_NO_ERROR;
}



/* ------------------------------------------------------------------------- */


static inline int rawRD ( CommandDescriptorBlock *cdb, DRIVES *drv )
{  
    int e;
    uint32_t length;


    /* ACSI / SCSI (6) read */
    if ( cdb->cmdLength == 6 )
    {
        length = cdb->len;
    }

    /* SCSI (10) read */
    else
    {                        
        length = drv->len;
    }

    if ( length == 0 )
        return 0;
    
    /* TODO check for length > 256 as exceded DMAbuffer size */

    if ( ( e = sd_read_blocks ( drv->pSD, DMAbuffer, drv->lba, length )) != SD_BLOCK_DEVICE_ERROR_NONE )
    {
        drv->lastError.status = SCSI_ERR_READ;
        drv->status           = ERR_CNTRL_DATA_NOT_FOUND;

        printf ( "READ: read failed %d, len = %d\n", e, length );
    }

    else 
    {
        dataBus (ENABLE);
        wrDMA ( DMAbuffer, length << 9 );
        dataBus (DISABLE);

        drv->lastError.status = SCSI_ERR_OK; 
        drv->status           = ERR_DRV_NO_ERROR;
    }
}



static inline int rawWR ( CommandDescriptorBlock *cdb, DRIVES *drv )
{
    int e;
    uint32_t length;


    if ( cdb->cmdLength == 6 )
    {
        length = cdb->len;
    }

    else
    {                        
        length = drv->len;
    }
    
    drv->lastError.status = SCSI_ERR_OK;
    drv->status           = ERR_DRV_NO_ERROR;

    if( length == 0 )
        return 0;
                
    dataBus (ENABLE);

    uint32_t offset = drv->lba;
    for ( uint32_t i = 0; i < length; i++ )
    {
        offset += i;
        rdDMA ( DMAbuffer, 512 );

        if ( ( e = sd_write_blocks ( drv->pSD, DMAbuffer, offset, 1 ) ) != SD_BLOCK_DEVICE_ERROR_NONE )
        {
            drv->lastError.status = SCSI_ERR_WRITE;
            drv->status           = ERR_CNTRL_DATA_NOT_FOUND;
            printf ( "WRITE: write failed %d, len = %d\n", e, length );
            break;
        }
    }
    
    dataBus (DISABLE);

}
