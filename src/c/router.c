/*
 *  chirouter - A simple, testable IP router
 *
 *  This module contains the actual functionality of the router.
 *  When a router receives an Ethernet frame, it is handled by
 *  the chirouter_process_ethernet_frame() function.
 *
 */

/*
 * This project is based on the Simple Router assignment included in the
 * Mininet project (https://github.com/mininet/mininet/wiki/Simple-Router) which,
 * in turn, is based on a programming assignment developed at Stanford
 * (http://www.scs.stanford.edu/09au-cs144/lab/router.html)
 *
 * While most of the code for chirouter has been written from scratch, some
 * of the original Stanford code is still present in some places and, whenever
 * possible, we have tried to provide the exact attribution for such code.
 * Any omissions are not intentional and will be gladly corrected if
 * you contact us at borja@cs.uchicago.edu
 */

/*
 *  Copyright (c) 2016-2018, The University of Chicago
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *
 *  - Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 *  - Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 *  - Neither the name of The University of Chicago nor the names of its
 *    contributors may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <stdio.h>
#include <assert.h>

#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#include "chirouter.h"
#include "arp.h"
#include "utils.h"
#include "utlist.h"

/* Helper function to get the correct forward IP destination.
 * If there routing entry for given destination IP has a non-zero gateway then
 * return gateway's IP address, else return original destination IP.
 * @Params: pointer to routing entry, destination IP
 */
uint32_t get_forward_ip (chirouter_rtable_entry_t *routing_entry, 
                            uint32_t dst_ip) 
{
    uint32_t gateway = in_addr_to_uint32(routing_entry->gw);
    if (gateway != 0)
    {
        return gateway;
    }
    return dst_ip;
}

/* Helper function to get appropriate routing entry for ethernet frame
 * with longest-prefix matching.
 * @Params: pointer to router's context struct, pointer to ethernet frame
 * Return: routing entry corresponding to the dst ip of the frame
 */
chirouter_rtable_entry_t* chirouter_get_matching_entry(chirouter_ctx_t *ctx,
                                                    ethernet_frame_t *frame)
{
    iphdr_t *ip_hdr = (iphdr_t *)(frame->raw + sizeof(ethhdr_t));
    chirouter_rtable_entry_t *result = NULL;
    
    for (int i = 0; i < ctx->num_rtable_entries; i++)
    {
        /* Loop through each entry in router's routing table */
        uint32_t entry_mask = in_addr_to_uint32(ctx->routing_table[i].mask);
        uint32_t entry_dst = in_addr_to_uint32(ctx->routing_table[i].dest);
        if ((ip_hdr->dst & entry_mask) == entry_dst)
        {
            // Found matching entry
            if (result != NULL)
            {
                uint32_t current_mask = in_addr_to_uint32(result->mask);
                if (current_mask < entry_mask)
                {
                    // Longest-prefix matching
                    result = &ctx->routing_table[i];
                }
            }
            else
            {
                result = &ctx->routing_table[i];
            }
        }
    }

    return result;
}

/* Helper function to forward IP datagram
 * @Params: pointer to chirouter_ctx_t, pointer to ethernet_frame_t, 
 * destination MAC address
 * Return nothing
 */
void forward_ip_datagram(chirouter_ctx_t *ctx, ethernet_frame_t *frame, uint8_t *dst_mac)
{
    // From original frame
    ethhdr_t *frame_ethhdr = (ethhdr_t *)frame->raw;
    iphdr_t *frame_iphdr = (iphdr_t *)(frame->raw + sizeof(ethhdr_t));
    // Routing entry based on frame
    chirouter_rtable_entry_t *rentry = chirouter_get_matching_entry(ctx, frame);

    /* Construct new frame */
    int msg_len = frame->length;
    uint8_t msg[msg_len];
    memset(msg, 0, msg_len);

    /* Ethernet header */
    ethhdr_t *ether_hdr = (ethhdr_t *) msg;
    memcpy(ether_hdr->dst, dst_mac, ETHER_ADDR_LEN);
    memcpy(ether_hdr->src, rentry->interface->mac, ETHER_ADDR_LEN);
    ether_hdr->type = htons(ETHERTYPE_IP);

    /* IP header */
    iphdr_t *ip_hdr = (iphdr_t *)(msg + sizeof(ethhdr_t));
    // Copy frame's ip header over
    memcpy(ip_hdr, frame_iphdr, ntohs(frame_iphdr->len));
    // Update TTL and checksum
    ip_hdr->ttl = frame_iphdr->ttl - 1;
    ip_hdr->cksum = htons(0);
    ip_hdr->cksum = cksum(ip_hdr, sizeof(iphdr_t));

    // Forward newly constructed IP datagram
    chirouter_send_frame(ctx, rentry->interface, msg, msg_len);
    return;
}

