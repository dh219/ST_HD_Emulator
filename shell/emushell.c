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
#include <pico/stdlib.h>
#include "pico.h"
#include "malloc.h"
#include "hardware/watchdog.h"
#include "../emu.h"
#include "emushell.h"


extern int  mountRAW ( int );
extern bool cpuFreq  ( char* );

extern DRIVES   drv []; 




void rebootMCU ( void )
{
    watchdog_reboot (0, SRAM_END, 10);
    while (1);
}




typedef struct {
    char *name;				/* command name */
    char *doc;				/* short documentation  */
    char *detail;			/* long documentation */
} COMMAND;

static COMMAND commands[] = {
{ "help",   
  "Display this text or give help about a command",
  "<cmd>\n"
  "     displays more information about <cmd>\n" 
  "     valid commands are:\n"
  "          cpdisk, cpu, date, debug, help, quit, reboot, status, time, uptime\n"
},

{ "date", 
  "read or set date",
  "[ddmmyyyy]" 
},

{ "time", 
  "read or set time",
  "[hhmmss]" 
},

{ "uptime", 
  "Display elapsed time",
  "displays elapsed time since power on" 
},

{ "status", 
  "Display Emulator info",
  "list stuff" 
},

{ "quit",  
  "Exit shell", 
  NULL 
},

{ "cpdisk",   
  "copy one disk to the other", 
  "<src> <dst>\n"
  "     <src> and <dst> must both be inserted\n"
  "     example: Emu> cpdisk sd0 sd1\n"
},

{ NULL, NULL, NULL }

};



void help ( char *command )
{
    COMMAND *help;
    
    help = commands;
    
    while ( help->name )
    {
        if ( strstr ( help->name, command ) )
        {
            printf( " %-10s  %s\r\n", help->name, help->detail );
            
            break;
        }
        
        help++;
    }
}



/*
 * shellCmd ()
 *
 * command parser
 *
 * valid commands are:
 *      cpdisk
 *      cpu
 *      date
 *      debug
 *      help
 *      quit
 *      reboot
 *      sdtest
 *      status
 *      time
 *      uptime
 *
 * returns true on success
 */


bool shellCmd ( char *cmd )
{
    char  argv[ MAX_ARGS + 1 ][ MAX_ARG_LENGTH ];
    char  *tptr, *cmdcpy;
    int   argc, cmdlen, ret;
    char  dels[3] = " \r\0";                    /* argument delimiters */


    cmdlen = strlen ( cmd );              
    cmdcpy = calloc ( cmdlen + 1, 1 ); 
    
    if ( ! cmdcpy ) {
        
        printf ( "%s: calloc failed\r\n", __func__ );
        
        return BAD;
    }
    
    memset ( argv, 0, ( MAX_ARGS + 1 ) * MAX_ARG_LENGTH );

    strcpy ( cmdcpy, cmd );                     /* make a copy of the command line */
                                                /* as strtok destroys it */
    tptr = strtok ( cmdcpy, dels );
                                                /* break the command line in to tokens */
    for ( argc = 0; tptr && argc <= MAX_ARGS; argc++ )
    {
        strcpy ( argv[ argc ], tptr );

        tptr = strtok ( NULL, dels );
    }
    
    if ( --argc < 0 )
        argc = 0;                               /* correction to reflect real argument count */

    ret = GOOD;
    
    if     ( strncmp (   argv[0],     "cpdisk",       6 ) == 0 )
    {
        if ( argc != 2  )
            SYNTAX( argv[0] )
        else
            cpdisk ( argv [1], argv [2] );
        
        ret = BAD;
    }

    else if ( strncmp (   argv[0],    "cpu",          3 ) == 0 )
    {
        if ( argc > 1 )
            SYNTAX ( argv [0] )

        else
            cpuFreq ( argv[1] );
    }

    else if ( strncmp (   argv[0],    "date",         4 ) == 0 )
        ret = emudate ( argv[1], 1 );

    else if ( strncmp (   argv[0],    "debug",        5 ) == 0 )
    {
        if ( argc != 1 )
            SYNTAX ( argv [0] )

        else
            debugVerbose ( argv[1] );
    }
    
    else if ( strncmp(   argv[0],     "help",         4 ) == 0 )
        help ( argv[1] );
    
    else if ( strncmp (   argv[0],    "quit",         4 ) == 0 )
    {
        ret = BAD;
    }

    else if ( strncmp (   argv[0],    "exit",         4 ) == 0 )
    {
        ret = BAD;
    }
    
    else if ( strncmp (   argv[0],    "reboot",       6 ) == 0 )
        rebootMCU ();

    if     ( strncmp (   argv[0],     "sdtest",       6 ) == 0 )
    {
        if ( argc != 1  )
            SYNTAX( argv[0] )
        else
            sdtest ( argv [1] );
        
        ret = BAD;
    }
    
    else if ( strncmp (   argv[0],    "status",       6 ) == 0 )
        status ();
    
    else if ( strncmp (   argv[0],    "time",         4 ) == 0 )
        ret = emutime ( argv[1], 1 );
    
    else if ( strncmp (   argv[0],    "uptime",       6 ) == 0 )
        ret = uptime ();
    
    free ( cmdcpy );
    
    return ret;
}



/*
 *
 * shell main
 *
 */

void doShell ( void )
{
    static char cmd [ SHELL_CMDBUF ];
    char   *p;
    int    i;
    
    
    do 
    {
        p = cmd;
        i = 0;
        
        printf( "Emu> " );
        
        while( ( *p = getchar ()  ) != CR ) {
            
            if ( *p == BS || *p == DEL )
            {
                if ( i == 0 )
                    continue;

                *p-- = 0;
                putchar ( BS );
                putchar ( SPACE );
                putchar ( BS );
               
                if ( --i < 0 )
                    i = 0;
            }
    
            else
            {
                putchar ( *p++ );
                i++;
            }
        }
       
        putchar ( CR );
        putchar ( LF );
        
        *p = 0;                                 /* terminate line with a NULL */
    } 
    while ( shellCmd ( cmd ) == GOOD );         /* keep going until quit/exit */
}


