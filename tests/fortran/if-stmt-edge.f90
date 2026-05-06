! Edge-case coverage for `if (<cond>) return` instrumentation -- the
! cases that the plugin handles correctly today.  See
! tests/fortran/if-stmt-continuation.f90 for the multi-line
! continuation cases that currently produce invalid Fortran (issue
! #39, plugin TODOs in flang_salt_instrument_plugin.cpp).
!
! Standard-conforming F2008+. Single file, compiled top-to-bottom.

module if_edge_helpers
  implicit none
contains
  logical function predicate(x)
    ! Intentionally not declared `pure`: marking it pure would let the
    ! plugin inject `integer, save :: tauProfileTimer(...)` inside a
    ! pure procedure, which the standard forbids. That defect is
    ! tracked separately under issue #36.
    integer, intent(in) :: x
    predicate = x > 0
  end function predicate

  subroutine note(label)
    character(len=*), intent(in) :: label
    print *, "passed branch: ", label
  end subroutine note
end module if_edge_helpers


subroutine if_compact(i)
  ! Tightly-spaced form, no whitespace inside parens.
  use if_edge_helpers, only : note
  implicit none
  integer, intent(in) :: i
  if(i==0)return
  call note("if_compact post-return")
end subroutine if_compact


subroutine if_compound(i, j)
  ! Compound logical condition on a single line.
  use if_edge_helpers, only : note
  implicit none
  integer, intent(in) :: i, j
  if (i == 0 .and. j == 0) return
  call note("if_compound post-return")
end subroutine if_compound


subroutine if_function_call(x)
  ! Condition is a function call rather than a comparison.
  use if_edge_helpers, only : note, predicate
  implicit none
  integer, intent(in) :: x
  if (predicate(x)) return
  call note("if_function_call post-return")
end subroutine if_function_call


subroutine if_double_negation(flag)
  ! Nested .not. wrapping.
  use if_edge_helpers, only : note
  implicit none
  logical, intent(in) :: flag
  if (.not. (.not. flag)) return
  call note("if_double_negation post-return")
end subroutine if_double_negation


program test_if_stmt_edge
  implicit none

  interface
    subroutine if_compact(i)
      integer, intent(in) :: i
    end subroutine if_compact
    subroutine if_compound(i, j)
      integer, intent(in) :: i, j
    end subroutine if_compound
    subroutine if_function_call(x)
      integer, intent(in) :: x
    end subroutine if_function_call
    subroutine if_double_negation(flag)
      logical, intent(in) :: flag
    end subroutine if_double_negation
  end interface

  ! Each subroutine is invoked twice: once where the condition is
  ! true (taking the early return branch) and once where it is false
  ! (falling through).
  call if_compact(0)
  call if_compact(1)

  call if_compound(0, 0)
  call if_compound(0, 1)

  call if_function_call(5)
  call if_function_call(-1)

  call if_double_negation(.true.)
  call if_double_negation(.false.)
end program test_if_stmt_edge
