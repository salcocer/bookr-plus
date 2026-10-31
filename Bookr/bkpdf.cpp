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

#ifdef PSP
#include <pspsysmem.h>
#endif

#include <map>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <time.h>

#include "bkbookmark.h"

#ifdef MAC
static void* memalign(int t, int s) {
	return malloc(s);
}
#else
#include <malloc.h>
#endif

#include "bkpdf.h"

extern "C" {
#include "fitz.h"
#include "mupdf.h"
}

static const float zoomLevels[] = { 0.25f, 0.5f, 0.75f, 0.90f, 1.0f, 1.1f, 1.2f, 1.3f, 1.4f, 1.5f,
	1.6f, 1.7f, 1.8f, 1.9f, 2.0f, 2.25f, 2.5f, 2.75f, 3.0f, 3.5f, 4.0f, 5.0f, 7.5f, 10.0f, 16.0f };

static const float rotateLevels[] = { 0.0f, 90.0f, 180.0f, 270.0f };
// comic mode turns the page 90 degrees counterclockwise
static const int comicRotateLevel = 3;

// Panning speeds for the D-Pad Speed and Analog Speed options, from Very slow
// to Very fast; Normal is the original feel. Speeds are in screen pixels per
// second, not per frame, so they keep their pace while a slow page is redrawn.
// D-pad: a press moves step pixels; held, it moves holdSpeed per second.
struct BKDPadSpeed {
	int step;
	float holdSpeed;
};
static const BKDPadSpeed dpadSpeeds[BK_PAN_SPEED_LEVELS] = {
	{ 2, 100.0f }, { 3, 180.0f }, { 5, 300.0f }, { 10, 500.0f }, { 20, 800.0f }
};
// Analog stick: speed grows with the square of the tilt, from minSpeed with a
// light push, for fine positioning, to maxSpeed at full tilt.
struct BKAnalogSpeed {
	float minSpeed;
	float maxSpeed;
};
static const BKAnalogSpeed analogSpeeds[BK_PAN_SPEED_LEVELS] = {
	{ 5.0f, 300.0f }, { 10.0f, 500.0f }, { 15.0f, 800.0f }, { 25.0f, 1200.0f }, { 40.0f, 1800.0f }
};
static const int analogDeadZone = 32;
// longest time one step may cover, so a stall can't cause a big jump
static const float analogMaxFrame = 0.15f;

static float analogSpeed(int v) {
	int a = v < 0 ? -v : v;
	if (a < analogDeadZone)
		return 0.0f;
	if (a > 127)
		a = 127;
	float t = float(a - analogDeadZone) / float(127 - analogDeadZone);
	const BKAnalogSpeed& speed = analogSpeeds[BKUser::options.pdfAnalogSpeed];
	float s = speed.minSpeed + (speed.maxSpeed - speed.minSpeed) * t * t;
	return v < 0 ? -s : s;
}

// analog pan state: time of the last step and the sub-pixel movement left over
static bool analogMoving = false;
static unsigned int analogLastMicros = 0;
static float analogRestX = 0.0f;
static float analogRestY = 0.0f;

// singleton
static unsigned int* bounceBuffer = NULL;
//static unsigned int* backBuffer = NULL;
static fz_pixmap* fullPageBuffer = NULL;

struct PDFContext {
	/* current document params */
	//string docTitle;
	pdf_xref *xref;
	pdf_outline *outline;
	pdf_pagetree *pages;

	/* current view params */
	/*float zoom;
	int rotate;
	fz_pixmap *image;*/

	/* current page params */
	int pageno;
	pdf_page *page;
	float zoom;
	int zoomLevel;
	float rotate;
	int rotateLevel;

	fz_renderer *rast;

	/* trim margins: each page's media box is narrowed to its content */
	bool trim;
	float trimFit;			// zoom relative to the trimmed page's fit width
	struct TrimEdges {
		bool known;
		float x0, x1;
	} *trims;				// per page cache, allocated on first use

	/* comic mode: rotation and trim to put back when it is turned off */
	bool comic;
	int comicPrevRotateLevel;
	bool comicPrevTrim;
};

static int pdfInit() {
//	fz_error *error;
	if (bounceBuffer == NULL) {
		bounceBuffer = (unsigned int*)memalign(16, 480*272*4);
	}
//	if (backBuffer == NULL) {
//		backBuffer = (unsigned int*)memalign(16, 480*272*4);
//	}
	return 0;
}

static PDFContext* pdfOpen(char *filename) {
	PDFContext* ctx = new PDFContext();
	memset(ctx, 0, sizeof(PDFContext));
	fz_error *error;
	fz_obj *obj;

	error = fz_newrenderer(&ctx->rast, pdf_devicergb, 0, 1024 * 512);
	if (error) {
		printf("err0: %s\n", error->msg);
		delete ctx;
		return 0;
	}

	/*
	* Open PDF and load xref table
	*/
	error = pdf_newxref(&ctx->xref);
	if (error) {
		printf("err1: %s\n", error->msg);
		delete ctx;
		return 0;
	}

	error = pdf_loadxref(ctx->xref, filename);
	if (error) {
		printf("err2: %s\n", error->msg);
		if (!strncmp(error->msg, "ioerror", 7)) {
			delete ctx;
			return 0;
		}
		printf("There was a problem with file \"%s\".\n"
			"It may be corrupted, or generated by broken software.\n\n"
			"%s\n\nTrying to continue anyway...\n", filename, error->msg);
		error = pdf_repairxref(ctx->xref, filename);
		if (error) {
			printf("err3: %s\n", error->msg);
			delete ctx;
			return 0;
		}
	}

	/*
	* Handle encrypted PDF files
	*/

	error = pdf_decryptxref(ctx->xref);
	if (error) {
		printf("err4: %s\n", error->msg);
		delete ctx;
		return 0;
	}

	/*if (ctx->xref->crypt) {
		//printf("err5: %s\n", error->msg);
		printf("err5\n", error->msg);
		delete ctx;
		return 0;
	}*/

	if (ctx->xref->crypt) {
		error = pdf_setpassword(ctx->xref->crypt, "");
		if (error) {
			printf("err5: encrypted file (tried empty password): %s\n", error->msg);
			delete ctx;
			return 0;
			/*
			fz_droperror(error);
			password = winpassword(app, filename);
			if (!password)
				exit(1);
			error = pdf_setpassword(app->xref->crypt, password);
			if (error)
				pdfapp_warn(app, "Invalid password.");
			*/
		}
	}

	/*
	* Load page tree
	*/
	error = pdf_loadpagetree(&ctx->pages, ctx->xref);
	if (error) {
		printf("err6: %s\n", error->msg);
		delete ctx;
		return 0;
	}

	/*
	* Load meta information
	* TODO: move this into mupdf library
	*/
	obj = fz_dictgets(ctx->xref->trailer, "Root");
	if (!obj) {
		printf("err7: %s\n", error->msg);
		//pdfapp_error(app, fz_throw("syntaxerror: missing Root object"));
		delete ctx;
		return 0;
	}

	error = pdf_loadindirect(&ctx->xref->root, ctx->xref, obj);
	if (error) {
		printf("err8: %s\n", error->msg);
		delete ctx;
		return 0;
	}

	obj = fz_dictgets(ctx->xref->trailer, "Info");
	if (obj) {

		error = pdf_loadindirect(&ctx->xref->info, ctx->xref, obj);

		if (error) {
			printf("err10: %s\n", error->msg);
			delete ctx;
			return 0;
		}
	}

	error = pdf_loadnametrees(ctx->xref);
	// non-critical error, we can live without the outline
	if (error) {
		printf("warn11 - no outline: %s\n", error->msg);
	} else {
		error = pdf_loadoutline(&ctx->outline, ctx->xref);
		if (error) {
			printf("warn12 - no outline: %s\n", error->msg);
		}
	}
/*
	char* ptitle = filename;
	if (strrchr(app->doctitle, '\\'))
		pt = strrchr(app->doctitle, '\\') + 1;
	if (strrchr(app->doctitle, '/'))
		app->doctitle = strrchr(app->doctitle, '/') + 1;
	if (app->xref->info) {
		obj = fz_dictgets(app->xref->info, "Title");
		if (obj) {
			error = pdf_toutf8(&app->doctitle, obj);
			if (error) {
				delete ctx;
				return 0;
			}
		}
	}
*/
	/*
	* Start at first page
	*/
/*
	app->shrinkwrap = 1;
	if (app->pageno < 1)
		app->pageno = 1;
	if (app->zoom <= 0.0)
		app->zoom = 1.0;
	app->rotate = 0;
	app->panx = 0;
	app->pany = 0;

	pdfapp_showpage(app, 1, 1);
*/

	ctx->pageno = 1;
	ctx->zoom = 1.0f;
	ctx->zoomLevel = 4;
	ctx->rotate = 0.0f;
	ctx->rotateLevel = 0;
	return ctx;
}

