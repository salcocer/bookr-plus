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

#ifndef BKCBZ_H
#define BKCBZ_H

// CBZ comics: a ZIP archive of JPEG or PNG pages, in file name order. Each
// page is turned into a MuPDF page holding a single image, so the PDF
// viewer shows comics with its zoom, panning, trim margins and comic mode.

struct BKComic;
struct pdf_page_s;

bool bkIsComic(const char* file);

// 0 if the file can't be read or holds no pages
BKComic* bkComicOpen(const char* file);
void bkComicClose(BKComic* c);
int bkComicPageCount(BKComic* c);

// page n, counting from 0; on failure returns 0 and writes the reason to err
struct pdf_page_s* bkComicLoadPage(BKComic* c, int n, char* err, int errSize);
void bkComicDropPage(struct pdf_page_s* page);

#endif
