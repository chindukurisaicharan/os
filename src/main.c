#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <readline/readline.h>

#include "../include/token.h"
#include "../include/lexer.h"
#include "../include/history.h"
#include "../include/parser.h"
#include "../include/expand.h"

int main(void)
{
    char *input;

    /* Initialize command history */
    history_init();

    printf("========================================\n");
    printf("             Shellforge\n");
    printf("      A Unix Style Shell written in C\n");
    printf("========================================\n");

    while (1)
    {
        /*
         * readline() provides:
         * UP ARROW   -> previous command
         * DOWN ARROW -> next command
         * LEFT/RIGHT -> move cursor
         * BACKSPACE  -> delete characters
         */
        input = readline("shellforge$ ");

        /* Ctrl+D / EOF */
        if (input == NULL)
        {
            printf("\n");
            break;
        }

        /* Ignore empty input */
        if (strlen(input) == 0)
        {
            free(input);
            continue;
        }

        /* Exit command */
        if (strcmp(input, "exit") == 0)
        {
            free(input);
            break;
        }

        /* History command */
        if (strcmp(input, "history") == 0)
        {
            history_show();
            free(input);
            continue;
        }

        /*
         * Store the command in history.
         * UP/DOWN arrow navigation is handled
         * by readline().
         */
        history_add_command(input);

        /*
         * ============================
         * LEXER
         * ============================
         */
        token_list list;

        lexer(input, &list);

        /*
         * Display generated tokens
         */
        token_print(&list);

        /*
         * ============================
         * PARSER
         * ============================
         */
        pipeline_t pipeline;

        if (parse(&list, &pipeline))
        {
            /*
             * ============================
             * EXPAND
             * ============================
             */
            expand_variables(&pipeline);

            /*
             * Display parsed pipeline
             */
            pipeline_print(&pipeline);

            /*
             * Free parser memory
             */
            pipeline_free(&pipeline);
        }

        free(input);
    }

    /* Free history memory */
    history_cleanup();

    printf("Exiting...\n");

    return 0;
}
