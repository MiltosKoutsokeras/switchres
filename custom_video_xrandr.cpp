/**************************************************************

   video_xrandr_xrandr.cpp - Linux XRANDR video management layer

   ---------------------------------------------------------

   SwitchRes   Modeline generation engine for emulation

   License     GPL-2.0+
   Copyright   2010-2020 - Chris Kennedy, Antonio Giner, Alexandre Wodarczyk

 **************************************************************/

#include <stdio.h>
#include "custom_video_xrandr.h"
#include "log.h"

//============================================================
//  error_handler 
//  xorg error handler
//============================================================

int xrandr_timing::m_xerrors = 0;

static int error_handler(Display *m_dpy, XErrorEvent *err)
{
	xrandr_timing::m_xerrors++;
	log_error("Display is set %d error code %d total error %d\n",m_dpy!=NULL, err->error_code, xrandr_timing::m_xerrors);
	return 0;
}

//============================================================
//  xrandr_timing::xrandr_timing
//============================================================

xrandr_timing::xrandr_timing(char *device_name, char *param)
{
	log_verbose("XRANDR: creation (%s,%s)\n", device_name, param);
	// copy screen device name and limit size
	if ((strlen(device_name)+1) > 32)
	{
		strncpy(m_device_name, device_name, 31);
		log_error("XRANDR: error, the devine name is too long it has been trucated to %s\n",m_device_name);
	} else {
		strcpy(m_device_name, device_name);
	}

	// m_dpy is global to reduce open/close calls, resource is freed when class is destroyed
	m_dpy = XOpenDisplay(NULL);

	// display XRANDR version
	int major_version, minor_version;
	XRRQueryVersion(m_dpy, &major_version, &minor_version);
	log_verbose("XRANDR: version %d.%d\n",major_version,minor_version);

	// inititalise the position for the modeline list
	m_video_modes_position = 0;
}
//============================================================
//  xrandr_timing::~xrandr_timing
//============================================================

xrandr_timing::~xrandr_timing()
{
	// free the display
	if ( m_dpy != NULL )
		XCloseDisplay(m_dpy);
}

//============================================================
//  xrandr_timing::init
//============================================================

bool xrandr_timing::init()
{
	// select current display and root window
	// root is set at init to reduce open/close calls
	
	int s = -1; // 0 for first screen detected

	bool detected = false;
	
	// handle the screen name, "auto", "screen[0-9]" and XRANDR device name
	if (strlen(m_device_name) == 7 && !strncmp(m_device_name,"screen",6) && m_device_name[6]>='0' && m_device_name[6]<='9')
	{
		s = m_device_name[6]-'0';
		log_verbose("XRANDR: check for screen number %d\n",s);
	} 

	for (int scr = 0;scr<ScreenCount(m_dpy); scr++)
	{
		m_root = RootWindow(m_dpy, scr);
		detected = detect_connector(s);
	}

	//handle no screen detected case
	if(!detected)
		log_error("XRANDR: error, no screen detected\n");

	return detected;
}

//============================================================
//  xrandr_timing::init
//============================================================

bool xrandr_timing::detect_connector(int screen_pos)
{
	//  parameter screen_pos define screen order, 0 is default and equivalent to 'auto'

	XRRScreenResources *res = XRRGetScreenResourcesCurrent(m_dpy, m_root);

	// get screen size, rate and rotation from screen configuration
	XRRScreenConfiguration *sc = XRRGetScreenInfo(m_dpy, m_root);
	m_original_rate = XRRConfigCurrentRate(sc);
	m_original_size_id = XRRConfigCurrentConfiguration(sc, &m_original_rotation);
	XRRFreeScreenConfigInfo(sc);

	Rotation current_rotation = 0;
	int output_position = 0;
	for (int o = 0; o < res->noutput; o++)
	{
		XRROutputInfo *output_info = XRRGetOutputInfo (m_dpy, res, res->outputs[o]);
		if (!output_info)
			log_error("XRANDR: error could not get output 0x%x information\n", (uint) res->outputs[o]);

		// first connected output
		if (output_info->connection == RR_Connected)
		{
			log_verbose("XRANDR: check output connector '%s'\n", output_info->name);
			for (int j = 0; j < output_info->nmode; j++)
			{
				if ( output_info->crtc && !m_output_mode)
				{
					XRRCrtcInfo *crtc_info = XRRGetCrtcInfo(m_dpy, res, output_info->crtc);
					current_rotation = crtc_info->rotation;
					if (!strcmp(m_device_name, "auto") || !strcmp(m_device_name,output_info->name) || output_position == screen_pos)
					{
						log_verbose("XRANDR: select output connector '%s' as primary\n", output_info->name);
						// save the output connector
						m_output_primary = o;
						for (int m = 0; m < res->nmode && !m_output_mode; m++)
						{
							XRRModeInfo *mode = &res->modes[m];
							// get screen mode
							if (crtc_info->mode == mode->id)
							{
								m_output_mode = mode->id;
								m_width = crtc_info->x + crtc_info->width;
								m_height = crtc_info->y + crtc_info->height;
							}
						}
					}
					XRRFreeCrtcInfo(crtc_info);
				}
				if (current_rotation & 0xe) // screen rotation is left or right
				{
					m_crtc_flags = MODE_ROTATED;
					log_verbose("XRANDR: desktop rotation is %s\n",(current_rotation & 0x2)?"left":((current_rotation & 0x8)?"right":"inverted"));
				}
			}
			output_position++;
		}
		XRRFreeOutputInfo(output_info);
	}
	XRRFreeScreenResources(res);

	//return true is screen is detected
	return m_output_primary != -1;
}

