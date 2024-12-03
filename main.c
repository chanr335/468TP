#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

// COMPILE: clang -O3 -march=native -flto -fomit-frame-pointer -o nqueens_pgo_gen main.c && ./nqueens_pgo_gen

// Board represents the N-Queens board
typedef struct {
    int n;
    int *queens;
    int *rowConflicts;
    int *diag1Conflicts;
    int *diag2Conflicts;
    pthread_mutex_t mu; // Mutex for synchronizing access to shared data
} Board;

// Function to compute absolute value
int abs_int(int x) { return x < 0 ? -x : x; }

// Initialize the board with a random placement
Board *NewBoard(int n) {
    Board *board = (Board *)malloc(sizeof(Board));
    board->n = n;
    board->queens = (int *)malloc(n * sizeof(int));
    board->rowConflicts = (int *)calloc(n, sizeof(int));
    board->diag1Conflicts = (int *)calloc(2 * n, sizeof(int));
    board->diag2Conflicts = (int *)calloc(2 * n, sizeof(int));
    pthread_mutex_init(&(board->mu), NULL);

    for (int col = 0; col < n; col++) {
        int row = rand() % board->n;
        board->queens[col] = row;
        board->rowConflicts[row]++;
        board->diag1Conflicts[row - col + board->n]++;
        board->diag2Conflicts[row + col]++;
    }

    return board;
}

// Check if the queen at the given column has any conflicts
int HasConflict(Board *b, int col) {
    // Assume the lock is already held
    int row = b->queens[col];
    if (b->rowConflicts[row] > 1 || b->diag1Conflicts[row - col + b->n] > 1 ||
        b->diag2Conflicts[row + col] > 1) {
        return 1; // true
    }
    return 0; // false
}

// Update the queen's position in the board
void UpdateQueen(Board *b, int col, int newRow) {
    // Assume the lock is already held
    int oldRow = b->queens[col];
    if (oldRow == newRow) {
        return;
    }

    // Remove old conflicts
    b->rowConflicts[oldRow]--;
    b->diag1Conflicts[oldRow - col + b->n]--;
    b->diag2Conflicts[oldRow + col]--;

    // Place queen at new position
    b->queens[col] = newRow;

    // Add new conflicts
    b->rowConflicts[newRow]++;
    b->diag1Conflicts[newRow - col + b->n]++;
    b->diag2Conflicts[newRow + col]++;
}

// Minimize conflicts for queens in the given columns
void MinimizeConflicts(Board *b, int *cols, int numCols, unsigned int *seed) {
    for (int idx = 0; idx < numCols; idx++) {
        int col = cols[idx];

        pthread_mutex_lock(&(b->mu));

        if (!HasConflict(b, col)) {
            pthread_mutex_unlock(&(b->mu));
            continue;
        }

        int *bestRows = (int *)malloc(b->n * sizeof(int));
        int numBestRows = 0;
        int minConflicts = b->n; // Maximum possible conflicts per queen

        int diag1 = -col + b->n;

        // Find all rows with the minimum number of conflicts

        for (int row = 0; row < b->queens[col]; row++) {
            int conflicts = b->rowConflicts[row] + b->diag1Conflicts[row + diag1] +
                b->diag2Conflicts[row + col];
            if (conflicts < minConflicts) {
                bestRows[0] = row;
                numBestRows = 1;
                minConflicts = conflicts;
            } else if (conflicts == minConflicts) {
                bestRows[numBestRows++] = row;
            }
        }

        for (int row = b->queens[col] + 1; row < b->n; row++) {
            int conflicts = b->rowConflicts[row] + b->diag1Conflicts[row + diag1] +
                b->diag2Conflicts[row + col];
            if (conflicts < minConflicts) {
                bestRows[0] = row;
                numBestRows = 1;
                minConflicts = conflicts;
            } else if (conflicts == minConflicts) {
                bestRows[numBestRows++] = row;
            }
        }

        // Randomly select one of the best rows to diversify moves
        int newRow = bestRows[rand_r(seed) % numBestRows];
        free(bestRows);

        UpdateQueen(b, col, newRow);

        pthread_mutex_unlock(&(b->mu));
    }
}

// Validate if the solution is valid (no queens attack each other)
int ValidateSolution(int *queens, int n) {
    for (int i = 0; i < n; i++) {
        for (int j = i + 1; j < n; j++) {
            // Check row conflicts
            if (queens[i] == queens[j]) {
                return 0; // false
            }
            // Check diagonal conflicts
            if (abs_int(i - j) == abs_int(queens[i] - queens[j])) {
                return 0; // false
            }
        }
    }
    return 1; // true
}

