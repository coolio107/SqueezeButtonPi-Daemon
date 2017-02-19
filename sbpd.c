//
//  main.c
//  SqueezeButtonPi
//
//  Squeezebox Button Control for Raspberry Pi - Daemon.
//
//  Use buttons and rotary encoders to control
//  SqueezeLite or SqueezePlay running on a Raspberry Pi
//  Works with Logitech Media Server
//
//  Created by Jörg Schwieder on 02.02.17.
//
//
//  Copyright (c) 2017, Joerg Schwieder, PenguinLovesMusic.com
//  All rights reserved.
//
//  Redistribution and use in source and binary forms, with or without
//  modification, are permitted provided that the following conditions are met:
//
//   * Redistributions of source code must retain the above copyright
//     notice, this list of conditions and the following disclaimer.
//   * Redistributions in binary form must reproduce the above copyright
//     notice, this list of conditions and the following disclaimer in the
//     documentation and/or other materials provided with the distribution.
//   * Neither the name of ickStream nor the names of its contributors
//     may be used to endorse or promote products derived from this software
//     without specific prior written permission.
//
//  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
//  ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
//  WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
//  IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
//  INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
//  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
//  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
//  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
//  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
//  EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//

#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdlib.h>
#include <fcntl.h>
#include <argp.h>
#include <sys/time.h>
#include <sys/param.h>
#include "sbpd.h"
#include "discovery.h"
#include "servercomm.h"
#include "control.h"

//
//  Server configuration
//
static sbpd_config_parameters_t configured_parameters = 0;
static sbpd_config_parameters_t discovered_parameters = 0;
static struct sbpd_server server;
static char * MAC;

//
//  signal handling
//
static volatile int stop_signal;
static void sigHandler( int sig, siginfo_t *siginfo, void *context );

//
//  Logging
//
static int streamloglevel = LOG_NOTICE;
static int sysloglevel = LOG_ALERT;

//
//  Argument Parsing
//
const char *argp_program_version = USER_AGENT " " VERSION;
const char *argp_program_bug_address = "<coolio@penguinlovesmusic.com>";
static error_t parse_opt(int key, char *arg, struct argp_state *state);
static error_t parse_arg();
//
//  OPTIONS.  Field 1 in ARGP.
//  Order of fields: {NAME, KEY, ARG, FLAGS, DOC, GROUP}.
//
static struct argp_option options[] =
{
    { "mac",       'M', "MAC-Address", 0,
        "Set MAC address of player. Deafult: autodetect", 0 },
    { "address",   'A', "Server-Address", 0,
        "Set server address. Default: autodetect", 0 },
    { "port",      'P', "xxxx", 0, "Set server control port. Default: autodetect", 0 },
    { "username",  'u', "user name", 0, "Set user name for server. Default: none", 0 },
    { "password",  'p', "password", 0, "Set password for server. Default: none", 0 },
    { "verbose",   'v', 0, 0, "Produce verbose output", 1 },
    { "silent",    's', 0, 0, "Don't produce output", 1 },
    { "daemonize", 'd', 0, 0, "Daemonize", 1 },
    { "kill",      'k', 0, 0, "Kill daemon", 1 },
    {0}
};
//
//  ARGS_DOC. Field 3 in ARGP.
//  Non-Option arguments.
//  At least one needs to be specified for the daemon to do anything useful
//  Arguments are a comma-separated list of configuration parameters:
//  For rotary encoders (one, volume only):
//      e,pin1,pin2,CMD[,edge]
//          "e" for "Encoder"
//          p1, p2: GPIO PIN numbers in BCM-notation
//          CMD: Command. Unused for encoders, always VOLM for Volume
//          edge: Optional. one of
//                  1 - falling edge
//                  2 - rising edge
//                  0, 3 - both
//  For buttons:
//      b,pin,CMD[,edge]
//          "b" for "Button"
//          pin: GPIO PIN numbers in BCM-notation
//          CMD: Command. One of:
//              PLAY:   Play/pause
//              PREV:   Jump to previous track
//              NEXT:   Jump to next track
//              VOL+:   Increase volume
//              VOL-:   Decrease volume
//              POWR:   Toggle power state
//          edge: Optional. one of
//                  1 - falling edge
//                  2 - rising edge
//                  0, 3 - both
//
static char args_doc[] = "[e,pin1,pin2,CMD,edge] [b,pin,CMD,edge...]";
//
//  DOC.  Field 4 in ARGP.
//  Program documentation.
//
static char doc[] = "sbpd - SqueezeButtinPiDaemon is a button and rotary encoder handling daemon for Raspberry Pi and a Squeezebox player software.\nsbpd connects to a Squeezebox server and sends the configured control commands on behalf of a player running on the RPi.\n<C>2017 Joerg Schwieder/PenguinLovesMusic.com";
//
//  ARGP parsing structure
//
static struct argp argp = {options, parse_opt, args_doc, doc};
static bool arg_daemonize = false;
static char *arg_elements[max_buttons + max_encoders];
static int arg_element_count = 0;

