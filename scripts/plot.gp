reset
set xlabel 'cycle'
set ylabel 'time (ns)'
set title 'ksort'
set term png enhanced font 'Verdana,10'
set output 'test_xoro_time.png'
set grid
plot [0:10][0:500000] \
'test_xoro_time' using 1:2 with linespoints linewidth 2 title "ksort",\
