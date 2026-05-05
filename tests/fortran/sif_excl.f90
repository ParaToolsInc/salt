program sif_excl
  implicit none
  call keep_routine()
  call excluded_routine()
contains
  subroutine keep_routine()
    print *, "kept"
  end subroutine keep_routine
  subroutine excluded_routine()
    print *, "excluded"
  end subroutine excluded_routine
end program sif_excl