// Structure to pass data to threads
typedef struct {
    Board *board;
    int *cols;
    int numCols;
    unsigned int seed;
} ThreadData;

// Thread function to minimize conflicts
void *MinimizeConflictsThread(void *arg) {
    ThreadData *data = (ThreadData *)arg;
    MinimizeConflicts(data->board, data->cols, data->numCols, &(data->seed));
    return NULL;
}

// Solve the N-Queens problem using an optimized parallel Min-Conflicts
// algorithm
int *SolveParallel(int n, int maxSteps, int *success) {
    unsigned int globalSeed = 12345; // Replace 12345 with any constant value
    Board *board = NewBoard(n);
    int step = 0;
    int numCPU = sysconf(_SC_NPROCESSORS_ONLN);

    if (numCPU < 1) {
        numCPU = 1;
    }

    while (step < maxSteps) {
        int *conflictCols = (int *)malloc(n * sizeof(int));
        int numConflicts = 0;

        // Collect columns with conflicts
        pthread_mutex_lock(&(board->mu));
        for (int col = 0; col < n; col++) {
            if (HasConflict(board, col)) {
                conflictCols[numConflicts++] = col;
            }
        }
        pthread_mutex_unlock(&(board->mu));

        if (numConflicts == 0) {
            *success = 1;
            int *solution = (int *)malloc(n * sizeof(int));
            for (int i = 0; i < n; i++) {
                solution[i] = board->queens[i];
            }
            free(conflictCols);
            free(board->queens);
            free(board->rowConflicts);
            free(board->diag1Conflicts);
            free(board->diag2Conflicts);
            pthread_mutex_destroy(&(board->mu));
            free(board);
            printf("number of steps: %d \n", step * numCPU);
            return solution; // Solution found
        }

        // Shuffle conflict columns to randomize processing order
        for (int i = numConflicts - 1; i > 0; i--) {
            int j = rand_r(&globalSeed) % (i + 1);
            int temp = conflictCols[i];
            conflictCols[i] = conflictCols[j];
            conflictCols[j] = temp;
        }

        // Divide conflict columns among workers
        int chunkSize = (numConflicts + numCPU - 1) / numCPU;
        pthread_t *threads = (pthread_t *)malloc(numCPU * sizeof(pthread_t));
        ThreadData *threadData = (ThreadData *)malloc(numCPU * sizeof(ThreadData));
        int numThreads = 0;
        for (int i = 0; i < numCPU; i++) {
            int start = i * chunkSize;
            int end = start + chunkSize;
            if (end > numConflicts) {
                end = numConflicts;
            }
            if (start >= end) {
                break;
            }

            threadData[i].board = board;
            threadData[i].cols = &conflictCols[start];
            threadData[i].numCols = end - start;
            threadData[i].seed = (unsigned int)time(NULL) + i;

            pthread_create(&threads[i], NULL, MinimizeConflictsThread,
                           &threadData[i]);
            numThreads++;
        }

        // Wait for all workers to finish
        for (int i = 0; i < numThreads; i++) {
            pthread_join(threads[i], NULL);
        }

        free(threads);
        free(threadData);
        free(conflictCols);
        step++;
    }

    *success = 0;
    free(board->queens);
    free(board->rowConflicts);
    free(board->diag1Conflicts);
    free(board->diag2Conflicts);
    pthread_mutex_destroy(&(board->mu));
    free(board);
    return NULL; // No solution found within maxSteps
}

int main() {
    // Initialize random seed
    srand(time(NULL));

    // Range of board sizes to test
    int boardSizes[] = {100, 1000, 10000, 100000, 1000000};
    int numSizes = sizeof(boardSizes) / sizeof(boardSizes[0]);

    for (int idx = 0; idx < numSizes; idx++) {
        int n = boardSizes[idx];
        printf("Solving N-Queens for n = %d...\n", n);

        int maxSteps = n * 10; // Adjust maxSteps based on n
        clock_t startTime = clock();

        int success;
        int *sol = SolveParallel(n, maxSteps, &success);
        clock_t endTime = clock();
        double duration = (double)(endTime - startTime) / CLOCKS_PER_SEC;

        if (success) {
            printf("Solution found in %f seconds \n", duration);

            // Validate the solution
            if (ValidateSolution(sol, n)) {
                printf("Solution is valid!\n");
            } else {
                printf("ERROR: Invalid solution found\n");
            }
            free(sol);
        } else {
            printf("No solution found in %f seconds\n", duration);
        }
        printf("---\n");
    }

    return 0;
}
