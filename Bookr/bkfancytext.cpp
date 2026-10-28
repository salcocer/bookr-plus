/*
 * Bookr: document reader for the Sony PSP
 * Copyright (C) 2005 Carlos Carrasco Martinez (carloscm at gmail dot com)
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

#include <list>
#include <ctype.h>
#include <stdlib.h>
using namespace std;
#include "bkfancytext.h"
#include "bkutf8.h"

BKFancyText::BKFancyText() : lines(0), nLines(0), topLine(0), maxY(0), font(0), rotation(0), runs(0), nRuns(0), holdScroll(false) {
	lastFontSize = BKUser::options.txtSize;
	lastFontFace = BKUser::options.txtFont;
	lastHeightPct = BKUser::options.txtHeightPct;
	lastWrapCR = BKUser::options.txtWrapCR;
}
BKFancyText::~BKFancyText() {
	if (runs)
		delete[] runs;
	if (lines)
		delete[] lines;
	if (font)
		font->release();
}

static inline bool isBlank(int c) {
	return c == 32 || c == 10 || c == 9;
}

struct BKRunsIterator {
	BKRun* runs;
	int currentRun;
	int currentChar;
	int currentWidth;
	int maxRuns;
	int lineBreakMark;
	int globalPos;
	bool end() {
		return currentRun >= maxRuns ||
			((currentRun == maxRuns - 1) && currentChar >= runs[currentRun].n);
	}
	inline unsigned char forward() {
		if (end()) return 0;
		if (currentChar >= runs[currentRun].n) {
			lineBreakMark += runs[currentRun].lineBreak ? 1 : 0;
			++currentRun;
			currentChar = 0;
			return forward();
		}
		unsigned char c = runs[currentRun].text[currentChar];
		++currentChar;
		++globalPos;
		return c;
	}
	inline unsigned char backward() {
		if (currentRun <= 0 && currentChar < 0) return 0;
		if (currentChar < 0) {
			--currentRun;
			currentChar = runs[currentRun].n - 1;
			return backward();
		}
		unsigned char c = runs[currentRun].text[currentChar];
		--currentChar;
		--globalPos;
		return c;
	}
	BKRunsIterator(BKRun* r, int cr, int cc, int mr) : 	runs(r), currentRun(cr), currentChar(cc), maxRuns(mr), lineBreakMark(false), globalPos(0) { }
	BKRunsIterator(const BKRunsIterator& s) :
		runs(s.runs),
		currentRun(s.currentRun),
		currentChar(s.currentRun),
		maxRuns(s.currentRun),
		lineBreakMark(s.lineBreakMark),
		globalPos(s.globalPos) { }
};

void BKFancyText::reflow(int width) {
	if (lines)
		delete[] lines;
	list<BKLine> tempLines;

	int lineFirstRun = 0;
	int lineFirstRunOffset = 0;
	int lineSpaces = 0;
	int lineStartGlobalPos = 0;
	int currentWidth = 0;
	float spaceWidth = 0.0f;
	BKRunsIterator rit(runs, 0, 0, nRuns);
	BKRunsIterator lastSpace = rit;
	FZCharMetrics* fontChars = font->getMetrics();
	const int spaceWidthC = fontChars[32].xadvance;
	const float spaceWidthCF = float(spaceWidthC);

	while (!rit.end()) {
		if (currentWidth > width || rit.lineBreakMark > 0) {
			spaceWidth = spaceWidthCF;
			int tl = rit.lineBreakMark;
			if (rit.lineBreakMark > 0) {
				 rit.lineBreakMark = 0;
			} else if (lineSpaces > 0) {
				rit = lastSpace;
				--lineSpaces;
				if (lineSpaces > 0)
					spaceWidth = spaceWidthCF + (float(width - rit.currentWidth) / float(lineSpaces));
				else
					spaceWidth = spaceWidthCF;
			} else {
				rit.backward();			// consume the overflowing char
			}
			--tl;
			//printf("line n %d\n", rit.globalPos - lineStartGlobalPos);
			tempLines.push_back(BKLine(lineFirstRun, lineFirstRunOffset, rit.globalPos - lineStartGlobalPos, spaceWidth));
			while (tl > 0) {
				tempLines.push_back(BKLine(lineFirstRun, lineFirstRunOffset, 0, spaceWidth));
				--tl;
			}
			lineFirstRun = rit.currentRun;
			lineFirstRunOffset = rit.currentChar;
			lineStartGlobalPos = rit.globalPos;
			lineSpaces = 0;
			currentWidth = 0;
		}
		int c = rit.forward();
		if (isBlank(c)) {
			lastSpace = rit;
			++lineSpaces;
		}
		currentWidth += fontChars[c].xadvance;
		rit.currentWidth = currentWidth;
	}

	nLines = tempLines.size();
	lines = new BKLine[nLines];
	list<BKLine>::iterator it(tempLines.begin());
	int i = 0;
	while (it != tempLines.end()) {
		const BKLine& l = *it;
		lines[i] = l;
		++i;
		++it;
	}
}

void BKFancyText::resizeView(int width, int height) {
	reflow(width - 10 - 10);
	linesPerPage = (height - 10) / (font->getLineHeight()*(BKUser::options.txtHeightPct/100.0));
	maxY = height - 10;
	totalPages = (nLines / linesPerPage) + 1;
}

// a lot of ebook formats use HTML as a display format, on top of a
// container format. so it makes sense to put the parser/tokenizer in
// the base class

struct BKStrIt {
	char* p;
	int i;
	int n;
	BKStrIt(char* _p, int _i, int _n) : p(_p), i(_i), n(_n) { }
	bool end() {
		return i >= n;
	}
	bool matches(const char* s) {				// does NOT advance the iteration
		char* q = p;
		int j = i;
		while (*s != 0 && j < n) {
			char c = *s >= 'A' && *s <= 'Z' ? *s | 0x20 : *s;		// tolower for ascii
			if (c != *q)
				return false;
			++q;
			++s;
			++j;
		}
		return true;
	}
	void skipTo(const char* s) {
		while (!matches(s)) {
			forward();
		}
		if (!end()) {
			int l = strlen(s);
			p += l;
			i += l;
		}
	}
	unsigned char forward() {
		unsigned char c = 0;
		if (!end()) {
			c = (unsigned char)*p;
			++p;
			++i;
		}
		return c;
	}
};

// named character references that books use, beyond numeric ones
struct BKEntity {
	const char* name;
	unsigned int cp;
};
static const BKEntity entities[] = {
	{ "amp", '&' }, { "lt", '<' }, { "gt", '>' }, { "quot", '"' }, { "apos", '\'' },
	{ "nbsp", 0xA0 }, { "shy", 0xAD }, { "mdash", 0x2014 }, { "ndash", 0x2013 },
	{ "hellip", 0x2026 }, { "lsquo", 0x2018 }, { "rsquo", 0x2019 }, { "sbquo", 0x201A },
	{ "ldquo", 0x201C }, { "rdquo", 0x201D }, { "bdquo", 0x201E }, { "laquo", 0xAB },
	{ "raquo", 0xBB }, { "lsaquo", 0x2039 }, { "rsaquo", 0x203A }, { "bull", 0x2022 },
	{ "middot", 0xB7 }, { "dagger", 0x2020 }, { "Dagger", 0x2021 }, { "copy", 0xA9 },
	{ "reg", 0xAE }, { "trade", 0x2122 }, { "euro", 0x20AC }, { "pound", 0xA3 },
	{ "cent", 0xA2 }, { "yen", 0xA5 }, { "sect", 0xA7 }, { "para", 0xB6 },
	{ "deg", 0xB0 }, { "plusmn", 0xB1 }, { "times", 0xD7 }, { "divide", 0xF7 },
	{ "frac14", 0xBC }, { "frac12", 0xBD }, { "frac34", 0xBE }, { "ordf", 0xAA },
	{ "ordm", 0xBA }, { "iexcl", 0xA1 }, { "iquest", 0xBF },
	{ "Agrave", 0xC0 }, { "Aacute", 0xC1 }, { "Acirc", 0xC2 }, { "Atilde", 0xC3 },
	{ "Auml", 0xC4 }, { "Aring", 0xC5 }, { "AElig", 0xC6 }, { "Ccedil", 0xC7 },
	{ "Egrave", 0xC8 }, { "Eacute", 0xC9 }, { "Ecirc", 0xCA }, { "Euml", 0xCB },
	{ "Igrave", 0xCC }, { "Iacute", 0xCD }, { "Icirc", 0xCE }, { "Iuml", 0xCF },
	{ "Ntilde", 0xD1 }, { "Ograve", 0xD2 }, { "Oacute", 0xD3 }, { "Ocirc", 0xD4 },
	{ "Otilde", 0xD5 }, { "Ouml", 0xD6 }, { "Oslash", 0xD8 }, { "Ugrave", 0xD9 },
	{ "Uacute", 0xDA }, { "Ucirc", 0xDB }, { "Uuml", 0xDC }, { "szlig", 0xDF },
	{ "agrave", 0xE0 }, { "aacute", 0xE1 }, { "acirc", 0xE2 }, { "atilde", 0xE3 },
	{ "auml", 0xE4 }, { "aring", 0xE5 }, { "aelig", 0xE6 }, { "ccedil", 0xE7 },
	{ "egrave", 0xE8 }, { "eacute", 0xE9 }, { "ecirc", 0xEA }, { "euml", 0xEB },
	{ "igrave", 0xEC }, { "iacute", 0xED }, { "icirc", 0xEE }, { "iuml", 0xEF },
	{ "ntilde", 0xF1 }, { "ograve", 0xF2 }, { "oacute", 0xF3 }, { "ocirc", 0xF4 },
	{ "otilde", 0xF5 }, { "ouml", 0xF6 }, { "oslash", 0xF8 }, { "ugrave", 0xF9 },
	{ "uacute", 0xFA }, { "ucirc", 0xFB }, { "uuml", 0xFC }, { "yuml", 0xFF },
	{ "OElig", 0x152 }, { "oelig", 0x153 }, { "Scaron", 0x160 }, { "scaron", 0x161 },
	{ 0, 0 }
};

#define BK_ENTITY_NONE -2
// The character reference at it ("&amp;", "&#233;", "&#x2019;"): moves past
// it and returns its glyph slot, -1 for one to drop, or BK_ENTITY_NONE
// (not moved) when the & does not start a reference
static int parseEntity(BKStrIt& it) {
	char name[16];
	int k = 0;
	char* q = it.p + 1;
	int j = it.i + 1;
	while (j < it.n && k < 15 && *q != ';') {
		if (!isalnum((unsigned char)*q) && *q != '#')
			return BK_ENTITY_NONE;
		name[k++] = *q;
		++q;
		++j;
	}
	if (j >= it.n || *q != ';' || k == 0)
		return BK_ENTITY_NONE;
	name[k] = 0;
	unsigned int cp = 0;
	if (name[0] == '#') {
		if (name[1] == 'x' || name[1] == 'X')
			cp = strtoul(name + 2, 0, 16);
		else
			cp = strtoul(name + 1, 0, 10);
	} else {
		for (const BKEntity* e = entities; e->name; e++) {
			if (strcmp(name, e->name) == 0) {
				cp = e->cp;
				break;
			}
		}
	}
	if (cp == 0)
		return BK_ENTITY_NONE;
	it.p += k + 2;
	it.i += k + 2;
	if (cp == 0xAD)		// soft hyphen, there is no hyphenation
		return -1;
	return bkSlotForUnicode(cp);
}

char* BKFancyText::parseHTML(BKFancyText* r, char* in, int n) {

	// detokenize html text
	// <head*>*</head> --> strip
	// <p*> --> run break, EXTRALF
	// <br*> --> run break, LF
	// <*> --> strip
	// ascii < 32 --> strip
	// &*; --> its character

	list<BKRun> tempRuns;
	BKRun run;

	BKStrIt it(in, 0, n);

	char* out = (char*)malloc(n);
	memset(out, 0, n);
	char* q = out;
	char* lastQ = out;
	int i = 0;
	int li = 0;

	run.lineBreak = true;

	bool lastBlank = false;
	while (!it.end()) {
		if (it.matches("<head")) {			// skip html header
			it.skipTo("</head>");
			continue;
		}
		if (it.matches("<p")) {
			it.skipTo(">");
			// close the previous run
			run.text = lastQ;
			run.n = i - li;
			//printf("p %d\n", run.n);
			li = i;
			lastQ = q;
			tempRuns.push_back(run);

			// the continuation for the next run
			run.lineBreak = true;
			// start the new run with a blank: reflow ends the previous line
			// after the first char of a new run, as a text file's runs start
			// with their newline. Without it the run's first letter moved up.
			*q = 32; ++q; ++i;
			lastBlank = true;
			continue;
		}
		if (it.matches("<h") ) {
			it.skipTo(">");
			// close the previous run
			run.text = lastQ;
			run.n = i - li;
			li = i;
			lastQ = q;
			tempRuns.push_back(run);

			// the continuation for the next run
			run.lineBreak = true;
			// start the new run with a blank: reflow ends the previous line
			// after the first char of a new run, as a text file's runs start
			// with their newline. Without it the run's first letter moved up.
			*q = 32; ++q; ++i;
			lastBlank = true;
			/**q = '>'; ++q;
			*q = '>'; ++q;
			*q = '>'; ++q;
			*q = ' '; ++q;
			i += 4;*/
			continue;
		}
		if (it.matches("<br")) {
			it.skipTo(">");
			// close the previous run
			run.text = lastQ;
			run.n = i - li;
			//printf("br %d\n", run.n);
			li = i;
			lastQ = q;
			tempRuns.push_back(run);

			// the continuation for the next run
			run.lineBreak = true;
			// start the new run with a blank: reflow ends the previous line
			// after the first char of a new run, as a text file's runs start
			// with their newline. Without it the run's first letter moved up.
			*q = 32; ++q; ++i;
			lastBlank = true;
			continue;
		}
		if (it.matches("<li") || it.matches("<dt") || it.matches("<dl")) {
			it.skipTo(">");
			// close the previous run
			run.text = lastQ;
			run.n = i - li;
			li = i;
			lastQ = q;
			tempRuns.push_back(run);

			// the continuation for the next run
			run.lineBreak = true;
			// start the new run with a blank: reflow ends the previous line
			// after the first char of a new run, as a text file's runs start
			// with their newline. Without it the run's first letter moved up.
			*q = 32; ++q; ++i;
			lastBlank = true;
			*q = '*'; ++q;
			*q = ' '; ++q;
			i += 2;
			continue;
		}
		if (it.matches("<")) {				// any other tag - ignore it
			it.skipTo(">");
			continue;
		}
		unsigned char c;
		if (it.matches("&")) {
			int slot = parseEntity(it);
			if (slot == -1)
				continue;
			c = slot == BK_ENTITY_NONE ? it.forward() : (unsigned char)slot;
		} else {
			c = it.forward();
		}
		if (!isBlank(c) && c < 32)			// skip non-blanks, non-printables
			continue;
		if (isBlank(c) && !lastBlank) {		// consolidate 1 to N blanks into a single space
			*q = 32;
			++q;
			++i;
			lastBlank = true;
		}
		if (c > 32) {						// passthru any other char
			*q = c;
			++q;
			++i;
			lastBlank = false;
		}
	}
	// last run
	run.text = lastQ;
	run.n = i - li;
	tempRuns.push_back(run);

	// create fast fixed size run array
	r->runs = new BKRun[tempRuns.size()];
	r->nRuns = tempRuns.size();
	list<BKRun>::iterator jt(tempRuns.begin());
	i = 0;
	while (jt != tempRuns.end()) {
		const BKRun& l = *jt;
		r->runs[i] = l;
		++i;
		++jt;
	}

	free(in);

	return out;
}