int main(int argc, char * argv[]) {
    //
    //  Parse Arguments
    //
    argp_parse (&argp, argc, argv, 0, 0, 0);

    //
    //  Daemonize
    //
    if (arg_daemonize) {
        int cpid = fork();
        if( cpid == -1 ) {
            logerr("Could not fork sbpd");
            return -1;
        }
        if (cpid)   // Parent process exits ...
            return 0;
        if( setsid() == -1 ) {
            logerr("Could not create new session");
            return -2;
        }
        int fd = open( "/dev/null", O_RDWR, 0 );
        if( fd!=-1) {
            dup2(fd, STDIN_FILENO);
            dup2(fd, STDOUT_FILENO);
            dup2(fd, STDERR_FILENO);
        }
    }
    
    //
    //  Init GPIO
    //  Done after daemonization becasue child process needs to have GPIO initilized
    //
    init_GPIO();
    
    //
    //  Now parse GPIO elements
    //  Needed to initialize GPIO first
    //
    parse_arg();
    
    //
    // Configure signal handling
    //
    struct sigaction act;
    memset( &act, 0, sizeof(act) );
    act.sa_sigaction = &sigHandler;
    act.sa_flags     = SA_SIGINFO;
    sigaction( SIGINT, &act, NULL );
    sigaction( SIGTERM, &act, NULL );
    
    
    //
    // Find MAC
    //
    if (!(configured_parameters & SBPD_cfg_MAC)) {
        MAC = find_mac();
        if (!MAC)
            return -1;  // no MAC, no control
        discovered_parameters |= SBPD_cfg_MAC;
    }
    
    //
    //  Initialize server communication
    //
    init_comm(MAC);
    
    //
    //
    // Main Loop
    //
    //
    loginfo("Starting main loop polling");
    while( !stop_signal ) {
        //
        //  Poll the server discovery
        //
        poll_discovery(configured_parameters,
                       &discovered_parameters,
                       &server);
        handle_buttons(&server);
        handle_encoders(&server);
        //
        // Just sleep...
        //
        usleep( SCD_SLEEP_TIMEOUT ); // 0.1s
        
    } // end of: while( !stop_signal )
    
    //
    //  Shutdown server communication
    //
    shutdown_comm();
    
    return 0;
}

