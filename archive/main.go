package main

import (
	"fmt"
	"math/rand"
	"runtime"
	"sync"
	"time"
)

// TODO: print the board for 10, 50, 100. Anything higher

// Board represents the N-Queens board
type Board struct {
	n              int
	queens         []int
	rowConflicts   []int
	diag1Conflicts []int
	diag2Conflicts []int
	mu             sync.Mutex // Mutex for synchronizing access to shared data
}


// NewBoard initializes the board with a smarter initial placement
func NewBoard(n int) *Board {
	board := &Board{
		n:              n,
		queens:         make([]int, n),
		rowConflicts:   make([]int, n),
		diag1Conflicts: make([]int, 2*n),
		diag2Conflicts: make([]int, 2*n),
	}

	// Smarter initial placement: Place queens on different rows and diagonals
  //   for j := 1; j <= (board.n/2); j++ {
  //       col := j - 1
  //       col2 := board.n/2 + j - 1
  //       row := 2*j-1
  //       row2 := row - 1
  //       // fmt.Println(col, col2, row, row2)
  //       board.queens[col] = row
  //       board.queens[col2] = row2
		// board.diag1Conflicts[row-col+board.n]++
		// board.diag2Conflicts[row+col]++
		// board.diag1Conflicts[row2-col2+board.n]++
		// board.diag2Conflicts[row2+col2]++
  //   }
    
	for col := 0; col < n; col++ {
        row := rand.Intn(board.n) // put random number
		board.queens[col] = row
		board.rowConflicts[row]++
		board.diag1Conflicts[row-col+board.n]++
		board.diag2Conflicts[row+col]++
	}

	return board
}

// HasConflict checks if the queen at the given column has any conflicts
func (b *Board) HasConflict(col int) bool {
	// Assume the lock is already held
	row := b.queens[col]
	return b.rowConflicts[row] > 1 ||
		b.diag1Conflicts[row-col+b.n] > 1 ||
		b.diag2Conflicts[row+col] > 1
}

// MinimizeConflicts minimizes conflicts for queens in the given columns
func (b *Board) MinimizeConflicts(cols []int, rnd *rand.Rand) {
	for _, col := range cols {
		b.mu.Lock()
		if !b.HasConflict(col) {
			b.mu.Unlock()
			continue
		}
		bestRows := []int{}
		minConflicts := b.n * 3 // Maximum possible conflicts per queen

        diag1 := -col + b.n
		// Find all rows with the minimum number of conflicts
		for row := 0; row < b.n; row++ {
			conflicts := b.rowConflicts[row] +
				b.diag1Conflicts[row+diag1] +
				b.diag2Conflicts[row+col]
			if row == b.queens[col] {
				conflicts -= 3 // Exclude self-conflicts
			}
			if conflicts < minConflicts {
				bestRows = []int{row}
				minConflicts = conflicts
			} else if conflicts == minConflicts {
				bestRows = append(bestRows, row)
			}
		}

		// Randomly select one of the best rows to diversify moves
		newRow := bestRows[rnd.Intn(len(bestRows))]
		b.UpdateQueen(col, newRow)
		b.mu.Unlock()
	}
}

// UpdateQueen updates the queen's position in the board
func (b *Board) UpdateQueen(col, newRow int) {
	// Assume the lock is already held
	oldRow := b.queens[col]
	if oldRow == newRow {
		return
	}

	// Remove old conflicts
	b.rowConflicts[oldRow]--
	b.diag1Conflicts[oldRow-col+b.n]--
	b.diag2Conflicts[oldRow+col]--

	// Place queen at new position
	b.queens[col] = newRow

	// Add new conflicts
	b.rowConflicts[newRow]++
	b.diag1Conflicts[newRow-col+b.n]++
	b.diag2Conflicts[newRow+col]++
}

// ValidateSolution checks if the solution is valid (no queens attack each other)
func ValidateSolution(queens []int) bool {
	n := len(queens)
	for i := 0; i < n; i++ {
		for j := i + 1; j < n; j++ {
			// Check row conflicts
			if queens[i] == queens[j] {
				return false
			}
			// Check diagonal conflicts
			if abs(i-j) == abs(queens[i]-queens[j]) {
				return false
			}
		}
	}
	return true
}

// abs returns the absolute value of an integer
func abs(x int) int {
	if x < 0 {
		return -x
	}
	return x
}

// SolveParallel solves the N-Queens problem using an optimized parallel Min-Conflicts algorithm
func SolveParallel(n, maxSteps int) ([]int, bool) {
    globalRand := rand.New(rand.NewSource(12345)) // Replace 12345 with any constant value
	board := NewBoard(n)
	step := 0
	numCPU := runtime.NumCPU()
	runtime.GOMAXPROCS(numCPU)

	var wg sync.WaitGroup

	for step < maxSteps {
		conflictCols := make([]int, 0, n/10)

		// Collect columns with conflicts
		board.mu.Lock()
		for col := 0; col < n; col++ {
			if board.HasConflict(col) {
				conflictCols = append(conflictCols, col)
			}
		}
		board.mu.Unlock()

		if len(conflictCols) == 0 {
			return board.queens, true // Solution found
		}

		// Shuffle conflict columns to randomize processing order
		globalRand.Shuffle(len(conflictCols), func(i, j int) {
			conflictCols[i], conflictCols[j] = conflictCols[j], conflictCols[i]
		})

		// Divide conflict columns among workers
		chunkSize := (len(conflictCols) + numCPU - 1) / numCPU
		for i := 0; i < numCPU; i++ {
			start := i * chunkSize
			end := start + chunkSize
			if end > len(conflictCols) {
				end = len(conflictCols)
			}
			if start >= end {
				break
			}

			wg.Add(1)
			go func(cols []int) {
				defer wg.Done()
				rnd := rand.New(rand.NewSource(time.Now().UnixNano()))
				board.MinimizeConflicts(cols, rnd)
			}(conflictCols[start:end])
		}

		// Wait for all workers to finish
		wg.Wait()
		step++
	}

	return nil, false // No solution found within maxSteps
}

func main() {
	// Range of board sizes to test
	boardSizes := []int{100, 1000, 10000, 100000, 1000000, 10000000}

	for _, n := range boardSizes {
		fmt.Printf("Solving N-Queens for n = %d...\n", n)

		maxSteps := n * 10 // Adjust maxSteps based on n
		startTime := time.Now()

		sol, success := SolveParallel(n, maxSteps)
		duration := time.Since(startTime)

		if success {
			fmt.Printf("Solution found in %v\n", duration)

			// Validate the solution
			if ValidateSolution(sol) {
				fmt.Println("Solution is valid!")
			} else {
				fmt.Println("ERROR: Invalid solution found")
			}
		} else {
			fmt.Printf("No solution found in %v\n", duration)
		}
		fmt.Println("---")
	}
}
