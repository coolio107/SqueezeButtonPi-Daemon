//
//  control.c
//  SqueezeButtonPi
//
//  The actual control code
//  - Configure buttons and rotary encoders
//  - Send commands when buttons and rotary encoders are used
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

#include "sbpd.h"
#include "control.h"
#include "servercomm.h"
#include <wiringPi.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>

//
//  Pre-allocate encoder and button objects on the stack so we don't have to
//  worry about freeing them
//
static struct button_ctrl button_ctrls[max_buttons];
static struct encoder_ctrl encoder_ctrls[max_encoders];
static int numberofbuttons = 0;
static int numberofencoders = 0;

//
//  Command fragments
//
//  Buttons
//
/*#define FRAGMENT_PAUSE          "[\"pause\"]"
#define FRAGMENT_VOLUME_UP      "[\"button\",\"volup\"]"
#define FRAGMENT_VOLUME_DOWN    "[\"button\",\"voldown\"]"
#define FRAGMENT_PREV           "[\"button\",\"rew\"]"
#define FRAGMENT_NEXT           "[\"button\",\"fwd\"]"
#define FRAGMENT_POWER           "[\"button\",\"power\"]"
*/
//
//  Encoder
//
#define FRAGMENT_VOLUME         "[\"mixer\",\"volume\",\"%s%d\"]"
#define FRAGMENT_TRACK          "[\"playlist\",\"jump\",\"%s%d\"]"

//
//  LMS Command structure
//
static struct lms_command lms_commands[MAX_COMMANDS];
static int numberofcommands = 0;

int add_lms_command_frament ( char * name, char * value ) {
    loginfo("Adding Command %s: Fragment %s", name, value);
    lms_commands[numberofcommands].code = STRTOU32(name);
    strncpy (lms_commands[numberofcommands].fragment, value, MAXLEN);
    numberofcommands ++;
    if (numberofcommands > MAX_COMMANDS )
        return 1;
    return 0;
}

char * get_lms_command_fragment ( int code ) {
    for (int i = 0; i < numberofcommands; i++) {
        if ( code == lms_commands[i].code )
            return lms_commands[i].fragment;
    }
    return NULL;
}

//
//  Button press callback
//  Sets the flag for "button pressed"
//
void button_press_cb(const struct button * button, int change, bool presstype) {
    for (int cnt = 0; cnt < numberofbuttons; cnt++) {
        if (button == button_ctrls[cnt].gpio_button) {
            button_ctrls[cnt].presstype = presstype;
            button_ctrls[cnt].waiting = true;
            loginfo("Button CB set for button #:%d, gpio pin %d", cnt, button_ctrls[cnt].gpio_button->pin);
            return;
        }
    }
}

//
//  Setup button control
//  Parameters:
//      pin: the GPIO-Pin-Number
//      cmd: Command. One of
//                  PLAY    - play/pause
//                  VOL+    - increment volume
//                  VOL-    - decrement volume
//                  PREV    - previous track
//                  NEXT    - next track
//          Command type SCRIPT.
//                  SCRIPT:/path/to/shell/script.sh
//      resist: Optional. one of
//          0 - Internal resistor off
//          1 - pull down         - input puts 3v on GPIO pin
//          2 - pull up (default) - input pulls GPIO pin to ground
//      pressed: Optional GPIO pinstate for button to read pressed
//          0 - state is 0 (default)
//          1 - state is 1
//      cmd_long Command to be used for a long button push, see above command list
//      long_time: Number of milliseconds to define a long press

