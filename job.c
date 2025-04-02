#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>  /* For sleep emulation if needed */

/* Detect environment and include appropriate headers */
#if defined(_WIN32) || defined(_WIN64)
    /* Windows environment */
    #include <windows.h>
    #include <winsock2.h>
    #define sleep(x) Sleep((x) * 1000)
    typedef HANDLE pipe_t;
    typedef DWORD pid_t;
    #define READ_END 0
    #define WRITE_END 1
#else
    /* UNIX-like environment */
    #include <unistd.h>
    #include <sys/types.h>
    /* Try to include select and wait headers, but don't error if missing */
    #if defined(__linux__) || defined(__APPLE__) || defined(__unix__)
        #include <sys/time.h>
        #include <sys/wait.h>
        #include <signal.h>
    #endif
    typedef int pipe_t;
    #define READ_END 0
    #define WRITE_END 1
#endif

#define MAX_WORKERS 100
#define MAX_WORKER_TYPES 5

/* Structure definitions */
typedef struct {
    int type;
    pipe_t pipe_to_worker[2];   // Main to worker
    pipe_t pipe_from_worker[2]; // Worker to main
    pid_t pid;
    int busy;               // 0 if free, 1 if busy
} Worker;

typedef struct {
    int type;
    int duration;
} Job;

/* Function prototypes */
#if defined(_WIN32) || defined(_WIN64)
    /* Windows-specific function prototypes */
    DWORD WINAPI worker_function(LPVOID param);
    int create_pipe(pipe_t pipes[2]);
    int write_pipe(pipe_t pipe, const void* buffer, int size);
    int read_pipe(pipe_t pipe, void* buffer, int size);
    void close_pipe(pipe_t pipe);
#else
    /* UNIX function prototypes */
    void worker_process(Worker *worker);
#endif

void create_workers(Worker *workers, int *worker_count, int worker_type, int worker_num);
void dispatch_jobs(Worker *workers, int worker_count);
int get_available_worker(Worker *workers, int worker_count, int job_type);

/* Simple emulation of select for Windows if needed */
#if defined(_WIN32) || defined(_WIN64)
int win_select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout) {
    /* Simple implementation for Windows - using WaitForMultipleObjects */
    /* This is a simplified version - a real implementation would be more complex */
    Sleep(timeout->tv_sec * 1000 + timeout->tv_usec / 1000);
    return 0; /* Simulate timeout */
}
#define select win_select
#endif

int main() {
    Worker workers[MAX_WORKERS];
    int worker_count = 0;
    int worker_type, worker_num;

    /* Read worker configuration (5 types) */
    for (int i = 0; i < MAX_WORKER_TYPES; i++) {
        if (scanf("%d %d", &worker_type, &worker_num) != 2) {
            fprintf(stderr, "Error reading worker configuration\n");
            exit(EXIT_FAILURE);
        }
        
        if (worker_type < 1 || worker_type > 5 || worker_num < 1) {
            fprintf(stderr, "Invalid worker configuration: type=%d, count=%d\n", 
                    worker_type, worker_num);
            exit(EXIT_FAILURE);
        }
        
        create_workers(workers, &worker_count, worker_type, worker_num);
    }

    /* Start job processing */
    dispatch_jobs(workers, worker_count);

    /* Wait for all worker processes to complete */
    for (int i = 0; i < worker_count; i++) {
        #if defined(_WIN32) || defined(_WIN64)
            /* Windows cleanup */
            close_pipe(workers[i].pipe_to_worker[WRITE_END]);
            close_pipe(workers[i].pipe_from_worker[READ_END]);
            WaitForSingleObject((HANDLE)workers[i].pid, INFINITE);
            CloseHandle((HANDLE)workers[i].pid);
        #else
            /* UNIX cleanup */
            close(workers[i].pipe_to_worker[WRITE_END]);
            close(workers[i].pipe_from_worker[READ_END]);
            waitpid(workers[i].pid, NULL, 0);
        #endif
    }

    return 0;
}

#if defined(_WIN32) || defined(_WIN64)
/* Windows implementations */

int create_pipe(pipe_t pipes[2]) {
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;

    if (!CreatePipe(&pipes[READ_END], &pipes[WRITE_END], &sa, 0)) {
        fprintf(stderr, "Pipe creation failed: %lu\n", GetLastError());
        return -1;
    }
    return 0;
}

int write_pipe(pipe_t pipe, const void* buffer, int size) {
    DWORD written;
    if (!WriteFile(pipe, buffer, size, &written, NULL)) {
        fprintf(stderr, "Pipe write failed: %lu\n", GetLastError());
        return -1;
    }
    return written;
}

int read_pipe(pipe_t pipe, void* buffer, int size) {
    DWORD read;
    if (!ReadFile(pipe, buffer, size, &read, NULL)) {
        if (GetLastError() != ERROR_BROKEN_PIPE) {
            fprintf(stderr, "Pipe read failed: %lu\n", GetLastError());
        }
        return -1;
    }
    return read;
}

void close_pipe(pipe_t pipe) {
    CloseHandle(pipe);
}

