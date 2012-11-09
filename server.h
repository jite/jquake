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

#define CHAT_ICON_EXPERIMENTAL 1

#define	MAX_MASTERS 8 // max recipients for heartbeat packets

#define	MAX_SIGNON_BUFFERS 16

// sv_specprint stuff
#define SPECPRINT_CENTERPRINT	0x1
#define SPECPRINT_SPRINT	0x2
#define SPECPRINT_STUFFCMD	0x4

typedef enum {
	ss_dead,	// no map loaded
	ss_loading,	// spawning level edicts
	ss_active	// actively running
} server_state_t;
// some qc commands are only valid before the server has finished
// initializing (precache commands, static sounds / objects, etc)

typedef struct packet_s
{
	double		time;
	sizebuf_t	msg;
	byte		buf[MSG_BUF_SIZE]; // ?MAX_MSGLEN?
	struct packet_s *next;
} packet_t;

#define MAX_DELAYED_PACKETS 1024 // maxclients 32 * 77fps * max minping 0.3 = 739.2
#define MAP_NAME_LEN 64
typedef struct
{
	server_state_t	state;				// precache commands are only valid during load

	double		time;
	double		old_time;			// bumped by SV_Physics

	double		physicstime;		// last time physics was run

	int			lastcheck;			// used by PF_checkclient
	double		lastchecktime;			// for monster ai

	qbool		paused;				// are we paused?
	double		pausedsince;		// Sys_DoubleTime() when pause started

	qbool		loadgame;			// handle connections specially

	//check player/eyes models for hacks
	unsigned	model_player_checksum;
	unsigned	eyes_player_checksum;
	
	char		mapname[MAP_NAME_LEN];		// map name
	char		modelname[MAX_QPATH];		// maps/<name>.bsp, for model_precache[0]
	unsigned	map_checksum;
	unsigned	map_checksum2;
	cmodel_t	*worldmodel;
	char		*model_precache[MAX_MODELS];	// NULL terminated
	char		*vw_model_name[MAX_VWEP_MODELS];	// NULL terminated
	char		*sound_precache[MAX_SOUNDS];	// NULL terminated
	char		*lightstyles[MAX_LIGHTSTYLES];
	cmodel_t	*models[MAX_MODELS];

	int		num_edicts;			// increases towards MAX_EDICTS
	edict_t		*edicts;			// can NOT be array indexed, because
							// edict_t is variable sized, but can
							// be used to reference the world ent
	sv_edict_t	sv_edicts[MAX_EDICTS]; // part of the edict_t

	byte		*pvs, *phs;			// fully expanded and decompressed

	// added to every client's unreliable buffer each frame, then cleared
	sizebuf_t	datagram;
	byte		datagram_buf[MAX_DATAGRAM];

	// added to every client's reliable buffer each frame, then cleared
	sizebuf_t	reliable_datagram;
	byte		reliable_datagram_buf[MAX_MSGLEN];

	// the multicast buffer is used to send a message to a set of clients
	sizebuf_t	multicast;
	byte		multicast_buf[MAX_MSGLEN];

	// the signon buffer will be sent to each client as they connect
	// includes the entity baselines, the static entities, etc
	// large levels will have >MAX_DATAGRAM sized signons, so 
	// multiple signon messages are kept
	sizebuf_t	signon;
	unsigned int	num_signon_buffers;
	int		signon_buffer_size[MAX_SIGNON_BUFFERS];
	byte		signon_buffers[MAX_SIGNON_BUFFERS][MAX_DATAGRAM];

	qbool		mvdrecording;
} server_t;

#define	NUM_SPAWN_PARMS 16

typedef enum
{
	cs_free,		// can be reused for a new connection
	cs_zombie,		// client has been disconnected, but don't reuse
				// connection for a couple seconds
	cs_preconnected,	// has been assigned, but login/realip not settled yet
	cs_connected,		// has been assigned to a client_t, but not in game yet
	cs_spawned		// client is fully in game
} sv_client_state_t;		// FIXME

typedef struct
{
	// received from client

	// reply
	double			senttime;
	float			ping_time;
	packet_entities_t	entities;
} client_frame_t;

