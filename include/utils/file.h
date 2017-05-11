#ifndef _CHARON_FILE_H_
#define _CHARON_FILE_H_

#include <sys/stat.h>

struct file {
    char* path;
    struct stat st;
};

#define file_size(f) ((f)->st.st_size)

typedef struct file file_t;

static int file_stat(file_t* f)
{
    return stat(f->path, &f->st);
}

#endif
