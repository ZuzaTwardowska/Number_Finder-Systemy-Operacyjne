# Number Finder

---
## Table of Contents

- [Description](#description)
- [Technologies](#technologies)
- [Usage](#usage)

---
## Description

The main goal of this project was creating a tool, which will be used for indexing and searching for integers in text files on Linux. 
The program creates a seperate child process for each of the directories given as arguments. In those directories, two files are created:

- .numf_pid contains pid of the child process that is processing this directory, takes part in ensuring that only one process is working on each directory
- .numf_index contains information about found integers (integer, file with its path, offset)

The program uses multiple processes, threads and is controlled by signals and commands from the user.


---
## Technologies
- C
- Linux
---
## Usage

### Running the program in the console:
```
./numf [-r] [-m] [-M] [-i] dir1 dir2 ...
```
- r - recursive search of subdirectories
- [m; M] - range of searched values, [10; 1000] by default
- i - interval between automatic indexations in seconds, 600 by default
- dir1, dir2, ... - indexed directories

### Commands
- status - gets status (active/not active) of all indexing processes (child processes)
- index - forces all indexing processes to start indexing procedure if it is not already active
- query [n1] [n2] ... - main process creates a new thread, which searches all .numf_index files and gives information about files (with paths)
containing integers given as arguments of the query with their offsets
- exit (or SIGINT) - main process terminates child processes, awaits their termination and terminates the program
