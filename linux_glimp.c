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

    $Id: linux_glimp.c,v 1.2 2007-02-12 05:30:37 qqshka Exp $

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

#include <X11/extensions/xf86dga.h>
#include <X11/extensions/xf86vmode.h>

#include "quakedef.h"

//
// cvars
//

cvar_t in_mouse           = { "in_mouse",    "1", CVAR_ARCHIVE };
cvar_t in_dgamouse        = { "in_dgamouse", "1", CVAR_ARCHIVE }; // user pref for dga mouse
cvar_t in_nograb          = { "in_nograb",   "0", 0 }; // this is strictly for developers

cvar_t r_allowSoftwareGL  = { "r_allowSoftwareGL", "0", CVAR_LATCH };   // don't abort out if the pixelformat claims software

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

// bk001206 - not needed anymore
// static qbool autorepeaton = true;

#define KEY_MASK   ( KeyPressMask | KeyReleaseMask )
#define MOUSE_MASK ( ButtonPressMask | ButtonReleaseMask | PointerMotionMask | ButtonMotionMask )
#define X_MASK     ( KEY_MASK | MOUSE_MASK | VisibilityChangeMask | StructureNotifyMask )

       qbool mouseinitialized = false; // unfortunately non static, lame...
static qbool mouse_active = false;
static int mwx, mwy;
static int amx = 0, amy = 0; // Zzzz hard to explain why we have amx and mx, for us that almost the same
int mx, my;

// Time mouse was reset, we ignore the first 50ms of the mouse to allow settling of events
static double mouseResetTime = 0;
#define MOUSE_RESET_DELAY 0.050 // 50 ms

static int mouse_accel_numerator;
static int mouse_accel_denominator;
static int mouse_threshold;    

qbool vidmode_ext = false;
static int vidmode_MajorVersion = 0, vidmode_MinorVersion = 0; // major and minor of XF86VidExtensions

static int win_x, win_y;

static XF86VidModeModeInfo **vidmodes;
//static int default_dotclock_vidmode; // bk001204 - unused
static int num_vidmodes;
static qbool vidmode_active = false;


//
// function declaration
//

void GLW_InitGamma(void);
void GLW_RestoreGamma(void);
void GLW_CheckNeedSetDeviceGammaRamp(void);


// FIXME: this is a stubs for now...

void ( APIENTRY * qglGetIntegerv )(GLenum pname, GLint *params);
GLenum ( APIENTRY * qglGetError )(void);
const GLubyte * ( APIENTRY * qglGetString )(GLenum name);

XVisualInfo * (*qglXChooseVisual)( Display *dpy, int screen, int *attribList );
GLXContext (*qglXCreateContext)( Display *dpy, XVisualInfo *vis, GLXContext shareList, Bool direct );
void (*qglXDestroyContext)( Display *dpy, GLXContext ctx );
Bool (*qglXMakeCurrent)( Display *dpy, GLXDrawable drawable, GLXContext ctx);
//void (*qglXCopyContext)( Display *dpy, GLXContext src, GLXContext dst, GLuint mask );
void (*qglXSwapBuffers)( Display *dpy, GLXDrawable drawable );

const char *(APIENTRY *qglXQueryExtensionsString)(Display *dpy, int screen);

//GLX_SGI_swap_control                                                                              
GLint (APIENTRY *qglXSwapIntervalSGI)(GLint interval);

void	 QGL_EnableLogging( qbool enable ) { /* TODO */ };

qbool QGL_Init( const char *dllname ) {
	// bombastic function
	ST_Printf( PRINT_ALL, "...initializing QGL\n" );

	qglGetIntegerv               = glGetIntegerv;
	qglGetError                  = glGetError;
	qglGetString                 = glGetString;
	
	qglXChooseVisual             = glXChooseVisual;
	qglXCreateContext            = glXCreateContext;
	qglXDestroyContext           = glXDestroyContext;
	qglXMakeCurrent              = glXMakeCurrent;
//	qglXCopyContext              = glXCopyContext;
	qglXSwapBuffers              = glXSwapBuffers;

  qglXQueryExtensionsString    = glXQueryExtensionsString;

// extensions	
  qglXSwapIntervalSGI          = 0;

	qglActiveTextureARB			     = 0;
	qglClientActiveTextureARB	   = 0;
	qglMultiTexCoord2fARB		     = 0;

	return true;
}

