#include <stdlib.h>
#include <stdio.h>
#include <complex.h>
#include <pthread.h>
#include <math.h>
#include <string.h>

#define JULIA_SEED .28+ .008*I
 
// rip from https://rosettacode.org/wiki/Bitmap/Write_a_PPM_file#C

unsigned char ***create_base(int size) {
    // It helped to think of this as a 2d array of a tuple
    unsigned char ***d1 = malloc(size * sizeof(unsigned char**));
    for (int i = 0; i < size; i++) {
        unsigned char **d2 = malloc(size * sizeof(unsigned char*));
        for (int j = 0; j < size; j++) {
            unsigned char *d3 = malloc(3 * sizeof(unsigned char));
            d3[0] = 0; d3[1] = 0; d3[2] = 0;
            d2[j] = d3;
        }
        d1[i] = d2;
    }
	return d1;
}


// I think I can inline this, though I doubt it makes a difference because the compiler is smarter than me
double complex m_seq(double complex z_n) {
    // I think this method of exponentiation might work? I haven't tested it yet
    //double complex r = cpow(z_n, (double complex)(2 + 0 * I)) + JULIA_SEED;
    double complex r = (z_n * z_n) + JULIA_SEED;
	return r;
}

void c2b(double complex c, int size, int *restrict x, int *restrict y) {
    // I wanna vectorize this somehow, but both the pointers and the creal/cimag might make that harder
    // and working around that might make it slower

    // I have 2 working versions 
    // The second successfully vectorized the part that isn't ternaries I think
    // I'm not an expert at assembly but there's a lot of xmm registers
    // and then two sets of comparisons
    // It could also just be working with doubles in xmm and I'm just not good at assembly lol

    //int m_x, m_y;
    //m_x = creal(c);
    //m_y = cimag(c);

	//m_x = (m_x + 2) * size / 4;
	//m_y = (m_y + 2) * size / 4;
    //// ugly ternaries because I can't find C min/max/clamp functions
    //m_x = (m_x < (size - 1)) ? m_x : (size - 1);
    //m_y = (m_y < (size - 1)) ? m_y : (size - 1);
    //m_x = (m_x > 0) ? m_x : 0;
    //m_y = (m_y > 0) ? m_y : 0;

    //*x = m_x;
    //*y = m_y;

	*x = (int)((creal(c) + 2.0) * (double)size / 4.0);
	*y = (int)((cimag(c) + 2.0) * (double)size / 4.0);
    // ugly ternaries because I can't find C min/max/clamp functions
    *x = (*x < (size - 1)) ? *x : (size - 1);
    *y = (*y < (size - 1)) ? *y : (size - 1);
    *x = (*x > 0) ? *x : 0;
    *y = (*y > 0) ? *y : 0;
	return;
}

double complex b2c(int size, int x, int y) {
    // I'm ngl the operator prescednce in this is confusing, I had to look at c2b to understand the proper order
	double complex r = (x * 4.0 / (double)size - 2.0) + (y * 4.0 / (double)size - 2.0) * I;
	return r;
}

int escapes(double complex c, int iters) {
    double complex z_n = c;

    for (int i = 0; i < iters; i++) {
        z_n = m_seq(z_n);
        if (cabs(z_n) > 2) {
            return 1;
        }
    }

    return 0;
}

void one_val(unsigned char ***base, int size, int iters, int color, double complex c) {
    double complex z_n = c;
    if (!escapes(c, iters)) return;
    for (int i = 0; i < iters; i++) {
        if (cabs(z_n) > 2) {
            return;
        }
        int x, y;
        c2b(z_n, size, &x, &y);

        // hopefully at least this can get vectorized
        // gotta have the microptimizations
        // Update: It can't be because of the compares
        x = (x < (size - 1)) ? x : (size - 1);
        y = (y < (size - 1)) ? y : (size - 1);

        unsigned char v = base[x][y][color];
        if (v > (255 - 25)) {
            v = 255;
        } else {
            v += 25;
        }

        base[x][y][color] = v;

        z_n = m_seq(z_n);
    }
	return;
}

