//Oswiadczam, ze niniejsza praca stanowiaca podstawe do uznania
//osiagniecia efektow uczenia sie z przedmiotu SOP zostala wykonana
//przeze mnie samodzielnie.
//Zuzanna Twardowska 305913
#define _XOPEN_SOURCE 500
#define _GNU_SOURCE
#include <stddef.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <dirent.h>
#include <errno.h>
#include <time.h>
#include <ftw.h>
#include <fcntl.h>
#include <ctype.h>
#define ERR(source) (perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), exit(EXIT_FAILURE))
void usage(char *pname)
{
    fprintf(stderr, "USAGE:%s [-r recursive] [-m min=10] [-M max=1000] [-i interval=600] dir1 [dir2]\n", pname);
    exit(EXIT_FAILURE);
}
#define MAX_PATH 200
#define MAX_COMMAND 51
#define MAX_PID_LEN 15
#define MAX_LINE 4096
#define ELAPSED(start, end) ((end).tv_sec - (start).tv_sec) + (((end).tv_nsec - (start).tv_nsec) * 1.0e-9)
typedef struct timespec timespec_t;
typedef struct query_thread
{
    pthread_t tid;
    int *query_nums;
    char **dirs;
    int dirs_number;
    int query_size;
} query_thread;
typedef struct loop_thread
{
    pthread_t tid;
    char **dirs;
    int dirs_number;
} loop_thread;
typedef struct index_thread
{
    pthread_t tid;
    char *dir;
    int recursive;
    int min;
    int max;
} index_thread;
typedef struct args_thread
{
    int recursive;
    int min;
    int max;
    int interval;
    int dirs_number;
    char **dirs;
} args_thread;
void sethandler(void (*f)(int), int sigNo)
{
    struct sigaction act;
    memset(&act, 0, sizeof(struct sigaction));
    act.sa_handler = f;
    if (-1 == sigaction(sigNo, &act, NULL))
        ERR("sigaction");
}
void ReadArguments(int argc, char **argv, args_thread *a_thread)
{
    a_thread->recursive = 0;
    a_thread->min = 10;
    a_thread->max = 1000;
    a_thread->interval = 600;
    int c;
    while ((c = getopt(argc, argv, "rm:M:i:")) != -1)
        switch (c)
        {
        case 'r':
            a_thread->recursive = 1;
            break;
        case 'M':
            a_thread->max = atoi(optarg);
            break;
        case 'm':
            a_thread->min = atoi(optarg);
            break;
        case 'i':
            a_thread->interval = atoi(optarg);
            break;
        case '?':
        default:
            usage(argv[0]);
        }
    if (argc <= optind)
        usage(argv[0]);
    a_thread->dirs_number = argc - optind;
    a_thread->dirs = (char **)malloc(sizeof(char *) * (a_thread->dirs_number));
    if (!(a_thread->dirs))
        ERR("malloc");
    for (int i = 0; i < a_thread->dirs_number; i++)
    {
        (a_thread->dirs)[i] = (char *)malloc(sizeof(char) * MAX_PATH);
        if (!(a_thread->dirs)[i])
            ERR("malloc");
        strcpy((a_thread->dirs)[i], argv[optind + i]);
    }
}
ssize_t signalproof_read(int file, void *line, size_t count)
{
    ssize_t err, len = 0;
    errno = 0;
    do
    {
        err = TEMP_FAILURE_RETRY(read(file, line, count));
        if (err < 0)
            return err;
        if (err == 0)
            return len;
        line += err;
        len += err;
        count -= err;
    } while (count > 0);
    return len;
}
ssize_t signalproof_write(int file, void *line, size_t count)
{
    ssize_t err, len = 0;
    errno = 0;
    do
    {
        err = TEMP_FAILURE_RETRY(write(file, line, count));
        if (err < 0)
            return err;
        if (err == 0)
            return len;
        line += err;
        len += err;
        count -= err;
    } while (count > 0);
    return len;
}
void query_loop(int file, int query_num)
{
    int num, offset, p_size, err;
    char path[MAX_PATH + 20];
    while (1)
    {
        memset(path, 0, sizeof(path));
        if ((err = signalproof_read(file, &p_size, sizeof(int))) < 0)
            ERR("read");
        if (err == 0)
            break;
        if ((err = signalproof_read(file, &path, sizeof(char) * p_size)) < 0)
            ERR("read");
        if (err == 0)
            break;
        if ((err = signalproof_read(file, &num, sizeof(int))) < 0)
            ERR("read");
        if (err == 0)
            break;
        if ((err = signalproof_read(file, &offset, sizeof(int))) < 0)
            ERR("read");
        if (err == 0)
            break;
        if (num == query_num)
            printf("%s:%d\n", path, offset);
    }
}
void *query_work(void *voidPtr)
{
    query_thread *q_thread = voidPtr;
    int file;
    char cur_dir[MAX_PATH];
    for (int i = 0; i < q_thread->query_size; i++)
    {
        printf("Number %d occurrences:\n", (q_thread->query_nums)[i]);
        for (int j = 0; j < q_thread->dirs_number; j++)
        {
            if (getcwd(cur_dir, MAX_PATH) == NULL)
                ERR("getcwd");
            if (chdir((q_thread->dirs)[j]))
                ERR("chdir");
            errno = 0;
            if ((file = TEMP_FAILURE_RETRY(open(".numf_index", O_RDONLY,NULL))) == -1)
            {
                if (errno != ENOENT)
                    ERR("open");
                printf("Dir: %s does not have index file yet\n", q_thread->dirs[j]);
                if (chdir(cur_dir))
                    ERR("chdir");
                continue;
            }
            query_loop(file, q_thread->query_nums[i]);
            if (TEMP_FAILURE_RETRY(close(file)) == -1)
                ERR("close");
            if (chdir(cur_dir))
                ERR("chdir");
        }
    }
    free(q_thread->query_nums);
    free(q_thread);
    return NULL;
}
int make_pid_file(char *name, char *dir)
{
    errno = 0;
    int file;
    if ((file = TEMP_FAILURE_RETRY(open(name,O_WRONLY | O_CREAT | O_EXCL, 0666))) == -1)
    {
        if (errno == EEXIST)
        {
            pid_t pid_occu;
            if ((file = TEMP_FAILURE_RETRY(open(name, O_RDONLY,NULL))) == -1)
                ERR("open .numf_pid");
            if (signalproof_read(file, &pid_occu, sizeof(pid_occu)) < 0)
                ERR("read from .numf_pid");
            fprintf(stderr, "Dir: [%s] already occupied by PID:[%d]\n", dir, pid_occu);
            return -1;
        }
        ERR("open .numf_pid");
    }
    pid_t pid = getpid();
    if (signalproof_write(file, &pid, sizeof(pid)) < 0)
        ERR("write to .numf_pid");
    if (TEMP_FAILURE_RETRY(close(file)) == -1)
        ERR("close .numf_pid");
    return 0;
}
void write_to_index(int dest, char *file_path, int val, int off_rem)
{
    int p_size = strlen(file_path);
    if (signalproof_write(dest, &p_size, sizeof(int)) < 0)
        ERR("write");
    if (signalproof_write(dest, file_path, sizeof(char) * p_size) < 0)
        ERR("write");
    if (signalproof_write(dest, &val, sizeof(int)) < 0)
        ERR("write");
    if (signalproof_write(dest, &off_rem, sizeof(int)) < 0)
        ERR("write");
}
void _close_file(void *voidArgs)
{
    int *file = voidArgs;
    if (TEMP_FAILURE_RETRY(close(*file)) == -1)
        ERR("close");
}
void file_check(char *path, int min, int max, char *file_name, int dest)
{
    char c;
    int offset = 0, val = 0, off_rem = 0, err, file;
    char file_path[MAX_PATH + 20], line[MAX_LINE];
    strcpy(file_path, path);
    strcat(file_path, file_name);
    if ((file = TEMP_FAILURE_RETRY(open(file_name, O_RDONLY,NULL))) == -1)
        ERR("open");
    pthread_cleanup_push(_close_file, &file);
    while (1)
    {
        memset(&line, 0, MAX_LINE);
        if ((err = signalproof_read(file, &line, MAX_LINE)) < 0)
            ERR("read");
        if (err == 0)
            break;
        for(int i = 0; i < err; i++)
        {
            c = line[i];
            if (isdigit(c))
            {
                if (val == 0)
                    off_rem = offset;
                val *= 10;
                val += c - '0';
            }
            else if (val != 0)
            {
                if (val >= min && val <= max)
                    write_to_index(dest, file_path, val, off_rem);
                val = 0;
            }
            offset++;
        }
    }
    if (TEMP_FAILURE_RETRY(close(file)) == -1)
        ERR("close");
    pthread_cleanup_pop(0);
}
void indexing(index_thread *i_thread, char *path, int dest, char *dir)
{
    DIR *dirp;
    struct dirent *dp;
    struct stat filestat;
    char cur_dir[MAX_PATH], cur_path[MAX_PATH];
    strcpy(cur_path, path);
    if (getcwd(cur_dir, MAX_PATH) == NULL)
        ERR("getcwd");
    if (chdir(dir))
        ERR("chdir");
    if (NULL == (dirp = opendir(".")))
        ERR("opendir");
    do
    {
        errno = 0;
        if ((dp = readdir(dirp)) != NULL)
        {
            if (lstat(dp->d_name, &filestat))
                ERR("lstat");
            if ((dp->d_name)[0] == '.')
                continue;
            if (S_ISREG(filestat.st_mode))
                file_check(path, i_thread->min, i_thread->max, dp->d_name, dest);
            if (i_thread->recursive == 1 && S_ISDIR(filestat.st_mode))
            {
                strcat(path, dp->d_name);
                strcat(path, "/");
                indexing(i_thread, path, dest, dp->d_name);
                strcpy(path, cur_path);
            }
        }
    } while (dp != NULL);
    if (errno != 0)
        ERR("readdir");
    if (chdir(cur_dir))
        ERR("chdir");
    if (closedir(dirp))
        ERR("closedir");
}
void _close_dest(void *voidArgs)
{
    int *dest = voidArgs;
    if (TEMP_FAILURE_RETRY(close(*dest)) == -1)
        ERR("close");
    if (rename(".numf_index1", ".numf_index"))
        ERR("rename");
}
void *index_work(void *voidPtr)
{
    index_thread *i_thread = voidPtr;
    pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);
    int dest, flags = O_WRONLY | O_CREAT | O_APPEND;
    char name[MAX_PATH + 20], cur_dir[MAX_PATH], path[MAX_PATH];
    strcpy(name, i_thread->dir);
    strcat(name, "/.numf_index1");
    strcpy(path, "./");
    if ((dest = TEMP_FAILURE_RETRY(open(name, flags, 0666))) == -1)
        ERR("open");
    pthread_cleanup_push(_close_dest, &dest);
    if (getcwd(cur_dir, MAX_PATH) == NULL)
        ERR("getwcd");
    indexing(i_thread, path, dest, i_thread->dir);
    if (chdir(i_thread->dir))
        ERR("chdir");
    if (TEMP_FAILURE_RETRY(close(dest)) == -1)
        ERR("close");
    if (rename(".numf_index1", ".numf_index"))
        ERR("rename");
    pthread_cleanup_pop(0);
    if (chdir(cur_dir))
        ERR("chdir");
    if (kill(getpid(), SIGCHLD) < 0)
        ERR("kill");
    return NULL;
}
void set_childmask(sigset_t *childMask)
{
    sigemptyset(childMask);
    sigaddset(childMask, SIGUSR1);
    sigaddset(childMask, SIGUSR2);
    sigaddset(childMask, SIGALRM);
    sigaddset(childMask, SIGINT);
    sigaddset(childMask, SIGCHLD);
}
void sigusr1_action(timespec_t *start, int *status, int interval, index_thread *i_thread)
{
    int err = pthread_create(&(i_thread->tid), NULL, index_work, i_thread);
    if (err != 0)
        ERR("Couldn't create index thread");
    *status = 1;
    if (clock_gettime(CLOCK_REALTIME, start))
        ERR("Failed to retrieve time");
    alarm(interval);
}
void child_loop(int *status, timespec_t *start, int interval, index_thread *i_thread)
{
    int signo = 0;
    timespec_t current;
    sigset_t childMask;
    set_childmask(&childMask);
    sigprocmask(SIG_BLOCK, &childMask, NULL);
    if (pthread_sigmask(SIG_BLOCK, &childMask, NULL))
        ERR("SIG_BLOCK error");
    while (signo != SIGINT)
    {
        if (sigwait(&childMask, &signo))
            ERR("Sigwait failed");
        switch (signo)
        {
        case SIGALRM:
        case SIGUSR1:
            if (*status == 0)
                sigusr1_action(start, status, interval, i_thread);
            break;
        case SIGUSR2:
            if (*status == 1)
            {
                if (clock_gettime(CLOCK_REALTIME, &current))
                    ERR("Failed to retrieve time");
                printf("Status of [%d]: active for %f s\n", getpid(), ELAPSED(*start, current));
            }
            else
                printf("Status of [%d]: not active\n", getpid());
            break;
        case SIGCHLD:
            *status = 0;
        case SIGINT:
            break;
        default:
            printf("Unknown signal\n");
        }
    }
}
void child_work(char *dir, args_thread *a_thread)
{
    timespec_t start;
    int status = 0, old;
    char name[MAX_PATH + 20], index_name[MAX_PATH + 20];
    strcpy(name, dir);
    strcat(name, "/.numf_pid");
    strcpy(index_name, dir);
    strcat(index_name, "/.numf_index");
    if (make_pid_file(name, dir) == -1)
        return;
    index_thread *i_thread = (index_thread *)malloc(sizeof(index_thread));
    if (!i_thread)
        ERR("malloc");
    i_thread->dir = dir;
    i_thread->max = a_thread->max;
    i_thread->min = a_thread->min;
    i_thread->recursive = a_thread->recursive;
    errno = 0;
    if ((old = TEMP_FAILURE_RETRY(open(index_name, O_WRONLY, 0666))) == -1)
    {
        if (errno != ENOENT)
            ERR("open");
        sigusr1_action(&start, &status, a_thread->interval, i_thread);
    }
    child_loop(&status, &start, a_thread->interval, i_thread);
    if (status == 1)
    {
        if (0 != pthread_cancel(i_thread->tid))
            ERR("thread cancel");
        if (pthread_join(i_thread->tid, NULL) != 0)
            ERR("thread join");
        status = 0;
    }
    if (0 != remove(name))
        ERR("remove");
    free(i_thread);
    exit(EXIT_SUCCESS);
}
void create_children(args_thread *a_thread)
{
    pid_t s;
    for (int i = 0; i < a_thread->dirs_number; i++)
    {
        if ((s = fork()) < 0)
            ERR("Fork:");
        if (!s)
        {
            child_work(a_thread->dirs[i], a_thread);
            for (int j = 0; j < a_thread->dirs_number; j++)
                free(a_thread->dirs[j]);
            free(a_thread->dirs);
            free(a_thread);
            exit(EXIT_SUCCESS);
        }
    }
}
int ReadQuery(int **query_nums, char *command)
{
    int query_size = 0, val = 0, index = 0;
    for (int i = 1; i < MAX_COMMAND; i++)
    {
        if (command[i - 1] == ' ' && isdigit(command[i]))
            query_size++;
        if (command[i] == '\0')
            break;
    }
    *query_nums = (int *)malloc(sizeof(int) * query_size);
    if (!(*query_nums))
        ERR("malloc");
    for (int i = 0; i < MAX_COMMAND; i++)
    {
        if (isdigit(command[i]))
        {
            val *= 10;
            val += command[i] - '0';
        }
        if ((command[i] == ' ' || command[i] == '\0') && val != 0)
        {
            (*query_nums)[index++] = val;
            val = 0;
        }
        if (command[i] == '\0')
            break;
    }
    return query_size;
}
void *loop_mode(void *voidArgs)
{
    loop_thread *l_thread = voidArgs;
    int err;
    char command[MAX_COMMAND];
    while (1)
    {
        if (fgets(command, MAX_COMMAND - 1, stdin) == NULL)
            ERR("fgets");
        if (strcmp(command, "exit\n") == 0)
        {
            if (kill(getpid(), SIGINT) < 0)
                ERR("kill");
        }
        if (strcmp(command, "status\n") == 0)
        {
            if (kill(0, SIGUSR2) < 0)
                ERR("kill");
        }
        if (strcmp(command, "index\n") == 0)
        {
            if (kill(0, SIGUSR1) < 0)
                ERR("kill");
        }
        if (strstr(command, "query") != NULL)
        {
            query_thread *q_thread = (query_thread *)malloc(sizeof(query_thread));
            if (!q_thread)
                ERR("malloc");
            q_thread->dirs = l_thread->dirs;
            q_thread->dirs_number = l_thread->dirs_number;
            q_thread->query_size = ReadQuery(&(q_thread->query_nums), command);
            err = pthread_create(&(q_thread->tid), NULL, query_work, q_thread);
            if (err != 0)
                ERR("Couldn't create query thread");
            err = pthread_join(q_thread->tid, NULL);
            if (err != 0)
                ERR("thread join");
        }
    }
    return NULL;
}
void sig_blocking()
{
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGUSR1);
    sigaddset(&mask, SIGUSR2);
    sigaddset(&mask, SIGALRM);
    sigaddset(&mask, SIGINT);
    if (pthread_sigmask(SIG_BLOCK, &mask, NULL))
        ERR("SIG_BLOCK error");
}
void sig_ignoring()
{
    sethandler(SIG_IGN, SIGUSR1);
    sethandler(SIG_IGN, SIGUSR2);
    sethandler(SIG_IGN, SIGALRM);
}
void wait_for_children()
{
    errno = 0;
    pid_t temp;
    while (1)
    {
        temp = TEMP_FAILURE_RETRY(waitpid(0, NULL, 0));
        if (temp == 0)
            break;
        if (temp <= 0)
        {
            if (ECHILD == errno)
                break;
            ERR("waitpid");
        }
    }
}
int main(int argc, char **argv)
{
    int signo, err;
    args_thread *a_thread = (args_thread *)malloc(sizeof(args_thread));
    if (!a_thread)
        ERR("malloc");
    ReadArguments(argc, argv, a_thread);
    sig_blocking();
    create_children(a_thread);
    sig_ignoring();
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    if (pthread_sigmask(SIG_BLOCK, &mask, NULL))
        ERR("SIG_BLOCK error");
    loop_thread *l_thread = (loop_thread *)malloc(sizeof(loop_thread));
    if (!l_thread)
        ERR("malloc");
    l_thread->dirs = a_thread->dirs;
    l_thread->dirs_number = a_thread->dirs_number;
    err = pthread_create(&(l_thread->tid), NULL, loop_mode, l_thread);
    if (err != 0)
        ERR("Couldn't create loop thread");
    if (sigwait(&mask, &signo))
        ERR("Sigwait failed");
    if (0 != pthread_cancel(l_thread->tid))
        ERR("thread cancel");
    err = pthread_join(l_thread->tid, NULL);
    if (err != 0)
        ERR("thread join");
    if (kill(0, SIGINT) < 0)
        ERR("kill");
    wait_for_children();
    for (int i = 0; i < a_thread->dirs_number; i++)
        free(a_thread->dirs[i]);
    free(a_thread->dirs);
    free(l_thread);
    free(a_thread);
    return EXIT_SUCCESS;
}