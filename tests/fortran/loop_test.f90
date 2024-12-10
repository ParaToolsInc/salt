subroutine bar(arg)
  integer arg

  print *, "inside bar arg = ", arg
end subroutine bar

subroutine foo(iVal)
  integer iVal
  integer j, k
! Do something here...
  print *, "Iteration = ", iVal
        do j = 1, 5
          do k = 1, 2
            print *, "j = ", j
          end do
        end do

        do i = 1, 3
          call bar(i+iVal)
        end do
        print *, "after calling bar in foo"
end subroutine foo

program main
  integer i

  print *, "test program"

  do i = 1, 3
    call foo(i)
  end do
end program main

