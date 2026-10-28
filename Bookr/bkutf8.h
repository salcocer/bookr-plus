/*
 * Bookr+: document reader for the Sony PSP
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

#ifndef BKUTF8_H
#define BKUTF8_H

// The text font holds 256 glyph slots laid out as Windows-1252, so text
// formats convert their UTF-8 to slots before display.

// Decodes the UTF-8 sequence at p. Returns its length, 0 if it is not valid
// UTF-8, or -1 if it is cut off by the end of the buffer.
int bkDecodeUTF8(const unsigned char* p, const unsigned char* end, unsigned int& cp);

// True for text with a UTF-8 BOM, or that is valid UTF-8 and uses at least
// one non-ASCII character. Latin-1 text with accents is never valid UTF-8.
bool bkIsUTF8(const unsigned char* p, int length);

// Glyph slot for a code point, with stand-ins for common characters that
// have no slot. Returns -1 for characters that should be dropped.
int bkSlotForUnicode(unsigned int cp);

// Converts UTF-8 text in place to glyph slots and returns the new length.
int bkConvertUTF8(char* b, int length);

#endif
