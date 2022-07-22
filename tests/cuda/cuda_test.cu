#include "cuda_runtime.h"

#define N 5

__global__ void add(int *a, int *b, int *c)
{
    int tID = blockIdx.x;
    if (tID < N)
    {
        c[tID] = a[tID] + b[tID];
    }
}

double foo()
{
    //   A function annotation. Opens region "function=foo" in Caliper,
    // and automatically closes it at the end of the function.

    int a[N], b[N], c[N];
    int *dev_a, *dev_b, *dev_c;

    cudaMalloc((void **) &dev_a, N*sizeof(int));
    cudaMalloc((void **) &dev_b, N*sizeof(int));
    cudaMalloc((void **) &dev_c, N*sizeof(int));

    // Fill Arrays
    for (int i = 0; i < N; i++)
    {
        a[i] = i,
        b[i] = i;
    }
    cudaMemcpy(dev_a, a, N*sizeof(int), cudaMemcpyHostToDevice);
    cudaMemcpy(dev_b, b, N*sizeof(int), cudaMemcpyHostToDevice);

    add<<<N,1>>>(dev_a, dev_b, dev_c);

    cudaMemcpy(c, dev_c, N*sizeof(int), cudaMemcpyDeviceToHost);

    return 0.5;
}

int main() {

    int iterations = 10;
    for (int i = 0; i < iterations; i++) {
        int out = foo();
    }  
}
