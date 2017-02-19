//
//  discovery.c
//  SqueezeButtonPi
//
//  Find server connection
//  Identify the address of the server the player is talking to
//  Find the communication port
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

#include "discovery.h"
#include "sbpd.h"

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/param.h>
#include <sys/ioctl.h>
#include <net/if.h>


//
// Prototypes
//
void update_server(sbpd_config_parameters_t * discovered,
                   struct sbpd_server * server);
void update_port();
void _write_server_string(struct sbpd_server * server, in_addr_t s_addr);
bool get_serverIPv4(uint32_t *ip);
void send_discovery(uint32_t address);
uint32_t read_discovery(uint32_t address);

bool get_mac(uint8_t mac[]);


#define IP_SEARCH_TIMEOUT 3 // every 3 s

//
//  Polling function for server discovery
//  Call from main loop
//  Handles internal scheduling between different
//

//
// counter for search scheduling
//
static uint32_t search_timer = 0;
//
//  Helper variable; don't want to convert back and forth between string and net-addr
//
static in_addr_t foundAddr = 0;
//
//  Parameters:
//  config: defines which parameters are preconfigured and will not be discovered
//  discovered: the discovered parameters
//  server: server configuration
//
void poll_discovery(sbpd_config_parameters_t config,
                    sbpd_config_parameters_t *discovered,
                    struct sbpd_server * server) {
    //logdebug("Polling server discovery");
    //
    // search for server
    //
    if (!(config & SBPD_cfg_host)) {
        if (!search_timer--) {
            search_timer = IP_SEARCH_TIMEOUT * SCD_SECOND / SCD_SLEEP_TIMEOUT;
            in_addr_t addr = 0;
            if (server->host)
                addr = inet_addr(server->host);
            bool change = get_serverIPv4(&addr);
            logdebug("New or changed server address %s", (change) ? "found" : "not found");
            if (change) {
                //
                // found server but not port
                //
                *discovered |= SBPD_cfg_host;
                *discovered &= ~SBPD_cfg_port;
                foundAddr = addr;
                
                // we don't update server struct, yet, if we also look for the port.
                if (config & SBPD_cfg_port)
                    _write_server_string(server, addr);
                // otherwise: look for port
                else
                    send_discovery(addr);
            }
        }
    }
    //
    // only search if configured to do so and port is not yet found
    //
    if (!(config & SBPD_cfg_port) &&
        !(*discovered & SBPD_cfg_port)) {
        logdebug("Looking for port");
        uint32_t foundPort = read_discovery(foundAddr);
        if (foundPort) {
            loginfo("Squeezebox control port found: %d", foundPort);
            if (!(config & SBPD_cfg_host))
                _write_server_string(server, foundAddr);
            server->port = foundPort;
            *discovered |= SBPD_cfg_port;
        }

    }
}

//
//  Helper function to convert server address to string
//
void _write_server_string(struct sbpd_server * server, in_addr_t s_addr) {
    struct in_addr addr;
    addr.s_addr = s_addr;
    static char foundServer[16]; // only one server, so we can do this statically
    
    char * aAddr = inet_ntoa(addr);
    loginfo("Server address found: %s", aAddr);
    strncpy(foundServer, aAddr, 16);
    server->host = foundServer;
}

//
//  Define TCP states here to not have to include kernel header
//
enum {
    TCP_ESTABLISHED = 1,
    TCP_SYN_SENT,
    TCP_SYN_RECV,
    TCP_FIN_WAIT1,
    TCP_FIN_WAIT2,
    TCP_TIME_WAIT,
    TCP_CLOSE,
    TCP_CLOSE_WAIT,
    TCP_LAST_ACK,
    TCP_LISTEN,
    TCP_CLOSING,    // Now a valid state
    
