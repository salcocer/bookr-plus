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

#include "bkdocument.h"
#include "bkpdf.h"
#include "bkdjvu.h"
#include "bkpalmdoc.h"
#include "bkepub.h"
#include "bkplaintext.h"

BKDocument* BKDocument::create(string& filePath) {
	BKDocument* doc = 0;
	if (BKPDF::isPDF(filePath)) {
		doc = BKPDF::create(filePath);
	} else if (BKDJVU::isDJVU(filePath)) {
		doc = BKDJVU::create(filePath);
	} else if (BKPalmDoc::isPalmDoc(filePath)) {
		doc = BKPalmDoc::create(filePath);
	} else if (BKEPub::isEPub(filePath)) {
		doc = BKEPub::create(filePath);
	} else {
		doc = BKPlainText::create(filePath);
	}
	if (doc != 0)
	{
		doc->buildToolbarMenus();

		if (doc->isBookmarkable()) {
			BKBookmark b;
			string fn;
			doc->getFileName(fn);
			if (BKBookmarksManager::getLastView(fn, b)) {
				doc->setBookmarkPosition(b.viewData);
			}
		}
	}
	return doc;
}

BKDocument::BKDocument() : 	mode(BKDOC_VIEW), bannerFrames(0), banner(""), 	tipFrames(120), toolbarSelMenu(0), toolbarSelMenuItem(0), frames(0),
	screenshotPending(false), noticeFrames(0), noticeError(false) {
	lastSuspendSerial = FZScreen::getSuspendSerial();
}

BKDocument::~BKDocument() {
}

void BKDocument::saveLastView() {
	if (isBookmarkable()) {
		string fn, t;
		getFileName(fn);
		getTitle(t);
		BKBookmark b;
		b.title = t;
		b.page = isPaginated() ? getCurrentPage() : 0;
		b.createdOn = "to do: creation date";
		b.lastView = true;
		getBookmarkPosition(b.viewData);
		BKBookmarksManager::addBookmark(fn, b);
		BKBookmarksManager::setLastFile(fn);
	}
}

void BKDocument::setBanner(char* b) {
	banner = b;
	bannerFrames = 60;
}

void BKDocument::setNotice(const char* title, const char* text, bool error) {
	noticeTitle = title;
	noticeText = text;
	noticeError = error;
	noticeFrames = 150;
}

void BKDocument::drawNotice() {
	if (noticeFrames <= 0 || screenshotPending)
		return;
	int alpha = 0xff;
	if (noticeFrames <= 32)
		alpha = noticeFrames*(256/32) - 8;
	if (alpha <= 0)
		return;
	unsigned int a = alpha << 24;
	int lh = fontBig->getLineHeight();
	int w1 = textW((char*)noticeTitle.c_str(), fontBig);
	int w2 = textW((char*)noticeText.c_str(), fontBig);
	int w = (w1 > w2 ? w1 : w2) + 40;
	int h = 2*lh + 22;
	int x = (480 - w) / 2;
	int y = (272 - h) / 2;
	texUI->bindForDisplay();
	drawNeoBox(x, y, w, h, ((noticeError ? NEO_RED : NEO_GREEN) & 0xffffff) | a, 3, 5, (NEO_INK & 0xffffff) | a);
	fontBig->bindForDisplay();
	FZScreen::ambientColor((NEO_INK & 0xffffff) | a);
	drawTextHC((char*)noticeTitle.c_str(), fontBig, y + 10);
	drawTextHC((char*)noticeText.c_str(), fontBig, y + 10 + lh);
}