typedef struct {
    unsigned char ***base;
    int size;
    int x_offset;
    int y_offset;
    int iters;
} color_portion_args;

// args are actually (unsigned char ***base, int size, int x_offset, int y_offset, int iters)
void *color_portion(void *args) {
    color_portion_args *actual_args = args;

    unsigned char ***base = actual_args->base;
    int size = actual_args->size;
    int x_offset = actual_args->x_offset;
    int y_offset = actual_args->y_offset;
    int iters = actual_args->iters;

    int i_list[3] = {iters * 5, iters * 5, iters * 5};
    //int i_list[3] = {iters * 50, iters * 500, iters * 5000};

    for (int x = 0; x < (size / 5); x++) {
        for (int y = 0; y < (size / 5); y++) {
            one_val(base, size, i_list[0], 0, b2c(size, x + x_offset, y + y_offset));
            one_val(base, size, i_list[1], 1, b2c(size, x + x_offset, y + y_offset));
            one_val(base, size, i_list[2], 2, b2c(size, x + x_offset, y + y_offset));
        }
    }
    free(actual_args);
    return 0;
}

void get_colors(unsigned char ***base, int size, int iters) {

    // I'm sure I can multithread this somehow
    // My laptop can handle 16 threads so uh gonna use that many hardcoded 
    // as opposed to $(nproc) as an argument.
    //
    // also because 16 is a perfet square.
    // I switched to 25 because my cpu was only at a measly 40% usage
    // I was wrong.
    // NVM forgot to join them

    pthread_t threads[25];

    for (int x = 0; x < 5; x++) {
        for (int y = 0; y < 5; y++) {
            color_portion_args *args = malloc(sizeof(color_portion_args));
            args->base = base;
            args->size = size;
            args->x_offset = (size / 5 * x);
            args->y_offset = (size / 5 * y);
            args->iters = iters;
            if (pthread_create(&threads[(5*x) + y], NULL, color_portion, (void*)args)) {
                printf("uh oh");
                free(args);
            }
        }
    }

    for (int i = 0; i < 25; i++) {
        pthread_join(threads[i], NULL);
    }

	return;
}

// this would make such a cool pure function 
unsigned char sigmoid(unsigned char pixel, const int newMax, const int alpha, const int beta) {
    // I think a language/ide where I could input and render LaTeX math as a valid function would be neat
    return (unsigned char)(newMax * (1 / (int)(1.0 + (int)exp(-((double)pixel - (double)beta) / (double)alpha))));
}

// OPTIONAL
// That said, you images will look bad without this.
// The Python sample had a hacky solution.
// We accept a base, and equalize values to percentiles rather than counts
// You equalized images in CS 151 ImageShop.
void sigmoid_normalize(unsigned char ***base, int size) {
    // Not sure if linear or sigmoid is better
    // I'm extra, so I'm gonna try sigmoid
    // $I_N = (newMax - newMin)\frac{1}{1+e^{i\frac{I-\beta}{\alpha}}} + newMin$
    // ^ Sigmoid curve from https://en.wikipedia.org/wiki/Normalization_(image_processing)
    // I am going to experiment with max and min values, but for now I'll settle on:
    // newMin = 0; newMax = 256;
    // $\alpha$ = input intensity width $\beta$ = intensity around the range
    // Putting into desmos, I believe $\beta$ = 128$ $\alpha = 27$ will be good
    // I also may try normalizing different colors different amounts

    for (int x = 0; x < size; x++) {
        for (int y = 0; y < size; y++) {
            const int newMax = 254;
            const int alpha = 27;
            const int beta = 254;
            base[x][y][0] = sigmoid(base[x][y][0], newMax, alpha, beta);
            base[x][y][1] = sigmoid(base[x][y][1], newMax, alpha, beta);
            base[x][y][2] = sigmoid(base[x][y][2], newMax, alpha, beta);
        }
    }

	return;
}

