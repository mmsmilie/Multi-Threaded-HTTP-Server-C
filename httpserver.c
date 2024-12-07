#include <unistd.h>
#include <linux/limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <limits.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <regex.h>
#include <signal.h>
#include <bits/pthreadtypes.h>
#include <pthread.h>
#include <getopt.h>

#include "asgn2_helper_funcs.h"
#include "rwlock.h"
#include "queue.h"
#include "debug.h"
#include "protocol.h"

#define BUFFER_SIZE PATH_MAX
#define DABUG       false

int main_loop = 1;

void signalHandler(int sig) {
    if (sig == SIGINT) {
        main_loop = 0;
    }
}

// Method to put int fd into void*
typedef struct connection {
    int fd;
} connection_t;

// Object to pair up Queue and Regex
typedef struct object {
    queue_t *queue;
    regex_t *regex;
    regex_t *content;
    regex_t *request;
    regex_t *header;
    rwlock_t *rwlock;
} object_t;

object_t *create_object(queue_t *q, regex_t *r, rwlock_t *rk, regex_t *c, regex_t *rt, regex_t *h) {
    object_t *obj = (object_t *) malloc(sizeof(object_t));
    obj->queue = q;
    obj->regex = r;
    obj->rwlock = rk;
    obj->content = c;
    obj->request = rt;
    obj->header = h;
    return obj;
}

// Helper Functions
void fixFilePath(char *f) {
    char *f1 = strrchr(f, '/');
    if (f1 != NULL) {
        f1++; // Move the pointer to the character after '/'
        memmove(f, f1, strlen(f1) + 1);
        // Copy the file name to the beginning of the string (including the null terminator)
    }
}

bool isDirectory(const char *location) {
    struct stat path_stat;
    if (stat(location, &path_stat) != 0) {
        // Error occurred while getting file status
        return false;
        // You can handle the error based on your requirements
    }
    return S_ISDIR(path_stat.st_mode);
}

void status_code(int connfd, int status_code) {
    char *status;
    switch (status_code) {
    case 200:
        //fprintf(stdout,"200\n");
        status = "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nOK\n";
        write_n_bytes(connfd, status, strlen(status));
        break;
    case 201:
        //fprintf(stdout,"201\n");
        status = "HTTP/1.1 201 Created\r\nContent-Length: 8\r\n\r\nCreated\n";
        write_n_bytes(connfd, status, strlen(status));
        break;
    case 400:
        //fprintf(stdout,"400\n");
        status = "HTTP/1.1 400 Bad Request\r\nContent-Length: 12\r\n\r\nBad Request\n";
        write(connfd, status, strlen(status));
        break;
    case 403:
        //fprintf(stdout,"403\n");
        status = "HTTP/1.1 403 Forbidden\r\nContent-Length: 10\r\n\r\nForbidden\n";
        write_n_bytes(connfd, status, strlen(status));
        break;
    case 404:
        //fprintf(stdout,"404\n");
        status = "HTTP/1.1 404 Not Found\r\nContent-Length: 10\r\n\r\nNot Found\n";
        write_n_bytes(connfd, status, strlen(status));
        break;
    case 500:
        //fprintf(stdout,"500\n");
        status = "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 22\r\n\r\nInternal Server "
                 "Error\n";
        write_n_bytes(connfd, status, strlen(status));
        break;
    case 501:
        //fprintf(stdout,"501\n");
        status = "HTTP/1.1 501 Not Implemented\r\nContent-Length: 16\r\n\r\nNot Implemented\n";
        write_n_bytes(connfd, status, strlen(status));
        break;
    case 505:
        //fprintf(stdout,"505\n");
        status = "HTTP/1.1 505 Version Not Supported\r\nContent-Length: 22\r\n\r\nVersion Not "
                 "Supported\n";
        write_n_bytes(connfd, status, strlen(status));
        break;
    }
}

