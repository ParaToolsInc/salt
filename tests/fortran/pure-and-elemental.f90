! Issue #36 placeholder behaviour: SALT skips pure, elemental, and
! impure-elemental procedures.  Pure procedures cannot host the
! standard TAU macros because they declare `integer, save ::
! tauProfileTimer(2)`, and SAVE is forbidden in a pure procedure
! (Fortran 2018 C1599).  Elemental procedures (pure or impure) are
! skipped to avoid spawning one TAU timer call per array element.

module pe_helpers
  implicit none
contains
  pure function pure_square(x) result(r)
    integer, intent(in) :: x
    integer :: r
    r = x * x
  end function pure_square

  elemental function elemental_negate(x) result(r)
    integer, intent(in) :: x
    integer :: r
    r = -x
  end function elemental_negate

  ! impure elemental procedures CAN have side effects per the
  ! standard, but SALT still skips them to avoid per-array-element
  ! timer overhead.
  impure elemental subroutine impure_emit(x)
    integer, intent(in) :: x
    print *, "impure_emit: ", x
  end subroutine impure_emit

  subroutine plain_caller(n)
    integer, intent(in) :: n
    integer :: i
    integer :: squares(n)
    do i = 1, n
      squares(i) = pure_square(i)
      call impure_emit(elemental_negate(i))
    end do
    print *, "squares = ", squares
  end subroutine plain_caller
end module pe_helpers


program test_pure_and_elemental
  use pe_helpers, only : plain_caller
  implicit none
  call plain_caller(3)
end program test_pure_and_elemental
