! Minimal source for IMPURE ELEMENTAL.  Despite being non-pure, SALT
! still skips it: applying an elemental procedure to an array
! triggers one timer call per element, drowning real signal.
module impure_elemental_only
  implicit none
contains
  impure elemental subroutine emit(x)
    integer, intent(in) :: x
    print *, "emit: ", x
  end subroutine emit
end module impure_elemental_only
