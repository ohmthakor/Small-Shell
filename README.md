# Small-Shell

## Overview

Smallsh is a custom-built shell program written in C that implements a subset of features found in well-known Unix/Linux shells, such as bash. This shell supports commands execution, input/output redirection, background processes, and signal handling. This project was developed as part of the CS 374 course at OSU.

## Features

Custom Prompt: Displays : as the command prompt.  
Built-in Commands: 
exit: Terminates the shell and all child processes.  
cd: Changes the working directory.  
status: Reports the exit status or terminating signal of the last foreground process.  
Command Execution: Executes external commands via the exec() family of functions.  
Input/Output Redirection: Redirects stdin and stdout using < and > operators.  
Background Processes:  
Supports running commands in the background using &.  
Manages background process IDs and cleans up completed processes.  
Signal Handling:  
Ignores SIGINT (Ctrl+C) in the shell itself but allows foreground child processes to terminate.  
Toggles between foreground-only and normal modes using SIGTSTP (Ctrl+Z).  
Variable Expansion: Expands $$ to the PID of the shell.  
