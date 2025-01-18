
#include "stb_image.hpp"

// public domain "baseline" PNG decoder   v0.10  Sean Barrett 2006-11-18
//    simple implementation
//      - only 8-bit samples
//      - no CRC checking
//      - allocates lots of intermediate memory
//        - avoids problem of streaming data between subsystems
//        - avoids explicit window management
//    performance
//      - uses stb_zlib, a PD zlib implementation with fast huffman decoding

#include "stb_zlib.hpp"

typedef struct
{
	stbi__uint32 length;
	stbi__uint32 type;
} stbi__pngchunk;

static stbi__pngchunk stbi__get_chunk_header(stbi__context *s)
{
	stbi__pngchunk c;
	c.length = stbi__get32be(s);
	c.type = stbi__get32be(s);
	return c;
}

static int stbi__check_png_header(stbi__context *s)
{
	static const stbi_uc png_sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
	int i;
	for (i = 0; i < 8; ++i)
		if (stbi__get8(s) != png_sig[i]) return stbi__err("bad png sig", "Not a PNG");
	return 1;
}

typedef struct
{
	stbi__context *s;
	stbi_uc *idata, *expanded, *out;
	int depth;
} stbi__png;


enum
{
	STBI__F_none = 0,
	STBI__F_sub = 1,
	STBI__F_up = 2,
	STBI__F_avg = 3,
	STBI__F_paeth = 4,
	// synthetic filter used for first scanline to avoid needing a dummy row of 0s
	STBI__F_avg_first
};

static stbi_uc first_row_filter[5] =
{
	STBI__F_none,
	STBI__F_sub,
	STBI__F_none,
	STBI__F_avg_first,
	STBI__F_sub // Paeth with b=c=0 turns out to be equivalent to sub
};

static int stbi__paeth(int a, int b, int c)
{
	// This formulation looks very different from the reference in the PNG spec, but is
	// actually equivalent and has favorable data dependencies and admits straightforward
	// generation of branch-free code, which helps performance significantly.
	int thresh = c * 3 - (a + b);
	int lo = a < b ? a : b;
	int hi = a < b ? b : a;
	int t0 = (hi <= thresh) ? lo : c;
	int t1 = (thresh <= lo) ? hi : t0;
	return t1;
}

static const stbi_uc stbi__depth_scale_table[9] = {0, 0xff, 0x55, 0, 0x11, 0, 0, 0, 0x01};

// adds an extra all-255 alpha channel
// dest == src is legal
// img_n must be 1 or 3
static void stbi__create_png_alpha_expand8(stbi_uc *dest, stbi_uc *src, stbi__uint32 x, int img_n)
{
	int i;
	// must process data backwards since we allow dest==src
	if (img_n == 1)
	{
		for (i = x - 1; i >= 0; --i)
		{
			dest[i * 2 + 1] = 255;
			dest[i * 2 + 0] = src[i];
		}
	}
	else
	{
		STBI_ASSERT(img_n == 3);
		for (i = x - 1; i >= 0; --i)
		{
			dest[i * 4 + 3] = 255;
			dest[i * 4 + 2] = src[i * 3 + 2];
			dest[i * 4 + 1] = src[i * 3 + 1];
			dest[i * 4 + 0] = src[i * 3 + 0];
		}
	}
}

