! Minimal source for the bare ELEMENTAL prefix (pure by default).
! Asserts SALT emits no TAU instrumentation into the body.
module elemental_fn_only
  implicit none
contains
  elemental function negate(x) result(r)
    integer, intent(in) :: x
    integer :: r
    r = -x
  end function negate
end module elemental_fn_only
