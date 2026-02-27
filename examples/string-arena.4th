: kilo ( n -- n ) 1024 * ;
: sar 0 ;
: sarhead 0  ;

: init-arena ( size -- )
   align here ' sar ! \ get the current data address
   8 * allot \ allocate the space
;

: reset-arena ( -- )
   \ simply set the address back
   sarhead ' sar  !
;

: s+a ( addr len -- str )
   over over sarhead copy
   over sarhead +
;

128 kilo init-arena .

: entry ( uri cert -- str )
   s" res://somefile" loadfile s+a
   reset-arena
;
