// txio
  "__root_epoch",
  "__open_subepoch",
  "__close_epoch",
  "__epoch",
  "__txio_compute_epoch",
  "__txio_last_epoch",
  "__txio_free_epoch",
  "__txio_vfprintf",
  "__txio_vprintf",
  "__txio_fprintf",
  "__txio_printf",
  "__txio_fputs",
  "__txio_puts",
  "__txio_fwrite",
  "__txio_fflush",
  "__txio_fclose",
  "__txio_fputc",
  "__txio_putc",
  "__txio_putchar",
  "__txio__IO_putc",
  "__txio_exit",
  "__txio_abort",
  "__txio___assert_fail",
  "__txio_remove",
  "__txio_perror",
  "__announce_restricted",

  "__txio___deferred_store_i32",
  "__txio___deferred_store_i64",
  "__txio___deferred_store_float",
  "__txio___deferred_store_double",

  "__txio___deferred_load_i32",
  "__txio___deferred_load_i64",
  "__txio___deferred_load_float",
  "__txio___deferred_load_double",

  "__txio___deferred_add_i32",
  "__txio___deferred_add_i64",
  "__txio___deferred_add_float",
  "__txio___deferred_add_double",

  "__txio___deferred_calll",

// llvm
  "llvm.pcmarker",
  "llvm.sqrt.f32",
  "llvm.sqrt.f64",
  "llvm.powi.f32",
  "llvm.powi.f64",
  "llvm.sin.f32",
  "llvm.sin.f64",
  "llvm.cos.f32",
  "llvm.cos.f64",
  "llvm.ctlz.i8",   // count leading zeros (appears in 447.dealII, believe it or not)
  "llvm.ctlz.i16",
  "llvm.ctlz.i32",
  "llvm.ctlz.i64",
  "llvm.cttz.i8",   // count trailing zeros.
  "llvm.cttz.i16",
  "llvm.cttz.i32",
  "llvm.cttz.i64",
  "llvm.pow.f32",
  "llvm.pow.f64",
  "llvm.fabs.f32",
  "llvm.fabs.f64",

// New with llvm 135637
  "llvm.lifetime.start",
  "llvm.lifetime.start.p0i8",
  "llvm.lifetime.end",
  "llvm.lifetime.end.p0i8",
  "llvm.invariant.start",
  "llvm.invariant.start.p0i8",
  "llvm.invariant.end",
  "llvm.invariant.end.p0i8",
  "llvm.var.annotation",
  "llvm.annotation.i8",
  "llvm.annotation.i16",
  "llvm.annotation.i32",
  "llvm.annotation.i64",
  "llvm.objectsize.i32",
  "llvm.objectsize.i64",

// llvm debug
  "llvm.dbg.value",
  "llvm.dbg.declare",