/* Helper function to check if there is an interface in the router that matches
 * frame's IP destination.
 * @Params: pointer to chirouter_ctx_t, pointer to ethernet_frame_t
 * Return true if there's a match, false otherwise
 */
bool chirouter_find_match_router(chirouter_ctx_t *ctx, ethernet_frame_t *frame)
{
    iphdr_t* ip_hdr = (iphdr_t*) (frame->raw + sizeof(ethhdr_t));
    chirouter_interface_t *interface;
    for (int i = 0; i < ctx->num_interfaces; i++)
    {
        // loop through each interface of router to see if
        // its ip address matches dst ip address of the frame
        interface = &ctx->interfaces[i];
        if (in_addr_to_uint32(interface->ip) == ip_hdr->dst)
        {
            return true;
        }
    }
    return false;
}

/* Helper function to create and send an ICMP message
 * @Params: pointer to router's context struct, ICMP type, ICMP code, pointer
 * to ethernet frame that triggers the icmp message
 * Return nothing
 */
void chirouter_send_icmp(chirouter_ctx_t *ctx, uint8_t type, 
                                        uint8_t code, ethernet_frame_t *frame)
{
    // From original frame
    ethhdr_t *frame_ethhdr = (ethhdr_t *)frame->raw;
    iphdr_t *frame_iphdr = (iphdr_t *)(frame->raw + sizeof(ethhdr_t));
    icmp_packet_t* icmp = (icmp_packet_t*) (frame->raw + sizeof(ethhdr_t) + sizeof(iphdr_t));

    // Setting ICMP message's payload length
    int payload_len;
    if (type == ICMPTYPE_ECHO_REPLY || type == ICMPTYPE_ECHO_REQUEST)
    {
        payload_len = ntohs(frame_iphdr->len) - sizeof(iphdr_t) 
                                                    - ICMP_HDR_SIZE;
    }
    else
    {
        payload_len = sizeof(iphdr_t) + 8;
    }

    /* Constructing new frame for ICMP message */
    int reply_len = sizeof(ethhdr_t) + sizeof(iphdr_t) + ICMP_HDR_SIZE + payload_len;
    uint8_t reply[reply_len];
    memset(reply, 0, reply_len);

    /* Extracting new frame's ethernet header, IP header, and ICMP packet */
    ethhdr_t *reply_ether_hdr = (ethhdr_t *)reply;
    iphdr_t *reply_ip_hdr = (iphdr_t *)(reply + sizeof(ethhdr_t));
    icmp_packet_t *reply_icmp = (icmp_packet_t *)(reply + sizeof(ethhdr_t) + sizeof(iphdr_t));

    /* Set appropriate headers */
    // Ethernet header
    memcpy(reply_ether_hdr->dst, frame_ethhdr->src, ETHER_ADDR_LEN);
    memcpy(reply_ether_hdr->src, frame->in_interface->mac, ETHER_ADDR_LEN);
    reply_ether_hdr->type = htons(ETHERTYPE_IP);

    // IP header
    reply_ip_hdr->version = 4;
    reply_ip_hdr->tos = 0;
    reply_ip_hdr->proto = 1;
    reply_ip_hdr->dst = frame_iphdr->src;
    reply_ip_hdr->src = in_addr_to_uint32(frame->in_interface->ip);
    reply_ip_hdr->cksum = htons(0);
    reply_ip_hdr->id = htons(0);
    reply_ip_hdr->off = htons(0);
    reply_ip_hdr->ihl = 5;
    if (type == ICMPTYPE_ECHO_REPLY || type == ICMPTYPE_ECHO_REQUEST)
    {
        reply_ip_hdr->len = htons(frame->length - sizeof(ethhdr_t));
    }
    else 
    {
        reply_ip_hdr->len = htons(sizeof(iphdr_t) + ICMP_HDR_SIZE + payload_len);
    }
    reply_ip_hdr->ttl = 64;
    reply_ip_hdr->cksum = cksum(reply_ip_hdr, sizeof(iphdr_t));

    // ICMP packet
    reply_icmp->type = type;
    reply_icmp->code = code;
    reply_icmp->chksum = 0;
    if (type == ICMPTYPE_ECHO_REQUEST || type == ICMPTYPE_ECHO_REPLY)
    {
        // echo
        if (code == 0)
        {
            reply_icmp->echo.identifier = icmp->echo.identifier;
            reply_icmp->echo.seq_num = icmp->echo.seq_num;
            memcpy(reply_icmp->echo.payload, icmp->echo.payload, payload_len);
        }
    }
    else if (type == ICMPTYPE_DEST_UNREACHABLE)
    {
        // dest_unreachable
        memcpy(reply_icmp->dest_unreachable.payload, frame_iphdr, payload_len);
    }
    else
    {
        // time_exceeded
        memcpy(reply_icmp->time_exceeded.payload, frame_iphdr, payload_len);
    }
    reply_icmp->chksum = cksum(reply_icmp, ICMP_HDR_SIZE + payload_len);

    // Send ICMP message
    chirouter_send_frame(ctx, frame->in_interface, reply, reply_len);
    return;
}