static void pdfClose(PDFContext* ctx);

static fz_matrix pdfViewctm(PDFContext* ctx) {
	fz_matrix ctm;
	ctm = fz_identity();
	ctm = fz_concat(ctm, fz_translate(0, -ctx->page->mediabox.y1));
	ctm = fz_concat(ctm, fz_scale(ctx->zoom, -ctx->zoom));
	ctm = fz_concat(ctm, fz_rotate(ctx->rotate + ctx->page->rotate));
	return ctm;
}

static fz_rect screenMediaBox;
static fz_pixmap* pdfRenderTile(PDFContext* ctx, int x, int y, int w, int h, bool transform = false) {
	fz_error *error;
	fz_matrix ctm;
	fz_rect bbox;

	bbox.x0 = x;
	bbox.y0 = y;
	bbox.x1 = x;
	bbox.y1 = y;
	ctm = pdfViewctm(ctx);
	if (transform) {
		bbox.x1 += w;
		bbox.y1 += h;
		bbox = fz_transformaabb(ctm, bbox);
	} else {
		bbox.x0 = x;
		bbox.y0 = y;
		bbox.x1 = bbox.x0 + w;
		bbox.y1 = bbox.y0 + h;
	}
	fz_irect ir = fz_roundrect(bbox);
	/*if (!transform) {
		ir.x1 = ir.x0 + w;
		ir.y1 = ir.y0 + h;
	}*/
	fz_pixmap* pix = NULL;
	error = fz_rendertree(&pix, ctx->rast, ctx->page->tree, ctm, ir, 1);
	if (error) {
		return 0;
	}
	if (BKUser::options.pdfInvertColors) {
		unsigned int* s = (unsigned int*)pix->samples;
		unsigned int n = pix->w * pix->h;
		for (unsigned int i = 0; i < n; ++i) {
			*s = ~(*s);
			++s;
		}
	}
	return pix;
}

static void pdfRenderFullPage(PDFContext* ctx) {
	if (fullPageBuffer != NULL) {
		fz_droppixmap(fullPageBuffer);
		fullPageBuffer = NULL;
	}
	float h = ctx->page->mediabox.y1 - ctx->page->mediabox.y0;
	float w = ctx->page->mediabox.x1 - ctx->page->mediabox.x0;
	fullPageBuffer = pdfRenderTile(ctx,
		(int)ctx->page->mediabox.x0,
		(int)ctx->page->mediabox.y0,
		(int)w, (int)h, true);
	// precalc color shift
	unsigned int* s = (unsigned int*)fullPageBuffer->samples;
	unsigned int n = fullPageBuffer->w * fullPageBuffer->h;
	for (unsigned int i = 0; i < n; ++i) {
		*s >>= 8;
		++s;
	}
}

static void recalcScreenMediaBox(PDFContext* ctx) {
	fz_matrix ctm;
	fz_rect bbox;
	float h = ctx->page->mediabox.y1 - ctx->page->mediabox.y0;
	float w = ctx->page->mediabox.x1 - ctx->page->mediabox.x0;
	float x = ctx->page->mediabox.x0;
	float y = ctx->page->mediabox.y0;
	bbox.x0 = x;
	bbox.y0 = y;
	bbox.x1 = x;
	bbox.y1 = y;
	ctm = pdfViewctm(ctx);
	bbox.x1 += w;
	bbox.y1 += h;
	bbox = fz_transformaabb(ctm, bbox);
	screenMediaBox = bbox;
	//printf("sCMB (%g, %g) - (%g, %g)\n", bbox.x0, bbox.y0, bbox.x1, bbox.y1);
}

// Where a right-to-left comic page starts: the top right corner of the
// content, wherever the rotation puts it on screen. The far edge is a large
// value that clipCoords pulls back onto the page.
static void comicStartPan(PDFContext* ctx, int& x, int& y) {
	const int farEdge = 1 << 20;
	fz_point p;
	p.x = ctx->page->mediabox.x1;	// PDF y points up, so y1 is the top
	p.y = ctx->page->mediabox.y1;
	p = fz_transformpoint(pdfViewctm(ctx), p);
	x = p.x > (screenMediaBox.x0 + screenMediaBox.x1) / 2.0f ? farEdge : 0;
	y = p.y > (screenMediaBox.y0 + screenMediaBox.y1) / 2.0f ? farEdge : 0;
}

// PDF page tree optimization --------------------------------------------------

