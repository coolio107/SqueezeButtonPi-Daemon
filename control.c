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
#define FRAGMENT_PAUSE          "[\"pause\"]"
#define FRAGMENT_VOLUME_UP      "[\"button\",\"volume_up\"]"
#define FRAGMENT_VOLUME_DOWN    "[\"button\",\"voldown\"]"
#define FRAGMENT_PREV           "[\"button\",\"rew\"]"
#define FRAGMENT_NEXT           "[\"button\",\"fwd\"]"
#define FRAGMENT_POWER           "[\"button\",\"power\"]"
//
//  Encoder
//
#define FRAGMENT_VOLUME         "[\"mixer\",\"volume\",\"%s%d\"]"

//
//  Button press callback
//  Sets the flag for "button pressed"
//
void button_press_cb(const struct button * button, int change) {
    for (int cnt = 0; cnt < numberofbuttons; cnt++) {
        if (button == button_ctrls[cnt].gpio_button) {
            button_ctrls[cnt].waiting = true;
            return;
        }
    }
}

//
//  Setup button control
//  Parameters:
//      cmd: Command. One of
//                  PLAY    - play/pause
//                  VOL+    - increment volume
//                  VOL-    - decrement volume
//                  PREV    - previous track
//                  NEXT    - next track
//      pin: the GPIO-Pin-Number
//      edge: one of
//                  1 - falling edge
//                  2 - rising edge
//                  0, 3 - both
//
int setup_button_ctrl(char * cmd, int pin, int edge) {
    char * fragment = NULL;
    if (strlen(cmd) > 4)
        return -1;
    
    //
    //  Select fragment for parameter
    //  Would love to "switch" here but that's not portable...
    //
    uint32_t code = STRTOU32(cmd);
    if (code == STRTOU32("PLAY")) {
        fragment = FRAGMENT_PAUSE;
    } else if (code == STRTOU32("VOL+")) {
        fragment = FRAGMENT_VOLUME_UP;
    } else if (code == STRTOU32("VOL-")) {
        fragment = FRAGMENT_VOLUME_DOWN;
    } else if (code == STRTOU32("PREV")) {
        fragment = FRAGMENT_PREV;
    } else if (code == STRTOU32("NEXT")) {
        fragment = FRAGMENT_NEXT;
    } else if (code == STRTOU32("POWR")) {
        fragment = FRAGMENT_POWER;
    }
    if (!fragment)
        return -1;
    
    struct button * gpio_b = setupbutton(pin, button_press_cb, edge);
    button_ctrls[numberofbuttons].fragment = fragment;
    button_ctrls[numberofbuttons].waiting = false;
    button_ctrls[numberofbuttons].gpio_button = gpio_b;
    numberofbuttons++;
    loginfo("Button defined: Pin %d, Edge: %s, Fragment: \n%s",
            pin,
            ((edge != INT_EDGE_FALLING) && (edge != INT_EDGE_RISING)) ? "both" :
            (edge == INT_EDGE_FALLING) ? "falling" : "rising",
            fragment);
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
            loginfo("Button pressed: Pin %d", button_ctrls[cnt].gpio_button->pin);
            send_command(server, button_ctrls[cnt].fragment);
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
    char * fragment = FRAGMENT_VOLUME;
    /*if (strlen(cmd) > 4)
        return -1;
    
    //
    //  Select fragment for parameter
    //  Would love to "switch" here but that's not portable...
    //
    uint32_t code = STRTOU32(cmd);
    if (code == STRTOU32("VOLU")) {
        fragment = FRAGMENT_VOLUME;
    }*/
    
    struct encoder * gpio_e = setupencoder(pin1, pin2, encoder_rotate_cb, edge);
    encoder_ctrls[numberofencoders].fragment = fragment;
    encoder_ctrls[numberofencoders].gpio_encoder = gpio_e;
    encoder_ctrls[numberofencoders].last_value = 0;
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
    //  chatter filter 200ms - disabled...
    //  We poll every 100ms anyway plus wait for network action to complete
    //
    /*static unsigned long lasttimeVol = 0;
    struct timespec thistime;
     
    clock_gettime(0, &thistime);
    unsigned long time = thistime.tv_sec * 10 + thistime.tv_nsec / (1e8);
    if (lasttimeVol - time < 2) {
        return;
    }*/
    
    //logdebug("Polling encoders");
    for (int cnt = 0; cnt < numberofencoders; cnt++) {
        //
        //  build volume delta
        //  ignore if > 100: overflow
        //
        int delta = (int)(encoder_ctrls[cnt].gpio_encoder->value - encoder_ctrls[cnt].last_value);
        if (delta > 100)
            delta = 0;  //
        if (delta != 0) {
            logdebug("Encoder on GPIO %d, %d value change: %d",
                    encoder_ctrls[cnt].gpio_encoder->pin_a,
                    encoder_ctrls[cnt].gpio_encoder->pin_b,
                    delta);

            char fragment[50];
            char * prefix = (delta > 0) ? "+" : "-";
            snprintf(fragment, sizeof(fragment),
                     encoder_ctrls[cnt].fragment, prefix, abs(delta));
            
            if (send_command(server, fragment)) {
                encoder_ctrls[cnt].last_value = encoder_ctrls[cnt].gpio_encoder->value;
                //lasttimeVol = time; // chatter filter
            }
        }
    }
}