    TCP_MAX_STATES  // Leave at the end!
};
//
//
// Get server IP from /proc/net/tcp
//
// returns true if server IP was found and changed
//
//
bool get_serverIPv4(uint32_t *ip) {
    uint32_t foundIp;
    FILE * procTcp = fopen("/proc/net/tcp", "r");
    if (!procTcp)
        return false;
    char line[256];
    if (!fgets(line, 255, procTcp)) {
        fclose(procTcp);
        return false;
    }
    bool found = false;
    while (fgets(line, 255, procTcp)) {
        logdebug("/proc/net/tcp line: %s", line);
        strtok(line, " "); // line number
        strtok(NULL, " "); // source address
        char * target = strtok(NULL, " "); // target address
        char * socketState = strtok(NULL, " "); // socket state
        logdebug("target: %s\n", target);
        if (!target) {
            logwarn("no tcp target found");
            fclose(procTcp);
            return false;
        }
        char * ipString = strtok(target, ":");
        char * portString = strtok(NULL, ":");
        if (!ipString || !portString || !socketState) {
            if (!ipString)
                logwarn("no ipString found");
            if (!portString)
                logwarn("no portString found");
            if (!socketState)
                logwarn("no socketState found");
            fclose(procTcp);
            return false;
        }
        char * portComp = "0D9B";
        //
        // just be sure the port is upper case
        //
        char * sTemp = portString;
        while ((*sTemp = toupper(*sTemp)))
            sTemp++;
        //
        //  state
        //
        unsigned long uSocketState = strtol(socketState, NULL, 16);
        //
        // port 3483 and socket state == TCP_ESTABLISHED?
        //
        if ((*((uint32_t *)portComp) == *((uint32_t *)portString)) &&
            (uSocketState == TCP_ESTABLISHED)) {
            foundIp = (uint32_t)strtoul(ipString, NULL, 16);
            if (foundIp != *ip) {
                if (!found) // only change once. The rest is for logging only
                    *ip = foundIp;
                loginfo("Found server %s. A new address", ipString);
                // no logging: we're done
                if (loglevel() < LOG_NOTICE) {
                    fclose(procTcp);
                    return true;
                }
                found = true;
            }
            loginfo("Found server %s. Same as before");
            // no logging? we're done
            if (loglevel() < LOG_NOTICE) {
                fclose(procTcp);
                return false;
            }
        }
    }
    fclose(procTcp);
    return found;
}

static int udpSocket = 0;
static uint32_t udpAddress;
# define SIZE_SERVER_DISCOVERY_LONG 23
# define SBS_UDP_PORT 3483

//
// get port through server discovery
//

//
// send server discovery
//
void send_discovery(uint32_t address) {
    if (udpSocket)
        close(udpSocket);
    // create discovery socket
    udpSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    
    int yes = 1;
    setsockopt(udpSocket, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(int));
    setsockopt(udpSocket, SOL_SOCKET, SO_REUSEADDR, (void *)&yes, sizeof(yes));
    
    // send packet
    udpAddress = address;
    struct sockaddr_in addr4;
    memset(&addr4, 0, sizeof(addr4));
    addr4.sin_family = AF_INET;
    addr4.sin_port = htons(SBS_UDP_PORT);
    addr4.sin_addr.s_addr = address;
    
    char * data = "eIPAD\0NAME\0JSON\0UUID\0\0\0";
    
    ssize_t error;
    error = sendto(udpSocket, data, SIZE_SERVER_DISCOVERY_LONG, 0, (struct sockaddr*)&addr4, sizeof(addr4));
}