fz_error * fakeImageTile(fz_image *img, fz_pixmap *tile) {

	for (int y = 0; y < tile->h; y++) {
		for (int x = 0; x < tile->w; x++) {
			//tile->samples[(y * tile->w + x) * tile->n] = 255;
			//int i = 255;//tmp->samples[y * tile->w + x] / bpcfact;
			for (int k = 0; k < tile->n; k++) {
				tile->samples[(y * tile->w + x) * tile->n + k] = 255;
			}
			//i = CLAMP(i, 0, src->indexed->high);
			//for (k = 0; k < src->indexed->base->n; k++) {
				//tile->samples[(y * tile->w + x) * tile->n + k + 1] =
					//src->indexed->lookup[i * src->indexed->base->n + k];
			//}
		}
	}

	return 0;
}

static void optimizeNode(fz_node *node);

static void optimizeImage(fz_imagenode *node) {
	fz_image *image = node->image;
	//if (image->w*image->h > 250000) {
	if (image->w*image->h > 1) {
		node->image->w = 1;
		node->image->h = 1;
		node->image->n = 3;
		node->image->a = 0;
		node->image->loadtile = fakeImageTile;
		void (*drop)(fz_image*);
	}
}

static void optimizeMeta(fz_metanode *node) {
	fz_node *child;
	for (child = node->super.first; child; child = child->next)
		optimizeNode(child);
}

static void optimizeOver(fz_overnode *node) {
	fz_node *child;
	for (child = node->super.first; child; child = child->next)
		optimizeNode(child);
}

static void optimizeMask(fz_masknode *node) {
	fz_node *child;
	for (child = node->super.first; child; child = child->next)
		optimizeNode(child);
}

static void optimizeBlend(fz_blendnode *node) {
	fz_node *child;
	for (child = node->super.first; child; child = child->next)
		optimizeNode(child);
}

static void optimizeTransform(fz_transformnode *node) {
	optimizeNode(node->super.first);
}

static void optimizeSolid(fz_solidnode *node) {
}

static void optimizeLink(fz_linknode *node) {
}

static void optimizePath(fz_pathnode *node) {
}

static void optimizeText(fz_textnode *node) {
}

static void optimizeShade(fz_shadenode *node) {
}

static void optimizeNode(fz_node *node) {
	if (!node) {
		return;
	}

	switch (node->kind) {
		case FZ_NIMAGE: optimizeImage((fz_imagenode*)node); break;
		case FZ_NMETA: optimizeMeta((fz_metanode*)node); break;
		case FZ_NOVER: optimizeOver((fz_overnode*)node); break;
		case FZ_NMASK: optimizeMask((fz_masknode*)node); break;
		case FZ_NBLEND: optimizeBlend((fz_blendnode*)node); break;
		case FZ_NTRANSFORM: optimizeTransform((fz_transformnode*)node); break;
		case FZ_NCOLOR: optimizeSolid((fz_solidnode*)node); break;
		case FZ_NPATH: optimizePath((fz_pathnode*)node); break;
		case FZ_NTEXT: optimizeText((fz_textnode*)node); break;
		case FZ_NSHADE: optimizeShade((fz_shadenode*)node); break;
		case FZ_NLINK: optimizeLink((fz_linknode*)node); break;
	}
}

static char lastPageError[1024];
static float clampZoom(float z);

// Finds where the page's real content starts and ends horizontally by
// rendering it small, then narrows the media box to that range plus a safety
// margin. Like a crop box that only removes the white sides. Results are
// cached per page so each page is only analysed once.
static void pdfTrimMargins(PDFContext* ctx) {
	const int detectWidth = 400;	// render width used to find the edges
	const int inkLevel = 215;		// pixels darker than this are content
	const int minInk = 3;			// ink pixels a column needs, to skip specks

	int pageCount = pdf_getpagecount(ctx->pages);
	if (ctx->trims == NULL) {
		ctx->trims = (PDFContext::TrimEdges*)calloc(pageCount, sizeof(PDFContext::TrimEdges));
		if (ctx->trims == NULL)
			return;
	}
	if (ctx->pageno < 1 || ctx->pageno > pageCount)
		return;
	PDFContext::TrimEdges& t = ctx->trims[ctx->pageno - 1];
	fz_rect box = ctx->page->mediabox;

	if (!t.known) {
		t.known = true;
		t.x0 = box.x0;
		t.x1 = box.x1;
		float w = box.x1 - box.x0;
		if (w <= 0.0f || box.y1 - box.y0 <= 0.0f)
			return;
		float s = detectWidth / w;
		fz_matrix ctm = fz_identity();
		ctm = fz_concat(ctm, fz_translate(-box.x0, -box.y1));
		ctm = fz_concat(ctm, fz_scale(s, -s));
		fz_irect ir = fz_roundrect(fz_transformaabb(ctm, box));
		fz_pixmap* pix = NULL;
		fz_error* error = fz_rendertree(&pix, ctx->rast, ctx->page->tree, ctm, ir, 1);
		if (error) {
			fz_droperror(error);
			return;
		}
		int* ink = (int*)calloc(pix->w, sizeof(int));
		if (ink != NULL) {
			// samples are alpha, red, green, blue
			fz_sample* p = pix->samples;
			for (int y = 0; y < pix->h; ++y) {
				for (int x = 0; x < pix->w; ++x, p += pix->n) {
					int m = p[1];
					if (p[2] < m)
						m = p[2];
					if (p[3] < m)
						m = p[3];
					if (m < inkLevel)
						++ink[x];
				}
			}
			int l = 0;
			while (l < pix->w && ink[l] < minInk)
				++l;
			int r = pix->w - 1;
			while (r >= l && ink[r] < minInk)
				--r;
			if (l <= r) {			// blank pages are left alone
				int margin = pix->w * 15 / 1000;
				l -= margin;
				r += margin;
				if (l < 0)
					l = 0;
				if (r > pix->w - 1)
					r = pix->w - 1;
				t.x0 = box.x0 + l / s;
				t.x1 = box.x0 + (r + 1) / s;
				if (t.x1 > box.x1)
					t.x1 = box.x1;
			}
			free(ink);
		}
		fz_droppixmap(pix);
	}
	ctx->page->mediabox.x0 = t.x0;
	ctx->page->mediabox.x1 = t.x1;
}

