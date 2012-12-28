/*
Copyright (C) 2002-2003 A Nourai

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

*/

// vid_common_gl.c -- Common code for vid_wgl.c and vid_glx.c

#include <GL/glew.h>
#include <stdlib.h>
#include "quakedef.h"
#include "gl_model.h"
#include "gl_local.h"
#include "fs.h"

#define SHADER_ENTRY(a) [SHADER_##a] = { 0, #a }
glsl_shader_t glsl_shaders[SHADER_LAST] = {
	SHADER_ENTRY(WORLD),
	SHADER_ENTRY(MODEL),
	SHADER_ENTRY(TURB),
	SHADER_ENTRY(HUD),
};

// dimman: Might have to remove these ..
#ifdef __APPLE__
void *Sys_GetProcAddress (const char *ExtName);
#endif

#ifdef __linux__
# ifndef __GLXextFuncPtr
  typedef void (*__GLXextFuncPtr)(void);
# endif
# ifndef glXGetProcAddressARB
  extern __GLXextFuncPtr glXGetProcAddressARB (const GLubyte *);
# endif
#endif
#ifdef __FreeBSD__
# ifndef glXGetProcAddressARB
  extern __GLXextFuncPtr glXGetProcAddressARB (const GLubyte *);
# endif
#endif

#if 0
void *GL_GetProcAddress (const char *ExtName)
{
#ifdef _WIN32
			return (void *) wglGetProcAddress(ExtName);
#else
#ifdef __APPLE__ // Mac OS X don't have an OpenGL extension fetch function. Isn't that silly?
			return Sys_GetProcAddress (ExtName);
#else
			return (void *) glXGetProcAddressARB((const GLubyte*) ExtName);
#endif /* __APPLE__ */
#endif /* _WIN32 */
}
#endif

const char *gl_vendor;
const char *gl_renderer;
const char *gl_version;
const char *gl_extensions;

int anisotropy_ext = 0;

qbool gl_mtexable = true;
int gl_textureunits = 4;

qbool gl_combine = false;

qbool gl_add_ext = false;

qbool gl_allow_ztrick = true;

float vid_gamma = 1.0;
byte vid_gamma_table[256];

unsigned short d_8to16table[256];
unsigned d_8to24table[256];
unsigned d_8to24table2[256];

byte color_white[4] = {255, 255, 255, 255};
byte color_black[4] = {0, 0, 0, 255};

void OnChange_gl_ext_texture_compression(cvar_t *, char *, qbool *);

cvar_t	gl_strings = {"gl_strings", "", CVAR_ROM | CVAR_SILENT};
cvar_t	gl_ext_texture_compression = {"gl_ext_texture_compression", "0", CVAR_SILENT, OnChange_gl_ext_texture_compression};
cvar_t  gl_maxtmu2 = {"gl_maxtmu2", "0", CVAR_LATCH};

// GL_ARB_texture_non_power_of_two
qbool gl_support_arb_texture_non_power_of_two = false;
cvar_t gl_ext_arb_texture_non_power_of_two = {"gl_ext_arb_texture_non_power_of_two", "1", CVAR_LATCH};

void OnChange_gl_ext_texture_compression(cvar_t *var, char *string, qbool *cancel) {
	float newval = Q_atof(string);

	gl_alpha_format = newval ? GL_COMPRESSED_RGBA_ARB : GL_RGBA;
	gl_solid_format = newval ? GL_COMPRESSED_RGB_ARB : GL_RGB;
}

/************************************** GL INIT **************************************/

static void print_infolog(GLuint program)
{
    char info[1 << 12];
    info[0] = 0;
    glGetInfoLogARB(program, sizeof info, NULL, info);
	const char *p = info;
	while(*p && *p == ' ')
		p++;
    Con_Printf("%s", p);
}

static unsigned int setup_shader(const char *src, unsigned int type)
{
	GLuint shader = glCreateShader(type);
	glShaderSource(shader, 1, &src, NULL);
	glCompileShader(shader);
	print_infolog(shader);
	return shader;
}

static unsigned int setup_program(const char *vertex_shader, const char *fragment_shader)
{
	GLuint prog, shader;;

	prog = glCreateProgram();
	glAttachShader(prog, shader = setup_shader(vertex_shader, GL_VERTEX_SHADER));
	glDeleteShader(shader);
	glAttachShader(prog, shader = setup_shader(fragment_shader, GL_FRAGMENT_SHADER));
	glDeleteShader(shader);
	glLinkProgram(prog);
	print_infolog(prog);

	return prog;
}

