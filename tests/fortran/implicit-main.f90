! Implicit main program: a program-unit with executable statements
! and no `program` statement.  Standards-conforming F2018 (R1101
! main-program: [program-stmt] [specification-part] [execution-part]
! [internal-subprogram-part] end-program-stmt -- the program-stmt is
! optional).
!
! With no program-stmt, the plugin has no name or source line to
! attach to the timer; it must synthesize both from fallbacks.

implicit none
integer :: i
i = 41
print *, "implicit main: i =", i + 1
end