int BKDocument::takeScreenshot() {
	// PICTURE/BookrPlus on the device the EBOOT runs from, so the XMB Photo menu lists them
	string dir(FZScreen::basePath());
	string::size_type colon = dir.find(':');
	dir = colon == string::npos ? string("ms0:") : dir.substr(0, colon + 1);
	dir += "/PICTURE";
	FZScreen::makeDir(dir.c_str());
	dir += "/BookrPlus";
	FZScreen::makeDir(dir.c_str());

	int year = 0, month = 0, day = 0, h = 0, m = 0, s = 0;
	FZScreen::getDateTime(year, month, day, h, m, s);
	char name[64];
	snprintf(name, 64, "BookrPlus_%04d%02d%02d_%02d%02d%02d", year, month, day, h, m, s);
	string path;
	for (int i = 1; i < 100; i++) {
		// two shots within the same second get _2, _3...
		char suffix[8] = "";
		if (i > 1)
			snprintf(suffix, 8, "_%d", i);
		path = dir + "/" + name + suffix + ".png";
		FILE* f = fopen(path.c_str(), "rb");
		if (f == 0)
			break;
		fclose(f);
	}

	FZScreen::setSpeed(BKUser::options.pspMenuSpeed);
	int r = FZScreen::saveScreenshot(path.c_str());
	FZScreen::setSpeed(BKUser::options.pspSpeed);
	if (r == 0) {
		string shown(path, path.find('/') + 1);		// drop the device, e.g. "ms0:/"
		setNotice("Screenshot saved", shown.c_str(), false);
	} else if (r == FZ_SCREENSHOT_ERR_OPEN) {
		setNotice("Screenshot failed", "Couldn't create the file on the Memory Stick", true);
	} else if (r == FZ_SCREENSHOT_ERR_WRITE) {
		setNotice("Screenshot failed", "Couldn't write the image. Memory Stick full?", true);
	} else {
		setNotice("Screenshot failed", "This screen format isn't supported", true);
	}
	return r;
}

int BKDocument::update(unsigned int buttons) {
	// let the view quit update processing early for some special events
	if (lastSuspendSerial != FZScreen::getSuspendSerial()) {
		lastSuspendSerial = FZScreen::getSuspendSerial();
		int r = resume();
		if (r != 0)
			return r;
	}
	int r = updateContent();
	if (r != 0)
		return r;

	bannerFrames--;
	tipFrames--;
	noticeFrames--;
	if (bannerFrames < 0)
		bannerFrames = 0;
	if (tipFrames < 0)
		tipFrames = 0;
	if (noticeFrames < 0)
		noticeFrames = 0;

	// the clean frame asked for when the screenshot item closed the toolbar is on screen now
	if (screenshotPending) {
		takeScreenshot();
		screenshotPending = false;
		return BK_CMD_MARK_DIRTY;
	}

	// banner fade - this blocks event input during the fade
	//if (bannerFrames > 0)
	//	return BK_CMD_MARK_DIRTY;

	r = 0;
	if (mode == BKDOC_VIEW)
		r = processEventsForView();
	else
		r = processEventsForToolbar();

	// banner fade - this allows events during the fade
	if (bannerFrames > 0 && r == 0)
		r = BK_CMD_MARK_DIRTY;
	if (tipFrames > 0 && r == 0)
		r = BK_CMD_MARK_DIRTY;
	if (noticeFrames > 0 && r == 0)
		r = BK_CMD_MARK_DIRTY;

	// clock tick
	frames++;
	if (frames % 60 == 0 && r == 0 && mode != BKDOC_VIEW)
		r = BK_CMD_MARK_DIRTY;

	return r;
}

