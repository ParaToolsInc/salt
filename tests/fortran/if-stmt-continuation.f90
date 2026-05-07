! Regression test for issue #39: `if (<cond>) return` whose physical
! source spans multiple lines via the `&` continuation marker.
!
! All forms are standards-conforming Fortran 2008+.

subroutine if_two_line_condition(i, j)
  ! Condition split once.
  implicit none
  integer, intent(in) :: i, j
  if (i == 0 .and. &
      j == 0) return
  print *, "if_two_line_condition post-return"
end subroutine if_two_line_condition


subroutine if_three_line_condition(i, j, k)
  ! Condition split twice.
  implicit none
  integer, intent(in) :: i, j, k
  if (i == 0 .and. &
      j == 0 .and. &
      k == 0) return
  print *, "if_three_line_condition post-return"
end subroutine if_three_line_condition


subroutine if_split_before_action(i)
  ! Continuation between the closing condition paren and the action.
  implicit none
  integer, intent(in) :: i
  if (i == 0) &
      return
  print *, "if_split_before_action post-return"
end subroutine if_split_before_action


subroutine if_continuation_with_comment(i, j)
  ! Comment line between continuation segments. Cooked source must
  ! strip the comment so the reconstructed condition is clean.
  implicit none
  integer, intent(in) :: i, j
  if (i == 0 .and. & ! first half
      j == 0) return
  print *, "if_continuation_with_comment post-return"
end subroutine if_continuation_with_comment


subroutine if_continuation_leading_amp(i, j)
  ! Continuation with leading `&` on the next line (a Fortran 2008
  ! convention that is sometimes preferred for column alignment).
  implicit none
  integer, intent(in) :: i, j
  if (i == 0 &
      &.and. j == 0) return
  print *, "if_continuation_leading_amp post-return"
end subroutine if_continuation_leading_amp


program test_if_stmt_continuation
  implicit none

  interface
    subroutine if_two_line_condition(i, j)
      integer, intent(in) :: i, j
    end subroutine if_two_line_condition
    subroutine if_three_line_condition(i, j, k)
      integer, intent(in) :: i, j, k
    end subroutine if_three_line_condition
    subroutine if_split_before_action(i)
      integer, intent(in) :: i
    end subroutine if_split_before_action
    subroutine if_continuation_with_comment(i, j)
      integer, intent(in) :: i, j
    end subroutine if_continuation_with_comment
    subroutine if_continuation_leading_amp(i, j)
      integer, intent(in) :: i, j
    end subroutine if_continuation_leading_amp
  end interface

  ! Each subroutine is invoked twice: once where the condition is
  ! true (taking the early return branch) and once where it is false
  ! (falling through).
  call if_two_line_condition(0, 0)
  call if_two_line_condition(0, 1)

  call if_three_line_condition(0, 0, 0)
  call if_three_line_condition(0, 0, 1)

  call if_split_before_action(0)
  call if_split_before_action(1)

  call if_continuation_with_comment(0, 0)
  call if_continuation_with_comment(0, 1)

  call if_continuation_leading_amp(0, 0)
  call if_continuation_leading_amp(0, 1)
end program test_if_stmt_continuation