DWORD WINAPI worker_function(LPVOID param) {
    Worker* worker = (Worker*)param;
    
    /* Worker's main loop */
    while (1) {
        int duration;
        int bytes_read = read_pipe(worker->pipe_to_worker[READ_END], &duration, sizeof(duration));
        
        if (bytes_read <= 0) {
            break; /* End of input or error */
        }
        
        /* Simulate job processing by sleeping */
        Sleep(duration * 1000);
        
        /* Signal completion */
        int done = 1;
        write_pipe(worker->pipe_from_worker[WRITE_END], &done, sizeof(done));
    }
    
    /* Close remaining pipe ends before exiting */
    close_pipe(worker->pipe_to_worker[READ_END]);
    close_pipe(worker->pipe_from_worker[WRITE_END]);
    
    return 0;
}

void create_workers(Worker *workers, int *worker_count, int worker_type, int worker_num) {
    for (int i = 0; i < worker_num; i++) {
        Worker *worker = &workers[*worker_count];
        worker->type = worker_type;
        worker->busy = 0;

        /* Create pipes for communication */
        if (create_pipe(worker->pipe_to_worker) < 0 || create_pipe(worker->pipe_from_worker) < 0) {
            fprintf(stderr, "Pipe creation failed\n");
            exit(EXIT_FAILURE);
        }

        /* Create thread instead of fork for Windows */
        HANDLE thread = CreateThread(NULL, 0, worker_function, worker, 0, &worker->pid);
        
        if (thread == NULL) {
            fprintf(stderr, "Thread creation failed: %lu\n", GetLastError());
            exit(EXIT_FAILURE);
        } else {
            /* Set worker's PID to thread handle for cleanup later */
            worker->pid = (DWORD)thread;
            
            /* Close unnecessary pipe ends */
            close_pipe(worker->pipe_to_worker[READ_END]);
            close_pipe(worker->pipe_from_worker[WRITE_END]);
            
            (*worker_count)++;
        }
    }
}

void dispatch_jobs(Worker *workers, int worker_count) {
    int jobs_in_progress = 0;
    Job current_job = {0, 0};
    int have_current_job = 0;

    while (1) {
        /* Try to read a new job if we don't have one */
        if (!have_current_job && jobs_in_progress == 0) {
            int result = scanf("%d %d", &current_job.type, &current_job.duration);
            if (result == EOF) {
                break; /* No more jobs */
            } else if (result != 2) {
                fprintf(stderr, "Error reading job\n");
                exit(EXIT_FAILURE);
            }
            have_current_job = 1;
        }

        /* Try to assign the current job to a worker */
        if (have_current_job) {
            int worker_idx = get_available_worker(workers, worker_count, current_job.type);
            if (worker_idx >= 0) {
                /* Send job to worker */
                write_pipe(workers[worker_idx].pipe_to_worker[WRITE_END], 
                       &current_job.duration, sizeof(current_job.duration));
                workers[worker_idx].busy = 1;
                jobs_in_progress++;
                have_current_job = 0;
            }
        }

        /* Check for completed jobs by polling */
        for (int i = 0; i < worker_count; i++) {
            if (workers[i].busy) {
                DWORD available = 0;
                DWORD result = 0;
                
                /* Check if there's data to read from the pipe */
                PeekNamedPipe(workers[i].pipe_from_worker[READ_END], NULL, 0, NULL, &available, NULL);
                
                if (available >= sizeof(int)) {
                    int done;
                    if (read_pipe(workers[i].pipe_from_worker[READ_END], &done, sizeof(done)) > 0) {
                        workers[i].busy = 0;
                        jobs_in_progress--;
                    }
                }
            }
        }

        /* Try to read a new job if we've finished processing the previous one */
        if (!have_current_job) {
            int result = scanf("%d %d", &current_job.type, &current_job.duration);
            if (result == EOF) {
                /* No more jobs, but continue processing existing jobs */
                if (jobs_in_progress == 0) {
                    break;
                }
            } else if (result != 2) {
                fprintf(stderr, "Error reading job\n");
                exit(EXIT_FAILURE);
            } else {
                have_current_job = 1;
            }
        }
        
        /* Small sleep to prevent busy-waiting */
        Sleep(100);
    }
}

#else
/* UNIX implementations */

void worker_process(Worker *worker) {
    /* Close unused ends of pipes in child */
    close(worker->pipe_to_worker[WRITE_END]);
    close(worker->pipe_from_worker[READ_END]);
    
    /* Worker's main loop */
    while (1) {
        int duration;
        ssize_t bytes_read = read(worker->pipe_to_worker[READ_END], &duration, sizeof(duration));
        
        if (bytes_read <= 0) {
            if (bytes_read < 0) {
                perror("Read error in worker");
            }
            break; /* End of input or error */
        }
        
        /* Simulate job processing by sleeping */
        sleep(duration);
        
        /* Signal completion */
        int done = 1;
        write(worker->pipe_from_worker[WRITE_END], &done, sizeof(done));
    }
    
    /* Close remaining pipe ends before exiting */
    close(worker->pipe_to_worker[READ_END]);
    close(worker->pipe_from_worker[WRITE_END]);
}

