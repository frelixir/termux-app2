#include <jni.h>
#include <android/log.h>
#include <sys/socket.h>
#include <linux/in.h>
#include <string.h>
#include <sys/endian.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>
#include <list.h>
#include <sys/un.h>
#include <android/looper.h>
#include <sys/mman.h>
#include <asm-generic/ioctls.h>
#include <poll.h>
#include <unistd.h>
#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include "waylandRenderServer.h"
#include "buffer.h"
#include "lorie.h"

#define MAX_WAITING_CONNECT_CLIENTS 5
#define SOCKET_PATH "/data/data/com.termux/files/home/.wayland/unix_socket"
#define log(prio, ...) __android_log_print(ANDROID_LOG_ ## prio, "LorieNative", __VA_ARGS__)
#define min(a, b) (((a) < (b)) ? (a) : (b))

extern int conn_fd;
static struct xorg_list registeredWaylandBuffers;

extern struct {
    jclass self;
    jmethodID getInstance, clientConnectedStateChanged, resetIme, onRenderConnected;
} MainActivity;

extern struct {
    jclass self;
    jmethodID forName;
    jmethodID decode;
} Charset;

extern struct {
    jclass self;
    jmethodID toString;
} CharBuffer;

extern JNIEnv *guienv;
extern jobject globalThiz;

static int textureId=0;

static void waylandSendSharedServerState(int memfd) {
    if (conn_fd != -1) {
        lorieEvent e = {.type = EVENT_SHARED_SERVER_STATE};
        write(conn_fd, &e, sizeof(e));
        ancil_send_fd(conn_fd, memfd);
    }
}

static void waylandRegisterBuffer(LorieBuffer *buffer) {
    unsigned long id = LorieBuffer_description(buffer)->id;
    textureId=id;
    if (conn_fd == -1 || LorieBufferList_findById(&registeredWaylandBuffers, id))
        return; // Already registered

    if (conn_fd != -1 && buffer) {
        lorieEvent e = {.type = EVENT_ADD_BUFFER};
        write(conn_fd, &e, sizeof(e));
        LorieBuffer_sendHandleToUnixSocket(buffer, conn_fd);
        rendererAddBuffer(buffer);
//        LorieBuffer_addToList(buffer, &registeredWaylandBuffers);
        const LorieBuffer_Desc *desc = LorieBuffer_description(buffer);
        log(INFO, "Sent shared buffer width %d stride %d height %d format %d type %d id %llu",
            desc->width, desc->stride, desc->height, desc->format, desc->type, desc->id);
    }
}

static void waylandUnregisterBuffer(LorieBuffer *buffer) {
    unsigned long id;
    if (!buffer || (!LorieBufferList_findById(&registeredWaylandBuffers,
                                              (id = LorieBuffer_description(buffer)->id))))
        return;  // Not exist or not registered so no need to unregister

    if (conn_fd != -1 && buffer) {
        LorieBuffer_removeFromList(buffer);
        rendererSetSharedState(NULL);
        rendererRemoveAllBuffers();
        log(DEBUG, "disconnected");
    }
}

