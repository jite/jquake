/*
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

	$Id: server.h 783 2008-06-26 23:15:19Z qqshka $
*/

// server.h
#ifndef __SERVER_H__
#define __SERVER_H__

#include "progs.h"
#include "qtv.h"

// { !!! FIXME: MOVE ME TO SYS.H !!!

#ifdef _WIN32

typedef HMODULE DL_t;

#define DLEXT "dll"

#else

typedef void *DL_t;

#define DLEXT "so"

#endif /* _WIN32 */

DL_t Sys_DLOpen (const char *path);
qbool Sys_DLClose( DL_t dl);
void *Sys_DLProc (DL_t dl, const char *name);

// }

// a client can leave the server in one of four ways:
// dropping properly by quiting or disconnecting
// timing out if no valid messages are received for timeout.value seconds
// getting kicked off by the server operator
// a program error, like an overflowed reliable buffer

typedef struct
{
	int				parsecount;

	vec3_t			origin;
	vec3_t			angles;
	int				weaponframe;
	int				frame;
	int				skinnum;
	int				model;
	int				effects;
	int				flags;

	qbool			fixangle;

	float			cmdtime;
	float			sec;

} demo_client_t;

typedef struct
{
	demo_client_t	clients[MAX_CLIENTS];
	double			time;

// { reset each time frame wroten with SV_MVDWritePackets()
	sizebuf_t		_buf_;
	// !!! OUCH OUCH OUCH, 64 frames, so it about 2mb !!!
	// here data with mvd headers, so it up to 4 mvd msg with maximum size, however basically here alot of small msgs,
	// so this size pathetic
	byte			_buf__data[(MAX_MVD_SIZE + 10) * 4];

	int				lastto;
	int				lasttype;
	int				lastsize;
	int				lastsize_offset; // this is tricky
// }

} demo_frame_t;

//qtv proxies are meant to send a small header now, bit like http
//this header gives supported version numbers and stuff
typedef struct mvdpendingdest_s
{
	qbool error; //disables writers, quit ASAP.
	int socket;

	char inbuffer[2048];
	char outbuffer[2048];

	char challenge[64];
	qbool hasauthed;

	int insize;
	int outsize;

	double			io_time; // when last IO occur on socket, so we can timeout this dest
	netadr_t		na;

	struct mvdpendingdest_s *nextdest;
} mvdpendingdest_t;

typedef enum {DEST_NONE, DEST_FILE, DEST_BUFFEREDFILE, DEST_STREAM} desttype_t;

#define MAX_PROXY_INBUFFER		4096 /* qqshka: too small??? */

//=============================================================================


#define	STATFRAMES	100
typedef struct
{
	double			active;
	double			idle;
	double			demo;
	int				count;
	int				packets;

	double			latched_active;
	double			latched_idle;
	double			latched_demo;
	int				latched_packets;
} svstats_t;

// MAX_CHALLENGES is made large to prevent a denial
// of service attack that could cycle all of them
// out before legitimate users connected
#define	MAX_CHALLENGES	1024

typedef struct
{
	netadr_t		adr;
	int				challenge;
	int				time;
} challenge_t;

//=============================================================================

// edict->movetype values
#define	MOVETYPE_NONE			0		// never moves
#define	MOVETYPE_ANGLENOCLIP		1
#define	MOVETYPE_ANGLECLIP		2
#define	MOVETYPE_WALK			3		// gravity
#define	MOVETYPE_STEP			4		// gravity, special edge handling
#define	MOVETYPE_FLY			5
#define	MOVETYPE_TOSS			6		// gravity
#define	MOVETYPE_PUSH			7		// no clip to world, push and crush
#define	MOVETYPE_NOCLIP			8
#define	MOVETYPE_FLYMISSILE		9		// extra size to monsters
#define	MOVETYPE_BOUNCE			10
#define	MOVETYPE_LOCK			15		// server controls view angles