static int pdfLoadPage(PDFContext* ctx) {
	if (fullPageBuffer != NULL) {
		fz_droppixmap(fullPageBuffer);
		fullPageBuffer = NULL;
	}
	strcpy(lastPageError, "No error");

	fz_error *error;
	fz_obj *obj;

	if (ctx->page)
		pdf_droppage(ctx->page);
	ctx->page = 0;

	// empty store to save memory
	if (ctx->xref->store)
		pdf_emptystore(ctx->xref->store);
	bk_pdf_resetbufferssize();
	/*
	pdf_item *item;
	fz_obj *key;
	list<fz_obj *key> keys;
	for (item = ctx->xref->store->root; item; item = item->next) {
		if (item->kind == PDF_KIMAGE)
			keys << key;
	}
	list<fz_obj *key>::iterator it(keys.begin);
	while (it != keys.end()) {

		++it;
	}
	*/

	obj = pdf_getpageobject(ctx->pages, ctx->pageno - 1, ctx->xref);

	error = pdf_loadpage(&ctx->page, ctx->xref, obj);
	if (error) {
		printf("errLP1: %s\n", error->msg);
		strcpy(lastPageError, error->msg);
		return -1;
	}

	//printf("\n\n debug tree ------------------------------------------------\n");
	//fz_debugtree(ctx->page->tree);

	//optimizeNode(ctx->page->tree->root);

	//printf("\n\n OPT debug tree OPT ------------------------------------------------\n");
	//fz_debugtree(ctx->page->tree);

	recalcScreenMediaBox(ctx);
	if (ctx->trim) {
		// keep the zoom relative to the trimmed width, so every page's
		// content fills the screen the same way
		pdfTrimMargins(ctx);
		recalcScreenMediaBox(ctx);
		float w = fabs(screenMediaBox.x1 - screenMediaBox.x0) / ctx->zoom;
		if (ctx->trimFit > 0.0f && w > 0.0f) {
			ctx->zoom = clampZoom(ctx->trimFit * 480.0f / w);
			recalcScreenMediaBox(ctx);
		}
	}
	if (BKUser::options.pdfFastScroll) {
		pdfRenderFullPage(ctx);
	}
	return 0;
}

BKPDF::BKPDF(string& f) : ctx(0), fileName(f), panX(0), panY(0), loadNewPage(false), pageError(false), pendingZoom(0.0f), comicPageStart(false) {
}

static BKPDF* singleton = 0;
static void bkfree_all();
BKPDF::~BKPDF() {
	if (ctx != 0) {
		saveLastView();
		pdfClose(ctx);
		delete ctx;
	}
	ctx = 0;
	singleton = 0;

	if (fullPageBuffer != NULL) {
		fz_droppixmap(fullPageBuffer);
		fullPageBuffer = NULL;
	}

//#error reactivate the hash!
//#error idea - list allocs linear, list frees linear, do merge every N allocs
	bkfree_all();
}

// PDF memory pooling ----------------------------------------------------------

static long long alloc_mallocs = 0;
static long long alloc_malloc_size = 0;
static long long alloc_frees = 0;
static long long alloc_reallocs = 0;
static long long alloc_realloc_size = 0;
static long long alloc_large_realloc = 0;

static long long alloc_current = 0;
typedef map<void*, int> AllocMap;
typedef AllocMap::iterator AllocMapIt;
static AllocMap alloc_allocs;

static void reset_allocs() {
	alloc_mallocs = 0;
	alloc_malloc_size = 0;
	alloc_frees = 0;
	alloc_reallocs = 0;
	alloc_realloc_size = 0;
	alloc_large_realloc = 0;
	alloc_current = 0;
	alloc_allocs.clear();
}

static void print_allocs() {
#ifndef PSP
/*
	printf("\n-----------------------------\n");
	printf("alloc_mallocs = %qd\n", alloc_mallocs);
	printf("alloc_malloc_size = %qd\n", alloc_malloc_size);
	printf("alloc_frees = %qd\n", alloc_frees);
	printf("alloc_reallocs = %qd\n", alloc_reallocs);
	printf("alloc_realloc_size = %qd\n", alloc_realloc_size);
	printf("alloc_large_realloc = %qd\n", alloc_large_realloc);
*/
#endif
}

static void* bkmalloc(fz_memorycontext *mem, int n) {
	void* buf = NULL;
	buf = malloc(n);
	//memset(buf, 0, n);
	alloc_malloc_size += n;
	++alloc_mallocs;
	alloc_allocs[buf] = n;
	alloc_current += n;
	return buf;
}

static void *bkrealloc(fz_memorycontext *mem, void *p, int n) {
	++alloc_reallocs;
	alloc_realloc_size += n;
	alloc_large_realloc = alloc_large_realloc < n ? n : alloc_large_realloc;
	AllocMapIt it = alloc_allocs.find(p);
	if (it != alloc_allocs.end()) {
		alloc_current -= (*it).second;
		alloc_allocs.erase(it);
	}
	void* r = realloc(p, n);
	alloc_allocs[r] = n;
	alloc_current += n;
	return r;
}

static void bkfree(fz_memorycontext *mem, void *p) {
	AllocMapIt it = alloc_allocs.find(p);
	if (it != alloc_allocs.end()) {
		alloc_current -= (*it).second;
		alloc_allocs.erase(it);
	}
	++alloc_frees;
	free(p);
}

static void bkfree_all() {
	//printf("bkfree_all - %d\n", (int)alloc_current);
	AllocMapIt it = alloc_allocs.begin();
	if (it != alloc_allocs.end()) {
		free((*it).first);
		++it;
	}
	alloc_current = 0;
	alloc_allocs.clear();
}

static fz_memorycontext bkmem = { bkmalloc, bkrealloc, bkfree };

static bool lastScrollFlag = false;
BKPDF* BKPDF::create(string& file) {
	if (singleton != 0) {
		printf("cannot open more than 1 pdf at the same time\n");
		return singleton;
	}

	reset_allocs();

	BKPDF* b = new BKPDF(file);
	singleton = b;

	fz_setmemorycontext(&bkmem);
	fz_cpudetect();
	fz_accelerate();

	pdfInit();

	PDFContext* ctx = pdfOpen((char*)file.c_str());
	if (ctx == 0) {
		delete b;
		return 0;
	}
	b->ctx = ctx;
	int lastSlash = 0;
	int n = file.size();
	for (int i = 0; i < n; ++i) {
		if (file[i] == '\\')
			lastSlash == i++;
		else if (file[i] == '/')
			lastSlash == i++;
	}
	b->title.assign(file, lastSlash, n - lastSlash);
	if (ctx->xref->info) {
		fz_error *error;
		fz_obj *obj;
		obj = fz_dictgets(ctx->xref->info, "Title");
		if (obj) {
			char *p = NULL;
			error = pdf_toutf8(&p, obj);
			if (error == NULL) {
				b->title = p;
			}
		}
	}

	// Add bookmark support
	//int position = BKBookmark::getLastView(b->filePath);
	//b->setPage(position);

	b->pageError = pdfLoadPage(ctx) != 0;

	FZScreen::resetReps();
	b->redrawBuffer();
	print_allocs();
	lastScrollFlag = BKUser::options.pdfFastScroll;
	return b;
}

static void pdfClose(PDFContext* ctx) {
	if (ctx->trims)
		free(ctx->trims);
	ctx->trims = 0;

	if (ctx->page)
		pdf_droppage(ctx->page);
	ctx->page = 0;

	if (ctx->pages)
		pdf_droppagetree(ctx->pages);
	ctx->pages = 0;

	/*if (ctx->image)
		fz_droppixmap(ctx->image);
	ctx->image = nil;*/

	if (ctx->outline)
		pdf_dropoutline(ctx->outline);
	ctx->outline = 0;

	if (ctx->xref->store)
		pdf_dropstore(ctx->xref->store);
	ctx->xref->store = 0;

	pdf_closexref(ctx->xref);
	ctx->xref = 0;

	if (ctx->rast)
		fz_droprenderer(ctx->rast);
	ctx->rast = 0;

	//printf("alloc_current = %d\n", (int)alloc_current);
}

