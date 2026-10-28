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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <setjmp.h>
#include <algorithm>
#include <string>
#include <vector>
using namespace std;

extern "C" {
#include <jpeglib.h>
#include "fitz.h"
#include "mupdf.h"
}
#include <png.h>

#include "fzscreen.h"
#include "bkzip.h"
#include "bkcbz.h"

// Pages are decoded to 4 bytes per pixel and the renderer makes a full-size
// copy each time it draws one, so big pages are decoded smaller: libjpeg can
// shrink by 2, 4 or 8 while decoding, at no extra cost.
#define BKCOMIC_MAX_PIXELS (2200*1000)
// the largest page file read from the archive
#define BKCOMIC_MAX_FILE (32*1024*1024)

struct BKComic {
	string path;
	vector<BKZipEntry> entries;
	vector<int> pages;				// indexes into entries, in reading order
};

// A page image, already decoded to the alpha-first layout MuPDF's pixmaps
// use, so loading a tile is a copy.
struct BKComicImage {
	fz_image super;
	unsigned char* samples;
};

// pixels a page may have, from the memory free now: the page plus the
// renderer's copy of it, with room to spare for drawing
static unsigned int pixelBudget() {
	int spare = FZScreen::getFreeMemory() - 4*1024*1024;
	unsigned int budget = spare > 0 ? spare / 8 : 0;
	return budget < BKCOMIC_MAX_PIXELS ? budget : BKCOMIC_MAX_PIXELS;
}

// "page2" before "page10": runs of digits compare by value, the rest
// ignoring case
static int naturalCompare(const string& a, const string& b) {
	unsigned int i = 0, j = 0;
	while (i < a.size() && j < b.size()) {
		if (isdigit((unsigned char)a[i]) && isdigit((unsigned char)b[j])) {
			while (i < a.size() && a[i] == '0')
				i++;
			while (j < b.size() && b[j] == '0')
				j++;
			unsigned int si = i, sj = j;
			while (i < a.size() && isdigit((unsigned char)a[i]))
				i++;
			while (j < b.size() && isdigit((unsigned char)b[j]))
				j++;
			if (i - si != j - sj)
				return i - si < j - sj ? -1 : 1;
			int c = a.compare(si, i - si, b, sj, j - sj);
			if (c != 0)
				return c;
		} else {
			int ca = tolower((unsigned char)a[i]), cb = tolower((unsigned char)b[j]);
			if (ca != cb)
				return ca < cb ? -1 : 1;
			i++;
			j++;
		}
	}
	return (int)(a.size() - i) - (int)(b.size() - j);
}

struct BKPageOrder {
	const vector<BKZipEntry>* entries;
	bool operator()(int a, int b) const {
		return naturalCompare((*entries)[a].name, (*entries)[b].name) < 0;
	}
};

static bool endsWith(const string& s, const char* ext) {
	unsigned int l = strlen(ext);
	return s.size() > l && strcasecmp(s.c_str() + s.size() - l, ext) == 0;
}

static bool isPageImage(const string& name) {
	if (name.empty() || name[name.size() - 1] == '/')
		return false;
	// resource forks and folder data that macOS adds when it zips files
	if (name.compare(0, 9, "__MACOSX/") == 0)
		return false;
	string::size_type slash = name.rfind('/');
	if (name.compare(slash == string::npos ? 0 : slash + 1, 2, "._") == 0)
		return false;
	return endsWith(name, ".jpg") || endsWith(name, ".jpeg") || endsWith(name, ".png");
}

struct BKJpegError {
	struct jpeg_error_mgr pub;
	jmp_buf jump;
	char message[JMSG_LENGTH_MAX];
};

static void jpegErrorExit(j_common_ptr cinfo) {
	BKJpegError* e = (BKJpegError*)cinfo->err;
	(*cinfo->err->format_message)(cinfo, e->message);
	longjmp(e->jump, 1);
}

// libjpeg prints warnings by default
static void jpegNoMessage(j_common_ptr cinfo, int level) {
}