void QGL_Shutdown( void ) {
	ST_Printf( PRINT_ALL, "...shutting down QGL\n" );

	qglGetIntegerv               = NULL;
	qglGetError                  = NULL;
	qglGetString                 = NULL;
	
	qglXChooseVisual             = NULL;
	qglXCreateContext            = NULL;
	qglXDestroyContext           = NULL;
	qglXMakeCurrent              = NULL;
//	qglXCopyContext              = NULL;
	qglXSwapBuffers              = NULL;
	
  qglXQueryExtensionsString    = NULL;
}


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
  // inviso cursor
  XWarpPointer(dpy, None, win,
               0, 0, 0, 0,
               glConfig.vidWidth / 2, glConfig.vidHeight / 2);
  XSync(dpy, False);

  XDefineCursor(dpy, win, CreateNullCursor(dpy, win));

  XGrabPointer(dpy, win, // bk010108 - do this earlier?
               False,
               MOUSE_MASK,
               GrabModeAsync, GrabModeAsync,
               win,
               None,
               CurrentTime);

  XGetPointerControl(dpy, &mouse_accel_numerator, &mouse_accel_denominator,
                     &mouse_threshold);

  XChangePointerControl(dpy, True, True, 1, 1, 0);

  XSync(dpy, False);

  mouseResetTime = Sys_DoubleTime();

  if (in_dgamouse.value)
  {
    int MajorVersion, MinorVersion;

    if (!XF86DGAQueryVersion(dpy, &MajorVersion, &MinorVersion))
    {
      // unable to query, probalby not supported, force the setting to 0
      ST_Printf( PRINT_ALL, "Failed to detect XF86DGA Mouse\n" );
      Cvar_Set( &in_dgamouse, "0" );
    } else
    {
      XF86DGADirectVideo(dpy, DefaultScreen(dpy), XF86DGADirectMouse);
      XWarpPointer(dpy, None, win, 0, 0, 0, 0, 0, 0);
    }
  } else
  {
    mwx = glConfig.vidWidth / 2;
    mwy = glConfig.vidHeight / 2;
    amx = amy = 0;
  }

  XGrabKeyboard(dpy, win,
                False,
                GrabModeAsync, GrabModeAsync,
                CurrentTime);

  XSync(dpy, False);
}

static void uninstall_grabs(void)
{
  if (in_dgamouse.value)
  {
		if (developer.value)
			ST_Printf( PRINT_ALL, "DGA Mouse - Disabling DGA DirectVideo\n" );
    XF86DGADirectVideo(dpy, DefaultScreen(dpy), 0);
  }

  XChangePointerControl(dpy, true, true, mouse_accel_numerator, 
                        mouse_accel_denominator, mouse_threshold);

  XUngrabPointer(dpy, CurrentTime);
  XUngrabKeyboard(dpy, CurrentTime);

  XWarpPointer(dpy, None, win,
               0, 0, 0, 0,
               glConfig.vidWidth / 2, glConfig.vidHeight / 2);

  // inviso cursor
  XUndefineCursor(dpy, win);
}