#define MAX_BACK_BUFFERS	128
#define MAX_STUFFTEXT		256
#define	CLIENT_LOGIN_LEN	16
#define	CLIENT_NAME_LEN		32
typedef struct client_s
{
	sv_client_state_t	state;

	int				extensions;			// what ZQuake extensions the client supports
	int				spectator;			// non-interactive
	int				vip;

	qbool			sendinfo;			// at end of frame, send info to all
							// this prevents malicious multiple broadcasts
	float			lastnametime;			// time of last name change
	int				lastnamecount;			// time of last name change
	unsigned		checksum;			// checksum for calcs
	qbool			drop;				// lose this guy next opportunity
	int				lossage;			// loss percentage

	int				userid;				// identifying number
	ctxinfo_t		_userinfo_ctx_;			// infostring
	ctxinfo_t		_userinfoshort_ctx_;	// infostring

	usercmd_t		lastcmd;			// for filling in big drops and partial predictions
	double			localtime;			// of last message
	qbool			jump_held;

	float			maxspeed;			// localized maxspeed
	float			entgravity;			// localized ent gravity

	edict_t			*edict;				// EDICT_NUM(clientnum+1)
#ifdef USE_PR2
	int		isBot;
	usercmd_t		botcmd;				// bot movment
	char			*name;				// in PR2 points to ent->v.netname
#else
	char			name[CLIENT_NAME_LEN];		// for printing to other people
#endif
	char			team[CLIENT_NAME_LEN];
							// extracted from userinfo
	int				messagelevel;			// for filtering printed messages

	// the datagram is written to after every frame, but only cleared
	// when it is sent out to the client.  overflow is tolerated.
	sizebuf_t		datagram;
	byte			datagram_buf[MAX_DATAGRAM];

	// back buffers for client reliable data
	sizebuf_t		backbuf;
	int				num_backbuf;
	int				backbuf_size[MAX_BACK_BUFFERS];
	byte			backbuf_data[MAX_BACK_BUFFERS][MAX_MSGLEN];

	char			stufftext_buf[MAX_STUFFTEXT];

	double			connection_started;		// or time of disconnect for zombies
	qbool			send_message;			// set on frames a datagram arived on

// spawn parms are carried from level to level
	float			spawn_parms[NUM_SPAWN_PARMS];

// client known data for deltas	
	int				old_frags;
	
	int				stats[MAX_CL_STATS];

	double			lastservertimeupdate;		// last realtime we sent STAT_TIME to the client

	client_frame_t	frames[UPDATE_BACKUP];		// updates can be deltad from here

	vfsfile_t		*download;			// file being downloaded

#ifdef PROTOCOL_VERSION_FTE
#ifdef FTE_PEXT_CHUNKEDDOWNLOADS
	int				download_chunks_perframe;
#endif
#endif
	int				downloadsize;			// total bytes
	int				downloadcount;			// bytes sent
// demo download list for internal cmd dl function
//Added by VVD {
	int				demonum[MAX_ARGS];
	qbool			demolist;
// } Added by VVD

	int				spec_track;			// entnum of player tracking

	double			whensaid[10];			// JACK: For floodprots
 	int				whensaidhead;			// Head value for floodprots
 	double			lockedtill;

	FILE			*upload;
	char			uploadfn[MAX_QPATH];
	netadr_t		snap_from;
	qbool			remote_snap;

	char			login[CLIENT_LOGIN_LEN];
	int				logged;

	int				spawncount;			// for tracking map changes during downloading

//bliP: additional ->
	int				file_percent;
	qbool			special;
	int				logincount;
	float			lasttoptime;			// time of last topcolor change
	int				lasttopcount;			// count of last topcolor change
	int				lastconnect;
	int				spec_print;
	double			cuff_time;
//bliP: 24/9 anti speed ->
	int				msecs;
	double			last_check;
//<-
//<-
	float			lastuserinfotime;		// time of last userinfo change
	int				lastuserinfocount;		// count of last userinfo change

#ifdef PROTOCOL_VERSION_FTE
	unsigned int	fteprotocolextensions;
#endif

#ifdef PROTOCOL_VERSION_FTE2
	unsigned int	fteprotocolextensions2;
#endif

#ifdef FTE_PEXT2_VOICECHAT
	unsigned int voice_read;	/*place in ring*/
	unsigned char voice_mute[MAX_CLIENTS/8];
	qbool voice_active;
	enum
	{
		/*note - when recording an mvd, only 'all' will be received by non-spectating viewers. all other chat will only be heard when spectating the receiver(or sender) of said chat*/

		/*should we add one to respond to the last speaker? or should that be an automagic +voip_reply instead?*/
		VT_TEAM,
		VT_ALL,
		VT_NONMUTED,	/*cheap, but allows custom private channels with no external pesters*/
		VT_PLAYERSLOT0
		/*player0+...*/
	} voice_target;
#endif

//===== NETWORK ============
	qbool			process_pext;		// true if we wait for reply from client on "cmd pext" command.
	int				chokecount;
	int				delta_sequence;			// -1 = no compression
	netchan_t		netchan;
	netadr_t		realip;				// client's ip, not latest proxy's
	int				realip_num;			// random value
	int				realip_count;
	int				rip_vip;
	double			delay;
	double			disable_updates_stop;		//Vladis
	packet_t		*packets, *last_packet;
} client_t;

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