//============================================================
//  xrandr_timing::restore_mode
//============================================================

bool xrandr_timing::restore_mode()
{
	//handle no screen detected case
	if (m_output_primary == -1)
	{
		log_error("XRANDR: error, no screen detected\n");
		return false;
	}

	// Restore desktop resolution
	XRRScreenResources *res = XRRGetScreenResourcesCurrent(m_dpy, m_root);
	XRRScreenConfiguration *sc = XRRGetScreenInfo(m_dpy, m_root);

	XRRSetScreenConfigAndRate(m_dpy, sc, m_root, m_original_size_id, m_original_rotation, m_original_rate, CurrentTime);
	XRRFreeScreenConfigInfo(sc);
	XRRFreeScreenResources(res);

	log_verbose("XRANDR: original video mode restored\n");

	return true;
}

//============================================================
//  xrandr_timing::add_mode
//============================================================

bool xrandr_timing::add_mode(modeline *mode)
{
	if (!mode)
		return false;

	//handle no screen detected case
	if (m_output_primary == -1)
	{
		log_error("XRANDR: error, no screen detected\n");
		return false;
	}

	// Add modeline to interface
	char name[48];
	sprintf(name,"GM-%dx%d_%.6f",mode->hactive, mode->vactive, mode->vfreq); // add ID

	// Setup the xrandr mode structure
	XRRModeInfo xmode;
	xmode.name       = name;
	xmode.nameLength = strlen(name);
	xmode.dotClock   = float(mode->pclock);
	xmode.width      = mode->hactive;
	xmode.hSyncStart = mode->hbegin;
	xmode.hSyncEnd   = mode->hend;
	xmode.hTotal     = mode->htotal;
	xmode.height     = mode->vactive;
	xmode.vSyncStart = mode->vbegin;
	xmode.vSyncEnd   = mode->vend;
	xmode.vTotal     = mode->vtotal;
	xmode.modeFlags  = (mode->interlace?RR_Interlace:0) | (mode->doublescan?RR_DoubleScan:0) | (mode->hsync?RR_HSyncPositive:RR_HSyncNegative) | (mode->vsync?RR_VSyncPositive:RR_VSyncNegative);
		
	mode->type |= CUSTOM_VIDEO_TIMING_XRANDR;

	// Create the modeline
	XSync(m_dpy, False);
	m_xerrors = 0;
	old_error_handler = XSetErrorHandler(error_handler);
	RRMode gmid = XRRCreateMode(m_dpy, m_root, &xmode);
	XSync(m_dpy, False);
	XSetErrorHandler(old_error_handler);
	if (m_xerrors)
		log_error("XRANDR: error in %s\n","XRRCreateMode");

	// Add new modeline to primary output
	XRRScreenResources *res = XRRGetScreenResourcesCurrent(m_dpy, m_root);

	XSync(m_dpy, False);
	m_xerrors = 0;
	old_error_handler = XSetErrorHandler(error_handler);
	XRRAddOutputMode(m_dpy, res->outputs[m_output_primary], gmid);
	XSync(m_dpy, False);
	XSetErrorHandler(old_error_handler);
	if (m_xerrors)
		log_error("XRANDR: error in %s\n","XRRAddOutputMode");

	XRRFreeScreenResources(res);

	return true;
}

