/*
 * Bookr: document reader for the Sony PSP
 * Copyright (C) 2005 Carlos Carrasco Martinez (carloscm at gmail dot com),
 *               2007 Christian Payeur (christian dot payeur at gmail dot com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "fzinstreammem.h"
#include "fzscreen.h"

#include "bklayer.h"

#include <cmath>

FZFont* BKLayer::fontBig = 0;
FZFont* BKLayer::fontSmall = 0;
FZTexture* BKLayer::texUI = 0;
FZTexture* BKLayer::texUI2 = 0;
FZTexture* BKLayer::texLogo = 0;

extern "C" {
extern unsigned int size_res_logo;
extern unsigned char res_logo[];
extern unsigned int size_res_uitex;
extern unsigned char res_uitex[];
extern unsigned int size_res_uitex2;
extern unsigned char res_uitex2[];
extern unsigned int size_res_uifont;
extern unsigned char res_uifont[];
};

void BKLayer::load() {
	fontBig = FZFont::createFromMemory(res_uifont, size_res_uifont, 14, false);
	fontBig->texEnv(FZ_TEX_MODULATE);
	fontBig->filter(FZ_NEAREST, FZ_NEAREST);

	fontSmall = FZFont::createFromMemory(res_uifont, size_res_uifont, 11, false);
	fontSmall->texEnv(FZ_TEX_MODULATE);
	fontSmall->filter(FZ_NEAREST, FZ_NEAREST);

	FZInputStreamMem* ins = FZInputStreamMem::create((char*)res_uitex, size_res_uitex);
	FZImage* image = FZImage::createFromPNG(ins);
	ins->release();
	ins = 0;
	texUI = FZTexture::createFromImage(image, false);
	texUI->texEnv(FZ_TEX_MODULATE);
	texUI->filter(FZ_NEAREST, FZ_NEAREST);
	image->release();

	ins = FZInputStreamMem::create((char*)res_uitex2, size_res_uitex2);
	image = FZImage::createFromPNG(ins);
	ins->release();
	ins = 0;
	texUI2 = FZTexture::createFromImage(image, false);
	texUI2->texEnv(FZ_TEX_MODULATE);
	texUI2->filter(FZ_NEAREST, FZ_NEAREST);
	image->release();

	ins = FZInputStreamMem::create((char*)res_logo, size_res_logo);
	image = FZImage::createFromPNG(ins);
	ins->release();
	ins = 0;
	texLogo = FZTexture::createFromImage(image, false);
	texLogo->texEnv(FZ_TEX_REPLACE);
	texLogo->filter(FZ_NEAREST, FZ_NEAREST);
	image->release();
}

struct T32FV32F2D {
	float u,v;
	float x,y,z;
};

void BKLayer::drawImage(int x, int y, int w, int h, int tx, int ty) {
	struct T32FV32F2D vertices[2] = {
		{ tx, ty, x, y, 0 },
		{ tx + w, ty + h, x + w, y + h, 0 }
	};
	T32FV32F2D* verts = (T32FV32F2D*)FZScreen::getListMemory(2*sizeof(struct T32FV32F2D));
	memcpy(verts, vertices, 2 * sizeof(struct T32FV32F2D));
	FZScreen::drawArray(FZ_SPRITES,FZ_TEXTURE_32BITF|FZ_VERTEX_32BITF|FZ_TRANSFORM_2D,2,0,verts);
}

void BKLayer::drawImageScale(int x, int y, int w, int h, int tx, int ty, int tw, int th) {
	struct T32FV32F2D vertices[2] = {
		{ tx, ty, x, y, 0 },
		{ tx + tw, ty + th, x + w, y + h, 0 }
	};
	T32FV32F2D* verts = (T32FV32F2D*)FZScreen::getListMemory(2*sizeof(struct T32FV32F2D));
	memcpy(verts, vertices, 2 * sizeof(struct T32FV32F2D));
	FZScreen::drawArray(FZ_SPRITES,FZ_TEXTURE_32BITF|FZ_VERTEX_32BITF|FZ_TRANSFORM_2D,2,0,verts);
}

void BKLayer::drawPill(int x, int y, int w, int h, int r, int tx, int ty) {
	struct T32FV32F2D vertices[7*2] = {
		{ tx, ty, x, y, 0 },
		{ tx + r, ty + r, x + r, y + r, 0 },

		{ tx + r, ty, x + w, y, 0 },
		{ tx, ty + r, x + w + r, y + r, 0 },

		{ tx, ty + r, x, y + h, 0 },
		{ tx + r, ty, x + r, y + h + r, 0 },

		{ tx + r, ty + r, x + w, y + h, 0 },
		{ tx, ty, x + w + r, y + h + r, 0 },

		{ tx + r, ty + r, x + r, y, 0 },
		{ tx + r, ty + r, x + w, y + h + r, 0 },

		{ tx + r, ty + r, x, y + r, 0 },
		{ tx + r, ty + r, x + r, y + h, 0 },

		{ tx + r, ty + r, x + w, y + r, 0 },
		{ tx + r, ty + r, x + r + w, y + h, 0 }
	};
	T32FV32F2D* verts = (T32FV32F2D*)FZScreen::getListMemory(2 * 7 * sizeof(struct T32FV32F2D));
	memcpy(verts, vertices, 2 * 7 * sizeof(struct T32FV32F2D));
	FZScreen::drawArray(FZ_SPRITES,FZ_TEXTURE_32BITF|FZ_VERTEX_32BITF|FZ_TRANSFORM_2D,14,0,verts);
}

void BKLayer::drawTPill(int x, int y, int w, int h, int r, int tx, int ty) {
	struct T32FV32F2D vertices[5*2] = {
		{ tx, ty, x, y, 0 },
		{ tx + r, ty + r, x + r, y + r, 0 },

		{ tx + r, ty, x + w, y, 0 },
		{ tx, ty + r, x + w + r, y + r, 0 },

		/*{ tx, ty + r, x, y + h, 0 },
		{ tx + r, ty, x + r, y + h + r, 0 },

		{ tx + r, ty + r, x + w, y + h, 0 },
		{ tx, ty, x + w + r, y + h + r, 0 },*/

		{ tx + r, ty + r, x + r, y, 0 },
		{ tx + r, ty + r, x + w, y + h + r, 0 },

		{ tx + r, ty + r, x, y + r, 0 },
		{ tx + r, ty + r, x + r, y + h, 0 },

		{ tx + r, ty + r, x + w, y + r, 0 },
		{ tx + r, ty + r, x + r + w, y + h, 0 }
	};
	T32FV32F2D* verts = (T32FV32F2D*)FZScreen::getListMemory(2 * 5 * sizeof(struct T32FV32F2D));
	memcpy(verts, vertices, 2 * 5 * sizeof(struct T32FV32F2D));
	FZScreen::drawArray(FZ_SPRITES,FZ_TEXTURE_32BITF|FZ_VERTEX_32BITF|FZ_TRANSFORM_2D,10,0,verts);
}

