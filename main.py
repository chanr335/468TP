import random


def min_conflicts(N, max_steps=100000):
    # Initialize the board with one queen in each column, placed randomly in any row
    board = [random.randint(0, N - 1) for _ in range(N)]

    for step in range(max_steps):
        # Find all queens that are in conflict
        conflicts = find_conflicts(board, N)
        if sum(conflicts.values()) == 0:
            # No conflicts, solution found
            return board
        # Choose a random column with a conflicted queen
        conflicted_columns = [col for col in range(N) if conflicts[col] > 0]
        col = random.choice(conflicted_columns)
        # Move the queen in this column to the row with minimum conflicts
        min_conflict_rows = []
        min_conflicts = N
        for row in range(N):
            board[col] = row
            conflict_count = calculate_conflicts(board, col, N)
            if conflict_count < min_conflicts:
                min_conflicts = conflict_count
                min_conflict_rows = [row]
            elif conflict_count == min_conflicts:
                min_conflict_rows.append(row)
        # Place the queen in one of the positions with minimum conflicts
        board[col] = random.choice(min_conflict_rows)
    # If no solution is found within max_steps
    return None


def find_conflicts(board, N):
    conflicts = {}
    for col in range(N):
        conflicts[col] = calculate_conflicts(board, col, N)
    return conflicts


def calculate_conflicts(board, col, N):
    count = 0
    for other_col in range(N):
        if other_col == col:
            continue
        if board[other_col] == board[col] or abs(board[other_col] - board[col]) == abs(
            other_col - col
        ):
            count += 1
    return count


def print_board(board):
    N = len(board)
    for row in range(N):
        line = ""
        for col in range(N):
            if board[col] == row:
                line += "Q "
            else:
                line += ". "
        print(line)
    print("\n")


# Example usage:
N = int(input("Enter the value of N: "))
solution = min_conflicts(N)
if solution:
    print(f"Solution for N={N}:")
    # print_board(solution)
else:
    print("No solution found within the given steps.")
