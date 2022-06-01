
#include<iostream>

template<typename T>
void doit(const T &t){
  t();
}
int main(void){
  doit([&](){ std::cout << "Hello" << std::endl; });

  return 0;
}