static unsigned char* decodeJPEG(unsigned char* data, int size, int& w, int& h, char* err, int errSize) {
	struct jpeg_decompress_struct cinfo;
	BKJpegError jerr;
	// set after setjmp and freed when libjpeg jumps back
	unsigned char* volatile out = 0;
	JSAMPROW volatile row = 0;

	cinfo.err = jpeg_std_error(&jerr.pub);
	jerr.pub.error_exit = jpegErrorExit;
	jerr.pub.emit_message = jpegNoMessage;
	if (setjmp(jerr.jump)) {
		jpeg_destroy_decompress(&cinfo);
		fz_free(out);
		free(row);
		snprintf(err, errSize, "bad JPEG: %s", jerr.message);
		return 0;
	}
	jpeg_create_decompress(&cinfo);
	jpeg_mem_src(&cinfo, data, size);
	jpeg_read_header(&cinfo, TRUE);

	unsigned int budget = pixelBudget();
	cinfo.scale_num = 1;
	cinfo.scale_denom = 1;
	while (cinfo.scale_denom < 8
		&& (cinfo.image_width / cinfo.scale_denom) * (cinfo.image_height / cinfo.scale_denom) > budget)
		cinfo.scale_denom *= 2;
	bool cmyk = cinfo.jpeg_color_space == JCS_CMYK || cinfo.jpeg_color_space == JCS_YCCK;
	cinfo.out_color_space = cmyk ? JCS_CMYK : JCS_RGB;
	jpeg_start_decompress(&cinfo);

	w = cinfo.output_width;
	h = cinfo.output_height;
	out = (unsigned char*)fz_malloc(w * h * 4);
	row = (JSAMPROW)malloc(w * cinfo.output_components);
	if (out == 0 || row == 0) {
		jpeg_destroy_decompress(&cinfo);
		fz_free(out);
		free(row);
		snprintf(err, errSize, "not enough memory for a %dx%d page", w, h);
		return 0;
	}
	unsigned char* p = out;
	while (cinfo.output_scanline < cinfo.output_height) {
		JSAMPROW r = row;
		jpeg_read_scanlines(&cinfo, &r, 1);
		unsigned char* s = row;
		if (cmyk) {
			// Adobe stores CMYK inverted
			for (int x = 0; x < w; x++, s += 4) {
				*p++ = 255;
				*p++ = s[0] * s[3] / 255;
				*p++ = s[1] * s[3] / 255;
				*p++ = s[2] * s[3] / 255;
			}
		} else {
			for (int x = 0; x < w; x++, s += 3) {
				*p++ = 255;
				*p++ = s[0];
				*p++ = s[1];
				*p++ = s[2];
			}
		}
	}
	jpeg_finish_decompress(&cinfo);
	jpeg_destroy_decompress(&cinfo);
	free(row);
	return out;
}

static unsigned char* decodePNG(unsigned char* data, int size, int& w, int& h, char* err, int errSize) {
	png_image img;
	memset(&img, 0, sizeof(img));
	img.version = PNG_IMAGE_VERSION;
	if (!png_image_begin_read_from_memory(&img, data, size)) {
		snprintf(err, errSize, "bad PNG: %s", img.message);
		return 0;
	}
	// libpng can't shrink while decoding
	if (img.width * img.height > pixelBudget()) {
		snprintf(err, errSize, "the %dx%d page is too large", (int)img.width, (int)img.height);
		png_image_free(&img);
		return 0;
	}
	img.format = PNG_FORMAT_ARGB;
	unsigned char* out = (unsigned char*)fz_malloc(PNG_IMAGE_SIZE(img));
	if (out == 0) {
		snprintf(err, errSize, "not enough memory for a %dx%d page", (int)img.width, (int)img.height);
		png_image_free(&img);
		return 0;
	}
	if (!png_image_finish_read(&img, NULL, out, 0, NULL)) {
		snprintf(err, errSize, "bad PNG: %s", img.message);
		fz_free(out);
		return 0;
	}
	w = img.width;
	h = img.height;
	return out;
}

// the renderer asks for the whole image; copy the requested rows anyway
static fz_error* comicLoadTile(fz_image* img, fz_pixmap* tile) {
	BKComicImage* ci = (BKComicImage*)img;
	if (tile->n != 4 || tile->x < 0 || tile->y < 0 || tile->x + tile->w > img->w || tile->y + tile->h > img->h)
		return fz_throw("rangecheck: comic page tile");
	for (int y = 0; y < tile->h; y++)
		memcpy(tile->samples + y * tile->w * 4,
			ci->samples + ((tile->y + y) * img->w + tile->x) * 4, tile->w * 4);
	return 0;
}

static void comicDropImage(fz_image* img) {
	fz_free(((BKComicImage*)img)->samples);
}

