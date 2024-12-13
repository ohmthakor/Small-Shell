#include <unistd.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <signal.h>
#include <stdbool.h>

#define MAX_BG_PROCESSES 512
#define MAX_ARGS 512

// Data structure to hold information about a shell command
typedef struct {
    char *name;
    char *args[MAX_ARGS + 1];
    char *input_file;
    char *output_file;
    bool background;
} Command;

// Global variables for foreground-only mode, background process IDs, and signal handling
static bool fgOnly = false; // Foreground-only mode flag
static pid_t processes[MAX_BG_PROCESSES]; // Array of background process IDs
struct sigaction sa_SIGTSTP, sa_SIGINT; // Signal action structs for SIGTSTP and SIGINT
pid_t fgpid = -1; // PID of the current foreground process

// Function prototypes
void shell();
void populateCommand(Command *cmd, char commandLine[2048]);
void exitShell();
void cd(char *args[MAX_ARGS + 1]);
int execCMD(Command *cmd);
int expandPID(Command *cmd);
int redirect(const Command *cmd);
void reap();
int addbg(pid_t pid);
void initprocesses();
void handleSIGINT(int sig);
void handleSIGTSTP(int sig);
void initSignalHandlers();

// Main function for the shell program
void shell() {
    char commandLine[2048];
    Command cmd;
    int exitStatus = 0;

    // Initialize processes and signal handlers
    initprocesses();
    initSignalHandlers();

    while (true) {
        printf(":"); // Shell prompt
        fflush(stdout);

        // Read the command line
        if (fgets(commandLine, sizeof(commandLine), stdin) == NULL) {
            perror("Error reading input");
            break;
        }

        // Remove newline character from the end of the input
        size_t length = strlen(commandLine);
        if (length > 0 && commandLine[length - 1] == '\n') {
            commandLine[length - 1] = '\0';
        }

        // Populate command struct and expand $$ with PID
        populateCommand(&cmd, commandLine);
        expandPID(&cmd);

        // Execute built-in or external commands
        if (cmd.name == NULL) { // Ignore empty or comment-only lines
            reap(); // Clean up completed background processes
            continue;
        } else if (strcmp(cmd.name, "exit") == 0) { // Exit command
            exitShell();
        } else if (strcmp(cmd.name, "cd") == 0) { // Change directory command
            cd(cmd.args);
        } else if (strcmp(cmd.name, "status") == 0) { // Status command
            if (exitStatus != 0 && exitStatus != 1) {
                printf("terminated by signal %d\n", exitStatus);
            } else {
                printf("exit value %d\n", exitStatus);
            }
        } else {
            exitStatus = execCMD(&cmd); // Execute external command
        }

        reap(); // Clean up completed background processes
    }
}

// Parse command line input and populate the Command struct
void populateCommand(Command *cmd, char commandLine[2048]) {
    char *token;
    int argIndex = 0;

    cmd->name = NULL;
    cmd->input_file = NULL;
    cmd->output_file = NULL;
    cmd->background = false;

    // Skip comments
    if (commandLine[0] == '#') {
        return;
    }

    // Tokenize command line to extract command and arguments
    token = strtok(commandLine, " ");
    if (token) {
        cmd->name = strdup(token); // Store command name
        cmd->args[argIndex++] = cmd->name; // First argument is the command itself
    } else {
        return;
    }

    // Parse arguments and I/O redirection
    while ((token = strtok(NULL, " ")) != NULL) {
        if (strcmp("<", token) == 0) { // Input file redirection
            token = strtok(NULL, " ");
            if (token) cmd->input_file = strdup(token);
        } else if (strcmp(">", token) == 0) { // Output file redirection
            token = strtok(NULL, " ");
            if (token) cmd->output_file = strdup(token);
        } else {
            cmd->args[argIndex++] = strdup(token); // Add argument to args array
        }
    }

    // Check for background process indication with '&'
    if (argIndex > 0 && strcmp(cmd->args[argIndex - 1], "&") == 0) {
        cmd->background = true;
        free(cmd->args[--argIndex]); // Remove '&' from args array
        cmd->args[argIndex] = NULL;
    } else {
        cmd->args[argIndex] = NULL;
    }
}

// Exit the shell, terminating all child processes
void exitShell() {
    int i, exitStatus = 0, wstatus;

    // Terminate foreground process if running
    if (fgpid != -1) {
        kill(fgpid, SIGTERM);
        waitpid(fgpid, &wstatus, 0);
        fgpid = -1;
    }

    // Terminate all background processes
    for (i = 0; i < MAX_BG_PROCESSES; i++) {
        if (processes[i] != -1) {
            kill(processes[i], SIGTERM);
            waitpid(processes[i], &wstatus, 0);
            processes[i] = -1;
        }
    }

    exit(exitStatus);
}

// Change directory command
void cd(char *args[MAX_ARGS + 1]) {
    int result;
    if (args[1] != NULL) {
        result = chdir(args[1]);
    } else {
        char *home_dir = getenv("HOME");
        result = chdir(home_dir);
    }

    if (result != 0) {
        perror("cd");
    }
}

