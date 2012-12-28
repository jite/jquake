/*
   ===========================================================================
   Copyright (C) 1999-2005 Id Software, Inc.

   This file is part of Quake III Arena source code.

   Quake III Arena source code is free software; you can redistribute it
   and/or modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the License,
   or (at your option) any later version.

   Quake III Arena source code is distributed in the hope that it will be
   useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with Foobar; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
   ===========================================================================

 */
/*
 ** GLW_IMP.C
 **
 ** This file contains ALL Linux specific stuff having to do with the
 ** OpenGL refresh.  When a port is being made the following functions
 ** must be implemented by the port:
 **
 ** GLimp_EndFrame
 ** GLimp_Init
 ** GLimp_Shutdown
 ** GLimp_SwitchFullscreen
 **
 */

#include <termios.h>
#include <sys/ioctl.h>
#ifdef __FreeBSD__
#include <sys/stat.h>
#endif
#ifdef __linux__
#include <sys/stat.h>
#include <sys/vt.h>
#endif
#include <stdarg.h>
#include <stdio.h>
#include <signal.h>
#include <pthread.h>
#include <semaphore.h>

// bk001204
#include <dlfcn.h>

// bk001206 - from my Heretic2 by way of Ryan's Fakk2
// Needed for the new X11_PendingInput() function.
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include <GL/glx.h>

#include <X11/keysym.h>
#include <X11/cursorfont.h>
#include <X11/Xatom.h>
#include <X11/XKBlib.h>
#include <X11/xpm.h>

#include <X11/extensions/xf86vmode.h>
#include <X11/extensions/XInput2.h>

#include "ezquake.xpm"
#include "quakedef.h"
#include "keys.h"
#include "tr_types.h"
#include "input.h"
#include "rulesets.h"
#include "utils.h"

//
// cvars
//

int opengl_initialized = 0;

typedef enum { mt_none = 0, mt_normal } mousetype_t;

cvar_t in_mouse           = { "in_mouse",    "1", CVAR_ARCHIVE | CVAR_LATCH }; // NOTE: "1" is mt_normal
cvar_t in_nograb          = { "in_nograb",   "0", CVAR_LATCH }; // this is strictly for developers

cvar_t r_allowSoftwareGL  = { "vid_allowSoftwareGL", "0", CVAR_LATCH };   // don't abort out if the pixelformat claims software
static cvar_t vid_flashonactivity = {"vid_flashonactivity", "1"};

#define	WINDOW_CLASS_NAME	"ezQuake"

typedef enum
{
	RSERR_OK,
	RSERR_INVALID_FULLSCREEN,
	RSERR_INVALID_MODE,
	RSERR_UNKNOWN
} rserr_t;

glwstate_t glw_state;

static Display *dpy = NULL;
static int scrnum;
static Window win = 0;
static GLXContext ctx = NULL;

static Atom wm_delete_window_atom; //LordHavoc

static int shift_down;

#define KEY_MASK   ( KeyPressMask | KeyReleaseMask )
#define MOUSE_MASK ( ButtonPressMask | ButtonReleaseMask | PointerMotionMask | ButtonMotionMask )
#define X_MASK     ( VisibilityChangeMask | StructureNotifyMask | FocusChangeMask )

qbool mouseinitialized = false; // unfortunately non static, lame...
static qbool mouse_active = false;
int mx, my;

qbool vidmode_ext = false;
static int vidmode_MajorVersion = 0, vidmode_MinorVersion = 0; // major and minor of XF86VidExtensions

static XF86VidModeModeInfo **vidmodes;
//static int default_dotclock_vidmode; // bk001204 - unused
static int num_vidmodes;
static qbool vidmode_active = false;

qbool ActiveApp = true;
qbool Minimized = false;

static int xi_opcode;

static int (*swapInterval)(int); // Check if we support glXSwapIntervalSGI or perhaps MESA? 

//
// function declaration
//

void	 QGL_EnableLogging( qbool enable ) { /* TODO */ };

qbool QGL_Init( const char *dllname )
{
	ST_Printf( PRINT_ALL, "...initializing QGL\n" );

	qglActiveTextureARB       = 0;
	qglClientActiveTextureARB = 0;
	qglMultiTexCoord2fARB     = 0;

	return true;
}

void QGL_Shutdown( void ) {
	ST_Printf( PRINT_ALL, "...shutting down QGL\n" );
}

#if 0
/*
 * Find the first occurrence of find in s.
 */
// bk001130 - from cvs1.17 (mkv), const
// bk001130 - made first argument const
static const char *Q_stristr( const char *s, const char *find)
{
	register char c, sc;
	register size_t len;

	if ((c = *find++) != 0)
	{
		if (c >= 'a' && c <= 'z')
		{
			c -= ('a' - 'A');
		}
		len = strlen(find);
		do
		{
			do
			{
				if ((sc = *s++) == 0)
					return NULL;
				if (sc >= 'a' && sc <= 'z')
				{
					sc -= ('a' - 'A');
				}
			} while (sc != c);
		} while (strncasecmp(s, find, len) != 0);
		s--;
	}
	return s;
}
#endif
// ========================================================================
// makes a null cursor
// ========================================================================