// create the png data from post-deflated data
static int stbi__create_png_image_raw(stbi__png *a, stbi_uc *raw, stbi__uint32 raw_len, int out_n, stbi__uint32 x,
                                      stbi__uint32 y, int depth, int color)
{
	int bytes = (depth == 16 ? 2 : 1);
	stbi__context *s = a->s;
	stbi__uint32 i, j, stride = x * out_n * bytes;
	stbi__uint32 img_len, img_width_bytes;
	stbi_uc *filter_buf;
	int all_ok = 1;
	int k;
	int img_n = s->img_n; // copy it into a local for later

	int output_bytes = out_n * bytes;
	int filter_bytes = img_n * bytes;
	int width = x;

	STBI_ASSERT(out_n == s->img_n || out_n == s->img_n+1);
	a->out = (stbi_uc *) stbi__malloc_mad3(x, y, output_bytes, 0); // extra bytes to write off the end into
	if (!a->out) return stbi__err("outofmem", "Out of memory");

	// note: error exits here don't need to clean up a->out individually,
	// stbi__do_png always does on error.
	if (!stbi__mad3sizes_valid(img_n, x, depth, 7)) return stbi__err("too large", "Corrupt PNG");
	img_width_bytes = (((img_n * x * depth) + 7) >> 3);
	if (!stbi__mad2sizes_valid(img_width_bytes, y, img_width_bytes)) return stbi__err("too large", "Corrupt PNG");
	img_len = (img_width_bytes + 1) * y;

	// we used to check for exact match between raw_len and img_len on non-interlaced PNGs,
	// but issue #276 reported a PNG in the wild that had extra data at the end (all zeros),
	// so just check for raw_len < img_len always.
	if (raw_len < img_len) return stbi__err("not enough pixels", "Corrupt PNG");

	// Allocate two scan lines worth of filter workspace buffer.
	filter_buf = (stbi_uc *) stbi__malloc_mad2(img_width_bytes, 2, 0);
	if (!filter_buf) return stbi__err("outofmem", "Out of memory");

	// Filtering for low-bit-depth images
	if (depth < 8)
	{
		filter_bytes = 1;
		width = img_width_bytes;
	}

	for (j = 0; j < y; ++j)
	{
		// cur/prior filter buffers alternate
		stbi_uc *cur = filter_buf + (j & 1) * img_width_bytes;
		stbi_uc *prior = filter_buf + (~j & 1) * img_width_bytes;
		stbi_uc *dest = a->out + stride * j;
		int nk = width * filter_bytes;
		int filter = *raw++;

		// check filter type
		if (filter > 4)
		{
			all_ok = stbi__err("invalid filter", "Corrupt PNG");
			break;
		}

		// if first row, use special filter that doesn't sample previous row
		if (j == 0) filter = first_row_filter[filter];

		// perform actual filtering
		switch (filter)
		{
			case STBI__F_none:
				memcpy(cur, raw, nk);
				break;
			case STBI__F_sub:
				memcpy(cur, raw, filter_bytes);
				for (k = filter_bytes; k < nk; ++k)
					cur[k] = STBI__BYTECAST(raw[k] + cur[k-filter_bytes]);
				break;
			case STBI__F_up:
				for (k = 0; k < nk; ++k)
					cur[k] = STBI__BYTECAST(raw[k] + prior[k]);
				break;
			case STBI__F_avg:
				for (k = 0; k < filter_bytes; ++k)
					cur[k] = STBI__BYTECAST(raw[k] + (prior[k]>>1));
				for (k = filter_bytes; k < nk; ++k)
					cur[k] = STBI__BYTECAST(raw[k] + ((prior[k] + cur[k-filter_bytes])>>1));
				break;
			case STBI__F_paeth:
				for (k = 0; k < filter_bytes; ++k)
					cur[k] = STBI__BYTECAST(raw[k] + prior[k]); // prior[k] == stbi__paeth(0,prior[k],0)
				for (k = filter_bytes; k < nk; ++k)
					cur[k] = STBI__BYTECAST(raw[k] + stbi__paeth(cur[k-filter_bytes], prior[k], prior[k-filter_bytes]));
				break;
			case STBI__F_avg_first:
				memcpy(cur, raw, filter_bytes);
				for (k = filter_bytes; k < nk; ++k)
					cur[k] = STBI__BYTECAST(raw[k] + (cur[k-filter_bytes] >> 1));
				break;
		}

		raw += nk;

		// expand decoded bits in cur to dest, also adding an extra alpha channel if desired
		if (depth < 8)
		{
			stbi_uc scale = (color == 0) ? stbi__depth_scale_table[depth] : 1; // scale grayscale values to 0..255 range
			stbi_uc *in = cur;
			stbi_uc *out = dest;
			stbi_uc inb = 0;
			stbi__uint32 nsmp = x * img_n;

			// expand bits to bytes first
			if (depth == 4)
			{
				for (i = 0; i < nsmp; ++i)
				{
					if ((i & 1) == 0) inb = *in++;
					*out++ = scale * (inb >> 4);
					inb <<= 4;
				}
			}
			else if (depth == 2)
			{
				for (i = 0; i < nsmp; ++i)
				{
					if ((i & 3) == 0) inb = *in++;
					*out++ = scale * (inb >> 6);
					inb <<= 2;
				}
			}
			else
			{
				STBI_ASSERT(depth == 1);
				for (i = 0; i < nsmp; ++i)
				{
					if ((i & 7) == 0) inb = *in++;
					*out++ = scale * (inb >> 7);
					inb <<= 1;
				}
			}

			// insert alpha=255 values if desired
			if (img_n != out_n)
				stbi__create_png_alpha_expand8(dest, dest, x, img_n);
		}
		else if (depth == 8)
		{
			if (img_n == out_n)
				memcpy(dest, cur, x * img_n);
			else
				stbi__create_png_alpha_expand8(dest, cur, x, img_n);
		}
		else if (depth == 16)
		{
			// convert the image data from big-endian to platform-native
			stbi__uint16 *dest16 = (stbi__uint16 *) dest;
			stbi__uint32 nsmp = x * img_n;

			if (img_n == out_n)
			{
				for (i = 0; i < nsmp; ++i, ++dest16, cur += 2)
					*dest16 = (cur[0] << 8) | cur[1];
			}
			else
			{
				STBI_ASSERT(img_n+1 == out_n);
				if (img_n == 1)
				{
					for (i = 0; i < x; ++i, dest16 += 2, cur += 2)
					{
						dest16[0] = (cur[0] << 8) | cur[1];
						dest16[1] = 0xffff;
					}
				}
				else
				{
					STBI_ASSERT(img_n == 3);
					for (i = 0; i < x; ++i, dest16 += 4, cur += 6)
					{
						dest16[0] = (cur[0] << 8) | cur[1];
						dest16[1] = (cur[2] << 8) | cur[3];
						dest16[2] = (cur[4] << 8) | cur[5];
						dest16[3] = 0xffff;
					}
				}
			}
		}
	}

	STBI_FREE(filter_buf);
	if (!all_ok) return 0;

	return 1;
}