void BKPDF::clipCoords(float& nx, float& ny) {
	if (!pageError) {
		//fz_matrix ctm = pdfViewctm(ctx);
		//fz_rect bbox = ctx->page->mediabox;
		fz_rect bbox = screenMediaBox;
		//bbox = fz_transformaabb(ctm, bbox);
		if (ny < 0.0f) {
			ny = 0.0f;
		}
		float h = bbox.y1 - bbox.y0;
		if (h <= 272.0f) {
			ny = 0.0f;
		} else if (ny >= h - 272.0f) {
			ny = h - 273.0f;
		}
		if (nx < 0.0f) {
			nx = 0.0f;
		}
		float w = bbox.x1 - bbox.x0;
		if (w <= 489.0f) {
			nx = 0.0f;
		} else if (nx >= w - 480.0f) {
			nx = w - 481.0f;
		}
		//printf("cc panX, panY - %g, %g\n", nx, ny);
	}
}

bool BKPDF::isZoomable() {
	return true;
}

void BKPDF::getZoomLevels(vector<BKDocument::ZoomLevel>& v) {
	int n = BKUser::options.pdfFastScroll ? 15 : sizeof(zoomLevels)/sizeof(float);
	for (int i = 0; i < n; ++i)
		v.push_back(BKDocument::ZoomLevel(BKDOCUMENT_ZOOMTYPE_ABSOLUTE, "FIX ZOOM LABELS"));
}

int BKPDF::getCurrentZoomLevel() {
	return ctx->zoomLevel;
}

// Lowest zoom for fit, fine zoom and trim. It is below the first zoom
// level so pages that are large in PDF units, like scanned comics stored at
// 72 dpi, can still fit the whole page on screen.
static const float minZoom = 0.05f;

// Fast images keeps a full page in memory, so it is limited to 2x.
static float clampZoom(float z) {
	float lo = minZoom;
	float hi = BKUser::options.pdfFastScroll ? 2.0f : zoomLevels[sizeof(zoomLevels)/sizeof(float) - 1];
	if (z < lo)
		return lo;
	if (z > hi)
		return hi;
	return z;
}

// Index of the zoom level closest to z, so the toolbar's zoom in/out steps
// continue from wherever fine zoom left off.
static int nearestZoomLevel(float z) {
	int n = sizeof(zoomLevels)/sizeof(float);
	if (BKUser::options.pdfFastScroll && n > 15)
		n = 15;
	int best = 0;
	for (int i = 1; i < n; ++i) {
		if (fabs(zoomLevels[i] - z) < fabs(zoomLevels[best] - z))
			best = i;
	}
	return best;
}

// Sets an absolute zoom, keeping the point at the centre of the screen in place.
int BKPDF::applyZoom(float z) {
	z = clampZoom(z);
	ctx->zoomLevel = nearestZoomLevel(z);
	if (z == ctx->zoom)
		return 0;
	float ratio = z / ctx->zoom;
	float nx = (float(panX) + 240.0f) * ratio - 240.0f;
	float ny = (float(panY) + 136.0f) * ratio - 136.0f;
	ctx->zoom = z;
	if (!pageError) {
		recalcScreenMediaBox(ctx);
		if (ctx->trim)
			ctx->trimFit = fabs(screenMediaBox.x1 - screenMediaBox.x0) / 480.0f;
	}
	clipCoords(nx, ny);
	panX = int(nx);
	panY = int(ny);
	char t[256];
	snprintf(t, 256, "Zoom %2.3gx", ctx->zoom);
	setBanner(t);
	if (BKUser::options.pdfFastScroll) {
		loadNewPage = true;
		return BK_CMD_MARK_DIRTY;
	}
	redrawBuffer();
	return BK_CMD_MARK_DIRTY;
}

int BKPDF::setZoomLevel(int z) {
	if (z == ctx->zoomLevel)
		return 0;
	bool zoomOut = z < ctx->zoomLevel;
	int n = sizeof(zoomLevels)/sizeof(float);
	if (z < 0)
		z = 0;
	if (z >= n)
		z = n - 1;
	// below the first level (after fit or fine zoom) a step out would jump
	// back in to it, and a step in goes to the first level
	if (zoomOut && zoomLevels[z] >= ctx->zoom)
		return 0;
	if (!zoomOut && ctx->zoom < zoomLevels[0])
		z = 0;
	return applyZoom(zoomLevels[z]);
}

bool BKPDF::hasFineZoom() {
	return true;
}

// Grows or shrinks the pending zoom so the page width on screen changes by
// the given number of pixels. Only the banner updates until finishFineZoom,
// so holding the button does not re-render the page on every step.
int BKPDF::fineZoom(int pixels) {
	if (pageError)
		return 0;
	if (pendingZoom <= 0.0f)
		pendingZoom = ctx->zoom;
	float w = fabs(screenMediaBox.x1 - screenMediaBox.x0) * pendingZoom / ctx->zoom;
	if (w < 1.0f)
		w = 1.0f;
	float z = clampZoom(pendingZoom * (w + pixels) / w);
	if (z == pendingZoom)
		return 0;
	pendingZoom = z;
	char t[256];
	snprintf(t, 256, "Zoom %2.3gx", pendingZoom);
	setBanner(t);
	return BK_CMD_MARK_DIRTY;
}

bool BKPDF::hasTrimMargins() {
	return true;
}

bool BKPDF::getTrimMargins() {
	return ctx->trim;
}

// Turning trim on starts at the trimmed page's fit width. The page is
// reloaded either way, which narrows or restores its media box.
int BKPDF::setTrimMargins(bool on) {
	if (on == ctx->trim)
		return 0;
	ctx->trim = on;
	ctx->trimFit = on ? 1.0f : 0.0f;
	panX = 0;
	loadNewPage = true;
	return BK_CMD_MARK_DIRTY;
}

bool BKPDF::hasComicMode() {
	return true;
}

bool BKPDF::getComicMode() {
	return ctx->comic;
}

// Comic mode turns the page and trims its margins in one step, and remembers
// the rotation and trim it replaced so turning it off puts them back.
int BKPDF::setComicMode(bool on) {
	if (on == ctx->comic)
		return 0;
	ctx->comic = on;
	if (on) {
		ctx->comicPrevRotateLevel = ctx->rotateLevel;
		ctx->comicPrevTrim = ctx->trim;
		ctx->rotateLevel = comicRotateLevel;
		if (!ctx->trim) {
			ctx->trim = true;
			ctx->trimFit = 1.0f;
		}
		comicPageStart = true;
	} else {
		ctx->rotateLevel = ctx->comicPrevRotateLevel;
		if (!ctx->comicPrevTrim) {
			ctx->trim = false;
			ctx->trimFit = 0.0f;
		}
	}
	ctx->rotate = rotateLevels[ctx->rotateLevel];
	loadNewPage = true;
	return BK_CMD_MARK_DIRTY;
}

