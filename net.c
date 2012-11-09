/*
Copyright (C) 1996-1997 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

    $Id: net.c,v 1.19 2007-10-04 13:48:11 dkure Exp $
*/

#include "quakedef.h"
#include "server.h"


netadr_t	net_local_cl_ipadr;

netadr_t	net_from;
sizebuf_t	net_message;

byte		net_message_buffer[MSG_BUF_SIZE];

#define MAX_LOOPBACK 4 // must be a power of two

typedef struct {
	byte	data[MAX_UDP_PACKET];
	int		datalen;
} loopmsg_t;

typedef struct {
	loopmsg_t	msgs[MAX_LOOPBACK];
	unsigned int	get, send;
} loopback_t;

#ifdef _WIN32
WSADATA winsockdata;
#endif

loopback_t	loopbacks[2];

//=============================================================================

void NetadrToSockadr (netadr_t *a, struct sockaddr_storage *s)
{
	memset (s, 0, sizeof(struct sockaddr_in));
	((struct sockaddr_in*)s)->sin_family = AF_INET;

	((struct sockaddr_in*)s)->sin_addr.s_addr = *(int *)&a->ip;
	((struct sockaddr_in*)s)->sin_port = a->port;
}

void SockadrToNetadr (struct sockaddr_storage *s, netadr_t *a)
{
	a->type = NA_IP;
	*(int *)&a->ip = ((struct sockaddr_in *)s)->sin_addr.s_addr;
	a->port = ((struct sockaddr_in *)s)->sin_port;
	return;
}

qbool NET_CompareBaseAdr (netadr_t a, netadr_t b)
{
	if (a.type == NA_LOOPBACK && b.type == NA_LOOPBACK)
		return true;
	if (a.ip[0] == b.ip[0] && a.ip[1] == b.ip[1] && a.ip[2] == b.ip[2] && a.ip[3] == b.ip[3])
		return true;
	return false;
}

qbool NET_CompareAdr (netadr_t a, netadr_t b)
{
	if (a.type == NA_LOOPBACK && b.type == NA_LOOPBACK)
		return true;
	if (a.ip[0] == b.ip[0] && a.ip[1] == b.ip[1] && a.ip[2] == b.ip[2] && a.ip[3] == b.ip[3] && a.port == b.port)
		return true;
	return false;
}

char *NET_AdrToString (netadr_t a)
{
	static char s[64];

	if (a.type == NA_LOOPBACK)
		return "loopback";

	snprintf (s, sizeof (s), "%i.%i.%i.%i:%i", a.ip[0], a.ip[1], a.ip[2], a.ip[3], ntohs(a.port));
	return s;
}

char *NET_BaseAdrToString (netadr_t a)
{
	static char s[64];
	
	snprintf (s, sizeof (s), "%i.%i.%i.%i", a.ip[0], a.ip[1], a.ip[2], a.ip[3]);
	return s;
}

/*
idnewt
idnewt:28000
192.246.40.70
192.246.40.70:28000
*/
qbool NET_StringToSockaddr (char *s, struct sockaddr_storage *sadr)
{
	struct hostent	*h;
	char	*colon;
	char	copy[128];

	if (!(*s))
		return false;

	memset (sadr, 0, sizeof(*sadr));

	((struct sockaddr_in *)sadr)->sin_family = AF_INET;

	((struct sockaddr_in *)sadr)->sin_port = 0;

	if (strlen(s) >= sizeof(copy) - 1)
		return false;

	strlcpy (copy, s, sizeof (copy));
	// strip off a trailing :port if present
	for (colon = copy ; *colon ; colon++)
	{
		if (*colon == ':') {
			*colon = 0;
			((struct sockaddr_in *)sadr)->sin_port = htons((short)atoi(colon+1));
		}
	}

	if (copy[0] >= '0' && copy[0] <= '9')
	{
		//this is the wrong way to test. a server name may start with a number.
		*(int *)&((struct sockaddr_in *)sadr)->sin_addr = inet_addr(copy);
	} else
	{
		if (!(h = gethostbyname(copy)))
			return false;
		if (h->h_addrtype != AF_INET)
			return false;
		*(int *)&((struct sockaddr_in *)sadr)->sin_addr = *(int *)h->h_addr_list[0];
	}

	return true;
}

