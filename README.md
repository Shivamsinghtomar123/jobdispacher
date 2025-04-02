# Job Dispatcher System

This program implements a job dispatching system where a main process distributes jobs to specialized worker processes based on job type. The system uses pipes for inter-process communication and the `select` system call for efficient communication management.

## Building the Code

The project includes a Makefile for easy building. Simply run:

```bash
make build
```

This will compile the code and create an executable named `job_dispatcher`.

## Running the Program

The program reads input from stdin in two phases:

1. Worker configuration (5 lines)
2. Job information (until EOF)

You can run the program in several ways:

### Method 1: Direct Input

```bash
./job_dispatcher
```

Then enter the input manually, for example:

```
1 1
2 3
3 1
4 1
5 1
1 3
2 5
3 1
```

Press Ctrl+D to signal EOF after entering all jobs.

### Method 2: Using Input File

```bash
./job_dispatcher < input.txt
```

Where `input.txt` contains the worker configuration followed by job information.

### Method 3: Using Echo and Pipe

```bash
echo -e "1 1\n2 3\n3 1\n4 1\n5 1\n1 3\n2 5\n3 1" | ./job_dispatcher
```

## Example Input

```
1 1
2 3
3 1
4 1
5 1
1 3
2 5
1 2
4 7
3 1
5 1
1 5
```

This input configures:
- 1 worker of type 1
- 3 workers of type 2
- 1 worker of type 3
- 1 worker of type 4
- 1 worker of type 5

And then sends various jobs to be processed.

## Testing

To thoroughly test the program, create different input scenarios:

1. **Basic Functionality**: Test with a simple configuration and a few jobs.
2. **Load Balancing**: Test with many jobs of the same type to see if they're distributed among workers of that type.
3. **Concurrency**: Test with jobs of different durations to verify they're processed concurrently.
4. **Edge Cases**: Test with a high number of workers or jobs to ensure the system handles them correctly.

## Implementation Details

The program follows these key steps:

1. Reads worker configuration and creates the specified number of worker processes for each type.
2. Establishes bi-directional communication with each worker using pipes.
3. Uses the `select` system call to efficiently monitor worker status.
4. Distributes jobs to available workers of the matching type.
5. Workers simulate processing by sleeping for the specified duration.
6. After a job is completed, workers signal completion and become available for new jobs.