// Execute external commands
int execCMD(Command *cmd) {
    pid_t pid = fork(); // Create child process
    int wstatus, options = 0;

    if (pid == -1) {
        perror("fork failed");
        return 1;
    }

    if (pid == 0) { // Child process
        // Redirect I/O if specified, and exit if redirect fails
        if (redirect(cmd) == -1) {
            exit(1); // Exit if redirection fails
        }

        if (fgOnly) { // Foreground-only mode check
            cmd->background = false;
        }

        // Set SIGINT handling based on foreground vs. background
        sa_SIGINT.sa_handler = (!cmd->background) ? handleSIGINT : SIG_IGN;
        sigaction(SIGINT, &sa_SIGINT, NULL);

        // Redirect stdin/stdout to /dev/null for background processes with no I/O redirection
        if (cmd->background) {
            if (!cmd->input_file) {
                int devNull = open("/dev/null", O_RDONLY);
                dup2(devNull, STDIN_FILENO);
                close(devNull);
            }
            if (!cmd->output_file) {
                int devNull = open("/dev/null", O_WRONLY);
                dup2(devNull, STDOUT_FILENO);
                close(devNull);
            }
        }

        execvp(cmd->name, cmd->args); // Execute command
        perror("exec");
        exit(1);
    } else { // Parent process
        if (cmd->background && !fgOnly) { // Background process handling
            printf("background pid is %d\n", pid);
            addbg(pid);
        } else { // Foreground process handling
            fgpid = pid;
            waitpid(pid, &wstatus, options);
            fgpid = -1;

            // Retrieve exit status or signal that caused termination
            if (WIFEXITED(wstatus)) {
                return WEXITSTATUS(wstatus);
            } else if (WIFSIGNALED(wstatus)) {
                fprintf(stderr, "terminated by signal %d\n", WTERMSIG(wstatus));
                return WTERMSIG(wstatus);
            }
        }
    }
    return 0;
}

// Replace occurrences of $$ in arguments with the PID
int expandPID(Command *cmd) {
    char pid[10];
    int len = sprintf(pid, "%d", getpid());
    
    int curArg = 0;
    char *pos;
    
    while (curArg < MAX_ARGS && cmd->args[curArg] != NULL) {
        while ((pos = strstr(cmd->args[curArg], "$$")) != NULL) {
            int newLen = strlen(cmd->args[curArg]) + strlen(pid) - 2;
            char *newStr = malloc(newLen + 1);

            strncpy(newStr, cmd->args[curArg], pos - cmd->args[curArg]);
            strcat(newStr, pid);
            strcat(newStr, pos + 2);

            free(cmd->args[curArg]);
            cmd->args[curArg] = newStr;
        }
        curArg++;
    }

    return 0;
}

// Perform input/output redirection for commands
int redirect(const Command *cmd) {
    if (cmd->input_file) {
        int input = open(cmd->input_file, O_RDONLY);
        if (input == -1) { // Error opening the input file
            perror("Error opening input file");
            return -1; // Return error code if file cannot be opened
        }
        dup2(input, STDIN_FILENO);
        close(input);
    }

    if (cmd->output_file) {
        int output = open(cmd->output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (output == -1) { // Error opening the output file
            perror("Error opening output file");
            return -1; // Return error code if file cannot be opened
        }
        dup2(output, STDOUT_FILENO);
        close(output);
    }

    return 0; // Success
}

// Add a background process to the list of tracked PIDs
int addbg(pid_t pid) {
    int i;
    for (i = 0; i < MAX_BG_PROCESSES; i++) {
        if (processes[i] == -1) {
            processes[i] = pid;
            return 0;
        }
    }
    return -1;
}

// Reap completed background processes
void reap() {
    int wstatus;
    pid_t pid;
    int i;
    
    for (i = 0; i < MAX_BG_PROCESSES; i++) {
        if (processes[i] != -1) {
            pid = waitpid(processes[i], &wstatus, WNOHANG);
            if (pid > 0) {
                printf("background pid %d is done: exit value %d\n", pid, WEXITSTATUS(wstatus));
                processes[i] = -1;
            }
        }
    }
}

// Initialize background process list with -1 (no process)
void initprocesses() {
    int i;
    for (i = 0; i < MAX_BG_PROCESSES; i++) {
        processes[i] = -1;
    }
}

// Handle SIGINT (Ctrl+C) signal in foreground processes
void handleSIGINT(int sig) {
    write(STDOUT_FILENO, "terminated by signal 2\n", 23);
}

// Handle SIGTSTP (Ctrl+Z) signal to toggle foreground-only mode
void handleSIGTSTP(int sig) {
    fgOnly = !fgOnly;
    const char *msg = fgOnly ? "Entering foreground-only mode (& is now ignored)\n" 
                             : "Exiting foreground-only mode\n";
    write(STDOUT_FILENO, msg, strlen(msg));
}

// Set up signal handlers for SIGINT and SIGTSTP
void initSignalHandlers() {
    sa_SIGTSTP.sa_handler = handleSIGTSTP;
    sigfillset(&sa_SIGTSTP.sa_mask);
    sa_SIGTSTP.sa_flags = SA_RESTART;
    sigaction(SIGTSTP, &sa_SIGTSTP, NULL);

    sa_SIGINT.sa_handler = SIG_IGN; // Ignore SIGINT initially
    sigfillset(&sa_SIGINT.sa_mask);
    sa_SIGINT.sa_flags = SA_RESTART;
    sigaction(SIGINT, &sa_SIGINT, NULL);
}

// Main entry point
int main() {
    shell();
    return 0;
}
