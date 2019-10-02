AC_DEFUN([FC_ANDROID],
[
  AC_MSG_CHECKING([whether we are building for Android])
  AC_COMPILE_IFELSE(
    [AC_LANG_SOURCE(
      [[#ifdef __ANDROID__ \
         int ok;
        #else
         error fail
        #endif
      ]])],
      [android=yes],
      [android=no])
  AC_MSG_RESULT([$android])

  # Handle Android-specific options. They appear only when using an Android
  # compiler.
  AS_IF(test "x$android" == "xyes", [
    android_complete=yes

    # --with-android-ndk (required)
    AC_MSG_CHECKING([for Android NDK])
    AC_ARG_WITH([android-ndk],
      AS_HELP_STRING([--with-android-ndk], [path to Android NDK]),
      [
        AC_SUBST(android_ndk_root, "$withval")
        AC_MSG_RESULT([$android_ndk_root])
      ], [
        AC_MSG_RESULT([no, specify --with-android-ndk])
        android_complete=no
      ])

    # --with-android-ndk (required)
    AC_MSG_CHECKING([for Android SDK])
    AC_ARG_WITH([android-sdk],
      AS_HELP_STRING([--with-android-sdk], [path to Android SDK]),
      [
        AC_SUBST(android_sdk_root, "$withval")
        AC_MSG_RESULT([$android_sdk_root])
      ], [
        AC_MSG_RESULT([no, specify --with-android-sdk])
        android_complete=no
      ])

    # Host architecture (through --host)
    AC_MSG_CHECKING([the Android architecture])
    AS_CASE([$host_alias],
      [aarch64*], [android_architecture=arm64-v8a],
      [armv7a*],  [android_architecture=armeabi-v7a],
      [i686*],    [android_architecture=x86],
      [x86_64*],  [android_architecture=x86_64],
      [
        android_architecture="not supported (\"$host_alias\")"
        android_complete=no
      ])
    AC_MSG_RESULT([$android_architecture])
    AC_SUBST(android_architecture)

    # Qt root (FIXME autodetect)
    AC_MSG_CHECKING([for root of Qt install])
    AC_ARG_WITH([qt5-root],
      AS_HELP_STRING([--with-qt5-root], [path to Qt5 root directory (Android only)]),
      [
        AC_SUBST(qt5_root, "$withval")
        AC_MSG_RESULT([$qt5_root])
      ], [
        AC_MSG_RESULT([no, specify with --with-qt5-root])
        android_complete=no
      ])

    # Fail if required paths are missing
    AS_IF(test "x$android_complete" == "xno", [
      AC_MSG_ERROR(Android setup incomplete)
    ])
  ])
])
