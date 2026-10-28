/*
 * Bookr: document reader for the Sony PSP
 * Copyright (C) 2005 Carlos Carrasco Martinez (carloscm at gmail dot com)
 *               2009 Nguyen Chi Tam (nguyenchitam at gmail dot com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
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

#include <string.h>
#include "fzscreen.h"
#include <pspthreadman.h>
#include "bklogo.h"

BKLogo::BKLogo() : loading(false), error(false), text("") {
}

BKLogo::~BKLogo() {
}

void BKLogo::setLoading(bool v) {
	loading = v;
}

void BKLogo::setError(bool v) {
	error = v;
}

int BKLogo::update(unsigned int buttons) {
	int* b = FZScreen::ctrlReps();
	if (b[BKUser::controls.showMainMenu] == 1) {
		return BK_CMD_INVOKE_MENU;
	}
	return 0;
}

void BKLogo::render() {
	FZScreen::clear(NEO_CREAM & 0xffffff, FZ_COLOR_BUFFER);
	FZScreen::color(0xffffffff);
	FZScreen::matricesFor2D();
	FZScreen::enable(FZ_TEXTURE_2D);
	FZScreen::enable(FZ_BLEND);
	FZScreen::blendFunc(FZ_ADD, FZ_SRC_ALPHA, FZ_ONE_MINUS_SRC_ALPHA);
	texLogo->bindForDisplay();
	drawImage(480/2 - 128, 24, 256, 128, 0, 0);
	char formats[] = "PDF - TXT - PalmDoc - DJVU - CHM - HTML";
	int fw = textW(formats, fontBig);
	texUI->bindForDisplay();
	drawNeoBox((480 - fw)/2 - 12, 172, fw + 24, 26, NEO_WHITE, 2, 3);
	drawNeoBox(20, 230, 436, 30, NEO_PINK, 3, 4);
	fontBig->bindForDisplay();
	FZScreen::ambientColor(NEO_INK);
	drawTextHC(formats, fontBig, 177);
	if (loading)
		drawTextHC("Loading...", fontBig, 237);
	else if (text.length() > 0)
		drawTextHC((char*) text.c_str(), fontBig, 237);
	else {
		if (error) {
			texUI->bindForDisplay();
			drawNeoRect(0, 123, 480, 32, NEO_INK);
			drawNeoRect(0, 126, 480, 26, NEO_RED);
			fontBig->bindForDisplay();
			FZScreen::ambientColor(NEO_INK);
			drawTextHC("Error: invalid or corrupted file", fontBig, 130);
		}
		FZScreen::ambientColor(NEO_INK);
		drawTextHC("Press Start", fontBig, 237);
	}
}

BKLogo* BKLogo::create() {
	BKLogo* f = new BKLogo();
	FZScreen::resetReps();
	return f;
}

void BKLogo::show(string text) {
	show(text, 0);
}

void BKLogo::show(string text, int delaySeconds) {
	BKLogo* l = BKLogo::create();
	l->text = text;
	FZScreen::startDirectList();
	l->render();
	FZScreen::endAndDisplayList();
	FZScreen::waitVblankStart();
	FZScreen::swapBuffers();
	FZScreen::checkEvents();
	l->release();
	if (delaySeconds > 0) {
		sceKernelDelayThread(delaySeconds * 1000000);
	}
}

