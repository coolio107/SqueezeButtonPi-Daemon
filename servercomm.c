//
//  servercomm.c
//  SqueezeButtonPi
//
//  Send CLI commands to the server
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

#include "servercomm.h"
#include "sbpd.h"
#include <curl/curl.h>

//
// lock for asynchronous sending of commands - we don't do this right now
//
/*#include <pthread.h>

static volatile bool commLock;
static pthread_mutex_t lock;*/

static CURL *curl;
static char * MAC = NULL;
static struct curl_slist * headerList = NULL;

#define JSON_CALL_MASK	"{\"id\":%ld,\"method\":\"slim.request\",\"params\":[\"%s\",%s]}"
#define SERVER_ADDRESS_TEMPLATE "http://localhost/jsonrpc.js"

//
//
//  Send CLI command fragment to Logitech Media Server/Squeezebox Server
//  This command blocks (synchronously communicates".
//  In timing critical situations this should be called from a separate thread
//
//  Parameters:
//      server: the server information structure defining host, port etc.
//      frament: the command fragment to be sent as JSON array
//               e.g. "[\"mixer\”,\"volume\",\"+2\"]"
//               optionally: some CLI commands can take parameter hashes as "params:{}"
//  Returns: success flag
//
//
bool send_command(struct sbpd_server * server, char * fragment) {
    if (!curl)
        return false;
    
    //
    //  Sending commands asynchronously? Secure with a mutex
    //  But right now we are actually not doing that, so comment out
    //
    /*pthread_mutex_lock(&lock);
    if (commLock) {
        pthread_mutex_unlock(&lock);
        return false;
    }
    commLock = true;
    pthread_mutex_unlock(&lock);*/
    
    //
    //  target setup. We call an IPv4 ip so we need to replace a default host
    //
    struct curl_slist * targetList = NULL;
    curl_easy_setopt(curl, CURLOPT_URL, SERVER_ADDRESS_TEMPLATE);
    char target[100];
    snprintf(target, sizeof(target), "::%s:%d", server->host, server->port);
    //logdebug("Command Target: %s", target);
    targetList = curl_slist_append(targetList, target);
    curl_easy_setopt(curl, CURLOPT_CONNECT_TO, targetList);
    
    //
    //  username/password?
    //
    char secret[255];
    if (server->user && server->password) {
        snprintf(secret, sizeof(secret), "%s:%s", server->user, server->password);
        curl_easy_setopt(curl, CURLOPT_USERPWD, secret);
    }
    
    //
    //  setup payload (JSON/RPC CLI command) for POST command
    //
    char jsonFragment[256];
    snprintf(jsonFragment, sizeof(jsonFragment), JSON_CALL_MASK, 1l, MAC, fragment);
    logdebug("Server %s command: %s", target, jsonFragment);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, jsonFragment);
    if (headerList)
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerList);
    
    //
    //  Send command and clean up
    //  Note: one could retrieve a result here since all communication is synchronous!
    //
    CURLcode res = curl_easy_perform(curl);
    logdebug("Curl result: %d", res);
    curl_slist_free_all(targetList);
    targetList = NULL;
    
    //commLock = false;
    return true;
}

//
//  Curl reply callback
//  Replies from the server go here.
//  We could handle return values here but we just log.
//
size_t write_data(char *buffer, size_t size, size_t nmemb, void *userp) {
    if (size)
        logdebug("Server reply %s", buffer);
    return size * nmemb;
}

//
//
//  Initialize CURL for server communication and set MAC address
//
//
int init_comm(char * use_mac) {
    loginfo("Initializing CURL");
    MAC = use_mac;
    //
    //  Setup mutex lock for comm - unused
    //
    /*if (pthread_mutex_init(&lock, NULL) != 0) {
        logerr("comm lock mutex init failed");
        return -1;
    }*/
    
    //
    //  Initialize curl comm
    //
    curl_global_init(CURL_GLOBAL_ALL);
    curl = curl_easy_init();
    if (!curl) {
        curl_global_cleanup();
        return -1;
    }
    
    //
    //  Set verbose mode for communication debugging
    //
    if (loglevel() == LOG_DEBUG)
        curl_easy_setopt(curl, CURLOPT_VERBOSE, 1);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
    headerList = curl_slist_append(headerList, "Content-Type: application/json");
    char userAgent[50];
    snprintf(userAgent, sizeof(userAgent), "User-Agent: %s/%s)", USER_AGENT, VERSION);
    headerList = curl_slist_append(headerList, userAgent);
    //
    //  Add session-ID? Only needed for MySB which is not supported
    //
    //headerList = curl_slist_append(headerList, "x-sdi-squeezenetwork-session: ...")
    return 0;
}

//
//
//  Shutdown CURL
//
//
void shutdown_comm() {
    curl_slist_free_all(headerList);
    curl_easy_cleanup(curl);
    curl_global_cleanup();
}