int BKPDF::finishFineZoom() {
	if (pendingZoom <= 0.0f)
		return 0;
	float z = pendingZoom;
	pendingZoom = 0.0f;
	return applyZoom(z);
}


bool BKPDF::hasZoomToFit() {
	return true;
}

int BKPDF::setZoomToFitWidth() {
	float nw = fabs(screenMediaBox.x1 - screenMediaBox.x0)/ctx->zoom;		// width at 1.0 zoom
	if (nw == 0.0f)
		nw = 1.0f;
	return applyZoom(480.0f/nw);
}

int BKPDF::setZoomToFitHeight() {
	float nh = fabs(screenMediaBox.y1 - screenMediaBox.y0)/ctx->zoom;		// height at 1.0 zoom
	if (nh == 0.0f)
		nh = 1.0f;
	return applyZoom(272.0f/nh);
}

bool BKPDF::isBookmarkable() {
	return true;
}

void BKPDF::getBookmarkPosition(map<string, int>& m) {
	m["page"] = ctx->pageno;
	m["zoom"] = ctx->zoomLevel;
	m["zoomPermille"] = int(ctx->zoom * 1000.0f + 0.5f);
	m["trim"] = ctx->trim ? 1 : 0;
	m["trimFitPermille"] = int(ctx->trimFit * 1000.0f + 0.5f);
	m["panX"] = panX;
	m["panY"] = panY;
	m["rotation"] = ctx->rotateLevel;
	m["comic"] = ctx->comic ? 1 : 0;
}

int BKPDF::setBookmarkPosition(map<string, int>& m) {
	setCurrentPage(m["page"]);
	map<string, int>::iterator zp = m.find("zoomPermille");
	if (zp != m.end() && zp->second > 0)
		applyZoom(float(zp->second) / 1000.0f);
	else
		setZoomLevel(m["zoom"]);
	map<string, int>::iterator tr = m.find("trim");
	if (tr != m.end() && tr->second) {
		ctx->trim = true;
		map<string, int>::iterator tf = m.find("trimFitPermille");
		ctx->trimFit = (tf != m.end() && tf->second > 0) ? float(tf->second) / 1000.0f : 1.0f;
		loadNewPage = true;
	}
	map<string, int>::iterator cm = m.find("comic");
	ctx->comic = cm != m.end() && cm->second != 0;
	setRotation(m["rotation"]);
	panX = m["panX"];
	panY = m["panY"];
	// the saved view wins over the comic page start
	comicPageStart = false;
	return BK_CMD_MARK_DIRTY;
}

bool BKPDF::isPaginated() {
	return true;
}

int BKPDF::getTotalPages() {
	return pdf_getpagecount(ctx->pages);
}

int BKPDF::getCurrentPage() {
	return ctx->pageno;
}

int BKPDF::setCurrentPage(int position) {
	int oldPage = ctx->pageno;
	ctx->pageno = position;
	if (ctx->pageno < 1)
		ctx->pageno = 1;
	if (ctx->pageno > pdf_getpagecount(ctx->pages))
		ctx->pageno = pdf_getpagecount(ctx->pages);
	if (ctx->pageno != oldPage) {
		loadNewPage = true;
		char t[256];
		snprintf(t, 256, "Loading page %d", ctx->pageno);
		setBanner(t);
		// comic mode starts every page at its top right corner; with trim
		// margins every page is framed alike, so keep the view where it was;
		// otherwise start the new page at the top
		if (ctx->comic)
			comicPageStart = true;
		else if (!ctx->trim)
			panY = 0;
		return BK_CMD_MARK_DIRTY;
	}
	return 0;
}

bool BKPDF::isRotable() {
	return true;
}

int BKPDF::getRotation() {
	return ctx->rotateLevel;
}

int BKPDF::setRotation(int z, bool bForce) {
	if (z == ctx->rotateLevel)
		return 0;
	int n = 4;
	ctx->rotateLevel = z;

	if (ctx->rotateLevel < 0)
		ctx->rotateLevel = 3;
	if (ctx->rotateLevel >= n)
		ctx->rotateLevel = 0;

	ctx->rotate = rotateLevels[ctx->rotateLevel];
	float nx = float(panX);
	float ny = float(panY);
	clipCoords(nx, ny);
	panX = int(nx);
	panY = int(ny);
	char t[256];
	snprintf(t, 256, "Rotate to %3.3g°", ctx->rotate);
	setBanner(t);

	if (BKUser::options.pdfFastScroll) {
		//pdfRenderFullPage(ctx);
		loadNewPage = true;
		return BK_CMD_MARK_DIRTY;
	}
	redrawBuffer();
	return BK_CMD_MARK_DIRTY;
}

void BKPDF::getTitle(string& s) {
	s = title;
}

void BKPDF::getType(string& s) {
	s = "PDF";
}

void BKPDF::getFileName(string& s) {
	s = fileName;
}

int BKPDF::prePan(int x, int y) {
	float nx = float(panX + x);
	float ny = float(panY + y);
	clipCoords(nx, ny);
	if (panX == int(nx) && panY == int(ny))
		return 0;
	panBuffer(int(nx), int(ny));
	return BK_CMD_MARK_DIRTY;
}

int BKPDF::pan(int x, int y) {
	float sx = analogSpeed(x);
	float sy = analogSpeed(y);
	if (sx == 0.0f)
		analogRestX = 0.0f;
	if (sy == 0.0f)
		analogRestY = 0.0f;
	if (sx == 0.0f && sy == 0.0f) {
		analogMoving = false;
		return 0;
	}
	// the first step after leaving the dead zone covers one frame
	unsigned int now = FZScreen::getMicros();
	float dt = analogMoving ? float(now - analogLastMicros) / 1000000.0f : 1.0f / 60.0f;
	if (dt > analogMaxFrame)
		dt = analogMaxFrame;
	analogMoving = true;
	analogLastMicros = now;
	// keep the fraction, so a light push still creeps smoothly
	float fx = analogRestX + sx * dt;
	float fy = analogRestY + sy * dt;
	int dx = int(fx);
	int dy = int(fy);
	analogRestX = fx - dx;
	analogRestY = fy - dy;
	if (dx == 0 && dy == 0)
		return 0;
	return prePan(dx, dy);
}

// how long a D-pad button must be held before it starts moving continuously
static const unsigned int dpadHoldDelayMicros = 300000;

