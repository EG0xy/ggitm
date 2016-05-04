
#include "ggitm.h"
#define _GNU_SOURCE
void http_dump(struct PKT *httppacket) {
        int i = 0;
        printf("\n--------------------------------------------\n");

        for (i; i < httppacket->datalen; i++) {
                printf("%c", httppacket->data[i]);
        }
        printf("\n--------------------------------------------\n");

}

int get_http_host(uint8_t * data, char *buf, int bufsz) {
        char *tmp,
        *tmp2,
         c;
        int n = 0,
            i = 0;
        char *chardata = (char *)data;
        char *s = strcasestr(chardata, "host:");

        if (s == NULL || (strlen(s) < 6))
                return 0;
        else {
                s = &s[6];
                for (i; i < bufsz; i++) {
                        c = s[i];
                        if (c == ':' || c == '\r' || c == '\n' || c == '\0' || (!isprint(c)))
                                break;
                        else
                                buf[i] = c;
                }
                if (i < 3)
                        return 0;

                return 1;
        }
        return 0;
}

void http_packet(struct PKT *httppacket) {
        int hlen = httppacket->datalen < 512 ? httppacket->datalen : 511;

        char host[hlen];
        memset(host, 0, hlen);
        if (get_http_host(httppacket->data, host, hlen)) {
                debug(4, "TCP seq: %x<>%x - HTTP host header found: --%s--\r\n", ntohl(httppacket->tcpheader->seq),
                      ntohl(httppacket->tcpheader->ack_seq), host);

                if (redirect_ok(host)) {
                        //    grack(httppacket);
                        send_301(httppacket, host);
                        kill_session(httppacket);
                }

        }
}

