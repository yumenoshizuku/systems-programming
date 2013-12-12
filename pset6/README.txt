README for CS 61 Problem Set 6
------------------------------
YOU MUST FILL OUT THIS FILE BEFORE SUBMITTING!

YOUR NAME: Fanxing Meng
YOUR HUID: 70836769

(Optional, for partner)
YOUR NAME:
YOUR HUID:

RACE CONDITIONS
---------------
Write a SHORT paragraph here explaining your strategy for avoiding
race conditions. No more than 400 words please.

Two places have been taken care of regarding race conditions: stop_time and
connection table. For stop_time, a conditional variable is used for keeping
other threads waiting when one thread is sleeping for the designated time, and
a mutex is used for both the variable and the conditional variable.

For the connection table, only a mutex is used to prevent concurrent access
since busy waiting is sufficient for data access and modification.

OTHER COLLABORATORS AND CITATIONS (if any):



KNOWN BUGS (if any):



NOTES FOR THE GRADER (if any):



EXTRA CREDIT ATTEMPTED (if any):
