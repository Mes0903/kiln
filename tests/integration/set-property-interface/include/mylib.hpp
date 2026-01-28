#ifndef MYLIB_HPP
#define MYLIB_HPP

// Check that the macros were propagated via set_property
#ifndef USE_MYLIB
#error "USE_MYLIB should be defined via INTERFACE_COMPILE_DEFINITIONS"
#endif

#ifndef MYLIB_VERSION
#error "MYLIB_VERSION should be defined via INTERFACE_COMPILE_DEFINITIONS"
#endif

#ifndef VIA_SET_PROPERTY
#error "VIA_SET_PROPERTY should be defined via INTERFACE_COMPILE_OPTIONS"
#endif

inline int get_value() {
    return MYLIB_VERSION * 10;
}

#endif // MYLIB_HPP
