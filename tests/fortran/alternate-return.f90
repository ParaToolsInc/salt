! Issue #32 regression: `return <scalar-int-expr>` (the obsolescent
! alternate-return form, allowed in subroutines that have alternate-
! return dummy arguments).  SALT instruments both the bare and
! `if (cond) return <expr>` forms by injecting TAU_PROFILE_STOP
! before the return; the int-expression text is preserved in the
! synthesized replacement so timers stay balanced regardless of
! which branch fires.
!
! The instrumented output is still rejected by `gfortran -Wpedantic
! -Werror` because alt-return is obsolescent, so this source lives
! in FORTRAN_TESTS_INSTRUMENT_ONLY_SOURCES rather than the regular
! compile/run/profile pipeline.

subroutine alt_ret_callee(i, *, *)
  implicit none
  integer, intent(in) :: i
  ! All three forms are intentionally present so the plugin sees and
  ! warns about each of them.  No branch fires when i == 0.
  ! 1) `if (cond) return <expr>` -- handled in the if-stmt path.
  if (i > 0) return 1
  if (i > 10) return 2
  ! 2) Bare `return <expr>` not inside an if-stmt -- handled in the
  !    plain ReturnStmt path.  Wrapped in an `if-then-endif` block
  !    (which is a different parser node than `if-stmt`) so it remains
  !    unreachable when i == 0.
  if (i < -1000) then
    return 3
  end if
  print *, "alt_ret_callee: regular return path"
end subroutine alt_ret_callee


program test_alternate_return
  implicit none

  interface
    subroutine alt_ret_callee(i, *, *)
      integer, intent(in) :: i
    end subroutine alt_ret_callee
  end interface

  call alt_ret_callee(0, *100, *200)
  return

100 print *, "branched to label 100"
  return
200 print *, "branched to label 200"
end program test_alternate_return
