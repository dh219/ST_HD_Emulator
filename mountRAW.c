/*
 * ATARI ST HDC Emulator
 * 
 * File:    mountraw.c
 * Author:  Steve Bradford
 * Created: September 2022
 *
 * PICO hardware initialisation
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <pico/stdlib.h>
#include "sdcard/sd_card.h"
#include "emu.h"


#define HDC_ReadInt32(a, i) (((unsigned) a[i] << 24) | ((unsigned) a[i + 1] << 16) | ((unsigned) a[i + 2] << 8) | a[i + 3])

extern DRIVES drv[];


int rawPartitionCount ( sd_card_t *pdrv )//DRIVES *pdrv )
{
	unsigned char *pinfo, bootsector[512];
	uint32_t start, sectors, total = 0;
	int i, parts = 0;
    int lba = 0;
    

    if ( ( i = sd_read_blocks ( pdrv, bootsector, lba, 1 ) ) != SD_BLOCK_DEVICE_ERROR_NONE )
	{
		printf( "rawPartitionCount: read error %d\n", i );

		return 0;
	}
    
    /* read MBR partition table
     * this is in the first sector (512 bytes) of the volume
     * if byte 0 is not equal to 0 then a boot programme resides
     * partition info starts at the following offsets, each is 16 bytes long
     * partition 1 at byte 446 (0x01be)
     * partition 2 at byte 462
     * partition 3 at byte 478
     * partition 4 at byte 494
     * byte offset 510 (0x01fe) should read 0xaa55 = valid MBR
     * 
     */
	if ( bootsector [0x01fe] == 0x55 && bootsector [0x01ff] == 0xaa )
	{
		int ptype, boot;

        
		/* first partition table entry */
		pinfo = bootsector + 0x1be;
  
		for ( i = 0; i < MAX_MBR_PARTS; i++, pinfo += 16 )
		{
			boot    = pinfo [0];                                                /* uint8_t - 0x00 = not bootable, 0x80 = bootable */
			ptype   = pinfo [4];                                                /* uint8_t - partition type
                                                                                 *          0x00: Blank entry. Any other field must be zero.
                                                                                 *          0x01: FAT12 (CHS/LBA, < 65536 sectors)
                                                                                 *          0x04: FAT16 (CHS/LBA, < 65536 sectors)
                                                                                 *          0x05: Extended partition (CHS/LBA)
                                                                                 *          0x06: FAT12/16 (CHS/LBA, >= 65536 sectors)
                                                                                 *          0x07: HPFS/NTFS/exFAT (CHS/LBA)
                                                                                 *          0x0B: FAT32 (CHS/LBA)
                                                                                 *          0x0C: FAT32 (LBA)
                                                                                 *          0x0E: FAT12/16 (LBA)
                                                                                 *          0x0F: Extended partition (LBA)
                                                                                 */
            
            start = ((uint32_t)pinfo [11] << 24) | ((uint32_t)pinfo [10] << 16) | ((uint32_t)pinfo [9] << 8) | pinfo [8];      /*  partition start sector in 32bit LBA format */
			sectors = ((uint32_t)pinfo [15] << 24) | ((uint32_t)pinfo [14] << 16) | ((uint32_t)pinfo [13] << 8) | pinfo [12];  /* partition size in sectors */
		 			
            if ( ptype && (ptype != 0x05 && ptype != 0x0f) )
            {
                total += sectors;

                printf ( "DOS MBR - Partition %d: type=0x%02x, start=0x%08x, size=%.1f MB %s%s\n",
                    i, ptype, start, sectors/2048.0, boot ? "(boot)" : "", sectors ? "" : "(invalid)");
                                
				parts++;
            }
//#ifdef TODO
            /* check extended boot records (if any) */
            if ( ptype == 0x0f || ptype == 0x05 )
            {
                uint8_t extpart [512];
                char    *ep = (extpart + 0x1be);
                uint8_t epboot, eptype;
                uint32_t epstart, epsectors;
                

                if ( ( i = sd_read_blocks ( pdrv, extpart, start, 1 ) ) != SD_BLOCK_DEVICE_ERROR_NONE )
                {
                    printf ( "2nd read failed %d\n", i );

                    return 0;
                }

                if ( extpart [0x01fe] == 0x55 && extpart [0x01ff] == 0xaa )
                {                  
                    for ( int p = 0; p < 4; p++, ep += 16 )
                    {
                        epboot    = ep [0];
                        eptype    = ep [4];
                        epstart = (((uint32_t)ep [11] << 24) | ((uint32_t)ep [10] << 16) | ((uint32_t)ep [9] << 8) | ep [8]) + start;  /* partition start sector in 32bit LBA format */
                        epsectors = ((uint32_t)ep [15] << 24) | ((uint32_t)ep [14] << 16) | ((uint32_t)ep [13] << 8) | ep [12];

                        if ( eptype && eptype != 0x05 )
                        {
                            total += epsectors;

                            printf ( "DOS EBR - Extended Partition %d: type=0x%02x, start=0x%08x, size=%.1f MB %s%s\n",
                                parts, eptype, epstart, epsectors/2048.0, epboot ? "(boot)" : "", epsectors ? "" : "(invalid)");
                            
                            parts++;
                        }




                        if ( eptype == 0x05 )
                        {
                            uint8_t eextpart [512];
                            char    *epp = (eextpart + 0x1be);
                            uint8_t eepboot, eeptype;
                            uint32_t eepstart, eepsectors;
                            

                            if ( ( i = sd_read_blocks ( pdrv, eextpart, epstart, 1 ) ) != SD_BLOCK_DEVICE_ERROR_NONE )
                            {
                                printf ( "2nd read failed %d\n", i );

                                return 0;
                            }

                            if ( eextpart [0x01fe] == 0x55 && eextpart [0x01ff] == 0xaa )
                            {                  
                                for ( int p = 0; p < 4; p++, epp += 16 )
                                {
                                    eepboot    = epp [0];
                                    eeptype    = epp [4];
                                    eepstart = (((uint32_t)epp [11] << 24) | ((uint32_t)epp [10] << 16) | ((uint32_t)epp [9] << 8) | epp [8]) + epstart;  /* partition start sector in 32bit LBA format */
                                    eepsectors = ((uint32_t)epp [15] << 24) | ((uint32_t)epp [14] << 16) | ((uint32_t)epp [13] << 8) | epp [12];

                                    if ( eeptype )
                                    {
                                        total += eepsectors;

                                        printf ( "DOS EBR - Extended Partition %d: type=0x%02x, start=0x%08x, size=%.1f MB %s%s\n",
                                            parts, eeptype, eepstart, eepsectors/2048.0, eepboot ? "(boot)" : "", eepsectors ? "" : "(invalid)");
                                        
                                        parts++;
                                    }

                                    
                                }
                            }





                        }
                    }
                }

            }
//#endif
		}
        
        if ( parts )
        {   
            //pdrv->diskSize = total;
            
		    printf( "Total size: %.1f MB in %d partitions\n", total/2048.0, parts);
        }
	}
 
    else
	{
		/* Partition table contains hd size + 4 partition entries
		 * (composed of flag byte, 3 char ID, start offset
		 * and size), this is followed by bad sector list +
		 * count and the root sector checksum. Before this
		 * there's the boot code.
		 */
		char c, pid[4];
		int j, flags;
		bool extended;

		pinfo = bootsector + 0x1C6;
        
		for (i = 0; i < MAX_MBR_PARTS; i++, pinfo += 12)
		{
			flags = pinfo[0];
			
			if (flags & 0x1)
            {
                for (j = 0; j < 3; j++)
                {
                    c = pinfo[j+1];
                    if (c < 32 || c >= 127)
                        c = '.';
                    pid[j] = c;
                }
                
                pid[3] = '\0';
                extended = strcmp("XGM", pid) == 0;
                    
                printf( "ATARI MBR "
                    "- Partition %d: ID=%s, start=0x%08x, size=%.1f MB, flags=0x%x %s%s\n",
                    parts, pid, 
                    HDC_ReadInt32(pinfo, 4),
                    HDC_ReadInt32(pinfo, 8) / 2048.0,
                    flags,
                    (flags & 0x80) ? "(boot)": "",
                    extended ? "(extended)" : "");
                
				parts++;
            }
		}
        
        if ( parts )
        {
		    total = HDC_ReadInt32(bootsector, 0x1C2);
            //pdrv->diskSize = total;

		    printf ( "- Total size: %.1f MB in %d partitions\n", total / 2048.0, parts );
        }
	}
    
    //if ( parts )
    //{
        pdrv->mounted = true;
        //pdrv->mounted = true;
       // pdrv->raw     = true;                   /* raw  partioned */
    //}

    //else
    //{
    //    pdrv->mounted = false;
    //    pdrv->raw     = false;
    //}

    //pdrv->partTotal = parts;
    return total;
}
    

extern sd_card_t sd_cards[];

int mountRAW ( int disk )
{
    int t = 0;
    int status;
    sd_card_t *pdrv; 


    //for ( int n = 0; n < MAX_DRIVES; n++ )
    //{        
        drv [disk].pSD = &sd_cards [disk];

        pdrv = drv [disk].pSD;

        if ( gpio_get (pdrv->card_detect_gpio) == false && pdrv->mounted == false )    /* check micro-sd card is inserted */
        {
            sleep_ms (1);

            if ( (status = sd_init_card ( pdrv ) ) == 0 )
            {
                drv [disk].diskSize = rawPartitionCount ( pdrv );
                printf ( "%s diskSize = %d\n", __func__, drv [disk].diskSize );
                
                if ( pdrv->mounted )
                {
                    printf ( "\n%s mounted\n\n", pdrv->pcName );
                    t++;
                }
            }
            
            else
            {
                printf ( "\nERROR: mountRAW init card failed on %s\n", pdrv->pcName );
            }
        }
    //}  
    
    return t;                                   /* return number of mounted drives */
}

