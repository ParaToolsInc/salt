! Minimal source containing only a pure function, used to assert
! that the SALT plugin emits no TAU instrumentation into the body of
! a pure procedure.  See issue #36.
module pure_fn_only
  implicit none
contains
  pure function squared(x) result(r)
    integer, intent(in) :: x
    integer :: r
    r = x * x
  end function squared
end module pure_fn_only
