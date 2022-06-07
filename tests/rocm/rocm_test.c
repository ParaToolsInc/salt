#include "hip/hip_runtime.h"
#include <stdio.h>

#define N 4


__global__ void dot(int n, double *x, double *y, double *z) {
    int index = blockIdx.x * blockDim.x + threadIdx.x;
    int stride = blockDim.x * gridDim.x;
    
    *z = 0;
    for (int i = index; i < n; i += stride) {
        *z += x[i] * y[i];
    }
}

int main() {

    float *x = (float *)malloc(N*sizeof(float));
    float *y = (float *)malloc(N*sizeof(float));
    float *z = (float *)malloc(sizeof(float));

    for (int i = 0; i < N; i++) {
        x[i] = i + 1;
        y[i] = i + 1;
    }

    float *din0, *din1, *dout;
    
    hipHostMalloc((void *)x, N*sizeof(float), hipHostMallocDefault);
    hipHostMalloc((void *)y, N*sizeof(float), hipHostMallocDefault);
    hipHostMalloc((void *)z, sizeof(float), hipHostMallocDefault);

    hipHostGetDevicePointer((void *)din0, x, 0);
    hipHostGetDevicePointer((void *)din1, y, 0);
    hipHostGetDevicePointer((void *)dout, z, 0);

    hipMemcpy(din0, x, N*sizeof(float), hipMemcpyHostToDevice);
    hipMemcpy(din1, y, N*sizeof(float), hipMemcpyHostToDevice);

    hipLaunchKernelGGL(dot,
                        N,
                        1,
                        0, 0,
                        N, (float4*)x, (float4*)y, (float4*)z);
    
}
