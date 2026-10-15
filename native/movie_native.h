#ifndef IMBK_MOVIE_NATIVE_H
#define IMBK_MOVIE_NATIVE_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct imbk_movie imbk_movie;

/* Opens exactly [offset, offset + size) in path. Returns NULL on failure.
   One caller thread per handle; separate handles may be used concurrently.
   The handle owns one Windows decode worker and four buffered frames. */
imbk_movie *imbk_movie_open(const char *path, unsigned long long offset,
                          unsigned long long size);
void imbk_movie_close(imbk_movie *movie);
int imbk_movie_width(imbk_movie *movie);
int imbk_movie_height(imbk_movie *movie);
double imbk_movie_fps(imbk_movie *movie);

/* Display-order, zero-based index. Repeated requests reuse the buffered frame;
   backward requests restart decoding. Copies Y, U (Cb), V (Cr), without colour
   conversion. Chroma dimensions are ceil(width/2), ceil(height/2).
   Pitches must be positive and at least the corresponding plane width.
   Returns 1 on success, 0 at EOF or for invalid arguments/decode failure.
   Do not close a handle concurrently with another call on that handle. */
int imbk_movie_frame(imbk_movie *movie, int index,
                     unsigned char *y, int y_pitch,
                     unsigned char *u, int u_pitch,
                     unsigned char *v, int v_pitch);

#ifdef __cplusplus
}
#endif
#endif
