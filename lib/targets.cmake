#Query all sources
file(GLOB_RECURSE SOURCE
		${PROJECT_SOURCE_DIR}/src/edge/*.inl*
		${PROJECT_SOURCE_DIR}/src/edge/*.h*
		${PROJECT_SOURCE_DIR}/src/edge/*.c*
		${PROJECT_SOURCE_DIR}/src/edge/*.cc*
		${PROJECT_SOURCE_DIR}/src/edge/*.hpp*
		${PROJECT_SOURCE_DIR}/src/edge/*.cpp*
		${PROJECT_SOURCE_DIR}/src/edge/*.hxx*
		${PROJECT_SOURCE_DIR}/src/edge/*.cxx*)

#Shader embedding specifics (with compilation)
set(ED_USE_SHADERS true CACHE BOOL "Enable built-in shaders")
set(BUFFER_OUT "${PROJECT_SOURCE_DIR}/src/edge/graphics/dynamic/shaders")
if (ED_USE_SHADERS)
	set(BUFFER_DIR "${PROJECT_SOURCE_DIR}/src/shaders")
    set(BUFFER_DATA "#ifndef HAS_SHADER_BATCH\n#define HAS_SHADER_BATCH\n\nnamespace shader_batch\n{\n\tvoid foreach(void* context, void(*callback)(void*, const char*, const unsigned char*, unsigned))\n\t{\n\t\tif (!callback)\n\t\t\treturn;\n")
    file(GLOB_RECURSE BINARIES ${BUFFER_DIR}/*)
    foreach(BINARY ${BINARIES})
        string(REPLACE "${BUFFER_DIR}" "" FILENAME ${BINARY})
        string(REPLACE "${BUFFER_DIR}/" "" FILENAME ${BINARY})
        string(REGEX REPLACE "\\.| |-" "_" VARNAME ${FILENAME})
        string(REPLACE "/" "_" VARNAME ${VARNAME})
        string(TOLOWER ${VARNAME} VARNAME)
        file(READ ${BINARY} FILEDATA LIMIT 32768)
        if (NOT FILEDATA STREQUAL "")
            string(REGEX REPLACE "\t" "" FILEDATA "${FILEDATA}")
            string(REGEX REPLACE "[\r\n][\r\n]" "\n" FILEDATA "${FILEDATA}")
            string(REGEX REPLACE "  *" " " FILEDATA "${FILEDATA}")
            string(REGEX REPLACE "[\r\n]" "\\\\n" FILEDATA "${FILEDATA}")
            string(REGEX REPLACE "\\\"" "\\\\\"" FILEDATA "${FILEDATA}")
            string(APPEND BUFFER_DATA "\n\t\tconst unsigned char ${VARNAME}[] = \"${FILEDATA}\";\n\t\tcallback(context, \"${FILENAME}\", ${VARNAME}, sizeof(${VARNAME}));\n")
        endif()
    endforeach()
    string(APPEND BUFFER_DATA "\t}\n}\n#endif")
    file(WRITE ${BUFFER_OUT}.hpp "${BUFFER_DATA}")	
    list(APPEND SOURCE "${BUFFER_OUT}.hpp")
    message(STATUS "Shaders have been written to: ${BUFFER_OUT}.hpp")
else()
    file(WRITE "${BUFFER_OUT}.hpp" "")
	message(STATUS "Shaders file has been cleared")
endif()