/*
 * chirouter_process_ethernet_frame - Process a single inbound Ethernet frame
 *
 * This function will get called every time an Ethernet frame is received by
 * a router. This function receives the router context for the router that
 * received the frame, and the inbound frame (the ethernet_frame_t struct
 * contains a pointer to the interface where the frame was received).
 * Take into account that the chirouter code will free the frame after this
 * function returns so, if you need to persist a frame (e.g., because you're
 * adding it to a list of withheld frames in the pending ARP request list)
 * you must make a deep copy of the frame.
 *
 * chirouter can manage multiple routers at once, but does so in a single
 * thread. i.e., it is guaranteed that this function is always called
 * sequentially, and that there will not be concurrent calls to this
 * function. If two routers receive Ethernet frames "at the same time",
 * they will be ordered arbitrarily and processed sequentially, not
 * concurrently (and with each call receiving a different router context)
 *
 * ctx: Router context
 *
 * frame: Inbound Ethernet frame
 *
 * Returns:
 *   0 on success,
 *
 *   1 if a non-critical error happens
 *
 *   -1 if a critical error happens
 *
 *   Note: In the event of a critical error, the entire router will shut down and exit.
 *         You should only return -1 for issues that would prevent the router from
 *         continuing to run normally. Return 1 to indicate that the frame could
 *         not be processed, but that subsequent frames can continue to be processed.
 */
