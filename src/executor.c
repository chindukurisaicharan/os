#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>

#include "executor.h"
#include "builtin.h"


/* =========================================================
 * SIGCHLD HANDLER
 * ========================================================= */

/*
 * Reap finished background processes.
 *
 * WNOHANG means:
 *     do not wait if no child has finished.
 *
 * This prevents zombie processes.
 */
static void sigchld_handler(int signal_number)
{
    int saved_errno;

    (void)signal_number;

    saved_errno = errno;

    while (waitpid(-1, NULL, WNOHANG) > 0)
    {
        /*
         * Keep reaping completed children.
         */
    }

    errno = saved_errno;
}


/*
 * Install SIGCHLD handler.
 */
void setup_background_handler(void)
{
    struct sigaction action;

    action.sa_handler = sigchld_handler;

    sigemptyset(&action.sa_mask);

    action.sa_flags = SA_RESTART | SA_NOCLDSTOP;

    if (sigaction(SIGCHLD, &action, NULL) < 0)
    {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }
}


/* =========================================================
 * SIGNAL BLOCKING
 * ========================================================= */

/*
 * Block SIGCHLD while creating/waiting for foreground
 * processes.
 *
 * This prevents the background cleanup handler from
 * accidentally reaping a foreground child.
 */
static int block_sigchld(sigset_t *old_mask)
{
    sigset_t mask;

    sigemptyset(&mask);

    sigaddset(&mask, SIGCHLD);

    if (sigprocmask(
            SIG_BLOCK,
            &mask,
            old_mask) < 0)
    {
        perror("sigprocmask");
        return -1;
    }

    return 0;
}


/*
 * Restore previous signal mask.
 */
static void restore_sigchld(const sigset_t *old_mask)
{
    if (sigprocmask(
            SIG_SETMASK,
            old_mask,
            NULL) < 0)
    {
        perror("sigprocmask");
    }
}


/* =========================================================
 * REDIRECTION
 * ========================================================= */

static int apply_redirections(command_t *cmd)
{
    int fd;


    /*
     * Input:
     *
     * command < file
     */
    if (cmd->input != NULL)
    {
        fd = open(cmd->input, O_RDONLY);

        if (fd < 0)
        {
            perror(cmd->input);
            return -1;
        }

        if (dup2(fd, STDIN_FILENO) < 0)
        {
            perror("dup2");

            close(fd);

            return -1;
        }

        close(fd);
    }


    /*
     * Output:
     *
     * command > file
     *
     * command >> file
     */
    if (cmd->output != NULL)
    {
        if (cmd->append)
        {
            fd = open(
                cmd->output,
                O_WRONLY | O_CREAT | O_APPEND,
                0644
            );
        }
        else
        {
            fd = open(
                cmd->output,
                O_WRONLY | O_CREAT | O_TRUNC,
                0644
            );
        }

        if (fd < 0)
        {
            perror(cmd->output);
            return -1;
        }

        if (dup2(fd, STDOUT_FILENO) < 0)
        {
            perror("dup2");

            close(fd);

            return -1;
        }

        close(fd);
    }

    return 0;
}


/* =========================================================
 * BACKGROUND STDIN
 * ========================================================= */

/*
 * Background processes must not read from the shell
 * terminal.
 *
 * Therefore stdin is connected to /dev/null.
 */
static int redirect_background_stdin(void)
{
    int fd;

    fd = open("/dev/null", O_RDONLY);

    if (fd < 0)
    {
        perror("/dev/null");
        return -1;
    }

    if (dup2(fd, STDIN_FILENO) < 0)
    {
        perror("dup2");

        close(fd);

        return -1;
    }

    close(fd);

    return 0;
}


/* =========================================================
 * SINGLE COMMAND
 * ========================================================= */