int setup_button_ctrl(char * cmd, int pin, int resist, int pressed, char * cmd_long, int long_time) {
    char * fragment = NULL;
    char * fragment_long = NULL;
    char * script;
    char * script_long;
    char * separator = ":";
    int cmdtype;
    int cmd_longtype;

    //
    //  Select fragment for short press parameter
    //
    if (strlen(cmd) == 4) {
        fragment = get_lms_command_fragment(STRTOU32(cmd));
        cmdtype = LMS;
    } else if (strncmp("SCRIPT:", cmd, 7) == 0) {
        cmdtype = SCRIPT;
        strtok( cmd, separator );
        script = strtok( NULL, "" );
        fragment = script;
    }
    if (!fragment){
        loginfo("Command %s, not found in defined commands", cmd);
        return -1;
    }
    
    //
    //  Select fragment for long press parameter
    //
    if ( cmd_long == NULL ) {
        cmd_longtype = NOTUSED;
    } else if ( strlen(cmd_long) == 4 ) {
        fragment_long = get_lms_command_fragment(STRTOU32(cmd_long));
        cmd_longtype = LMS;
    } else if (strncmp("SCRIPT:", cmd_long, 7) == 0) {
        cmd_longtype = SCRIPT;
        strtok( cmd_long, separator );
        script_long = strtok( NULL, "" );
        fragment_long = script_long;
    }
    if ( (cmd_long != NULL) & (!fragment_long) ){
        loginfo("Command %s, not found in defined commands", cmd_long);
        cmd_longtype = NOTUSED;
    }
   
    // Make sure resistor setting makes sense, or reset to default
    if ( (resist != PUD_OFF) && (resist != PUD_DOWN) && (resist == PUD_UP) )
        resist = PUD_UP;

    struct button * gpio_b = setupbutton(pin, button_press_cb, resist, (bool)(pressed == 0) ? 0 : 1, long_time);

    button_ctrls[numberofbuttons].cmdtype = cmdtype;
    button_ctrls[numberofbuttons].shortfragment = fragment;
    button_ctrls[numberofbuttons].cmd_longtype = cmd_longtype;
    button_ctrls[numberofbuttons].longfragment = fragment_long;
    button_ctrls[numberofbuttons].waiting = false;
    button_ctrls[numberofbuttons].gpio_button = gpio_b;
    numberofbuttons++;
    loginfo("Button defined: Pin %d, BCM Resistor: %s, Short Type: %s, Short Fragment: %s , Long Type: %s, Long Fragment: %s, Long Press Time: %i",

            pin,
            (resist == PUD_OFF) ? "both" :
            (resist == PUD_DOWN) ? "down" : "up",
            (cmdtype == LMS) ? "LMS" :
            (cmdtype == SCRIPT) ? "Script" : "unused",
            fragment,
            (cmd_longtype == LMS) ? "LMS" :
            (cmd_longtype == SCRIPT) ? "Script" : "unused",
            fragment_long,
            long_time);
    return 0;
}

//
//  Polling function: handle button commands
//  Parameters:
//      server: the server to send commands to
//
void handle_buttons(struct sbpd_server * server) {
    //logdebug("Polling buttons");
    for (int cnt = 0; cnt < numberofbuttons; cnt++) {
        if (button_ctrls[cnt].waiting) {
            loginfo("Button pressed: Pin: %d, Press Type:%s", button_ctrls[cnt].gpio_button->pin,
                   (button_ctrls[cnt].presstype == LONGPRESS) ? "Long" : "Short" );
            if ( button_ctrls[cnt].presstype == SHORTPRESS ) {
                if ( button_ctrls[cnt].shortfragment != NULL ) {
                    send_command(server, button_ctrls[cnt].cmdtype, button_ctrls[cnt].shortfragment);
                } 
            }
            if ( button_ctrls[cnt].presstype == LONGPRESS ) {
                if ( button_ctrls[cnt].longfragment != NULL ) {
                    send_command(server, button_ctrls[cnt].cmd_longtype, button_ctrls[cnt].longfragment);
                } else {
                    loginfo("No Long Press command configured");
                }
            }
            button_ctrls[cnt].waiting = false;  // clear waiting
        }
    }
}


//
//  Encoder interrupt callback
//  Currently does nothing since we poll for volume changes
//
void encoder_rotate_cb(const struct encoder * encoder, long change) {
    logdebug("Interrupt: encoder value: %d change: %d", encoder->value, change);
}