void BKLayer::drawRect(int x, int y, int w, int h, int r, int tx, int ty) {
	struct T32FV32F2D vertices[1*2] = {
		{ tx + r, ty + r, x, y, 0 },
		{ tx + r, ty + r, x + w, y + h, 0 }
	};
	T32FV32F2D* verts = (T32FV32F2D*)FZScreen::getListMemory(2 * 1 * sizeof(struct T32FV32F2D));
	memcpy(verts, vertices, 2 * 1 * sizeof(struct T32FV32F2D));
	FZScreen::drawArray(FZ_SPRITES,FZ_TEXTURE_32BITF|FZ_VERTEX_32BITF|FZ_TRANSFORM_2D,2,0,verts);
}

void BKLayer::drawNeoRect(int x, int y, int w, int h, unsigned int color) {
	FZScreen::ambientColor(color);
	drawRect(x, y, w, h, 6, 31, 1);
}

// flat box with a solid ink border and a hard ink drop shadow, texUI must be bound.
// Drawn as non-overlapping strips so translucent (fading) boxes stay even.
void BKLayer::drawNeoBox(int x, int y, int w, int h, unsigned int fill, int border, int shadow, unsigned int ink) {
	if (shadow > 0) {
		drawNeoRect(x + w, y + shadow, shadow, h, ink);
		drawNeoRect(x + shadow, y + h, w - shadow, shadow, ink);
	}
	drawNeoRect(x, y, w, border, ink);
	drawNeoRect(x, y + h - border, w, border, ink);
	drawNeoRect(x, y + border, border, h - 2*border, ink);
	drawNeoRect(x + w - border, y + border, border, h - 2*border, ink);
	drawNeoRect(x + border, y + border, w - 2*border, h - 2*border, fill);
}