int BKDocument::processEventsForView() {
	int* b = FZScreen::ctrlReps();

	// button handling - pagination
	if (isPaginated()) {
//		int n = getTotalPages();
		int p = getCurrentPage();
		int op = p;
		if (b[BKUser::controls.nextPage] == 1) {
			p++;
		}
		if (b[BKUser::controls.previousPage] == 1) {
			p--;
		}
		if (b[BKUser::controls.next10Pages] == 1) {
			p += 10;
		}
		if (b[BKUser::controls.previous10Pages] == 1) {
			p -= 10;
		}
		/*if (b[BKUser::controls.firstPage] == 1) {
			p = 1;
		}
		if (b[BKUser::controls.lastPage] == 1) {
			p = n;
		}*/
		/*if (b[BKUser::controls.screenfulUp] == 1) {
			?
		}
		if (b[BKUser::controls.screenfulDown] == 1) {
			?
		}*/
		int r = 0;
		if (op != p)
			setCurrentPage(p);
		if (r != 0)
			return r;
	}

	// button handling - fine zoom, repeating while the button is held
	if (isZoomable() && hasFineZoom()) {
		const int step = 5;		// screen pixels per step
		int zin = b[BKUser::controls.zoomIn];
		int zout = b[BKUser::controls.zoomOut];
		int r = 0;
		if (zin == 1 || zin > 20)
			r = fineZoom(step);
		else if (zout == 1 || zout > 20)
			r = fineZoom(-step);
		else if (zin == 0 && zout == 0)
			r = finishFineZoom();
		if (r != 0)
			return r;
	}

	// button handling - zoom
	if (isZoomable() && !hasFineZoom()) {
		vector<ZoomLevel> zooms;
		getZoomLevels(zooms);
		int z = getCurrentZoomLevel();
		if (b[BKUser::controls.zoomIn] == 1) {
			z++;
		}
		if (b[BKUser::controls.zoomOut] == 1) {
			z--;
		}
		/*if (b[BKUser::controls.zoomFitWidth] == 1) {
			z ?
		}
		if (b[BKUser::controls.zoomFitHeight] == 1) {
			z ?
		}
		*/
		int r = setZoomLevel(z);
		if (r != 0)
			return r;
	}

	// button handling - analog pad panning
	{
		int ax = 0, ay = 0;
		FZScreen::getAnalogPad(ax, ay);
		int r = pan(ax, ay);
		if (r != 0)
			return r;
	}

	// button handling - digital panning
	{
		if (b[BKUser::controls.screenUp] == 1 || b[BKUser::controls.screenUp] > 20) {
			int r = screenUp();
			if (r != 0)
				return r;
		}
		if (b[BKUser::controls.screenDown] == 1 || b[BKUser::controls.screenDown] > 20) {
			int r = screenDown();
			if (r != 0)
				return r;
		}
		if (b[BKUser::controls.screenLeft] == 1 || b[BKUser::controls.screenLeft] > 20) {
			int r = screenLeft();
			if (r != 0)
				return r;
		}
		if (b[BKUser::controls.screenRight] == 1 || b[BKUser::controls.screenRight] > 20) {
			int r = screenRight();
			if (r != 0)
				return r;
		}
	}

	// button handling - rotation - TO DO
	/*
	virtual bool isRotable() = 0;
	virtual int getRotation() = 0;
	virtual void setRotation(int) = 0;
	*/

	// bookmarks and other features are not supported by mapeable keys

	// main menu
	if (b[BKUser::controls.showMainMenu] == 1) {
		return BK_CMD_INVOKE_MENU;
	}

	// toolbar
	if (b[BKUser::controls.showToolbar] == 1) {
		mode = BKDOC_TOOLBAR;
		return BK_CMD_MARK_DIRTY;
	}

	return 0;
}

