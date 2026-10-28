/*
 * Bookr Plus: document reader for the Sony PSP
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

#include "bkutf8.h"
#include "fzfont.h"

int bkDecodeUTF8(const unsigned char* p, const unsigned char* end, unsigned int& cp) {
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

bool bkIsUTF8(const unsigned char* p, int length) {
	if (length >= 3 && p[0] == 0xEF && p[1] == 0xBB && p[2] == 0xBF)
		return true;
	const unsigned char* end = p + length;
	bool multibyte = false;
	while (p < end) {
		unsigned int cp;
		int n = bkDecodeUTF8(p, end, cp);
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

int bkSlotForUnicode(unsigned int cp) {
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

int bkConvertUTF8(char* b, int length) {
	unsigned char* in = (unsigned char*)b;
	unsigned char* end = in + length;
	unsigned char* out = in;
	while (in < end) {
		unsigned int cp;
		int n = bkDecodeUTF8(in, end, cp);
		if (n < 0)
			break;
		if (n == 0) {
			*out++ = '?';
			++in;
			continue;
		}
		in += n;
		int slot = bkSlotForUnicode(cp);
		if (slot >= 0)
			*out++ = slot;
	}
	return out - (unsigned char*)b;
}