static Cursor CreateNullCursor(Display *display, Window root)
{
	Pixmap cursormask;
	XGCValues xgc;
	GC gc;
	XColor dummycolour;
	Cursor cursor;

	cursormask = XCreatePixmap(display, root, 1, 1, 1/*depth*/);
	xgc.function = GXclear;
	gc =  XCreateGC(display, cursormask, GCFunction, &xgc);
	XFillRectangle(display, cursormask, gc, 0, 0, 1, 1);
	dummycolour.pixel = 0;
	dummycolour.red = 0;
	dummycolour.flags = 04;
	cursor = XCreatePixmapCursor(display, cursormask, cursormask,
			&dummycolour,&dummycolour, 0,0);
	XFreePixmap(display,cursormask);
	XFreeGC(display,gc);
	return cursor;
}

static void install_grabs(void)
{
	XDefineCursor(dpy, win, CreateNullCursor(dpy, win));
        XIEventMask mask;

        mask.deviceid = XIAllMasterDevices;
        mask.mask_len = XIMaskLen(XI_LASTEVENT);
        mask.mask = calloc(mask.mask_len, sizeof(char));
        XISetMask(mask.mask, XI_KeyPress);
        XISetMask(mask.mask, XI_KeyRelease);
        XISetMask(mask.mask, XI_ButtonPress);
        XISetMask(mask.mask, XI_ButtonRelease);
        XISelectEvents(dpy, win, &mask, 1);
        XISetMask(mask.mask, XI_Enter);
        XISetMask(mask.mask, XI_Leave);
        XISetMask(mask.mask, XI_ButtonPress);
        XISetMask(mask.mask, XI_ButtonRelease);

        int i;
        int num_devices;
        XIDeviceInfo *info;
        info = XIQueryDevice(dpy, XIAllDevices, &num_devices);
        for(i = 0; i < num_devices; i++) {
                int id = info[i].deviceid;
                if(info[i].use == XISlavePointer) {
                        mask.deviceid = id;
                        XIGrabDevice(dpy, id, win, CurrentTime, None, GrabModeSync,
                                        GrabModeSync, True, &mask);
                }
                else if(info[i].use == XIMasterPointer) {
                        /* FIXME: Ugly hack, XIWarpPointer is fucking with me if using fullscreen.. I get 
                         *        like "scrolling of screen" like cursor window is bigger than the gl window */
                        if (r_fullscreen.integer)
                                XIWarpPointer(dpy, id, None, win, 0, 0, 0, 0, 0, 0);
                        else
                                XIWarpPointer(dpy, id, None, win, 0, 0, 0, 0, (double)glConfig.vidWidth/2, (double)glConfig.vidHeight/2);
                }
                else if(info[i].use == XIMasterKeyboard)
                {
                        Con_DPrintf("input: grabbing master keyboard...\n");
                        XIGrabDevice(dpy, id, win, CurrentTime, None, GrabModeAsync, GrabModeAsync, False, &mask);
                }
        }
        XIFreeDeviceInfo(info);

        mask.deviceid = XIAllDevices;
        memset(mask.mask, 0, mask.mask_len);
        XISetMask(mask.mask, XI_RawMotion);

        XISelectEvents(dpy, DefaultRootWindow(dpy), &mask, 1);

        free(mask.mask);

        XSync(dpy, True);
}

static void uninstall_grabs(void)
{
        XUndefineCursor(dpy, win);
        int i;
        int num_devices;
        XIDeviceInfo *info;
        info = XIQueryDevice(dpy, XIAllDevices, &num_devices);

        for(i = 0; i < num_devices; i++) {
                if(info[i].use == XIFloatingSlave || info[i].use == XIMasterKeyboard) {
                        XIUngrabDevice(dpy, info[i].deviceid, CurrentTime);
                }
                else if(info[i].use == XIMasterPointer) {
                        XIWarpPointer(dpy, info[i].deviceid, None, win, 0, 0, 0, 0,
                                        glConfig.vidWidth / 2, glConfig.vidHeight / 2);
                }
        }
        XIFreeDeviceInfo(info);
}

void IN_Commands (void) {
}

void IN_StartupMouse(void) {

	Cvar_SetCurrentGroup(CVAR_GROUP_INPUT_MOUSE);
	// mouse variables
	Cvar_Register (&in_mouse);
	// developer feature, allows to break without loosing mouse pointer
	Cvar_Register (&in_nograb);
	Cvar_ResetCurrentGroup();

	switch (in_mouse.integer) {
		case mt_none:   mouseinitialized = false; break;
		case mt_normal: mouseinitialized = true;  break;
		default:
				Com_Printf("Unknow value %d of %s, using XInput2 mouse\n", in_mouse.integer, in_mouse.name);
				Cvar_LatchedSetValue(&in_mouse, mt_normal);
				mouseinitialized = true;
				break;
	}
}

void IN_ActivateMouse( void )
{
	if (!mouseinitialized || !dpy || !win || mouse_active)
		return;

	if (!in_nograb.value)
		install_grabs();
	mouse_active = true;
}

void IN_DeactivateMouse( void )
{
	if (!mouseinitialized || !dpy || !win || !mouse_active)
		return;

	if (!in_nograb.value)
		uninstall_grabs();
	mouse_active = false;
}

void IN_Frame (void) {

	if (!r_fullscreen.integer && (key_dest != key_game || cls.state != ca_active))
	{
		// temporarily deactivate if not in the game and
		// running on the desktop
		IN_DeactivateMouse ();
	}
	else
	{
		IN_ActivateMouse();
	}
}

void IN_Restart_f(void)
{
	qbool old_mouse_active = mouse_active;

	IN_Shutdown();
	IN_Init();

	// if mouse was active before restart, try to re-activate it
	if (old_mouse_active)
		IN_ActivateMouse();
}

