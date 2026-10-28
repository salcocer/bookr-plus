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

// An EPUB is a ZIP archive. META-INF/container.xml names the package (OPF)
// file, whose manifest lists the book's files and whose spine gives the
// reading order of its XHTML chapters.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <zlib.h>
#include <map>
#include <vector>
using namespace std;

#include "bkepub.h"
#include "bkutf8.h"

// joined chapters larger than this are cut, the PSP has to hold them twice
// while parsing
#define BKEPUB_MAX_TEXT (8*1024*1024)

struct BKZipEntry {
	string name;
	int method;
	unsigned int compSize;
	unsigned int size;
	unsigned int offset;		// of the local file header
};

static unsigned int le16(const unsigned char* p) {
	return p[0] | (p[1] << 8);
}

static unsigned int le32(const unsigned char* p) {
	return p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned int)p[3] << 24);
}

// lists the archive's files from the central directory at its end
static bool zipReadDirectory(FILE* f, vector<BKZipEntry>& entries) {
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

static const BKZipEntry* zipFind(const vector<BKZipEntry>& entries, const string& name) {
	for (unsigned int i = 0; i < entries.size(); i++)
		if (entries[i].name == name)
			return &entries[i];
	// some books get the case of their own file names wrong
	for (unsigned int i = 0; i < entries.size(); i++)
		if (strcasecmp(entries[i].name.c_str(), name.c_str()) == 0)
			return &entries[i];
	return 0;
}

// returns the file's contents with a NUL appended, or 0; free() it after use
static char* zipExtract(FILE* f, const BKZipEntry& z, int& length) {
	unsigned char lh[30];
	if (fseek(f, z.offset, SEEK_SET) != 0 || fread(lh, 1, 30, f) != 30 || le32(lh) != 0x04034b50)
		return 0;
	// the sizes in the local header can be zero, the central directory has them
	if (fseek(f, z.offset + 30 + le16(lh + 26) + le16(lh + 28), SEEK_SET) != 0)
		return 0;
	if (z.size > BKEPUB_MAX_TEXT)
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

// archive path of an href in the OPF: relative to the OPF's folder, with %XX
// escapes decoded, "." and ".." folded and any #fragment dropped
static string resolvePath(const string& base, const string& href) {
	string h;
	for (unsigned int i = 0; i < href.size() && href[i] != '#'; i++) {
		if (href[i] == '%' && i + 2 < href.size() && isxdigit((unsigned char)href[i + 1]) && isxdigit((unsigned char)href[i + 2])) {
			h += (char)strtol(href.substr(i + 1, 2).c_str(), 0, 16);
			i += 2;
		} else {
			h += href[i];
		}
	}
	string path = h.size() > 0 && h[0] == '/' ? h.substr(1) : base + h;
	vector<string> parts;
	unsigned int s = 0;
	while (s <= path.size()) {
		string::size_type e = path.find('/', s);
		if (e == string::npos)
			e = path.size();
		string seg = path.substr(s, e - s);
		if (seg == "..") {
			if (!parts.empty())
				parts.pop_back();
		} else if (seg.size() > 0 && seg != ".") {
			parts.push_back(seg);
		}
		s = e + 1;
	}
	string r;
	for (unsigned int i = 0; i < parts.size(); i++) {
		if (i > 0)
			r += '/';
		r += parts[i];
	}
	return r;
}

// The XML in container.xml and the OPF is machine written, so a small scanner
// that walks the tags and reads their attributes is enough. (The bundled
// TinyXML rejects common EPUB 3 metadata.)
struct BKXmlTag {
	string name;					// without its namespace prefix, "opf:item" -> "item"
	bool closing;
	map<string, string> attrs;
};

// the predefined entities and numeric references in attribute values and text
static string xmlUnescape(const string& s) {
	string r;
	for (unsigned int i = 0; i < s.size(); i++) {
		string::size_type e = s[i] == '&' ? s.find(';', i) : string::npos;
		if (e == string::npos || e - i > 10) {
			r += s[i];
			continue;
		}
		string n = s.substr(i + 1, e - i - 1);
		unsigned int cp = 0;
		if (n == "amp") cp = '&';
		else if (n == "lt") cp = '<';
		else if (n == "gt") cp = '>';
		else if (n == "quot") cp = '"';
		else if (n == "apos") cp = '\'';
		else if (n.size() > 1 && n[0] == '#')
			cp = n[1] == 'x' || n[1] == 'X' ? strtoul(n.c_str() + 2, 0, 16) : strtoul(n.c_str() + 1, 0, 10);
		if (cp == 0) {
			r += s[i];
			continue;
		}
		// back to UTF-8, the text is converted to glyph slots later
		if (cp < 0x80) {
			r += (char)cp;
		} else if (cp < 0x800) {
			r += (char)(0xC0 | (cp >> 6));
			r += (char)(0x80 | (cp & 0x3F));
		} else {
			r += (char)(0xE0 | ((cp >> 12) & 0x0F));
			r += (char)(0x80 | ((cp >> 6) & 0x3F));
			r += (char)(0x80 | (cp & 0x3F));
		}
		i = e;
	}
	return r;
}

static string stripPrefix(const string& s) {
	string::size_type c = s.find(':');
	return c == string::npos ? s : s.substr(c + 1);
}

// reads the next tag at or after p into t and returns the position after it,
// or 0 at the end; comments, declarations and doctypes are skipped
static const char* nextTag(const char* p, BKXmlTag& t) {
	for (;;) {
		p = strchr(p, '<');
		if (p == 0)
			return 0;
		if (p[1] != '!' && p[1] != '?')
			break;
		const char* e = strncmp(p, "<!--", 4) == 0 ? strstr(p, "-->") : strchr(p, '>');
		if (e == 0)
			return 0;
		p = e + 1;
	}
	t.closing = p[1] == '/';
	const char* q = p + (t.closing ? 2 : 1);
	const char* n = q;
	while (*q && !isspace((unsigned char)*q) && *q != '>' && *q != '/')
		q++;
	t.name = stripPrefix(string(n, q - n));
	t.attrs.clear();
	for (;;) {
		while (*q && (isspace((unsigned char)*q) || *q == '/'))
			q++;
		if (*q == 0)
			return 0;
		if (*q == '>')
			return q + 1;
		n = q;
		while (*q && *q != '=' && !isspace((unsigned char)*q) && *q != '>' && *q != '/')
			q++;
		string name = stripPrefix(string(n, q - n));
		while (*q && isspace((unsigned char)*q))
			q++;
		if (*q != '=')
			continue;
		q++;
		while (*q && isspace((unsigned char)*q))
			q++;
		char quote = *q;
		if (quote != '"' && quote != '\'')
			continue;
		const char* v = ++q;
		while (*q && *q != quote)
			q++;
		if (*q == 0)
			return 0;
		t.attrs[name] = xmlUnescape(string(v, q - v));
		q++;
	}
}

static bool startsTag(const char* p, int left, const char* tag) {
	int l = strlen(tag);
	if (left <= l || memcmp(p, tag, l) != 0)
		return false;
	char c = p[l];
	return c == ' ' || c == '>' || c == '/' || c == '\t' || c == '\n' || c == '\r';
}

// parseHTML breaks lines at <p>, <h*>, <br> and list items only. Chapters
// also lay text out in <div> blocks and table rows, and the HTML5 <header>
// would be taken for <head>, whose contents are skipped. Rewritten in place
// with tags of the same length.
static void normalizeTags(char* b, int n) {
	for (int i = 0; i < n; i++) {
		if (b[i] != '<')
			continue;
		if (startsTag(b + i, n - i, "<div"))
			memcpy(b + i, "<p  ", 4);
		else if (startsTag(b + i, n - i, "<header"))
			memcpy(b + i, "<p     ", 7);
		else if (startsTag(b + i, n - i, "<tr"))
			memcpy(b + i, "<br", 3);
	}
}

// the chapters joined into one HTML buffer in glyph slots, or 0
static char* readBook(FILE* f, const vector<BKZipEntry>& entries, string& title, int& length) {
	int n = 0;
	// DRM encrypts the chapters themselves; font obfuscation alone is fine
	const BKZipEntry* enc = zipFind(entries, "META-INF/encryption.xml");
	if (enc) {
		char* x = zipExtract(f, *enc, n);
		bool drm = x == 0 || strstr(x, "htm") != 0;
		free(x);
		if (drm)
			return 0;
	}

	const BKZipEntry* ce = zipFind(entries, "META-INF/container.xml");
	char* x = ce ? zipExtract(f, *ce, n) : 0;
	if (x == 0)
		return 0;
	string opfName;
	BKXmlTag tag;
	for (const char* p = nextTag(x, tag); p && opfName.empty(); p = nextTag(p, tag))
		if (tag.name == "rootfile" && !tag.closing && tag.attrs.count("full-path"))
			opfName = tag.attrs["full-path"];
	free(x);
	if (opfName.empty())
		return 0;
	string base;
	string::size_type slash = opfName.rfind('/');
	if (slash != string::npos)
		base = opfName.substr(0, slash + 1);

	const BKZipEntry* oe = zipFind(entries, opfName);
	x = oe ? zipExtract(f, *oe, n) : 0;
	if (x == 0)
		return 0;
	map<string, string> hrefs;
	vector<string> spine;
	for (const char* p = nextTag(x, tag); p; p = nextTag(p, tag)) {
		if (tag.closing)
			continue;
		if (tag.name == "title" && title.empty()) {
			const char* e = strchr(p, '<');
			if (e) {
				string s = xmlUnescape(string(p, e - p));
				if (s.size() > 0)
					s.resize(bkConvertUTF8(&s[0], s.size()));
				title = s;
			}
		} else if (tag.name == "item" && tag.attrs.count("id") && tag.attrs.count("href")) {
			hrefs[tag.attrs["id"]] = tag.attrs["href"];
		} else if (tag.name == "itemref" && tag.attrs.count("idref")) {
			// linear="no" marks covers, notes and other side material
			if (tag.attrs["linear"] != "no")
				spine.push_back(tag.attrs["idref"]);
		}
	}
	free(x);

	// join the spine's chapters, with a blank line between them
	const char* separator = "<br><br>";
	const int sl = strlen(separator);
	char* book = 0;
	int bookLength = 0;
	for (unsigned int s = 0; s < spine.size(); s++) {
		if (hrefs.count(spine[s]) == 0)
			continue;
		const BKZipEntry* ch = zipFind(entries, resolvePath(base, hrefs[spine[s]]));
		int cl = 0;
		char* c = ch ? zipExtract(f, *ch, cl) : 0;
		if (c == 0)
			continue;
		if (bookLength + cl + sl > BKEPUB_MAX_TEXT) {
			free(c);
			break;
		}
		normalizeTags(c, cl);
		cl = bkConvertUTF8(c, cl);
		char* nb = (char*)realloc(book, bookLength + cl + sl);
		if (nb == 0) {
			free(c);
			break;
		}
		book = nb;
		memcpy(book + bookLength, c, cl);
		memcpy(book + bookLength + cl, separator, sl);
		bookLength += cl + sl;
		free(c);
	}
	length = bookLength;
	return book;
}

BKEPub::BKEPub() : buffer(0) { }

BKEPub::~BKEPub() {
	saveLastView();
	if (buffer)
		free(buffer);
}

BKEPub* BKEPub::create(string& file) {
	FILE* f = fopen(file.c_str(), "rb");
	if (f == NULL)
		return NULL;
	vector<BKZipEntry> entries;
	string title;
	int length = 0;
	char* b = 0;
	if (zipReadDirectory(f, entries))
		b = readBook(f, entries, title, length);
	fclose(f);
	if (b == 0)
		return NULL;

	BKEPub* r = new BKEPub();
	r->fileName = file;
	r->title = title;
	r->buffer = BKFancyText::parseHTML(r, b, length);
	r->resetFonts();
	r->resizeView(480, 272);
	return r;
}

void BKEPub::getFileName(string& fn) {
	fn = fileName;
}

void BKEPub::getTitle(string& t) {
	t = title;
}

void BKEPub::getType(string& t) {
	t = "EPUB";
}

bool BKEPub::isEPub(string& file) {
	FILE* f = fopen(file.c_str(), "rb");
	if (f == NULL)
		return false;
	unsigned char b[64];
	size_t n = fread(b, 1, sizeof(b), f);
	fclose(f);
	if (n < 30 || le32(b) != 0x04034b50)
		return false;
	// a ZIP named .epub, or one whose first file is the EPUB "mimetype"
	int fs = file.size();
	if (fs > 5 && strcasecmp(file.c_str() + fs - 5, ".epub") == 0)
		return true;
	unsigned int at = 30 + le16(b + 26) + le16(b + 28);
	return le16(b + 26) == 8 && memcmp(b + 30, "mimetype", 8) == 0
		&& at + 20 <= n && memcmp(b + at, "application/epub+zip", 20) == 0;
}