static int stbi__create_png_image(stbi__png *a, stbi_uc *image_data, stbi__uint32 image_data_len, int out_n, int depth,
                                  int color, int interlaced)
{
	int bytes = (depth == 16 ? 2 : 1);
	int out_bytes = out_n * bytes;
	stbi_uc *final;
	int p;
	if (!interlaced)
		return stbi__create_png_image_raw(a, image_data, image_data_len, out_n, a->s->img_x, a->s->img_y, depth, color);

	// de-interlacing
	final = (stbi_uc *) stbi__malloc_mad3(a->s->img_x, a->s->img_y, out_bytes, 0);
	if (!final) return stbi__err("outofmem", "Out of memory");
	for (p = 0; p < 7; ++p)
	{
		int xorig[] = {0, 4, 0, 2, 0, 1, 0};
		int yorig[] = {0, 0, 4, 0, 2, 0, 1};
		int xspc[] = {8, 8, 4, 4, 2, 2, 1};
		int yspc[] = {8, 8, 8, 4, 4, 2, 2};
		int i, j, x, y;
		// pass1_x[4] = 0, pass1_x[5] = 1, pass1_x[12] = 1
		x = (a->s->img_x - xorig[p] + xspc[p] - 1) / xspc[p];
		y = (a->s->img_y - yorig[p] + yspc[p] - 1) / yspc[p];
		if (x && y)
		{
			stbi__uint32 img_len = ((((a->s->img_n * x * depth) + 7) >> 3) + 1) * y;
			if (!stbi__create_png_image_raw(a, image_data, image_data_len, out_n, x, y, depth, color))
			{
				STBI_FREE(final);
				return 0;
			}
			for (j = 0; j < y; ++j)
			{
				for (i = 0; i < x; ++i)
				{
					int out_y = j * yspc[p] + yorig[p];
					int out_x = i * xspc[p] + xorig[p];
					memcpy(final + out_y * a->s->img_x * out_bytes + out_x * out_bytes,
					       a->out + (j * x + i) * out_bytes, out_bytes);
				}
			}
			STBI_FREE(a->out);
			image_data += img_len;
			image_data_len -= img_len;
		}
	}
	a->out = final;

	return 1;
}