static int XLateKey(XKeyEvent *ev) {
	int key, kp;
	//char buf[64];
	KeySym keysym;

	key = 0;
	kp = (int) cl_keypad.value;

	keysym = XLookupKeysym (ev, 0);
	//XLookupString(ev, buf, sizeof (buf), &keysym, 0);

	switch(keysym) {
		case XK_Scroll_Lock:	key = K_SCRLCK; break;

		case XK_Caps_Lock:		key = K_CAPSLOCK; break;

		case XK_Num_Lock:		key = kp ? KP_NUMLOCK : K_PAUSE; break;

		case XK_KP_Page_Up:		key = kp ? KP_PGUP : K_PGUP; break;
		case XK_Page_Up:		key = K_PGUP; break;

		case XK_KP_Page_Down:	key = kp ? KP_PGDN : K_PGDN; break;
		case XK_Page_Down:		key = K_PGDN; break;

		case XK_KP_Home:		key = kp ? KP_HOME : K_HOME; break;
		case XK_Home:			key = K_HOME; break;

		case XK_KP_End:			key = kp ? KP_END : K_END; break;
		case XK_End:			key = K_END; break;

		case XK_KP_Left:		key = kp ? KP_LEFTARROW : K_LEFTARROW; break;
		case XK_Left:			key = K_LEFTARROW; break;

		case XK_KP_Right:		key = kp ? KP_RIGHTARROW : K_RIGHTARROW; break;
		case XK_Right:			key = K_RIGHTARROW; break;

		case XK_KP_Down:		key = kp ? KP_DOWNARROW : K_DOWNARROW; break;

		case XK_Down:			key = K_DOWNARROW; break;

		case XK_KP_Up:			key = kp ? KP_UPARROW : K_UPARROW; break;

		case XK_Up:				key = K_UPARROW; break;

		case XK_Escape:			key = K_ESCAPE; break;

		case XK_KP_Enter:		key = kp ? KP_ENTER : K_ENTER; break;

		case XK_Return:			key = K_ENTER; break;

		case XK_Tab:			key = K_TAB; break;

		case XK_F1:				key = K_F1; break;

		case XK_F2:				key = K_F2; break;

		case XK_F3:				key = K_F3; break;

		case XK_F4:				key = K_F4; break;

		case XK_F5:				key = K_F5; break;

		case XK_F6:				key = K_F6; break;

		case XK_F7:				key = K_F7; break;

		case XK_F8:				key = K_F8; break;

		case XK_F9:				key = K_F9; break;

		case XK_F10:			key = K_F10; break;

		case XK_F11:			key = K_F11; break;

		case XK_F12:			key = K_F12; break;

		case XK_BackSpace:		key = K_BACKSPACE; break;

		case XK_KP_Delete:		key = kp ? KP_DEL : K_DEL; break;
		case XK_Delete:			key = K_DEL; break;

		case XK_Pause:			key = K_PAUSE; break;

		case XK_Shift_L:		key = K_LSHIFT; break;
		case XK_Shift_R:		key = K_RSHIFT; break;

		case XK_Execute: 
		case XK_Control_L:		key = K_LCTRL; break;
		case XK_Control_R:		key = K_RCTRL; break;

		case XK_Alt_L:	
		case XK_Meta_L:			key = K_LALT; break;
		case XK_Alt_R:	
		case XK_Meta_R:			key = K_RALT; break;

		case XK_Super_L:		key = K_LWIN; break;
		case XK_Super_R:		key = K_RWIN; break;
		case XK_Menu:			key = K_MENU; break;

		case XK_KP_Begin:		key = kp ? KP_5 : '5'; break;

		case XK_KP_Insert:		key = kp ? KP_INS : K_INS; break;
		case XK_Insert:			key = K_INS; break;

		case XK_KP_Multiply:	key = kp ? KP_STAR : '*'; break;

		case XK_KP_Add:			key = kp ? KP_PLUS : '+'; break;

		case XK_KP_Subtract:	key = kp ? KP_MINUS : '-'; break;

		case XK_KP_Divide:		key = kp ? KP_SLASH : '/'; break;


		default:
			if (keysym >= 32 && keysym <= 126) {
				key = tolower(keysym);
			}
			break;
	}
	return key;
}