char* BKFancyText::parseText(BKFancyText* r, char* b, int length) {
	// tokenize text file
	list<BKRun> tempRuns;
	int li = 0;
	BKRun run;
//	int lastbreak = 0;
	for (int i = 0; i < length; ++i) {
		if (b[i] == 10) {
			bool bBreak = true;
			if( BKUser::options.txtWrapCR > 0 )
			{
//				if( i-lastbreak < 100 )
//				{
//					b[i] = 'X';
//					bBreak = false;
//				} else
//					lastbreak = i;
				bBreak = true;
				for( int j = 1 ; j <= BKUser::options.txtWrapCR+1 ; j++ )
				{
					if( i+j >= length || b[i+j] != 10 )
						bBreak = false;
				}
				if( !bBreak )
				{
					for( int j = 1 ; j <= BKUser::options.txtWrapCR+1 ; j++ )
					{
						if( i+j < length && b[i+j] == 10 )
							b[i+j] = 32;
					}
				}
			}
			if( bBreak )
			{
				run.text = &b[li];
				run.n = i - li;
				li = i;
				run.lineBreak = true;
				tempRuns.push_back(run);
			}
		}
	}
	// last run
	run.text = &b[li];
	run.n = length - li;
	run.lineBreak = true;
	tempRuns.push_back(run);

	// create fast fixed size run array
	r->runs = new BKRun[tempRuns.size()];
	r->nRuns = tempRuns.size();
	list<BKRun>::iterator it(tempRuns.begin());
	int i = 0;
	while (it != tempRuns.end()) {
		const BKRun& l = *it;
		r->runs[i] = l;
		++i;
		++it;
	}

	return b;
}

