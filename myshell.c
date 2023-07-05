#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>

/*if "|" in arglist returns its index else returns -1*/
int get_pipe_index(char **arglist, int count)
{
    int i = 0;

    for (i = 0; i < count; i++)
    {
        if (strcmp(arglist[i], "|") == 0)
        {
            return i;
        }
    }

    return -1;
}

/*credit: http://www.microhowto.info/howto/reap_zombie_processes_using_a_sigchld_handler.html*/
void handle_sigchld(int sig)
{
    int wait_res;
    int saved_errno = errno;

    while (1)
    {
        /*wait to a zombie child*/
        wait_res = waitpid((pid_t)(-1), NULL, WNOHANG);
        if (wait_res == 0)
        {
            /*still has children but no zombies exist anymore, finish waiting*/
            break;
        }

        if (wait_res == -1 && errno == ECHILD)
        {
            /*The calling process does not have any unwaited-for children, finish waiting*/
            break;
        }

        if (wait_res == -1 && errno != EINTR)
        {
            /*waiting failed - printing error and terminating parent shell process with exit(1)*/
            perror(strerror(errno));
            exit(1);
        }
    }
    errno = saved_errno;
}

int prepare(void)
{
    struct sigaction sa_sigchld;

    /*setting parent shell process to ignore SIGINT*/

    if (signal(SIGINT, SIG_IGN) == SIG_ERR)
    {
        /*signal failed - printing error and returning 1 for a failure*/
        perror(strerror(errno));
        return 1;
    }

    /*setting a signal handler for SIGCHLD to prevent zombies*/

    sa_sigchld.sa_handler = &handle_sigchld;
    sigemptyset(&sa_sigchld.sa_mask);
    sa_sigchld.sa_flags = SA_RESTART | SA_NOCLDSTOP;

    if (sigaction(SIGCHLD, &sa_sigchld, 0) == -1)
    {
        /*sigaction failed - printing error message and returning 1 for a failure*/
        perror(strerror(errno));
        return 1;
    }

    return 0;
};