/* FIXME: Move these.. Should make it independent of order by using pointers perhaps.. */
static const char *fragshader[] = {
	/* WORLD.FRAG */
	"varying vec2 tex_coord;\
	varying vec2 lightmap_coord;\
	uniform sampler2D world_tex;\
	uniform sampler2D lightmap_tex;\
	uniform float gamma;\
	uniform float contrast;\
	void\
	main()\
	{\
		vec3 world = texture2D(world_tex, tex_coord).rgb;\
		vec3 lightmap = vec3(1.0) - 0.999 * texture2D(lightmap_tex, lightmap_coord).rgb;\
		gl_FragColor.rgb = pow(contrast * world * lightmap, vec3(gamma));\
		gl_FragColor.a = 1.0;\
	}\
	",
	/* MODEL.FRAG */
	"uniform sampler2D model_tex;\
	varying vec2 tex_coord;\
	uniform float gamma;\
	uniform float contrast;\
	void\
	main()\
	{\
		vec3 color = texture2D(model_tex, tex_coord).rgb;\
		gl_FragColor.rgb = pow(vec3(gl_Color) * contrast * color, vec3(gamma));\
		gl_FragColor.a = 1.0;\
	}\
	",
	/* TURB.FRAG */
	"uniform sampler2D turb_tex;\
	varying vec2 tex_coord;\
	uniform float gamma;\
	uniform float contrast;\
	void\
	main()\
	{\
		vec4 color = texture2D(turb_tex, tex_coord);\
		gl_FragColor.rgb = pow(vec3(gl_Color) * contrast * vec3(color), vec3(gamma));\
		gl_FragColor.a = color.a;\
	}\
	",
	/* HUD.FRAG */
	"uniform sampler2D hud_tex;\
	varying vec2 tex_coord;\
	uniform float gamma;\
	uniform float contrast;\
	void\
	main()\
	{\
		vec4 color = texture2D(hud_tex, tex_coord);\
		gl_FragColor.rgb = pow(vec3(gl_Color) * contrast * vec3(color), vec3(gamma));\
		gl_FragColor.a = color.a * gl_Color.a;\
	}\
	"
};

static const char *vertshader[] = {
	/* MODEL.VERT */
	"varying vec2 tex_coord;\
	varying vec2 lightmap_coord;\
	void\
	main()\
	{\
	        gl_Position = ftransform();\
	        tex_coord = vec2(gl_MultiTexCoord0);\
	        lightmap_coord = vec2(gl_MultiTexCoord1);\
	}\
	",
	/* WORLD.VERT */
	"varying vec2 tex_coord;\
	void\
	main()\
	{\
		gl_Position = ftransform();\
		tex_coord = vec2(gl_MultiTexCoord0);\
		gl_FrontColor = gl_Color;\
	}\
	",
	/* TURB.VERT */
	"varying vec2 tex_coord;\
	void\
	main()\
	{\
		gl_Position = ftransform();\
		tex_coord = vec2(gl_MultiTexCoord0);\
		gl_FrontColor = gl_Color;\
	}\
	",
	/* HUD.VERT */
	"varying vec2 tex_coord;\
	void\
	main()\
	{\
		gl_Position = ftransform();\
		tex_coord = vec2(gl_MultiTexCoord0);\
		gl_FrontColor = gl_Color;\
	}\
	"
};


static void load_shader()
{
	int i, j;
	char filename[256], shadername[128];
	unsigned long len_vert, len_frag;
	char *src_vert, *src_frag;
	vfsfile_t *vert, *frag;
	for(i = 0; i < SHADER_LAST; i++) {
		for(j = 0; glsl_shaders[i].name[j] && j < sizeof shadername - 1; j++)
			shadername[j] = tolower(glsl_shaders[i].name[j]);
		shadername[j] = 0;
		glsl_shaders[i].shader = 0;
		if(0)
		{	
			src_vert = src_frag = NULL;
			vert = frag = NULL;
			Con_Printf("loading shader: %s\n", shadername);
			snprintf(filename, sizeof filename, "shader/%s.vert", shadername);
			vert = FS_OpenVFS(filename, "rb", FS_ANY);
			if(!vert) {
				Con_Printf("could not open \"%s\", skipping shader\n", filename);
				goto out;
			}
			snprintf(filename, sizeof filename, "shader/%s.frag", shadername);
			frag = FS_OpenVFS(filename, "rb", FS_ANY);
			if(!frag) {
				Con_Printf("could not open \"%s\", skipping shader\n", filename);
				goto out;
			}

			len_vert = VFS_GETLEN(vert);
			len_frag = VFS_GETLEN(frag);
			src_vert = malloc(len_vert + 1);
			src_frag = malloc(len_frag + 1);


			VFS_READ(vert, src_vert, len_vert, NULL);
			src_vert[len_vert] = 0;
			VFS_READ(frag, src_frag, len_frag, NULL);
			src_frag[len_frag] = 0;

			glsl_shaders[i].shader = setup_program(src_vert, src_frag);
	out:
			if(vert)
				VFS_CLOSE(vert);
			if(frag)
				VFS_CLOSE(frag);

			free(src_vert);
			free(src_frag);
		}
		else
		{
			Con_Printf("loading builtin shader: %s\n", shadername);
			glsl_shaders[i].shader = setup_program(vertshader[i], fragshader[i]);
		}


	}
	//setup_program("void main() { gl_Position = ftransform(); }","void main() { gl_FragColor = vec4(1.0); }");
}