void BKDocument::buildToolbarMenus() {
	toolbarMenus[0].clear();
	if (isBookmarkable()) {
		ToolbarItem i;
		i.label = "Add bookmark";
		i.iconX = 19;
		i.iconY = 0;
		i.iconW = 22;
		i.iconH = 26;
		i.circleLabel = "Select";
		toolbarMenus[0].push_back(i);

		string fn;
		getFileName(fn);
		bookmarkList.clear();
		BKBookmarksManager::getBookmarks(fn, bookmarkList);
		BKBookmarkListIt it(bookmarkList.begin());
		char t[256];
		while (it != bookmarkList.end()) {
			const BKBookmark& b = *it;
			snprintf(t, 256, "Page %d", b.page);
			i.label = t;
			i.iconX = 0;
			i.iconY = 0;
			i.iconW = 18;
			i.iconH = 26;
			i.circleLabel = "Jump to";
			i.triangleLabel = "Delete";
			toolbarMenus[0].push_back(i);
			++it;
		}
	} else {
		ToolbarItem i;
		i.label = "No bookmark support";
		i.iconX = 57;
		i.iconY = 0;
		i.iconW = 18;
		i.iconH = 26;
		toolbarMenus[0].push_back(i);
	}

	toolbarMenus[1].clear();
	if (isPaginated()) {
		ToolbarItem i;
		i.label = "First page";
		i.iconX = 0;
		i.iconY = 26;
		i.iconW = 18;
		i.iconH = 26;
		i.circleLabel = "Select";
		toolbarMenus[1].push_back(i);

		i.label = "Last page";
		i.iconX = 19;
		i.iconY = 26;
		i.iconW = 18;
		i.iconH = 26;
		toolbarMenus[1].push_back(i);

		i.label = "Previous 10 pages";
		i.iconX = 95;
		i.iconY = 26;
		i.iconW = 18;
		i.iconH = 26;
		toolbarMenus[1].push_back(i);

		i.label = "Next 10 pages";
		i.iconX = 76;
		i.iconY = 26;
		i.iconW = 18;
		i.iconH = 26;
		toolbarMenus[1].push_back(i);

		i.label = "Go to page";
		i.iconX = 0;
		i.iconY = 53;
		i.iconW = 18;
		i.iconH = 26;
		toolbarMenus[1].push_back(i);
	} else {
		ToolbarItem i;
		i.label = "No pagination support";
		i.iconX = 57;
		i.iconY = 0;
		i.iconW = 18;
		i.iconH = 26;
		toolbarMenus[1].push_back(i);
	}

	toolbarMenus[2].clear();
	if (isZoomable()) {
		ToolbarItem i;
		i.circleLabel = "Select";

		if (hasZoomToFit()) {
			i.label = "Fit height";
			i.iconX = 0;
			i.iconY = 78;
			i.iconW = 18;
			i.iconH = 26;
			toolbarMenus[2].push_back(i);

			i.label = "Fit width";
			i.iconX = 95;
			i.iconY = 53;
			i.iconW = 18;
			i.iconH = 26;
			toolbarMenus[2].push_back(i);
		}

		i.label = "Zoom out";
		i.iconX = 76;
		i.iconY = 53;
		i.iconW = 18;
		i.iconH = 26;
		toolbarMenus[2].push_back(i);

		i.label = "Zoom in";
		i.iconX = 57;
		i.iconY = 53;
		i.iconW = 18;
		i.iconH = 26;
		toolbarMenus[2].push_back(i);

		if (hasTrimMargins()) {
			i.label = getTrimMargins() ? "Trim margins: On" : "Trim margins: Off";
			i.circleLabel = getTrimMargins() ? "Turn off" : "Turn on";
			i.iconX = 95;
			i.iconY = 53;
			i.iconW = 18;
			i.iconH = 26;
			toolbarMenus[2].push_back(i);
		}

		if (hasComicMode()) {
			i.label = getComicMode() ? "Comic mode: On" : "Comic mode: Off";
			i.circleLabel = getComicMode() ? "Turn off" : "Turn on";
			i.iconX = 57;
			i.iconY = 79;
			i.iconW = 17;
			i.iconH = 26;
			toolbarMenus[2].push_back(i);
		}
	} else {
		ToolbarItem i;
		i.label = "No zoom support";
		i.iconX = 57;
		i.iconY = 0;
		i.iconW = 18;
		i.iconH = 26;
		toolbarMenus[2].push_back(i);
	}

	toolbarMenus[4].clear();
	{
		ToolbarItem i;
		i.label = "Screenshot";
		i.circleLabel = "Take";
		i.iconX = 100;
		i.iconY = 98;
		i.iconW = 18;
		i.iconH = 26;
		toolbarMenus[4].push_back(i);
	}

	toolbarMenus[3].clear();
	if (isRotable()) {
		ToolbarItem i;
		i.circleLabel = "Select";
		i.label = "Rotate 90° clockwise";
		i.iconX = 39;
		i.iconY = 79;
		i.iconW = 17;
		i.iconH = 26;
		toolbarMenus[3].push_back(i);

		i.label = "Rotate 90° counterclockwise";
		i.iconX = 57;
		i.iconY = 79;
		i.iconW = 17;
		i.iconH = 26;
		toolbarMenus[3].push_back(i);
	} else {
		ToolbarItem i;
		i.label = "No rotation support";
		i.iconX = 57;
		i.iconY = 0;
		i.iconW = 18;
		i.iconH = 26;
		toolbarMenus[3].push_back(i);
	}
}

