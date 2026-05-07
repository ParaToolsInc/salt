! Issue #31 regression: a labeled DO whose terminator is the last
! executable statement of a procedure used to crash the SALT plugin
! because flang's GetSourcePositionRange returns nullopt for the
! labeled CONTINUE / labeled END DO terminator's CharBlock.  The
! plugin now falls back to the wrapping End-stmt's line.
!
! Standards-conforming F2018 (alternate label-do form is obsolescent
! per Annex B.3.4 but still standard).

subroutine label_do_continue_at_end(n, total)
  implicit none
  integer, intent(in) :: n
  integer, intent(out) :: total
  integer :: i
  total = 0
  do 10 i = 1, n
    total = total + i
10 continue
end subroutine label_do_continue_at_end


subroutine label_do_enddo_at_end(n, total)
  implicit none
  integer, intent(in) :: n
  integer, intent(out) :: total
  integer :: i
  total = 0
  do 20 i = 1, n
    total = total + i*i
20 end do
end subroutine label_do_enddo_at_end


! Procedure body ends with a labeled CONTINUE *and* the procedure
! has an internal subprogram via `contains`.  The plugin must place
! PROCEDURE_END just before the contains keyword (not just before
! END SUBROUTINE), otherwise the outer subroutine's stop timer would
! land inside / after the inner procedure's body and break the
! line-sorted instrumentation invariant.
subroutine label_do_with_contains(n, total)
  implicit none
  integer, intent(in) :: n
  integer, intent(out) :: total
  integer :: i
  total = 0
  do 30 i = 1, n
    total = total + bumped(i)
30 continue
contains
  function bumped(x) result(r)
    integer, intent(in) :: x
    integer :: r
    r = x + 1
  end function bumped
end subroutine label_do_with_contains


program test_label_do
  implicit none

  interface
    subroutine label_do_continue_at_end(n, total)
      integer, intent(in) :: n
      integer, intent(out) :: total
    end subroutine label_do_continue_at_end
    subroutine label_do_enddo_at_end(n, total)
      integer, intent(in) :: n
      integer, intent(out) :: total
    end subroutine label_do_enddo_at_end
    subroutine label_do_with_contains(n, total)
      integer, intent(in) :: n
      integer, intent(out) :: total
    end subroutine label_do_with_contains
  end interface

  integer :: t1, t2, t3

  call label_do_continue_at_end(5, t1)
  call label_do_enddo_at_end(4, t2)
  call label_do_with_contains(3, t3)
  print *, "t1=", t1, " t2=", t2, " t3=", t3
end program test_label_do