int process_arglist(int count, char **arglist)
{
    int pipe_index;
    pid_t pid;
    pid_t pid2;
    pid_t input_file_fd;
    int pipe_fd[2];

    if (strcmp(arglist[count - 1], "&") == 0)
    {
        /*The background case ("&"")*/

        /*deleting "&" from arglist*/
        arglist[count - 1] = NULL;

        pid = fork();
        if (pid < 0)
        {
            /*fork failed - printing error and returning 0*/
            perror(strerror(errno));
            return 0;
        }

        if (pid == 0)
        {
            /*child process*/

            /*setting SIGCHLD handling back to default in child process*/
            if (signal(SIGCHLD, SIG_DFL) == SIG_ERR)
            {
                /*signal failed - printing error and terminating only child process with exit(1)*/
                perror(strerror(errno));
                exit(1);
            }

            if (execvp(arglist[0], arglist) == -1)
            {
                /*execvp failed - printing error and terminating only child process with exit(1)*/
                perror(strerror(errno));
                exit(1);
            }
        }

        /*not waiting for child because it runs on the background*/

        /*returns 1 on success of father process*/
        return 1;
    }
    if (count > 1 && strcmp(arglist[count - 2], "<") == 0)
    {
        /*The input redirection case*/

        /*deleting "<" from arglist*/
        arglist[count - 2] = NULL;

        pid = fork();
        if (pid < 0)
        {
            /*fork failed - printing error and returning 0*/
            perror(strerror(errno));
            return 0;
        }

        if (pid == 0)
        {
            /*child process*/

            /*setting SIGINT handling in foreground child process to default - meaning to terminate from SIGINT*/
            if (signal(SIGINT, SIG_DFL) == SIG_ERR)
            {
                /*signal failed - printing error and terminating only child process with exit(1)*/
                perror(strerror(errno));
                exit(1);
            }

            /*setting SIGCHLD handling back to default in child process*/
            if (signal(SIGCHLD, SIG_DFL) == SIG_ERR)
            {
                /*signal failed - printing error and terminating only child process with exit(1)*/
                perror(strerror(errno));
                exit(1);
            }

            /*opening input file*/
            input_file_fd = open(arglist[count - 1], O_RDONLY);
            if (input_file_fd < 0)
            {
                /*open input file failed - printing error and terminating only child process with exit(1)*/
                perror(strerror(errno));
                exit(1);
            }

            /*setting the stdin fd to point to input file*/
            if (dup2(input_file_fd, STDIN_FILENO) == (-1))
            {
                /*dup2 failed - printing error and terminating only child process with exit(1)*/
                perror(strerror(errno));
                exit(1);
            }

            /*closing the fd that was pointing to input file because stdin fd is now pointing to it and hence it is
             * redundant now*/
            if (close(input_file_fd) == (-1))
            {
                /*closing input_file_fd failed - printing error and terminating only child process with exit(1)*/
                perror(strerror(errno));
                exit(1);
            }

            /*executing the program with stdin fd pointing to input file*/
            if (execvp(arglist[0], arglist) == -1)
            {
                /*execvp failed - printing error and terminating only child process with exit(1)*/
                perror(strerror(errno));
                exit(1);
            }
        }

        /*waiting for child process to finish*/
        if (waitpid(pid, NULL, 0) == -1 && errno != ECHILD && errno != EINTR)
        {
            /*waiting child process failed - printing error and returning 0*/
            perror(strerror(errno));
            return 0;
        }

        /*returns 1 on success of father process*/
        return 1;
    }

    pipe_index = get_pipe_index(arglist, count);
    if (pipe_index >= 0)
    {
        /*The pipe case ("|")*/

        /*deleting "|" from arglist*/
        arglist[pipe_index] = NULL;

        /*Opening a pipe*/
        if (pipe(pipe_fd) == -1)
        {
            /*opening a pipe failed - printing error and returning 0*/
            perror(strerror(errno));
            return 0;
        }

        pid = fork();
        if (pid < 0)
        {
            /*forking first child failed - printing error and returning 0*/
            perror(strerror(errno));
            return 0;
        }

        if (pid == 0)
        {
            /*writing child process*/

            /*setting SIGINT handling in foreground child process to default - meaning to terminate from SIGINT*/
            if (signal(SIGINT, SIG_DFL) == SIG_ERR)
            {
                /*signal failed - printing error and terminating only child process with exit(1)*/
                perror(strerror(errno));
                exit(1);
            }

            /*setting SIGCHLD handling back to default in child process*/
            if (signal(SIGCHLD, SIG_DFL) == SIG_ERR)
            {
                /*signal failed - printing error and terminating only child process with exit(1)*/
                perror(strerror(errno));
                exit(1);
            }

            /*closing reading side of writing child process*/
            if (close(pipe_fd[0]) == -1)
            {
                /*closing reading side failed - printing error and terminating only child process with exit(1)*/
                perror(strerror(errno));
                exit(1);
            }

            /*setting the stdout fd to point to writing side of the pipe*/
            if (dup2(pipe_fd[1], STDOUT_FILENO) == -1)
            {
                /*dup2 failed - printing error and terminating only child process with exit(1)*/
                perror(strerror(errno));
                exit(1);
            }

            /*closing the fd pointing to writing side of the pipe because now it is redundant*/
            if (close(pipe_fd[1]) == -1)
            {
                /*closing writing side failed - printing error and terminating only child process with exit(1)*/
                perror(strerror(errno));
                exit(1);
            }
            /*executing the program with stdout fd pointing to writing side of the pipe*/
            if (execvp(arglist[0], arglist) == -1)
            {
                /*execvp failed - printing error and terminating only child process with exit(1)*/
                perror(strerror(errno));
                exit(1);
            }
        }

        /*closing writing side on father process, by that reading child process won't need to close it too*/
        if (close(pipe_fd[1]) == -1)
        {
            /*closing writing side failed - printing error and returning*/
            perror(strerror(errno));
            return 0;
        }

        pid2 = fork();
        if (pid2 < 0)
        {
            /*forking second child failed - printing error and returning 0*/
            perror(strerror(errno));
            return 0;
        }

        if (pid2 == 0)
        {
            /*reading child process*/

            /*setting SIGINT handling in foreground child process to default - meaning to terminate from SIGINT*/
            if (signal(SIGINT, SIG_DFL) == SIG_ERR)
            {
                /*signal failed - printing error and terminating only child process with exit(1)*/
                perror(strerror(errno));
                exit(1);
            }

            /*setting SIGCHLD handling back to default in child process*/
            if (signal(SIGCHLD, SIG_DFL) == SIG_ERR)
            {
                /*signal failed - printing error and terminating only child process with exit(1)*/
                perror(strerror(errno));
                exit(1);
            }

            /*setting the stdin fd to point to reading side of the pipe*/
            if (dup2(pipe_fd[0], STDIN_FILENO) == -1)
            {
                /*dup2 failed - printing error and terminating only child process with exit(1)*/
                perror(strerror(errno));
                exit(1);
            }

            /*closing the fd pointing to reading side of the pipe because now it is redundant*/
            if (close(pipe_fd[0]) == -1)
            {
                /*closing reading side failed - printing error and terminating only child process with exit(1)*/
                perror(strerror(errno));
                exit(1);
            }

            arglist = arglist + pipe_index + 1;
            /*executing the program with stdin fd pointing to reading side of the pipe*/
            if (execvp(arglist[0], arglist) == -1)
            {
                /*execvp failed - printing error and terminating only child process with exit(1)*/
                perror(strerror(errno));
                exit(1);
            }
        }

        /*closing reading side in parent process*/
        if (close(pipe_fd[0]) == -1)
        {
            /*closing writing side failed - printing error and returning*/
            perror(strerror(errno));
            return 0;
        }

        /*waiting for writing child process to finish*/
        if (waitpid(pid, NULL, 0) == -1 && errno != ECHILD && errno != EINTR)
        {
            /*waiting child process failed - printing error and returning 0*/
            perror(strerror(errno));
            return 0;
        }
        /*waiting for reading child process to finish*/
        if (waitpid(pid2, NULL, 0) == -1 && errno != ECHILD && errno != EINTR)
        {
            /*waiting child process failed - printing error and returning 0*/
            perror(strerror(errno));
            return 0;
        }

        /*returns 1 on success of father process*/
        return 1;
    }

    /*The regular case (Not "&", "<" , "|")*/

    pid = fork();
    if (pid < 0)
    {
        /*fork failed - printing error and returning 0*/
        perror(strerror(errno));
        return 0;
    }

    if (pid == 0)
    {
        /*child process*/

        /*setting SIGINT handling in foreground child process to default - meaning to terminate from SIGINT*/
        if (signal(SIGINT, SIG_DFL) == SIG_ERR)
        {
            /*signal failed - printing error and terminating only child process with exit(1)*/
            perror(strerror(errno));
            exit(1);
        }

        /*setting SIGCHLD handling back to default in child process*/
        if (signal(SIGCHLD, SIG_DFL) == SIG_ERR)
        {
            /*signal failed - printing error and terminating only child process with exit(1)*/
            perror(strerror(errno));
            exit(1);
        }

        /*executing child process*/
        if (execvp(arglist[0], arglist) == -1)
        {
            /*execvp failed - printing error and terminating only child process with exit(1)*/
            perror(strerror(errno));
            exit(1);
        }
    }

    /*waiting child process*/
    if (waitpid(pid, NULL, 0) == -1 && errno != ECHILD && errno != EINTR)
    {
        /*waiting child process failed - printing error and returning 0*/
        perror(strerror(errno));
        return 0;
    }

    /*returns 1 on success of father process*/
    return 1;
}

int finalize(void)
{
    return 0;
};