static void HandleEvents(void)
{
  extern int ctrlDown, shiftDown, altDown; 
  int key;
  XEvent event;
  qbool dowarp = false;
  int dx, dy;
	
  if (!dpy)
    return;

  while (XPending(dpy))
  {
    XNextEvent(dpy, &event);
    switch (event.type)
    {
	  case KeyPress:
	  case KeyRelease:
		  key = XLateKey(&event.xkey);
		  if (key == K_CTRL  || key == K_LCTRL  || key == K_RCTRL)
        ctrlDown  = event.type == KeyPress;
		  if (key == K_SHIFT || key == K_LSHIFT || key == K_RSHIFT)
			  shiftDown = event.type == KeyPress;
		  if (key == K_ALT   || key == K_LALT   || key == K_RALT)
			  altDown   = event.type == KeyPress;

#ifdef WITH_KEYMAP
		  // if set, print the current Key information
		  if (cl_showkeycodes.value > 0)
			 IN_Keycode_Print_f (&event.xkey, false, event.type == KeyPress, key);
#endif // WITH_KEYMAP

		  Key_Event(key, event.type == KeyPress);
		  break;

    case MotionNotify:
      if (mouse_active)
      {
        if (in_dgamouse.value)
        {
          if (abs(event.xmotion.x_root) > 1)
            amx += event.xmotion.x_root * 2;
          else
            amx += event.xmotion.x_root;
          if (abs(event.xmotion.y_root) > 1)
            amy += event.xmotion.y_root * 2;
          else
            amy += event.xmotion.y_root;
          if (Sys_DoubleTime() - mouseResetTime > MOUSE_RESET_DELAY )
          {
					  mx += amx;
						my += amy;
          }
          amx = amy = 0;
        } else
        {
          // If it's a center motion, we've just returned from our warp
          if (event.xmotion.x == glConfig.vidWidth/2 &&
              event.xmotion.y == glConfig.vidHeight/2)
          {
            mwx = glConfig.vidWidth/2;
            mwy = glConfig.vidHeight/2;
            if (Sys_DoubleTime() - mouseResetTime > MOUSE_RESET_DELAY )
            {
					 	  mx += amx;
						  my += amy;
            }
            amx = amy = 0;
            break;
          }

          dx = ((int)event.xmotion.x - mwx);
          dy = ((int)event.xmotion.y - mwy);
          if (abs(dx) > 1)
            amx += dx * 2;
          else
            amx += dx;
          if (abs(dy) > 1)
            amy += dy * 2;
          else
            amy += dy;

          mwx = event.xmotion.x;
          mwy = event.xmotion.y;
          dowarp = true;
        }
      }
      break;

    case ButtonPress:
    case ButtonRelease:
		  switch (event.xbutton.button) {
		    case 1:
			    Key_Event(K_MOUSE1, event.type == ButtonPress); break;
		    case 2:
			    Key_Event(K_MOUSE3, event.type == ButtonPress); break;
		    case 3:
			    Key_Event(K_MOUSE2, event.type == ButtonPress); break;
		    case 4:
			    Key_Event(K_MWHEELUP, event.type == ButtonPress); break;
		    case 5:
			    Key_Event(K_MWHEELDOWN, event.type == ButtonPress); break;			
        case 6:
          Key_Event(K_MOUSE4, event.type == ButtonPress); break;
        case 7:
          Key_Event(K_MOUSE5, event.type == ButtonPress); break;
		  }
		
      break;

    case CreateNotify :
      win_x = event.xcreatewindow.x;
      win_y = event.xcreatewindow.y;
      break;

    case ConfigureNotify :
      win_x = event.xconfigure.x;
      win_y = event.xconfigure.y;
      break;
    }
  }

  if (dowarp)
  {
    XWarpPointer(dpy,None,win,0,0,0,0, 
                 (glConfig.vidWidth/2),(glConfig.vidHeight/2));
  }
}

void IN_ActivateMouse( void ) 
{
  if (!mouseinitialized || !dpy || !win)
    return;

  if (!mouse_active)
  {
		if (!in_nograb.value)
      install_grabs();
		else if (in_dgamouse.value) // force dga mouse to 0 if using nograb
			Cvar_Set( &in_dgamouse, "0" );

    mouse_active = true;
  }
}

