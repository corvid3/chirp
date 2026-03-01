: ( 41 parse drop drop ; immediate
: \ 10 parse drop drop ; immediate

: variable CREATE , ; 
: constant CREATE , DOES> @c ; 
: bytearray CREATE allot DOES> + ;

: IF 
   HERE branch0 
; immediate 

: ELSE 
   HERE swap
   branch
   HERE resLink
; immediate 

: THEN 
   HERE resLink 
; immediate 

: NOT 
   IF 0 ELSE 1 THEN
;

: BEGIN
   HERE
; immediate

: UNTIL
   postpone NOT
   HERE swap
   branch0
   resLink
; immediate

: arenaMake ( size -- arenaPtr )
   HERE swap ALLOT
   dup 0 swap !c
;

: arenaHere ( arenaPtr -- ptr )
   dup @c +
;

: arenaAlloc ( arenaPtr size -- ptr )
   over arenaHere >r
   over @c + swap !c
   r>
;

: arenaReset ( arenaPtr -- )
   0 swap !c 
;

: kb 1024 * ;

1 kb arenaMake constant arena
arena 16 arenaAlloc constant str