typedef struct mvddest_s
{
	qbool error; //disables writers, quit ASAP.

	desttype_t desttype;

	int socket;
	FILE *file;

	char name[MAX_QPATH];
	char path[MAX_QPATH];

	char *cache;
	int cacheused;
	int maxcachesize;

	unsigned int totalsize;

// { used by QTV
	double			io_time; // when last IO occur on socket, so we can timeout this dest
	int				id; // dest id, used by QTV only
	netadr_t		na;

	char			inbuffer[MAX_PROXY_INBUFFER];
	int				inbuffersize;

	char			qtvname[64];
	qtvuser_t		*qtvuserlist;
// }

	struct mvddest_s *nextdest;
} mvddest_t;

typedef struct
{
	sizebuf_t		datagram;
	byte			datagram_data[MAX_MVD_SIZE]; // data without mvd header

	double			time;
	double			pingtime;

	// SOmething like time of last mvd message, so we can guess delta milliseconds for next message.
	// you better not relay on this variable...
	double			prevtime;

	client_t		recorder;

	qbool			fixangle[MAX_CLIENTS];

	int				stats[MAX_CLIENTS][MAX_CL_STATS];

	int				parsecount;  // current frame, to which we add demo data
	int				lastwritten; // lastwriten frame

	demo_frame_t	frames[UPDATE_BACKUP]; // here we store all previous frames
	demo_client_t	clients[MAX_CLIENTS]; // we store here what we wrote last time so we can delta

	int				forceFrame;

	// =====================================
	char			mem_set_point; // fields below, like ->dest and ->pendingdest must not be memset to 0
	// =====================================

	struct mvddest_s		*dest;
	struct mvdpendingdest_s *pendingdest;

	// last recorded demo's names for command "cmd dl . .." (maximum 15 dots)
	char			*lastdemosname[16];
	int				lastdemospos;
} demo_t;

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

// TCPCONNECT -->
typedef struct svtcpstream_s {
	int socketnum;
	int inlen;
	qbool waitingforprotocolconfirmation;
	char inbuffer[1500];
	float timeouttime;
	netadr_t remoteaddr;
	struct svtcpstream_s *next;
} svtcpstream_t;
// <-- TCPCONNECT

typedef struct
{
	int				spawncount;		// number of servers spawned since start,
						// used to check late spawns
	int				lastuserid;		// userid of last spawned client
	socket_t socketip;

// TCPCONNECT -->
	int sockettcp;
	svtcpstream_t *tcpstreams;
// <-- TCPCONNECT

	client_t		clients[MAX_CLIENTS];
	int				serverflags;		// episode completion information
	
	double			last_heartbeat;
	int				heartbeat_sequence;
	svstats_t		stats;

	char				info[MAX_SERVERINFO_STRING];

#ifdef PROTOCOL_VERSION_FTE
	unsigned int fteprotocolextensions;
#endif

#ifdef PROTOCOL_VERSION_FTE2
	unsigned int fteprotocolextensions2;
#endif

	// log messages are used so that fraglog processes can get stats
	int				logsequence;		// the message currently being filled
	double			logtime;		// time of last swap
	sizebuf_t		log[2];
	byte			log_buf[2][MAX_DATAGRAM];

	challenge_t		challenges[MAX_CHALLENGES];	// to prevent invalid IPs from connecting

	packet_t		*free_packets;
} server_static_t;

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

extern	server_static_t	svs;	// persistant server info
extern	server_t	sv;	// local server
extern	demo_t		demo;	// server demo struct
//extern	entity_state_t	cl_state_entities[MAX_CLIENTS][UPDATE_BACKUP][MAX_PACKET_ENTITIES]; // client entities

extern	client_t	*sv_client;
extern	edict_t		*sv_player;

#define	MODEL_NAME_LEN	5
extern	char		localmodels[MAX_MODELS][MODEL_NAME_LEN]; // inline model names for precache
//extern	char		localinfo[MAX_LOCALINFO_STRING+1];
extern  ctxinfo_t _localinfo_;

extern	int		host_hunklevel;

extern char		master_rcon_password[128];

extern qbool is_ktpro;

//===========================================================

//
// sv_main.c
//

extern	double	realtime; // not bounded in any way, changed at start of every frame, never reset

typedef enum {
	ipft_ban,
	ipft_safe
} ipfiltertype_t;

typedef struct
{
	unsigned	mask;
	unsigned	compare;
	int			level;
	double		time; // for ban expiration
	ipfiltertype_t type;
} ipfilter_t;

//bliP: penalty filters ->
typedef enum {
	ft_mute,
	ft_cuff
} filtertype_t;

typedef struct
{
	byte		ip[4];
	double		time;
	filtertype_t	type;
} penfilter_t;
//<-

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
int SV_GenerateUserID (void);

char *Q_normalizetext (char *name); //bliP: red to white text
unsigned char *Q_redtext (unsigned char *str); //bliP: white to red text
unsigned char *Q_yelltext (unsigned char *str); //VVD: white to red text and yellow numbers

#endif /* !__SERVER_H__ */
