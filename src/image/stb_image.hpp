#ifndef STBI_INCLUDE_STB_IMAGE_H
#define STBI_INCLUDE_STB_IMAGE_H

#define STBI_NO_LINEAR
#define STBI_ONLY_PNG
#define STBI_NO_STDIO

enum
{
	STBI_default = 0, // only used for desired_channels

	STBI_grey = 1,
	STBI_grey_alpha = 2,
	STBI_rgb = 3,
	STBI_rgb_alpha = 4
};

typedef unsigned char stbi_uc;
typedef unsigned short stbi_us;

extern "C" {
#define STBIDEF extern

STBIDEF stbi_uc *coyote_stbi_load_from_memory(
	stbi_uc const *buffer,
	int len,
	int *x,
	int *y,
	int *channels_in_file,
	int desired_channels
);


// get a VERY brief reason for failure
// on most compilers (and ALL modern mainstream compilers) this is threadsafe
STBIDEF const char *coyote_stbi_failure_reason(void);

// free the loaded image -- this is just free()
STBIDEF void coyote_stbi_image_free(void *retval_from_stbi_load);

// get image dimensions & components without fully decoding
STBIDEF int coyote_stbi_info_from_memory(
	stbi_uc const *buffer,
	int len,
	int *x,
	int *y,
	int *comp
);
}

#endif // STBI_INCLUDE_STB_IMAGE_H