static char *XLateKey(int keycode, int *key) {
        KeySym keysym;

        keysym = XkbKeycodeToKeysym(dpy, keycode, 0, 0); /* Don't care about shift state for in game keycode, but... */
        int kp = cl_keypad.value;

        switch(keysym) {
                case XK_Print:                  *key = K_PRINTSCR; break;
                case XK_Scroll_Lock:            *key = K_SCRLCK; break;

                case XK_Caps_Lock:              *key = K_CAPSLOCK; break;

                case XK_Num_Lock:               *key = kp ? KP_NUMLOCK : K_PAUSE; break;

                case XK_KP_Page_Up:             *key = kp ? KP_PGUP : K_PGUP; break;
                case XK_Page_Up:                *key = K_PGUP; break;

                case XK_KP_Page_Down:           *key = kp ? KP_PGDN : K_PGDN; break;
                case XK_Page_Down:              *key = K_PGDN; break;

                case XK_KP_Home:                *key = kp ? KP_HOME : K_HOME; break;
                case XK_Home:                   *key = K_HOME; break;

                case XK_KP_End:                 *key = kp ? KP_END : K_END; break;
                case XK_End:                    *key = K_END; break;

                case XK_KP_Left:                *key = kp ? KP_LEFTARROW : K_LEFTARROW; break;
                case XK_Left:                   *key = K_LEFTARROW; break;

                case XK_KP_Right:               *key = kp ? KP_RIGHTARROW : K_RIGHTARROW; break;
                case XK_Right:                  *key = K_RIGHTARROW; break;

                case XK_KP_Down:                *key = kp ? KP_DOWNARROW : K_DOWNARROW; break;

                case XK_Down:                   *key = K_DOWNARROW; break;

                case XK_KP_Up:                  *key = kp ? KP_UPARROW : K_UPARROW; break;

                case XK_Up:                     *key = K_UPARROW; break;

                case XK_Escape:                 *key = K_ESCAPE; break;

                case XK_KP_Enter:               *key = kp ? KP_ENTER : K_ENTER; break;

                case XK_Return:                 *key = K_ENTER; break;

                case XK_Tab:                    *key = K_TAB; break;

                case XK_F1:                     *key = K_F1; break;
                case XK_F2:                     *key = K_F2; break;
                case XK_F3:                     *key = K_F3; break;
                case XK_F4:                     *key = K_F4; break;
                case XK_F5:                     *key = K_F5; break;
                case XK_F6:                     *key = K_F6; break;
                case XK_F7:                     *key = K_F7; break;
                case XK_F8:                     *key = K_F8; break;
                case XK_F9:                     *key = K_F9; break;
                case XK_F10:                    *key = K_F10; break;
                case XK_F11:                    *key = K_F11; break;
                case XK_F12:                    *key = K_F12; break;

                case XK_BackSpace:              *key = K_BACKSPACE; break;

                case XK_KP_Delete:              *key = kp ? KP_DEL : K_DEL; break;
                case XK_Delete:                 *key = K_DEL; break;

                case XK_Pause:                  *key = K_PAUSE; break;

                case XK_Shift_L:                *key = K_LSHIFT; break;
                case XK_Shift_R:                *key = K_RSHIFT; break;

                case XK_Execute:
                case XK_Control_L:              *key = K_LCTRL; break;
                case XK_Control_R:              *key = K_RCTRL; break;

                case XK_Alt_L:
                case XK_Meta_L:                 *key = K_LALT; break;
                case XK_Alt_R:
                case XK_ISO_Level3_Shift:
                case XK_Meta_R:                 *key = K_RALT; break;
                case XK_Super_L:                *key = K_LWIN; break;
                case XK_Super_R:                *key = K_RWIN; break;
                case XK_Multi_key:              *key = K_RWIN; break;
                case XK_Menu:                   *key = K_MENU; break;

                case XK_section:                *key = K_SECTION; break;
                case XK_acute:
                case XK_dead_acute:             *key = K_ACUTE; break;
                case XK_diaeresis:
                case XK_dead_diaeresis:         *key = K_DIAERESIS; break;

                case XK_aring:                  *key = K_ARING; break;
                case XK_adiaeresis:             *key = K_ADIAERESIS; break;
                case XK_odiaeresis:             *key = K_ODIAERESIS; break;

                case XK_KP_Begin:               *key = kp ? KP_5 : '5'; break;

                case XK_KP_Insert:              *key = kp ? KP_INS : K_INS; break;
                case XK_Insert:                 *key = K_INS; break;

                case XK_KP_Multiply:            *key = kp ? KP_STAR : '*'; break;

                case XK_KP_Add:                 *key = kp ? KP_PLUS : '+'; break;

                case XK_KP_Subtract:            *key = kp ? KP_MINUS : '-'; break;

                case XK_KP_Divide:              *key = kp ? KP_SLASH : '/'; break;


                default:
                        if (keysym >= 32 && keysym <= 126) {
                                *key = keysym;
                        }
                        break;
        }
        /* ... if we're in console or chatting, please activate SHIFT */
        keysym = XkbKeycodeToKeysym(dpy, keycode, 0, shift_down);

        /* this is stupid, there must exist a better way */
        switch(keysym) {
                case XK_bracketleft:  return "[";
                case XK_bracketright: return "]";
                case XK_parenleft:    return "(";
                case XK_parenright:   return ")";
                case XK_braceleft:    return "{";
                case XK_braceright:   return "}";
                case XK_space:        return " ";
                case XK_asciitilde:   return "~";
                case XK_grave:        return "`";
                case XK_exclam:       return "!";
                case XK_at:           return "@";
                case XK_numbersign:   return "#";
                case XK_dollar:       return "$";
                case XK_percent:      return "%";
                case XK_asciicircum:  return "^";
                case XK_ampersand:    return "&";
                case XK_asterisk:     return "*";
                case XK_minus:        return "-";
                case XK_underscore:   return "_";
                case XK_equal:        return "=";
                case XK_plus:         return "+";
                case XK_semicolon:    return ";";
                case XK_colon:        return ":";
                case XK_apostrophe:   return "'";
                case XK_quotedbl:     return "\"";
                case XK_backslash:    return "\\";
                case XK_bar:          return "|";
                case XK_comma:        return ",";
                case XK_period:       return ".";
                case XK_less:         return "<";
                case XK_greater:      return ">";
                case XK_slash:        return "/";
                case XK_question:     return "?";
                default:              if (XKeysymToString(keysym))
		                              return XKeysymToString(keysym);
		                      else
		                              return "";
        }

}

