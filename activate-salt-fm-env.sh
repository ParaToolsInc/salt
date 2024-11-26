echo "purging loaded modules"
module purge
echo "listing loaded modules:"
module list
#echo "loading llvm and mpich:"
echo "loading llvm and gcc:"
module load llvm/git.086d8e6bb5daf8de43880ba90258c49e0fabf2c9_19.1.4-zpacv56
#module load mpich/4.2.3-ugxzfxf
module load gcc/14.2.0-ttkqi3s
#module load tau/master-l3jx42k
export LD_LIBRARY_PATH="/storage/packages/salt-fm/spack/opt/spack/linux-centos7-x86_64/gcc-10.2.1/gcc-14.2.0-ttkqi3sp7xwrxtwftfysf54cl4jje4qk/lib64:$LD_LIBRARY_PATH"
echo "listing loaded modules:"
module list
echo "Finished"