//
//  Setup encoder control
//  Parameters:
//      cmd: Command. Currently only
//                  VOLU    - volume
//                  TRAC    - previous or next track
//          Can be NULL for volume
//      pin1: the GPIO-Pin-Number for the first pin used
//      pin2: the GPIO-Pin-Number for the second pin used
//      edge: one of
//                  1 - falling edge
//                  2 - rising edge
//                  0, 3 - both
//
//
int setup_encoder_ctrl(char * cmd, int pin1, int pin2, int edge) {
    char * fragment = NULL;
    if (strlen(cmd) > 4)
        return -1;
    //
    //  Select fragment for parameter
    //  Would love to "switch" here but that's not portable...
    //
    uint32_t code = STRTOU32(cmd);
    if (code == STRTOU32("VOLU")) {
        fragment = FRAGMENT_VOLUME;
        encoder_ctrls[numberofencoders].limit = 100;
        encoder_ctrls[numberofencoders].min_time = 0;
    } else if (code == STRTOU32("TRAC")) {
        fragment = FRAGMENT_TRACK;
        encoder_ctrls[numberofencoders].limit = 1;
        encoder_ctrls[numberofencoders].min_time = 500;
    } 
    if ( fragment == NULL ) {
        return -1;
    }

    struct encoder * gpio_e = setupencoder(pin1, pin2, encoder_rotate_cb, edge);
    encoder_ctrls[numberofencoders].fragment = fragment;
    encoder_ctrls[numberofencoders].gpio_encoder = gpio_e;
    encoder_ctrls[numberofencoders].last_value = 0;
    encoder_ctrls[numberofencoders].last_time = 0;
    numberofencoders++;
    loginfo("Rotary encoder defined: Pin %d, %d, Edge: %s, Fragment: \n%s",
            pin1, pin2,
            ((edge != INT_EDGE_FALLING) && (edge != INT_EDGE_RISING)) ? "both" :
            (edge == INT_EDGE_FALLING) ? "falling" : "rising",
            fragment);
    return 0;
}

//
//  Polling function: handle encoder commands
//  Parameters:
//      server: the server to send commands to
//
void handle_encoders(struct sbpd_server * server) {
    //
    //  chatter filter set duration in encoder setup.
    //      - volume set to 0...
    //      - track change set to 500ms
    //  We poll every 100ms anyway plus wait for network action to complete
    //
    long long time = ms_timer();

    //logdebug("Polling encoders");

    int command = LMS;

    for (int cnt = 0; cnt < numberofencoders; cnt++) {
        //
        //  build volume delta
        //  ignore if > 100: overflow
        //
        int delta = (int)(encoder_ctrls[cnt].gpio_encoder->value - encoder_ctrls[cnt].last_value);
        if (delta > 100)
            delta = 0;
        if (delta != 0) {
            //Check if change happened before minimum delay, clear out data.
            if ( encoder_ctrls[cnt].last_time + encoder_ctrls[cnt].min_time > time ) {
                loginfo("Encoder on GPIO %d, %d value change: %d, before %d ms ellapsed not sending lms command.",
                    encoder_ctrls[cnt].gpio_encoder->pin_a,
                    encoder_ctrls[cnt].gpio_encoder->pin_b,
                    delta,
                    (encoder_ctrls[cnt].min_time) );
                encoder_ctrls[cnt].last_value = encoder_ctrls[cnt].gpio_encoder->value;
                return;
            }

            loginfo("Encoder on GPIO %d, %d value change: %d",
                    encoder_ctrls[cnt].gpio_encoder->pin_a,
                    encoder_ctrls[cnt].gpio_encoder->pin_b,
                    delta);

            char fragment[50];
            char * prefix = (delta > 0) ? "+" : "-";
            if ( abs(delta) > encoder_ctrls[cnt].limit ) {
                     delta = encoder_ctrls[cnt].limit;
            }
            snprintf(fragment, sizeof(fragment),
                     encoder_ctrls[cnt].fragment, prefix, abs(delta));

            if (send_command(server, command, fragment)) {
                encoder_ctrls[cnt].last_value = encoder_ctrls[cnt].gpio_encoder->value;
                encoder_ctrls[cnt].last_time = time; // chatter filter
            }
        }
    }
}





