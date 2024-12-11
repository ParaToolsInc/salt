function func(i) result(j)
    integer, intent (in) :: i ! input
    integer              :: j ! output

    j = i**2 + i**3
end function

subroutine square_cube(i, isquare, icube)
    integer, intent (in)  :: i              ! input
    integer, intent (out) :: isquare, icube ! output

    isquare = i**2
    icube   = i**3
end subroutine

subroutine hello
  print *, "Hello world"
end subroutine

program main
    implicit none
    external square_cube ! external subroutine
    integer :: isq, icub
    integer :: i
    integer :: func

    call square_cube(4, isq, icub)
    print *, "i,i^2,i^3=", 4, isq, icub

    i = 3
    print *, "sum of the square and cube of", i, "is", func(i)
end program