qbool NET_StringToAdr (char *s, netadr_t *a)
{
	struct sockaddr_storage sadr;

	if (!strcmp(s, "local")) {
		memset(a, 0, sizeof(*a));
		a->type = NA_LOOPBACK;
		return true;
	}

	if (!NET_StringToSockaddr (s, &sadr))
		return false;

	SockadrToNetadr (&sadr, a);

	return true;
}

/*
=============================================================================
LOOPBACK BUFFERS FOR LOCAL PLAYER
=============================================================================
*/

qbool NET_GetLoopPacket (netsrc_t sock, netadr_t *from, sizebuf_t *message)
{
	int i;
	loopback_t *loop;

	loop = &loopbacks[sock];

	if (loop->send - loop->get > MAX_LOOPBACK)
		loop->get = loop->send - MAX_LOOPBACK;

	if (loop->get >= loop->send)
		return false;

	i = loop->get & (MAX_LOOPBACK-1);
	loop->get++;

	if (message->maxsize < loop->msgs[i].datalen)
		Sys_Error("NET_SendLoopPacket: Loopback buffer was too big");

	memcpy (message->data, loop->msgs[i].data, loop->msgs[i].datalen);
	message->cursize = loop->msgs[i].datalen;
	memset (from, 0, sizeof(*from));
	from->type = NA_LOOPBACK;
	return true;
}

void NET_SendLoopPacket (netsrc_t sock, int length, void *data, netadr_t to)
{
	int i;
	loopback_t *loop;

	loop = &loopbacks[sock ^ 1];

	i = loop->send & (MAX_LOOPBACK - 1);
	loop->send++;

	if (length > (int) sizeof(loop->msgs[i].data))
		Sys_Error ("NET_SendLoopPacket: length > MAX_UDP_PACKET");

	memcpy (loop->msgs[i].data, data, length);
	loop->msgs[i].datalen = length;
}

//=============================================================================

void NET_ClearLoopback (void)
{
	loopbacks[0].send = loopbacks[0].get = 0;
	loopbacks[1].send = loopbacks[1].get = 0;
}

static cl_delayed_packet_t cl_delayed_packets_get[CL_MAX_DELAYED_PACKETS];

qbool NET_GetPacketEx (netsrc_t netsrc, qbool delay)
{
	int ret, socket, err, i;
	struct sockaddr_storage from;
	socklen_t fromlen;

	if (delay)
	{
		double time = Sys_DoubleTime();

		for (i = 0; i < CL_MAX_DELAYED_PACKETS; i++)
		{
			if (!cl_delayed_packets_get[i].time)
				continue; // unused slot
			if (cl_delayed_packets_get[i].time > time)
				continue; // we are not yet ready to get this

			// ok, we got something
			SZ_Clear(&net_message);
			SZ_Write(&net_message, cl_delayed_packets_get[i].data, cl_delayed_packets_get[i].length);
			net_from = cl_delayed_packets_get[i].addr;

			cl_delayed_packets_get[i].time = 0; // mark as free slot

			return true;
		}

		return false;
	}

	if (NET_GetLoopPacket(netsrc, &net_from, &net_message))
		return true;

	for (i = 0; i < 1; i++) {
		if (netsrc == NS_SERVER) {
			Sys_Error("NET_GetPacket: Bad netsrc");
			socket = 0;
		} else {
			if (i == 0)
				socket = cls.socketip;
			else
				socket = INVALID_SOCKET;
		}

		if (socket == INVALID_SOCKET)
			continue;

		fromlen = sizeof(from);
		ret = recvfrom (socket, (char *)net_message_buffer, sizeof(net_message_buffer), 0, (struct sockaddr *)&from, &fromlen);

		if (ret == -1) {
			err = qerrno;

			if (err == EWOULDBLOCK)
				continue;

			if (err == EMSGSIZE) {
				SockadrToNetadr (&from, &net_from);
				Com_Printf ("Warning:  Oversize packet from %s\n", NET_AdrToString (net_from));
				continue;
			}

			if (err == ECONNABORTED || err == ECONNRESET) {
				Com_Printf ("Connection lost or aborted\n");
				continue;
			}

			Com_Printf ("NET_GetPacket: recvfrom: (%i): %s\n", err, strerror(err));
			continue;
		}

		SockadrToNetadr (&from, &net_from);

		net_message.cursize = ret;
		if (ret == sizeof(net_message_buffer)) {
			Com_Printf ("Oversize packet from %s\n", NET_AdrToString (net_from));
			return false;
		}

		return ret;
	}

// TCPCONNECT -->
	if (netsrc == NS_CLIENT) {
		if (cls.sockettcp != INVALID_SOCKET) { //client receiving only via tcp
			ret = recv(cls.sockettcp, (char *) cls.tcpinbuffer+cls.tcpinlen, sizeof(cls.tcpinbuffer)-cls.tcpinlen, 0);
			if (ret == -1) {
				err = qerrno;
	
				if (err == EWOULDBLOCK) {
					ret = 0;
				} else {
					if (err == ECONNABORTED || err == ECONNRESET) {
						closesocket(cls.sockettcp);
						cls.sockettcp = INVALID_SOCKET;
						Com_Printf ("Connection lost or aborted\n"); //server died/connection lost.
						return false;
					}

					closesocket(cls.sockettcp);
					cls.sockettcp = INVALID_SOCKET;
					Com_Printf ("NET_GetPacket: Error (%i): %s\n", err, strerror(err));
					return false;
				}
			}
			cls.tcpinlen += ret;

			if (cls.tcpinlen < 2)
				return false;

			net_message.cursize = BigShort(*(short*)cls.tcpinbuffer);
			if (net_message.cursize >= sizeof(net_message_buffer) ) {
				closesocket(cls.sockettcp);
				cls.sockettcp = INVALID_SOCKET;
				Com_Printf ("Warning:  Oversize packet from %s\n", NET_AdrToString (net_from));
				return false;
			}

			if (net_message.cursize+2 > cls.tcpinlen) {
				//not enough buffered to read a packet out of it.
				return false;
			}

			memcpy(net_message_buffer, cls.tcpinbuffer+2, net_message.cursize);
			memmove(cls.tcpinbuffer, cls.tcpinbuffer+net_message.cursize+2, cls.tcpinlen - (net_message.cursize+2));
			cls.tcpinlen -= net_message.cursize+2;

			net_from = cls.sockettcpdest;

			return true;
		}
	}

// <--TCPCONNECT
	return false;
}