static int stbi__compute_transparency(stbi__png *z, stbi_uc tc[3], int out_n)
{
	stbi__context *s = z->s;
	stbi__uint32 i, pixel_count = s->img_x * s->img_y;
	stbi_uc *p = z->out;

	// compute color-based transparency, assuming we've
	// already got 255 as the alpha value in the output
	STBI_ASSERT(out_n == 2 || out_n == 4);

	if (out_n == 2)
	{
		for (i = 0; i < pixel_count; ++i)
		{
			p[1] = (p[0] == tc[0] ? 0 : 255);
			p += 2;
		}
	}
	else
	{
		for (i = 0; i < pixel_count; ++i)
		{
			if (p[0] == tc[0] && p[1] == tc[1] && p[2] == tc[2])
				p[3] = 0;
			p += 4;
		}
	}
	return 1;
}

static int stbi__compute_transparency16(stbi__png *z, stbi__uint16 tc[3], int out_n)
{
	stbi__context *s = z->s;
	stbi__uint32 i, pixel_count = s->img_x * s->img_y;
	stbi__uint16 *p = (stbi__uint16 *) z->out;

	// compute color-based transparency, assuming we've
	// already got 65535 as the alpha value in the output
	STBI_ASSERT(out_n == 2 || out_n == 4);

	if (out_n == 2)
	{
		for (i = 0; i < pixel_count; ++i)
		{
			p[1] = (p[0] == tc[0] ? 0 : 65535);
			p += 2;
		}
	}
	else
	{
		for (i = 0; i < pixel_count; ++i)
		{
			if (p[0] == tc[0] && p[1] == tc[1] && p[2] == tc[2])
				p[3] = 0;
			p += 4;
		}
	}
	return 1;
}

static int stbi__expand_png_palette(stbi__png *a, stbi_uc *palette, int len, int pal_img_n)
{
	stbi__uint32 i, pixel_count = a->s->img_x * a->s->img_y;
	stbi_uc *p, *temp_out, *orig = a->out;

	p = (stbi_uc *) stbi__malloc_mad2(pixel_count, pal_img_n, 0);
	if (p == NULL) return stbi__err("outofmem", "Out of memory");

	// between here and free(out) below, exitting would leak
	temp_out = p;

	if (pal_img_n == 3)
	{
		for (i = 0; i < pixel_count; ++i)
		{
			int n = orig[i] * 4;
			p[0] = palette[n];
			p[1] = palette[n + 1];
			p[2] = palette[n + 2];
			p += 3;
		}
	}
	else
	{
		for (i = 0; i < pixel_count; ++i)
		{
			int n = orig[i] * 4;
			p[0] = palette[n];
			p[1] = palette[n + 1];
			p[2] = palette[n + 2];
			p[3] = palette[n + 3];
			p += 4;
		}
	}
	STBI_FREE(a->out);
	a->out = temp_out;

	STBI_NOTUSED(len);

	return 1;
}

static int stbi__unpremultiply_on_load_global = 0;
static int stbi__de_iphone_flag_global = 0;

STBIDEF void stbi_set_unpremultiply_on_load(int flag_true_if_should_unpremultiply)
{
	stbi__unpremultiply_on_load_global = flag_true_if_should_unpremultiply;
}

STBIDEF void stbi_convert_iphone_png_to_rgb(int flag_true_if_should_convert)
{
	stbi__de_iphone_flag_global = flag_true_if_should_convert;
}

#ifndef STBI_THREAD_LOCAL
#define stbi__unpremultiply_on_load  stbi__unpremultiply_on_load_global
#define stbi__de_iphone_flag  stbi__de_iphone_flag_global
#else
static STBI_THREAD_LOCAL int stbi__unpremultiply_on_load_local, stbi__unpremultiply_on_load_set;
static STBI_THREAD_LOCAL int stbi__de_iphone_flag_local, stbi__de_iphone_flag_set;

STBIDEF void stbi_set_unpremultiply_on_load_thread(int flag_true_if_should_unpremultiply)
{
	stbi__unpremultiply_on_load_local = flag_true_if_should_unpremultiply;
	stbi__unpremultiply_on_load_set = 1;
}

STBIDEF void stbi_convert_iphone_png_to_rgb_thread(int flag_true_if_should_convert)
{
	stbi__de_iphone_flag_local = flag_true_if_should_convert;
	stbi__de_iphone_flag_set = 1;
}

#define stbi__unpremultiply_on_load  (stbi__unpremultiply_on_load_set           \
                                       ? stbi__unpremultiply_on_load_local      \
                                       : stbi__unpremultiply_on_load_global)
#define stbi__de_iphone_flag  (stbi__de_iphone_flag_set                         \
                                ? stbi__de_iphone_flag_local                    \
                                : stbi__de_iphone_flag_global)
