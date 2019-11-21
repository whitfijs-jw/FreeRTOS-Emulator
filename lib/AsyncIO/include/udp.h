#ifndef __UDP_H__
#define __UDP_H__

#define SOCKET_TYPE_UDP IPPROTO_UDP
#define SOCKET_TYPE_TCP IPPROTO_TCP

void udpInit(void);
void udpOpenSocket(char *ip, unsigned short port, int con_type,
		   void (*callback)(int, void *), void *(args));

#endif
