
#include <netinet/in.h>
#include <errno.h>
#include <netdb.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <netinet/tcp.h>
#include <netinet/ip.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <net/ethernet.h>
#include <netpacket/packet.h>
#include <sys/time.h>
#include <algorithm>
#include <slankdev/socketfd.h>
#include <slankdev/poll.h>
#include <slankdev/net/hdr.h>

struct policy {
  int input_fd;
  int output_fd;
  const char* match;
  const char* apply;
} policies[] = {
  { -1, -1, "ntt", "ipa" },
};

static int
process_packet(uint8_t* pkt, size_t len, int inputfd)
{
  // printf("%s\n", __func__);
  slankdev::ether* eh = reinterpret_cast<slankdev::ether*>(pkt);
  if (ntohs(eh->type) != 0x86dd) return -1;

  struct in6_addr match_src;
  inet_pton(AF_INET6, "2001:34::00", &match_src);
  slankdev::ip6* ih = reinterpret_cast<slankdev::ip6*>(eh + 1);
  if (memcmp(&ih->src, &match_src, 8) != 0) return -1;

  char src[100],dst[100];
  inet_ntop(AF_INET6, &ih->src, src, sizeof(src));
  inet_ntop(AF_INET6, &ih->dst, dst, sizeof(dst));
  printf("match flow %s->%s\n", src, dst);

  for (size_t i=0; i<1; i++) {
    if (policies[i].input_fd == inputfd) {
      const char* match = policies[i].match;
      const char* apply = policies[i].apply;
      for (size_t i=0; i<len-strlen(match); i++) {
        // if (pkt[i] == 'n') printf("slank\n");
        int diff = memcmp(pkt+i, match, strlen(match));
        if (diff == 0) {
          printf("KAKIKAE!!!\n");
          memcpy(pkt+i, apply, strlen(apply));
        }
      }
    }
  }
  return 0;
}

static void
forward_frame(int sockA)
{
  // printf("start forwarding %d <--> %d\n", sockA, sockB);
  slankdev::pollfd pfd;
  pfd.append_fd(sockA, POLLIN|POLLERR);
  while (true) {

    pfd.poll(-1);
    for (size_t i=0; i<1; i++) {

      struct sockaddr_ll rll;
      if (pfd.get_revents(i) & POLLIN) {
        int input_fd = pfd.get_fd(i);
        int output_fd = pfd.get_fd(i);
        memset(&rll, 0, sizeof(rll));
        socklen_t rll_size = sizeof(rll);

        uint8_t buffer[8000];
        ssize_t frame_len = recvfrom(input_fd, &buffer, sizeof(buffer),
              0, (struct sockaddr *)&rll, &rll_size);
        if (frame_len < 0) {
          perror("recvfrom");
          exit(1);
        }

        if(rll.sll_pkttype!=PACKET_OUTGOING) {
          int ret = process_packet(buffer, frame_len, input_fd);
          if (ret < 0)
            continue;

          uint8_t dst[] = { 0x52, 0x54, 0x00, 0x86, 0x6a, 0xef };
          uint8_t src[] = { 0xc6, 0xca, 0xdf, 0x4b, 0x95, 0xba };
          memcpy(buffer  , dst, 6);
          memcpy(buffer+6, src, 6);

          printf("REFLECT\n");
          ssize_t send_len = send(output_fd, &buffer, frame_len, 0);
          if (send_len < 0) {
            perror("send");
            exit(1);
          }
        }
      }

    } // for
  }
}

static int
open_raw_sock(const char* devname)
{
  int sock = socket(PF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
  if (sock < 0 ) {
    perror("socket");
    exit(1);
  }

  struct ifreq ifr;
  memset(&ifr, 0, sizeof(struct ifreq));
  strcpy(ifr.ifr_name, devname);
  int ret = ioctl(sock, SIOCGIFINDEX, &ifr);
  if (ret < 0 ) {
    perror("ioctl");
    exit(1);
  }

  struct sockaddr_ll sa;
  sa.sll_family=AF_PACKET;
  sa.sll_protocol=htons(ETH_P_ALL);
  sa.sll_ifindex=ifr.ifr_ifindex;
  ret = bind(sock, (struct sockaddr *)&sa, sizeof(sa));
  if (ret < 0) {
    perror("bind");
    close(sock);
    exit(1);
  }

  struct packet_mreq mreq;
  mreq.mr_type = PACKET_MR_PROMISC;
  mreq.mr_ifindex = ifr.ifr_ifindex;
  mreq.mr_alen = 0;
  mreq.mr_address[0] ='\0';
  ret = setsockopt(sock, SOL_PACKET,
        PACKET_ADD_MEMBERSHIP,
        (void *)&mreq, sizeof(mreq));
  if (ret < 0) {
    perror("setsockopt");
    exit(1);
  }
  return sock;
}

int
main(int argc, char *argv[])
{
  if(argc < 2){
    fprintf(stderr, "Usage: %s <interface1>\n", argv[0]);
    return 1;
  }

  int sockA = open_raw_sock(argv[1]);
  policies[0].input_fd = sockA;
  policies[0].output_fd = sockA;

  printf("Interface1: %s\n", argv[1]);
  // printf("Interface2: %s\n", argv[2]);
  forward_frame(sockA);
}