int BKDocument::processEventsForToolbar() {
	int* b = FZScreen::ctrlReps();

	if (b[BKUser::controls.menuUp] == 1 || b[BKUser::controls.menuUp] > 20) {
		toolbarSelMenuItem++;
	}
	if (b[BKUser::controls.menuDown] == 1 || b[BKUser::controls.menuDown] > 20) {
		toolbarSelMenuItem--;
	}
	if (b[BKUser::controls.menuLeft] == 1 || b[BKUser::controls.menuLeft] > 20) {
		toolbarSelMenu--;
	}
	if (b[BKUser::controls.menuRight] == 1 || b[BKUser::controls.menuRight] > 20) {
		toolbarSelMenu++;
	}

	// wrap menu indexes
	if (toolbarSelMenu >= 5)
		toolbarSelMenu = 0;
	if (toolbarSelMenu < 0)
		toolbarSelMenu = 4;
	if (toolbarSelMenuItem >= (int)toolbarMenus[toolbarSelMenu].size())
		toolbarSelMenuItem = 0;
	if (toolbarSelMenuItem < 0)
		toolbarSelMenuItem = toolbarMenus[toolbarSelMenu].size() - 1;

	if (b[BKUser::controls.alternate] == 1) {
		// delete bookmark
		if (toolbarSelMenu == 0 && toolbarSelMenuItem > 0 && isBookmarkable()) {
			string fn;
			getFileName(fn);
			BKBookmarkListIt it(bookmarkList.begin());
			int di =  toolbarSelMenuItem - 1;
			for (int i = 0; i < di; i++, it++);
			bookmarkList.erase(it);
			BKBookmarksManager::setBookmarks(fn, bookmarkList);
			buildToolbarMenus();
			return BK_CMD_MARK_DIRTY;
		}
	}

	if (b[BKUser::controls.select] == 1) {
		// add bookmark
		if (toolbarSelMenu == 0 && toolbarSelMenuItem == 0 && isBookmarkable()) {
			string fn, t;
			getFileName(fn);
			getTitle(t);
			BKBookmark b;
			b.title = t;
			b.page = isPaginated() ? getCurrentPage() : 0;
			b.createdOn = "to do: creation date";
			getBookmarkPosition(b.viewData);
			BKBookmarksManager::addBookmark(fn, b);
			buildToolbarMenus();
			return BK_CMD_MARK_DIRTY;
		}
		// jump to bookmark
		if (toolbarSelMenu == 0 && toolbarSelMenuItem > 0 && isBookmarkable()) {
			int di =  toolbarSelMenuItem - 1;
			return setBookmarkPosition(bookmarkList[di].viewData);
		}
		// first page
		if (toolbarSelMenu == 1 && toolbarSelMenuItem == 0 && isPaginated()) {
			int r = setCurrentPage(0);
			if (r != 0)
				return r;
		}
		// last page
		if (toolbarSelMenu == 1 && toolbarSelMenuItem == 1 && isPaginated()) {
			int n = getTotalPages();
			int r = setCurrentPage(n);
			if (r != 0)
				return r;
		}
		// prev 10 pages
		if (toolbarSelMenu == 1 && toolbarSelMenuItem == 2 && isPaginated()) {
			int p = getCurrentPage();
			p -= 10;
			int r = setCurrentPage(p);
			if (r != 0)
				return r;
		}
		// next 10 pages
		if (toolbarSelMenu == 1 && toolbarSelMenuItem == 3 && isPaginated()) {
			int p = getCurrentPage();
			p += 10;
			int r = setCurrentPage(p);
			if (r != 0)
				return r;
		}
		// go to page
		if (toolbarSelMenu == 1 && toolbarSelMenuItem == 4 && isPaginated()) {
			return BK_CMD_INVOKE_PAGE_CHOOSER;
		}
		int zi = 3;
		int zo = 2;
		if (hasZoomToFit()) {
			if (toolbarSelMenu == 2 && toolbarSelMenuItem == 1 && isZoomable()) {
				int r = setZoomToFitWidth();
				if (r != 0)
					return r;
			}
			if (toolbarSelMenu == 2 && toolbarSelMenuItem == 0 && isZoomable()) {
				int r = setZoomToFitHeight();
				if (r != 0)
					return r;
			}
		} else {
			zi = 1;
			zo = 0;
		}

		// zoom in
		if (toolbarSelMenu == 2 && toolbarSelMenuItem == zi && isZoomable()) {
			vector<ZoomLevel> zooms;
			getZoomLevels(zooms);
			int z = getCurrentZoomLevel();
			z++;
			int r = setZoomLevel(z);
			if (r != 0)
				return r;
		}
		// zoom out
		if (toolbarSelMenu == 2 && toolbarSelMenuItem == zo && isZoomable()) {
			vector<ZoomLevel> zooms;
			getZoomLevels(zooms);
			int z = getCurrentZoomLevel();
			z--;
			int r = setZoomLevel(z);
			if (r != 0)
				return r;
		}
		// trim margins, the item after zoom in
		if (toolbarSelMenu == 2 && toolbarSelMenuItem == zi + 1 && isZoomable() && hasTrimMargins()) {
			int r = setTrimMargins(!getTrimMargins());
			buildToolbarMenus();
			if (r != 0)
				return r;
		}
		// comic mode, the item after trim margins
		int ci = hasTrimMargins() ? zi + 2 : zi + 1;
		if (toolbarSelMenu == 2 && toolbarSelMenuItem == ci && isZoomable() && hasComicMode()) {
			int r = setComicMode(!getComicMode());
			buildToolbarMenus();
			if (r != 0)
				return r;
		}

		// rotate cw
		if (toolbarSelMenu == 3 && toolbarSelMenuItem == 0 && isRotable()) {
			FZScreen::setSpeed(BKUser::options.pspMenuSpeed);
			int z = getRotation();
			z++;
			int r = setRotation(z);
			FZScreen::setSpeed(BKUser::options.pspSpeed);
			if (r != 0)
				return r;
		}
		// rotate ccw
		if (toolbarSelMenu == 3 && toolbarSelMenuItem == 1 && isRotable()) {
			FZScreen::setSpeed(BKUser::options.pspMenuSpeed);
			int z = getRotation();
			z--;
			int r = setRotation(z);
			FZScreen::setSpeed(BKUser::options.pspSpeed);
			if (r != 0)
				return r;
		}
		// screenshot - hide the toolbar, redraw the page clean, capture on the next update
		if (toolbarSelMenu == 4 && toolbarSelMenuItem == 0) {
			mode = BKDOC_VIEW;
			screenshotPending = true;
			return BK_CMD_MARK_DIRTY;
		}
	}

	// main menu
	if (b[BKUser::controls.showMainMenu] == 1) {
		return BK_CMD_INVOKE_MENU;
	}

	// view
	if (b[BKUser::controls.showToolbar] == 1) {
		mode = BKDOC_VIEW;
		return BK_CMD_MARK_DIRTY;
	}

	return 0;
}