void IN_DeactivateMouse( void ) 
{
  if (!mouseinitialized || !dpy || !win)
    return;

  if (mouse_active)
  {
		if (!in_nograb.value)
      uninstall_grabs();
		else if (in_dgamouse.value) // force dga mouse to 0 if using nograb
			Cvar_Set( &in_dgamouse, "0" );

    mouse_active = false;
  }
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
  // bk001206 - replaced with H2/Fakk2 solution
  // XAutoRepeatOn(dpy);
  // autorepeaton = false; // bk001130 - from cvs1.17 (mkv)
  if (dpy)
  {
    if (ctx)
      qglXDestroyContext(dpy, ctx);

    if (win)
      XDestroyWindow(dpy, win);

    if (vidmode_active)
		{
      XF86VidModeSwitchToMode(dpy, scrnum, vidmodes[0]);
		  XFlush(dpy);
		}

//  if (glConfig.deviceSupportsGamma)
//  {
      GLW_RestoreGamma();
//  }

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
** GLimp_LogComment
*/
void GLimp_LogComment( char *comment ) 
{
  if ( glw_state.log_fp )
  {
    fprintf( glw_state.log_fp, "%s", comment );
  }
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

  // don't ever bother going into fullscreen with a voodoo card
#if 1	// JDC: I reenabled this
  if ( Q_stristr( drivername, "Voodoo" ) )
  {
    Cvar_Set( &r_fullscreen, "0" );
    r_fullscreen.modified = false;
    fullscreen = false;
  }
#endif
	
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
  default:
    break;
  }
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
  unsigned long mask;
  int colorbits, depthbits, stencilbits;
  int tcolorbits, tdepthbits, tstencilbits;
  int dga_MajorVersion, dga_MinorVersion;
  int actualWidth, actualHeight;
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

  // Check for DGA	
  dga_MajorVersion = 0, dga_MinorVersion = 0;
  if (in_dgamouse.value)
  {
    if (!XF86DGAQueryVersion(dpy, &dga_MajorVersion, &dga_MinorVersion))
    {
      // unable to query, probalby not supported
      ST_Printf( PRINT_ALL, "Failed to detect XF86DGA Mouse\n" );
      Cvar_Set( &in_dgamouse, "0" );
    } else
    {
      ST_Printf( PRINT_ALL, "XF86DGA Mouse (Version %d.%d) initialized\n",
                 dga_MajorVersion, dga_MinorVersion);
    }
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

    visinfo = qglXChooseVisual(dpy, scrnum, attrib);
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

  XMapWindow( dpy, win );

  if (vidmode_active)
    XMoveWindow(dpy, win, 0, 0);

  XFlush(dpy);
  XSync(dpy,False); // bk001130 - from cvs1.17 (mkv)
  ctx = qglXCreateContext(dpy, visinfo, NULL, True);
  XSync(dpy,False); // bk001130 - from cvs1.17 (mkv)

  /* GH: Free the visinfo after we're done with it */
  XFree( visinfo );

  qglXMakeCurrent(dpy, win, ctx);

  // bk001130 - from cvs1.17 (mkv)
  glstring = (char*)qglGetString (GL_RENDERER);
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

  return RSERR_OK;
}

/*
** GLW_InitExtensions
*/
static void GLW_InitExtensions( void )
{
  extern void *GL_GetProcAddress (const char *ExtName);

  if ( !r_allowExtensions.integer )
  {
    ST_Printf( PRINT_ALL, "*** IGNORING OPENGL EXTENSIONS ***\n" );
    return;
  }

  ST_Printf( PRINT_ALL, "Initializing OpenGL extensions\n" );

  //GLX_SGI_swap_control
	if ( Q_stristr( glConfig.extensions_string, "GLX_SGI_swap_control" ) )
    qglXSwapIntervalSGI = GL_GetProcAddress("glXSwapIntervalSGI");

	if ( qglXSwapIntervalSGI )
	{
		ST_Printf( PRINT_ALL, "...using GLX_SGI_swap_control\n" );
		r_swapInterval.modified = true;	// force a set next frame
	}
	else
	{
		ST_Printf( PRINT_ALL, "...GLX_SGI_swap_control not found\n" );
	}
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

// qqshka, we are not loading...
//  ST_Printf( PRINT_ALL, "...loading %s: ", name );

  // disable the 3Dfx splash screen and set gamma
  // we do this all the time, but it shouldn't hurt anything
  // on non-3Dfx stuff
  putenv("FX_GLIDE_NO_SPLASH=0");

  // Mesa VooDoo hacks
  putenv("MESA_GLX_FX=fullscreen\n");

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
  qbool attempted3Dfx = false;
  qbool success = false;
  char  buf[1024];
//  cvar_t *lastValidRenderer = ri.Cvar_Get( "r_lastValidRenderer", "(uninitialized)", CVAR_ARCHIVE );

	Cvar_SetCurrentGroup(CVAR_GROUP_VIDEO);
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
    } else if ( !strcasecmp( r_glDriver.string, _3DFX_DRIVER_NAME ) )
    {
      attempted3Dfx = true;
    }

    #if 0
    // TTimo
    // https://zerowing.idsoftware.com/bugzilla/show_bug.cgi?id=455
    // old legacy load code, was confusing people who had a bad OpenGL setup
    if ( !attempted3Dfx && !success )
    {
      attempted3Dfx = true;
      if ( GLW_LoadOpenGL( _3DFX_DRIVER_NAME ) )
      {
        Cvar_Set( &r_glDriver, _3DFX_DRIVER_NAME );
        r_glDriver.modified = false;
        success = true;
      }
    }
    #endif

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
      ST_Printf( PRINT_ERR_FATAL, "GLimp_Init() - could not load OpenGL subsystem\n" );

  }

  // This values force the UI to disable driver selection
  glConfig.driverType = GLDRV_ICD;
  glConfig.hardwareType = GLHW_GENERIC;

  // get our config strings
  strlcpy( glConfig.vendor_string,     (char*)qglGetString (GL_VENDOR),     sizeof( glConfig.vendor_string ) );
  strlcpy( glConfig.renderer_string,   (char*)qglGetString (GL_RENDERER),   sizeof( glConfig.renderer_string ) );
  if (*glConfig.renderer_string && glConfig.renderer_string[strlen(glConfig.renderer_string) - 1] == '\n')
    glConfig.renderer_string[strlen(glConfig.renderer_string) - 1] = 0;
  strlcpy( glConfig.version_string,    (char*)qglGetString (GL_VERSION),    sizeof( glConfig.version_string ) );
  strlcpy( glConfig.extensions_string, (char*)qglGetString (GL_EXTENSIONS), sizeof( glConfig.extensions_string ) );
  // append GLX extensions, DO NOT CONFUSE WITH GL EXTENSIONS
	strlcat( glConfig.extensions_string, " ", sizeof( glConfig.extensions_string ) );
  strlcat( glConfig.extensions_string, qglXQueryExtensionsString(dpy, scrnum), sizeof( glConfig.extensions_string ) );
										 
  //
  // chipset specific configuration
  //
  strlcpy( buf, glConfig.renderer_string, sizeof( buf ) );
  Q_strlwr( buf );

  //
  // NOTE: if changing cvars, do it within this block.  This allows them
  // to be overridden when testing driver fixes, etc. but only sets
  // them to their default state when the hardware is first installed/run.
  //
#if 0 /* qqshka: sad, that good for q3, but not for ezquake cfg managment */
  if ( strcasecmp( lastValidRenderer.string, glConfig.renderer_string ) )
  {
    glConfig.hardwareType = GLHW_GENERIC;

    Cvar_Set( &r_texturemode, "GL_LINEAR_MIPMAP_NEAREST" );

    // VOODOO GRAPHICS w/ 2MB
    if ( Q_stristr( buf, "voodoo graphics/1 tmu/2 mb" ) )
    {
      Cvar_Set( &r_picmip, "2" );
    } else
    {
      Cvar_Set( &r_picmip, "1" );

      if ( Q_stristr( buf, "rage 128" ) || Q_stristr( buf, "rage128" ) )
      {
        Cvar_Set( &r_finish, "0" );
      }
      // Savage3D and Savage4 should always have trilinear enabled
      else if ( Q_stristr( buf, "savage3d" ) || Q_stristr( buf, "s3 savage4" ) )
      {
        Cvar_Set( &r_texturemode, "GL_LINEAR_MIPMAP_LINEAR" );
      }
    }
  }
#endif

  //
  // this is where hardware specific workarounds that should be
  // detected/initialized every startup should go.
  //
  if ( Q_stristr( buf, "banshee" ) || Q_stristr( buf, "Voodoo_Graphics" ) )
  {
    glConfig.hardwareType = GLHW_3DFX_2D3D;
  } else if ( Q_stristr( buf, "rage pro" ) || Q_stristr( buf, "RagePro" ) )
  {
    glConfig.hardwareType = GLHW_RAGEPRO;
  } else if ( Q_stristr( buf, "permedia2" ) )
  {
    glConfig.hardwareType = GLHW_PERMEDIA2;
  } else if ( Q_stristr( buf, "riva 128" ) )
  {
    glConfig.hardwareType = GLHW_RIVA128;
  } else if ( Q_stristr( buf, "riva tnt " ) )
  {
  }

//  Cvar_Set( &r_lastValidRenderer, glConfig.renderer_string );

  // initialize extensions
  GLW_InitExtensions();
  GLW_InitGamma();

  InitSig(); // not clear why this is at begin & end of function

  return;
}