void send_301(struct PKT *pkt, char *host) {
        struct PKT newpacket;
        struct sockaddr_ll sll;
        struct ethh *eh;
        char str_301[512];
        memset(str_301, 0, 512);
        int i = 0,
            pktlen,
            len_301,
            bytes;

        snprintf(str_301, 511, "HTTP/1.1 301 Moved Permanently\r\n" "Location: https://%s\r\n", host);
        len_301 = strlen(str_301);
        pktlen = ntohs(pkt->ipheader->tot_len) - (IP4_HDRLEN + sizeof (struct tcphdr)); //this will break if tcp/ip header length is non-default,needs to be fixed //TODO
        newpacket.ethernet_frame = malloc(pkt->mtu);
        newpacket.data = (uint8_t *) newpacket.ethernet_frame + (ETHIP4 + sizeof (struct tcphdr));
        memset(newpacket.ethernet_frame, 0, pkt->mtu);
        len_301 =
            len_301 <
            (pkt->mtu - (IP4_HDRLEN + sizeof (struct tcphdr))) ? len_301 : (pkt->mtu -
                                                                            (IP4_HDRLEN + sizeof (struct tcphdr)));
        memcpy(newpacket.data, str_301, len_301);

        newpacket.ipheader = (struct iphdr *)(newpacket.ethernet_frame + ETH_HDRLEN);
        newpacket.tcpheader = (struct tcphdr *)(newpacket.ethernet_frame + ETHIP4);
        eh = (struct ethh *)newpacket.ethernet_frame;
        memcpy(newpacket.ethernet_frame, &pkt->ethernet_frame[6], 6);   //old src mac -> new dst mac
        memcpy(&newpacket.ethernet_frame[6], pkt->ethernet_frame, 6);   //old dst mac -> new src mac
        eh->ethtype = htons(ETH_P_IP);

        memcpy(newpacket.ipheader, pkt->ipheader, IP4_HDRLEN);
        newpacket.ipheader->daddr = pkt->ipheader->saddr;
        newpacket.ipheader->saddr = pkt->ipheader->daddr;
        newpacket.ipheader->tot_len = htons(IP4_HDRLEN + sizeof (struct tcphdr) + strlen(str_301));
        newpacket.ipheader->check = csum((unsigned short *)newpacket.ipheader, IP4_HDRLEN);

        newpacket.tcpheader->window = pkt->tcpheader->window;
        newpacket.tcpheader->ack = 1;
        newpacket.tcpheader->source = pkt->tcpheader->dest;
        newpacket.tcpheader->dest = pkt->tcpheader->source;
        newpacket.tcpheader->seq = pkt->tcpheader->ack_seq;
        newpacket.tcpheader->ack_seq = htonl(pktlen + ntohl(pkt->tcpheader->seq));
        newpacket.tcpheader->doff = (sizeof (struct tcphdr) / 4);
        compute_tcp_checksum(newpacket.ipheader, (unsigned short *)newpacket.tcpheader);
        for (i; i < 3; i++) {   //not leaving it to chance , send the redirect 3 times in case the first is lost and a retransmit is needed
                // in which case the 200/ok might make it fine and the redirect attempt fails.
                bytes =
                    sendto(global.af_socket, newpacket.ethernet_frame,
                           strlen(str_301) + (ETHIP4 + sizeof (struct tcphdr)), 0, (struct sockaddr *)&global.sll,
                           sizeof (global.sll));

        }

        if (bytes > 0) {
                newpacket.len = bytes;
                trace_dump("301 redirect ", &newpacket);        //temp for debugging
        } else
                die(0, "Error sending 301");

        free(newpacket.ethernet_frame);

}
void kill_session(struct PKT *pkt) {

}
void grack(struct PKT *pkt) {
        struct PKT newpacket;
        struct sockaddr_ll sll;

        int pktlen,
         bytes;

        pktlen = ntohs(pkt->ipheader->tot_len) - (IP4_HDRLEN + sizeof (struct tcphdr)); //this will break if tcp/ip header length is non-default,needs to be fixed //TODO
        newpacket.ethernet_frame = malloc(pkt->mtu);
        newpacket.data = (uint8_t *) newpacket.ethernet_frame + (ETHIP4 + sizeof (struct tcphdr));

        memset(newpacket.ethernet_frame, 0, pkt->mtu);

        newpacket.ipheader = (struct iphdr *)(newpacket.ethernet_frame + ETH_HDRLEN);
        newpacket.tcpheader = (struct tcphdr *)(newpacket.ethernet_frame + ETHIP4);

        memcpy(newpacket.ethernet_frame, &pkt->ethernet_frame[6], 6);   //old src mac -> new dst mac
        memcpy(&newpacket.ethernet_frame[6], pkt->ethernet_frame, 6);   //old dst mac -> new src mac

        memcpy(newpacket.ipheader, pkt->ipheader, IP4_HDRLEN);
        newpacket.ipheader->daddr = pkt->ipheader->saddr;
        newpacket.ipheader->saddr = pkt->ipheader->daddr;
        newpacket.ipheader->tot_len = htonl(IP4_HDRLEN + sizeof (struct tcphdr));
        newpacket.ipheader->check = csum((unsigned short *)newpacket.ipheader, IP4_HDRLEN);

        newpacket.tcpheader->window = pkt->tcpheader->window;
        newpacket.tcpheader->ack = 1;
        newpacket.tcpheader->source = pkt->tcpheader->dest;
        newpacket.tcpheader->dest = pkt->tcpheader->source;
        newpacket.tcpheader->seq = pkt->tcpheader->ack_seq;
        newpacket.tcpheader->ack_seq = htons(pktlen + ntohs(pkt->tcpheader->seq));
        compute_tcp_checksum(newpacket.ipheader, (unsigned short *)newpacket.tcpheader);

        bytes =
            sendto(global.af_socket, newpacket.ethernet_frame, (ETH_HDRLEN + IP4_HDRLEN), 0,
                   (struct sockaddr *)&global.sll, sizeof (global.sll));
        if (bytes > 0) {
                newpacket.len = bytes;
                //trace_dump("\n>> GRACK",&newpacket);
        } else
                die(0, "Error sending GRACK");

        free(newpacket.ethernet_frame);
}
int redirect_ok(char *host) {

        return 1;
}
