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

#include <stdio.h>
#include <list>
using namespace std;
#include "bkplaintext.h"
#include "fzfont.h"

// Decodes the UTF-8 sequence at p. Returns its length, 0 if it is not valid
// UTF-8, or -1 if it is cut off by the end of the buffer.
static int decodeUTF8(const unsigned char* p, const unsigned char* end, unsigned int& cp) {
	static const unsigned int minimum[5] = { 0, 0, 0x80, 0x800, 0x10000 };
	unsigned char c = p[0];
	int n;
	if (c < 0x80) {
		cp = c;
		return 1;
	} else if ((c & 0xE0) == 0xC0) {
		n = 2;
		cp = c & 0x1F;
	} else if ((c & 0xF0) == 0xE0) {
		n = 3;
		cp = c & 0x0F;
	} else if ((c & 0xF8) == 0xF0) {
		n = 4;
		cp = c & 0x07;
	} else {
		return 0;
	}
	if (end - p < n)
		return -1;
	for (int i = 1; i < n; ++i) {
		if ((p[i] & 0xC0) != 0x80)
			return 0;
		cp = (cp << 6) | (p[i] & 0x3F);
	}
	if (cp < minimum[n] || cp > 0x10FFFF)
		return 0;
	return n;
}

// True for files with a UTF-8 BOM, or that are valid UTF-8 and use at least
// one non-ASCII character. Latin-1 text with accents is never valid UTF-8.
static bool isUTF8(const unsigned char* p, int length) {
	if (length >= 3 && p[0] == 0xEF && p[1] == 0xBB && p[2] == 0xBF)
		return true;
	const unsigned char* end = p + length;
	bool multibyte = false;
	while (p < end) {
		unsigned int cp;
		int n = decodeUTF8(p, end, cp);
		if (n < 0)
			break;
		if (n == 0)
			return false;
		if (n > 1)
			multibyte = true;
		p += n;
	}
	return multibyte;
}

// Glyph slot for a code point, with stand-ins for common characters that
// have no slot. Returns -1 for characters that should be dropped.
static int slotForUnicode(unsigned int cp) {
	int slot = FZFont::unicodeToSlot(cp);
	if (slot >= 0)
		return slot;
	switch (cp) {
		case 0xFEFF: case 0x200B: case 0x200C: case 0x200D:
			return -1;
		case 0x2010: case 0x2011: case 0x2212:
			return '-';
		case 0x2012:
			return FZFont::unicodeToSlot(0x2013);
		case 0x2015:
			return FZFont::unicodeToSlot(0x2014);
		case 0x2032:
			return '\'';
		case 0x2033:
			return '"';
		case 0x2028: case 0x2029:
			return '\n';
	}
	if (cp >= 0x2000 && cp <= 0x200A)
		return ' ';
	return '?';
}

// Converts UTF-8 text in place to glyph slots and returns the new length.
static int convertUTF8(char* b, int length) {
	unsigned char* in = (unsigned char*)b;
	unsigned char* end = in + length;
	unsigned char* out = in;
	while (in < end) {
		unsigned int cp;
		int n = decodeUTF8(in, end, cp);
		if (n < 0)
			break;
		if (n == 0) {
			*out++ = '?';
			++in;
			continue;
		}
		in += n;
		int slot = slotForUnicode(cp);
		if (slot >= 0)
			*out++ = slot;
	}
	return out - (unsigned char*)b;
}

BKPlainText::BKPlainText() : buffer(0) { }
BKPlainText::~BKPlainText() {
	saveLastView();
	if (buffer)
		free(buffer);
}

BKPlainText* BKPlainText::create(string& file) {
	BKPlainText* r = new BKPlainText();
	r->fileName = file;

	// read file to memory
	FILE* f = fopen(file.c_str(), "r");
	if (f == NULL) {
		delete r;
		return NULL;
	}
	long length = 0;
	fseek(f, 0, SEEK_END);
	length = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (length > 4*1024*1024)
		length = 4*1024*1024;
	char* b = (char*)malloc(length);
	fread(b, length, 1, f);
	fclose(f);

	if (isUTF8((unsigned char*)b, length))
		length = convertUTF8(b, length);

	bool isHTML = false;
	// FIX: make the heuristic a bit more advanced than that...
	const char* fc = file.c_str();
	int fs = file.size();
	if (
		((fc[fs - 1] | 0x20) == 'l') &&
		((fc[fs - 2] | 0x20) == 'm') &&
		((fc[fs - 3] | 0x20) == 't') &&
		((fc[fs - 4] | 0x20) == 'h')
	) {
		isHTML = true;
	}
	if (
		((fc[fs - 1] | 0x20) == 'm') &&
		((fc[fs - 2] | 0x20) == 't') &&
		((fc[fs - 3] | 0x20) == 'h')
	) {
		isHTML = true;
	}
	
	if (isHTML) {
		r->buffer = BKFancyText::parseHTML(r, b, length);
	} else {
		r->buffer = BKFancyText::parseText(r, b, length);
	}

	r->resetFonts();
	r->resizeView(480, 272);
	return r;
}

void BKPlainText::getFileName(string& fn) {
	fn = fileName;
}

void BKPlainText::getTitle(string& t) {
	t = "FIX PLAIN TEXT TITLES";
}

void BKPlainText::getType(string& t) {
	t = "Plain text";
}