// math.h
  "acos",
  "acosf",
  "acosh",
  "acoshf",
  "acoshl",
  "acosl",
  "asin",
  "asinf",
  "asinh",
  "asinhf",
  "asinhl",
  "asinl",
  "atan",
  "atan2",
  "atan2f",
  "atan2l",
  "atanf",
  "atanh",
  "atanhf",
  "atanhl",
  "atanl",
  "cbrt",
  "cbrtf",
  "cbrtl",
  "ceil",
  "ceilf",
  "ceill",
  "copysign",
  "copysignf",
  "copysignl",
  "cos",
  "cosf",
  "cosh",
  "coshf",
  "coshl",
  "cosl",
  "erf",
  "erfc",
  "erfcf",
  "erfcl",
  "erff",
  "erfl",
  "exp",
  "exp2",
  "exp2f",
  "exp2l",
  "expf",
  "expl",
  "expm1",
  "expm1f",
  "expm1l",
  "fabs",
  "fabsf",
  "fabsl",
  "fdim",
  "fdimf",
  "fdiml",
  "floor",
  "floorf",
  "floorl",
  "fma",
  "fmaf",
  "fmal",
  "fmax",
  "fmaxf",
  "fmaxl",
  "fmin",
  "fminf",
  "fminl",
  "fmod",
  "fmodf",
  "fmodl",
  "frexp",
  "frexpf",
  "frexpl",
  "hypot",
  "hypotf",
  "hypotl",
  "ilogb",
  "ilogbf",
  "ilogbl",
  "j0",
  "j1",
  "jn",
  "ldexp",
  "ldexpf",
  "ldexpl",
  "lgamma",
  "lgammaf",
  "lgammal",
  "llrint",
  "llrintf",
  "llrintl",
  "llround",
  "llroundf",
  "llroundl",
  "log",
  "log10",
  "log10f",
  "log10l",
  "log1p",
  "log1pf",
  "log1pl",
  "log2",
  "log2f",
  "log2l",
  "logb",
  "logbf",
  "logbl",
  "logf",
  "logl",
  "lrint",
  "lrintf",
  "lrintl",
  "lround",
  "lroundf",
  "lroundl",
  "modf",
  "modff",
  "modfl",
  "nan",
  "nanf",
  "nanl",
  "nearbyint",
  "nearbyintf",
  "nearbyintl",
  "nextafter",
  "nextafterf",
  "nextafterl",
  "nexttoward",
  "nexttowardf",
  "nexttowardl",
  "pow",
  "powf",
  "powl",
  "remainder",
  "remainderf",
  "remainderl",
  "remquo",
  "remquof",
  "remquol",
  "rint",
  "rintf",
  "rintl",
  "round",
  "roundf",
  "roundl",
  "scalb",
  "scalbln",
  "scalblnf",
  "scalblnl",
  "scalbn",
  "scalbnf",
  "scalbnl",
  "sin",
  "sinf",
  "sinh",
  "sinhf",
  "sinhl",
  "sinl",
  "sqrt",
  "sqrtf",
  "sqrtl",
  "tan",
  "tanf",
  "tanh",
  "tanhf",
  "tanhl",
  "tanl",
  "tgamma",
  "tgammaf",
  "tgammal",
  "trunc",
  "truncf",
  "truncl",
  "y0",
  "y1",
  "yn",

// ctype.h
  "toupper",
  "tolower",
  "isalnum",
  "isalpha",
  "isascii",
  "isblank",
  "iscntrl",
  "isdigit",
  "isgraph",
  "islower",
  "isprint",
  "ispunct",
  "isspace",
  "isupper",
  "isxdigit",
  "__ctype_b_loc",       // Used by ctype.h to lookup characters in a locale table.
  "__ctype_toupper_loc", // locale-specific backend of toupper()
  "__ctype_tolower_loc",

// stdlib.h
  "atoi",
  "calloc",
  "malloc",
  "free",

// string.h
  "memchr",
  "strchr",
  "strcmp",
  "strcoll",
  "strcspn",
  "strlen",
  "strncmp",
  "strpbrk",
  "strrchr",
  "strspn",
  "strstr",

// strings.h
  "strcasecmp",
  "strncasecmp",

// libautocuda.so
  "lcuAlloca",
  "lcuCheck",
  "lcuMemArrayRelease",
  "lcuMemcpyDtoD",
  "lcuMemMapArrayHtoD",
  "lcuMemMapHtoD",
  "lcuMemmoveDtoD",
  "lcuMemRelease",
  "lcuMemsetD8",
  "lcuRegisterGlobal",
  "lcuLaunchGrid",

// cuda.h
  "cuMemcpyHtoD",
  "cuMemcpyHtoD_v2",
  "cuModuleGetGlobal",
  "cuParamSeti",
  "cuParamSetf",
  "cuParamSetv",
  "cuParamSetSize",
  "cuFuncSetBlockShape",
  "cuModuleGetSize",
  "cuModuleGetFunction",
  "cuLaunchGrid",