#endif // STBI_THREAD_LOCAL

static void stbi__de_iphone(stbi__png *z)
{
	stbi__context *s = z->s;
	stbi__uint32 i, pixel_count = s->img_x * s->img_y;
	stbi_uc *p = z->out;

	if (s->img_out_n == 3)
	{
		// convert bgr to rgb
		for (i = 0; i < pixel_count; ++i)
		{
			stbi_uc t = p[0];
			p[0] = p[2];
			p[2] = t;
			p += 3;
		}
	}
	else
	{
		STBI_ASSERT(s->img_out_n == 4);
		if (stbi__unpremultiply_on_load)
		{
			// convert bgr to rgb and unpremultiply
			for (i = 0; i < pixel_count; ++i)
			{
				stbi_uc a = p[3];
				stbi_uc t = p[0];
				if (a)
				{
					stbi_uc half = a / 2;
					p[0] = (p[2] * 255 + half) / a;
					p[1] = (p[1] * 255 + half) / a;
					p[2] = (t * 255 + half) / a;
				}
				else
				{
					p[0] = p[2];
					p[2] = t;
				}
				p += 4;
			}
		}
		else
		{
			// convert bgr to rgb
			for (i = 0; i < pixel_count; ++i)
			{
				stbi_uc t = p[0];
				p[0] = p[2];
				p[2] = t;
				p += 4;
			}
		}
	}
}

#define STBI__PNG_TYPE(a,b,c,d)  (((unsigned) (a) << 24) + ((unsigned) (b) << 16) + ((unsigned) (c) << 8) + (unsigned) (d))

