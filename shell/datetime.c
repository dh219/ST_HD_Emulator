/* 
 * ATARI ST HDC Emulator
 *
 * File:    datetime.c
 * Author:  Steve Bradford
 * Created: 1st Nov 2022
 * 
 * shell commands 
 * Syntax:  date [ddmm[yy]]
 * Syntax:  time [hhmm[ss]]
 * 
 * return true on success
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include "pico/stdlib.h"
#include <pico/util/datetime.h>
#include "hardware/rtc.h"
#include "emushell.h"
#include "../emu.h"


extern int dow ( datetime_t *dt );


static char days    [7][4] = { "Sun\0", "Mon\0", "Tue\0", "Wed\0", "Thu\0", "Fri\0", "Sat\0" };
static char months [12][4] = { "Jan\0", "Feb\0", "Mar\0", "Apr\0", "May\0", "Jun\0", "Jul\0", "Aug\0", "Sep\0", "Oct\0", "Nov\0", "Dec\0" };

bool emudate ( char *d, bool verbose )
{
    int         l, ret;
    char        dd[3], mm[3], yy[3];
    datetime_t  dt;
    long        day, mon, year;
    char        *daystr, *monstr;
    const char  endstr[4][2] = {"st", "nd", "rd", "th"};
    char        estr[3];

    
    ret = BAD;                                  /* assume fail */
    
    if ( *d )                                   /* have an argument, so set date */
    {
        l = strlen (d);
        
        if ( l == 4 || l == 6 )
        {
            rtc_get_datetime ( &dt );           /* without this, time component gets corrupted */
        
            memcpy( dd, &d [0], 2 );
          
            day = atoi (dd);

            if ( day == 0 || day > 31 )
                return ret;

            dt.day = day;
            
            memcpy( mm, &d[2], 2 );
      
            mon = atoi (mm);

            if ( mon == 0 || mon > 12 )
                return ret;

            dt.month = mon;
            
            if ( l == 6 )                       /* setting year as well */
            {
                memcpy( yy, &d[4], 2 );
               
                year = atoi (yy);

                if ( year < 0 || year > 99 )
                    return ret;

                dt.year = year + 2000;
            }

            dt.dotw = dow ( &dt );              /* 0 - 6 (SUN - SAT) */

            rtc_set_datetime ( &dt );           /* set new date */
        }

        else
        {
            printf( "\tdate: Syntax error\n" );

            return ret;
        }

        sleep_ms (10); 
    }

    rtc_get_datetime ( &dt );                                                       /* read what we set */

    if ( verbose )
    {
        daystr = calloc ( 4, 1 );
        monstr = calloc ( 4, 1 );

        //day = dow ( &dt );                          /* 0 - 6 (SUN - SAT) */
        //dt.dotw = day;
        strcpy ( daystr, days [dt.dotw] );
        
        strncpy ( monstr, months [dt.month - 1], 4 );

        switch ( dt.day )
        {
            case 1:                                 /* st */
            case 21:
            case 31:
                memcpy ( estr, endstr[0], 2 );
                break;
            case 2:                                 /* nd */
            case 22:
                memcpy ( estr, endstr[1], 2 );
                break;
            case 3:                                 /* rd */
            case 23:
                memcpy ( estr, endstr[2], 2 );
                break;
            default:                                /* th */
                memcpy ( estr, endstr[3], 2 );;
        }
        
        estr[2] = 0;                                /* terminate string */
        
        printf( "%s %2d%s %s %4d\n", daystr, dt.day, estr, monstr, dt.year );
    
        free( monstr );
        free( daystr );
    }

#if ICD_RTC
    extern uint8_t ICDreg [];

    ICDreg [7]  = dt.day % 10;
    ICDreg [8]  = dt.day / 10;
    ICDreg [9]  = dt.month % 10;
    ICDreg [10] = dt.month / 10;
    ICDreg [11] = (dt.year - 1900) % 10;
    ICDreg [12] = (dt.year - 1900) / 10;
#endif

    return GOOD;
}



bool emutime ( char *t, bool verbose )
{
    int         l, ret;
    char        hh[3], mm[3], ss[3];
    datetime_t  tm;
    long        hr, min, sec;

    ret = BAD;                                  /* assume fail */

    if ( *t )                                   /* have an argument, so set time */
    {
        l = strlen (t);

        if ( l == 4 || l == 6 )
        {
            rtc_get_datetime ( &tm );           /* without this, the date component gets corrupted */

            memcpy( hh, &t[0], 2 );

            hh[2] = 0;
            hr = atoi (hh);

            if ( hr < 0 || hr > 23 ) 
            {
                printf ( "emutime() hr entry failed - hr = %d\n", hr );

                return ret;
            }

            tm.hour = hr;

            memcpy( mm, &t[2], 2 );

            mm[2] = 0;
            min = atoi (mm);

            if ( min < 0 || min > 59 ) 
            {
                printf ( "emutime() min entry failed - min = %d\n", min );

                return ret;
            }

            tm.min = min;

            if ( l == 6 )                       /* setting seconds as well */
            {
                memcpy( ss, &t[4], 2 );

                ss[2] = 0;
                sec = atoi (ss);

                if ( sec < 0 || sec > 59 ) 
                {
                    printf ( "emutime() sec entry failed - sec = %d\n", sec );

                    return ret;
                }

                tm.sec = sec;
            }

            rtc_set_datetime ( &tm );           /* set new time */
        }

        else
        {
            printf( "\ttime: Syntax error\n" );

            return ret;
        }

        sleep_ms (10); 
    }

    rtc_get_datetime ( &tm );                   /* read what we have set */

    if ( verbose )
    {
        printf( "%02d:%02d:%02d\n", tm.hour, tm.min, tm.sec );
    }

#if ICD_RTC
    extern uint8_t ICDreg [];

    ICDreg [0]  = tm.sec  % 10;
    ICDreg [1]  = tm.sec  / 10;
    ICDreg [2]  = tm.min  % 10;
    ICDreg [3]  = tm.min  / 10;
    ICDreg [4]  = tm.hour % 10;
    ICDreg [5]  = tm.hour / 10;
#endif

    return GOOD;
}