static void handle_button(XGenericEventCookie *cookie)
{
        int down = cookie->evtype == XI_ButtonPress;
        int button = ((XIDeviceEvent *)cookie->data)->detail;
        int k_button;

        switch(button) {
        case 1:  k_button = K_MOUSE1;      break;
        case 2:  k_button = K_MOUSE3;      break;
        case 3:  k_button = K_MOUSE2;      break;
        case 4:  k_button = K_MWHEELUP;    break;
        case 5:  k_button = K_MWHEELDOWN;  break;
        /* Switch place of MOUSE4-5 with MOUSE6-7 */
        case 6:  k_button = K_MOUSE6;      break;
        case 7:  k_button = K_MOUSE7;      break;
        case 8:  k_button = K_MOUSE4;      break;
        case 9:  k_button = K_MOUSE5;      break;
        /* End switch */
        case 10: k_button = K_MOUSE8;      break;
        default: return;
        }

        Key_Event(k_button, down);
}

static void handle_key(XGenericEventCookie *cookie)
{
        int down = cookie->evtype == XI_KeyPress;
        int keycode = ((XIDeviceEvent *)cookie->data)->detail;

        int key = 0;

        const char *name = XLateKey(keycode, &key);

        if (key == K_SHIFT || key == K_LSHIFT || key == K_RSHIFT)
                shift_down = down;

        Key_EventEx(key, strlen(name) == 1 ? name[0] : 0, down);

}

static void handle_raw_motion(XIRawEvent *ev)
{
        if(!mouse_active)
                return;

        double *raw_valuator = ev->raw_values;

        if(XIMaskIsSet(ev->valuators.mask, 0)) {
                mx += *raw_valuator++;
        }

        if(XIMaskIsSet(ev->valuators.mask, 1)) {
                my += *raw_valuator++;
        }

}

static void handle_cookie(XGenericEventCookie *cookie)
{
        switch(cookie->evtype) {
        case XI_RawMotion:
                handle_raw_motion(cookie->data);
                break;
        case XI_Enter:
        case XI_Leave:
                break;
        case XI_ButtonPress:
        case XI_ButtonRelease:
                handle_button(cookie);
                break;
        case XI_KeyPress:
        case XI_KeyRelease:
                handle_key(cookie);
                break;
        default:
                break;
        }
}

static void HandleEvents(void)
{
        XEvent event;

        if (!dpy)
                return;


        while (XPending(dpy))
        {
                XGenericEventCookie *cookie = &event.xcookie;
                XNextEvent(dpy, &event);

                if(cookie->type == GenericEvent && cookie->extension == xi_opcode
                                && XGetEventData(dpy, cookie)) {
                        handle_cookie(cookie);
                        XFreeEventData(dpy, cookie);
                        continue;
                }

                switch (event.type)
                {
                        case DestroyNotify:
                                // window has been destroyed
                                Host_Quit();
                                break;

                        case ClientMessage:
                                // window manager messages
                                if ((event.xclient.format == 32) && ((unsigned int)event.xclient.data.l[0] == wm_delete_window_atom))
                                        Host_Quit();
                                break;

                        case FocusIn:
                                if (!ActiveApp)
                                {
                                        XWMHints wmhints;
                                        ActiveApp = true;
                                        // CLear urgency bit
                                        wmhints.flags = 0;
                                        XSetWMHints( dpy, win, &wmhints );
                                }
                                break;

                        case FocusOut:
                                if (ActiveApp)
                                {
                                        Key_ClearStates();
                                        ActiveApp = false;
                                        shift_down = 0;
                                }
                                break;

                        case MapNotify:
                                Minimized = false;
                                break;

                        case UnmapNotify:
                                Minimized = true;
                                break;

                }
        }

}

void Sys_SendKeyEvents (void) {
	// XEvent event; // bk001204 - unused

	if (!dpy)
		return;

	IN_Frame();
	HandleEvents();
}


/*****************************************************************************/

/*
 ** GLimp_Shutdown
 **
 ** This routine does all OS specific shutdown procedures for the OpenGL
 ** subsystem.  Under OpenGL this means NULLing out the current DC and
 ** HGLRC, deleting the rendering context, and releasing the DC acquired
 ** for the window.  The state structure is also nulled out.
 **
 */
