# Dockerfile to create a Dakota development environment
#

# use rolling build to get latest updates
FROM centos:7
MAINTAINER Jon Wilson <jwilso@sandia.gov>

# RUN yum -y install applydeltarpm

# ==========================================================================
# Install requested RPMs (package order is by Docker convention)
RUN yum -y update && yum -y install \
blas.x86_64 \
blas-devel.x86_64 \
boost.x86_64 \
boost-devel.x86_64 \
cmake.x86_64 \
gcc.x86_64 \
gcc-c++.x86_64 \
gcc-gfortran.x86_64 \
git.x86_64 \
gsl.x86_64 \
gsl-devel.x86_64 \
java-1.8.0-openjdk-devel.x86_64 \
lapack.x86_64 \
lapack-devel.x86_64 \
make.x86_64 \
openmpi.x86_64 \
openmpi-devel.x86_64 \
perl.x86_64 \
python.x86_64 \
epel-release.noarch \
ed \
vim-enhanced

# (EPEL needed for CMake 3.x)
# ed needed to fix HDF5
# vim needed for developer sanity

RUN yum -y update && yum -y install \
cmake3.x86_64

# Listing potential packages that may be added later

## For documentation build
# RUN yum -y install \
# doxygen.x86_64 \
# texlive-collection-latexrecommended.noarch

## For developer convenience
# RUN yum -y install \
# dos2unix.x86_64 \
# emacs.x86_64 \
# gitk.noarch \
# subversion.x86_64 \
# valgrind.x86_64

## If later using NFS-mounted TPLs
# RUN yum -y install \
# redhat-lsb-core.x86_64

RUN yum -y update 


## Could sync sems TPLs into RHEL7 docker, for now trying this HDF5 binary
RUN mkdir /opt/hdf5/
ADD ./hdf5-1.10.4-linux-centos7-x86_64-gcc485-shared.tar.gz /opt/hdf5/
RUN mv /opt/hdf5/hdf5-1.10.4-linux-centos7-x86_64-gcc485-shared /opt/hdf5/1.10.4
RUN cd /opt/hdf5/1.10.4/bin && ./h5redeploy -prefix=/opt/hdf5/1.10.4 -force && cd -

# ==========================================================================
# Finish construction
#   - create workspace directory
#   - install gosu utility (be sure that proxy is defined)
#   - install and configure entrypoint


##RUN mkdir /workspace

# Install "gosu" utility
# 1. Import Tianon Gravi/Andrew Page's public key
# 2. Download gosu app and app signature
# 3. Verify gosu app with app signature and Tianon's public key
# 4. Install app
RUN gpg --keyserver pool.sks-keyservers.net --recv-keys B42F6819007F00F88E364FD4036A9C25BF357DD4 \
    && curl -o /usr/local/bin/gosu -SL "https://github.com/tianon/gosu/releases/download/1.10/gosu-amd64" \
    && curl -o /usr/local/bin/gosu.asc -SL "https://github.com/tianon/gosu/releases/download/1.10/gosu-amd64.asc" \
    && gpg --verify /usr/local/bin/gosu.asc \
    && rm /usr/local/bin/gosu.asc \
    && rm -r /root/.gnupg/ \
    && chmod +x /usr/local/bin/gosu


# install and configure entrypoint script
COPY entrypoint.sh /usr/local/bin/entrypoint.sh
ENTRYPOINT ["/usr/local/bin/entrypoint.sh"]

