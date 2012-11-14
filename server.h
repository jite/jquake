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

/* MOVE ME PLZ */
char *Q_normalizetext (char *name); //bliP: red to white text
unsigned char *Q_redtext (unsigned char *str); //bliP: white to red text
unsigned char *Q_yelltext (unsigned char *str); //VVD: white to red text and yellow numbers

#endif /* !__SERVER_H__ */