// D-pad pan state per axis (x, y): when the button went down, whether it is
// moving continuously yet, time of the last step and the sub-pixel movement
// left over
static unsigned int dpadPressMicros[2] = { 0, 0 };
static bool dpadHolding[2] = { false, false };
static unsigned int dpadLastMicros[2] = { 0, 0 };
static float dpadRest[2] = { 0.0f, 0.0f };

bool BKPDF::hasTimedPanning() {
	return true;
}

// Called on every frame a direction is held. A press moves one step; after
// dpadHoldDelayMicros the button moves continuously, each frame by the time
// since the last one, as the analog stick does. Both the delay and the speed
// are timed, not counted in frames, so a slow page redraw doesn't slow them.
int BKPDF::dpadPan(int dx, int dy, int button) {
	const BKDPadSpeed& speed = dpadSpeeds[BKUser::options.pdfDPadSpeed];
	int axis = dx != 0 ? 0 : 1;
	unsigned int now = FZScreen::getMicros();
	if (FZScreen::ctrlReps()[button] == 1) {
		dpadPressMicros[axis] = now;
		dpadHolding[axis] = false;
		return prePan(dx * speed.step, dy * speed.step);
	}
	if (!dpadHolding[axis]) {
		if (now - dpadPressMicros[axis] < dpadHoldDelayMicros)
			return 0;
		dpadHolding[axis] = true;
		dpadLastMicros[axis] = now;
		dpadRest[axis] = 0.0f;
		return 0;
	}
	float dt = float(now - dpadLastMicros[axis]) / 1000000.0f;
	if (dt > analogMaxFrame)
		dt = analogMaxFrame;
	dpadLastMicros[axis] = now;
	float d = dpadRest[axis] + speed.holdSpeed * dt;
	int n = int(d);
	dpadRest[axis] = d - n;
	if (n == 0)
		return 0;
	return prePan(dx * n, dy * n);
}

int BKPDF::screenUp() {
	return dpadPan(0, -1, BKUser::controls.screenUp);
}

int BKPDF::screenDown() {
	return dpadPan(0, 1, BKUser::controls.screenDown);
}

int BKPDF::screenLeft() {
	return dpadPan(-1, 0, BKUser::controls.screenLeft);
}

int BKPDF::screenRight() {
	return dpadPan(1, 0, BKUser::controls.screenRight);
}

void BKPDF::renderContent() {
	// the copyImage fills the entire screen
	//FZScreen::clear(0xffffff, FZ_COLOR_BUFFER);
	FZScreen::color(0xffffffff);
	FZScreen::matricesFor2D();
	FZScreen::enable(FZ_TEXTURE_2D);
	FZScreen::enable(FZ_BLEND);
	FZScreen::blendFunc(FZ_ADD, FZ_SRC_ALPHA, FZ_ONE_MINUS_SRC_ALPHA);

	FZScreen::copyImage(FZ_PSM_8888, 0, 0, 480, 272, 480, bounceBuffer, 0, 0, 512, (void*)(0x04000000+(unsigned int)FZScreen::framebuffer()));

	if (pageError) {
		texUI->bindForDisplay();
		drawNeoRect(0, 123, 480, 32, NEO_INK);
		drawNeoRect(0, 126, 480, 26, NEO_RED);
		fontBig->bindForDisplay();
		FZScreen::ambientColor(NEO_INK);
		char t[256];
		snprintf(t, 256, "Error in page %d: %s", ctx->pageno, lastPageError);
		drawTextHC(t, fontBig, 130);
	}

#ifdef PSP
	//int tfm = sceKernelTotalFreeMemSize();
	//int mfm = sceKernelMaxFreeMemSize();
	//printf("tfm %d, mfm %d\n", tfm, mfm);
#endif
}

static inline void bk_memcpysr8(void* to, void* from, unsigned int n) {
	int* s = (int*)from;
	int* d = (int*)to;
	n >>= 2;
	for (unsigned int i = 0; i < n; ++i) {
		*d = *s >> 8;
		++s;
		++d;
	}
}

#ifdef PSP
extern "C" {
	extern int bk_memcpy(void*, void*, unsigned int);
	// not working...
	//extern int bk_memcpysr8(void*, void*, unsigned int);
};
#else
#define bk_memcpy memcpy
#endif

void BKPDF::redrawBuffer() {
	if (pageError)
		return;
	recalcScreenMediaBox(ctx);
	if (BKUser::options.pdfFastScroll && fullPageBuffer != NULL) {
		// copy region of the full page buffer
		int cw = 480;
		bool fillGrey = false;
		int dskip = 0;
		int px = panX;
		int py = panY;
		//printf("panX, panY - %d, %d\n", px, py);
		if (fullPageBuffer->w < 480) {
			px = 0;
			cw = fullPageBuffer->w;
			fillGrey = true;
			dskip += (480 - fullPageBuffer->w) / 2;
		} else if (px + 480 > fullPageBuffer->w) {
			px = fullPageBuffer->w - 480;
		}
		int ch = 272;
		if (fullPageBuffer->h < 272) {
			py = 0;
			ch = fullPageBuffer->h;
			fillGrey = true;
			dskip += ((272 - fullPageBuffer->h) / 2)*480;
		} else if (py + 272 > fullPageBuffer->h) {
			py = fullPageBuffer->h - 272;
		}
		//printf("px, py - %d, %d\n", px, py);
		unsigned int* s = (unsigned int*)fullPageBuffer->samples + px + (fullPageBuffer->w*py);
		unsigned int* d = bounceBuffer;
		if (fillGrey) {
			unsigned int *dd = d;
			const unsigned int c = BKUser::options.colorSchemes[BKUser::options.currentScheme].txtBGColor;
			for (int i = 0; i < 480*272; i++) {
				*dd = c;
				++dd;
			}
		}
		d += dskip;
		for (int j = 0; j < ch; ++j) {
			bk_memcpy(d, s, cw*4);
			d += 480;
			s += fullPageBuffer->w;
		}
		return;
	}
	/*
	fz_pixmap* pix = pdfRenderTile(ctx, panX + screenMediaBox.x0, panY + screenMediaBox.y0, 480, 272);
	//fz_pixmap* pix = pdfRenderTile(ctx, panX, panY, 480, 272);
	// copy and shift colors
	unsigned int* s = (unsigned int*)pix->samples;
	unsigned int* d = backBuffer;
	const int n = 480*272;
	bk_memcpysr8(d, s, n*4);
	fz_droppixmap(pix);
	bk_memcpy(bounceBuffer, backBuffer, n*4);
	*/

	// render new area
	int bw = int(fabs(screenMediaBox.x1 - screenMediaBox.x0));
	int bh = int(fabs(screenMediaBox.y1 - screenMediaBox.y0));
	if (bw > 480)
		bw = 480;
	if (bh > 272)
		bh = 272;
	fz_pixmap* pix = pdfRenderTile(ctx, panX + int(screenMediaBox.x0), panY + int(screenMediaBox.y0), bw, bh);
	unsigned int* s = (unsigned int*)pix->samples;
	unsigned int* d = bounceBuffer;

	// fill bg area if needed
	if (pix->w < 480 || pix->h < 272) {
		unsigned int *dd = d;
		const unsigned int c = BKUser::options.colorSchemes[BKUser::options.currentScheme].txtBGColor;
		for (int i = 0; i < 480*272; i++) {
			*dd = c;
			++dd;
		}
	}
	// copy rendered tile to bounce buffer for direct viewing
	int pw = pix->w;
	if (pix->w > 480)
		pw = 480;
	int ph = pix->h;
	if (pix->h > 272)
		ph = 272;
	// center tile
	if (pw < 480)
		d += (480 - pw) / 2;
	if (ph < 272)
		d += ((272 - ph) / 2)*480;
	for (int j = 0; j < ph; ++j) {
		bk_memcpysr8(d, s, pw*4);
		d += 480;
		s += pix->w;
	}
	fz_droppixmap(pix);
	/*panX = nx;
	unsigned int* t = bounceBuffer;
	bounceBuffer = backBuffer;
	backBuffer = t;*/
}