extern "C" {
extern unsigned int size_res_txtfont;
extern unsigned char res_txtfont[];
};

void BKFancyText::resetFonts() {
	if (font)
		font->release();
	// load font
	bool useBuiltin = BKUser::options.txtFont == "bookr:builtin";
	if (!useBuiltin) {
		font = FZFont::createFromFile((char*)BKUser::options.txtFont.c_str(), BKUser::options.txtSize, false);
		useBuiltin = font == 0;
	}
	if (useBuiltin) {
		font = FZFont::createFromMemory(res_txtfont, size_res_txtfont, BKUser::options.txtSize, false);
	}
	font->texEnv(FZ_TEX_MODULATE);
	font->filter(FZ_NEAREST, FZ_NEAREST);
}

int BKFancyText::updateContent() {
	if (lastFontSize != BKUser::options.txtSize
	 || lastFontFace != BKUser::options.txtFont
	 || lastHeightPct != BKUser::options.txtHeightPct // should be able to just resize view here
	 || lastWrapCR != BKUser::options.txtWrapCR )
		return BK_CMD_RELOAD;
	return 0;
}

int BKFancyText::resume() {
	return 0;
}

void BKFancyText::renderContent() {
	FZScreen::clear(BKUser::options.colorSchemes[BKUser::options.currentScheme].txtBGColor & 0xffffff, FZ_COLOR_BUFFER);
	FZScreen::color(0xffffffff);
	FZScreen::matricesFor2D(rotation);
	FZScreen::enable(FZ_TEXTURE_2D);
	FZScreen::enable(FZ_BLEND);
	FZScreen::blendFunc(FZ_ADD, FZ_SRC_ALPHA, FZ_ONE_MINUS_SRC_ALPHA);

	font->bindForDisplay();

	//bool txtJustify; ??

	FZScreen::ambientColor(0xff000000 | BKUser::options.colorSchemes[BKUser::options.currentScheme].txtFGColor);
	int y = 10;
	int bn = topLine + linesPerPage;
	if (bn >= nLines) bn = nLines;
	for (int i = topLine; i < bn; i++) {
		BKRun* run = &runs[lines[i].firstRun];
		int offset = lines[i].firstRunOffset;
		int x = 10;
		int n = lines[i].totalChars;
		do {
			int pn = n < run->n ? n : run->n;
			if (pn > 0) {
				x = drawText(&run->text[offset], font, x, y, pn, false, BKUser::options.txtJustify, lines[i].spaceWidth, true);
			}
			n -= pn;
			offset = 0;
			++run;
		} while (n > 0);
		//y += lines[i].vSpace;
		y += font->getLineHeight()*(BKUser::options.txtHeightPct/100.0);
		//if (y > maxY)
		//	break;
	}

	FZScreen::matricesFor2D();
}

