#include <stdio.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dmp.h>

#define DMP_MAX_FILE_SIZE 100000000

#define DMP_GFS_NO_FILE 1
#define DMP_GFS_FILE_TOO_LARGE 2
#define DMP_GFS_FILENAME_TOO_LONG 3
#define DMP_GFS_MMAP_FAILED 4
#define DMP_GFS_STAT_FAILED 5

typedef struct {
  off_t len;
  int fd;
  unsigned char *m;
} diff_file;

void cleanup (diff_file *df)
{
  if (df->fd > 0) {
    df->fd = 0;
    close(df->fd);
  }
}

int get_file_size(diff_file *df, char *name)
{
  off_t len;
  struct stat st;
  int i;

  if (strnlen(name, FILENAME_MAX) == FILENAME_MAX) {
    return DMP_GFS_FILENAME_TOO_LONG;
  }
  df->fd = open(name, O_RDONLY);
  if (df->fd < 0)
    return DMP_GFS_NO_FILE;
  if (fstat(df->fd, &st) < 0)
    return DMP_GFS_STAT_FAILED;
  df->len = st.st_size; // lseek(df->fd, (off_t)0, SEEK_END);
  if (df->len > DMP_MAX_FILE_SIZE) {
    return DMP_GFS_FILE_TOO_LARGE;
  };
  printf("len:%d\n", df->len);
  df->m = mmap (0, (size_t)df->len, PROT_READ, MAP_PRIVATE, df->fd, 0);
  if (df->m == MAP_FAILED) {
    printf("mmap failed: %s\n", "explain_mmap (0, (size_t)df->len, PROT_READ, 0, df->fd, 0)");
    return DMP_GFS_MMAP_FAILED;
  }
  return 0;
}

static int cb(void *cb_ref, dmp_operation_t op, const void *data, uint32_t len)
{
  int i;
  //printf ("cb\n");

  switch(op) {
    case 0:
      break;

    case 1:
      //fwrite("\e[1m", 4, 1, stdout);
      fwrite("\e[32m", 5, 1, stdout);
      break;

    case -1:
      fwrite("\e[9;31m", 7, 1, stdout);
      break;
  }
  fwrite(data, len, 1, stdout);
  if (op)
      fwrite("\e[0m", 4, 1, stdout);
  return 0;
}

int main(int argc, char *argv[])
{
  diff_file f1, f2;
  int r1, r2;
  dmp_diff *diff;
  dmp_options opts;
  /*
   * Sanity checking
   */
  if (argc != 3) {
    printf("Usage %s file1 file2\n", argv[0]);
    exit(64);
  }
  if ((r1 = get_file_size(&f1, argv[1])) || (r2 = get_file_size(&f2, argv[2]))) {
    printf("r1=%d r2=%d!\n", r1, r2);
    exit(66);
  }
  dmp_options_init(&opts);
  opts.timeout = 5.0F;
  dmp_diff_new(&diff, &opts, f1.m, f1.len, f2.m, f2.len);
  //dmp_diff_print_raw(stdout, diff);
  dmp_diff_foreach(diff, cb, 0);
  dmp_diff_free(diff);
  cleanup(&f1);
  cleanup(&f2);
}