// Side effect: populates image histogram
void create_image_histogram(unsigned char ***base, int size, int *histogram) {
    for (int x = 0; x < size; x++) {
        for (int y = 0; y < size; y++) {
            int lum = (int)(
                    (double)(base[x][y][0] * .299)+ 
                    (double)(base[x][y][1] * .587)+ 
                    (double)(base[x][y][2] * .114)
                    );
            if (lum > 255) {
                lum = 255;
            } else if (lum < 0) {
                lum = 0;
            }
            if ((lum > 255) || (lum < 0)) printf("lum: %d\n", lum);
            histogram[lum]++;
        }
    }
    return;
}

#define HISTOGRAM_SIZE 256

void equalize(unsigned char ***base, int size) {
    int image_histogram[HISTOGRAM_SIZE] = { 0 };

    create_image_histogram(base, size, image_histogram);

    // This function sums each index with the previous one, in order
    // thus, it creates a cumulative histogram;
    for (int i = 1; i < HISTOGRAM_SIZE; i++) {
        image_histogram[i] += image_histogram[i-1];
    }

    const int total_pixels = size * size;

    int cdf_min = total_pixels;

    for (int i = 0; i < HISTOGRAM_SIZE; i++) {
        if (image_histogram[i] < cdf_min) cdf_min = image_histogram[i];
    }

    // this is some of the grossest disgusting code I've ever written, 
    // and I learned to code in Java
    printf("mid: %d, total: %d\n", cdf_min, total_pixels);
    volatile int lum = 0;
    for (volatile int x = 0; x < size; x++) {
        for (volatile int y = 0; y < size; y++) {
            lum = (int)(
                (double)(base[x][y][0] * .299) + 
                (double)(base[x][y][1] * .587) + 
                (double)(base[x][y][2] * .114)
            );
            if (lum > 255) {
                lum = 255;
            } else if (lum < 0) {
                lum = 0;
            }
            base[x][y][0] = (unsigned char)((double)base[x][y][0] * 255.0 *  .299 * ((double)image_histogram[lum] - (double)cdf_min) / ((double)total_pixels - (double)cdf_min));
            base[x][y][1] = (unsigned char)((double)base[x][y][1] * 255.0 *  .587 * ((double)image_histogram[lum] - (double)cdf_min) / ((double)total_pixels - (double)cdf_min));
            base[x][y][2] = (unsigned char)((double)base[x][y][2] * 255.0 *  .114 * ((double)image_histogram[lum] - (double)cdf_min) / ((double)total_pixels - (double)cdf_min));

            //base[x][y][0] = (unsigned char)((double)base[x][y][0] * 255.0 * (1.0 / .299) * ((double)image_histogram[lum] - (double)cdf_min) / ((double)total_pixels - (double)cdf_min));
            //base[x][y][1] = (unsigned char)((double)base[x][y][1] * 255.0 * (1.0 / .587) * ((double)image_histogram[lum] - (double)cdf_min) / ((double)total_pixels - (double)cdf_min));
            //base[x][y][2] = (unsigned char)((double)base[x][y][2] * 255.0 * (1.0 / .114) * ((double)image_histogram[lum] - (double)cdf_min) / ((double)total_pixels - (double)cdf_min));

            //base[x][y][0] = (unsigned char)( (double)base[x][y][0] * 255.0 * ((double)image_histogram[lum] - (double)cdf_min) / ((double)total_pixels - (double)cdf_min));
            //base[x][y][1] = (unsigned char)( (double)base[x][y][1] * 255.0 * ((double)image_histogram[lum] - (double)cdf_min) / ((double)total_pixels - (double)cdf_min));
            //base[x][y][2] = (unsigned char)( (double)base[x][y][2] * 255.0 * ((double)image_histogram[lum] - (double)cdf_min) / ((double)total_pixels - (double)cdf_min));
        }
    }
    return;
}

// I had to write it myself I couldn't handle the minimalist design of the C stdlib
int clamp(int val, int min, int max) {
    if (val > max) {
        return max;
    } else if (val < min) {
        return min;
    } else {
        return val;
    }
}

