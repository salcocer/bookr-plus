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

#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "bkzip.h"

static unsigned int le16(const unsigned char* p) {
	return p[0] | (p[1] << 8);
}

static unsigned int le32(const unsigned char* p) {
	return p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned int)p[3] << 24);
}

bool bkZipReadDirectory(FILE* f, vector<BKZipEntry>& entries) {
	if (fseek(f, 0, SEEK_END) != 0)
		return false;
	long size = ftell(f);
	if (size < 22)
		return false;
	// the end record is 22 bytes plus a comment of up to 64 KB
	long tail = size < 22 + 65535 ? size : 22 + 65535;
	unsigned char* b = (unsigned char*)malloc(tail);
	if (b == 0)
		return false;
	fseek(f, size - tail, SEEK_SET);
	if (fread(b, 1, tail, f) != (size_t)tail) {
		free(b);
		return false;
	}
	long e = -1;
	for (long i = tail - 22; i >= 0; i--) {
		if (le32(b + i) == 0x06054b50) {
			e = i;
			break;
		}
	}
	if (e < 0) {
		free(b);
		return false;
	}
	unsigned int count = le16(b + e + 10);
	unsigned int cdSize = le32(b + e + 12);
	unsigned int cdOffset = le32(b + e + 16);
	free(b);
	if ((long)cdOffset + (long)cdSize > size)
		return false;

	unsigned char* cd = (unsigned char*)malloc(cdSize);
	if (cd == 0)
		return false;
	fseek(f, cdOffset, SEEK_SET);
	if (fread(cd, 1, cdSize, f) != cdSize) {
		free(cd);
		return false;
	}
	unsigned int p = 0;
	for (unsigned int k = 0; k < count && p + 46 <= cdSize; k++) {
		const unsigned char* h = cd + p;
		if (le32(h) != 0x02014b50)
			break;
		unsigned int nameLen = le16(h + 28);
		unsigned int extraLen = le16(h + 30);
		unsigned int commentLen = le16(h + 32);
		if (p + 46 + nameLen > cdSize)
			break;
		BKZipEntry z;
		z.method = le16(h + 10);
		z.compSize = le32(h + 20);
		z.size = le32(h + 24);
		z.offset = le32(h + 42);
		z.name.assign((const char*)h + 46, nameLen);
		entries.push_back(z);
		p += 46 + nameLen + extraLen + commentLen;
	}
	free(cd);
	return entries.size() > 0;
}

const BKZipEntry* bkZipFind(const vector<BKZipEntry>& entries, const string& name) {
	for (unsigned int i = 0; i < entries.size(); i++)
		if (entries[i].name == name)
			return &entries[i];
	// some books get the case of their own file names wrong
	for (unsigned int i = 0; i < entries.size(); i++)
		if (strcasecmp(entries[i].name.c_str(), name.c_str()) == 0)
			return &entries[i];
	return 0;
}

char* bkZipExtract(FILE* f, const BKZipEntry& z, int& length, unsigned int maxSize) {
	unsigned char lh[30];
	if (fseek(f, z.offset, SEEK_SET) != 0 || fread(lh, 1, 30, f) != 30 || le32(lh) != 0x04034b50)
		return 0;
	// the sizes in the local header can be zero, the central directory has them
	if (fseek(f, z.offset + 30 + le16(lh + 26) + le16(lh + 28), SEEK_SET) != 0)
		return 0;
	if (z.size > maxSize)
		return 0;
	char* out = (char*)malloc(z.size + 1);
	if (out == 0)
		return 0;
	if (z.method == 0) {				// stored
		if (z.compSize != z.size || fread(out, 1, z.size, f) != z.size) {
			free(out);
			return 0;
		}
	} else if (z.method == 8) {		// deflated
		unsigned char* in = (unsigned char*)malloc(z.compSize);
		if (in == 0 || fread(in, 1, z.compSize, f) != z.compSize) {
			free(in);
			free(out);
			return 0;
		}
		z_stream s;
		memset(&s, 0, sizeof(s));
		s.next_in = in;
		s.avail_in = z.compSize;
		s.next_out = (Bytef*)out;
		s.avail_out = z.size;
		int r = inflateInit2(&s, -MAX_WBITS);
		if (r == Z_OK) {
			r = inflate(&s, Z_FINISH);
			inflateEnd(&s);
		}
		free(in);
		if (r != Z_STREAM_END || s.total_out != z.size) {
			free(out);
			return 0;
		}
	} else {
		free(out);
		return 0;
	}
	out[z.size] = 0;
	length = z.size;
	return out;
}