bool bkIsComic(const char* file) {
	int l = strlen(file);
	if (l < 5 || strcasecmp(file + l - 4, ".cbz") != 0)
		return false;
	FILE* f = fopen(file, "rb");
	if (f == NULL)
		return false;
	unsigned char b[4] = { 0, 0, 0, 0 };
	fread(b, 1, 4, f);
	fclose(f);
	return b[0] == 'P' && b[1] == 'K' && b[2] == 3 && b[3] == 4;
}

BKComic* bkComicOpen(const char* file) {
	FILE* f = fopen(file, "rb");
	if (f == NULL)
		return 0;
	BKComic* c = new BKComic();
	c->path = file;
	bool ok = bkZipReadDirectory(f, c->entries);
	fclose(f);
	if (ok) {
		for (unsigned int i = 0; i < c->entries.size(); i++)
			if (isPageImage(c->entries[i].name))
				c->pages.push_back(i);
		BKPageOrder order;
		order.entries = &c->entries;
		sort(c->pages.begin(), c->pages.end(), order);
	}
	if (c->pages.empty()) {
		delete c;
		return 0;
	}
	return c;
}

void bkComicClose(BKComic* c) {
	delete c;
}

int bkComicPageCount(BKComic* c) {
	return c->pages.size();
}

pdf_page* bkComicLoadPage(BKComic* c, int n, char* err, int errSize) {
	if (n < 0 || n >= (int)c->pages.size()) {
		snprintf(err, errSize, "no such page");
		return 0;
	}
	const BKZipEntry& z = c->entries[c->pages[n]];
	// opened per page, as file handles don't survive the PSP sleeping
	FILE* f = fopen(c->path.c_str(), "rb");
	if (f == NULL) {
		snprintf(err, errSize, "can't open the comic");
		return 0;
	}
	int length = 0;
	unsigned char* data = (unsigned char*)bkZipExtract(f, z, length, BKCOMIC_MAX_FILE);
	fclose(f);
	if (data == 0) {
		snprintf(err, errSize, "can't read %s", z.name.c_str());
		return 0;
	}
	int w = 0, h = 0;
	unsigned char* samples = 0;
	if (length > 3 && data[0] == 0xFF && data[1] == 0xD8)
		samples = decodeJPEG(data, length, w, h, err, errSize);
	else if (length > 8 && memcmp(data, "\x89PNG", 4) == 0)
		samples = decodePNG(data, length, w, h, err, errSize);
	else
		snprintf(err, errSize, "%s is not a JPEG or PNG image", z.name.c_str());
	free(data);
	if (samples == 0)
		return 0;

	BKComicImage* img = (BKComicImage*)fz_malloc(sizeof(BKComicImage));
	if (img == 0) {
		fz_free(samples);
		snprintf(err, errSize, "not enough memory");
		return 0;
	}
	memset(img, 0, sizeof(BKComicImage));
	img->super.refs = 1;
	img->super.loadtile = comicLoadTile;
	img->super.drop = comicDropImage;
	img->super.cs = fz_keepcolorspace(pdf_devicergb);
	img->super.w = w;
	img->super.h = h;
	img->super.n = 3;
	img->super.a = 0;
	img->samples = samples;

	// the image fills a page of one unit per pixel, placed the way a PDF
	// places an image: its unit square scaled to the page
	fz_tree* tree = 0;
	fz_node* transform = 0;
	fz_node* image = 0;
	pdf_page* page = 0;
	fz_error* error = fz_newtree(&tree);
	if (error == 0)
		error = fz_newtransformnode(&transform, fz_scale(w, h));
	if (error == 0)
		error = fz_newimagenode(&image, &img->super);
	fz_dropimage(&img->super);		// the image node holds it now, if it was made
	if (error == 0) {
		page = (pdf_page*)fz_malloc(sizeof(pdf_page));
		if (page == 0)
			error = fz_outofmem;
	}
	if (error) {
		if (image)
			fz_dropnode(image);
		if (transform)
			fz_dropnode(transform);
		if (tree)
			fz_droptree(tree);
		if (error != fz_outofmem)
			fz_droperror(error);
		snprintf(err, errSize, "not enough memory");
		return 0;
	}
	fz_insertnodelast(transform, image);
	tree->root = transform;
	memset(page, 0, sizeof(pdf_page));
	page->mediabox.x0 = 0;
	page->mediabox.y0 = 0;
	page->mediabox.x1 = w;
	page->mediabox.y1 = h;
	page->rotate = 0;
	page->tree = tree;
	return page;
}

void bkComicDropPage(pdf_page* page) {
	if (page == 0)
		return;
	fz_droptree(page->tree);
	fz_free(page);
}