void audit_Log(int status_code, bool isGet, int request_id, char *location) {
    if (isGet) {
        fprintf(stderr, "GET,/%s,%d,%d\n", location, status_code, request_id);
        if (DABUG) {
            FILE *f = fopen("audit.txt", "a");
            fprintf(f, "GET,/%s,%d,%d\n", location, status_code, request_id);
            fclose(f);
        }
    } else {
        fprintf(stderr, "PUT,/%s,%d,%d\n", location, status_code, request_id);
        if (DABUG) {
            FILE *fp = fopen("audit.txt", "a");
            fprintf(fp, "PUT,/%s,%d,%d\n", location, status_code, request_id);
            fclose(fp);
        }
    }
}

void Get(int connfd, char *location, int request_id, rwlock_t *rwlock) {
    if (DABUG) {
        FILE *f = fopen("log.txt", "a");
        fprintf(f, "Entering Get\n");
        fclose(f);
    }
    if (isDirectory(location)) {
        status_code(connfd, 403);
        audit_Log(403, true, request_id, location);
        return;
    }

    int result_r;
    int fd = open(location, O_RDONLY);
    if (fd == -1) {
        status_code(connfd, 404);
        audit_Log(404, true, request_id, location);
        //close(connfd);
        return;
    }
    if (DABUG) {
        FILE *f = fopen("log.txt", "a");
        fprintf(f, "Get: %s\n", location);
        fclose(f);
    }
    struct stat *st = malloc(sizeof(struct stat));
    stat(location, st);
    int content_length = st->st_size;

    if (DABUG) {
        FILE *f = fopen("log.txt", "a");
        fprintf(f, "Content Length: %d\n", content_length);
        fclose(f);
    }
    // make a function about response
    char content[BUFFER_SIZE];
    char *preface = "HTTP/1.1 200 OK\r\nContent-Length: ";
    char length[100];
    sprintf(length, "%d", content_length);
    char *postfix = "\r\n\r\n";
    reader_lock(rwlock);
    write(connfd, preface, strlen(preface));
    write(connfd, length, strlen(length));
    write(connfd, postfix, strlen(postfix));
    if (DABUG) {
        FILE *f = fopen("log.txt", "a");
        fprintf(f, "READING\n");
        fclose(f);
    }
    while ((result_r = read(fd, content, BUFFER_SIZE)) > 0) {
        write_n_bytes(connfd, content, result_r);
    }
    reader_unlock(rwlock);
    audit_Log(200, true, request_id, location);
    close(fd);
    free(st);
}

