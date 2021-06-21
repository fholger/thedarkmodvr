/*****************************************************************************
The Dark Mod GPL Source Code

This file is part of the The Dark Mod Source Code, originally based
on the Doom 3 GPL Source Code as published in 2011.

The Dark Mod Source Code is free software: you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation, either version 3 of the License,
or (at your option) any later version. For details, see LICENSE.TXT.

Project: The Dark Mod (http://www.thedarkmod.com/)

******************************************************************************/

#ifndef __SLIDERWINDOW_H__
#define __SLIDERWINDOW_H__

class idUserInterfaceLocal;

class idSliderWindow : public idWindow {
public:
						idSliderWindow(idUserInterfaceLocal *gui);
						idSliderWindow(idDeviceContext *d, idUserInterfaceLocal *gui);
	virtual				~idSliderWindow();

	void				InitWithDefaults(const char *_name, const idRectangle &rect, const idVec4 &foreColor, const idVec4 &matColor, const char *_background, const char *thumbShader, bool _vertical, bool _scrollbar);

	void				SetRange(float _low, float _high, float _step);
	float				GetLow() { return low; }
	float				GetHigh() { return high; }

	void				SetValue(float _value);
	float				GetValue() { return value; };

	virtual size_t		Allocated(){return idWindow::Allocated();};
	virtual idWinVar *	GetWinVarByName(const char *_name, bool winLookup = false, drawWin_t** owner = NULL);
	virtual const char *HandleEvent(const sysEvent_t *event, bool *updateVisuals);
	virtual void		PostParse();
	virtual void		Draw(int time, float x, float y);
	virtual void		DrawBackground(const idRectangle &drawRect);
	virtual const char *RouteMouseCoords(float xd, float yd);
	virtual void		Activate(bool activate, idStr &act);
	virtual void		SetBuddy(idWindow *buddy);

	void				RunNamedEvent( const char* eventName );
	
private:
	virtual bool		ParseInternalVar(const char *name, idParser *src);
	void				CommonInit();
	void				InitCvar();
						// true: read the updated cvar from cvar system
						// false: write to the cvar system
						// force == true overrides liveUpdate 0
	void				UpdateCvar( bool read, bool force = false );
	
	idWinFloat			value;
	float				low;
	float				high;
	float				thumbWidth;
	float				thumbHeight;
	float				stepSize;
	float				lastValue;
	idRectangle			thumbRect;
	const idMaterial *	thumbMat;
	bool				vertical;
	bool				verticalFlip;
	bool				scrollbar;
	idWindow *			buddyWin;
	idStr				thumbShader;
	
	idWinStr			cvarStr;
	idCVar *			cvar;
	bool				cvar_init;
	idWinBool			liveUpdate;
	idWinStr			cvarGroup;	
};

#endif /* !__SLIDERWINDOW_H__ */

