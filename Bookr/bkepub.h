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

#ifndef BKEPUB_H
#define BKEPUB_H

#include <string>

#include "fzscreen.h"

using namespace std;

#include "bkfancytext.h"

// EPUB 2/3 books without DRM, shown as text: the chapters are read in spine
// order and joined into one document. Images and styles are ignored.
class BKEPub : public BKFancyText {
	private:
	string fileName;
	string title;
	char* buffer;

	protected:
	BKEPub();
	~BKEPub();

	public:
	virtual void getFileName(string&);
	virtual void getTitle(string&);
	virtual void getType(string&);

	static BKEPub* create(string& file);
	static bool isEPub(string& file);
};

#endif
