FROM centos:centos7

ARG PROXY
RUN if [[ -n "${PROXY}" ]] ; then echo "proxy=${PROXY}" >> /etc/yum.conf ; fi

RUN yum -y install		\
	rpm-build		\
	git			\
	redhat-rpm-config 	\
	systemd-devel		\
	gcc 	 		\
        make 			\
        git 			\
        pkgconfig 		\
        rpm-build		\
	openssl-devel

RUN useradd rpmbuild

USER rpmbuild
ENV HOME /home/rpmbuild
WORKDIR /home/rpmbuild
RUN mkdir -p /home/rpmbuild/{BUILD,RPMS,SOURCES,SPECS,SRPMS}

RUN echo '%_topdir %{getenv:HOME}' > /home/rpmbuild/.rpmmacros

