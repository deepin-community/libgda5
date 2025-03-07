# Default provider build options (MySQL, Postgres & unixODBC)
#
# Package build options:
# --with tds
# --with db2
# --with oracle
# --with sybase
# --with mdb
# --with ldap
# --with firebird
# --without sqlite
# --without mysql
# --without odbc
# --without postgres
# 

%define           FREETDS  0
%define           IBMDB2   0
%define           MYSQL    1
%define           ODBC     1
%define           ORACLE   0
%define           POSTGRES 1
%define           SQLITE   1
%define           SYBASE   0
%define 	  MDB	   0
%define		  LDAP	   0
%define		  FIREBIRD 1

%{?_with_tds:%define FREETDS 	1}
%{?_with_db2:%define IBMDB2 	1}
%{?_with_ldap:%define LDAP 	1}
%{?_with_mdb:%define MDB 	1}
%{?_with_oracle:%define ORACLE 	1}
%{?_with_sybase:%define SYBASE 	1}
%{?_with_firebird:%define FIREBIRD 1}
%{?_without_sqlite:%define SQLITE 0}
%{?_without_mysql:%define MYSQL 0}
%{?_without_odbc:%define ODBC 	0}
%{?_without_postgres:%define POSTGRES 0}

Summary:          Library for writing gnome database programs
Name:             libgda
Version:          5.2.10
Release:          1
Epoch:		  1
Source:           %{name}-%{version}.tar.gz
URL:              http://www.gnome-db.org/
Group:            System Environment/Libraries
License:          LGPL
BuildRoot:        %{_tmppath}/%{name}-%{version}-root
BuildRequires:    pkgconfig >= 0.8
Requires:         glib2 >= 2.0.0
Requires:         libxml2
Requires:         libxslt >= 1.0.9
Requires:	  ncurses
BuildRequires:    glib2-devel >= 2.0.0
BuildRequires:    libxml2-devel
BuildRequires:    libxslt-devel >= 1.0.9
BuildRequires:    ncurses-devel
BuildRequires:    scrollkeeper
BuildRequires:    groff
BuildRequires:    readline-devel

%define short_version %( printf '%3.3s' %{version} )

%if %{FREETDS}
BuildRequires:    freetds-devel
%endif

%if %{MYSQL}
BuildRequires:    mysql-devel
%endif

%if %{POSTGRES}
BuildRequires:    postgresql-devel
%endif

%if %{ODBC}
BuildRequires:    unixODBC-devel
%endif

%if %{SQLITE}
BuildRequires:	  sqlite-devel
%endif

%if %{MDB}
BuildRequires:	  mdbtools-devel
%endif

%if %{LDAP}
BuildRequires:	  openldap-devel
%endif

%description
libgda is a library that eases the task of writing
gnome database programs.


%package devel
Summary:          Development libraries and header files for libgda.
Group:            Development/Libraries
Requires:         glib2-devel >= 2.0.0
Requires:         libxml2-devel
Requires:         libxslt-devel >= 1.0.9
Requires:         %{name} = %{epoch}:%{version}-%{release}

%description devel
This package contains the header files and libraries needed to write
or compile programs that use libgda.

%if %{FREETDS}
%package -n gda-freetds
Summary:	GDA FreeTDS Provider
Group:		System Environment/Libraries
%description -n gda-freetds
This package includes the GDA FreeTDS provider.
%endif

%if %{IBMDB2}
%package -n gda-ibmdb2
Summary:	GDA IBM DB2 Provider
Group:		System Environment/Libraries
%description -n gda-ibmdb2
This package includes the GDA IBM DB2 provider.
%endif

%if %{MYSQL}
%package -n gda-mysql
Summary:	GDA MySQL Provider
Group:		System Environment/Libraries
%description -n gda-mysql
This package includes the GDA MySQL provider.
%endif

%if %{ODBC}
%package -n gda-odbc
Summary:	GDA ODBC Provider
Group:		System Environment/Libraries
%description -n gda-odbc
This package includes the GDA ODBC provider.
%endif

%if %{ORACLE}
%package -n gda-oracle
Summary:	GDA Oracle Provider
Group:		System Environment/Libraries
%description -n gda-oracle
This package includes the GDA Oracle provider.
%endif

