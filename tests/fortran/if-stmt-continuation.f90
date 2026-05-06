! Regression test for issue #39: `if (<cond>) return` whose condition
! spans multiple source lines via the `&` continuation marker.
!
! The Flang plugin's IfStmt handler currently uses the end column of
! the condition as the splice point for the synthesized
! `then ... endif` block, and the splice does not account for the
! condition continuing onto a later source line.  As a result, the
! `&` and the trailing `) return` end up after the synthesized
! `endif`, producing invalid Fortran.  See the TODOs in
! flang_salt_instrument_plugin.cpp's `Pre(IfStmt&)` overload.
!
! These tests are wired with WILL_FAIL=TRUE so that CI tracks the
! defect: when the plugin is fixed, the WILL_FAIL property must be
! removed.
!
! Standard-conforming F2008+. Single file, compiled top-to-bottom.

subroutine if_continued_condition(i, j)
  implicit none
  integer, intent(in) :: i, j
  if (i == 0 .and. &
      j == 0) return
  print *, "if_continued_condition post-return"
end subroutine if_continued_condition


subroutine if_continued_long(i, j, k)
  ! Three-clause condition split twice via `&`.
  implicit none
  integer, intent(in) :: i, j, k
  if (i == 0 .and. &
      j == 0 .and. &
      k == 0) return
  print *, "if_continued_long post-return"
end subroutine if_continued_long


program test_if_stmt_continuation
  implicit none

  interface
    subroutine if_continued_condition(i, j)
      integer, intent(in) :: i, j
    end subroutine if_continued_condition
    subroutine if_continued_long(i, j, k)
      integer, intent(in) :: i, j, k
    end subroutine if_continued_long
  end interface

  call if_continued_condition(0, 0)
  call if_continued_condition(0, 1)

  call if_continued_long(0, 0, 0)
  call if_continued_long(0, 0, 1)
end program test_if_stmt_continuation
