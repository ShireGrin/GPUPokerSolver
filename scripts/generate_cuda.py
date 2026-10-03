#!/usr/bin/env python3
import os
import re

def process_file(src_path, dst_path):
    if not os.path.exists(src_path):
        print(f"Skipping {src_path} (not found)")
        return
        
    with open(src_path, 'r') as f:
        content = f.read()
        
    # Replace headers
    content = content.replace('<hip/hip_runtime_api.h>', '<cuda_runtime.h>')
    content = content.replace('<hip/hip_runtime.h>', '<cuda_runtime.h>')
    content = content.replace('<hip/hip_fp16.h>', '<cuda_fp16.h>')
    content = content.replace('include/solver/HipPCfrSolver.h', 'include/solver/CudaPCfrSolver.h')
    
    # Platform defines
    content = content.replace('__HIP_PLATFORM_AMD__', '__HIP_PLATFORM_NVIDIA__')
    
    # Class names
    content = content.replace('HipPCfrSolver', 'CudaPCfrSolver')
    
    # Specific API mappings
    content = content.replace('hipHostMalloc', 'cudaMallocHost')
    
    # Regex replacements for hip -> cuda
    content = re.sub(r'\bhip', 'cuda', content)
    content = re.sub(r'\bHip', 'Cuda', content)
    content = re.sub(r'\bHIP', 'CUDA', content)
    
    # Add cudaLaunchKernelGGL macro if this is a .cu file
    if dst_path.endswith('.cu'):
        macro = """
#include <stdint.h>
#include <stdio.h>
#ifndef cudaLaunchKernelGGL
#define cudaLaunchKernelGGL(kernel, grid, block, sharedMem, stream, ...) \\
    do { \\
        kernel<<<(grid), (block), (sharedMem), (stream)>>>(__VA_ARGS__); \\
        cudaError_t err = cudaGetLastError(); \\
        if (err != cudaSuccess) { \\
            printf("Kernel Launch Error (%s): %s\\n", #kernel, cudaGetErrorString(err)); \\
        } \\
    } while(0)
#endif
"""
        # Insert macro after the last include
        last_include_idx = content.rfind('#include')
        if last_include_idx != -1:
            end_of_line = content.find('\n', last_include_idx)
            content = content[:end_of_line+1] + macro + content[end_of_line+1:]
        else:
            content = macro + content
            
    with open(dst_path, 'w') as f:
        f.write(content)
        
    print(f"Generated {dst_path}")

def main():
    base_dir = os.path.join(os.path.dirname(__file__), '..')
    
    files_to_convert = [
        ('src/solver/HipKernels.hip', 'src/solver/CudaKernels.cu'),
        ('src/solver/HipPCfrSolver.cpp', 'src/solver/CudaPCfrSolver.cpp'),
        ('include/solver/HipPCfrSolver.h', 'include/solver/CudaPCfrSolver.h')
    ]
    
    for src, dst in files_to_convert:
        process_file(
            os.path.join(base_dir, src),
            os.path.join(base_dir, dst)
        )

if __name__ == '__main__':
    main()