void GLimp_Shutdown( void )
{
	if (!ctx || !dpy)
		return;
	IN_DeactivateMouse();
	opengl_initialized = 0;
	// bk001206 - replaced with H2/Fakk2 solution
	// XAutoRepeatOn(dpy);
	// autorepeaton = false; // bk001130 - from cvs1.17 (mkv)
	if (dpy)
	{
		if (ctx)
			glXDestroyContext(dpy, ctx);

		if (win)
			XDestroyWindow(dpy, win);

		if (vidmode_active)
		{
			XF86VidModeSwitchToMode(dpy, scrnum, vidmodes[0]);
			XFlush(dpy);
		}

		// NOTE TTimo opening/closing the display should be necessary only once per run
		//   but it seems QGL_Shutdown gets called in a lot of occasion
		//   in some cases, this XCloseDisplay is known to raise some X errors
		//   ( https://zerowing.idsoftware.com/bugzilla/show_bug.cgi?id=33 )
		XCloseDisplay(dpy);
	}
	vidmode_active = false;
	dpy = NULL;
	win = 0;
	ctx = NULL;

	memset( &glConfig, 0, sizeof( glConfig ) );

	QGL_Shutdown();
}

/*
 ** GLW_StartDriverAndSetMode
 */
// bk001204 - prototype needed
int GLW_SetMode( const char *drivername, int mode, qbool fullscreen );
static qbool GLW_StartDriverAndSetMode( const char *drivername,
		int mode,
		qbool fullscreen )
{
	rserr_t err;

	if (fullscreen && in_nograb.value)
	{
		ST_Printf( PRINT_ALL, "Fullscreen not allowed with in_nograb 1\n");
		Cvar_Set( &r_fullscreen, "0" );
		r_fullscreen.modified = false;
		fullscreen = false;
	}

	err = GLW_SetMode( drivername, mode, fullscreen );

	switch ( err )
	{
		case RSERR_INVALID_FULLSCREEN:
			ST_Printf( PRINT_ALL, "...WARNING: fullscreen unavailable in this mode\n" );
			return false;
		case RSERR_INVALID_MODE:
			ST_Printf( PRINT_ALL, "...WARNING: could not set the given mode (%d)\n", mode );
			return false;
		case RSERR_UNKNOWN:
			return false;
		default:
			break;
	}
	opengl_initialized = 1;
	return true;
}

/*
 ** GLW_SetMode
 */