void avg_filter(unsigned char ***base, int size) {
    unsigned char ***copy = create_base(size);
    for (int x = 0; x < size; x++) {
        for (int y = 0; y < size; y++) {
            copy[x][y][0] = base[x][y][0];
            copy[x][y][1] = base[x][y][1];
            copy[x][y][2] = base[x][y][2];
        }
    }

    for (int x = 0; x < size; x++) {
        for (int y = 0; y < size; y++) {
            base[x][y][0] = (unsigned char)((
                (int)copy[x][clamp(y-1, 0, size - 1)][0] +
                (int)copy[x][y][0] +
                (int)copy[x][clamp(y+1, 0, size - 1)][0] +
                (int)copy[clamp(x-1, 0, size - 1)][clamp(y-1, 0, size - 1)][0] +
                (int)copy[clamp(x-1, 0, size - 1)][clamp(y  , 0, size - 1)][0] +
                (int)copy[clamp(x-1, 0, size - 1)][clamp(y+1, 0, size - 1)][0] +
                (int)copy[clamp(x+1, 0, size - 1)][clamp(y-1, 0, size - 1)][0] +
                (int)copy[clamp(x+1, 0, size - 1)][clamp(y  , 0, size - 1)][0] +
                (int)copy[clamp(x+1, 0, size - 1)][clamp(y+1, 0, size - 1)][0]
                    ) / 9);
            base[x][y][1] = (unsigned char)((
                (int)copy[x][clamp(y-1, 0, size - 1)][1] +
                (int)copy[x][y][1] +
                (int)copy[x][clamp(y+1, 0, size - 1)][1] +
                (int)copy[clamp(x-1, 0, size - 1)][clamp(y-1, 0, size - 1)][1] +
                (int)copy[clamp(x-1, 0, size - 1)][clamp(y  , 0, size - 1)][1] +
                (int)copy[clamp(x-1, 0, size - 1)][clamp(y+1, 0, size - 1)][1] +
                (int)copy[clamp(x+1, 0, size - 1)][clamp(y-1, 0, size - 1)][1] +
                (int)copy[clamp(x+1, 0, size - 1)][clamp(y  , 0, size - 1)][1] +
                (int)copy[clamp(x+1, 0, size - 1)][clamp(y+1, 0, size - 1)][1]
                    ) / 9);
            base[x][y][2] = (unsigned char)((
                (int)copy[x][clamp(y-1, 0, size - 1)][2] +
                (int)copy[x][y][2] +
                (int)copy[x][clamp(y+1, 0, size - 1)][2] +
                (int)copy[clamp(x-1, 0, size - 1)][clamp(y-1, 0, size - 1)][2] +
                (int)copy[clamp(x-1, 0, size - 1)][clamp(y  , 0, size - 1)][2] +
                (int)copy[clamp(x-1, 0, size - 1)][clamp(y+1, 0, size - 1)][2] +
                (int)copy[clamp(x+1, 0, size - 1)][clamp(y-1, 0, size - 1)][2] +
                (int)copy[clamp(x+1, 0, size - 1)][clamp(y  , 0, size - 1)][2] +
                (int)copy[clamp(x+1, 0, size - 1)][clamp(y+1, 0, size - 1)][2]
                    ) / 9);
        }
    }
}

void make_brot(int size, int iters) {
	FILE *fp = fopen("brot.ppm", "wb"); /* b - binary mode */
	fprintf(fp, "P6\n%d %d\n255\n", size, size);
	static unsigned char color[3];

	fflush(stdout);
    unsigned char ***base = create_base(size);

    get_colors(base, size, iters);

    //sigmoid_normalize(base, size);

    //equalize(base, size);

    //avg_filter(base, size);

	for (int x = 0; x < size; x++) {
		for (int y = 0; y < size; y++) {
			fwrite(base[x][y], 1, 3, fp);
		}
	}
	fclose(fp);
	return;
}

 
int main(int argc, char **argv) {
    int size = 200;
    int iters = 20;

    if (argc == 1) {
        puts("Supply args: $1 = size $2 = iters");
    } else {
        size = atoi(argv[1]);
        if (size % 5 != 0) {
            size -= (size % 5);
        }
        iters = atoi(argv[2]);
    }

	make_brot(1024, 15);
	return 0;
}