static int stbi__parse_png_file(stbi__png *z, int scan, int req_comp)
{
	stbi_uc palette[1024], pal_img_n = 0;
	stbi_uc has_trans = 0, tc[3] = {0};
	stbi__uint16 tc16[3];
	stbi__uint32 ioff = 0, idata_limit = 0, i, pal_len = 0;
	int first = 1, k, interlace = 0, color = 0, is_iphone = 0;
	stbi__context *s = z->s;

	z->expanded = NULL;
	z->idata = NULL;
	z->out = NULL;

	if (!stbi__check_png_header(s)) return 0;

	if (scan == STBI__SCAN_type) return 1;

	for (;;)
	{
		stbi__pngchunk c = stbi__get_chunk_header(s);
		switch (c.type)
		{
			case STBI__PNG_TYPE('C', 'g', 'B', 'I'):
				is_iphone = 1;
				stbi__skip(s, c.length);
				break;
			case STBI__PNG_TYPE('I', 'H', 'D', 'R'): {
				int comp, filter;
				if (!first) return stbi__err("multiple IHDR", "Corrupt PNG");
				first = 0;
				if (c.length != 13) return stbi__err("bad IHDR len", "Corrupt PNG");
				s->img_x = stbi__get32be(s);
				s->img_y = stbi__get32be(s);
				if (s->img_y > STBI_MAX_DIMENSIONS) return stbi__err("too large", "Very large image (corrupt?)");
				if (s->img_x > STBI_MAX_DIMENSIONS) return stbi__err("too large", "Very large image (corrupt?)");
				z->depth = stbi__get8(s);
				if (z->depth != 1 && z->depth != 2 && z->depth != 4 && z->depth != 8 && z->depth != 16) return stbi__err(
					"1/2/4/8/16-bit only", "PNG not supported: 1/2/4/8/16-bit only");
				color = stbi__get8(s);
				if (color > 6) return stbi__err("bad ctype", "Corrupt PNG");
				if (color == 3 && z->depth == 16) return stbi__err("bad ctype", "Corrupt PNG");
				if (color == 3) pal_img_n = 3;
				else if (color & 1) return stbi__err("bad ctype", "Corrupt PNG");
				comp = stbi__get8(s);
				if (comp) return stbi__err("bad comp method", "Corrupt PNG");
				filter = stbi__get8(s);
				if (filter) return stbi__err("bad filter method", "Corrupt PNG");
				interlace = stbi__get8(s);
				if (interlace > 1) return stbi__err("bad interlace method", "Corrupt PNG");
				if (!s->img_x || !s->img_y) return stbi__err("0-pixel image", "Corrupt PNG");
				if (!pal_img_n)
				{
					s->img_n = (color & 2 ? 3 : 1) + (color & 4 ? 1 : 0);
					if ((1 << 30) / s->img_x / s->img_n < s->img_y) return stbi__err(
						"too large", "Image too large to decode");
				}
				else
				{
					// if paletted, then pal_n is our final components, and
					// img_n is # components to decompress/filter.
					s->img_n = 1;
					if ((1 << 30) / s->img_x / 4 < s->img_y) return stbi__err("too large", "Corrupt PNG");
				}
				// even with SCAN_header, have to scan to see if we have a tRNS
				break;
			}

			case STBI__PNG_TYPE('P', 'L', 'T', 'E'): {
				if (first) return stbi__err("first not IHDR", "Corrupt PNG");
				if (c.length > 256 * 3) return stbi__err("invalid PLTE", "Corrupt PNG");
				pal_len = c.length / 3;
				if (pal_len * 3 != c.length) return stbi__err("invalid PLTE", "Corrupt PNG");
				for (i = 0; i < pal_len; ++i)
				{
					palette[i * 4 + 0] = stbi__get8(s);
					palette[i * 4 + 1] = stbi__get8(s);
					palette[i * 4 + 2] = stbi__get8(s);
					palette[i * 4 + 3] = 255;
				}
				break;
			}

			case STBI__PNG_TYPE('t', 'R', 'N', 'S'): {
				if (first) return stbi__err("first not IHDR", "Corrupt PNG");
				if (z->idata) return stbi__err("tRNS after IDAT", "Corrupt PNG");
				if (pal_img_n)
				{
					if (scan == STBI__SCAN_header)
					{
						s->img_n = 4;
						return 1;
					}
					if (pal_len == 0) return stbi__err("tRNS before PLTE", "Corrupt PNG");
					if (c.length > pal_len) return stbi__err("bad tRNS len", "Corrupt PNG");
					pal_img_n = 4;
					for (i = 0; i < c.length; ++i)
						palette[i * 4 + 3] = stbi__get8(s);
				}
				else
				{
					if (!(s->img_n & 1)) return stbi__err("tRNS with alpha", "Corrupt PNG");
					if (c.length != (stbi__uint32) s->img_n * 2) return stbi__err("bad tRNS len", "Corrupt PNG");
					has_trans = 1;
					// non-paletted with tRNS = constant alpha. if header-scanning, we can stop now.
					if (scan == STBI__SCAN_header)
					{
						++s->img_n;
						return 1;
					}
					if (z->depth == 16)
					{
						for (k = 0; k < s->img_n && k < 3; ++k) // extra loop test to suppress false GCC warning
							tc16[k] = (stbi__uint16) stbi__get16be(s); // copy the values as-is
					}
					else
					{
						for (k = 0; k < s->img_n && k < 3; ++k)
							tc[k] = (stbi_uc) (stbi__get16be(s) & 255) * stbi__depth_scale_table[z->depth];
						// non 8-bit images will be larger
					}
				}
				break;
			}

			case STBI__PNG_TYPE('I', 'D', 'A', 'T'): {
				if (first) return stbi__err("first not IHDR", "Corrupt PNG");
				if (pal_img_n && !pal_len) return stbi__err("no PLTE", "Corrupt PNG");
				if (scan == STBI__SCAN_header)
				{
					// header scan definitely stops at first IDAT
					if (pal_img_n)
						s->img_n = pal_img_n;
					return 1;
				}
				if (c.length > (1u << 30)) return stbi__err("IDAT size limit", "IDAT section larger than 2^30 bytes");
				if ((int) (ioff + c.length) < (int) ioff) return 0;
				if (ioff + c.length > idata_limit)
				{
					stbi__uint32 idata_limit_old = idata_limit;
					stbi_uc *p;
					if (idata_limit == 0) idata_limit = c.length > 4096 ? c.length : 4096;
					while (ioff + c.length > idata_limit)
						idata_limit *= 2;
					STBI_NOTUSED(idata_limit_old);
					p = (stbi_uc *) STBI_REALLOC_SIZED(z->idata, idata_limit_old, idata_limit);
					if (p == NULL) return stbi__err("outofmem", "Out of memory");
					z->idata = p;
				}
				if (!stbi__getn(s, z->idata + ioff, c.length)) return stbi__err("outofdata", "Corrupt PNG");
				ioff += c.length;
				break;
			}

			case STBI__PNG_TYPE('I', 'E', 'N', 'D'): {
				stbi__uint32 raw_len, bpl;
				if (first) return stbi__err("first not IHDR", "Corrupt PNG");
				if (scan != STBI__SCAN_load) return 1;
				if (z->idata == NULL) return stbi__err("no IDAT", "Corrupt PNG");
				// initial guess for decoded data size to avoid unnecessary reallocs
				bpl = (s->img_x * z->depth + 7) / 8; // bytes per line, per component
				raw_len = bpl * s->img_y * s->img_n /* pixels */ + s->img_y /* filter mode per row */;
				z->expanded = (stbi_uc *) stbi_zlib_decode_malloc_guesssize_headerflag(
					(char *) z->idata, ioff, raw_len, (int *) &raw_len, !is_iphone);
				if (z->expanded == NULL) return 0; // zlib should set error
				STBI_FREE(z->idata);
				z->idata = NULL;
				if ((req_comp == s->img_n + 1 && req_comp != 3 && !pal_img_n) || has_trans)
					s->img_out_n = s->img_n + 1;
				else
					s->img_out_n = s->img_n;
				if (!stbi__create_png_image(z, z->expanded, raw_len, s->img_out_n, z->depth, color, interlace)) return 0;
				if (has_trans)
				{
					if (z->depth == 16)
					{
						if (!stbi__compute_transparency16(z, tc16, s->img_out_n)) return 0;
					}
					else
					{
						if (!stbi__compute_transparency(z, tc, s->img_out_n)) return 0;
					}
				}
				if (is_iphone && stbi__de_iphone_flag && s->img_out_n > 2)
					stbi__de_iphone(z);
				if (pal_img_n)
				{
					// pal_img_n == 3 or 4
					s->img_n = pal_img_n; // record the actual colors we had
					s->img_out_n = pal_img_n;
					if (req_comp >= 3) s->img_out_n = req_comp;
					if (!stbi__expand_png_palette(z, palette, pal_len, s->img_out_n))
						return 0;
				}
				else if (has_trans)
				{
					// non-paletted image with tRNS -> source image has (constant) alpha
					++s->img_n;
				}
				STBI_FREE(z->expanded);
				z->expanded = NULL;
				// end of PNG chunk, read and skip CRC
				stbi__get32be(s);
				return 1;
			}

			default:
				// if critical, fail
				if (first) return stbi__err("first not IHDR", "Corrupt PNG");
				if ((c.type & (1 << 29)) == 0)
				{
#ifndef STBI_NO_FAILURE_STRINGS
					// not threadsafe
					static char invalid_chunk[] = "XXXX PNG chunk not known";
					invalid_chunk[0] = STBI__BYTECAST(c.type >> 24);
					invalid_chunk[1] = STBI__BYTECAST(c.type >> 16);
					invalid_chunk[2] = STBI__BYTECAST(c.type >> 8);
					invalid_chunk[3] = STBI__BYTECAST(c.type >> 0);
#endif
					return stbi__err(invalid_chunk, "PNG not supported: unknown PNG chunk type");
				}
				stbi__skip(s, c.length);
				break;
		}
		// end of PNG chunk, read and skip CRC
		stbi__get32be(s);
	}
}

