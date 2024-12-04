# 468TP
- Run Either "old_testable.c" or "new_testable.c" with the following command

- After specifying (at the bottom) the sizes and quantity of tests you want to run

- clang -O3 -march=native -flto -fomit-frame-pointer -o nqueens_pgo_gen <file_name> && ./nqueens_pgo_gen   (or gcc)

- you can also change numCPU arbitrarily, doesnt have to match

