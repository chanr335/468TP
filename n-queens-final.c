#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <stdatomic.h>

uint32_t random_state;

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

// Function to find next random state
uint32_t xorshift(){
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    random_state ^= random_state << 5;
    return random_state;
}



// Initialize the board with a random placement
Board *NewBoard(int n) {

    //Setup Variables
    Board *board = (Board *)malloc(sizeof(Board));
    board->n = n;
    board->queens = (int *)malloc(n * sizeof(int));
    board->rowConflicts = (atomic_int *)calloc(n, sizeof(atomic_int));
    board->diag1Conflicts = (atomic_int *)calloc(2 * n, sizeof(atomic_int));
    board->diag2Conflicts = (atomic_int *)calloc(2 * n, sizeof(atomic_int));
    
    //Atomicize Them
    for (int i = 0; i < n; i++){
        atomic_store(&board->rowConflicts[i],0);
    }
    for (int i = 0; i < 2*n; i++){
        atomic_store(&board->diag1Conflicts[i],0);
        atomic_store(&board->diag2Conflicts[i],0);
    }

    //Fill Queens With Initial Random State
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
    if (board == NULL){return;}
    free(board->queens);
    free(board->rowConflicts);
    free(board->diag1Conflicts);
    free(board->diag2Conflicts);
    free(board);
}




// Check If Queen At Given Column Has Any Conflicts
int HasConflict(Board *b, int col) {

    //Find The Row This Queen Is In
    int row = b->queens[col];

    //Determine If Conflict Exists
    if (b->rowConflicts[row] > 1 || b->diag1Conflicts[row - col + b->n] > 1 ||
        b->diag2Conflicts[row + col] > 1) {
        return 1; // true
    }
    return 0; // false
}

// Update the queen's position in the board
void UpdateQueen(Board *b, int col, int newRow) {
    
    //Find Old Row
    int oldRow = b->queens[col];

    //If No Movement, Return
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

// Find Minimum Conflict Location for Queens in the given columns
void MinimizeConflicts(Board *b, int *cols, int numCols) {

    //Storing the Best Row Indexs
    int *bestRows = (int *)malloc(b->n * sizeof(int));

    //For every Row
    for (int idx = 0; idx < numCols; idx++) {
        int col = cols[idx];

        //If No Conflicts - Do Nothing
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
            //New Best - Replace
            if (conflicts < minConflicts) {
                bestRows[0] = row;
                numBestRows = 1;
                minConflicts = conflicts;
            //Tied Best - Add To List
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
        //Check Values Within Range
        if ((queens[i] > n) || (queens[i] < 0)){
            return 0;
        }
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

//Prints Solution Board Into Named .txt file
void PrintSolutionToFile(int *queens, int n, int run, int total){
    char filename[100];
    sprintf(filename, "Solution_%d_%dof%d.txt",n,run,total);
    
    FILE *fp = fopen(filename, "w");

    for (int row = 0; row < n; row++){
        for (int col = 0; col < n; col++){
            if (queens[col] == row){
                fprintf(fp, "Q ");
            } else {
                fprintf(fp, ". ");
            }
        }
        fprintf(fp, "\n");
    }
    fclose(fp);
}




// Solve the N-Queens problem using an optimized parallel Min-Conflicts
// algorithm
double SolveParallel(int n, int maxSteps, int numCPU, int run_num, int run_total) {

    //Store REAL time counts
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    //Create Board
    Board *board = NewBoard(n);

    //Setup Memory And Threads
    int step = 0;
    int *conflictCols = (int *)malloc(n * sizeof(int));
    pthread_t *threadPool = (pthread_t *)malloc(numCPU*sizeof(pthread_t));
    ThreadData *threadDataPool = (ThreadData *)malloc(numCPU*sizeof(ThreadData));


    //While Within Valid Step
    while (step < maxSteps) {
        
        int numConflicts = 0;

        // Collect columns with conflicts
        for (int col = 0; col < n; col++) {
            if (HasConflict(board, col)) {
                conflictCols[numConflicts++] = col;
            }
        }

        //If Solved
        if (numConflicts == 0) {

            //Display Time Taken
            clock_gettime(CLOCK_MONOTONIC, &end);
            double duration = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_sec) / 1e9;
            printf(" -- Solution found in %.3f seconds \n", duration);
            printf(" -- Solution found in %d (%d) sets of steps \n", step, step*numCPU);

            //Validate Solution
            if (ValidateSolution(board->queens, n)){
                printf(" -- Solution is valid!\n\n");

                //Print Solution To File
                PrintSolutionToFile(board->queens, n, run_num, run_total);
            } else {printf(" -- ERROR: Invalid solution found\n");}

            //Delete Board and memory
            DeleteBoard(board);
            free(conflictCols);
            free(threadPool);
            free(threadDataPool);
            return duration;
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

        //Allocate Work For Threads
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
            //Add Work To Threads
            threadDataPool[i].board = board;
            threadDataPool[i].cols = &conflictCols[start];
            threadDataPool[i].numCols = end - start;
            
            //Set Them Going
            pthread_create(&threadPool[i], NULL, MinimizeConflictsThread, &threadDataPool[i]);
            numThreads++;
        }

        // Wait for all workers to finish
        for (int i = 0; i < numThreads; i++) {
            pthread_join(threadPool[i], NULL);
        }

        step++;
    }
    //Free board and memory
    free(conflictCols);
    free(threadPool);
    free(threadDataPool);
    DeleteBoard(board);
    printf(" -- ERROR Solution NOT found in %d sets of steps\n", step);
    return 0; // No solution found
}



int main() {

    int boardSizes[] = {100};
    int testQuantity = 5;
    random_state = (uint32_t)time(NULL); //Based on current time, SO UNIQUE

    int numCPU = sysconf(_SC_NPROCESSORS_ONLN);
    
    if (numCPU < 1) {
        numCPU = 1;
    }
    int numSizes = sizeof(boardSizes) / sizeof(boardSizes[0]);

    //For Every Test Size
    for (int idx = 0; idx < numSizes; idx++) {

        int n = boardSizes[idx];
        int maxSteps = n * 10;
        printf("Starting Tests Of Size %d\n", n);
        double total_time = 0;

        //Run Quantity of Tests
        for (int x = 0; x < testQuantity; x++){
            total_time += SolveParallel(n, maxSteps, numCPU, x, testQuantity);
        }
        //Return Average
        printf("\n\n AVERAGE FOR %d RANDOM n=%d BOARD:  %.3f s\n\n\n", testQuantity, n, total_time/testQuantity);
    }
    return 0;
}