%if %{POSTGRES}
%package -n gda-postgres
Summary:	GDA PostgreSQL Provider
Group:		System Environment/Libraries
%description -n gda-postgres
This package includes the GDA PostgreSQL provider.
%endif

%if %{SQLITE}
%package -n gda-sqlite
Summary:	GDA SQLite Provider
Group:		System Environment/Libraries
%description -n gda-sqlite
This package includes the GDA SQLite provider.
%endif

%if %{SYBASE}
%package -n gda-sybase
Summary:	GDA Sybase Provider
Group:		System Environment/Libraries
%description -n gda-sybase
This package includes the GDA Sybase provider.
%endif

%if %{MDB}
%package -n gda-mdb
Summary:	GDA MDB Provider
Group:		System Environment/Libraries
%description -n gda-mdb
This package includes the GDA MDB provider.
%endif

%if %{LDAP}
%package -n gda-ldap
Summary:	GDA LDAP Provider
Group:		System Environment/Libraries
%description -n gda-ldap
This package includes the GDA LDAP provider.
%endif

%prep
%setup -q

%build
%if %{FIREBIRD}
CONFIG="$CONFIG --with-firebird"
%else
CONFIG="$CONFIG --without-firebird"
%endif

%if %{FREETDS}
CONFIG="$CONFIG --with-tds"
%else
CONFIG="$CONFIG --without-tds"
%endif

%if %{IBMDB2}
CONFIG="$CONFIG --with-ibmdb2"
%else
CONFIG="$CONFIG --without-ibmdb2"
%endif

%if %{MYSQL}
CONFIG="$CONFIG --with-mysql"
%else
CONFIG="$CONFIG --without-mysql"
%endif

%if %{POSTGRES}
CONFIG="$CONFIG --with-postgres"
%else
CONFIG="$CONFIG --without-postgres"
%endif

%if %{ODBC}
CONFIG="$CONFIG --with-odbc"
%else
CONFIG="$CONFIG --without-odbc"
%endif

%if %{ORACLE}
CONFIG="$CONFIG --with-oracle"
%else
CONFIG="$CONFIG --without-oracle"
%endif

%if %{SQLITE}
CONFIG="$CONFIG --with-sqlite"
%else
CONFIG="$CONFIG --without-sqlite"
%endif

%if %{SYBASE}
CONFIG="$CONFIG --with-sybase"
%else
CONFIG="$CONFIG --without-sybase"
%endif

%if %{MDB}
CONFIG="$CONFIG --with-mdb"
%else
CONFIG="$CONFIG --without-mdb"
%endif

%if %{LDAP}
CONFIG="$CONFIG --with-ldap"
%else
CONFIG="$CONFIG --without-ldap"
%endif

%configure $CONFIG --disable-gtk-doc
make %{?_smp_mflags}

%install
rm -rf %{buildroot}
%makeinstall

