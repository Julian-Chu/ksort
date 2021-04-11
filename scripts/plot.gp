reset
set xlabel 'cycle'
set ylabel 'time (ns)'
set title 'ksort'
set term png enhanced font 'Verdana,10'
set output 'test_xoro_time.png'
set grid
plot [0:10][0:500000] \
'test_xoro_time' using 1:2 with linespoints linewidth 2 title "sort impl",\
'' using 1:3 with linespoints linewidth 2 title "shell sort",\
'' using 1:4 with linespoints linewidth 2 title "binary selection sort",\
'' using 1:5 with linespoints linewidth 2 title "heap sort",\
'' using 1:6 with linespoints linewidth 2 title "quick sort",\
'' using 1:7 with linespoints linewidth 2 title "merge sort",\
'' using 1:8 with linespoints linewidth 2 title "selection sort",\
'' using 1:9 with linespoints linewidth 2 title "tim sort",\
