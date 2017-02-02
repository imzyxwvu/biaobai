#include <stdio.h>
#include <string.h>
#include <math.h>
#include <gd.h>
#include <sqlite3.h>

typedef struct {
	struct { float h, max; } parts[4];
	int imageId;
	gdImagePtr image;
	struct imagedesc_t *next;
} imagedesc_t;

typedef struct {
	float r, g, b;
} rgbcolor_t;

#define BLOCKSIZE 50

sqlite3 *db = NULL;
imagedesc_t *images = NULL;

void loadAllImages(sqlite3 *dbptr)
{
	sqlite3_stmt *stmt;
	if(sqlite3_prepare_v2(dbptr,
		"SELECT id, desc FROM images", -1,
		&stmt, NULL) == SQLITE_OK) {
		while(sqlite3_step(stmt) == SQLITE_ROW) {
			imagedesc_t *desc = (imagedesc_t *)malloc(sizeof(imagedesc_t));
			const void *imageDesc;
			if(sqlite3_column_bytes(stmt, 1) != sizeof(desc->parts)) {
				free(desc);
				continue;
			}
			desc->imageId = sqlite3_column_int(stmt, 0);
			desc->image = NULL;
			imageDesc = sqlite3_column_blob(stmt, 1);
			memcpy(desc->parts, imageDesc, sizeof(desc->parts));
			desc->next = (struct imagedesc_t *)images;
			images = desc;
		}
		sqlite3_finalize(stmt);
	}
}

imagedesc_t *findSimilarImage(rgbcolor_t rgb[])
{
	imagedesc_t *current = images;
	imagedesc_t *bestMatch = NULL;
	float bestMatchScore = 0.0f;
	struct { float h, s, max; } color[4];
	int i;
	for(i = 0; i < 4; i++) {
		float r = rgb[i].r, g = rgb[i].g, b = rgb[i].b;
		float max = r > g ? r : g, min = r < g ? r : g, delta, h;
		max = max > b ? max : b;
		delta = max - (min = min < b ? min : b);
		if(max == min) { color[i].h = 0.0f / 0.0f; }
		else if(max == g) { color[i].h = 60.0f * (b - r) / delta + 120.0f; }
		else if(max == b) { color[i].h = 60.0f * (r - g) / delta + 240.0f; }
		else if(g >= b) {
			color[i].h = 60.0f * (g - b) / delta;
		} else {
			color[i].h = 60.0f * (g - b) / delta + 240.0f;
		}
		if(max > 0) {
			color[i].s = 1.0f - (min / max);
		} else {
			color[i].s = 0.0f;
		}
		color[i].max = max;
	}
	while(current) {
		float score = 0.0f;
		for(i = 0; i < 4; i++) {
			float hcurr = current->parts[i].h;
			if(color[i].s > 0.1f && hcurr == hcurr) {
				float hdelta = fabs(color[i].h - hcurr);
				float hscore, lscore;
				if(hdelta >= 180.0f) hdelta = 360.0f - hdelta;
				hscore = 1.0f - (hdelta / 180.0f);
				lscore = 1.0f -
					fabs(color[i].max - current->parts[i].max);
				score += color[i].s * hscore +
					(1.0f - color[i].s) * lscore;
			} else {
				score += 1.0f -
					fabs(color[i].max - current->parts[i].max);
			}
		}
		if(bestMatch == NULL || score > bestMatchScore) {
			bestMatch = current;
			bestMatchScore = score;
		}
		current = (imagedesc_t *)current->next;
	}
	return bestMatch;
}

gdImagePtr findImage(imagedesc_t *desc) {
	sqlite3_stmt *stmt;
	gdImagePtr image = NULL;
	if(desc->image) return desc->image;
	if(sqlite3_prepare_v2(db,
		"SELECT pngdata FROM images WHERE id = ?", -1,
		&stmt, NULL) == SQLITE_OK) {
		sqlite3_bind_int(stmt, 1, desc->imageId);
		if(sqlite3_step(stmt) == SQLITE_ROW) {
			gdImagePtr original = gdImageCreateFromPngPtr(
				sqlite3_column_bytes(stmt, 0),
				(void *)sqlite3_column_blob(stmt, 0));
			if(original) {
				image = gdImageCreateTrueColor(
					BLOCKSIZE, BLOCKSIZE);
				gdImageCopyResampled(image, original,
					0, 0, 0, 0, BLOCKSIZE, BLOCKSIZE,
					gdImageSX(original), gdImageSY(original));
				gdImageDestroy(original);
			}
		}
		sqlite3_finalize(stmt);
	}
	desc->image = image;
	return image;
}

gdImagePtr composeWith(gdImagePtr image) {
	float xBlkCount = gdImageSX(image) / 2,
	      yBlkCount = gdImageSY(image) / 2;
	int x, y, xs, ys;
	gdImagePtr target = gdImageCreateTrueColor(
		xBlkCount * BLOCKSIZE, yBlkCount * BLOCKSIZE);
	if(target == NULL) return NULL;
	for(x = 0; x < xBlkCount; x++) {
		for(y = 0; y < yBlkCount; y++) {
			rgbcolor_t color[4];
			gdImagePtr im;
			for(xs = 0; xs < 2; xs++) {
				for(ys = 0; ys < 2; ys++) {
					int i = ys * 2 + xs;
					int pix = gdImageGetTrueColorPixel(
						image, x * 2 + xs, y * 2 + ys);
					color[i].r = ((pix & 0xff0000) >> 16) / 255.0f;
					color[i].g = ((pix & 0xff00) >> 8) / 255.0f;
					color[i].b = (pix & 0xff) / 255.0f;
				}
			}
			if(im = findImage(findSimilarImage(color))) {
				gdImageCopy(
					target, im, x * BLOCKSIZE, y * BLOCKSIZE, 0, 0,
					BLOCKSIZE, BLOCKSIZE);
			}
		}
	}
	return target;
}

int main(int argc, char *argv[])
{
	if(argc == 3) {
		gdImagePtr reference;
		if(SQLITE_OK == sqlite3_open(argv[1], &db)) {
			loadAllImages(db);
			if(images == NULL) {
				fputs("error: no image loaded\n", stderr);
			}
		} else {
			fputs("error: failed to open database\n", stderr);
			return 1;
		}
		reference = gdImageCreateFromFile(argv[2]);
		if(reference) {
			gdImagePtr output = composeWith(reference);
			imagedesc_t *current = images;
			gdImageDestroy(reference);
			while(current) {
				if(current->image) gdImageDestroy(current->image);
				current->image = NULL;
				current = (imagedesc_t *)current->next;
			}
			gdImagePng(output, stdout);
			gdImageDestroy(output);
		} else {
			fputs("error: failed to load reference\n", stderr);
		}
		sqlite3_close(db);
	} else {
		fprintf(stderr, "usage: %s <dbfile> <imagefile>\n", argv[0]);
	}
}
