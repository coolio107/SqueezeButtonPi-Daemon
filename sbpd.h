//
//  sbpd.h
//  SqueezeButtonPi
//
//  General state and data structure definitions
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

#ifndef sbpd_h
#define sbpd_h

#include <stdio.h>
#include <syslog.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdbool.h>

#define USER_AGENT  "SqueezeButtonPi"
#define VERSION     "1.0"

// configuration parameters.
// used to flag pre-configured or detected parameters
typedef enum {
    // server
    SBPD_cfg_host = 0x1,
    SBPD_cfg_port = 0x2,
    SBPD_cfg_user = 0x4,
    SBPD_cfg_password = 0x8,
    
    // player
    SBPD_cfg_MAC = 0x1000,
} sbpd_config_parameters_t;


// supported commands
// need to be configured
typedef enum {
    SBPD_cmd_volume = 0x1,
    SBPD_cmd_pause = 0x2,
    SBPD_cmd_next = 0x4,
    SBPD_cmd_prev = 0x8,
    SBPD_cmd_power = 0x10,
} sbpd_commands_t;


// server configuration data structure
// contains address and user/password
struct sbpd_server {
    char *      host;
    uint32_t    port;
    char *      user;
    char *      password;
};

//
//  Define scheduling behavior
//  We use usleep so this is in µs
//
#define SCD_SLEEP_TIMEOUT   100000
#define SCD_SECOND          1000000

//
//  Helpers
//
#define STRTOU32(x) (*((uint32_t *)x))  // make a 32 bit integer from a 4 char string

//
//  Logging
//
#define logerr( args... )     _mylog( __FILE__, __LINE__, LOG_ERR, args )
#define logwarn( args... )    _mylog( __FILE__, __LINE__, LOG_WARNING, args )
#define lognotice( args... )  _mylog( __FILE__, __LINE__, LOG_NOTICE, args )
#define loginfo( args... )    _mylog( __FILE__, __LINE__, LOG_INFO, args )
#define logdebug( args... )    _mylog( __FILE__, __LINE__, LOG_DEBUG, args )
void _mylog( const char *file, int line, int prio, const char *fmt, ... );
int loglevel();

#endif /* sbpd_h */