qbool CL_QueInputPacket(void)
{
	int i;

	if (!NET_GetPacketEx(NS_CLIENT, false))
		return false;
	
	for (i = 0; i < CL_MAX_DELAYED_PACKETS; i++)
	{
		if (cl_delayed_packets_get[i].time)
			continue; // busy slot

		// we found unused slot, copy packet, get it later, later depends of cl_delay_packet
		memmove(cl_delayed_packets_get[i].data, net_message.data, net_message.cursize);
		cl_delayed_packets_get[i].length = net_message.cursize;
		cl_delayed_packets_get[i].addr = net_from;
		cl_delayed_packets_get[i].time = Sys_DoubleTime() + 0.001 * bound(0, 0.5 * cl_delay_packet.value, CL_MAX_PACKET_DELAY);

		return true;
	}

	Com_DPrintf("CL_QueInputPacket: cl_delayed_packets_get overflowed\n");

	return false;
}

qbool NET_GetPacket (netsrc_t netsrc)
{
	qbool delay = (netsrc == NS_CLIENT && cl_delay_packet.integer);

	return NET_GetPacketEx (netsrc, delay);
}

//=============================================================================

static cl_delayed_packet_t cl_delayed_packets_send[CL_MAX_DELAYED_PACKETS];

void NET_SendPacketEx (netsrc_t netsrc, int length, void *data, netadr_t to, qbool delay)
{
	struct sockaddr_storage addr;
	int socket;
	int size;
	int ret;

	if (delay)
	{
		int i;

		for (i = 0; i < CL_MAX_DELAYED_PACKETS; i++)
		{
			if (cl_delayed_packets_send[i].time)
				continue; // busy slot
			// we found unused slot, copy packet, send it later, later depends of cl_delay_packet
			memmove(cl_delayed_packets_send[i].data, data, length);
			cl_delayed_packets_send[i].length = length;
			cl_delayed_packets_send[i].addr = to;
			cl_delayed_packets_send[i].time = Sys_DoubleTime() + 0.001 * bound(0, 0.5 * cl_delay_packet.value, CL_MAX_PACKET_DELAY);

			return;
		}

		Com_DPrintf("NET_SendPacketEx: cl_delayed_packets_send overflowed\n");
		return;
	}

	if (to.type == NA_LOOPBACK) {
		NET_SendLoopPacket (netsrc, length, data, to);
		return;
	}

	if (netsrc == NS_SERVER) {
		Sys_Error("NET_SendPacket: Bad netsrc");
		socket = 0;
	} else {
// TCPCONNECT -->
		if (cls.sockettcp != INVALID_SOCKET)
		{
			if (NET_CompareAdr(to, cls.sockettcpdest))
			{
				//this goes to the server so send it via tcp
				unsigned short slen = BigShort((unsigned short)length);
				send(cls.sockettcp, (char*)&slen, sizeof(slen), 0);
				send(cls.sockettcp, data, length, 0);
		
				return;
			}
		}
// <--TCPCONNECT

		socket = cls.socketip;
	}

	if (socket == INVALID_SOCKET)
		return;

	NetadrToSockadr (&to, &addr);
	size = sizeof(struct sockaddr_in);

	ret = sendto (socket, data, length, 0, (struct sockaddr *)&addr, size);
	if (ret == -1) {
		if (qerrno == EWOULDBLOCK)
			return;
		if (qerrno == ECONNREFUSED)
			return;
		if (qerrno == EADDRNOTAVAIL)
			return;
		Sys_Printf ("NET_SendPacket: sendto: (%i): %s %i\n", qerrno, strerror(qerrno), socket);
	}
}

