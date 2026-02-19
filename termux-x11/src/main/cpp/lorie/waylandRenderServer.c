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
int event_fd;
static struct lorie_shared_server_state *shared_state = NULL;
static int shared_state_fd = -1;
static volatile int client_pid = -1;
static volatile int connection_alive = 1;

extern struct {
    jclass self;
    jmethodID getInstance, clientConnectedStateChanged, resetIme, onRenderConnectionChanged;
} MainActivity;

extern JNIEnv *guienv;
extern jobject globalThiz;

static int textureId = 0;

static void waylandSendSharedServerState(int memfd) {
    if (event_fd != -1) {
        lorieEvent e = {.type = EVENT_SHARED_SERVER_STATE};
        write(event_fd, &e, sizeof(e));
        ancil_send_fd(event_fd, memfd);
    }
}

static void waylandRegisterBuffer(LorieBuffer *buffer) {
    unsigned long id = LorieBuffer_description(buffer)->id;
    textureId = id;
    if (event_fd == -1)
        return; // Already registered

    if (event_fd != -1 && buffer) {
        lorieEvent e = {.type = EVENT_ADD_BUFFER};
        write(event_fd, &e, sizeof(e));
        LorieBuffer_sendHandleToUnixSocket(buffer, event_fd);
        rendererAddBuffer(buffer);
        const LorieBuffer_Desc *desc = LorieBuffer_description(buffer);
        log(INFO, "Sent shared buffer width %d stride %d height %d format %d type %d id %llu",
            desc->width, desc->stride, desc->height, desc->format, desc->type, desc->id);
    }
}
static void cleanupSharedResources(void);
static void connectionCheckHandler(int signum) {
    if (client_pid <= 0 || !connection_alive) {
        return;
    }

    if (kill(client_pid, 0) == -1 && errno == ESRCH) {
        connection_alive = 0;
    }
}

static void cleanupSharedResources(void) {
    struct itimerval timer = {0};
    setitimer(ITIMER_REAL, &timer, NULL);

    connection_alive = 0;
    client_pid = -1;

    rendererSetSharedState(NULL);

    if (shared_state_fd != -1) {
        close(shared_state_fd);
        shared_state_fd = -1;
    }
    rendererRemoveAllBuffers();

    if (event_fd != -1) {
        close(event_fd);
        event_fd = -1;
    }

    if (conn_fd != -1) {
        close(conn_fd);
        conn_fd = -1;
    }

    jobject instance = (*guienv)->CallStaticObjectMethod(guienv,
                                                      MainActivity.self,
                                                      MainActivity.getInstance);
    if (instance)
        (*guienv)->CallVoidMethod(guienv, instance,
                               MainActivity.onRenderConnectionChanged);
}

static int process(int fd) {
    if (fd == -1) {
        return 0;
    }

    connection_alive = 1;

    struct sigaction sa;
    sa.sa_handler = connectionCheckHandler;
    sa.sa_flags = SA_RESTART;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGALRM, &sa, NULL);

    struct itimerval timer;
    timer.it_value.tv_sec = 1;
    timer.it_value.tv_usec = 0;
    timer.it_interval.tv_sec = 1;
    timer.it_interval.tv_usec = 0;
    setitimer(ITIMER_REAL, &timer, NULL);

    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLIN;
    while (connection_alive) {
        int ret = poll(&pfd, 1, 1000);
        if (ret < 0) {
            if (errno == EINTR) continue;
            perror("poll");
            return -1;
        }

        if (!connection_alive) {
            cleanupSharedResources();
            log(DEBUG,"client killed");
            return 0;
        }

        if (ret == 0) continue;

        if (pfd.revents & POLLIN) {

            while (1) {
                lorieEvent e = {0};
                ssize_t nread = read(fd, &e, sizeof(e));
                if (nread == sizeof(e)) {
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
                            state->rootWindowTextureID = textureId;
                            waylandSendSharedServerState(stateFd);
                            rendererSetSharedState(state);

                            shared_state = state;
                            shared_state_fd = stateFd;
                            break;
                        }
                        case EVENT_APPLY_BUFFER: {
                            lorieEvent e2 = {0};
                            read(fd, &e2, sizeof(e2));
                            LorieBuffer *buffer = LorieBuffer_allocate(e2.screenSize.width,
                                                                       e2.screenSize.height,
                                                                       e2.screenSize.format,
                                                                       e2.screenSize.type);
                            waylandRegisterBuffer(buffer);
                            break;
                        }
                        case EVENT_CLIENT_VERIFY_SUCCEED: {
                            lorieEvent e1 = {0};
                            read(fd, &e1, sizeof(e1));
                            if (e1.client.pid > 0) {
                                client_pid = e1.client.pid;
                            }
                            JNIEnv *env = guienv;
                            jobject instance = (*env)->CallStaticObjectMethod(env,
                                                                              MainActivity.self,
                                                                              MainActivity.getInstance);
                            if (instance)
                                (*env)->CallVoidMethod(env, instance,
                                                       MainActivity.onRenderConnectionChanged);
                            break;
                        }
                        case EVENT_STOP_RENDER: {
                            cleanupSharedResources();
                            return 0;
                        }
                        default:
                            log(DEBUG, "Unknown event type: %d", e.type);
                            break;
                    }
                } else if (nread == 0) {
                    return 0;
                } else if (nread < 0) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        break;
                    }
                    perror("read");
                    return -1;
                }
            }
        }
    }
    return 0;
}

static void startRenderServer(JavaVM *vm) {
    event_fd = -1;
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

                if (event_fd != -1 && event_fd != client_fd) {
                    log(DEBUG, "Disconnecting existing client");
                    lorieEvent stop_event = {.type = EVENT_STOP_RENDER};
                    write(event_fd, &stop_event, sizeof(stop_event));
                    cleanupSharedResources();
                    sleep(1);
                }

                lorieEvent e = {.type = EVENT_SERVER_VERIFY_SUCCEED};
                write(client_fd, &e, sizeof(e));
                event_fd = client_fd;
                conn_fd = 0;
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
    JNIEnv *env;
    (*vm)->AttachCurrentThread(vm, &env, NULL);
    pthread_create(&t, NULL, (void *(*)(void *)) startRenderServer, vm);
}
