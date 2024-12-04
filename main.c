#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <stdatomic.h>

uint32_t random_state;

// COMPILE: clang -O3 -march=native -flto -fomit-frame-pointer -o nqueens_pgo_gen main.c && ./nqueens_pgo_gen

// Board represents the N-Queens board
typedef struct {
    int n;
    int *queens;
    atomic_int *rowConflicts;
    atomic_int *diag1Conflicts;
    atomic_int *diag2Conflicts;
} Board;

// Function to compute absolute value
int abs_int(int x) { return x < 0 ? -x : x; }


uint32_t xorshift(){
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    random_state ^= random_state << 5;
    return random_state;
}



// Initialize the board with a random placement
Board *NewBoard(int n) {
    Board *board = (Board *)malloc(sizeof(Board));
    board->n = n;

    board->queens = (int *)malloc(n * sizeof(int));
    board->rowConflicts = (atomic_int *)calloc(n, sizeof(atomic_int));
    board->diag1Conflicts = (atomic_int *)calloc(2 * n, sizeof(atomic_int));
    board->diag2Conflicts = (atomic_int *)calloc(2 * n, sizeof(atomic_int));
    
    for (int i = 0; i < n; i++){
        atomic_store(&board->rowConflicts[i],0);
    }

    for (int i = 0; i < 2*n; i++){
        atomic_store(&board->diag1Conflicts[i],0);
        atomic_store(&board->diag2Conflicts[i],0);
    }

    for (int col = 0; col < n; col++) {
        int row = xorshift() % board->n;
        board->queens[col] = row;
        board->rowConflicts[row]++;
        board->diag1Conflicts[row - col + board->n]++;
        board->diag2Conflicts[row + col]++;
    }

    return board;
}

// Deletes Board
void DeleteBoard(Board *board){
    if (board == NULL){
        return;
    }
    free(board->queens);
    free(board->rowConflicts);
    free(board->diag1Conflicts);
    free(board->diag2Conflicts);
    free(board);
}




// Check if the queen at the given column has any conflicts
int HasConflict(Board *b, int col) {
    int row = b->queens[col];
    // if (atomic_load(&b->rowConflicts[row]) > 1 || atomic_load(&b->diag1Conflicts[row - col + b->n]) > 1 ||
    //     atomic_load(&b->diag2Conflicts[row + col]) > 1) {
    //     return 1; // true
    // }
    // return 0; // false

    if (b->rowConflicts[row] > 1 || b->diag1Conflicts[row - col + b->n] > 1 ||
        b->diag2Conflicts[row + col] > 1) {
        return 1; // true
    }
    return 0; // false
}

// Update the queen's position in the board
void UpdateQueen(Board *b, int col, int newRow) {
    

    int oldRow = b->queens[col];
    if (oldRow == newRow) {
        return;
    }

    // Remove old conflicts
    atomic_fetch_sub(&b->rowConflicts[oldRow], 1);
    atomic_fetch_sub(&b->diag1Conflicts[oldRow - col + b->n],1);
    atomic_fetch_sub(&b->diag2Conflicts[oldRow + col],1);

    // Place queen at new position
    b->queens[col] = newRow;

    // Add new conflicts
    atomic_fetch_add(&b->rowConflicts[newRow],1);
    atomic_fetch_add(&b->diag1Conflicts[newRow - col + b->n],1);
    atomic_fetch_add(&b->diag2Conflicts[newRow + col],1);
}

