function(enable_strict_compiler)
    # Check if any targets were provided at all
    if(NOT ARGN)
        message(WARNING "enable_strict_compiler: No targets were passed to the function.")
        return()
    endif()

    # Loop through every target passed to the function
    foreach(TARGET_NAME ${ARGN})
        # Validate that the target actually exists
        if(NOT TARGET ${TARGET_NAME})
            message(FATAL_ERROR "enable_strict_compiler: '${TARGET_NAME}' is not a valid CMake target.")
        endif()

        # Only apply flags if we are on MSVC
        if(MSVC)
            # Apply the strict warning/error settings to the current target
            target_compile_options(${TARGET_NAME} PRIVATE
                # --- WARNING LEVELS & ERROR PROMOTION ---
                /Wall          # Enable absolutely ALL warnings
                /WX            # Treat all warnings as errors
                /MP            # Multiprocess compilation (keeps build times fast)
                
                # --- SPECIFIC CODE QUALITY ENFORCEMENT ---
                /permissive-   # Turn on strict ISO C++ standards compliance mode
                
                # --- PROMOTIONS TO ERROR ---
                /we4265        # Class has virtual functions, but destructor is not virtual
                /we4244        # Conversion, possible loss of data
                /we4267        # Conversion from 'size_t' to 'type', possible loss of data
                /we4800        # Implicit conversion from 'type' to bool
                /we4305        # Truncation from 'type' to 'type'
                
                # --- SELECTIVE DISABLES (Crucial noise reduction for /Wall) ---
                /wd4668        # Warns if a preprocessor macro is not defined (Windows.h noise)
                /wd4710        # Function not inlined optimization info
                /wd4711        # Function selected for automatic inline expansion info
                /wd4820        # struct alignment padding notifications
                /wd5039        # Pointer to potentially throwing function passed to extern "C"
                /wd4514        # Unused function
                /wd5045        # Spectre mitigation warnin  
                /wd4275        # Non DLL-interface class used as base for DLL-interface class
                /wd4251        # Non DLL-interface class used as member of DLL-interface class
                /wd4868        # Brace initializer left-to-right evaluation warnings
                /wd4866        # operator[] left-to-right evaluation order (chained subscripts)
                
                # --- EXCEPTIONS CONFIGURATION ---
                $<$<CONFIG:Release>:/EHs-c-> 
                $<$<NOT:$<CONFIG:Release>>:/EHsc>
            )
            message(STATUS "Strict compiler flags enabled for target: ${TARGET_NAME}")
        else()
            message(VERBOSE "enable_strict_compiler: Non-MSVC compiler detected. Skipping flags for target '${TARGET_NAME}'.")
        endif()
    endforeach()
endfunction()