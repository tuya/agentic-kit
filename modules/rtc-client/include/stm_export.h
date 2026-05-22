
#ifndef STM_EXPORT_H
#define STM_EXPORT_H

#ifdef STM_STATIC_DEFINE
#  define STM_EXPORT
#  define STM_NO_EXPORT
#else
#  ifndef STM_EXPORT
#    ifdef stm_EXPORTS
        /* We are building this library */
#      define STM_EXPORT 
#    else
        /* We are using this library */
#      define STM_EXPORT 
#    endif
#  endif

#  ifndef STM_NO_EXPORT
#    define STM_NO_EXPORT 
#  endif
#endif

#ifndef STM_DEPRECATED
#  define STM_DEPRECATED __attribute__ ((__deprecated__))
#endif

#ifndef STM_DEPRECATED_EXPORT
#  define STM_DEPRECATED_EXPORT STM_EXPORT STM_DEPRECATED
#endif

#ifndef STM_DEPRECATED_NO_EXPORT
#  define STM_DEPRECATED_NO_EXPORT STM_NO_EXPORT STM_DEPRECATED
#endif

/* NOLINTNEXTLINE(readability-avoid-unconditional-preprocessor-if) */
#if 0 /* DEFINE_NO_DEPRECATED */
#  ifndef STM_NO_DEPRECATED
#    define STM_NO_DEPRECATED
#  endif
#endif

#endif /* STM_EXPORT_H */