//============================================================
//  xrandr_timing::set_mode
//============================================================

bool xrandr_timing::set_mode(modeline *mode)
{
	//handle no screen detected case
	if (m_output_primary == -1)
	{
		log_error("XRANDR: error, no screen detected\n");
		return false;
	}

	// Use xrandr to switch to new mode.
	char name[48];
	sprintf(name,"GM-%dx%d_%.6f",mode->hactive, mode->vactive, mode->vfreq); // add ID

	XRRScreenResources *res = XRRGetScreenResourcesCurrent(m_dpy, m_root);
	XRROutputInfo *output_info = XRRGetOutputInfo(m_dpy, res, res->outputs[m_output_primary]);
	XRRCrtcInfo *crtc_info = XRRGetCrtcInfo(m_dpy, res, output_info->crtc);

	// Select corresponding mode from modeline, can be enhanced by saving mode index to modeline structure
	XRRModeInfo *xmode=0;
	for (int m = 0; m < res->nmode; m++)
	{
		XRRModeInfo *tmp_mode = &res->modes[m];
		if (!strcmp(name, tmp_mode->name))
		{
			xmode = &res->modes[m];
		}
	}

	// Grab X server to prevent unwanted interaction from the window manager
	XGrabServer(m_dpy);

	// Disable all CRTCs
	for (int i = 0; i < output_info->ncrtc; i++)
	{
		if (XRRSetCrtcConfig(m_dpy, res, output_info->crtcs[i], CurrentTime, 0, 0, None, RR_Rotate_0, NULL, 0) != RRSetConfigSuccess)
			log_error("XRANDR: error when disabling CRTC\n");
	}
	log_verbose("XRANDR: CRTC %d: mode %#lx, %ux%u+%d+%d.\n", 0, crtc_info->mode,crtc_info->width, crtc_info->height, crtc_info->x, crtc_info->y);

	// Check if framebuffer size is correct
	int change_resolution = 0;
	if (m_width < crtc_info->x + mode->hactive)
	{
		m_width = crtc_info->x + mode->hactive;
		change_resolution = 1;
	}
	if (m_height < crtc_info->y + mode->vactive)
	{
		m_height = crtc_info->y + mode->vactive;
		change_resolution = 1;
	}

	// Enlarge the screen size for the new mode
	if (change_resolution)
	{
		log_verbose("XRANDR: change screen size\n");
		XSync(m_dpy, False);
		m_xerrors = 0;
		old_error_handler = XSetErrorHandler(error_handler);
		XRRSetScreenSize(m_dpy, m_root, m_width, m_height, (25.4 * m_width) / 96.0, (25.4 * m_height) / 96.0);
		XSync(m_dpy, False);
		XSetErrorHandler(old_error_handler);
		if (m_xerrors)
			log_error("XRANDR: error in %s\n","XRRSetScreenSize");
	}

	// Switch to new modeline
	XSync(m_dpy, False);
	m_xerrors = 0;
	old_error_handler = XSetErrorHandler(error_handler);
	XRRSetCrtcConfig(m_dpy, res, output_info->crtc, CurrentTime , crtc_info->x, crtc_info->y, xmode->id, m_original_rotation, crtc_info->outputs, crtc_info->noutput);
	XSync(m_dpy, False);
	XSetErrorHandler(old_error_handler);

	XRRFreeCrtcInfo(crtc_info);

	if (m_xerrors)
		log_error("XRANDR: error in %s\n","XRRSetCrtcConfig");

	// Release X server, events can be processed now
	XUngrabServer(m_dpy);

	crtc_info = XRRGetCrtcInfo(m_dpy, res, output_info->crtc); // recall crtc to settle parameters

	// If the crtc config modeline change fails, revert to original mode (prevents ending with black screen due to all crtc disabled)
	if (crtc_info->mode == 0)
	{
		log_error("XRANDR: error switching resolution, original mode restored\n");
		XRRScreenConfiguration *sc = XRRGetScreenInfo(m_dpy, m_root);
		XRRSetScreenConfigAndRate(m_dpy, sc, m_root, m_original_size_id, m_original_rotation, m_original_rate, CurrentTime);
		XRRFreeScreenConfigInfo(sc);
	}

	// check, verify current active mode
	for (int m = 0; m < res->nmode; m++)
	{
		XRRModeInfo *mode = &res->modes[m];
		if (mode->id == crtc_info->mode)
		log_verbose("XRANDR: mode %d id 0x%04x name %s clock %6.6fMHz\n", m, (int)mode->id, mode->name, (double)mode->dotClock / 1000000.0);
	}

	XRRFreeCrtcInfo(crtc_info);
	XRRFreeOutputInfo(output_info);
	XRRFreeScreenResources(res);

	return true;
}