void CL_UnqueOutputPacket(qbool sendall)
{
	int i;
	double time = Sys_DoubleTime();

	for (i = 0; i < CL_MAX_DELAYED_PACKETS; i++)
	{
		if (!cl_delayed_packets_send[i].time)
			continue; // unused slot
		if (cl_delayed_packets_send[i].time > time && !sendall)
			continue; // we are not yet ready for send

		// ok, send it
		NET_SendPacketEx(NS_CLIENT, cl_delayed_packets_send[i].length, 
			cl_delayed_packets_send[i].data, cl_delayed_packets_send[i].addr, false);

		cl_delayed_packets_send[i].time = 0; // mark as unused slot

// perhaps there other packets should be sent
//		return;
	}
}

void NET_SendPacket (netsrc_t netsrc, int length, void *data, netadr_t to)
{
	qbool delay = (netsrc == NS_CLIENT && cl_delay_packet.integer);

	NET_SendPacketEx (netsrc, length, data, to, delay);
}

//=============================================================================

qbool TCP_Set_KEEPALIVE(int sock)
{
	int		iOptVal = 1;

	if (sock == INVALID_SOCKET) {
		Con_Printf("TCP_Set_KEEPALIVE: invalid socket\n");
		return false;
	}

	if (setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, (void*)&iOptVal, sizeof(iOptVal)) == SOCKET_ERROR) {
		Con_Printf ("TCP_Set_KEEPALIVE: setsockopt: (%i): %s\n", qerrno, strerror (qerrno));
		return false;
	}

#if defined(__linux__)

//	The time (in seconds) the connection needs to remain idle before TCP starts sending keepalive probes, 
//  if the socket option SO_KEEPALIVE has been set on this socket.

	iOptVal = 60;

	if (setsockopt(sock, SOL_TCP, TCP_KEEPIDLE, (void*)&iOptVal, sizeof(iOptVal)) == -1) {
		Con_Printf ("TCP_Set_KEEPALIVE: setsockopt TCP_KEEPIDLE: (%i): %s\n", qerrno, strerror(qerrno));
		return false;
	}

//  The time (in seconds) between individual keepalive probes.
	iOptVal = 30;

	if (setsockopt(sock, SOL_TCP, TCP_KEEPINTVL, (void*)&iOptVal, sizeof(iOptVal)) == -1) {
		Con_Printf ("TCP_Set_KEEPALIVE: setsockopt TCP_KEEPINTVL: (%i): %s\n", qerrno, strerror(qerrno));
		return false;
	}

//  The maximum number of keepalive probes TCP should send before dropping the connection. 
	iOptVal = 6;

	if (setsockopt(sock, SOL_TCP, TCP_KEEPCNT, (void*)&iOptVal, sizeof(iOptVal)) == -1) {
		Con_Printf ("TCP_Set_KEEPALIVE: setsockopt TCP_KEEPCNT: (%i): %s\n", qerrno, strerror(qerrno));
		return false;
	}
#else
	// FIXME: windows, bsd etc...
#endif

	return true;
}


