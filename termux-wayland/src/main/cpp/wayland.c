#include <jni.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/mman.h>
#include <pthread.h>
#include "buffer.h"

static int dataSocket = -1;
static int connect_retry = 0;
#define MAX_RETRY_TIMES 5

#define SIGTERM_MSG "\nKILL | SIGTERM received.\n"
#define SOCKET_NAME     "shard_texture_socket"

static int eventFd, stateFd;
static LorieBuffer *lorieBuffer;
static struct lorie_shared_server_state* state;

JNIEXPORT jstring JNICALL
Java_com_termux_wayland_NativeLib_stringFromJNI(
        JNIEnv* env,
        jobject  this ) {
    return "hello";
}
void OsVendorInit(void) {
    pthread_mutexattr_t mutex_attr;
    pthread_condattr_t cond_attr;

    if (stateFd != -1) // already initialized
        return;

    if (-1 == (stateFd = LorieBuffer_createRegion("wayland", sizeof(*state)))) {
        dprintf(2, "FATAL: Failed to allocate server state.\n");
        _exit(1);
    }

    if (!(state = mmap(NULL, sizeof(*state), PROT_READ|PROT_WRITE, MAP_SHARED, stateFd, 0))) {
        dprintf(2, "FATAL: Failed to map server state.\n");
        _exit(1);
    }

    pthread_mutexattr_init(&mutex_attr);
    pthread_mutexattr_setpshared(&mutex_attr, PTHREAD_PROCESS_SHARED);
    pthread_mutexattr_settype(&mutex_attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(state->lock, &mutex_attr);
    pthread_mutex_init(state->cursor.lock, &mutex_attr);

    pthread_condattr_init(&cond_attr);
    pthread_condattr_setpshared(&cond_attr, PTHREAD_PROCESS_SHARED);
    pthread_cond_init(state->cond, &cond_attr);
}
static void sigTermHandler(int signum, siginfo_t *info, void *ptr) {
    write(STDERR_FILENO, SIGTERM_MSG, sizeof(SIGTERM_MSG));
}
static void catchSigTerm() {
    static struct sigaction sigact;

    memset(&sigact, 0, sizeof(sigact));
    sigact.sa_sigaction = sigTermHandler;
    sigact.sa_flags = SA_SIGINFO;
    sigaction(SIGINT, &sigact, NULL);
}
static int connectToRender(){
    char socketName[108];
    struct sockaddr_un serverAddr;

    dataSocket = socket(AF_UNIX, SOCK_STREAM, 0);
    if (dataSocket < 0) {
        printf("socket: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    memcpy(&socketName[0], "\0", 1);
    strcpy(&socketName[1], SOCKET_NAME);

    memset(&serverAddr, 0, sizeof(struct sockaddr_un));
    serverAddr.sun_family = AF_UNIX;
    strncpy(serverAddr.sun_path, socketName, sizeof(serverAddr.sun_path) - 1);

    // connect
    while (connect_retry < MAX_RETRY_TIMES) {
        int ret = connect(dataSocket, (const struct sockaddr *)&serverAddr,
                          sizeof(struct sockaddr_un));
        if (ret < 0) {
            printf("connect: %s, retry %d\n", strerror(errno), connect_retry + 1);
            if (connect_retry >= MAX_RETRY_TIMES - 1) {
                printf("connect: %s, failed after %d times\n", strerror(errno), connect_retry + 1);
                exit(EXIT_FAILURE);
            }
            connect_retry++;
            sleep(5);
        } else {
            break;
        }
    }
    catchSigTerm();
    printf("%s\n", "render connect succeed.");

    return 0;
}