int GLW_SetMode( const char *drivername, int mode, qbool fullscreen )
{
	int attrib[] = {
		GLX_RGBA,         // 0
		GLX_RED_SIZE, 4,      // 1, 2
		GLX_GREEN_SIZE, 4,      // 3, 4
		GLX_BLUE_SIZE, 4,     // 5, 6
		GLX_DOUBLEBUFFER,     // 7
		GLX_DEPTH_SIZE, 1,      // 8, 9
		GLX_STENCIL_SIZE, 1,    // 10, 11
		None
	};
	// these match in the array
#define ATTR_RED_IDX 2
#define ATTR_GREEN_IDX 4
#define ATTR_BLUE_IDX 6
#define ATTR_DEPTH_IDX 9
#define ATTR_STENCIL_IDX 11
	Window root;
	XVisualInfo *visinfo;
	XSetWindowAttributes attr;
	XSizeHints sizehints;
	XWMHints wmhints;
	Pixmap icon_pixmap, icon_mask;
	unsigned long mask;
	int colorbits, depthbits, stencilbits;
	int tcolorbits, tdepthbits, tstencilbits;
	int actualWidth, actualHeight;
	int event, error;
	int i;
	const char*   glstring; // bk001130 - from cvs1.17 (mkv)

	ST_Printf( PRINT_ALL, "Initializing OpenGL display\n");

	ST_Printf( PRINT_ALL, "...setting mode %d:", mode );

	if ( !R_GetModeInfo( &glConfig.vidWidth, &glConfig.vidHeight, &glConfig.windowAspect, mode ) )
	{
		ST_Printf( PRINT_ALL, " invalid mode\n" );
		return RSERR_INVALID_MODE;
	}
	ST_Printf( PRINT_ALL, " %d %d\n", glConfig.vidWidth, glConfig.vidHeight);

	if (!(dpy = XOpenDisplay(NULL)))
	{
		fprintf(stderr, "Error couldn't open the X display\n");
		return RSERR_INVALID_MODE;
	}

	scrnum = DefaultScreen(dpy);
	root = RootWindow(dpy, scrnum);

	actualWidth = glConfig.vidWidth;
	actualHeight = glConfig.vidHeight;

	// Get video mode list
	if (!XF86VidModeQueryVersion(dpy, &vidmode_MajorVersion, &vidmode_MinorVersion))
	{
		vidmode_ext = false;
	} else
	{
		ST_Printf(PRINT_ALL, "Using XFree86-VidModeExtension Version %d.%d\n",
				vidmode_MajorVersion, vidmode_MinorVersion);
		vidmode_ext = true;
	}

	if (vidmode_ext)
	{
		int best_fit, best_dist, dist, x, y;

		XF86VidModeGetAllModeLines(dpy, scrnum, &num_vidmodes, &vidmodes);

		// Are we going fullscreen?  If so, let's change video mode
		if (fullscreen)
		{
			best_dist = 9999999;
			best_fit = -1;

			for (i = 0; i < num_vidmodes; i++)
			{
				if (glConfig.vidWidth > vidmodes[i]->hdisplay ||
						glConfig.vidHeight > vidmodes[i]->vdisplay)
					continue;

				x = glConfig.vidWidth - vidmodes[i]->hdisplay;
				y = glConfig.vidHeight - vidmodes[i]->vdisplay;
				dist = (x * x) + (y * y);
				if (dist < best_dist)
				{
					best_dist = dist;
					best_fit = i;
				}
			}

			if (best_fit != -1)
			{
				actualWidth = vidmodes[best_fit]->hdisplay;
				actualHeight = vidmodes[best_fit]->vdisplay;

				// change to the mode
				XF86VidModeSwitchToMode(dpy, scrnum, vidmodes[best_fit]);
				vidmode_active = true;

				// Move the viewport to top left
				XF86VidModeSetViewPort(dpy, scrnum, 0, 0);

				ST_Printf(PRINT_ALL, "XFree86-VidModeExtension Activated at %dx%d\n",
						actualWidth, actualHeight);

			} else
			{
				fullscreen = 0;
				ST_Printf(PRINT_ALL, "XFree86-VidModeExtension: No acceptable modes found\n");
			}
		} else
		{
			ST_Printf(PRINT_ALL, "XFree86-VidModeExtension:  Ignored on non-fullscreen/Voodoo\n");
		}
	}


	if (!r_colorbits.value)
		colorbits = 24;
	else
		colorbits = r_colorbits.value;

	if ( !strcasecmp( r_glDriver.string, _3DFX_DRIVER_NAME ) )
		colorbits = 16;

	if (!r_depthbits.value)
		depthbits = 24;
	else
		depthbits = r_depthbits.value;
	stencilbits = r_stencilbits.value;

	for (i = 0; i < 16; i++)
	{
		// 0 - default
		// 1 - minus colorbits
		// 2 - minus depthbits
		// 3 - minus stencil
		if ((i % 4) == 0 && i)
		{
			// one pass, reduce
			switch (i / 4)
			{
				case 2 :
					if (colorbits == 24)
						colorbits = 16;
					break;
				case 1 :
					if (depthbits == 24)
						depthbits = 16;
					else if (depthbits == 16)
						depthbits = 8;
				case 3 :
					if (stencilbits == 24)
						stencilbits = 16;
					else if (stencilbits == 16)
						stencilbits = 8;
			}
		}

		tcolorbits = colorbits;
		tdepthbits = depthbits;
		tstencilbits = stencilbits;

		if ((i % 4) == 3)
		{ // reduce colorbits
			if (tcolorbits == 24)
				tcolorbits = 16;
		}

		if ((i % 4) == 2)
		{ // reduce depthbits
			if (tdepthbits == 24)
				tdepthbits = 16;
			else if (tdepthbits == 16)
				tdepthbits = 8;
		}

		if ((i % 4) == 1)
		{ // reduce stencilbits
			if (tstencilbits == 24)
				tstencilbits = 16;
			else if (tstencilbits == 16)
				tstencilbits = 8;
			else
				tstencilbits = 0;
		}

		if (tcolorbits == 24)
		{
			attrib[ATTR_RED_IDX] = 8;
			attrib[ATTR_GREEN_IDX] = 8;
			attrib[ATTR_BLUE_IDX] = 8;
		} else
		{
			// must be 16 bit
			attrib[ATTR_RED_IDX] = 4;
			attrib[ATTR_GREEN_IDX] = 4;
			attrib[ATTR_BLUE_IDX] = 4;
		}

		attrib[ATTR_DEPTH_IDX] = tdepthbits; // default to 24 depth
		attrib[ATTR_STENCIL_IDX] = tstencilbits;

		visinfo = glXChooseVisual(dpy, scrnum, attrib);
		if (!visinfo)
		{
			continue;
		}

		ST_Printf( PRINT_ALL, "Using %d/%d/%d Color bits, %d depth, %d stencil display.\n",
				attrib[ATTR_RED_IDX], attrib[ATTR_GREEN_IDX], attrib[ATTR_BLUE_IDX],
				attrib[ATTR_DEPTH_IDX], attrib[ATTR_STENCIL_IDX]);

		glConfig.colorBits = tcolorbits;
		glConfig.depthBits = tdepthbits;
		glConfig.stencilBits = tstencilbits;
		break;
	}

	if (!visinfo)
	{
		ST_Printf( PRINT_ALL, "Couldn't get a visual\n" );
		return RSERR_INVALID_MODE;
	}

	/* window attributes */
	attr.background_pixel = BlackPixel(dpy, scrnum);
	attr.border_pixel = 0;
	attr.colormap = XCreateColormap(dpy, root, visinfo->visual, AllocNone);
	attr.event_mask = X_MASK;
	if (vidmode_active)
	{
		mask = CWBackPixel | CWColormap | CWSaveUnder | CWBackingStore |
			CWEventMask | CWOverrideRedirect;
		attr.override_redirect = True;
		attr.backing_store = NotUseful;
		attr.save_under = False;
	} else
		mask = CWBackPixel | CWBorderPixel | CWColormap | CWEventMask;

	win = XCreateWindow(dpy, root, 0, 0,
			actualWidth, actualHeight,
			0, visinfo->depth, InputOutput,
			visinfo->visual, mask, &attr);

	XStoreName( dpy, win, WINDOW_CLASS_NAME );

	/* GH: Don't let the window be resized */
	sizehints.flags = PMinSize | PMaxSize;
	sizehints.min_width = sizehints.max_width = actualWidth;
	sizehints.min_height = sizehints.max_height = actualHeight;

	XSetWMNormalHints( dpy, win, &sizehints );

	XpmCreatePixmapFromData( dpy, root, ezquake_xpm__, &icon_pixmap, &icon_mask, NULL );
	wmhints.icon_pixmap = icon_pixmap;
	wmhints.icon_mask = icon_mask;
	wmhints.flags = IconPixmapHint | IconMaskHint;
	XSetWMHints( dpy, win, &wmhints );

	XMapWindow( dpy, win );

	// LordHavoc: making the close button on a window do the right thing
	// seems to involve this mess, sigh...
	// {
	wm_delete_window_atom = XInternAtom(dpy, "WM_DELETE_WINDOW", false);
	XSetWMProtocols(dpy, win, &wm_delete_window_atom, 1);
	// }

	if (vidmode_active)
		XMoveWindow(dpy, win, 0, 0);

	XFlush(dpy);
	XSync(dpy,False); // bk001130 - from cvs1.17 (mkv)
	ctx = glXCreateContext(dpy, visinfo, NULL, True);
	XSync(dpy,False); // bk001130 - from cvs1.17 (mkv)

	/* GH: Free the visinfo after we're done with it */
	XFree( visinfo );

	glXMakeCurrent(dpy, win, ctx);

	// bk001130 - from cvs1.17 (mkv)
	glstring = (char*)glGetString (GL_RENDERER);
	ST_Printf( PRINT_ALL, "GL_RENDERER: %s\n", glstring );

	// bk010122 - new software token (Indirect)
	if ( !strcasecmp( glstring, "Mesa X11") || !strcasecmp( glstring, "Mesa GLX Indirect") )
	{
		if ( !r_allowSoftwareGL.integer )
		{
			ST_Printf( PRINT_ALL, "\n\n***********************************************************\n" );
			ST_Printf( PRINT_ALL, " You are using software Mesa (no hardware acceleration)!   \n" );
			ST_Printf( PRINT_ALL, " Driver DLL used: %s\n", drivername );
			ST_Printf( PRINT_ALL, " If this is intentional, add\n" );
			ST_Printf( PRINT_ALL, "       \"+set r_allowSoftwareGL 1\"\n" );
			ST_Printf( PRINT_ALL, " to the command line when starting the game.\n" );
			ST_Printf( PRINT_ALL, "***********************************************************\n");
			GLimp_Shutdown( );
			return RSERR_INVALID_MODE;
		} else
		{
			ST_Printf( PRINT_ALL, "...using software Mesa (r_allowSoftwareGL==1).\n" );
		}
	}

	glConfig.isFullscreen	= fullscreen; // qqshka: this line absent in q3, dunno is this correct...

	if(!XQueryExtension(dpy, "XInputExtension", &xi_opcode, &event, &error))
	{
		ST_Printf(PRINT_ALL, "ERROR: XInput Extension not available.\n");
		return RSERR_UNKNOWN;
	}

	install_grabs();

	mouse_active = true; /* ?? */

	return RSERR_OK;
}

