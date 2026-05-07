! Issue #32 regression: `return <scalar-int-expr>` (the obsolescent
! alternate-return form, allowed in subroutines that have alternate-
! return dummy arguments).  Why instrument an obsolescent form: timer
! starts already exist for these subprograms; without matching stops
! injected before each `return <expr>`, profile data is unbalanced.
!
! Listed in CMakeLists's FORTRAN_TESTS_PERMISSIVE_SOURCES because
! `-Wpedantic` flags alt-return and the rest of the suite is built
! with `-Werror`.

subroutine alt_ret_callee(i, *, *)
  implicit none
  integer, intent(in) :: i
  ! No branch fires when i == 0; all three forms are intentionally
  ! present so the plugin's bare-return, if-stmt-return, and
  ! ReturnStmt paths each see one alt-return.
  if (i > 0) return 1
  if (i > 10) return 2
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