int BKFancyText::setLine(int l) {
	int oldP = getCurrentPage();
	int oldTL = topLine;
	topLine = l;
	if (topLine >= nLines)
		topLine = nLines - 1;
	if (topLine < 0)
		topLine = 0;
	int cp = getCurrentPage();
	if (cp != oldP) {
		char t[256];
		snprintf(t, 256, "Page %d", cp);
		setBanner(t);
	}
	return oldTL != topLine ? BK_CMD_MARK_DIRTY : 0;
}

int BKFancyText::runForLine(int l) {
	if (l >= nLines || l < 0)
		return 0;
	return lines[l].firstRun;
}

int BKFancyText::lineForRun(int r) {
	int l = 0;
	for (; l < nLines; l++) {
		if (lines[l].firstRun >= r)
			break;
	}
	return l;
}

bool BKFancyText::isPaginated() {
	return true;
}

int BKFancyText::getTotalPages() {
	return totalPages;
}

int BKFancyText::getCurrentPage() {
	return (topLine / linesPerPage) + 1;
}

int BKFancyText::setCurrentPage(int p) {
	if (p <= 0)
		p = 1;
	if (p > totalPages)
		p = totalPages;
	--p;
	return setLine(p * linesPerPage);
}

int BKFancyText::pan(int x, int y) {
	if (y > -32 && y < 32)
		y = 0;
	if (y == 0) {
		holdScroll = false;
		return 0;
	}
	if (y != 0 && !holdScroll) {
		holdScroll = true;
		return setLine(topLine + ((y < 0) ? -1 : 1));
	}
	return 0;
}