//
//
//  Argument parsing
//
//
//
//  PARSER. Field 2 in ARGP.
//  Order of parameters: KEY, ARG, STATE.
//
static error_t
parse_opt (int key, char *arg, struct argp_state *state)
{
    switch (key)
    {
            //
            //  Output Modes and Misc.
            //
            //  Verbose mode
        case 'v':
            loginfo("Options parsing: Set verbose mode");
            streamloglevel = LOG_DEBUG;
            break;
            //  Silent Mode
        case 's':
            streamloglevel = 0;
            loginfo("Options parsing: Set quiet mode");
            break;
            //  Daemonize
        case 'd':
            loginfo("Options parsing: Daemonize");
            // don't daemonize here, parse all args first
            arg_daemonize = true;
            break;
            
            //
            //  Player parameter
            //
            //  MAC Address
        case 'M':
            MAC = arg;
            loginfo("Options parsing: Manually set MAC: %s", MAC);
            configured_parameters |= SBPD_cfg_MAC;
            break;
            
            //
            // Server Parameters
            //
            //  Server address
        case 'A':
            server.host = arg;
            loginfo("Options parsing: Manually set http address %s", server.host);
            configured_parameters |= SBPD_cfg_host;
            break;
            //  Server port
        case 'P':
            server.port = (uint32_t)strtoul(arg, NULL, 10);
            loginfo("Options parsing: Manually set http port %s", server.port);
            configured_parameters |= SBPD_cfg_port;
            break;
            //  Server user name
        case 'u':
            server.user = arg;
            loginfo("Options parsing: Manually set user name");
            configured_parameters |= SBPD_cfg_user;
            break;
            //  Server user name
        case 'p':
            server.password = arg;
            loginfo("Options parsing: Manually set http password");
            configured_parameters |= SBPD_cfg_password;
            break;
            
        case ARGP_KEY_ARG:
            if (arg_element_count == (max_encoders + max_buttons)) {
                logerr("Too many control elements defined");
                return ARGP_ERR_UNKNOWN;
            }
            arg_elements[arg_element_count] = arg;
            arg_element_count++;
            break;
        case ARGP_KEY_END:
            break;
        default:
            return ARGP_ERR_UNKNOWN;
    }
    return 0;
}
//
//  Parse non-option arguments
//
//  GPIO devices
//
//  Arguments are a comma-separated list of configuration parameters:
//  For rotary encoders (one, volume only):
//      e,pin1,pin2,CMD[,edge]
//          "e" for "Encoder"
//          p1, p2: GPIO PIN numbers in BCM-notation
//          CMD: Command. Unused for encoders, always VOLM for Volume
//          edge: Optional. one of
//                  1 - falling edge
//                  2 - rising edge
//                  0, 3 - both
//  For buttons:
//      b,pin,CMD[,edge]
//          "b" for "Button"
//          pin: GPIO PIN numbers in BCM-notation
//          CMD: Command. One of:
//              PLAY:   Play/pause
//              PREV:   Jump to previous track
//              NEXT:   Jump to next track
//              VOL+:   Increase volume
//              VOL-:   Decrease volume
//              POWR:   Toggle power state
//          edge: Optional. one of
//                  1 - falling edge
//                  2 - rising edge
//                  0, 3 - both
//
//
static error_t parse_arg() {
    for (int arg_num = 0; arg_num < arg_element_count; arg_num++) {
        char * arg = arg_elements[arg_num];
        {
            char * code = strtok(arg, ",");
            if (strlen(code) != 1)
                return ARGP_ERR_UNKNOWN;
            switch (code[0]) {
                case 'e': {
                    char * string = strtok(NULL, ",");
                    int p1 = 0;
                    if (string)
                        p1 = (int)strtol(string, NULL, 10);
                    string = strtok(NULL, ",");
                    int p2 = 0;
                    if (string)
                        p2 = (int)strtol(string, NULL, 10);
                    char * cmd = strtok(NULL, ",");
                    string = strtok(NULL, ",");
                    int edge = 0;
                    if (string)
                        edge = (int)strtol(string, NULL, 10);
                    setup_encoder_ctrl(cmd, p1, p2, edge);
                }
                    break;
                case 'b': {
                    char * string = strtok(NULL, ",");
                    int pin = 0;
                    if (string)
                        pin = (int)strtol(string, NULL, 10);
                    char * cmd = strtok(NULL, ",");
                    string = strtok(NULL, ",");
                    int edge = 0;
                    if (string)
                        edge = (int)strtol(string, NULL, 10);
                    setup_button_ctrl(cmd, pin, edge);
                }
                    break;
                    
                default:
                    break;
            }
        }
    }
    return 0;
}


//
//
//  Misc. code
//
//


//
// Handle signals
//
static void sigHandler( int sig, siginfo_t *siginfo, void *context )
{
    //
    // What sort of signal is to be processed ?
    //
    switch( sig ) {
            //
            // A normal termination request
            //
        case SIGINT:
        case SIGTERM:
            stop_signal = sig;
            break;
            //
            // Ignore broken pipes
            //
        case SIGPIPE:
            break;
    }
}

//
//  Logging facility
//
void _mylog( const char *file, int line,  int prio, const char *fmt, ... )
{
    //
    // Init arguments, lock mutex
    //
    va_list a_list;
    va_start( a_list, fmt );
    
    //
    //  Log to stream
    //
    if( prio <= streamloglevel) {
        
        // select stream due to priority
        FILE *f = (prio < LOG_INFO) ? stderr : stdout;
        //FILE *f = stderr;
        
        // print timestamp, prio and thread info
        struct timeval tv;
        gettimeofday( &tv, NULL );
        double time = tv.tv_sec+tv.tv_usec*1E-6;
        fprintf( f, "%.4f %d", time, prio);
        
        // prepend location to message (if available)
        if( file )
            fprintf( f, " %s,%d: ", file, line );
        else
            fprintf( f, ": " );
        
        // the message itself
        vfprintf( f, fmt, a_list );
        
        // New line and flush stream buffer
        fprintf( f, "\n" );
        fflush( f );
    }
    
    //
    //  use syslog facility, hide debugging messages from syslog
    //
    if( prio <= sysloglevel && prio < LOG_DEBUG)
        vsyslog( prio, fmt, a_list );
    
    //
    //  Clean variable argument list, unlock mutex
    //
    va_end ( a_list );
}

int loglevel() {
    return MAX(sysloglevel, streamloglevel);
}

