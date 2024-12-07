# 468TP
- Run "n-queens-final.c" with the following command

- After specifying (at the bottom) the sizes and quantity of tests you want to run, and optionally comment out the PrintSolution method (suggested for large n's as it creates quite a large text file)

- clang -O3 -march=native -flto -fomit-frame-pointer -o nqueens_pgo_gen <file_name> && ./nqueens_pgo_gen   (or gcc)

- you can also change numCPU arbitrarily, doesnt have to match
