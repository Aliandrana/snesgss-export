#include <stdio.h>
#include <stdlib.h>
//#include <unistd.h>
//#include <getopt.h>
//#include <stdbool.h>
#include <string.h>
#include <math.h>
#include "common.h"
#include "brr.h"

#include <limits>

// Convert a block from PCM to BRR
// Returns the squared error between original data and encoded data
// If "is_end_point" is true, the predictions p1/p2 at loop are also used in caluclating the error (depending on filter at loop)

#define CLAMP_16(n) ( ((signed short)(n) != (n)) ? ((signed short)(0x7fff - ((n)>>24))) : (n) )

double ADPCMMash(unsigned int shiftamount, u8 filter, const pcm_t PCM_data[16], bool write, bool is_end_point);
void ADPCMBlockMash(const pcm_t PCM_data[16], bool is_loop_point, bool is_end_point);
pcm_t *resample(pcm_t *samples, int samples_length, int out_length, char type);

// This function applies a treble boosting filter that compensates the gauss lowpass filter
pcm_t *treble_boost_filter(pcm_t *samples, int length);