int BKFancyText::screenUp() {
	return setCurrentPage(getCurrentPage() - 1);
}

int BKFancyText::screenDown() {
	return setCurrentPage(getCurrentPage() + 1);
}

int BKFancyText::screenLeft() {
	return setCurrentPage(getCurrentPage() - 10);
}

int BKFancyText::screenRight() {
	return setCurrentPage(getCurrentPage() + 10);
}

bool BKFancyText::isRotable() {
	return true;
}

int BKFancyText::getRotation() {
	return rotation;
}

int BKFancyText::setRotation(int r, bool bForce) {
	if (r == rotation && !bForce)
		return 0;
	rotation = r;
	int run = runForLine(topLine);
	if (rotation < 0)
		rotation = 3;
	if (rotation >= 4)
		rotation = 0;
	if (rotation == 0 || rotation == 2) {
		resizeView(480, 272);
		setLine(lineForRun(run));
	} else {
		resizeView(272, 480);
		setLine(lineForRun(run));
	}
	return BK_CMD_MARK_DIRTY;
}

bool BKFancyText::isBookmarkable() {
	return true;
}

void BKFancyText::getBookmarkPosition(map<string, int>& m) {
	m["topLineFirstRun"] = runForLine(topLine);
	m["zoom"] = 0;
	m["rotation"] = rotation;
}