//============================================================
//  xrandr_timing::delete_mode
//============================================================

bool xrandr_timing::delete_mode(modeline *mode)
{
	//handle no screen detected case
	if (m_output_primary == -1)
	{
		log_error("XRANDR: error, no screen detected\n");
		return false;
	}

	if (!mode)
		return false;

	char name[48];
	sprintf(name,"GM-%dx%d_%.6f",mode->hactive, mode->vactive, mode->vfreq); // add ID

	XRRScreenResources *res = XRRGetScreenResourcesCurrent (m_dpy, m_root);

	// Delete modeline
	for (int m = 0; m < res->nmode; m++)
	{
		XRRModeInfo *xmode = &res->modes[m];
		if (!strcmp(name, xmode->name))
		{
			XSync(m_dpy, False);
			m_xerrors = 0;
			old_error_handler = XSetErrorHandler(error_handler);
			XRRDeleteOutputMode (m_dpy, res->outputs[m_output_primary], xmode->id);
			if (m_xerrors)
				log_error("XRANDR: error in %s\n","XRRDeleteOutputMode");

			m_xerrors = 0;
			XRRDestroyMode (m_dpy, xmode->id);
			XSync(m_dpy, False);
			XSetErrorHandler(old_error_handler);
			if (m_xerrors)
				log_error("XRANDR: error in %s\n","XRRDestroyMode");
		}
	}

	XRRFreeScreenResources(res);

	return true;
}

//============================================================
//  pstrip_timing::set_timing
//============================================================

bool xrandr_timing::set_timing(modeline *mode)
{
	if ( mode->type & MODE_DESKTOP )
		return restore_mode();

	return set_mode(mode);
}
//============================================================
//  pstrip_timing::get_timing
//============================================================

bool xrandr_timing::get_timing(modeline *mode)
{
	//handle no screen detected case
	if (m_output_primary == -1)
	{
		log_error("XRANDR: error, no screen detected\n");
		return false;
	}

	XRRScreenResources *res = XRRGetScreenResourcesCurrent(m_dpy, m_root);
	XRROutputInfo *output_info = XRRGetOutputInfo(m_dpy, res, res->outputs[m_output_primary]);

	// cycle through the modelines and report them back to the display manager
	if (m_video_modes_position < output_info->nmode)
	{
		for (int m = 0;m<res->nmode;m++)
		{
			XRRModeInfo *xmode = &res->modes[m];

			if (xmode->id==output_info->modes[m_video_modes_position]) 
			{
				mode->pclock  	= xmode->dotClock;
				mode->hactive 	= xmode->width;
				mode->hbegin  	= xmode->hSyncStart;
				mode->hend    	= xmode->hSyncEnd;
				mode->htotal  	= xmode->hTotal;
				mode->vactive 	= xmode->height;
				mode->vbegin  	= xmode->vSyncStart;
				mode->vend    	= xmode->vSyncEnd;
				mode->vtotal  	= xmode->vTotal;
				mode->interlace = (xmode->modeFlags & RR_Interlace)?1:0;
				mode->doublescan = (xmode->modeFlags & RR_DoubleScan)?1:0;
				mode->hsync     = (xmode->modeFlags & RR_HSyncPositive)?1:0;
				mode->vsync     = (xmode->modeFlags & RR_VSyncPositive)?1:0;

				mode->hfreq 	= mode->pclock / mode->htotal;
				mode->vfreq 	= mode->hfreq / mode->vtotal * (mode->interlace?2:1);
				mode->refresh 	= mode->vfreq;

				mode->width	= xmode->width;
				mode->height	= xmode->height;
		
				mode->type |= m_crtc_flags; // the rotation is defined by crtc
				mode->type |= CUSTOM_VIDEO_TIMING_SYSTEM;
		
				if (m_output_mode == (int)xmode->id)
					mode->type |= MODE_DESKTOP;
			}
		} 
		m_video_modes_position++;
	}

	XRRFreeOutputInfo(output_info);
	XRRFreeScreenResources(res);

	return true;
}
