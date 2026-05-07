! `if (<cond>) return` carrying a numeric statement label that is
! the target of a `goto` elsewhere in the procedure.  Per F2018 R601
! and R863, an if-stmt may be labeled, and the label is a valid
! branch target.  The TAU instrumentation transform replaces the
! whole if-stmt with a synthesized `if (cond) then ... end if`
! block; the label must move to the synthesized header so existing
! `goto N` references still resolve.

subroutine labeled_if_return(i, k)
  implicit none
  integer, intent(in) :: i
  integer, intent(out) :: k
  k = 0
  if (i < 0) goto 30
  k = 1
  goto 40
30 if (i == 0) return
  k = 2
40 continue
end subroutine labeled_if_return


program test_labeled_if_return
  implicit none

  interface
    subroutine labeled_if_return(i, k)
      integer, intent(in) :: i
      integer, intent(out) :: k
    end subroutine labeled_if_return
  end interface

  integer :: out
  call labeled_if_return(-1, out)
  call labeled_if_return(0,  out)
  call labeled_if_return(1,  out)
end program test_labeled_if_return