int chirouter_process_ethernet_frame(chirouter_ctx_t *ctx, ethernet_frame_t *frame)
{
    /* Your code goes here */

    /* Accessing the Ethernet header */
    ethhdr_t* hdr = (ethhdr_t*) frame->raw;

    /* Accessing the IP header */
    iphdr_t* ip_hdr = (iphdr_t*) (frame->raw + sizeof(ethhdr_t));
    uint16_t hdr_type = ntohs(hdr->type);
    if ((hdr_type == ETHERTYPE_IP) || (hdr_type == ETHERTYPE_IPV6))
    {
        chilog(DEBUG, "[ETHERNET TYPE]: IP DATAGRAM");
        if (ip_hdr->dst == in_addr_to_uint32(frame->in_interface->ip))
        {
            chilog(DEBUG, "[FIRST CASE]: FRAME COMES TO THE ROUTER");
            if ((ip_hdr->proto == IPPROTO_TCP) || 
                                            (ip_hdr->proto == IPPROTO_UDP))
            {
                // ICMP DESTINATION PORT UNREACHABLE
                chilog(DEBUG, "[TCP/UDP PROTOCOL TYPE]");
                chirouter_send_icmp(ctx, ICMPTYPE_DEST_UNREACHABLE, 
                                    ICMPCODE_DEST_PORT_UNREACHABLE, frame);
            }
            else if (ip_hdr->ttl == 1)
            {
                // ICMP time exceeded
                chilog(DEBUG, "[TIME EXCEEDED TTL = 1]");
                chirouter_send_icmp(ctx, ICMPTYPE_TIME_EXCEEDED, 0, frame);
            }
            else if (ip_hdr->proto == IPPROTO_ICMP)
            {
                /* Accessing an ICMP message */
                chilog(DEBUG, "[ICMP MESSAGE]");
                icmp_packet_t* icmp = (icmp_packet_t*) (frame->raw + sizeof(ethhdr_t) + sizeof(iphdr_t));
                if (icmp->type == ICMPTYPE_ECHO_REQUEST)
                {
                    // ICMPTYPE_ECHO_REPLY
                    chilog(DEBUG, "[ICMP] SEND ECHO REPLIES");
                    chirouter_send_icmp(ctx, ICMPTYPE_ECHO_REPLY, 0, frame);
                }
            }
            else 
            {
                // ICMP destination protocol unreachable
                chilog(DEBUG, "[DEST UNREACHABLE]");
                chirouter_send_icmp(ctx, ICMPTYPE_DEST_UNREACHABLE, 
                                    ICMPCODE_DEST_PROTOCOL_UNREACHABLE, frame);
            }
        }
        else if (chirouter_find_match_router(ctx, frame))
        {
            chilog(DEBUG, "[SECOND CASE]: FRAME COMES TO OTHER INTERFACES OF THE ROUTER");
            // ICMP HOST UNREACHABLE
            chirouter_send_icmp(ctx, ICMPTYPE_DEST_UNREACHABLE, 
                                        ICMPCODE_DEST_HOST_UNREACHABLE, frame);
        }
        else
        {
            chilog(DEBUG, "[THIRD CASE]: TRY TO FORWARD DATAGRAM");
            chirouter_rtable_entry_t* forward_entry = chirouter_get_matching_entry(ctx, frame);
            if (forward_entry != NULL)
            {
                chilog(DEBUG, "[IP FORWARDING]: ROUTING ENTRY FOUND");
                uint32_t forward_ip = get_forward_ip(forward_entry, ip_hdr->dst);
                pthread_mutex_lock(&(ctx->lock_arp));
                chirouter_arpcache_entry_t* arpcache_entry = chirouter_arp_cache_lookup(ctx, uint32_to_in_addr(forward_ip));
                pthread_mutex_unlock(&(ctx->lock_arp));
                if (arpcache_entry == NULL)
                {
                    chilog(DEBUG, "[IP FORWARDING]: ARP CACHE ENTRY NOT FOUND");
                    pthread_mutex_lock(&(ctx->lock_arp));
                    chirouter_pending_arp_req_t* pending_req = chirouter_arp_pending_req_lookup(ctx, uint32_to_in_addr(forward_ip));
                    pthread_mutex_unlock(&(ctx->lock_arp));
                    if (pending_req == NULL)
                    {
                        
                        chilog(DEBUG, "[IP FORWARDING]: NOT IN PENDING REQUEST LIST");
                        pthread_mutex_lock(&(ctx->lock_arp));
                        chilog(DEBUG, "[ARP MESSAGE]: SEND ARP REQUEST");
                        chirouter_send_arp_message(ctx, 
                                                    forward_entry->interface, 
                                                    NULL, forward_ip, 
                                                    ARP_OP_REQUEST);
                        // add IP address to pending arp request list
                        pending_req = chirouter_arp_pending_req_add(ctx, 
                                                uint32_to_in_addr(forward_ip), 
                                                forward_entry->interface);
                        pending_req->times_sent++;
                        pending_req->last_sent = time(NULL);
                        // add frame to the newly created pending arp request item
                        int result = chirouter_arp_pending_req_add_frame(ctx, 
                                                        pending_req, frame);
                        if (result == 1) {
                            /* An error occurred when adding withheld frames */
                            return -1;
                        }
                        pthread_mutex_unlock(&(ctx->lock_arp));
                    }
                    else
                    {
                        chilog(DEBUG, "[IP FORWARDING]: ALREADY IN PENDING REQUEST LIST");
                        pthread_mutex_lock(&(ctx->lock_arp));
                        // add frame to the already created pending arp request item
                        int result = chirouter_arp_pending_req_add_frame(ctx, 
                                                        pending_req, frame);
                        if (result == 1)
                        {
                            /* An error occurred when adding withheld frames */
                            return -1;
                        }
                        pthread_mutex_unlock(&(ctx->lock_arp));
                    }
                }
                else
                {
                    chilog(DEBUG, "[IP FORWARDING]: ARP CACHE ENTRY FOUND");
                    if (ip_hdr->ttl == 1)
                    {
                        // TIME_EXCEEDED
                        chirouter_send_icmp(ctx, 
                                            ICMPTYPE_TIME_EXCEEDED, 
                                            0, frame);
                    }
                    else
                    {
                        // Forward IP datagram
                        forward_ip_datagram(ctx, frame, arpcache_entry->mac);
                    }
                }
            }
            else 
            {
                chilog(DEBUG, "[IP FORWARDING]: ROUTING ENTRY NOT FOUND");
                // ICMP network unreachable
                chirouter_send_icmp(ctx, ICMPTYPE_DEST_UNREACHABLE, 
                                    ICMPCODE_DEST_NET_UNREACHABLE, frame);
            }
        }
        return 0;
    }
    else if (hdr_type == ETHERTYPE_ARP) 
    {
        /* Accessing an ARP message */
        chilog(DEBUG, "[ETHERNET TYPE]: ARP MESSAGES");
        arp_packet_t* arp = (arp_packet_t*) (frame->raw + sizeof(ethhdr_t));
        if (arp->tpa == in_addr_to_uint32(frame->in_interface->ip))
        {
            chilog(DEBUG, "[ARP MESSAGE]: IT'S FOR ME");
            if (ntohs(arp->op) == ARP_OP_REPLY)
            {
                chilog(DEBUG, "[ARP MESSAGE]: ARP REPLY");
                pthread_mutex_lock(&(ctx->lock_arp));
                // add ip and corresponding mac address to arp cache
                int result = chirouter_arp_cache_add(ctx, 
                                                uint32_to_in_addr(arp->spa),
                                                arp->sha); 
                pthread_mutex_unlock(&(ctx->lock_arp));
                if (result != 0)
                {
                    /* An error occurred when adding to ARP cache */
                    return -1;
                }
                // forward withheld frames - decrement TTL - checksum
                pthread_mutex_lock(&(ctx->lock_arp));
                chirouter_pending_arp_req_t *arp_req = chirouter_arp_pending_req_lookup(ctx, uint32_to_in_addr(arp->spa));
                if (arp_req == NULL)
                {
                    chilog(DEBUG, "[ARP MESSAGE]: NO PENDING ARP FOUND");
                }
                else
                {
                    chilog(DEBUG, "[ARP MESSAGE] PENDING ARP FOUND");
                    withheld_frame_t *elt;
                    DL_FOREACH(arp_req->withheld_frames, elt)
                    {
                        // Forward IP datagram
                        if (elt != NULL)
                        {
                            iphdr_t *ip_hdr = (iphdr_t *)(elt->frame->raw + sizeof(ethhdr_t));
                            if (ip_hdr->ttl == 1) 
                            {
                                // Time exceeded
                                chirouter_send_icmp(ctx, 
                                                    ICMPTYPE_TIME_EXCEEDED,
                                                    0, elt->frame);
                            }
                            else
                            {
                                // Forward withheld frame
                                forward_ip_datagram(ctx, elt->frame, arp->sha);
                            }
                            
                        }
                    }
                    // Free withheld frames
                    int result = chirouter_arp_pending_req_free_frames(arp_req);
                    if (result == 1) {
                        /* An error occurred */
                        return result;
                    }
                    // remove the pending ARP request from the pending ARP request list
                    DL_DELETE(ctx->pending_arp_reqs, arp_req);
                    free(arp_req);
                }
                pthread_mutex_unlock(&(ctx->lock_arp));
                
            } 
            else if (ntohs(arp->op) == ARP_OP_REQUEST)
            {
                // send arp reply
                chilog(DEBUG, "[ARP MESSAGE]: ARP REQUEST");
                chirouter_send_arp_message(ctx, frame->in_interface, 
                                    arp->sha, arp->spa,
                                    ARP_OP_REPLY);
            }
            else
            {
                chilog(DEBUG, "[ARP MESSAGE]: ARP CODE NOT VALID");
            }
        }
        else
        {
            chilog(DEBUG, "[ARP MESSAGE]: IT'S NOT FOR ME");
            return 0;
        }
        return 0;
    }
}


