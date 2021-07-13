#include <stdint.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <time.h>
#include <memory.h>
#include <jpeglib.h>
#include <pthread.h>
#include "config.h"

const unsigned char INPUT_IMAGE_COMPONENTS_NUMBER = 3;
const unsigned char OUTPUT_IMAGE_COMPONENTS_NUMBER = 1;

const unsigned char calculate_gray(
  const unsigned char red,
  const unsigned char green,
  const unsigned char blue
) {
  return red * 0.2126 + green * 0.7152 + blue * 0.0722;
}

void transform_input_image_row(
  const JSAMPROW input_image_row_sample,
  const JSAMPROW output_image_row_sample,
  const size_t original_width
) {
  for (size_t j = 0; j < original_width; j++) {
    const size_t pixel_index = j * INPUT_IMAGE_COMPONENTS_NUMBER;
    const unsigned char red = input_image_row_sample[pixel_index + 0];
    const unsigned char green = input_image_row_sample[pixel_index + 1];
    const unsigned char blue = input_image_row_sample[pixel_index + 2];
    const unsigned char gray = calculate_gray(red, green, blue);
    output_image_row_sample[j] = gray;
  }
}

void set_decompressor_options(
  struct jpeg_decompress_struct *decompressor,
  struct jpeg_error_mgr *error_manager,
  FILE *input_file
) {
  decompressor->err = jpeg_std_error(error_manager);
  jpeg_create_decompress(decompressor);
  jpeg_stdio_src(decompressor, input_file);
  (void)jpeg_read_header(decompressor, TRUE);
  (void)jpeg_start_decompress(decompressor);
}

void set_compressor_options(
  struct jpeg_compress_struct *compressor,
  const struct jpeg_decompress_struct *decompressor,
  struct jpeg_error_mgr *error_manager,
  FILE *output_file
) {
  compressor->err = jpeg_std_error(error_manager);
  jpeg_create_compress(compressor);
  jpeg_stdio_dest(compressor, output_file);
  compressor->in_color_space = JCS_GRAYSCALE;
  compressor->input_components = OUTPUT_IMAGE_COMPONENTS_NUMBER;
  jpeg_set_defaults(compressor);
  compressor->image_width = decompressor->output_width;
  compressor->image_height = decompressor->image_height;
  compressor->density_unit = decompressor->density_unit;
  compressor->X_density = decompressor->X_density;
  compressor->Y_density = decompressor->Y_density;
  jpeg_start_compress(compressor, TRUE);
}

const size_t calculate_input_image_row_length(
  const struct jpeg_decompress_struct *decompressor
) {
  return decompressor->output_width * decompressor->num_components;
}

struct transform_row_params {
  struct jpeg_decompress_struct *decompressor;
  struct jpeg_compress_struct *compressor;
  JSAMPARRAY scanned_lines;
  JSAMPARRAY output_lines;
  JDIMENSION num_rows;
  unsigned long thread_id;
};

void *transform_rows(void *param) {
  struct transform_row_params *params = (struct transform_row_params *)param;
  for (size_t i = 0; i < params->num_rows; i++) {
    transform_input_image_row(
      params->scanned_lines[i],
      params->output_lines[i],
      params->decompressor->output_width
    );
  }

  return NULL;
}