# Cleanup unnecessary, unpackaged files
rm -f %{buildroot}/%{_libdir}/libgda-%{short_version}/providers/*.{a,la}
rm -f %{buildroot}/%{_sysconfdir}/libgda-%{short_version}/sales_test.db

%find_lang libgda-%{short_version}

%post -p /sbin/ldconfig

%post devel
if which scrollkeeper-update >/dev/null 2>&1; then scrollkeeper-update; fi

%postun -p /sbin/ldconfig

%postun devel
if which scrollkeeper-update >/dev/null 2>&1; then scrollkeeper-update; fi

%clean
rm -rf %{buildroot}

%files -f libgda-%{short_version}.lang
%defattr(-,root,root)
%doc AUTHORS COPYING ChangeLog README NEWS
%dir %{_sysconfdir}/libgda-%{short_version}
%config(noreplace) %{_sysconfdir}/libgda-%{short_version}/config
%{_bindir}/*
%{_datadir}/libgda-%{short_version}
%{_libdir}/*.so.*
%dir %{_libdir}/libgda-%{short_version}
%dir %{_libdir}/libgda-%{short_version}/providers
%{_mandir}/man1/*
%{_mandir}/man5/*

%files devel
%defattr(-,root,root)
%doc %{_datadir}/gtk-doc/html/libgda-%{short_version}
%dir %{_includedir}/libgda-%{short_version}/
%{_includedir}/libgda-%{short_version}/*
%{_libdir}/*.a
%{_libdir}/*.la
%{_libdir}/*.so
%{_libdir}/pkgconfig/*

%if %{FREETDS}
%files -n gda-freetds
%defattr(-,root,root)
%{_libdir}/libgda-%{short_version}/providers/libgda-freetds.so
%endif

%if %{IBMDB2}
%files -n gda-ibmdb2
%defattr(-,root,root)
%{_libdir}/libgda-%{short_version}/providers/libgda-ibmdb2.so
%endif

%if %{MYSQL}
%files -n gda-mysql
%defattr(-,root,root)
%{_libdir}/libgda-%{short_version}/providers/libgda-mysql.so
%endif

%if %{ODBC}
%files -n gda-odbc
%defattr(-,root,root)
%{_libdir}/libgda-%{short_version}/providers/libgda-odbc.so
%endif

%if %{ORACLE}
%files -n gda-oracle
%defattr(-,root,root)
%{_libdir}/libgda-%{short_version}/providers/libgda-oracle.so
%endif

%if %{POSTGRES}
%files -n gda-postgres
%defattr(-,root,root)
%{_libdir}/libgda-%{short_version}/providers/libgda-postgres.so
%endif

%if %{SQLITE}
%files -n gda-sqlite
%defattr(-,root,root)
%{_libdir}/libgda-%{short_version}/providers/libgda-sqlite.so
%endif

%if %{SYBASE}
%files -n gda-sybase
%defattr(-,root,root)
%{_libdir}/libgda-%{short_version}/providers/libgda-sybase.so
%endif

%if %{MDB}
%files -n gda-mdb
%defattr(-,root,root)
%{_libdir}/libgda-%{short_version}/providers/libgda-mdb.so
%endif

%if %{LDAP}
%files -n gda-ldap
%defattr(-,root,root)
%{_libdir}/libgda-%{short_version}/providers/libgda-ldap.so
%endif

%changelog
* Fri Nov  2 2007 Petr Klima <qaxi@seznam.cz> 1:3.0.1-1
- Incorporate diferencies form version 1.1
- update %files for new includes & provs
- version added to dir names (the same way as make install do)

* Tue Oct 12 2004 David Hollis <dhollis@davehollis.com> 1:1.1.99
- Incorporate RH changes, update %files for new includes & provs

* Fri Oct  8 2004 Caolan McNamara <caolanm@redhat.com> 1:1.0.4-3
- #rh135043# Extra BuildRequires

* Thu Sep  9 2004 Bill Nottingham <notting@redhat.com> 1:1.0.4-2
- %%defattr

* Thu Aug 12 2004 Caolan McNamara <caolanm@redhat.com> 1:1.0.4-1
- Initial Red Hat import
- patch for missing break statement
- fixup devel package requirement pickiness
- autoconf patch to pick up correct mysql path from mysql_config (e.g. x64)
- autoconf patch to just look in the normal place for postgres first

* Tue Mar 11 2003 David Hollis <dhollis@davehollis.com>
- Fix --with-tds & --without-tds to match what configure wants

* Tue Jan 28 2003 Yanko Kaneti <yaneti@declera.com>
- Remove the idl path
- Include gda-config man page
- add --without-* for disabled providers
- package and use the omf/scrollkeeper bits

* Tue Dec 31 2002 David Hollis <dhollis@davehollis.com>
- Added sqlite-devel buildreq
- Include gda-config-tool man page

* Mon Aug 19 2002 Ben Liblit <liblit@acm.org>
- Fixed version number substitutions

- Removed some explicit "Requires:" prerequisites that RPM will figure
  out on its own.  Removed explicit dependency on older MySQL client
  libraries

- Required that the ODBC development package be installed if we are
  building the ODBC provider

- Created distinct subpackages for each provider, conditional on that
  provider actually being enabled; some of these will need to be
  updated as the family of available providers changes

- Updated files list to match what "make install" actually installs

- Added URL tag pointing to GNOME-DB project's web site

* Tue Feb 26 2002 Chris Chabot <chabotc@reviewboard.com>
- Added defines and configure flags for all supported DB types

* Mon Feb 25 2002 Chris Chabot <chabotc@reviewboard.com>
- Cleaned up formatting
- Added Requirements
- Added defines for postgres, mysql, odbc support

* Thu Feb 21 2002 Chris Chabot <chabotc@reviewboard.com>
- Initial spec file
