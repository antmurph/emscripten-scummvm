/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */

/*
 * This file is based on WME Lite.
 * http://dead-code.org/redir.php?target=wmelite
 * Copyright (c) 2011 Jan Nedoma
 */

#ifndef WINTERMUTE_BSUBFRAME_H
#define WINTERMUTE_BSUBFRAME_H


#include "engines/wintermute/Base/BBase.h"
#include "engines/wintermute/Base/BScriptable.h"

namespace WinterMute {
class CBObject;
class CBSurface;
class CBSubFrame : public CBScriptable {
public:
	bool _mirrorX;
	bool _mirrorY;
	bool _decoration;
	ERRORCODE setSurface(const char *filename, bool defaultCK = true, byte ckRed = 0, byte ckGreen = 0, byte ckBlue = 0, int lifeTime = -1, bool keepLoaded = false);
	ERRORCODE setSurfaceSimple();
	DECLARE_PERSISTENT(CBSubFrame, CBScriptable)
	void setDefaultRect();
	uint32 _transparent;
	ERRORCODE saveAsText(CBDynBuffer *buffer, int indent) { return saveAsText(buffer, indent, true); }
	ERRORCODE saveAsText(CBDynBuffer *buffer, int indent, bool complete);
	bool _editorSelected;
	CBSubFrame(CBGame *inGame);
	virtual ~CBSubFrame();
	ERRORCODE loadBuffer(byte *buffer, int lifeTime, bool keepLoaded);
	ERRORCODE draw(int x, int y, CBObject *registerOwner = NULL, float zoomX = 100, float zoomY = 100, bool precise = true, uint32 alpha = 0xFFFFFFFF, float rotate = 0.0f, TSpriteBlendMode blendMode = BLEND_NORMAL);
	bool getBoundingRect(Common::Rect *rect, int x, int y, float scaleX = 100, float scaleY = 100);

	int _hotspotX;
	int _hotspotY;
	uint32 _alpha;
	Common::Rect _rect;

	bool _cKDefault;
	byte _cKRed;
	byte _cKGreen;
	byte _cKBlue;
	int _lifeTime;
	bool _keepLoaded;
	char *_surfaceFilename;

	bool _2DOnly;
	bool _3DOnly;

	CBSurface *_surface;

	// scripting interface
	virtual CScValue *scGetProperty(const char *name);
	virtual ERRORCODE scSetProperty(const char *name, CScValue *value);
	virtual ERRORCODE scCallMethod(CScScript *script, CScStack *stack, CScStack *thisStack, const char *name);
	virtual const char *scToString();

};

} // end of namespace WinterMute

#endif
