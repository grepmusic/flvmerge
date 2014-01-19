flvmerge
========

Fast flv merge program

gcc -O3 -o /path/to/flvmerge /path/to/flvmerge.c

Usage:
  /path/to/flvmerge flv_to_be_saved 1stflv [2ndflv [3rdflv [...]]]
  
command `/path/to/flvmerge final.flv 1.flv 2.flv 3.flv` will merge '1.flv', '2.flv', '3.flv' into one flv file 'final.flv'

This program does not care flv headers, its very fast because it simply merges each files' flv tag into one flv file and re-caculate each tag's timestamp field, and also update the duration field in the script data part.

It does not rely on any 3rd party library, such as ffmpeg.