void Put(int cd, char *loc, int cl, char *bf, int size, int rd, rwlock_t *rk) {
    if (DABUG) {
        FILE *f = fopen("log.txt", "a");
        fprintf(f, "Entering Put\n");
        fclose(f);
    }

    int fd;
    int code;
    int result_r = 0;

    fd = open(loc, O_RDONLY);
    if (fd != -1) {
        code = 200;
        close(fd);
    } else {
        code = 201;
        close(fd);
    }
    fd = open(loc, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (DABUG) {
        FILE *f = fopen("log.txt", "a");
        fprintf(f, "WRITING\n");
        fclose(f);
    }
    if (size == 0) {
        writer_lock(rk);
        memset(bf, 0, BUFFER_SIZE);
        while ((result_r = read(cd, bf, BUFFER_SIZE)) > 0) {
            write_n_bytes(fd, bf, result_r);
        }
        writer_unlock(rk);
        status_code(cd, code);
        audit_Log(code, false, rd, loc);
        close(fd);
    } else {
        writer_lock(rk);
        int result_w = write(fd, bf, size);

        if (result_w < cl) {
            while ((result_r = read(cd, bf, BUFFER_SIZE)) > 0) {
                write(fd, bf, result_r);
            }
        }
        writer_unlock(rk);
        status_code(cd, code);
        audit_Log(code, false, rd, loc);
        close(fd);
    }
}

// Thread Function
void *worker(void *ob) {
    object_t *obj = (object_t *) ob;

    while (queue_terminated(obj->queue) == false) {
        connection_t *conn = (connection_t *) malloc(sizeof(connection_t));
        char buffer[BUFFER_SIZE];

        if (DABUG) {
            FILE *f = fopen("log.txt", "a");
            fprintf(f, "Worker: Waiting for connection\n");
            fclose(f);
        }

        queue_pop(obj->queue, (void *) &conn);

        if (DABUG) {
            FILE *f = fopen("log.txt", "a");
            fprintf(f, "Worker: Connection pulled\n");
            fclose(f);
        }

        // Will push thread NULL connections to queue
        // After initiating terminate.
        // If connection is NULL which is only by me
        // Then terminate
        if (conn == NULL) {

            if (DABUG) {
                FILE *f = fopen("log.txt", "a");
                fprintf(f, "Worker: Terminating\n");
                fclose(f);
            }

            free(conn);
            continue;
        }

        // Clear buffer
        memset(buffer, 0, BUFFER_SIZE);

        // Read from connection
        int result = read(conn->fd, buffer, BUFFER_SIZE);
        if (result == -1) {
            status_code(conn->fd, 500);
            free(conn);
            continue;
        }

        if (DABUG) {
            FILE *f = fopen("log.txt", "a");
            //fprintf(f,"read_until: %s\n", buffer);
            fclose(f);
        }

        // Beging Regex

        int nmatches = 10;
        regmatch_t matches[nmatches + 1];
        regmatch_t rmatches[nmatches + 1];
        regmatch_t cmatches[nmatches + 1];
        regmatch_t hmatches[nmatches + 1];

        if (DABUG) {
            // Print out command recieved to file
            FILE *f = fopen("log.txt", "a");
            fprintf(f, "Command: %s\n", buffer);
            fclose(f);
        }

        if (regexec(obj->regex, buffer, nmatches, matches, 0) == 0) {

            if (DABUG) {
                FILE *f = fopen("log.txt", "a");
                fprintf(f, "Passed Regex Pattern Matching\n");
                fclose(f);
            }

            char version[matches[3].rm_eo - matches[3].rm_so + 1];
            strncpy(version, &buffer[matches[3].rm_so], matches[3].rm_eo - matches[3].rm_so);
            version[matches[3].rm_eo - matches[3].rm_so] = '\0'; // Null-terminate the string
            if (DABUG) {
                FILE *f = fopen("log.txt", "a");
                fprintf(f, "Version: %s\n", version);
                fclose(f);
            }
            if (strcmp(version, "HTTP/1.1") != 0) {
                status_code(conn->fd, 505);
                free(conn);
                continue;
            }

            if (strncmp(&buffer[matches[1].rm_so], "GET", matches[1].rm_eo - matches[1].rm_so)
                == 0) {

                if (DABUG) {
                    FILE *f = fopen("log.txt", "a");
                    fprintf(f, "GET\n");
                    fclose(f);
                }

                char location[PATH_MAX];
                strncpy(location, &buffer[matches[2].rm_so], matches[2].rm_eo - matches[2].rm_so);
                location[matches[2].rm_eo - matches[2].rm_so] = '\0'; // Null-terminate the string
                fixFilePath(location);
                int request_id = 0;

                if (DABUG) {
                    FILE *f = fopen("log.txt", "a");
                    fprintf(f, "Checking for Request Id in \n");
                    fprintf(f, "%s\n", buffer);
                    fclose(f);
                }

                if (regexec(obj->header, buffer, nmatches, hmatches, 0) != 0) {
                    memset(buffer, 0, BUFFER_SIZE);
                    read(conn->fd, buffer, BUFFER_SIZE);

                    if (DABUG) {
                        FILE *f = fopen("log.txt", "a");
                        fprintf(f, "%s\n", buffer);
                        fclose(f);
                    }

                    if (regexec(obj->request, buffer, nmatches, rmatches, 0) == 0) {
                        request_id = atoi(&buffer[rmatches[1].rm_so]);
                    }
                }

                // Assuming this function sends the contents of the requested file
                Get(conn->fd, location, request_id, obj->rwlock);
                close(conn->fd);
                continue;
            } else if (strncmp(
                           &buffer[matches[1].rm_so], "PUT", matches[1].rm_eo - matches[1].rm_so)
                       == 0) {

                if (DABUG) {
                    FILE *f = fopen("log.txt", "a");
                    fprintf(f, "PUT\n");
                    fclose(f);
                }

                char location[1024]; // Assuming location size
                char message[5000];
                memset(location, 0, 1024);
                memset(message, 0, 5000);

                strncpy(location, &buffer[matches[2].rm_so], matches[2].rm_eo - matches[2].rm_so);
                location[matches[2].rm_eo - matches[2].rm_so] = '\0'; // Null-terminate the string
                fixFilePath(location);
                //fprintf(stdout,"location: %s\n", location);

                int request_id = 0;

                if (DABUG) {
                    FILE *f = fopen("log.txt", "a");
                    fprintf(f, "Checking for Request Id in \n");
                    fprintf(f, "%s\n", buffer);
                    fclose(f);
                }

                if (regexec(obj->request, buffer, nmatches, rmatches, 0) == 0) {
                    request_id = atoi(&buffer[rmatches[1].rm_so]);
                }

                int content_length = 0;

                if (DABUG) {
                    FILE *f = fopen("log.txt", "a");
                    fprintf(f, "Checking for Content Length in \n");
                    fclose(f);
                }
                if (regexec(obj->header, buffer, nmatches, hmatches, 0) != 0) {
                    memset(buffer, 0, BUFFER_SIZE);
                    read(conn->fd, buffer, BUFFER_SIZE);

                    if (DABUG) {
                        FILE *f = fopen("log.txt", "a");
                        fprintf(f, "%s\n", buffer);
                        fclose(f);
                    }

                    if (regexec(obj->request, buffer, nmatches, rmatches, 0) == 0) {
                        request_id = atoi(&buffer[rmatches[1].rm_so]);
                    }
                    if (regexec(obj->content, buffer, nmatches, cmatches, 0) != 0) {
                        status_code(conn->fd, 400);
                        audit_Log(400, false, request_id, location);
                        continue;
                    }
                }
                content_length = atoi(&buffer[cmatches[1].rm_so]);

                if (DABUG) {
                    FILE *f = fopen("log.txt", "a");
                    fprintf(f, "Content Length: %d\n", content_length);
                    fprintf(f, "Request-Id: %d\n", request_id);
                    fclose(f);
                }

                strncpy(message, &buffer[matches[5].rm_so], matches[5].rm_eo - matches[5].rm_so);
                if (DABUG) {
                    FILE *f = fopen("log.txt", "a");
                    fprintf(f, "Message: %s\n", message);
                    fclose(f);
                }

                int size = matches[5].rm_eo - matches[5].rm_so;
                Put(conn->fd, location, content_length, message, size, request_id, obj->rwlock);
                close(conn->fd);
                continue;
            } else {
                status_code(conn->fd, 501);
                //close(conn->fd);
                free(conn);
                continue;
            }

        } else {
            status_code(conn->fd, 400);
            if (DABUG) {
                FILE *f = fopen("log.txt", "a");
                fprintf(f, "Bad Request\n");
                fclose(f);
            }
            //close(conn->fd);
            free(conn);
            continue;
        }
        //close(conn->fd);
        //continue;
    }

    return NULL;
}

int main(int argc, char *argv[]) {

    signal(SIGINT, signalHandler);

    if (DABUG) {
        FILE *f = fopen("log.txt", "w");
        FILE *fp = fopen("audit.txt", "w");
        fclose(fp);
        fclose(f);
    }

    int opt, connfd;
    int threads = 4; // Default number of threads
    int port;
    int connections = 0;

    while ((opt = getopt(argc, argv, "t:")) != -1) {
        switch (opt) {
        case 't': threads = atoi(optarg); break;
        default: // '?' case for unrecognized options and missing option arguments
            fprintf(stderr, "Usage: %s [-t threads] <port>\n", argv[0]);
            exit(EXIT_FAILURE);
        }
    }

    if (optind < argc) {
        port
            = atoi(argv[optind]); // argv[optind] Assuming the port is the first non-option argument
    } else {
        fprintf(stderr, "Usage: %s [-t threads] <port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    if (port > 65535 || port < 0) {
        fprintf(stderr, "Invalid Port\n");
        exit(1);
    }

    // Regex Stuff
    int rt;
    regex_t rx; // Main Regex
    regex_t cl; // Content-Length
    regex_t rid; // Request ID
    regex_t hdr; // Header Fields
    char *q = "Content-Length: ([0-9]+)";
    char *r = "Request-Id: ([0-9]+)";

    rt = regcomp(&rx, REQUEST_LINE_REGEX, REG_EXTENDED); // Main Regex
    if (rt != 0) {
        fprintf(stderr, "Regex Error 0\n");
        exit(1);
    }

    rt = regcomp(&cl, q, REG_EXTENDED); // Content-Length
    if (rt != 0) {
        fprintf(stderr, "Regex Error 1\n");
        exit(1);
    }

    rt = regcomp(&rid, r, REG_EXTENDED); // Request ID
    if (rt != 0) {
        fprintf(stderr, "Regex Error 2\n");
        exit(1);
    }

    rt = regcomp(&hdr, HEADER_FIELD_REGEX, REG_EXTENDED); // Header Fields
    if (rt != 0) {
        fprintf(stderr, "Regex Error 3\n");
        exit(1);
    }

    // RWLOCK
    int nway = threads / 2;
    rwlock_t *rw = rwlock_new(N_WAY, nway);

    // Thread Safe Queue
    queue_t *queue = queue_new(100);
    object_t *obj = create_object(queue, &rx, rw, &cl, &rid, &hdr);

    pthread_t workers[threads];
    for (int i = 0; i < threads; i++) {
        pthread_create(&workers[i], NULL, worker, (void *) obj);
    }

    // Main Dispatcer Thread

    // Create a Listener_Socket
    Listener_Socket *sock = (Listener_Socket *) malloc(sizeof(Listener_Socket));
    int status = listener_init(sock, port);
    if (DABUG) {
        if (status == -1) {
            fprintf(stdout, "Listener Init Failed\n");
            exit(1);
        }
    }

    // Runtime Loop
    while (main_loop) {
        // Accept connection push to queue
        if ((connfd = listener_accept(sock)) == -1) {
            continue;
        }
        connection_t *conn = (connection_t *) malloc(sizeof(connection_t));
        conn->fd = connfd;
        // Push connection fd to queue
        if (DABUG) {
            FILE *fp = fopen("log.txt", "a");
            fprintf(fp, "Main Thread: Connection Accept\n");
            connections++;
            fclose(fp);
        }
        if (DABUG) {
            FILE *fp = fopen("log.txt", "a");
            fprintf(fp, "Main Thread: Connection %d Pushed\n", connections);
            fclose(fp);
        }
        (void) queue_push(queue, conn);
    }
    queue_terminate(queue);
    for (int i = 0; i < threads; i++) {
        queue_push(queue, NULL);
    }

    // Wait for all threads to finish
    for (int i = 0; i < threads; i++) {
        pthread_join(workers[i], NULL);
    }
    queue_delete(&queue);
    rwlock_delete(&rw);
    free(obj);
    free(sock);
    return 0;
}
