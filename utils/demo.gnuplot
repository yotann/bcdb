set term pngcairo size 800,600
set output 'demo.tmp.png'

#set term dumb

#set xtics rotate
set datafile separator ','
set key left
set xlabel "Number of programs added"
set ylabel "Number of functions"
#plot 'demo.csv' using 2:xtic(1) with lines title 'Total functions', 'demo.csv' using 3 with lines title 'Unique functions'
plot 'demo.csv' using 2 with lines lw 4 title 'Total functions', 'demo.csv' using 3 with lines lw 4 lt rgb "black" title 'Unique functions'