#define BUFSIZE 1600
//
// poll udp port for discovery reply
//
uint32_t read_discovery(uint32_t address) {
    char buffer[BUFSIZE];
    static struct sockaddr_in returnAddr;
    memset(&returnAddr, 0, sizeof(returnAddr));
    returnAddr.sin_family = AF_INET;
    //returnAddr.sin_port = htons(SBS_UDP_PORT);
    returnAddr.sin_addr.s_addr = address;
    //returnAddr.sin_len = sizeof(returnAddr);
    socklen_t addrSize = sizeof(returnAddr);
    
    ssize_t size = recvfrom(udpSocket,
                            (void *)buffer,
                            sizeof(buffer),
                            MSG_DONTWAIT,
                            (struct sockaddr *)&returnAddr,
                            &addrSize);
    
    if ((size <= 0) ||
        (buffer[0] != 'E')) {
        logdebug("Server discovery: no reply, yet");
        return 0;
    }
    
    logdebug("Server discovery: packet found");
    unsigned int pos = 1;
    char code[5];
    code[4] = 0;
    // this is the whole packet interpretation for debug purposes
    // not needed in final code
    char port[6];
    strncpy(port, "9000\0", 6);
    if (loglevel() >= LOG_NOTICE) {
        char name[256];
        memset(name, 0, sizeof(name));
        char UUID[256];
        memset(UUID, 0, sizeof(UUID));
        while (pos < (size - 5)) {
            unsigned int fieldLen = buffer[pos + 4];
            uint32_t * selector = (uint32_t *)(buffer + pos);
            if (*selector == STRTOU32("NAME")) {
                strncpy(name, buffer + pos + 5, MIN(fieldLen, sizeof(name)));
            } else if (*selector == STRTOU32("JSON")) {
                strncpy(port, buffer + pos + 5, MIN(fieldLen, sizeof(port)));
            } else if (*selector == STRTOU32("UUID")) {
                strncpy(UUID, buffer + pos + 5, MIN(fieldLen, sizeof(UUID)));
            }
            pos += fieldLen + 5;
        }
        loginfo("discovery packet: port: %s, name: %s, uuid: %s", port, name, UUID);
    } else {
        while (pos < (size - 5)) {
            unsigned int fieldLen = buffer[pos + 4];
            uint32_t * selector = (uint32_t *)(buffer + pos);
            if (*selector == STRTOU32("JSON")) {
                strncpy(port, buffer + pos + 5, MIN(fieldLen, sizeof(port)));
            }
            pos += fieldLen + 5;
        }
        loginfo("discovery packet: port: %s", port);
    }
    close(udpSocket);
    
    return (uint32_t)strtoul(port, NULL, 10);
}


//
// MAC address search
//
// mac address. From SqueezeLite so should match that behaviour.
// search first 4 interfaces returned by IFCONF
//
//  Parameters:
//  config: defines which parameters are preconfigured and will not be discovered
//  returns: MAC string
//
char * find_mac() {
    // Find MAC
    uint8_t mac[6];
    bool ret = get_mac(mac);
    if (!ret)
        return NULL;
    static char macBuf[18];
    sprintf(macBuf, "%x:%x:%x:%x:%x:%x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    loginfo("MAC address found: %s", macBuf);
    return macBuf;
}


//
// Actualy MAC address search
//
// mac address. From SqueezeLite so should match that behaviour.
// search first 4 interfaces returned by IFCONF
//
// Returns 6 bytes MAC.
//
bool get_mac(uint8_t mac[]) {
#ifdef __unix__ // just to silence errors on Mac while developing
    char *utmac;
    struct ifconf ifc;
    struct ifreq *ifr, *ifend;
    struct ifreq ifreq;
    struct ifreq ifs[4];
    
    utmac = getenv("UTMAC");
    if (utmac)
    {
        if ( strlen(utmac) == 17 )
        {
            if (sscanf(utmac,"%2hhx:%2hhx:%2hhx:%2hhx:%2hhx:%2hhx",
                       &mac[0],&mac[1],&mac[2],&mac[3],&mac[4],&mac[5]) == 6)
            {
                return true;
            }
        }
        
    }
    
    mac[0] = mac[1] = mac[2] = mac[3] = mac[4] = mac[5] = 0;
    
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    
    ifc.ifc_len = sizeof(ifs);
    ifc.ifc_req = ifs;
    
    if (ioctl(s, SIOCGIFCONF, &ifc) == 0) {
        ifend = ifs + (ifc.ifc_len / sizeof(struct ifreq));
        
        for (ifr = ifc.ifc_req; ifr < ifend; ifr++) {
            if (ifr->ifr_addr.sa_family == AF_INET) {
                
                strncpy(ifreq.ifr_name, ifr->ifr_name, sizeof(ifreq.ifr_name));
                if (ioctl (s, SIOCGIFHWADDR, &ifreq) == 0) {
                    memcpy(mac, ifreq.ifr_hwaddr.sa_data, 6);
                    if (mac[0]+mac[1]+mac[2] != 0) {
                        close(s);
                        return true;
                    }
                }
            }
        }
    }
    
    close(s);
#endif
    return false;
}




