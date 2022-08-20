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
    { "conf_file", 'f', "</path/config-file>", 0, "Full path to command configuration file", 0},
    { "mac",       'M', "MAC-Address", 0,
        "Set MAC address of player. Default: autodetect", 0 },
    { "address",   'A', "Server-Address", 0,
        "Set server address. Default: autodetect", 0 },
    { "port",      'P', "xxxx", 0, "Set server control port. Default: autodetect", 0 },
    { "username",  'u', "user name", 0, "Set user name for server. Default: none", 0 },
    { "password",  'p', "password", 0, "Set password for server. Default: none", 0 },
    { "verbose",   'v', 0, 0, "Produce verbose output", 1 },
    { "silent",    's', 0, 0, "Don't produce output", 1 },
    { "daemonize", 'd', 0, 0, "Daemonize", 1 },
//    { "kill",      'k', 0, 0, "Kill daemon", 1 },
    { "debug",     'z', 0, 0, "Produce debug output", 1 },
    {0}
};
//
//  ARGS_DOC. Field 3 in ARGP.
//  Non-Option arguments.
static char args_doc[] = "[e,pin1,pin2,CMD,edge] [b,pin,CMD,resist,pressed...]";
//
//
//  DOC.  Field 4 in ARGP.
//  Program documentation.
//
static char doc[] = "sbpd - SqueezeButtinPiDaemon is a button and rotary encoder handling daemon for Raspberry Pi and a Squeezebox player software.\nsbpd connects to a Squeezebox server and sends the configured control commands on behalf of a player running on the RPi.\n<C>2017 Joerg Schwieder/PenguinLovesMusic.com\n\n\
At least one needs to be specified for the daemon to do anything useful\n\
Arguments are a comma-separated list of configuration parameters:\n\
For rotary encoders:\n\
    e,pin1,pin2,CMD[,edge]\n\
        \"e\" for \"Encoder\"\n\
        pin1, pin2: GPIO PIN numbers in BCM-notation\n\
        CMD: Command. one of. \n\
                    VOLU for Volume\n\
                    TRAC for Prev/Next track\n\
        edge: Optional. one of\n\
                1 - falling edge\n\
                2 - rising edge\n\
                0, 3 - both\n\
For buttons:\n\
    b,pin,CMD[,resist,pressed,CMD_LONG,long_time]\n\
        \"b\" for \"Button\"\n\
         pin:  GPIO PIN numbers in BCM-notation\n\
         CMD: Command. One of.\n\
                    PLAY    - play/pause\n\
                    VOL+    - increment volume\n\
                    VOL-    - decrement volume\n\
                    PREV    - previous track\n\
                    NEXT    - next track\n\
              Commands can be defined in config file\n\
                    use -f option, ref:sbpd_commands.cfg \n\
              Command type SCRIPT.\n\
                    SCRIPT:/path/to/shell/script.sh\n\
         resist: Optional. one of\n\
              0 - Internal resistor off\n\
              1 - pull down         - input puts 3v on GPIO pin\n\
              2 - pull up (default) - input pulls GPIO pin to ground\n\
         pressed: Optional GPIO pinstate for button to read pressed\n\
              0 - state is 0 (default)\n\
              1 - state is 1\n\
         CMD_LONG: Command to be used for a long button push, see above list\n\
         long_time: Number of milliseconds for a long button press\n";
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
    //  Parse command config file
    //
    parse_config();

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
    error_t arg_err = parse_arg();
    
	if ( arg_err != 0 ) {
       return -2;
    }
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
            streamloglevel = LOG_INFO;
            loginfo("Options parsing: Set verbose mode");
            break;
            //  Debug mode
        case 'z':
            streamloglevel = LOG_DEBUG;
            loginfo("Options parsing: Set debug mode");
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
            //  Server password
        case 'p':
            server.password = arg;
            loginfo("Options parsing: Manually set http password");
            configured_parameters |= SBPD_cfg_password;
            break;
        // Server Configuration file for button commands
        case 'f':
            server.config_file = arg;
            loginfo("Options parsing: Setting command config file to %s", server.config_file);
            configured_parameters |= SBPD_cfg_config;
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
//  For rotary encoders:
//      e,pin1,pin2,CMD[,edge]
//          "e" for "Encoder"
//          pin1, pin2: GPIO PIN numbers in BCM-notation
//          CMD:        VOLU for Volume
//                      TRAC for Playlist previous/next
//          edge: Optional. one of
//                  1 - falling edge
//                  2 - rising edge
//                  0, 3 - both
//  For buttons:
//      b,pin,CMD[,resist,pressed,CMD_LONG]
//          "b" for "Button"
//           pin:  GPIO PIN numbers in BCM-notation
//           CMD: Command. One of
//                      PLAY    - play/pause
//                      VOL+    - increment volume
//                      VOL-    - decrement volume
//                      PREV    - previous track
//                      NEXT    - next track
//                Command type SCRIPT.
//                      SCRIPT:/path/to/shell/script.sh
//           resist: Optional. one of
//                0 - Internal resistor off
//                1 - pull down         - input puts 3v on GPIO pin
//                2 - pull up (default) - input pulls GPIO pin to ground
//           pressed: Optional GPIO pinstate for button to read pressed
//                0 - state is 0 (default)
//                1 - state is 1
//           CMD_LONG: Command to be used for a long button push, see above command list
//           long_time: Number of milliseconds to define a long press
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
                    if ( (p1 == 0) | (p2 == 0) | (cmd == NULL) ) {
                        logerr("Encoder argument error");
                        return ARGP_ERR_UNKNOWN;
                    }
                    setup_encoder_ctrl(cmd, p1, p2, edge);
                }
                    break;
                case 'b': {
                    char * string = strtok(NULL, ",");
                    int pin = 0;
                    if (string)
                        pin = (int)strtol(string, NULL, 10);
                    char * cmd = strtok(NULL, ",");
                    int resist = 2;
                    string = strtok(NULL, ",");
                    if (string)
                        resist = (int)strtol(string, NULL, 10);
                    bool pressed = 0;
                    string = strtok(NULL, ",");
                    if (string)
                        pressed = (int)strtol(string, NULL, 10);
                    char * cmd_long = NULL;
                    if (string)
                        cmd_long = strtok(NULL, ",");
                    string = strtok(NULL, ",");
                    uint32_t long_time=3000;
                    if (string)
                        long_time = (int)strtol(string, NULL, 10);
                    if ( (pin == 0) | (cmd == NULL) ) {
                        logerr("Button argument error");
                        return ARGP_ERR_UNKNOWN;
                    }
                    setup_button_ctrl(cmd, pin, resist, pressed, cmd_long, long_time);
                }
                    break;
                    
                default:
                    break;
            }
        }
    }
    return 0;
}