void BKDocument::render() {
	// content
	renderContent();

	// flash tip for menu/toolbar on load
	if (tipFrames > 0 && mode != BKDOC_TOOLBAR && !screenshotPending) {
		int alpha = 0xff;
		if (tipFrames <= 32) {
			alpha = tipFrames*(256/32) - 8;
		}
		if (alpha > 0) {
			unsigned int a = alpha << 24;
			texUI->bindForDisplay();
			drawNeoBox(480 - 37 - 37 - 10, 272 - 18 - 6, 37*2 + 12, 30,
				(NEO_YELLOW & 0xffffff) | a, 3, 0, (NEO_INK & 0xffffff) | a);
			FZScreen::ambientColor((NEO_INK & 0xffffff) | a);
			drawImage(480 - 37 - 2, 272 - 18,
				37, 18,
				75, 60);
			drawImage(480 - 37 - 2 - 2 - 37, 272 - 18,
				37, 18,
				75, 39);
		}
	}

	// label
	if (bannerFrames > 0 && BKUser::options.displayLabels && !screenshotPending) {
		int y = mode == BKDOC_TOOLBAR ? 10 : 240;
		int alpha = 0xff;
		if (bannerFrames <= 32) {
			alpha = bannerFrames*(256/32) - 8;
		}
		if (alpha > 0) {
			unsigned int a = alpha << 24;
			texUI->bindForDisplay();
			drawNeoBox(147, y - 2, 186, 24, (NEO_YELLOW & 0xffffff) | a, 2, 3, (NEO_INK & 0xffffff) | a);
			fontBig->bindForDisplay();
			FZScreen::ambientColor((NEO_INK & 0xffffff) | a);
			drawTextHC((char*)banner.c_str(), fontBig, y + 3);
		}
	}

	if (mode != BKDOC_TOOLBAR) {
		drawNotice();
		return;
	}

	// all of the icons menus must have at least one item

	// wrap menu indexes
	if (toolbarSelMenu >= 5)
		toolbarSelMenu = 0;
	if (toolbarSelMenu < 0)
		toolbarSelMenu = 4;
	if (toolbarSelMenuItem >= (int)toolbarMenus[toolbarSelMenu].size())
		toolbarSelMenuItem = 0;
	if (toolbarSelMenuItem < 0)
		toolbarSelMenuItem = toolbarMenus[toolbarSelMenu].size() - 1;

	const ToolbarItem& it = toolbarMenus[toolbarSelMenu][toolbarSelMenuItem];

	// background
	texUI->bindForDisplay();
	drawNeoBox(18, 272 - 77, 440, 90, NEO_CREAM, 3, 5);

	// context label
	drawNeoRect(21, 272 - 33, 434, 3, NEO_INK);
	drawNeoRect(21, 272 - 30, 434, 30, NEO_CYAN);

	// selected column - decide if it overflows
	int ts = toolbarMenus[toolbarSelMenu].size();
	int init = 0;
	bool overflow = false;
	int cs = ts;
	if (ts > 5) {		// overflow mode
		overflow = true;
		init = toolbarSelMenuItem - 4;
		if (init < 0)
			init = 0;
		ts = 5 + init;
		cs = 5;
	}
	// highlight icon column, from the icon bar up (to the top edge when the "..." shows)
	int colY = overflow ? 0 : 272 - 156 - cs*35 + 70;
	drawNeoBox(24 + toolbarSelMenu*55, colY, 46, 237 - colY, NEO_WHITE, 3, 4);

	// selected icon item row
	int iw = textW((char*)toolbarMenus[toolbarSelMenu][toolbarSelMenuItem].label.c_str(), fontBig);
	int mw = toolbarMenus[toolbarSelMenu][toolbarSelMenuItem].minWidth;
	if (iw < mw)
		iw = mw;
	int selItemI = overflow ?
		toolbarSelMenuItem > 4 ? 4 : toolbarSelMenuItem
		: toolbarSelMenuItem;
	drawNeoBox(29 + toolbarSelMenu*55, 272 - 156 - selItemI*35 + 39, iw + 56, 34, NEO_PINK, 2, 3);

	// button icons
	FZScreen::ambientColor(NEO_INK);
	int tw = textW((char*)it.circleLabel.c_str(), fontBig);
	if (it.circleLabel.size() > 0) {
		// the icon of whichever button selects, as in the menus
		switch (BKUser::controls.select) {
		case FZ_REPS_CROSS:
			drawImage(480 - tw - 65, 248, BK_IMG_CROSS_XSIZE, BK_IMG_CROSS_YSIZE, BK_IMG_CROSS_X, BK_IMG_CROSS_Y);
			break;
		case FZ_REPS_CIRCLE:
		default:
			drawImage(480 - tw - 65, 248, BK_IMG_CIRCLE_XSIZE, BK_IMG_CIRCLE_YSIZE, BK_IMG_CIRCLE_X, BK_IMG_CIRCLE_Y);
			break;
		}
	}
	if (it.triangleLabel.size() > 0) {
		drawImage(37, 248, 20, 18, BK_IMG_TRIANGLE_X, BK_IMG_TRIANGLE_Y);
	}

	// icon bar
	texUI2->bindForDisplay();
	FZScreen::ambientColor(NEO_INK);

	// menu row
	drawImage(38 + 0*55, 205, 18, 26, 0, 0);
	drawImage(38 + 1*55, 205, 18, 26, 19, 53);
	drawImage(38 + 2*55, 205, 18, 26, 38, 53);
	drawImage(38 + 3*55, 205, 19, 26, 19, 79);
	drawImage(38 + 4*55, 205, 18, 26, 100, 98);
	// selected column
	for (int i = init, j = 0; i < ts; i++, j++) {
		const ToolbarItem& it2 = toolbarMenus[toolbarSelMenu][i];
		if (it2.iconW > 0)
			drawImage(
				40 + toolbarSelMenu*55,
				272 - 156 - j*35+45,
				it2.iconW, it2.iconH, it2.iconX, it2.iconY);
	}

	fontBig->bindForDisplay();
	// item label for selected item
	FZScreen::ambientColor(NEO_INK);
	drawText((char*)it.label.c_str(), fontBig, 40 + toolbarSelMenu*55 + 35, 272 - 156 - selItemI*35+48);

	// button labels
	if (it.triangleLabel.size() > 0) {
		drawText((char*)it.triangleLabel.c_str(), fontBig, 37 + 25, 248);
	}
	if (it.circleLabel.size() > 0) {
		drawText((char*)it.circleLabel.c_str(), fontBig, 480 - tw - 40, 248);
	}

	// overflow indicators
	if (overflow) {
		drawText("...", fontBig, 43 + toolbarSelMenu*55, 1);
		drawText("...", fontBig, 43 + toolbarSelMenu*55, 272 - 92);
	}


	string t;
	if (isPaginated()) {
		char tp[256];
		snprintf(tp, 256, "Page %d of %d", getCurrentPage(), getTotalPages());
		t = tp;
	}
	drawClockAndBattery(t);
	drawNotice();
}

