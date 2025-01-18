
#include "stb_image.hpp"

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


#ifndef STBI_NO_STDIO
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
#endif // !STBI_NO_STDIO


