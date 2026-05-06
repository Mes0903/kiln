# Levenshtein Distance — pure CMake, dynamic programming
#
# Basic usage:
#   cmake -P lavnistine.cmake
#
# Flags (all optional, all via -D):
#   -DSTR_A=<str>        String A  (default: "kitten")
#   -DSTR_B=<str>        String B  (default: "sitting")
#   -DSTR_LENGTH=<N>     Ignore STR_A/B; generate two pseudo-random strings of
#                        length N from cycling alphabet. Square DP table — clean scaling.
#   -DVERBOSE=1          Dump the full DP table  (DO NOT use with large N)
#   -DQUIET=1            Print only the numeric distance — good for scripted benchmarks
#
# Benchmark recipe (bash):
#   for N in 10 20 40 80 160; do
#     echo -n "N=$N  "
#     time cmake -DSTR_LENGTH=$N -DQUIET=1 -P lavnistine.cmake
#   done

cmake_minimum_required(VERSION 3.15)

# ── DEFAULTS ──────────────────────────────────────────────────────────────────
if(NOT DEFINED STR_A)
    set(STR_A "kitten")
endif()
if(NOT DEFINED STR_B)
    set(STR_B "sitting")
endif()
if(NOT DEFINED VERBOSE)
    set(VERBOSE 0)
endif()
if(NOT DEFINED QUIET)
    set(QUIET 0)
endif()

# ── PSEUDO-RANDOM STRING GENERATOR ────────────────────────────────────────────
# Cycles through a-z with a phase offset so the two strings differ.
# Deterministic: same N always gives the same pair.
function(generate_string LENGTH PHASE OUT_VAR)
    set(ALPHA "a;b;c;d;e;f;g;h;i;j;k;l;m;n;o;p;q;r;s;t;u;v;w;x;y;z")
    set(RESULT "")
    foreach(I RANGE 1 ${LENGTH})
        math(EXPR IDX "(${I} + ${PHASE}) % 26")
        list(GET ALPHA ${IDX} CH)
        string(APPEND RESULT "${CH}")
    endforeach()
    set(${OUT_VAR} "${RESULT}" PARENT_SCOPE)
endfunction()

if(DEFINED STR_LENGTH AND STR_LENGTH GREATER 0)
    generate_string(${STR_LENGTH} 0 STR_A)
    generate_string(${STR_LENGTH} 7 STR_B)   # phase=7 keeps them meaningfully distinct
endif()

# ── HELPER: string -> cmake char list ─────────────────────────────────────────
function(str_to_chars STR OUT_VAR)
    string(LENGTH "${STR}" LEN)
    set(CHARS "")
    if(LEN GREATER 0)
        math(EXPR LAST "${LEN} - 1")
        foreach(I RANGE 0 ${LAST})
            string(SUBSTRING "${STR}" ${I} 1 CH)
            list(APPEND CHARS "${CH}")
        endforeach()
    endif()
    set(${OUT_VAR} "${CHARS}" PARENT_SCOPE)
endfunction()

# ── HELPERS: flat-list 2D DP table ────────────────────────────────────────────
# dp[i][j]  ->  flat index  i*(N+1)+j
# Using macros (not functions) so they read/write the caller's DP variable directly.
macro(dp_get ROW COL COLS OUT)
    math(EXPR _IDX "${ROW} * ${COLS} + ${COL}")
    list(GET DP ${_IDX} ${OUT})
endmacro()

macro(dp_set ROW COL COLS VAL)
    math(EXPR _IDX "${ROW} * ${COLS} + ${COL}")
    list(REMOVE_AT DP ${_IDX})
    list(INSERT    DP ${_IDX} ${VAL})
endmacro()

# ── MAIN ──────────────────────────────────────────────────────────────────────
str_to_chars("${STR_A}" CHARS_A)
str_to_chars("${STR_B}" CHARS_B)

string(LENGTH "${STR_A}" M)
string(LENGTH "${STR_B}" N)

math(EXPR ROWS "${M} + 1")
math(EXPR COLS "${N} + 1")
math(EXPR CELLS "${ROWS} * ${COLS}")

if(NOT QUIET)
    message(STATUS "String A : \"${STR_A}\"  (length ${M})")
    message(STATUS "String B : \"${STR_B}\"  (length ${N})")
    message(STATUS "DP table : ${ROWS} x ${COLS} = ${CELLS} cells")
endif()

# Early exit — guards against RANGE 0 -1 undefined behaviour on empty strings
if(M EQUAL 0)
    if(QUIET)
        message(STATUS "${N}")
    else()
        message(STATUS "Levenshtein distance: ${N}")
    endif()
    return()
endif()
if(N EQUAL 0)
    if(QUIET)
        message(STATUS "${M}")
    else()
        message(STATUS "Levenshtein distance: ${M}")
    endif()
    return()
endif()

# Allocate flat DP list initialised to 0
set(DP "")
math(EXPR TOTAL "${CELLS} - 1")
foreach(I RANGE 0 ${TOTAL})
    list(APPEND DP 0)
endforeach()

# Base cases: cost of transforming empty string <-> prefix of length k
foreach(I RANGE 0 ${M})
    dp_set(${I} 0 ${COLS} ${I})
endforeach()
foreach(J RANGE 0 ${N})
    dp_set(0 ${J} ${COLS} ${J})
endforeach()

# Fill DP table
foreach(I RANGE 1 ${M})
    math(EXPR A_IDX "${I} - 1")
    list(GET CHARS_A ${A_IDX} CH_A)

    foreach(J RANGE 1 ${N})
        math(EXPR B_IDX "${J} - 1")
        list(GET CHARS_B ${B_IDX} CH_B)

        math(EXPR I_PREV "${I} - 1")
        math(EXPR J_PREV "${J} - 1")

        dp_get(${I_PREV} ${J}      ${COLS} COST_DEL)
        dp_get(${I}      ${J_PREV} ${COLS} COST_INS)
        dp_get(${I_PREV} ${J_PREV} ${COLS} COST_SUB_BASE)

        math(EXPR COST_DEL "${COST_DEL} + 1")
        math(EXPR COST_INS "${COST_INS} + 1")

        if("${CH_A}" STREQUAL "${CH_B}")
            set(COST_SUB ${COST_SUB_BASE})
        else()
            math(EXPR COST_SUB "${COST_SUB_BASE} + 1")
        endif()

        # min(delete, insert, substitute)
        set(BEST ${COST_DEL})
        if(COST_INS LESS BEST)
            set(BEST ${COST_INS})
        endif()
        if(COST_SUB LESS BEST)
            set(BEST ${COST_SUB})
        endif()

        dp_set(${I} ${J} ${COLS} ${BEST})
    endforeach()
endforeach()

# ── VERBOSE: dump full DP table ───────────────────────────────────────────────
if(VERBOSE)
    message(STATUS "--- DP table (row = A prefix length, col = B prefix length) ---")
    foreach(I RANGE 0 ${M})
        set(ROW_STR "")
        foreach(J RANGE 0 ${N})
            dp_get(${I} ${J} ${COLS} V)
            string(APPEND ROW_STR " ${V}")
        endforeach()
        message(STATUS "  [${I}]:${ROW_STR}")
    endforeach()
    message(STATUS "---------------------------------------------------------------")
endif()

# ── RESULT ────────────────────────────────────────────────────────────────────
dp_get(${M} ${N} ${COLS} RESULT)

if(QUIET)
    message(STATUS "${RESULT}")
else()
    message(STATUS "Levenshtein distance: ${RESULT}")
endif()