int BKLayer::textWidthRange(char* t, int n, FZFont* font) {
	FZCharMetrics* fontChars = font->getMetrics();
	int baseX = 0;
	for (unsigned char *p = (unsigned char*)t; *p != 0; p++) {
		int idx = *p;
		// printable & white space
		if (idx >= 32)
			baseX += fontChars[idx].xadvance;
	}
	return baseX;
}

int BKLayer::textW(char* t, FZFont* font) {
	FZCharMetrics* fontChars = font->getMetrics();
	// precalc vertex count
	int vc = 0;
	for (unsigned char *p = (unsigned char*)t; *p != 0; p++) {
		if (*p <= 32)
			continue;
		vc += 2;
	}
	// lame
	if (vc == 0)
		return 0;
	int baseX = 0;
	for (unsigned char *p = (unsigned char*)t; *p != 0; p++) {
		int idx = *p;
		// new line
		if (idx == 10) {
			return baseX;
		}
		// white space
		if (idx == 32) {
			baseX += fontChars[idx].xadvance;
			continue;
		}
		// printable
		if (idx > 32) {
			baseX += fontChars[idx].xadvance;
			continue;
		}
	}
	return baseX;
}

void BKLayer::drawTextHC(char* t, FZFont* font, int y) {
	int w = textW(t, font);
	drawText(t, font, (480 - w) / 2, y);
}

int BKLayer::drawText(char* t, FZFont* font, int x, int y, int n, bool useLF, bool usePS, float ps, bool use3D) {
	if (n < 0) {
		n = strlen(t);
	}
	FZCharMetrics* fontChars = font->getMetrics();
	// precalc vertex count
	int vc = 0;
	int i = 0;
	for (unsigned char *p = (unsigned char*)t; i < n; i++, p++) {
		if (*p <= 32)
			continue;
		vc += 2;
	}
	// lame
	if (vc == 0)
		return 0;
	// build varray
	T32FV32F2D* vertices = (T32FV32F2D*)FZScreen::getListMemory(vc * sizeof(struct T32FV32F2D));
	// fill varray
	int iv = 0;
	int baseX = x;
	int baseY = y;
	i = 0;
	float fx = 0.0f;
	for (unsigned char *p = (unsigned char*)t; i < n; i++, p++) {
		int idx = *p;
		// new line
		if (idx == 10 && useLF) {
			baseY += font->getLineHeight()*(BKUser::options.txtHeightPct/100.0);
			baseX = x;
			fx = 0.0f;
		}
		// white space
		if (idx == 32) {
			if (usePS)
				fx += ps;
			else
				baseX += fontChars[idx].xadvance;
			continue;
		}
		// printable
		if (idx > 32) {
			int topleft = iv;
			int botright = topleft + 1;
			int cx = usePS ? x + int(floor(fx)) : baseX;

			vertices[topleft].u = fontChars[idx].x;
			vertices[topleft].v = fontChars[idx].y;
			vertices[topleft].x = cx + fontChars[idx].xoffset;
			vertices[topleft].y = baseY + fontChars[idx].yoffset;
			vertices[topleft].z = 0;

			vertices[botright].u = fontChars[idx].x + fontChars[idx].width;
			vertices[botright].v = fontChars[idx].y + fontChars[idx].height;
			vertices[botright].x = cx + fontChars[idx].xoffset + fontChars[idx].width;
			vertices[botright].y = baseY + fontChars[idx].yoffset + fontChars[idx].height;
			vertices[botright].z = 0;

			baseX += fontChars[idx].xadvance;
			if (usePS)
				fx += float(fontChars[idx].xadvance);
			iv+=2;
			continue;
		}
	}
	if (use3D) {
		// no quads on psp so we need triangles
		T32FV32F2D* vertices3d = (T32FV32F2D*)FZScreen::getListMemory(vc * sizeof(struct T32FV32F2D) * 3);
		int i3d = 0;
		for (iv = 0; iv < vc; iv+=2, i3d+=6) {
			int topleft = iv;
			int botright = topleft + 1;

			// tri 1
			vertices3d[i3d] = vertices[topleft];

			vertices3d[i3d+1].u = vertices[botright].u;
			vertices3d[i3d+1].v = vertices[topleft].v;
			vertices3d[i3d+1].x = vertices[botright].x;
			vertices3d[i3d+1].y = vertices[topleft].y;
			vertices3d[i3d+1].z = 0;

			vertices3d[i3d+2].u = vertices[topleft].u;
			vertices3d[i3d+2].v = vertices[botright].v;
			vertices3d[i3d+2].x = vertices[topleft].x;
			vertices3d[i3d+2].y = vertices[botright].y;
			vertices3d[i3d+2].z = 0;

			// tri 2
			vertices3d[i3d+3] = vertices3d[i3d+1];
			vertices3d[i3d+4] = vertices[botright];
			vertices3d[i3d+5] = vertices3d[i3d+2];
		}
		FZScreen::drawArray(FZ_TRIANGLES,FZ_TEXTURE_32BITF|FZ_VERTEX_32BITF|FZ_TRANSFORM_3D,vc * 3,0,vertices3d);
	} else {
		FZScreen::drawArray(FZ_SPRITES,FZ_TEXTURE_32BITF|FZ_VERTEX_32BITF|FZ_TRANSFORM_2D,vc,0,vertices);
	}
	return baseX;
}