static void *stbi__do_png(stbi__png *p, int *x, int *y, int *n, int req_comp, stbi__result_info *ri)
{
	void *result = NULL;
	if (req_comp < 0 || req_comp > 4) return stbi__errpuc("bad req_comp", "Internal error");
	if (stbi__parse_png_file(p, STBI__SCAN_load, req_comp))
	{
		if (p->depth <= 8)
			ri->bits_per_channel = 8;
		else if (p->depth == 16)
			ri->bits_per_channel = 16;
		else
			return stbi__errpuc("bad bits_per_channel", "PNG not supported: unsupported color depth");
		result = p->out;
		p->out = NULL;
		if (req_comp && req_comp != p->s->img_out_n)
		{
			if (ri->bits_per_channel == 8)
				result = stbi__convert_format((unsigned char *) result, p->s->img_out_n, req_comp, p->s->img_x,
				                              p->s->img_y);
			else
				result = stbi__convert_format16((stbi__uint16 *) result, p->s->img_out_n, req_comp, p->s->img_x,
				                                p->s->img_y);
			p->s->img_out_n = req_comp;
			if (result == NULL) return result;
		}
		*x = p->s->img_x;
		*y = p->s->img_y;
		if (n) *n = p->s->img_n;
	}
	STBI_FREE(p->out);
	p->out = NULL;
	STBI_FREE(p->expanded);
	p->expanded = NULL;
	STBI_FREE(p->idata);
	p->idata = NULL;

	return result;
}