int TCP_OpenStream (netadr_t remoteaddr)
{
	unsigned long _true = true;
	int newsocket;
	int temp;
	struct sockaddr_storage qs;

	NetadrToSockadr(&remoteaddr, &qs);
	temp = sizeof(struct sockaddr_in);

	if ((newsocket = socket (((struct sockaddr_in*)&qs)->sin_family, SOCK_STREAM, IPPROTO_TCP)) == INVALID_SOCKET) {
		Com_Printf ("TCP_OpenStream: socket: (%i): %s\n", qerrno, strerror(qerrno));
		return INVALID_SOCKET;
	}

	if (connect (newsocket, (struct sockaddr *)&qs, temp) == INVALID_SOCKET) {
		Com_Printf ("TCP_OpenStream: connect: (%i): %s\n", qerrno, strerror(qerrno));
		closesocket(newsocket);
		return INVALID_SOCKET;
	}

#ifndef _WIN32
	if ((fcntl (newsocket, F_SETFL, O_NONBLOCK)) == -1) { // O'Rly?! @@@
		Com_Printf ("TCP_OpenStream: fcntl: (%i): %s\n", qerrno, strerror(qerrno));
		closesocket(newsocket);
		return INVALID_SOCKET;
	}
#endif

	if (ioctlsocket (newsocket, FIONBIO, &_true) == -1) { // make asynchronous
		Com_Printf ("TCP_OpenStream: ioctl: (%i): %s\n", qerrno, strerror(qerrno));
		closesocket(newsocket);
		return INVALID_SOCKET;
	}

	return newsocket;
}

int TCP_OpenListenSocket (int port)
{
	int newsocket;
	struct sockaddr_in address;
	unsigned long _true = true;
	int i;

	if ((newsocket = socket (PF_INET, SOCK_STREAM, IPPROTO_TCP)) == INVALID_SOCKET) {
		Com_Printf ("TCP_OpenListenSocket: socket: (%i): %s\n", qerrno, strerror(qerrno));
		return INVALID_SOCKET;
	}

#ifndef _WIN32
	if ((fcntl (newsocket, F_SETFL, O_NONBLOCK)) == -1) { // O'Rly?! @@@
		Com_Printf ("TCP_OpenListenSocket: fcntl: (%i): %s\n", qerrno, strerror(qerrno));
		closesocket(newsocket);
		return INVALID_SOCKET;
	}
#endif

	if (ioctlsocket (newsocket, FIONBIO, &_true) == -1) { // make asynchronous
		Com_Printf ("TCP_OpenListenSocket: ioctl: (%i): %s\n", qerrno, strerror(qerrno));
		closesocket(newsocket);
		return INVALID_SOCKET;
	}

	address.sin_family = AF_INET;

	// check for interface binding option
	if ((i = COM_CheckParm("-ip")) != 0 && i < COM_Argc()) {
		address.sin_addr.s_addr = inet_addr(COM_Argv(i+1));
		Com_DPrintf ("Binding to IP Interface Address of %s\n", inet_ntoa(address.sin_addr));
	} else {
		address.sin_addr.s_addr = INADDR_ANY;
	}
	
	if (port == PORT_ANY)
		address.sin_port = 0;
	else
		address.sin_port = htons((short)port);

	if (bind (newsocket, (void *)&address, sizeof(address)) == -1) {
		Com_Printf ("TCP_OpenListenSocket: bind: (%i): %s\n", qerrno, strerror(qerrno));
		closesocket(newsocket);
		return INVALID_SOCKET;
	}

	if (listen (newsocket, 1) == INVALID_SOCKET) {
		Com_Printf ("TCP_OpenListenSocket: listen: (%i): %s\n", qerrno, strerror(qerrno));
		closesocket(newsocket);
		return INVALID_SOCKET;
	}

	return newsocket;
}



int UDP_OpenSocket (int port)
{
	int newsocket;
	struct sockaddr_in address;
	unsigned long _true = true;
	int i;

	if ((newsocket = socket (PF_INET, SOCK_DGRAM, IPPROTO_UDP)) == INVALID_SOCKET) {
		Com_Printf ("UDP_OpenSocket: socket: (%i): %s\n", qerrno, strerror(qerrno));
		return INVALID_SOCKET;
	}

#ifndef _WIN32
	if ((fcntl (newsocket, F_SETFL, O_NONBLOCK)) == -1) { // O'Rly?! @@@
		Com_Printf ("UDP_OpenSocket: fcntl: (%i): %s\n", qerrno, strerror(qerrno));
		closesocket(newsocket);
		return INVALID_SOCKET;
	}
#endif

	if (ioctlsocket (newsocket, FIONBIO, &_true) == -1) { // make asynchronous
		Com_Printf ("UDP_OpenSocket: ioctl: (%i): %s\n", qerrno, strerror(qerrno));
		closesocket(newsocket);
		return INVALID_SOCKET;
	}

	address.sin_family = AF_INET;

	// check for interface binding option
	if ((i = COM_CheckParm("-ip")) != 0 && i < COM_Argc()) {
		address.sin_addr.s_addr = inet_addr(COM_Argv(i+1));
		Com_DPrintf ("Binding to IP Interface Address of %s\n", inet_ntoa(address.sin_addr));
	} else {
		address.sin_addr.s_addr = INADDR_ANY;
	}

	if (port == PORT_ANY)
		address.sin_port = 0;
	else
		address.sin_port = htons((short)port);

	if (bind (newsocket, (void *)&address, sizeof(address)) == -1) {
		Com_Printf ("UDP_OpenSocket: bind: (%i): %s\n", qerrno, strerror(qerrno));
		closesocket(newsocket);
		return INVALID_SOCKET;
	}

	return newsocket;
}