void BKLayer::drawDialogFrame(string& title, string& triangleLabel, string& circleLabel, int flags) {
	int scrY = 0;
	char *t =(char*)circleLabel.c_str();
	int tw = textW(t, fontBig);

	texUI->bindForDisplay();
	// back
	drawNeoBox(18, 16 + scrY, 440, 272, NEO_CREAM, 3, 5);
	// title
	drawNeoRect(21, 19 + scrY, 434, 28, NEO_YELLOW);
	drawNeoRect(21, 47 + scrY, 434, 3, NEO_INK);
	// context label
	drawNeoRect(21, 239 + scrY, 434, 3, NEO_INK);
	drawNeoRect(21, 242 + scrY, 434, 30, NEO_CYAN);
	// icons
	FZScreen::ambientColor(NEO_INK);
	switch (BKUser::controls.select) {
	case FZ_REPS_CROSS:
		drawImage(428, 23 + scrY, BK_IMG_CIRCLE_XSIZE, BK_IMG_CIRCLE_YSIZE, BK_IMG_CIRCLE_X, BK_IMG_CIRCLE_Y);
		break;
	case FZ_REPS_CIRCLE:
	default:
		drawImage(428, 25 + scrY, BK_IMG_CROSS_XSIZE, BK_IMG_CROSS_YSIZE, BK_IMG_CROSS_X, BK_IMG_CROSS_Y);
		break;
	}

	// circle or other context icon
	if (flags & BK_MENU_ITEM_USE_LR_ICON) {
		drawImage(480 - tw - 65, 248 + scrY, BK_IMG_LRARROWS_XSIZE, BK_IMG_LRARROWS_YSIZE, BK_IMG_LRARROWS_X, BK_IMG_LRARROWS_Y);
	} else {
		switch(BKUser::controls.select) {
		case FZ_REPS_CROSS:
			drawImage(480 - tw - 65, 248 + scrY, BK_IMG_CROSS_XSIZE, BK_IMG_CROSS_YSIZE, BK_IMG_CROSS_X, BK_IMG_CROSS_Y);
			break;
		case FZ_REPS_CIRCLE:
		default:
			drawImage(480 - tw - 65, 248 + scrY, BK_IMG_CIRCLE_XSIZE, BK_IMG_CIRCLE_YSIZE, BK_IMG_CIRCLE_X, BK_IMG_CIRCLE_Y);
			break;
		}
	}
	if (triangleLabel.size() > 0 || flags & BK_MENU_ITEM_OPTIONAL_TRIANGLE_LABEL) {
		drawImage(37, 248 + scrY, BK_IMG_TRIANGLE_XSIZE, BK_IMG_TRIANGLE_YSIZE, BK_IMG_TRIANGLE_X, BK_IMG_TRIANGLE_Y);
	}

	fontBig->bindForDisplay();

	// title
	FZScreen::ambientColor(NEO_INK);
	drawText((char*)title.c_str(), fontBig, 31, 25 + scrY);
	// labels
	if (triangleLabel.size() > 0) {
		drawText((char*)triangleLabel.c_str(), fontBig, 37 + 25, 248 + scrY);
	}
	drawText(t, fontBig, 480 - tw - 40, 248 + scrY);
}