void BKPDF::panBuffer(int nx, int ny) {
	if (pageError)
		return;
	if (BKUser::options.pdfFastScroll && fullPageBuffer != NULL) {
		panX = nx;
		panY = ny;
		redrawBuffer();
		return;
	}
	panX = nx;
	panY = ny;
	redrawBuffer();
	// incremental pan is disabled, for now
#if 0
	if (ny != panY) {
		int dy = ny - panY;
		int ady = dy > 0 ? dy : -dy;
		int pdfx = panX;
		int pdfy = dy > 0 ? panY + 272 : panY + dy;
		int pdfw = 480;
		int pdfh = ady;
		int bx = 0;
		int by = dy > 0 ? 272 - dy : 0;
		int destDirtyX = 0;
		int destDirtyY = dy > 0 ? 0 : ady;
		int srcDirtyY = dy > 0 ? ady : 0;
		int n = pdfh*pdfw;
		// displaced area
		bk_memcpy(backBuffer + destDirtyX + destDirtyY*480, bounceBuffer + srcDirtyY*480, 480*(272 - pdfh)*4);
		// render new area
		fz_pixmap* pix = pdfRenderTile(ctx, pdfx, pdfy, pdfw, pdfh);
		// copy and shift colors
		bk_memcpysr8(backBuffer + bx + by*480, pix->samples, n*4);
		fz_droppixmap(pix);
		panY = ny;
		unsigned int* t = bounceBuffer;
		bounceBuffer = backBuffer;
		backBuffer = t;
	}
	if (nx != panX) {
		int dx = nx - panX;
		int adx = dx > 0 ? dx : -dx;
		int pdfx = dx > 0 ? panX + 480 : panX + dx;
		int pdfy = panY;
		int pdfw = adx;
		int pdfh = 272;
		int bx = dx > 0 ? 480 - dx : 0;
		int by = 0;

		int destDirtyX = dx > 0 ? 0 : adx;
		int destDirtyY = 0;
		int srcDirtyX = dx > 0 ? adx : 0;
		// displaced area
		unsigned int* s = bounceBuffer + srcDirtyX;
		unsigned int* d = backBuffer + destDirtyX + destDirtyY*480;
		int tw = 480 - pdfw;
		for (int j = 0; j < 272; ++j) {
			bk_memcpy(d, s, tw*4);
			s += 480;
			d += 480;
		}
		// render new area
		fz_pixmap* pix = pdfRenderTile(ctx, pdfx, pdfy, pdfw, pdfh);
		// copy and shift colors
		s = (unsigned int*)pix->samples;
		d = backBuffer + bx + by*480;
		for (int j = 0; j < 272; ++j) {
			bk_memcpysr8(d, s, pdfw*4);
			d += tw + pdfw;
			s += pdfw;
		}
		fz_droppixmap(pix);
		panX = nx;
		unsigned int* t = bounceBuffer;
		bounceBuffer = backBuffer;
		backBuffer = t;
	}
#endif
}

int BKPDF::resume() {
	// mupdf leaves open file descriptors around. they don't survive a suspend.
	return BK_CMD_RELOAD;
}

int BKPDF::updateContent() {
	if (lastScrollFlag != BKUser::options.pdfFastScroll)
		return BK_CMD_RELOAD;

	if (loadNewPage) {
		pageError = pdfLoadPage(ctx) != 0;
		if (!pageError) {
			if (comicPageStart)
				comicStartPan(ctx, panX, panY);
			// the page size can change with trim margins
			float nx = float(panX);
			float ny = float(panY);
			clipCoords(nx, ny);
			panX = int(nx);
			panY = int(ny);
		}
		redrawBuffer();
		loadNewPage = false;
		comicPageStart = false;
		char t[256];
		int skipped = bk_pdf_skippedimages();
		if (skipped > 0)
			snprintf(t, 256, "Page %d of %d - not enough memory for %d image%s", ctx->pageno,
				pdf_getpagecount(ctx->pages), skipped, skipped == 1 ? "" : "s");
		else
			snprintf(t, 256, "Page %d of %d", ctx->pageno, pdf_getpagecount(ctx->pages));
		setBanner(t);
		return BK_CMD_MARK_DIRTY;
	}

	/*bannerFrames--;
	if (bannerFrames < 0)
		bannerFrames = 0;*/

	/*
	if (loadNewPage) {
		if (FZScreen::wasSuspended()) {
			FZScreen::clearSuspended();
			PDFContext* oldctx = ctx;
			ctx = pdfOpen((char*)path.c_str());
			if (ctx == 0) {
				// fucked... just implement *proper* error handling some day ok???
				return 0;
			}
			ctx->pageno = oldctx->pageno;
			ctx->zoom = oldctx->zoom;
			ctx->zoomLevel = oldctx->zoomLevel;
			ctx->rotate = oldctx->rotate;
			// remove this if it crashes on resume, but it will leak a ton of ram
			// need*pooled*malloc*NOW
			//pdfClose(oldctx);
			delete oldctx;
		}
		pageError = pdfLoadPage(ctx) != 0;
		redrawBuffer();
		loadNewPage = false;
		char t[256];
		snprintf(t, 256, "Page %d of %d", ctx->pageno, pdf_getpagecount(ctx->pages));
		banner = t;
		bannerFrames = 60;
		return BK_CMD_MARK_DIRTY;
	}
	*/
	return 0;
}

bool BKPDF::isPDF(string& file) {
	char header[4];
	memset((void*)header, 0, 4);
	FILE* f = fopen(file.c_str(), "r");
	if( !f )
		return false;
	fread(header, 4, 1, f);
	fclose(f);
	return header[0] == 0x25 && header[1] == 0x50 && header[2] == 0x44 && header[3] == 0x46;
}