#ifndef _WIN32
extern qbool stdin_ready;
extern int do_stdin;
#endif
qbool NET_Sleep (int msec)
{
	struct timeval timeout;
	fd_set fdset;
	int i;

	FD_ZERO (&fdset);

#ifndef _WIN32
	if (do_stdin)
		FD_SET (0, &fdset); // stdin is processed too (tends to be socket 0)
#endif

	i = 0;

	timeout.tv_sec = msec/1000;
	timeout.tv_usec = (msec%1000)*1000;
	select(i+1, &fdset, NULL, NULL, &timeout);

#ifndef _WIN32
	stdin_ready = FD_ISSET (0, &fdset);
#endif

	return true;
}

void NET_GetLocalAddress (int socket, netadr_t *out)
{
	char buff[512];
	struct sockaddr_storage address;
	size_t namelen;
	netadr_t adr = {0};
	qbool notvalid = false;

	strlcpy (buff, "localhost", sizeof (buff));
	gethostname (buff, sizeof (buff));

	if (!NET_StringToAdr (buff, &adr))	//urm
		NET_StringToAdr ("127.0.0.1", &adr);

	namelen = sizeof(address);
	if (getsockname (socket, (struct sockaddr *)&address, (socklen_t *)&namelen) == -1) {
		notvalid = true;
		NET_StringToSockaddr("0.0.0.0", (struct sockaddr_storage *)&address);
		//		Sys_Error ("NET_Init: getsockname:", strerror(qerrno));
	}

	SockadrToNetadr(&address, out);
	if (!*(int*)out->ip)	//socket was set to auto
		*(int *)out->ip = *(int *)adr.ip;	//change it to what the machine says it is, rather than the socket.

	if (notvalid)
		Com_Printf_State (PRINT_FAIL, "Couldn't detect local ip\n");
	else
		Com_Printf_State (PRINT_OK, "IP address %s\n", NET_AdrToString (*out));
}

void NET_Init (void)
{
#ifdef _WIN32
	WORD wVersionRequested;
	int r;

	wVersionRequested = MAKEWORD(1, 1);
	r = WSAStartup (wVersionRequested, &winsockdata);
	if (r)
		Sys_Error ("Winsock initialization failed.");
#endif

	Com_DPrintf("UDP Initialized\n");

	cls.socketip = INVALID_SOCKET;
// TCPCONNECT -->
	cls.sockettcp = INVALID_SOCKET;
// <--TCPCONNECT

}

void NET_InitClient(void)
{
	int port = PORT_CLIENT;
	int p;

	p = COM_CheckParm ("-clientport");
	if (p && p < COM_Argc()) {
		port = atoi(COM_Argv(p+1));
	}

	if (cls.socketip == INVALID_SOCKET)
		cls.socketip = UDP_OpenSocket (port);

	if (cls.socketip == INVALID_SOCKET)
		cls.socketip = UDP_OpenSocket (PORT_ANY); // any dynamic port

	if (cls.socketip == INVALID_SOCKET)
		Sys_Error ("Couldn't allocate client socket");

	// init the message buffer
	SZ_Init (&net_message, net_message_buffer, sizeof(net_message_buffer));

	// determine my name & address
	NET_GetLocalAddress (cls.socketip, &net_local_cl_ipadr);

	Com_Printf_State (PRINT_OK, "Client port Initialized\n");
}

void NET_Shutdown (void)
{
	if (cls.socketip != INVALID_SOCKET) {
	closesocket(cls.socketip);
		cls.socketip = INVALID_SOCKET;
	}

#ifdef _WIN32
	WSACleanup ();
#endif
}

