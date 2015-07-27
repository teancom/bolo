Name:           bolo
Version:        0.2.10
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

%prep
%setup -q


%build
%configure --with-rrd-subscriber --with-pg-subscriber --without-sqlite-subscriber
make %{?_smp_mflags}


%install
rm -rf $RPM_BUILD_ROOT
make install DESTDIR=$RPM_BUILD_ROOT
rm -f $RPM_BUILD_ROOT%{_bindir}/bolo_nsca

# dist config
install -m 0644 -D examples/bolo.conf       $RPM_BUILD_ROOT%{_sysconfdir}/bolo.conf
install -m 0644 -D examples/schema/pg.sql   $RPM_BUILD_ROOT%{_datadir}/bolo/schema/pg.sql
# init scripts
install -m 0755 -D redhat/init.d/dbolo      $RPM_BUILD_ROOT%{_initrddir}/dbolo
install -m 0755 -D redhat/init.d/bolo       $RPM_BUILD_ROOT%{_initrddir}/bolo
install -m 0755 -D redhat/init.d/bolo2rrd   $RPM_BUILD_ROOT%{_initrddir}/bolo2rrd
install -m 0755 -D redhat/init.d/bolo2pg    $RPM_BUILD_ROOT%{_initrddir}/bolo2pg


%clean
rm -rf $RPM_BUILD_ROOT


%post
/sbin/chkconfig --add bolo


%preun
if [ $1 == 0 ]; then # erase!
	/sbin/service stop bolo
	/sbin/chkconfig --del bolo
fi


%postun
if [ $1 == 0 ]; then # upgrade!
	/sbin/service condrestart bolo
fi


%files
%defattr(-,root,root,-)
%{_bindir}/bolospy
%{_sbindir}/bolo
%{_initrddir}/bolo
%config %{_sysconfdir}/bolo.conf
%{_mandir}/man5/bolo.conf.5.gz
%{_mandir}/man8/bolo.8.gz


#######################################################################

%package clients
Summary:        Monitoring System Clients
Group:          Applications/System

%description clients
bolo is a lightweight and scalable monitoring system that can
track samples, counters, states and configuration data.

This package provides client programs for bolo.


%post clients
/sbin/chkconfig --add dbolo


%preun clients
if [ $1 == 0 ]; then # erase!
	/sbin/service stop dbolo
	/sbin/chkconfig --del dbolo
fi


%postun clients
if [ $1 == 0 ]; then # upgrade!
	/sbin/service condrestart dbolo
fi


%files clients
%defattr(-,root,root,-)
%{_sbindir}/dbolo
%{_initrddir}/dbolo
%{_bindir}/send_bolo
%{_bindir}/stat_bolo
%{_mandir}/man1/send_bolo.1.gz
%{_mandir}/man1/stat_bolo.1.gz


#######################################################################

%package subscribers
Summary:        Monitoring System Subscribers
Group:          Applications/System

%description subscribers
bolo is a lightweight and scalable monitoring system that can
track samples, counters, states and configuration data.

This package provides subscriber components for bolo.


%post subscribers
/sbin/chkconfig --add bolo2rrd
/sbin/chkconfig --add bolo2pg


%preun subscribers
if [ $1 == 0 ]; then # erase!
	/sbin/service stop bolo2rrd
	/sbin/chkconfig --del bolo2rrd

	/sbin/service stop bolo2pg
	/sbin/chkconfig --del bolo2pg
fi


%postun subscribers
if [ $1 == 0 ]; then # upgrade!
	/sbin/service condrestart bolo2rrd
	/sbin/service condrestart bolo2pg
fi


%files subscribers
%defattr(-,root,root,-)
%{_sbindir}/bolo2console
%{_sbindir}/bolo2log
%{_sbindir}/bolo2pg
%{_sbindir}/bolo2rrd
%{_initrddir}/bolo2pg
%{_initrddir}/bolo2rrd
%{_mandir}/man8/bolo2pg.8.gz
%{_mandir}/man8/bolo2rrd.8.gz
%doc %{_datadir}/bolo


#######################################################################

%changelog
* Mon Jul 27 2015 James Hunt <james@niftylogic.com> 0.2.10-1
- Upstream release

* Thu Jul 23 2015 James Hunt <james@niftylogic.com> 0.2.9-1
- Upstream release (memory leak fix)

* Wed Jul 22 2015 James Hunt <james@niftylogic.com> 0.2.8-1
- Upstream release

* Wed Jul 15 2015 James Hunt <james@niftylogic.com> 0.2.7-1
- Upstream release

* Thu Jul  2 2015 James Hunt <james@niftylogic.com> 0.2.6-1
- Upstream release

* Wed Jun 10 2015 James Hunt <james@niftylogic.com> 0.2.5-1
- Upstream release

* Tue Jun  9 2015 James Hunt <james@niftylogic.com> 0.2.4-2
- Force bolo2pg and bolo2rrd subscribers via ./configure options
- Package init scripts and pre/post chckonfig/service magic

* Tue May 19 2015 James Hunt <james@niftylogic.com> 0.2.4-1
- Initial RPM package
