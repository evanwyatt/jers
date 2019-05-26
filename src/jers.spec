%{!?rel: %define rel 1}
%{!?major_ver: %define major_ver 0}
%{!?minor_ver: %define minor_ver 0}
%{!?patch: %define patch 0}

Name:		jers
Version:	%{major_ver}.%{minor_ver}.%{patch}
Release:	%{rel}%{?dist}
Summary:	The Job Execution and Resource Scheduler

License:	BSD-3-Clause
URL:		https://github.com/evanwyatt/jers
Source0:	%{name}-%{version}-%{rel}.tar.gz

%{?systemd_requires}
BuildRequires: systemd
Requires:   logrotate
Requires:   sudo

%description
The Job Execution and Resource Scheduler

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
install -d %{buildroot}%{_bindir} %{buildroot}%{_includedir} %{buildroot}%{_libdir}
install -d %{buildroot}/%{_unitdir} %{buildroot}%{_sysconfdir}/logrotate.d
install -d %{buildroot}/%{_unitdir} %{buildroot}%{_tmpfilesdir}
install -d %{buildroot}%{_localstatedir}/log/%{name}
install -d %{buildroot}%{_sysconfdir}/%{name}
install -d %{buildroot}%{_sharedstatedir}/%{name} %{buildroot}%{_sharedstatedir}/%{name}/state

make install DESTDIR=%{buildroot}%{_prefix}

find %buildroot -type f \( -name '*.so' -o -name '*.so.*' \) -exec chmod 755 {} +

install -Dm 0644 src/jersd.service src/jers_agentd.service %{buildroot}/%{_unitdir}/
install -m 0644 src/jers_logrotate.conf %{buildroot}%{_sysconfdir}/logrotate.d/%{name}
install -m 0644 src/jers_tmpfiles.conf %{buildroot}%{_tmpfilesdir}/%{name}.conf
install -m 0644 src/default.conf %{buildroot}%{_sysconfdir}/%{name}/%{name}.conf

%files
%{_bindir}/jers
%{_bindir}/jersd
%{_bindir}/jers_agentd
%{_bindir}/jers_dump_env
%{_libdir}/libjers.so.%{major_ver}.%{minor_ver}.%{patch}
%{_libdir}/libjers.so.%{major_ver}

# Systemd service files
%config(noreplace) %{_sysconfdir}/jers/jers.conf
%config(noreplace) %{_unitdir}/*.service
%config(noreplace) %{_sysconfdir}/logrotate.d/jers
%config(noreplace) %{_tmpfilesdir}/jers.conf

%dir %attr(0755,jers,jers) %{_localstatedir}/log/jers
%dir %attr(0750,jers,jers) %{_sharedstatedir}/jers/state

%files devel
%{_includedir}/jers.h
%{_libdir}/libjers.so

%clean
rm -rf $RPM_BUILD_ROOT
rm -rf $RPM_BUILD_DIR/%{name}-%{version}

%pre
# Add a default 'jers' user/group
getent group jers >/dev/null || /usr/sbin/groupadd -r jers || /usr/bin/true
getent passwd jers >/dev/null || /usr/sbin/useradd -g jers -s /bin/false -r -c "JERS Batch scheduler user" jers || /usr/bin/true


%post
/sbin/ldconfig
/usr/bin/systemctl daemon-reload
systemd-tmpfiles --create %{name}.conf

%postun
/sbin/ldconfig
/usr/bin/systemctl daemon-reload

%changelog