static int process(int fd) {
    if (fd == -1) {
        return 0;
    }

    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLIN;
    while (1) {
        int ret = poll(&pfd, 1, -1);  // 阻塞等待
        if (ret < 0) {
            perror("poll");
            return -1;
        }

        if (pfd.revents & POLLIN) {

            while (1) { // 循环读取，直到没数据
                lorieEvent e = {0};
                ssize_t nread = read(fd, &e, sizeof(e));
                if (nread == sizeof(e)) {
                    // 处理事件
                    switch (e.type) {
                        case EVENT_APPLY_SERVER_STATE: {
                            struct lorie_shared_server_state *state = NULL;
                            int stateFd = LorieBuffer_createRegion("wayland", sizeof(*state));
                            if (stateFd == -1) {
                                dprintf(2, "FATAL: Failed to allocate server state.\n");
                                _exit(1);
                            }

                            state = mmap(NULL, sizeof(*state), PROT_READ | PROT_WRITE, MAP_SHARED,
                                         stateFd, 0);
                            if (state == MAP_FAILED) {
                                dprintf(2, "FATAL: Failed to map server state.\n");
                                _exit(1);
                            }

                            // Initialize cross-process synchronization primitives
                            pthread_mutexattr_t mutex_attr;
                            pthread_condattr_t cond_attr;

                            pthread_mutexattr_init(&mutex_attr);
                            pthread_mutexattr_setpshared(&mutex_attr, PTHREAD_PROCESS_SHARED);
                            pthread_mutexattr_settype(&mutex_attr, PTHREAD_MUTEX_RECURSIVE);
                            pthread_mutex_init(&state->lock, &mutex_attr);
                            pthread_mutex_init(&state->cursor.lock, &mutex_attr);

                            pthread_condattr_init(&cond_attr);
                            pthread_condattr_setpshared(&cond_attr, PTHREAD_PROCESS_SHARED);
                            pthread_cond_init(&state->cond, &cond_attr);

                            pthread_mutexattr_destroy(&mutex_attr);
                            pthread_condattr_destroy(&cond_attr);

                            log(DEBUG, "lorie_shared_server_state:%p", state);
                            state->rootWindowTextureID=textureId;
                            waylandSendSharedServerState(stateFd);
                            rendererSetSharedState(state);
                            break;
                        }
                        case EVENT_APPLY_BUFFER: {
                            lorieEvent e2 = {0};
                            read(fd,&e2, sizeof (e2));
                            LorieBuffer *buffer = LorieBuffer_allocate(e2.screenSize.width,
                                                                       e2.screenSize.height,
                                                                       e2.screenSize.format,
                                                                       e2.screenSize.type);
                            waylandRegisterBuffer(buffer);
                            break;
                        }
                        case EVENT_CLIENT_VERIFY_SUCCEED:{
                            JNIEnv *env = guienv;
                            jobject thiz = globalThiz;
                            jobject instance = (*env)->CallStaticObjectMethod(env, MainActivity.self, MainActivity.getInstance);
                            if (instance)
                                (*env)->CallVoidMethod(env, instance, MainActivity.clientConnectedStateChanged);
                            break;
                        }
                        case EVENT_DESTROY_BUFFER: {
                            waylandUnregisterBuffer(LorieBufferList_findById(&registeredWaylandBuffers,
                                                                             e.removeBuffer.id));
                            return 0;
                        }
                    }
                } else if (nread == 0) {
                    // 对端关闭连接
                    return 0;
                } else if (nread < 0) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        // 没有更多数据，退出读取循环，继续poll等待
                        break;
                    }
                    perror("read");
                    return -1;
                }else{
//                    int cnt = read(fd,&e+nread,sizeof (e)-nread);
//                    nread+=cnt;
                }
            }
        }
    }
    return 0;
}

static void startRenderServer(JavaVM *vm) {
    conn_fd = -1;
    int server_fd, client_fd, count;
    struct sockaddr_un address;
    uint8_t buffer[512] = {0};

    // 创建socket
    server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd < 0) {
        log(ERROR, "Socket creation failed: %s", strerror(errno));
        return;
    }

    // 绑定socket文件路径，先unlink避免路径已存在
    unlink(SOCKET_PATH);
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    strncpy(address.sun_path, SOCKET_PATH, sizeof(address.sun_path) - 1);

    if (bind(server_fd, (struct sockaddr *) &address, sizeof(address)) < 0) {
        log(ERROR, "Socket bind failed: %s", strerror(errno));
        close(server_fd);
        return;
    }

    // 监听连接
    if (listen(server_fd, MAX_WAITING_CONNECT_CLIENTS) < 0) {
        log(ERROR, "Socket listen failed: %s", strerror(errno));
        close(server_fd);
        unlink(SOCKET_PATH);
        return;
    }

    log(DEBUG, "Unix domain socket server listening at %s", SOCKET_PATH);

    while (1) {
        client_fd = accept(server_fd, NULL, NULL);
        if (client_fd < 0) {
            log(ERROR, "Socket accept failed: %s", strerror(errno));
            continue;
        }

        count = read(client_fd, buffer, sizeof(buffer));
        if (count > 0) {
            if (!memcmp(buffer, MAGIC, count < (int) sizeof(MAGIC) ? count : (int) sizeof(MAGIC))) {
                log(DEBUG, "New client connection!");
                lorieEvent e = {.type = EVENT_SERVER_VERIFY_SUCCEED};
                write(client_fd, &e, sizeof(e));
                conn_fd = client_fd;
                (*vm)->AttachCurrentThread(vm, &guienv, NULL);
                process(client_fd);
            } else {
                close(client_fd);
                log(ERROR, "Invalid client connection!");
            }
        }
    }

    close(server_fd);
    unlink(SOCKET_PATH);
}

void waylandRenderInit(JavaVM *vm) {
    pthread_t t;
    xorg_list_init(&registeredWaylandBuffers);
    JNIEnv *env;
    (*vm)->AttachCurrentThread(vm, &env, NULL);
    pthread_create(&t, NULL, (void *(*)(void *)) startRenderServer, vm);
}