// edict->solid values
#define	SOLID_NOT			0		// no interaction with other objects
#define	SOLID_TRIGGER			1		// touch on edge, but not blocking
#define	SOLID_BBOX			2		// touch on edge, block
#define	SOLID_SLIDEBOX			3		// touch on edge, but not an onground
#define	SOLID_BSP			4		// bsp clip, touch on edge, block

// edict->deadflag values
#define	DAMAGE_NO			0
#define	DAMAGE_YES			1
#define	DAMAGE_AIM			2

// edict->flags
#define	FL_FLY				1
#define	FL_SWIM				2
#define	FL_GLIMPSE			4
#define	FL_CLIENT			8
#define	FL_INWATER			16
#define	FL_MONSTER			32
#define	FL_GODMODE			64
#define	FL_NOTARGET			128
#define	FL_ITEM				256
#define	FL_ONGROUND			512
#define	FL_PARTIALGROUND		1024	// not all corners are valid
#define	FL_WATERJUMP			2048	// player jumping out of water

#define	SPAWNFLAG_NOT_EASY		256
#define	SPAWNFLAG_NOT_MEDIUM		512
#define	SPAWNFLAG_NOT_HARD		1024
#define	SPAWNFLAG_NOT_DEATHMATCH	2048

#define	MULTICAST_ALL			0
#define	MULTICAST_PHS			1
#define	MULTICAST_PVS			2

#define	MULTICAST_ALL_R			3
#define	MULTICAST_PHS_R			4
#define	MULTICAST_PVS_R			5

// maps in localinfo supported only by ktpro & ktx mods {

#define MAX_LOCALINFOS 10000

#define LOCALINFO_MAPS_LIST_START		1000
#define LOCALINFO_MAPS_LIST_END			4999

#define LOCALINFO_MAPS_KTPRO_VERSION	1.63
#define LOCALINFO_MAPS_KTPRO_VERSION_S	"1.63"
#define LOCALINFO_MAPS_KTPRO_BUILD		42795
#define SERVERINFO_KTPRO_VERSION		"kmod"
#define SERVERINFO_KTPRO_BUILD			"build"

// all versions of ktx with such serverinfo's keys support maps in localinfo
#define SERVERINFO_KTX_VERSION			"ktxver"
#define SERVERINFO_KTX_BUILD			"ktxbuild"
// }

#define MAX_REDIRECTMESSAGES	128
#define OUTPUTBUF_SIZE			8000

// { server flags

// force player enter server as spectator if all players's slots are busy and
// if there are empty slots for spectators and sv_forcespec_onfull == 2
#define SVF_SPEC_ONFULL			(1<<0)
// do not join server as spectator if server full and sv_forcespec_onfull == 1
#define SVF_NO_SPEC_ONFULL		(1<<1)


// } server flags

//============================================================================

extern	int current_skill;

extern	cvar_t	spawn;
extern	cvar_t	teamplay;
extern	cvar_t	serverdemo;
extern	cvar_t	deathmatch;
extern	cvar_t	fraglimit;
extern	cvar_t	timelimit;
extern	cvar_t	skill;
extern	cvar_t	coop;
extern	cvar_t	maxclients;

extern	cvar_t	sv_specprint;	//bliP: spectator print

//extern	entity_state_t	cl_state_entities[MAX_CLIENTS][UPDATE_BACKUP][MAX_PACKET_ENTITIES]; // client entities

#define	MODEL_NAME_LEN	5
extern	char		localmodels[MAX_MODELS][MODEL_NAME_LEN]; // inline model names for precache
//extern	char		localinfo[MAX_LOCALINFO_STRING+1];
extern  ctxinfo_t _localinfo_;

extern	int		host_hunklevel;

extern char		master_rcon_password[128];

extern qbool is_ktpro;

//===========================================================

typedef struct
{
	int sec;
	int min;
	int hour;
	int day;
	int mon;
	int year;
	char str[128];
} date_t;


qbool GameStarted(void);
//<-
char *Q_normalizetext (char *name); //bliP: red to white text
unsigned char *Q_redtext (unsigned char *str); //bliP: white to red text
unsigned char *Q_yelltext (unsigned char *str); //VVD: white to red text and yellow numbers

#endif /* !__SERVER_H__ */
