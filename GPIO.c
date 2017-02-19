//
//  GPIO.c
//  SqueezeButtonPi
//
//  Low-Level code to configure and read buttons and rotary encoders.
//  Calls callbacks on activity
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

#include "GPIO.h"
#include "sbpd.h"

#include <wiringPi.h>

//
//  Configured buttons
//
static int numberofbuttons = 0;
//
//  Pre-allocate encoder objects on the stack so we don't have to
//  worry about freeing them
//
static struct button buttons[max_buttons];

//
//
//  Button handler function
//  Called by the GPIO interrupt when a button is pressed or released
//  Depends on edge configuration.
//  Checks all configred buttons for status changes and
//  calls callback if state change detected.
//
//
void updateButtons()
{
    struct button *button = buttons;
    for (; button < buttons + numberofbuttons; button++)
    {
        bool bit = digitalRead(button->pin);
        
        int increment = 0;
        // same? no increment
        if (button->value != bit)
            increment = (bit) ? 1 : -1; // Increemnt and current state true: positive increment
        
        button->value = bit;
        
        if (button->callback)
            button->callback(button, increment);
    }
}

//
//
//  Configuration function to define a button
//  Should be run for every button you want to control
//  For each button a button struct will be created
//
//  Parameters:
//      pin: GPIO-Pin used in BCM numbering scheme
//      callback: callback function to be called when button state changed
//      edge: edge to be used for trigger events,
//            one of INT_EDGE_RISING, INT_EDGE_FALLING or INT_EDGE_BOTH (the default)
//  Returns: pointer to the new button structure
//           The pointer will be NULL is the function failed for any reason
//
//
struct button *setupbutton(int pin, button_callback_t callback, int edge)
{
    if (numberofbuttons > max_buttons)
    {
        logerr("Maximum number of buttons exceded: %i", max_buttons);
        return NULL;
    }
    
    if (edge != INT_EDGE_FALLING && edge != INT_EDGE_RISING)
        edge = INT_EDGE_BOTH;
    
    struct button *newbutton = buttons + numberofbuttons++;
    newbutton->pin = pin;
    newbutton->value = 0;
    newbutton->callback = callback;
    
    pinMode(pin, INPUT);
    pullUpDnControl(pin, PUD_UP);
    wiringPiISR(pin,edge, updateButtons);
    
    return newbutton;
}

//
//
// Encoders
// Rotary Encoder taken from https://github.com/astine/rotaryencoder
// http://theatticlight.net/posts/Reading-a-Rotary-Encoder-from-a-Raspberry-Pi/
//
//
//  Configured encoders
//
static int numberofencoders = 0;
//
//  Pre-allocate encoder objects on the stack so we don't have to
//  worry about freeing them
//
static struct encoder encoders[max_encoders];

//
//
//  Encoder handler function
//  Called by the GPIO interrupt when encoder is rotated
//  Depends on edge configuration
//
//
void updateEncoders()
{
    struct encoder *encoder = encoders;
    for (; encoder < encoders + numberofencoders; encoder++)
    {
        int MSB = digitalRead(encoder->pin_a);
        int LSB = digitalRead(encoder->pin_b);
        
        int encoded = (MSB << 1) | LSB;
        int sum = (encoder->lastEncoded << 2) | encoded;
        
        int increment = 0;
        
        if(sum == 0b1101 || sum == 0b0100 || sum == 0b0010 || sum == 0b1011) increment = 1;
        if(sum == 0b1110 || sum == 0b0111 || sum == 0b0001 || sum == 0b1000) increment = -1;
        
        encoder->value += increment;
        
        encoder->lastEncoded = encoded;
        if (encoder->callback)
            encoder->callback(encoder, increment);
    }
}

//
//
//  Configuration function to define a rotary encoder
//  Should be run for every rotary encoder you want to control
//  For each encoder a button struct will be created
//
//  Parameters:
//      pin_a, pin_b: GPIO-Pins used in BCM numbering scheme
//      callback: callback function to be called when encoder state changed
//      edge: edge to be used for trigger events,
//            one of INT_EDGE_RISING, INT_EDGE_FALLING or INT_EDGE_BOTH (the default)
//  Returns: pointer to the new encoder structure
//           The pointer will be NULL is the function failed for any reason
//
//
struct encoder *setupencoder(int pin_a,
                             int pin_b,
                             rotaryencoder_callback_t callback,
                             int edge)
{
    if (numberofencoders > max_encoders)
    {
        logerr("Maximum number of encodered exceded: %i", max_encoders);
        return NULL;
    }
    
    if (edge != INT_EDGE_FALLING && edge != INT_EDGE_RISING)
        edge = INT_EDGE_BOTH;
    
    struct encoder *newencoder = encoders + numberofencoders++;
    newencoder->pin_a = pin_a;
    newencoder->pin_b = pin_b;
    newencoder->value = 0;
    newencoder->lastEncoded = 0;
    newencoder->callback = callback;
    
    pinMode(pin_a, INPUT);
    pinMode(pin_b, INPUT);
    pullUpDnControl(pin_a, PUD_UP);
    pullUpDnControl(pin_b, PUD_UP);
    wiringPiISR(pin_a,edge, updateEncoders);
    wiringPiISR(pin_b,edge, updateEncoders);
    
    return newencoder;
}

//
//
//  Init GPIO functionality
//  Essentially just initialized WiringPi to use GPIO pin numbering
//
//
void init_GPIO() {
    loginfo("Initializing GPIO");
    wiringPiSetupGpio() ;
}






