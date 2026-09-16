/* Hardware-independent failure injection for the actual decoder lifecycle.
 * Only this translation unit substitutes device creation. Production code has
 * no test hooks; the other codec functions come from the normal plugin. */
#include <dirent.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <sys/eventfd.h>

#define eventfd test_eventfd
#define fcntl test_fcntl
#define find_v4l2_decoder test_find_decoder
#define createCodec test_create_codec
#define pthread_mutex_destroy test_mutex_destroy
#define pthread_cond_destroy test_cond_destroy

int test_eventfd(unsigned int value, int flags);
int test_fcntl(int fd, int command, ...);
int test_mutex_destroy(pthread_mutex_t *mutex);
int test_cond_destroy(pthread_cond_t *cond);
#include "../al/vcodec/linlonv5v7/linlonv5v7_dec.c"

#undef eventfd
#undef fcntl
#undef find_v4l2_decoder
#undef createCodec
#undef pthread_mutex_destroy
#undef pthread_cond_destroy

enum Failure { FAIL_EVENTFD, FAIL_DEVICE, FAIL_GETFL, FAIL_SETFL, FAIL_CODEC, FAIL_CODEC_FD_ZERO };
static enum Failure failure;
static unsigned int mutex_destroys, cond_destroys, destroy_errors;
static int opened_device = -1;

int test_eventfd(unsigned int value, int flags) {
    if (failure == FAIL_EVENTFD) {
        errno = EMFILE;
        return -1;
    }
    return eventfd(value, flags);
}

int test_fcntl(int fd, int command, ...) {
    if ((failure == FAIL_GETFL && command == F_GETFL) ||
        (failure == FAIL_SETFL && command == F_SETFL)) {
        errno = EIO;
        return -1;
    }
    if (command == F_GETFL)
        return fcntl(fd, command);
    if (command == F_SETFL) {
        va_list args;
        va_start(args, command);
        int flags = va_arg(args, int);
        va_end(args);
        return fcntl(fd, command, flags);
    }
    fprintf(stderr, "unexpected decoder fcntl command: %d\n", command);
    exit(1);
}

S32 test_find_decoder(U8 *device_path, S32 coding_type) {
    if (failure == FAIL_DEVICE) {
        errno = ENODEV;
        return -1;
    }
    int fd = open("/dev/null", O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        perror("open /dev/null");
        exit(1);
    }
    if (failure == FAIL_CODEC_FD_ZERO && fd != 0) {
        if (dup2(fd, 0) < 0) {
            perror("dup2");
            exit(1);
        }
        close(fd);
        fd = 0;
    }
    opened_device = fd;
    return fd;
}

Codec *test_create_codec(S32 fd, S32 width, S32 height, S32 align, BOOL interlaced, enum v4l2_buf_type input_type,
    enum v4l2_buf_type output_type, U32 input_format, U32 output_format, U32 input_mem,
    U32 output_mem, U32 input_count, U32 output_count, BOOL block,
    MppFrameBufferType buffer_type) {
    return NULL;
}

int test_mutex_destroy(pthread_mutex_t *mutex) {
    int ret = pthread_mutex_destroy(mutex);
    ++mutex_destroys;
    destroy_errors += ret != 0;
    return ret;
}

int test_cond_destroy(pthread_cond_t *cond) {
    int ret = pthread_cond_destroy(cond);
    ++cond_destroys;
    destroy_errors += ret != 0;
    return ret;
}

static int fd_count(void) {
    DIR *dir = opendir("/proc/self/fd");
    if (!dir)
        return -1;
    int count = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL)
        if (entry->d_name[0] != '.')
            ++count;
    closedir(dir);
    return count;
}

int main(void) {
    /* Give fd-zero coverage a reversible stdin even when CTest closed it. */
    int saved_stdin = dup(0);
    if (saved_stdin < 0) {
        if (open("/dev/null", O_RDONLY) != 0)
            return 1;
        saved_stdin = dup(0);
    }
    if (saved_stdin < 0)
        return 1;
    int before = fd_count();
    if (before < 0)
        return 1;

    VdecChnAttr attr = {.eCodecType = MPP_STREAM_CODEC_MJPEG,
                        .eOutputPixelFormat = MPP_PIXEL_FORMAT_NV12,
                        .u32Width = 1920,
                        .u32Height = 1080,
                        .bEnableInputDmaBuf = MPP_TRUE};
    for (failure = FAIL_EVENTFD; failure <= FAIL_CODEC_FD_ZERO; ++failure) {
        for (int iteration = 0; iteration < 25; ++iteration) {
            mutex_destroys = cond_destroys = destroy_errors = 0;
            opened_device = -1;
            ALBaseContext *base = al_dec_create();
            if (!base)
                return 1;
            AlDecBufferRequirement req = {0};
            S32 expected = failure == FAIL_DEVICE ? MPP_OPEN_FAILED : MPP_INIT_FAILED;
            if (al_dec_init(base, &attr, &req) != expected)
                return 1;
            ALLinlonv5v7DecContext *context = (ALLinlonv5v7DecContext *)base;
            /* External resources must be rolled back before init returns,
             * even if the caller retains the context before destroying it. */
            if (context->nVideoFd != -1 || context->inputWakeFd != -1 || context->stCodec) {
                fprintf(stderr, "init failure %d retained device/eventfd/codec resources\n", failure);
                return 1;
            }
            if (opened_device >= 0 && (fcntl(opened_device, F_GETFD) != -1 || errno != EBADF)) {
                fprintf(stderr, "decoder fd %d leaked after init failure %d\n", opened_device, failure);
                return 1;
            }
            if (dup2(saved_stdin, 0) < 0 || fd_count() != before)
                return 1;
            /* init failure must leave the context's synchronization alive for
             * the normal caller-owned destroy, not destroy it twice. */
            if (mutex_destroys || cond_destroys || pthread_mutex_lock(&context->inputDmaLock) != 0)
                return 1;
            pthread_mutex_unlock(&context->inputDmaLock);
            al_dec_destory(base);
            if (mutex_destroys != 2 || cond_destroys != 1 || destroy_errors)
                return 1;
            if (fd_count() != before) {
                fprintf(stderr, "fd count changed after init failure %d\n", failure);
                return 1;
            }
        }
    }
    close(saved_stdin);
    puts("[PASS] 150 failed initializations: eventfd/device/fcntl/codec/fd-zero immediate cleanup");
    return 0;
}