void BKLayer::drawMenu(string& title, string& triangleLabel, vector<BKMenuItem>& items) {
	int selPos = selItem - topItem;
	int scrY = 0;
	int lh = fontBig->getLineHeight();

	if (selPos < 0) {
		topItem += selPos;
		selPos = 0;
	}

	if (selPos > 7) {
		topItem += selPos - 7;
		selPos = 7;
	}

	bool scrollbar = items.size() > 8;

	string tl(triangleLabel);
	if (items[selItem].flags & BK_MENU_ITEM_OPTIONAL_TRIANGLE_LABEL) {
		tl = items[selItem].triangleLabel;
	}
	drawDialogFrame(title, tl, items[selItem].circleLabel, items[selItem].flags);

	texUI->bindForDisplay();
	// selected item
	int wSelBox = scrollbar ? 400 : 424;
	drawNeoBox(28, 57 + scrY + selPos*lh, wSelBox, 22, NEO_PINK, 2, 3);

	// scrollbar
	if (scrollbar) {
		float barh = 8.0f / float(items.size());
		barh *= 170.0f;
		if (barh < 15.0f)
			barh = 15.0f;
		float trel = float(topItem) / float(items.size());
		trel *= 170.0f;
		if (trel + barh > 170.0f)
			trel = 170.0f - barh;
		drawNeoBox(434, 56, 18, 178, NEO_WHITE, 2, 0);
		drawNeoRect(438, 60 + int(trel), 10, int(barh), NEO_INK);
	}

	// color rects items
	for (int i = 0; i < 8; ++i) {
		if ((60 + (i+1)*lh) > 250)
			break;
		if ((i + topItem) >= (int)(items.size()))
			break;
		if (items[i + topItem].flags & BK_MENU_ITEM_COLOR_RECT) {
			int tw = textW((char*)items[i + topItem].label.c_str(), fontBig);
			drawNeoBox(40 + 25 + tw + 10, 60 + i*lh + scrY, 30, 15, items[i + topItem].bgcolor | 0xff000000, 2, 0);
			drawNeoBox(40 + 25 + tw + 15, 60 + i*lh + scrY + 4, 30, 15, items[i + topItem].fgcolor | 0xff000000, 2, 0);
		}
	}

	// folder icons
	FZScreen::ambientColor(NEO_INK);
	for (int i = 0; i < 8; ++i) {
		if ((60 + (i+1)*lh) > 250)
			break;
		if ((i + topItem) >= (int)(items.size()))
			break;
		if (items[i + topItem].flags & BK_MENU_ITEM_FOLDER) {
			drawImage(40, 60 + i*lh + scrY, BK_IMG_FOLDER_XSIZE, BK_IMG_FOLDER_YSIZE, BK_IMG_FOLDER_X, BK_IMG_FOLDER_Y);
		}
	}

	fontBig->bindForDisplay();

	// contents
	FZScreen::ambientColor(NEO_INK);
	for (int i = 0; i < 8; ++i) {
		if ((60 + (i+1)*lh) > 250)
			break;
		if ((i + topItem) >= (int)(items.size()))
			break;
		drawText((char*)items[i + topItem].label.c_str(), fontBig, 40 + 25, 60 + i*lh + scrY);
	}
}

