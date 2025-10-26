/**
 * HimuOperatingSystem
 *
 * File: _hobase.h
 * Description: Base header file for HimuOS, defining basic types and structures.
 * Module: (null)
 *
 * Copyright(c) 2024-2025 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include "libc/stddef.h"
#include "libc/string.h"

#define TRUE  1
#define FALSE 0

typedef uint8_t BOOL;

typedef uint64_t HO_PHYSICAL_ADDRESS;
typedef uint64_t HO_VIRTUAL_ADDRESS;

/* APIs that can only be accessed within the kernel */
#define HO_KERNEL_API  __attribute__((visibility("default")))

/* APIs that can be accessed by user applications or kernel */
#define HO_SYSTEM_CALL __attribute__((visibility("default")))

/* APIs that are part of the public API and can be used by external modules */
#define HO_PUBLIC_API  __attribute__((visibility("default")))

/* APIs that are strictly internal to a single kernel component
    or file and should not be used by other parts of the kernel */
#define HO_PRIVATE_API __attribute__((visibility("hidden")))

#define HO_NORETURN    __attribute__((noreturn))

#define HO_LIKELY(x)   __builtin_expect(!!(x), 1)

// IN is used to indicate input parameters in function declarations
#define IN

// OUT is used to indicate output parameters in function declarations
#define OUT

// IN_OUT is used to indicate parameters that are both input and output
#define IN_OUT

// Should check the return value of the function
#define HO_NODISCARD __attribute__((warn_unused_result))

// This field is private and should not be accessed directly.
#define HO_PRIVATE_FIELD
/**
 * MAYBE_UNUSED
 * @brief Marks a variable or function as possibly unused to suppress compiler warnings.
 *
 * This macro expands to the GCC/Clang-specific `__attribute__((unused))`, which tells the compiler
 * that the annotated variable or function may not be used, preventing "unused variable" or "unused function"
 * warnings during compilation.
 */
#define MAYBE_UNUSED __attribute__((unused))

// This class or struct is a kernel structure, which is opaque to user applications or
// public APIs. It is only used internally by the kernel.
#define HO_INTERNAL_STRUCT

//
// HimuOS Error Codes
//

enum _HIMUOS_ERROR_CODE
{
    EC_SUCCESS = 0,       // Operation successful
    EC_FAILURE,           // General failure
    EC_ILLEGAL_ARGUMENT,  // Illegal argument
    EC_NOT_ENOUGH_MEMORY, // Not enough memory
    EC_UNREACHABLE,       // Should never reach here
    EC_NOT_SUPPORTED,     // Operation not supported
    EC_OUT_OF_RESOURCE    // Out of resource
};

typedef int HO_STATUS;

//
// Utilities
//

/**
 * @brief Defines a variable of the specified type and name, and initializes it to zero.
 *
 * This macro creates a variable of the given type and name, then sets all its bytes to zero using memset.
 *
 * @param type The data type of the variable to be defined.
 * @param name The name of the variable to be defined and initialized.
 *
 * @note This macro should be used with caution for types that contain pointers or require non-zero initialization.
 */
#define HO_PARAM(type, name)                                                                                           \
    type name;                                                                                                         \
    memset(&name, 0, sizeof(type))

#define HO_ALIGN_UP(value, alignment)   (((value) + ((alignment)-1)) & ~((alignment)-1))
#define HO_ALIGN_DOWN(value, alignment) ((value) & ~((alignment)-1))