static void *stbi__png_load(stbi__context *s, int *x, int *y, int *comp, int req_comp, stbi__result_info *ri)
{
	stbi__png p;
	p.s = s;
	return stbi__do_png(&p, x, y, comp, req_comp, ri);
}

static int stbi__png_test(stbi__context *s)
{
	int r;
	r = stbi__check_png_header(s);
	stbi__rewind(s);
	return r;
}

static int stbi__png_info_raw(stbi__png *p, int *x, int *y, int *comp)
{
	if (!stbi__parse_png_file(p, STBI__SCAN_header, 0))
	{
		stbi__rewind(p->s);
		return 0;
	}
	if (x) *x = p->s->img_x;
	if (y) *y = p->s->img_y;
	if (comp) *comp = p->s->img_n;
	return 1;
}

static int stbi__png_info(stbi__context *s, int *x, int *y, int *comp)
{
	stbi__png p;
	p.s = s;
	return stbi__png_info_raw(&p, x, y, comp);
}

static int stbi__png_is16(stbi__context *s)
{
	stbi__png p;
	p.s = s;
	if (!stbi__png_info_raw(&p, NULL, NULL, NULL))
		return 0;
	if (p.depth != 16)
	{
		stbi__rewind(p.s);
		return 0;
	}
	return 1;
}


static int stbi__info_main(stbi__context *s, int *x, int *y, int *comp)
{
	if (stbi__png_info(s, x, y, comp)) return 1;

	return stbi__err("unknown image type", "Image not of any known type, or corrupt");
}

static int stbi__is_16_main(stbi__context *s)
{
	if (stbi__png_is16(s)) return 1;

	return 0;
}



STBIDEF int stbi_info_from_memory(stbi_uc const *buffer, int len, int *x, int *y, int *comp)
{
	stbi__context s;
	stbi__start_mem(&s, buffer, len);
	return stbi__info_main(&s, x, y, comp);
}

STBIDEF int stbi_info_from_callbacks(stbi_io_callbacks const *c, void *user, int *x, int *y, int *comp)
{
	stbi__context s;
	stbi__start_callbacks(&s, (stbi_io_callbacks *) c, user);
	return stbi__info_main(&s, x, y, comp);
}

STBIDEF int stbi_is_16_bit_from_memory(stbi_uc const *buffer, int len)
{
	stbi__context s;
	stbi__start_mem(&s, buffer, len);
	return stbi__is_16_main(&s);
}

STBIDEF int stbi_is_16_bit_from_callbacks(stbi_io_callbacks const *c, void *user)
{
	stbi__context s;
	stbi__start_callbacks(&s, (stbi_io_callbacks *) c, user);
	return stbi__is_16_main(&s);
}



STBIDEF int stbi_info(char const *filename, int *x, int *y, int *comp)
{
	FILE *f = stbi__fopen(filename, "rb");
	int result;
	if (!f) return stbi__err("can't fopen", "Unable to open file");
	result = stbi_info_from_file(f, x, y, comp);
	fclose(f);
	return result;
}

STBIDEF int stbi_info_from_file(FILE *f, int *x, int *y, int *comp)
{
	int r;
	stbi__context s;
	long pos = ftell(f);
	stbi__start_file(&s, f);
	r = stbi__info_main(&s, x, y, comp);
	fseek(f, pos,SEEK_SET);
	return r;
}

STBIDEF int stbi_is_16_bit(char const *filename)
{
	FILE *f = stbi__fopen(filename, "rb");
	int result;
	if (!f) return stbi__err("can't fopen", "Unable to open file");
	result = stbi_is_16_bit_from_file(f);
	fclose(f);
	return result;
}

STBIDEF int stbi_is_16_bit_from_file(FILE *f)
{
	int r;
	stbi__context s;
	long pos = ftell(f);
	stbi__start_file(&s, f);
	r = stbi__is_16_main(&s);
	fseek(f, pos,SEEK_SET);
	return r;
}