int execute_command(command_t *cmd)
{
    pid_t pid;

    int status;

    sigset_t old_mask;


    /*
     * Invalid command.
     */
    if (cmd == NULL ||
        cmd->argc == 0 ||
        cmd->argv[0] == NULL)
    {
        return -1;
    }


    /*
     * -----------------------------------------------------
     * FOREGROUND BUILTIN
     * -----------------------------------------------------
     *
     * cd must execute in the parent shell.
     */
    if (is_builtin(cmd) &&
        !cmd->background)
    {
        return execute_builtin(cmd);
    }


    /*
     * Block SIGCHLD while creating the child.
     */
    if (block_sigchld(&old_mask) < 0)
    {
        return -1;
    }


    /*
     * Create child.
     */
    pid = fork();


    if (pid < 0)
    {
        perror("fork");

        restore_sigchld(&old_mask);

        return -1;
    }


    /*
     * =====================================================
     * CHILD
     * =====================================================
     */
    if (pid == 0)
    {
        /*
         * Restore normal signal mask.
         */
        restore_sigchld(&old_mask);


        /*
         * Background command:
         *
         * stdin -> /dev/null
         */
        if (cmd->background)
        {
            if (redirect_background_stdin() < 0)
            {
                _exit(1);
            }
        }


        /*
         * Explicit redirection is applied after
         * /dev/null.
         *
         * Therefore:
         *
         * command &
         *
         * gets /dev/null
         *
         * while:
         *
         * command < input.txt &
         *
         * gets input.txt.
         */
        if (apply_redirections(cmd) < 0)
        {
            _exit(1);
        }


        /*
         * Background builtin.
         *
         * It runs in the child, so:
         *
         * cd /tmp &
         *
         * does NOT change the parent shell directory.
         */
        if (is_builtin(cmd))
        {
            int result;

            result = execute_builtin(cmd);

            if (result < 0)
            {
                _exit(1);
            }

            _exit(0);
        }


        /*
         * External command.
         */
        execvp(
            cmd->argv[0],
            cmd->argv
        );


        /*
         * execvp() failed.
         */
        perror("execvp");

        _exit(127);
    }


    /*
     * =====================================================
     * PARENT
     * =====================================================
     */


    /*
     * -----------------------------------------------------
     * BACKGROUND
     * -----------------------------------------------------
     */
    if (cmd->background)
    {
        printf(
            "[Background PID: %d]\n",
            (int)pid
        );

        fflush(stdout);


        /*
         * Do NOT wait for background process.
         */
        restore_sigchld(&old_mask);

        return 0;
    }


    /*
     * -----------------------------------------------------
     * FOREGROUND
     * -----------------------------------------------------
     */
    while (waitpid(
               pid,
               &status,
               0) < 0)
    {
        if (errno == EINTR)
        {
            continue;
        }

        perror("waitpid");

        restore_sigchld(&old_mask);

        return -1;
    }


    /*
     * Restore SIGCHLD after foreground wait.
     */
    restore_sigchld(&old_mask);


    /*
     * Return exit status.
     */
    if (WIFEXITED(status))
    {
        return WEXITSTATUS(status);
    }


    if (WIFSIGNALED(status))
    {
        return 128 + WTERMSIG(status);
    }


    return 0;
}


/* =========================================================
 * PIPELINE
 * ========================================================= */