void GL_Init (void) {
	if (glewInit())
	{
		ST_Printf(PRINT_ERR_FATAL, "Could not initialize GLEW\n");
	}

	if(!glewIsSupported("GL_VERSION_2_0"))
	{
		ST_Printf(PRINT_ERR_FATAL, "OpenGL 2.0 support missing\n");
	}

	gl_vendor     = (const char*) glGetString (GL_VENDOR);
	gl_renderer   = (const char*) glGetString (GL_RENDERER);
	gl_version    = (const char*) glGetString (GL_VERSION);
	gl_extensions = (const char*) glGetString (GL_EXTENSIONS);

#if !defined( _WIN32 ) && !defined( __linux__ ) /* we print this in different place on WIN and Linux */
/* FIXME/TODO: FreeBSD too? */
	Com_Printf_State(PRINT_INFO, "GL_VENDOR: %s\n",   gl_vendor);
	Com_Printf_State(PRINT_INFO, "GL_RENDERER: %s\n", gl_renderer);
	Com_Printf_State(PRINT_INFO, "GL_VERSION: %s\n",  gl_version);
#endif

	if (COM_CheckParm("-gl_ext"))
		Com_Printf_State(PRINT_INFO, "GL_EXTENSIONS: %s\n", gl_extensions);

	Cvar_Register (&gl_strings);
	Cvar_ForceSet (&gl_strings, va("GL_VENDOR: %s\nGL_RENDERER: %s\n"
		"GL_VERSION: %s\nGL_EXTENSIONS: %s", gl_vendor, gl_renderer, gl_version, gl_extensions));
    Cvar_Register (&gl_maxtmu2);
#ifndef __APPLE__
	glClearColor (1,0,0,0);
#else
	glClearColor (0.2,0.2,0.2,1.0);
#endif

	glCullFace(GL_FRONT);
	glEnable(GL_TEXTURE_2D);

	glEnable(GL_ALPHA_TEST);
	glAlphaFunc(GL_GREATER, 0.666);

	glPolygonMode (GL_FRONT_AND_BACK, GL_FILL);
	glShadeModel (GL_FLAT);

	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

	glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);

	load_shader();
}

/************************************* VID GAMMA *************************************/

void Check_Gamma (unsigned char *pal) {
	float inf;
	unsigned char palette[768];
	int i;

	// we do not need this after host initialized
	if (!host_initialized)
	{
		float old = v_gamma.value;
		if ((i = COM_CheckParm("-gamma")) != 0 && i + 1 < COM_Argc())
			vid_gamma = bound (0.3, Q_atof(COM_Argv(i + 1)), 1);
		else
			vid_gamma = 1;

		Cvar_SetDefault (&v_gamma, vid_gamma);
		// Cvar_SetDefault set not only default value, but also reset to default, fix that
		Cvar_SetValue(&v_gamma, old ? old : vid_gamma);
	}

	if (vid_gamma != 1){
		for (i = 0; i < 256; i++){
			inf = 255 * pow((i + 0.5) / 255.5, vid_gamma) + 0.5;
			if (inf > 255)
				inf = 255;
			vid_gamma_table[i] = inf;
		}
	} else {
		for (i = 0; i < 256; i++)
			vid_gamma_table[i] = i;
	}

	for (i = 0; i < 768; i++)
		palette[i] = vid_gamma_table[pal[i]];

	memcpy (pal, palette, sizeof(palette));
}

/************************************* HW GAMMA *************************************/

void VID_SetPalette (unsigned char *palette) {
	int i;
	byte *pal;
	unsigned r,g,b, v, *table;

	// 8 8 8 encoding
	// Macintosh has different byte order
	pal = palette;
	table = d_8to24table;
	for (i = 0; i < 256; i++) {
		r = pal[0];
		g = pal[1];
		b = pal[2];
		pal += 3;
		v = LittleLong ((255 << 24) + (r << 0) + (g << 8) + (b << 16));
		*table++ = v;
	}
	d_8to24table[255] = 0;		// 255 is transparent

	// Tonik: create a brighter palette for bmodel textures
	pal = palette;
	table = d_8to24table2;

	for (i = 0; i < 256; i++) {
		r = pal[0] * (2.0 / 1.5); if (r > 255) r = 255;
		g = pal[1] * (2.0 / 1.5); if (g > 255) g = 255;
		b = pal[2] * (2.0 / 1.5); if (b > 255) b = 255;
		pal += 3;
		*table++ = LittleLong ((255 << 24) + (r << 0) + (g << 8) + (b << 16));
	}
	d_8to24table2[255] = 0;	// 255 is transparent
}
