#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

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

// GENERATE NEXT RANDOM
uint32_t xorshift() {
  random_state ^= random_state << 13;
  random_state ^= random_state >> 17;
  random_state ^= random_state << 5;
  return random_state;
}

// Initialize the board with a random placement
Board *NewBoard(int n) {
  Board *board = (Board *)malloc(sizeof(Board));
  board->n = n;

  // Allocating memory for the board and its various arrays
  board->queens = (int *)malloc(n * sizeof(int));
  board->rowConflicts = (atomic_int *)calloc(n, sizeof(atomic_int));
  board->diag1Conflicts = (atomic_int *)calloc(2 * n, sizeof(atomic_int));
  board->diag2Conflicts = (atomic_int *)calloc(2 * n, sizeof(atomic_int));

  // Initializing atomic values
  for (int i = 0; i < n; i++)
    atomic_store(&board->rowConflicts[i], 0);
  for (int i = 0; i < 2 * n; i++) {
    atomic_store(&board->diag1Conflicts[i], 0);
    atomic_store(&board->diag2Conflicts[i], 0);
  }

  // Place Queen by column and add it to its coresponding row/diagonal
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
void DeleteBoard(Board *board) {
  if (board == NULL)
    return;
  free(board->queens);
  free(board->rowConflicts);
  free(board->diag1Conflicts);
  free(board->diag2Conflicts);
  free(board);
}

// Check if the queen at the given column has any conflicts
int HasConflict(Board *b, int col) {
  int row = b->queens[col];
  int diagonal1 = row - col + b->n;
  int diagonal2 = row + col;
  if (b->rowConflicts[row] > 1 || b->diag1Conflicts[diagonal1] > 1 ||
      b->diag2Conflicts[diagonal2] > 1) {
    return 1; // true
  }
  return 0; // false
}

// Update the queen's position in the board
void UpdateQueen(Board *b, int col, int newRow) {
  int oldRow = b->queens[col];
  if (oldRow == newRow)
    return;

  // Remove old conflicts
  atomic_fetch_sub(&b->rowConflicts[oldRow], 1);
  atomic_fetch_sub(&b->diag1Conflicts[oldRow - col + b->n], 1);
  atomic_fetch_sub(&b->diag2Conflicts[oldRow + col], 1);

  // Place queen at new position
  b->queens[col] = newRow;

  // Add new conflicts
  atomic_fetch_add(&b->rowConflicts[newRow], 1);
  atomic_fetch_add(&b->diag1Conflicts[newRow - col + b->n], 1);
  atomic_fetch_add(&b->diag2Conflicts[newRow + col], 1);
}

// Minimize conflicts for queens in the given columns
void MinimizeConflicts(Board *b, int *cols, int numCols) {
  // Array to keep track of potential best rows to place on
  int *bestRows = (int *)malloc(b->n * sizeof(int));

  for (int idx = 0; idx < numCols; idx++) {
    int col = cols[idx];
    // Skip if column has no conflicts
    if (!HasConflict(b, col)) {
      continue;
    }
    int numBestRows = 0;
    int minConflicts = b->n; // Maximum possible conflicts per queen
    int diag1 = -col + b->n;
    // Find all rows with the minimum number of conflicts
    for (int row = 0; row < b->n; row++) {
      int conflicts = b->rowConflicts[row] + b->diag1Conflicts[row + diag1] +
                      b->diag2Conflicts[row + col];
      // Restart bestRows
      if (conflicts < minConflicts) {
        bestRows[0] = row;
        numBestRows = 1;
        minConflicts = conflicts;
        // Add to existing rows that are same
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
double SolveParallel(int n, int maxSteps, int numCPU) {
  // Use system time to keep track of threads finishing and ending
  struct timespec start, end;
  clock_gettime(CLOCK_MONOTONIC, &start);

  Board *board = NewBoard(n);
  int step = 0; // Keep track of steps till solution found

  int *conflictCols = (int *)malloc(n * sizeof(int));

  // Create pool to store multiple threads
  pthread_t *threadPool = (pthread_t *)malloc(numCPU * sizeof(pthread_t));
  ThreadData *threadDataPool =
      (ThreadData *)malloc(numCPU * sizeof(ThreadData));

  while (step < maxSteps) {
    int numConflicts = 0;
    // Collect columns with conflicts
    for (int col = 0; col < n; col++) {
      if (HasConflict(board, col))
        conflictCols[numConflicts++] = col;
    }

    // Board has been solved
    if (numConflicts == 0) {
      // Display Time Taken
      clock_gettime(CLOCK_MONOTONIC, &end);
      double duration =
          (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_sec) / 1e9;
      printf(" -- Solution found in %.3f seconds \n", duration);
      printf(" -- Solution found in %d (%d) steps \n", step, step * numCPU);

      // Validate Solution
      if (ValidateSolution(board->queens, n)) {
        printf(" -- Solution is valid!\n");
      } else {
        printf(" -- ERROR: Invalid solution found\n");
      }

      DeleteBoard(board);
      free(conflictCols);
      free(threadPool);
      free(threadDataPool);
      return duration; // Solution found
    }

    // Shuffle conflict columns to randomize processing order
    for (int i = numConflicts - 1; i > 0; i--) {
      int j = xorshift() % (i + 1); // Selects random column
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
      if (end > numConflicts)
        end = numConflicts;
      if (start >= end)
        break;

      // Add data to thread
      threadDataPool[i].board = board;
      threadDataPool[i].cols = &conflictCols[start];
      threadDataPool[i].numCols = end - start;

      // Create new thread
      pthread_create(&threadPool[i], NULL, MinimizeConflictsThread,
                     &threadDataPool[i]);
      numThreads++;
    }
    // Wait for all workers to finish
    for (int i = 0; i < numThreads; i++)
      pthread_join(threadPool[i], NULL);
    step++;
  }
  free(conflictCols);
  free(threadPool);
  free(threadDataPool);
  DeleteBoard(board);
  printf(" -- ERROR Solution NOT found in %d (%d) steps\n", step,
         step * numCPU);
  return 0; // No solution found
}

// File input function that validates input.txt as a solution for nQueens, and
// validates it Required function for assignment submission
void file_input() {
  FILE *fp = fopen("input.txt", "r");
  if (fp == NULL) {
    printf("Error: Cannot open input.txt\n");
    return;
  }

  int n = 0;
  int *queens = NULL;
  char line[1024]; // Assuming max board size is less than 1024

  int row = 0;
  int board_size = -1; // Initialize to invalid value

  while (fgets(line, sizeof(line), fp)) {
    // Remove newline character at the end, if any
    size_t len = strlen(line);
    if (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
      line[--len] = '\0';
    }

    // Ignore empty lines
    if (len == 0) {
      continue;
    }

    // Split the line into tokens
    char *tokens[1024];
    int token_count = 0;
    char *token = strtok(line, " \t");
    while (token != NULL) {
      tokens[token_count++] = token;
      token = strtok(NULL, " \t");
    }

    if (board_size == -1) {
      // First non-empty line, determine board size
      board_size = token_count;
      n = board_size;
      if (n <= 0) {
        printf("Error: Invalid board size\n");
        fclose(fp);
        return;
      }
      // Allocate queens array
      queens = (int *)malloc(n * sizeof(int));
      if (queens == NULL) {
        printf("Error: Memory allocation failed\n");
        fclose(fp);
        return;
      }
      // Initialize queens array to -1
      for (int i = 0; i < n; i++) {
        queens[i] = -1;
      }
    } else {
      if (token_count != board_size) {
        printf("Error: Inconsistent line length at row %d\n", row);
        free(queens);
        fclose(fp);
        return;
      }
    }

    // Process the tokens
    int numQ = 0; // Number of 'Q's in this row
    for (int col = 0; col < n; col++) {
      char *c = tokens[col];
      if (strlen(c) != 1) {
        printf("Error: Invalid character '%s' at row %d, column %d\n", c, row,
               col);
        free(queens);
        fclose(fp);
        return;
      }
      if (c[0] == 'Q') {
        numQ++;
        if (numQ > 1) {
          printf("Error: More than one queen in row %d\n", row);
          free(queens);
          fclose(fp);
          return;
        }
        if (queens[col] != -1) {
          printf("Error: More than one queen in column %d\n", col);
          free(queens);
          fclose(fp);
          return;
        }
        queens[col] = row;
      } else if (c[0] != '.') {
        printf("Error: Invalid character '%c' at row %d, column %d\n", c[0],
               row, col);
        free(queens);
        fclose(fp);
        return;
      }
    }

    if (numQ == 0) {
      printf("Error: No queen found in row %d\n", row);
      free(queens);
      fclose(fp);
      return;
    }

    row++;
  }

  if (row != n) {
    printf("Error: Expected %d rows, but got %d rows\n", n, row);
    free(queens);
    fclose(fp);
    return;
  }

  fclose(fp);

  // Now check if all columns have a queen
  for (int col = 0; col < n; col++) {
    if (queens[col] == -1) {
      printf("Error: No queen found in column %d\n", col);
      free(queens);
      return;
    }
  }

  // Now we have queens[col] = row for each column.
  // Now validate the solution
  if (ValidateSolution(queens, n)) {
    printf("Valid solution\n");
  } else {
    printf("Invalid solution\n");
  }

  free(queens);
}

int main() {
  // File input function for when validating txt files
  file_input();
  /**
  int boardSizes[] = {10000};
  int testQuantity = 20;
  random_state = (uint32_t)time(NULL); // Set random
  int numSizes = sizeof(boardSizes) / sizeof(boardSizes[0]);

  int numCPU = sysconf(_SC_NPROCESSORS_ONLN); // Find number of CPU's available
  if (numCPU < 1)
    numCPU = 1;

  for (int idx = 0; idx < numSizes; idx++) {
    int n = boardSizes[idx];
    int maxSteps = n * 10;
    double total_time = 0;
    for (int x = 0; x < testQuantity; x++) {
      total_time += SolveParallel(n, maxSteps, numCPU);
    }
    printf("\n\n AVERAGE FOR %d:  %.3f s\n\n\n", n, total_time / testQuantity);
  }
  */
  return 0;
}