static int countLines(string& t) {
	int lines = 1;
	char* c = (char*)t.c_str();
	while (*c != 0) {
		if (*c == 0xa) ++lines;
		++c;
	}
	return lines;
}

void BKLayer::drawPopup(string& text, string& title, int bg1, int bg2, int fg) {
	texUI->bindForDisplay();
	int l = countLines(text);
	int h = 25 + 25*l;
	int y = (272 - h) /2;
	// back
	drawNeoBox(30, y - 3, 420, h + 16, bg1, 3, 5);
	// title
	drawNeoRect(33, y, 414, 27, bg2);
	drawNeoRect(33, y + 27, 414, 3, NEO_INK);
	// icons
	FZScreen::ambientColor(NEO_INK);
	switch (BKUser::controls.select) {
	case FZ_REPS_CIRCLE:
		drawImage(420, 5 + y, BK_IMG_CROSS_XSIZE, BK_IMG_CROSS_YSIZE, BK_IMG_CROSS_X, BK_IMG_CROSS_Y);
		break;
	case FZ_REPS_CROSS:
	default:
		drawImage(420, 4 + y, BK_IMG_CIRCLE_XSIZE, BK_IMG_CIRCLE_YSIZE, BK_IMG_CIRCLE_X, BK_IMG_CIRCLE_Y);
		break;
	}

	fontBig->bindForDisplay();

	FZScreen::ambientColor(fg);
	drawText((char*)title.c_str(), fontBig, 43, y + 6);
	drawText((char*)text.c_str(), fontBig, 43, y + 37);
}

void BKLayer::drawClockAndBattery(string& extra) {
	texUI->bindForDisplay();
	FZScreen::ambientColor(NEO_INK);
	drawImage(357, 226, BK_IMG_BATTERY_XSIZE, BK_IMG_BATTERY_YSIZE, BK_IMG_BATTERY_X, BK_IMG_BATTERY_Y);
	drawImage(405, 222, BK_IMG_CLOCK_XSIZE, BK_IMG_CLOCK_YSIZE, BK_IMG_CLOCK_X, BK_IMG_CLOCK_Y);
	drawImage(298, 224, BK_IMG_MEMORY_XSIZE, BK_IMG_MEMORY_YSIZE, BK_IMG_MEMORY_X, BK_IMG_MEMORY_Y);
	fontSmall->bindForDisplay();
	FZScreen::ambientColor(NEO_INK);
	int ew = textW((char*)extra.c_str(), fontSmall);
	drawText((char*)extra.c_str(), fontSmall, 480 - 30 - ew, 205);
	int h = 0, m = 0;
	FZScreen::getTime(h, m);
	int b = FZScreen::getBattery();
	int mem = FZScreen::getUsedMemory();
	int speed = FZScreen::getSpeed();
	char t1[20];
	snprintf(t1, 20, "%02d:%02d", h, m);
	char t2[20];
	snprintf(t2, 20, "%d%%", b);
	char t3[20];
//	snprintf(t3, 20, "%.1fM", ((float)(mem)) / (1024.0f*1024.0f));
	// used / largest free block, in MB
	snprintf(t3, 20, "%d/%dM", mem / (1024*1024), FZScreen::getFreeMemory() / (1024*1024));
	char t4[20];
	snprintf(t4, 20, "%dMHz", speed);
	drawText(t1, fontSmall, 423, 224);
	drawText(t2, fontSmall, 375, 224);
	drawText(t3, fontSmall, 315, 224);
	drawText(t4, fontSmall, 298, 205);
}

void BKLayer::menuCursorUpdate(unsigned int buttons, int max) {
	int* b = FZScreen::ctrlReps();
	if (b[BKUser::controls.menuUp] == 1 || b[BKUser::controls.menuUp] > 20) {
		selItem--;
		if (selItem < 0) {
			selItem = max - 1;
		}
	}
	if (b[BKUser::controls.menuDown] == 1 || b[BKUser::controls.menuDown] > 20) {
		selItem++;
		if (selItem >= max) {
			selItem = 0;
		}
	}
}

BKLayer::BKLayer() : topItem(0), selItem(0) {
}

BKLayer::~BKLayer() {
}