void parse_config() {
    char *s, buff[256];
    FILE *fp = NULL;
    if (configured_parameters & SBPD_cfg_config) {
        fp = fopen ( server.config_file, "r");
        if (fp == NULL) loginfo("Config file %s : not found", server.config_file);
    }
    if (fp == NULL) {
        loginfo("Using builtin button configuration");
        add_lms_command_frament ( "PLAY", "[\"pause\"]" );
        add_lms_command_frament ( "VOL+", "[\"button\",\"volup\"]" );
        add_lms_command_frament ( "VOL-", "[\"button\",\"voldown\"]" );
        add_lms_command_frament ( "PREV", "[\"button\",\"rew\"]" );
        add_lms_command_frament ( "NEXT", "[\"button\",\"fwd\"]" );
        add_lms_command_frament ( "POWR", "[\"button\",\"power\"]" );
        return;
    }
    //Start reading file, line by line
    while ((s = fgets (buff, sizeof buff, fp)) != NULL) {
        //Skip blank lines and comments
        if (buff[0] == '\n' || buff[0] == '#')
            continue;

        //Parse name/value pair from line
        // names are 4 characters only.
        char name[MAXLEN] = "";
        char value[MAXLEN] = "";
        s = strtok (buff, "=");
        if (s==NULL)
            continue;
        else
            strncpy (name, s, 4);
        s = strtok (NULL, "=");
        if (s==NULL)
            continue;
        else
            strncpy (value, s, MAXLEN);
        // Remove beginning and trailing whitespace
        trim (value);

        loginfo ("name=%s, value=%s", name, value);
        if (strlen(name) == 4) {  
            if ( add_lms_command_frament ( name, value ) != 0 ) {  
                loginfo ("Too many commands in config file, reduce the number of commands");
                continue;
            } 
        } else {
            loginfo ("Invalid or missing commands in config file.");
        }
    }
    fclose (fp);
}

//
//
//  Misc. code
//
//

//
// trim: get rid of trailing and leading whitespace, including the trailing "\n" from fgets()
//
char * trim (char * s) {
    // Initialize start, end pointers
    char *s1 = s, *s2 = &s[strlen (s) - 1];
    // Trim and delimit right side
    while ( (isspace (*s2)) && (s2 >= s1) )
        s2--;
    *(s2+1) = '\0';

    // Trim left side
    while ( (isspace (*s1)) && (s1 < s2) )
        s1++;

    // Copy finished string
    strcpy (s, s1);
    return s;
}

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

long long ms_timer(void) {
    struct timespec tv;

    clock_gettime(CLOCK_REALTIME, &tv);
    return (((long long)tv.tv_sec)*1000)+(tv.tv_nsec/1e6);
}