/*
 ** GLW_LoadOpenGL
 **
 ** GLimp_win.c internal function that that attempts to load and use
 ** a specific OpenGL DLL.
 */
static qbool GLW_LoadOpenGL( const char *name )
{
	qbool fullscreen;

	// load the QGL layer
	if ( QGL_Init( name ) )
	{
		fullscreen = r_fullscreen.integer;

		// create the window and set up the context
		if ( !GLW_StartDriverAndSetMode( name, r_mode.integer, fullscreen ) )
		{
			if (r_mode.integer != 3)
			{
				if ( !GLW_StartDriverAndSetMode( name, 3, fullscreen ) )
				{
					goto fail;
				}
			} else
				goto fail;
		}

		return true;
	} else
	{
		ST_Printf( PRINT_ALL, "failed\n" );
	}
fail:

	QGL_Shutdown();

	return false;
}

/*
 ** XErrorHandler
 **   the default X error handler exits the application
 **   I found out that on some hosts some operations would raise X errors (GLXUnsupportedPrivateRequest)
 **   but those don't seem to be fatal .. so the default would be to just ignore them
 **   our implementation mimics the default handler behaviour (not completely cause I'm lazy)
 */
int qXErrorHandler(Display *dpy, XErrorEvent *ev)
{
	static char buf[1024];
	XGetErrorText(dpy, ev->error_code, buf, 1024);
	ST_Printf( PRINT_ALL, "X Error of failed request: %s\n", buf);
	ST_Printf( PRINT_ALL, "  Major opcode of failed request: %d\n", ev->request_code, buf);
	ST_Printf( PRINT_ALL, "  Minor opcode of failed request: %d\n", ev->minor_code);
	ST_Printf( PRINT_ALL, "  Serial number of failed request: %d\n", ev->serial);
	return 0;
}

/*
 ** GLimp_Init
 **
 ** This routine is responsible for initializing the OS specific portions
 ** of OpenGL.
 */