void create_workers(Worker *workers, int *worker_count, int worker_type, int worker_num) {
    for (int i = 0; i < worker_num; i++) {
        Worker *worker = &workers[*worker_count];
        worker->type = worker_type;
        worker->busy = 0;

        /* Create pipes for communication */
        if (pipe(worker->pipe_to_worker) < 0 || pipe(worker->pipe_from_worker) < 0) {
            perror("Pipe creation failed");
            exit(EXIT_FAILURE);
        }

        /* Fork a new process */
        pid_t pid = fork();
        if (pid < 0) {
            perror("Fork failed");
            exit(EXIT_FAILURE);
        } else if (pid == 0) {
            /* Child process (worker) */
            worker_process(worker);
            exit(EXIT_SUCCESS); /* Should not reach here */
        } else {
            /* Parent process */
            worker->pid = pid;
            
            /* Close unused ends of pipes in parent */
            close(worker->pipe_to_worker[READ_END]);
            close(worker->pipe_from_worker[WRITE_END]);
            
            (*worker_count)++;
        }
    }
}

void dispatch_jobs(Worker *workers, int worker_count) {
    fd_set read_fds;
    int max_fd = -1;
    int jobs_in_progress = 0;
    Job current_job = {0, 0};
    int have_current_job = 0;

    /* Find max file descriptor for select */
    for (int i = 0; i < worker_count; i++) {
        if (workers[i].pipe_from_worker[READ_END] > max_fd) {
            max_fd = workers[i].pipe_from_worker[READ_END];
        }
    }

    while (1) {
        /* Try to read a new job if we don't have one */
        if (!have_current_job && jobs_in_progress == 0) {
            int result = scanf("%d %d", &current_job.type, &current_job.duration);
            if (result == EOF) {
                break; /* No more jobs */
            } else if (result != 2) {
                fprintf(stderr, "Error reading job\n");
                exit(EXIT_FAILURE);
            }
            have_current_job = 1;
        }

        /* Try to assign the current job to a worker */
        if (have_current_job) {
            int worker_idx = get_available_worker(workers, worker_count, current_job.type);
            if (worker_idx >= 0) {
                /* Send job to worker */
                write(workers[worker_idx].pipe_to_worker[WRITE_END], 
                      &current_job.duration, sizeof(current_job.duration));
                workers[worker_idx].busy = 1;
                jobs_in_progress++;
                have_current_job = 0;
            }
        }

        /* Use select to wait for worker responses */
        FD_ZERO(&read_fds);
        for (int i = 0; i < worker_count; i++) {
            if (workers[i].busy) {
                FD_SET(workers[i].pipe_from_worker[READ_END], &read_fds);
            }
        }

        /* If there are no busy workers and no current job, try to read a new job */
        if (jobs_in_progress == 0 && !have_current_job) {
            int result = scanf("%d %d", &current_job.type, &current_job.duration);
            if (result == EOF) {
                break; /* No more jobs */
            } else if (result != 2) {
                fprintf(stderr, "Error reading job\n");
                exit(EXIT_FAILURE);
            }
            have_current_job = 1;
            continue;
        }

        /* If there are no busy workers and we couldn't get a new job, we're done */
        if (jobs_in_progress == 0 && !have_current_job) {
            break;
        }

        /* Wait for any worker to finish */
        struct timeval timeout;
        timeout.tv_sec = 1;  /* 1 second timeout to periodically check for new input */
        timeout.tv_usec = 0;

        int select_result = select(max_fd + 1, &read_fds, NULL, NULL, &timeout);
        
        if (select_result < 0) {
            if (errno == EINTR) continue; /* Interrupted by signal */
            perror("Select error");
            exit(EXIT_FAILURE);
        } else if (select_result > 0) {
            /* Check which workers have completed */
            for (int i = 0; i < worker_count; i++) {
                if (workers[i].busy && FD_ISSET(workers[i].pipe_from_worker[READ_END], &read_fds)) {
                    int done;
                    if (read(workers[i].pipe_from_worker[READ_END], &done, sizeof(done)) > 0) {
                        workers[i].busy = 0;
                        jobs_in_progress--;
                    }
                }
            }
        }

        /* Try to read a new job if we've finished processing the previous one */
        if (!have_current_job) {
            int result = scanf("%d %d", &current_job.type, &current_job.duration);
            if (result == EOF) {
                /* No more jobs, but continue processing existing jobs */
                if (jobs_in_progress == 0) {
                    break;
                }
            } else if (result != 2) {
                fprintf(stderr, "Error reading job\n");
                exit(EXIT_FAILURE);
            } else {
                have_current_job = 1;
            }
        }
    }
}
#endif

/* Common functions */
int get_available_worker(Worker *workers, int worker_count, int job_type) {
    for (int i = 0; i < worker_count; i++) {
        if (workers[i].type == job_type && !workers[i].busy) {
            return i;
        }
    }
    return -1; /* No available worker */
}