dnl SEARCH_FOR_LIB(LIB, FUNCTIONS, FUNCTION,
dnl                [ACTION-IF-NOT-FOUND],
dnl                [LIBS_TO_ADD])

AC_DEFUN([SEARCH_FOR_LIB],
[
  AS_VAR_PUSHDEF([with_lib], [with_lib$1])
  AS_VAR_PUSHDEF([ac_header], [ac_cv_header_$3])
  AS_VAR_PUSHDEF([have_lib], [ac_cv_have_$1])
  AS_VAR_PUSHDEF([libs_var], AS_TR_CPP([$1_LIBS]))
  AS_VAR_PUSHDEF([cppflags_var], AS_TR_CPP([$1_CPPFLAGS]))
  AS_LITERAL_IF([$1],
                [AS_VAR_PUSHDEF([ac_lib], [ac_cv_lib_$1_$2])],
                [AS_VAR_PUSHDEF([ac_lib], [ac_cv_lib_$1''_$2])])

  AC_ARG_WITH([lib$1],
    [AS_HELP_STRING([--with-lib$1@<:@=DIR@:>@],
       [Use lib$1 in DIR])],
    [ AS_VAR_SET([with_lib], [$withval]) ],
    [ AS_VAR_SET([with_lib], [yes]) ])

  AS_IF([test AS_VAR_GET([with_lib]) = yes],[
    AC_CHECK_HEADERS([$3])

    my_save_LIBS="$LIBS"
    LIBS="$5"
    AC_CHECK_LIB($1, $2)
    AS_VAR_SET([libs_var],[${LIBS}])
    LIBS="${my_save_LIBS}"
    AS_VAR_SET([cppflags_var],[""])
    AS_IF([test AS_VAR_GET([ac_header]) = "$3" -a AS_VAR_GET([ac_lib]) = yes],
      [AS_VAR_SET([have_lib],[yes])],
      [AS_VAR_SET([have_lib],[no])
       AS_VAR_SET([with_lib],["AS_VAR_GET([with_lib]) /usr/local /opt/csw"])
      ])
  ])
  AS_IF([test "AS_VAR_GET([with_lib])" != yes],[
   for libloc in AS_VAR_GET([with_lib])
   do
    AC_MSG_CHECKING(for $1 in $libloc)
    if test -f $libloc/$3 -a -f $libloc/lib$1.a
    then
      owd=`pwd`
      if cd $libloc; then libloc=`pwd`; cd $owd; fi
      AS_VAR_SET([cppflags_var],[-I$libloc])
      AS_VAR_SET([libs_var],["-L$libloc -l$1"])
      AS_VAR_SET([have_lib],[yes])
      break
    elif test -f $libloc/include/$3 -a -f $libloc/lib/lib$1.a; then
      owd=`pwd`
      if cd $libloc; then libloc=`pwd`; cd $owd; fi
      AS_VAR_SET([cppflags_var],[-I$libloc/include])
      AS_VAR_SET([libs_var],["-L$libloc/lib -l$1"])
      AS_VAR_SET([have_lib],[yes])
      break
    else
      AS_VAR_SET([have_lib],[no])
    fi
   done
  ])
  AS_IF([test AS_VAR_GET([have_lib]) = no],[
    AC_MSG_WARN([$3 or lib$1.a not found. Try installing $1 developement packages])
    [$4]
  ])
  AC_SUBST(libs_var)
  AC_SUBST(cppflags_var)
  AM_CONDITIONAL(AS_TR_CPP(HAVE_$1),[test AS_VAR_GET([have_lib]) = yes])
  AS_VAR_POPDEF([with_lib])
  AS_VAR_POPDEF([ac_header])
  AS_VAR_POPDEF([libs_var])
  AS_VAR_POPDEF([cppflags_var])
  AS_VAR_POPDEF([have_lib])
  AS_VAR_POPDEF([ac_lib])
])    
