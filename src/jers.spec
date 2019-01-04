Name:		jers
Version:	0.0.1
Release:	1%{?dist}
Summary:	The Job Execution and Resource Scheduler

License:	BSD-3-Clause
URL:		https://github.com/evanwyatt/jers
Source0:	%{name}-%{version}-1.tar.gz

%description
The Job Execution and Resource Scheduler

%{?systemd_requires}
BuildRequires: systemd

%package devel
Summary:	JERS development files
Requires:	%{name}

%description devel
Development files for Job Execution and Resource Scheduler

%prep
%setup -q


%build
make %{?_smp_mflags}


%install
rm -rf $RPM_BUILD_ROOT
install -d %{buildroot}/usr/bin %{buildroot}/usr/include %{buildroot}/usr/lib
install -d %{buildroot}/%{_unitdir}

make install DESTDIR=%{buildroot}%{_prefix}

find %buildroot -type f \( -name '*.so' -o -name '*.so.*' \) -exec chmod 755 {} +

install -Dm 0644 src/jersd.service src/jers_agentd.service %{buildroot}/%{_unitdir}/ 

%files
%{_bindir}/jers
%{_bindir}/jersd
%{_bindir}/jers_agentd
%{_bindir}/jers_dump_env
%{_libdir}/libjers.so

# Systemd service files
%{_unitdir}/*.service

%files devel
%{_includedir}/jers.h

%clean
rm -rf $RPM_BUILD_ROOT
rm -rf $RPM_BUILD_DIR/%{name}-%{version}

%post
/sbin/ldconfig

%postun
/sbin/ldconfig


%changelog



