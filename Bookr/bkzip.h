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

#ifndef BKZIP_H
#define BKZIP_H

#include <stdio.h>
#include <string>
#include <vector>

using namespace std;

// Reading files out of ZIP archives (EPUB books, CBZ comics). Stored and
// deflated entries are supported; ZIP64 and encryption are not.

struct BKZipEntry {
	string name;
	int method;
	unsigned int compSize;
	unsigned int size;
	unsigned int offset;		// of the local file header
};

// lists the archive's files from the central directory at its end
bool bkZipReadDirectory(FILE* f, vector<BKZipEntry>& entries);

// exact name match first, then ignoring case
const BKZipEntry* bkZipFind(const vector<BKZipEntry>& entries, const string& name);

// returns the file's contents with a NUL appended, or 0 if it can't be read
// or is larger than maxSize; free() it after use
char* bkZipExtract(FILE* f, const BKZipEntry& z, int& length, unsigned int maxSize);

#endif