void GL_BeginRendering (int *x, int *y, int *width, int *height) {
	*x = *y = 0;
	*width  = glConfig.vidWidth;
	*height = glConfig.vidHeight;
}

void GL_EndRendering (void) {
	//
	// swapinterval stuff
	//
	if ( r_swapInterval.modified ) {
		r_swapInterval.modified = false;

		if ( !glConfig.stereoEnabled ) {	// why?
			if ( qglXSwapIntervalSGI ) {
				qglXSwapIntervalSGI( r_swapInterval.integer );
			}
		}
	}

  GLW_CheckNeedSetDeviceGammaRamp();

  GLimp_EndFrame();
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
  qglXSwapBuffers(dpy, win);

  // check logging
//  QGL_EnableLogging( (qbool)r_logFile.integer ); // bk001205 - was ->value
}

/*****************************************************************************/
/* MOUSE                                                                     */
/*****************************************************************************/

void IN_Commands (void) { /* etmpty */ }

void IN_StartupMouse(void) {
	Cvar_SetCurrentGroup(CVAR_GROUP_INPUT_MOUSE);
  // mouse variables
	Cvar_Register (&in_mouse);
	Cvar_Register (&in_dgamouse);
	// developer feature, allows to break without loosing mouse pointer
	Cvar_Register (&in_nograb);
	Cvar_ResetCurrentGroup();

  if (in_mouse.value)
    mouseinitialized = true;
  else
    mouseinitialized = false;
}

