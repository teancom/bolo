Name:           bolo
Version:        0.2.4
Release:        1%{?dist}
Summary:        Monitoring System Server

Group:          Applications/System
License:        GPLv3+
URL:            https://github.com/filefrog/ctap
Source0:        %{name}-%{version}.tar.gz
BuildRoot:      %{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)

BuildRequires:  autoconf
BuildRequires:  automake
BuildRequires:  gcc
BuildRequires:  libctap-devel
BuildRequires:  pcre-devel
BuildRequires:  zeromq-devel
BuildRequires:  rrdtool-devel
BuildRequires:  libvigor-devel
BuildRequires:  postgresql-devel

%description
bolo is a lightweight and scalable monitoring system that can
track samples, counters, states and configuration data.

This package provides the server implementation.

%package clients
Summary:        Monitoring System Clients
Group:          Applications/System

%description clients
bolo is a lightweight and scalable monitoring system that can
track samples, counters, states and configuration data.

This package provides client programs for bolo.


%package subscribers
Summary:        Monitoring System Subscribers
Group:          Applications/System

%description subscribers
bolo is a lightweight and scalable monitoring system that can
track samples, counters, states and configuration data.

This package provides subscriber components for bolo.


%prep
%setup -q


%build
%configure
make %{?_smp_mflags}


%install
rm -rf $RPM_BUILD_ROOT
make install DESTDIR=$RPM_BUILD_ROOT
rm -f $RPM_BUILD_ROOT%{_bindir}/bolo_nsca

install -m 0644 -D examples/bolo.conf $RPM_BUILD_ROOT%{_sysconfdir}/bolo.conf
install -m 0644 -D examples/schema/pg.sql $RPM_BUILD_ROOT%{_datadir}/bolo/schema/pg.sql


%clean
rm -rf $RPM_BUILD_ROOT


%files
%defattr(-,root,root,-)
%{_bindir}/bolospy
%{_sbindir}/bolo
%{_sysconfdir}/bolo.conf
%{_mandir}/man5/bolo.conf.5.gz
%{_mandir}/man8/bolo.8.gz

%files clients
%defattr(-,root,root,-)
%{_sbindir}/dbolo
%{_bindir}/send_bolo
%{_bindir}/stat_bolo
%{_mandir}/man1/send_bolo.1.gz
%{_mandir}/man1/stat_bolo.1.gz

%files subscribers
%defattr(-,root,root,-)
%{_sbindir}/bolo2console
%{_sbindir}/bolo2log
%{_sbindir}/bolo2pg
%{_sbindir}/bolo2rrd
%{_mandir}/man8/bolo2pg.8.gz
%{_mandir}/man8/bolo2rrd.8.gz
%doc %{_datadir}/bolo

%changelog
* Tue May 19 2015 James Hunt <james@niftylogic.com> 0.2.4-1
- Initial RPM package