int execute_pipeline(pipeline_t *pipeline)
{
    int command_count;

    int background;

    int pipes[MAX_COMMANDS - 1][2];

    pid_t pids[MAX_COMMANDS];

    int last_status = 0;

    sigset_t old_mask;


    /*
     * Invalid pipeline.
     */
    if (pipeline == NULL)
    {
        return -1;
    }


    command_count = pipeline->command_count;


    /*
     * Empty pipeline.
     */
    if (command_count <= 0)
    {
        return -1;
    }


    /*
     * Single command.
     */
    if (command_count == 1)
    {
        return execute_command(
            &pipeline->commands[0]
        );
    }


    /*
     * Background status belongs to the LAST command.
     *
     * The parser already places the background flag
     * on the final command of the pipeline.
     */
    background =
        pipeline->commands[
            command_count - 1
        ].background;


    /*
     * Block SIGCHLD while creating the pipeline.
     */
    if (block_sigchld(&old_mask) < 0)
    {
        return -1;
    }


    /*
     * =====================================================
     * CREATE PIPES
     * =====================================================
     */
    for (int i = 0;
         i < command_count - 1;
         i++)
    {
        if (pipe(pipes[i]) < 0)
        {
            perror("pipe");


            for (int j = 0; j < i; j++)
            {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }


            restore_sigchld(&old_mask);

            return -1;
        }
    }


    /*
     * =====================================================
     * CREATE CHILDREN
     * =====================================================
     */
    for (int i = 0;
         i < command_count;
         i++)
    {
        pid_t pid;

        command_t *cmd;


        cmd = &pipeline->commands[i];


        pid = fork();


        if (pid < 0)
        {
            perror("fork");


            /*
             * Close pipes.
             */
            for (int j = 0;
                 j < command_count - 1;
                 j++)
            {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }


            /*
             * Wait for already-created children
             * when this is a foreground pipeline.
             */
            if (!background)
            {
                for (int j = 0;
                     j < i;
                     j++)
                {
                    waitpid(
                        pids[j],
                        NULL,
                        0
                    );
                }
            }


            restore_sigchld(&old_mask);

            return -1;
        }


        /*
         * =================================================
         * CHILD
         * =================================================
         */
        if (pid == 0)
        {
            /*
             * Restore normal signal mask.
             */
            restore_sigchld(&old_mask);


            /*
             * ---------------------------------------------
             * INPUT FROM PREVIOUS PIPE
             * ---------------------------------------------
             */
            if (i > 0)
            {
                if (dup2(
                        pipes[i - 1][0],
                        STDIN_FILENO) < 0)
                {
                    perror("dup2");

                    _exit(1);
                }
            }


            /*
             * ---------------------------------------------
             * OUTPUT TO NEXT PIPE
             * ---------------------------------------------
             */
            if (i < command_count - 1)
            {
                if (dup2(
                        pipes[i][1],
                        STDOUT_FILENO) < 0)
                {
                    perror("dup2");

                    _exit(1);
                }
            }


            /*
             * ---------------------------------------------
             * BACKGROUND PIPELINE STDIN
             * ---------------------------------------------
             *
             * Only the FIRST command needs terminal input.
             *
             * Example:
             *
             *     cat | grep abc &
             *
             * cat gets /dev/null.
             *
             * grep gets input from cat through the pipe.
             */
            if (background &&
                i == 0 &&
                cmd->input == NULL)
            {
                if (redirect_background_stdin() < 0)
                {
                    _exit(1);
                }
            }


            /*
             * ---------------------------------------------
             * EXPLICIT REDIRECTION
             * ---------------------------------------------
             */
            if (apply_redirections(cmd) < 0)
            {
                _exit(1);
            }


            /*
             * ---------------------------------------------
             * CLOSE ALL PIPE DESCRIPTORS
             * ---------------------------------------------
             */
            for (int j = 0;
                 j < command_count - 1;
                 j++)
            {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }


            /*
             * ---------------------------------------------
             * BUILTIN IN PIPELINE
             * ---------------------------------------------
             *
             * Pipeline builtins execute in a child.
             */
            if (is_builtin(cmd))
            {
                int result;

                result = execute_builtin(cmd);

                if (result < 0)
                {
                    _exit(1);
                }

                _exit(0);
            }


            /*
             * ---------------------------------------------
             * EXTERNAL COMMAND
             * ---------------------------------------------
             */
            execvp(
                cmd->argv[0],
                cmd->argv
            );


            /*
             * execvp() failed.
             */
            perror("execvp");

            _exit(127);
        }


        /*
         * Parent stores PID.
         */
        pids[i] = pid;
    }


    /*
     * =====================================================
     * PARENT CLOSES ALL PIPE DESCRIPTORS
     * =====================================================
     */
    for (int i = 0;
         i < command_count - 1;
         i++)
    {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }


    /*
     * =====================================================
     * BACKGROUND PIPELINE
     * =====================================================
     */
    if (background)
    {
        /*
         * Print PID of first process.
         */
        printf(
            "[Background Pipeline PID: %d]\n",
            (int)pids[0]
        );

        fflush(stdout);


        /*
         * Do NOT wait.
         */
        restore_sigchld(&old_mask);

        return 0;
    }


    /*
     * =====================================================
     * FOREGROUND PIPELINE
     * =====================================================
     */
    for (int i = 0;
         i < command_count;
         i++)
    {
        int status;


        while (waitpid(
                   pids[i],
                   &status,
                   0) < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }

            perror("waitpid");

            restore_sigchld(&old_mask);

            return -1;
        }


        /*
         * Save only the status of the last command.
         */
        if (i == command_count - 1)
        {
            if (WIFEXITED(status))
            {
                last_status =
                    WEXITSTATUS(status);
            }
            else if (WIFSIGNALED(status))
            {
                last_status =
                    128 + WTERMSIG(status);
            }
        }
    }


    /*
     * Restore SIGCHLD handling.
     */
    restore_sigchld(&old_mask);


    return last_status;
}