int BKFancyText::setBookmarkPosition(map<string, int>& m) {
	setRotation(m["rotation"]);
	setLine(lineForRun(m["topLineFirstRun"]));
	return BK_CMD_MARK_DIRTY;
}

// zoom for text is the font size: one level per pixel, same range as the options menu
#define BK_TXT_MIN_SIZE 6
#define BK_TXT_MAX_SIZE 20

bool BKFancyText::isZoomable() {
	return true;
}

void BKFancyText::getZoomLevels(vector<BKDocument::ZoomLevel>& v) {
	char t[32];
	for (int s = BK_TXT_MIN_SIZE; s <= BK_TXT_MAX_SIZE; s++) {
		snprintf(t, 32, "Text size %d px", s);
		v.push_back(BKDocument::ZoomLevel(BKDOCUMENT_ZOOMTYPE_ABSOLUTE, t));
	}
}

int BKFancyText::getCurrentZoomLevel() {
	return BKUser::options.txtSize - BK_TXT_MIN_SIZE;
}

int BKFancyText::setZoomLevel(int l) {
	int s = l + BK_TXT_MIN_SIZE;
	if (s < BK_TXT_MIN_SIZE)
		s = BK_TXT_MIN_SIZE;
	if (s > BK_TXT_MAX_SIZE)
		s = BK_TXT_MAX_SIZE;
	if (s == BKUser::options.txtSize)
		return 0;
	BKUser::options.txtSize = s;
	// relayout in place instead of a full reload, keeping the top line's text on screen
	lastFontSize = s;
	resetFonts();
	setRotation(rotation, true);
	BKUser::save();
	char t[32];
	snprintf(t, 32, "Text size %d px", s);
	setBanner(t);
	return BK_CMD_MARK_DIRTY;
}

bool BKFancyText::hasZoomToFit() {
	return false;
}

int BKFancyText::setZoomToFitWidth() {
	return 0;
}

int BKFancyText::setZoomToFitHeight() {
	return 0;
}

