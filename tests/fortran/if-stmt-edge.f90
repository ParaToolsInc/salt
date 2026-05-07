! Edge-case coverage for `if (<cond>) return` on a single physical
! line: tight spacing, compound conditions, function-call
! conditions, nested negation.  Companion fixture
! if-stmt-continuation.f90 covers `&` line continuations.

module if_edge_helpers
  implicit none
contains
  logical function predicate(x)
    ! Deliberately not `pure` so the plugin instruments it; pure
    ! procedures are skipped (#36) and would not contribute to
    ! coverage of the if-stmt-with-function-call branch below.
    integer, intent(in) :: x
    predicate = x > 0
  end function predicate

  subroutine note(label)
    character(len=*), intent(in) :: label
    print *, "passed branch: ", label
  end subroutine note
end module if_edge_helpers


subroutine if_compact(i)
  use if_edge_helpers, only : note
  implicit none
  integer, intent(in) :: i
  if(i==0)return
  call note("if_compact post-return")
end subroutine if_compact


subroutine if_compound(i, j)
  use if_edge_helpers, only : note
  implicit none
  integer, intent(in) :: i, j
  if (i == 0 .and. j == 0) return
  call note("if_compound post-return")
end subroutine if_compound


subroutine if_function_call(x)
  use if_edge_helpers, only : note, predicate
  implicit none
  integer, intent(in) :: x
  if (predicate(x)) return
  call note("if_function_call post-return")
end subroutine if_function_call


subroutine if_double_negation(flag)
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

  ! Each subroutine called twice: once with the early-return
  ! condition true, once with it false, so both branches execute and
  ! the runtime-balanced TAU timer pairs are exercised.
  call if_compact(0)
  call if_compact(1)

  call if_compound(0, 0)
  call if_compound(0, 1)

  call if_function_call(5)
  call if_function_call(-1)

  call if_double_negation(.true.)
  call if_double_negation(.false.)
end program test_if_stmt_edge