void IN_Frame (void) {

  if ( key_dest != key_game )
  {
    // temporarily deactivate if not in the game and
    // running on the desktop
    // voodoo always counts as full screen
    if (Cvar_VariableValue ("r_fullscreen") == 0
        && strcmp( Cvar_VariableString("r_glDriver"), _3DFX_DRIVER_NAME ) )
    {
      IN_DeactivateMouse ();
      return;
    }
  }

  IN_ActivateMouse();
}


void Sys_SendKeyEvents (void) {
  // XEvent event; // bk001204 - unused

  if (!dpy)
    return;

  IN_Frame();				
  HandleEvents();
}

/************************************* Window related *******************************/

void VID_SetCaption (char *text)
{
	if (!dpy)
		return;
	XStoreName (dpy, win, text);
}

/************************************* HW GAMMA *************************************/

static unsigned short *currentgammaramp = NULL;
static unsigned short sysramp[3][256]; // system gamma ramp

extern cvar_t	vid_hwgammacontrol; // put here, so u remeber this cvar exist

qbool vid_gammaworks      = false;
qbool vid_hwgamma_enabled = false;
qbool old_hwgamma_enabled = false;
qbool customgamma         = false;


void GLW_InitGamma (void)
{
	int size; // gamma ramp size

	// main
	vid_gammaworks      = false;
	// damn helpers
	vid_hwgamma_enabled = false;
	old_hwgamma_enabled = false;
	customgamma		      = false;
	currentgammaramp    = NULL;

	v_gamma.modified	= true; // force update on next frame	

	if (COM_CheckParm("-nohwgamma") && (!strncasecmp(Rulesets_Ruleset(), "MTFL", 4))) // FIXME
		return;

	XF86VidModeGetGammaRampSize(dpy, scrnum, &size);
	
	vid_gammaworks = (size == 256);

	if ( vid_gammaworks )
	{
		XF86VidModeGetGammaRamp(dpy, scrnum, size, sysramp[0], sysramp[1], sysramp[2]);
	}
}

void GLW_RestoreGamma(void) {
	if ( vid_gammaworks && customgamma )
	{
		customgamma = false;
		XF86VidModeSetGammaRamp(dpy, scrnum, 256, sysramp[0], sysramp[1], sysramp[2]);
	}
}

void GLW_CheckNeedSetDeviceGammaRamp(void) {
	vid_hwgamma_enabled = vid_hwgammacontrol.value && vid_gammaworks/* && ActiveApp && !Minimized */;
	vid_hwgamma_enabled = vid_hwgamma_enabled && (glConfig.isFullscreen || vid_hwgammacontrol.value == 2);

	if ( vid_hwgamma_enabled != old_hwgamma_enabled )
	{
		old_hwgamma_enabled = vid_hwgamma_enabled;
		if ( vid_hwgamma_enabled && currentgammaramp )
			VID_SetDeviceGammaRamp ( currentgammaramp );
		else
			GLW_RestoreGamma ();
	}
}

void VID_SetDeviceGammaRamp (unsigned short *ramps) {
	if ( vid_gammaworks )
	{
		currentgammaramp = ramps;
		if ( vid_hwgamma_enabled )
		{
			XF86VidModeSetGammaRamp(dpy, scrnum, 256, ramps, ramps + 256, ramps + 512);
			customgamma = true;
		}
	}
}