void GLimp_Init( void )
{
	extern void InitSig(void);

	qbool attemptedlibGL = false;
	qbool success = false;
	char  buf[1024];
	//  cvar_t *lastValidRenderer = ri.Cvar_Get( "vid_lastValidRenderer", "(uninitialized)", CVAR_ARCHIVE );

	Cvar_SetCurrentGroup(CVAR_GROUP_VIDEO);
	Cvar_Register (&vid_flashonactivity);
	Cvar_Register (&r_allowSoftwareGL);
	Cvar_ResetCurrentGroup();

	InitSig();

	// set up our custom error handler for X failures
	XSetErrorHandler(&qXErrorHandler);

	//
	// load and initialize the specific OpenGL driver
	//
	if ( !GLW_LoadOpenGL( r_glDriver.string ) )
	{
		if ( !strcasecmp( r_glDriver.string, OPENGL_DRIVER_NAME ) )
		{
			attemptedlibGL = true;
		}

		// try ICD before trying 3Dfx standalone driver
		if ( !attemptedlibGL && !success )
		{
			attemptedlibGL = true;
			if ( GLW_LoadOpenGL( OPENGL_DRIVER_NAME ) )
			{
				Cvar_Set( &r_glDriver, OPENGL_DRIVER_NAME );
				r_glDriver.modified = false;
				success = true;
			}
		}

		if (!success)
			ST_Printf(PRINT_ERR_FATAL, "GLimp_Init() - could not load OpenGL subsystem\n");
	}

	glConfig.vendor_string         = glGetString(GL_VENDOR);
	glConfig.renderer_string       = glGetString(GL_RENDERER);
	glConfig.version_string        = glGetString(GL_VERSION);
	glConfig.extensions_string     = glGetString(GL_EXTENSIONS);
	glConfig.glx_extensions_string = glXQueryExtensionsString(dpy, scrnum);

	//
	// NOTE: if changing cvars, do it within this block.  This allows them
	// to be overridden when testing driver fixes, etc. but only sets
	// them to their default state when the hardware is first installed/run.
	//

	// Look up vsync-stuff
        if (strstr(glConfig.glx_extensions_string, "GLX_SGI_swap_control"))
                swapInterval = (int (*)(int)) glXGetProcAddress((const GLubyte*) "glXSwapIntervalSGI");
        else if (strstr(glConfig.glx_extensions_string, "GLX_MESA_swap_control"))
                swapInterval = (int (*)(int)) glXGetProcAddress((const GLubyte*) "glXSwapIntervalMESA");
        else
                swapInterval = NULL;

        r_swapInterval.modified = true; /* Force re-set of vsync value */
	
	InitSig(); // not clear why this is at begin & end of function
}


void GL_BeginRendering (int *x, int *y, int *width, int *height) {
	*x = *y = 0;
	*width  = glConfig.vidWidth;
	*height = glConfig.vidHeight;

	if (cls.state != ca_active)
		glClear (GL_COLOR_BUFFER_BIT);
}

void GL_EndRendering (void)
{
	if (r_swapInterval.modified) {
                if (!swapInterval)
                        Con_Printf("Warning: No vsync handler found...\n");
                else
                {
                        if (r_swapInterval.integer > 0) {
                                if (swapInterval(1)) {
                                        Con_Printf("vsync: Failed to enable vsync...\n");
                                }
                        }
                        else if (r_swapInterval.integer <= 0) {
                                if (swapInterval(0)) {
                                        Con_Printf("vsync: Failed to disable vsync...\n");
                                }
                        }
                        r_swapInterval.modified = false;
                }
        }


	if (!scr_skipupdate || block_drawing) {

		// Multiview - Only swap the back buffer to front when all views have been drawn in multiview.
		if (cl_multiview.value && cls.mvdplayback) 
		{
			if (CURRVIEW == 1)
			{
				GLimp_EndFrame();
			}
		}
		else 
		{
			// Normal, swap on each frame.
			GLimp_EndFrame(); 
		}
	}
}


/*
 ** GLimp_EndFrame
 **
 ** Responsible for doing a swapbuffers and possibly for other stuff
 ** as yet to be determined.  Probably better not to make this a GLimp
 ** function and instead do a call to GLimp_SwapBuffers.
 */
void GLimp_EndFrame (void)
{
	glXSwapBuffers(dpy, win);
}

/************************************* Window related *******************************/

void VID_SetCaption (char *text)
{
	if (!dpy)
		return;
	XStoreName (dpy, win, text);
}

void VID_NotifyActivity(void) {
	XWMHints wmhints;

	if (!dpy)
		return;

	if (ActiveApp || !vid_flashonactivity.value)
		return;

	wmhints.flags = XUrgencyHint;
	XSetWMHints( dpy, win, &wmhints );
}

/********************************* CLIPBOARD *********************************/

#define SYS_CLIPBOARD_SIZE		256
static wchar clipboard_buffer[SYS_CLIPBOARD_SIZE] = {0};

wchar *Sys_GetClipboardTextW(void)
{
	// in warsow, it depends on is it CTRL-V or SHIFT-INS
	qbool primary = ((keydown[K_INS] || keydown[KP_INS]) && keydown[K_SHIFT]);

	Window win;
	Atom type;
	int format, ret;
	unsigned long nitems, bytes_after, bytes_left;
	unsigned char *data;
	wchar *s, *t;
	Atom atom;

	if( !dpy )
		return NULL;

	if( primary )
	{
		atom = XInternAtom( dpy, "PRIMARY", True );
	}
	else
	{
		atom = XInternAtom( dpy, "CLIPBOARD", True );
	}
	if( atom == None )
		return NULL;

	win = XGetSelectionOwner( dpy, atom );
	if( win == None )
		return NULL;

	XConvertSelection( dpy, atom, XA_STRING, atom, win, CurrentTime );
	XFlush( dpy );

	XGetWindowProperty( dpy, win, atom, 0, 0, False, AnyPropertyType, &type, &format, &nitems, &bytes_left,
			&data );
	if( bytes_left <= 0 )
		return NULL;

	ret = XGetWindowProperty( dpy, win, atom, 0, bytes_left, False, AnyPropertyType, &type,
			&format, &nitems, &bytes_after, &data );
	if( ret != Success )
	{
		XFree( data );
		return NULL;
	}

	s = str2wcs((char *)data);
	t = clipboard_buffer;
	// we stop pasting if found particular chars, perhaps that no so smart, we may replace they with space?
	// however in windows we do the same, so...
	while (*s && t - clipboard_buffer < SYS_CLIPBOARD_SIZE - 1 && *s != '\n' && *s != '\r' && *s != '\b')
		*t++ = *s++;
	*t = 0;

	XFree( data );

	return clipboard_buffer;
}

void Sys_CopyToClipboard(char *text)
{
	; // TODO in 2019
}