int transform_image(const char *input_filename, const char *output_filename) {
  FILE *input_file = fopen(input_filename, "rb");
  if (!input_file) {
    (void)fprintf(
      stderr,
      "🛑🙁 error opening jpeg file '%s': %s 🙁🛑\n",
      input_filename,
      strerror(errno)
    );
    return errno;
  }

  FILE *output_file = fopen(output_filename, "wb");
  if (!output_file) {
    (void)fprintf(
      stderr,
      "🛑🙁 error opening output jpeg file '%s': %s 🙁🛑\n",
      output_filename,
      strerror(errno)
    );
    return errno;
  }

  struct jpeg_error_mgr error_manager;

  struct jpeg_decompress_struct decompressor;
  set_decompressor_options(&decompressor, &error_manager, input_file);

  if (decompressor.image_height < NUM_THREADS) {
    (void)fprintf(
      stderr,
      "🛑🤔 how is that possible to distribute processing %u rows on %u threads? 🤔🛑\n",
      decompressor.image_height,
      NUM_THREADS
    );
    return 1;

  }

  struct jpeg_compress_struct compressor;
  set_compressor_options(&compressor, &decompressor, &error_manager, output_file);

  const size_t input_image_row_length = calculate_input_image_row_length(&decompressor);
  const unsigned int output_image_row_length = compressor.image_width;

  unsigned char *all_buffer = malloc(
    decompressor.image_height * input_image_row_length +
    compressor.image_height * output_image_row_length +
    NUM_THREADS * sizeof(struct transform_row_params)
  );
  if (all_buffer == NULL) {
    (void)fprintf(stderr, "failed to allocate enough memory.\n");
    exit(-1);
  }

  unsigned char *input_buffer = &all_buffer[0];

  JSAMPROW scan_rows_buffer[decompressor.image_height];
  for (size_t i = 0; i < decompressor.image_height; i++) {
    scan_rows_buffer[i] = &input_buffer[i * input_image_row_length];
  }

  while (decompressor.output_scanline < decompressor.output_height) {
    (void)jpeg_read_scanlines(
      &decompressor,
      &scan_rows_buffer[decompressor.output_scanline],
      decompressor.output_height - decompressor.output_scanline
    );
  }

  unsigned char *output_buffer = &all_buffer[decompressor.image_height * input_image_row_length];
  JSAMPROW output_rows_buffer[compressor.image_height];
  for (size_t i = 0; i < compressor.image_height; i++) {
    output_rows_buffer[i] = &output_buffer[i * output_image_row_length];
  }

  pthread_t thread_ids[NUM_THREADS];
  struct transform_row_params *thread_params_refs[NUM_THREADS];

  const unsigned int quotient = decompressor.image_height / NUM_THREADS;
  const unsigned int remainder = decompressor.image_height % NUM_THREADS;

  struct timespec start, end;
  timespec_get(&start, TIME_UTC);

  for (size_t i = 0; i < NUM_THREADS; i++) {
    const unsigned long int worker_quotient = (i < remainder) ? (quotient + 1) : (quotient);
    struct transform_row_params *params = (struct transform_row_params *)&all_buffer[
      decompressor.image_height * input_image_row_length +
      compressor.image_height * output_image_row_length +
      i * sizeof(struct transform_row_params)
    ];
    params->decompressor = &decompressor;
    params->compressor = &compressor;
    params->scanned_lines = &scan_rows_buffer[i * worker_quotient];
    params->output_lines = &output_rows_buffer[i * worker_quotient];
    params->num_rows = worker_quotient;
    params->thread_id = i;
    thread_params_refs[i] = params;
    (void)pthread_create(&thread_ids[i], NULL, transform_rows, thread_params_refs[i]);
  }

  for (size_t i = 0; i < NUM_THREADS; i++) {
    (void)pthread_join(thread_ids[i], NULL);
  }

  timespec_get(&end, TIME_UTC);
  unsigned long int time_in_nano_seconds = (end.tv_sec - start.tv_sec) * 1e9 + (end.tv_nsec - start.tv_nsec);
  printf("%lu\n", time_in_nano_seconds);

  for (size_t i = 0; i < compressor.image_height; i++) {
    (void)jpeg_write_scanlines(&compressor, &output_rows_buffer[i], 1);
  }

  (void)jpeg_finish_decompress(&decompressor);
  jpeg_finish_compress(&compressor);
  jpeg_destroy_decompress(&decompressor);
  jpeg_destroy_compress(&compressor);
  free(all_buffer);
  (void)fclose(input_file);
  (void)fclose(output_file);

  return 0;
}

int main() {
  return transform_image(INPUT_IMAGE_FILENAME, OUTPUT_IMAGE_FILENAME);
}