// Minimize conflicts for queens in the given columns
void MinimizeConflicts(Board *b, int *cols, int numCols) {

    int *bestRows = (int *)malloc(b->n * sizeof(int));

    for (int idx = 0; idx < numCols; idx++) {
        int col = cols[idx];

        
        if (!HasConflict(b, col)) {
            continue;
        }

        
        int numBestRows = 0;
        int minConflicts = b->n; // Maximum possible conflicts per queen
        int diag1 = -col + b->n;


        // Find all rows with the minimum number of conflicts
        for (int row = 0; row < b->n; row++) {
            int conflicts = b->rowConflicts[row] + \
                            b->diag1Conflicts[row + diag1] + \
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
        int newRow = bestRows[xorshift() % numBestRows];

        UpdateQueen(b, col, newRow);
    }
    free(bestRows);
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
} ThreadData;

// Thread function to minimize conflicts
void *MinimizeConflictsThread(void *arg) {
    ThreadData *data = (ThreadData *)arg;
    MinimizeConflicts(data->board, data->cols, data->numCols);
    return NULL;
}

// Solve the N-Queens problem using an optimized parallel Min-Conflicts
// algorithm
double SolveParallel(int n, int maxSteps) {

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    Board *board = NewBoard(n);
    int step = 0;
    int numCPU = sysconf(_SC_NPROCESSORS_ONLN);

    if (numCPU < 1) {
        numCPU = 1;
    }


    int *conflictCols = (int *)malloc(n * sizeof(int));

    //EXPEPRIMENTAL
    pthread_t *threadPool = (pthread_t *)malloc(numCPU*sizeof(pthread_t));
    ThreadData *threadDataPool = (ThreadData *)malloc(numCPU*sizeof(ThreadData));


    while (step < maxSteps) {
        
        int numConflicts = 0;

        // Collect columns with conflicts
        for (int col = 0; col < n; col++) {
            if (HasConflict(board, col)) {
                conflictCols[numConflicts++] = col;
            }
        }

        if (numConflicts == 0) {


            //Display Time Taken
            clock_gettime(CLOCK_MONOTONIC, &end);
            double duration = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_sec) / 1e9;
            printf(" -- Solution found in %.3f seconds \n", duration);
            printf(" -- Solution found in %d (%d) steps \n", step, step*numCPU);

            //Validate Solution
            // if (ValidateSolution(board->queens, n)){
            //     printf(" -- Solution is valid!\n");
            // } else {printf(" -- ERROR: Invalid solution found\n");}

            DeleteBoard(board);
            free(conflictCols);
            free(threadPool);
            free(threadDataPool);
            return duration; // Solution found
        }

        // Shuffle conflict columns to randomize processing order
        for (int i = numConflicts - 1; i > 0; i--) {
            int j = xorshift() % (i + 1);
            int temp = conflictCols[i];
            conflictCols[i] = conflictCols[j];
            conflictCols[j] = temp;
        }

        // Divide conflict columns among workers
        int chunkSize = (numConflicts + numCPU - 1) / numCPU;

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

            threadDataPool[i].board = board;
            threadDataPool[i].cols = &conflictCols[start];
            threadDataPool[i].numCols = end - start;
            
            pthread_create(&threadPool[i], NULL, MinimizeConflictsThread, &threadDataPool[i]);
            numThreads++;
        }

        // Wait for all workers to finish
        for (int i = 0; i < numThreads; i++) {
            pthread_join(threadPool[i], NULL);
        }

        step++;
    }

    free(conflictCols);
    free(threadPool);
    free(threadDataPool);

    DeleteBoard(board);
    printf(" -- ERROR Solution NOT found in %d (%d) steps\n", step, step*numCPU);
    return 0; // No solution found
}



int main() {

    
    int boardSizes[] = {100000};
    int testQuantity = 20;
    random_state = (uint32_t)time(NULL);
    int numSizes = sizeof(boardSizes) / sizeof(boardSizes[0]);

    for (int idx = 0; idx < numSizes; idx++) {

        int n = boardSizes[idx];
        int maxSteps = n * 10;

        double total_time = 0;
        for (int x = 0; x < testQuantity; x++){
            total_time += SolveParallel(n, maxSteps);
        }
        
        printf("\n\n AVERAGE FOR %d:  %.3f s\n\n\n", n, total_time/testQuantity);
    }
    return 0;
}