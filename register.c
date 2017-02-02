#include <stdio.h>
#include <gd.h>
#include <sqlite3.h>

typedef struct {
	float r, g, b;
	int n;
} frgb_color_t;

void handleImage(gdImagePtr im, sqlite3 *db)
{
	int midX = gdImageSX(im) / 2, midY = gdImageSY(im) / 2;
	int x, y, i, len;
	frgb_color_t avgColors[4];
	struct {
		float h, max;
	} partdesc[4];
	int shortWidth = gdImageSX(im) < gdImageSY(im) ? gdImageSX(im) : gdImageSY(im);
	void *pngptr;
	sqlite3_stmt *stmt = NULL;
	for(i = 0; i < 4; i++) {
		avgColors[i].r = 0.0f, avgColors[i].g = 0.0f;
		avgColors[i].b = 0.0f; avgColors[i].n = 0;
	}
	for(x = 0; x < gdImageSX(im); x++) {
		for(y = 0; y < gdImageSY(im); y++) {
			int pix = gdImageGetTrueColorPixel(im, x, y);
			float r = ((pix & 0xff0000) >> 16) / 255.0f;
			float g = ((pix & 0xff00) >> 8) / 255.0f, b = (pix & 0xff) / 255.0f;
			frgb_color_t *avg = &avgColors[(y >= midY ? 2 : 0) + (x >= midX ? 1 : 0)];
			int n = avg->n++;
			avg->r = ((avg->r * n) + r) / (n + 1);
			avg->g = ((avg->g * n) + g) / (n + 1);
			avg->b = ((avg->b * n) + b) / (n + 1);
		}
	}
	for(i = 0; i < 4; i++) {
		float r = avgColors[i].r, g = avgColors[i].g, b = avgColors[i].b;
		float max = r > g ? r : g, min = r < g ? r : g, delta, h;
		max = max > b ? max : b;
		delta = max - (min = min < b ? min : b);
		if(max == min) { h = 0.0f / 0.0f; }
		else if(max == g) { h = 60.0f * (b - r) / delta + 120.0f; }
		else if(max == b) { h = 60.0f * (r - g) / delta + 240.0f; }
		else if(g >= b) {
			h = 60.0f * (g - b) / delta;
		} else {
			h = 60.0f * (g - b) / delta + 240.0f;
		}
		partdesc[i].h = h, partdesc[i].max = max;
	}
	if(sqlite3_prepare_v2(db,
		"SELECT id FROM images WHERE imgsize = ? AND desc = ?",
		-1, &stmt, NULL) == SQLITE_OK) {
		sqlite3_bind_int(stmt, 1, shortWidth);
		sqlite3_bind_blob(stmt, 2, partdesc, sizeof(partdesc), NULL);
		if(sqlite3_step(stmt) == SQLITE_ROW) {
			int id = sqlite3_column_int(stmt, 0);
			printf("similar image #%d already exists\n", id);
			sqlite3_finalize(stmt);
			return;
		}
		sqlite3_finalize(stmt);
		stmt = NULL;
	}
	if(!(pngptr = gdImagePngPtr(im, &len))) return;
	if(sqlite3_prepare_v2(db,
		"INSERT INTO images(imgsize, desc, pngdata, regtime) "
		"VALUES(?, ?, ?, DATETIME('now'))",
		-1, &stmt, NULL) == SQLITE_OK) {
		sqlite3_bind_int(stmt, 1, shortWidth);
		sqlite3_bind_blob(stmt, 2, partdesc, sizeof(partdesc), NULL);
		sqlite3_bind_blob(stmt, 3, pngptr, len, NULL);
		if(sqlite3_step(stmt) == SQLITE_DONE) {
			int id = sqlite3_last_insert_rowid(db);
			printf("new image #%d registered\n", id);
		} else {
			puts(sqlite3_errmsg(db));
		}
		sqlite3_finalize(stmt);
	}
	gdFree(pngptr);
}

gdImagePtr squareCrop(gdImagePtr source) {
	gdRect crop;
	if(gdImageSX(source) < gdImageSY(source)) {
		crop.width = crop.height = gdImageSX(source);
		crop.x = 0;
		crop.y = (gdImageSY(source) - crop.height) / 2;
	} else {
		crop.width = crop.height = gdImageSY(source);
		crop.x = (gdImageSX(source) - crop.width) / 2;
		crop.y = 0;
	}
	return gdImageCrop(source, &crop);
}

void handleArg(char *arg, sqlite3 *db)
{
	gdImagePtr source = gdImageCreateFromFile(arg);
	if(source) {
		gdImagePtr image = NULL;
		if(gdImageSX(source) == gdImageSY(source)) {
			image = source;
		} else {
			image = squareCrop(source);
			gdImageDestroy(source);
			if(!image) return;
		}
		handleImage(image, db);
		gdImageDestroy(image);
	} else {
		fputs("error: failed to load image\n", stderr);
		return;
	}
}

int main(int argc, char *argv[])
{
	if(argc >= 3) {
		sqlite3 *db = NULL;
		if(SQLITE_OK == sqlite3_open(argv[1], &db)) {
			sqlite3_exec(db, "CREATE TABLE IF NOT EXISTS images("
				"id INTEGER PRIMARY KEY, imgsize INTEGER, desc BLOB,"
				"pngdata BLOB, regtime DATETIME);", NULL, NULL, NULL);
			for(int i = 2; i < argc; i++) {
				handleArg(argv[i], db);
			}
			sqlite3_close(db);
		} else {
			fputs("error: failed to open database\n", stderr);
			return 1;
		}
	} else {
		fprintf(stderr, "usage: %s <dbfile> <imagefile>\n", argv[0]);
	}
	return 0;
}
