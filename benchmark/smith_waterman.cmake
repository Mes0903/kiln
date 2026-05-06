# Smith-Waterman Local Sequence Alignment in CMake
#
# Same O(m×n) DP structure as Levenshtein but more work per cell:
#   - four-way max instead of three-way min
#   - global maximum scan across the entire table
# Both properties make it run slower than Levenshtein at equal N — good for benchmarking.
#
# Usage:
#   cmake -P smith_waterman.cmake
#
# Flags (all via -D):
#   -DSTR_A=<str>      String A  (default: "AGTACGCA")
#   -DSTR_B=<str>      String B  (default: "TATGC")
#   -DSTR_LENGTH=<N>   Generate two pseudo-random DNA strings of length N.
#                      Overrides STR_A/STR_B. Square N×N table — clean scaling.
#   -DVERBOSE=1        Dump the full score matrix  (avoid for large N)
#   -DQUIET=1          Print only the numeric best score
#
# Scoring (edit here to experiment):
#   SCORE_MATCH    +2   characters equal
#   SCORE_MISMATCH -1   characters differ
#   SCORE_GAP      -1   insertion or deletion
#
# Benchmark recipe (bash):
#   for N in 50 100 200 400; do
#     echo -n "N=$N  "
#     time cmake -DSTR_LENGTH=$N -DQUIET=1 -P smith_waterman.cmake
#   done

cmake_minimum_required(VERSION 3.15)

# ── SCORING CONSTANTS ─────────────────────────────────────────────────────────
set(SCORE_MATCH     2)
set(SCORE_MISMATCH -1)
set(SCORE_GAP      -1)

# ── DEFAULTS ──────────────────────────────────────────────────────────────────
if(NOT DEFINED STR_A)
  set(STR_A "AGTACGCA")
endif()
if(NOT DEFINED STR_B)
  set(STR_B "TATGC")
endif()
if(NOT DEFINED VERBOSE)
  set(VERBOSE 0)
endif()
if(NOT DEFINED QUIET)
  set(QUIET 0)
endif()

# ── PSEUDO-RANDOM DNA GENERATOR ───────────────────────────────────────────────
# Cycles through A C G T with a phase offset so the two strings differ.
# Deterministic: same N always gives the same pair.
function(generate_string LENGTH PHASE OUT_VAR)
  set(ALPHA "A;C;G;T")
  set(RESULT "")
  foreach(I RANGE 1 ${LENGTH})
    math(EXPR IDX "(${I} + ${PHASE}) % 4")
    list(GET ALPHA ${IDX} CH)
    string(APPEND RESULT "${CH}")
  endforeach()
  set(${OUT_VAR} "${RESULT}" PARENT_SCOPE)
endfunction()

if(DEFINED STR_LENGTH AND STR_LENGTH GREATER 0)
  generate_string(${STR_LENGTH} 0 STR_A)
  generate_string(${STR_LENGTH} 1 STR_B)   # phase=1 → meaningful overlap at short N
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

# ── HELPERS: flat-list 2D score matrix ────────────────────────────────────────
# H[i][j]  ->  flat index  i*(N+1)+j
# Macros so they read/write the caller's H variable directly (no PARENT_SCOPE dance).
macro(h_get ROW COL COLS OUT)
  math(EXPR _IDX "${ROW} * ${COLS} + ${COL}")
  list(GET H ${_IDX} ${OUT})
endmacro()

macro(h_set ROW COL COLS VAL)
  math(EXPR _IDX "${ROW} * ${COLS} + ${COL}")
  list(REMOVE_AT H ${_IDX})
  list(INSERT    H ${_IDX} ${VAL})
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
  message(STATUS "String A   : \"${STR_A}\"  (length ${M})")
  message(STATUS "String B   : \"${STR_B}\"  (length ${N})")
  message(STATUS "Score      : match=${SCORE_MATCH}  mismatch=${SCORE_MISMATCH}  gap=${SCORE_GAP}")
  message(STATUS "H matrix   : ${ROWS} x ${COLS} = ${CELLS} cells")
endif()

# Early exit — no alignment possible if either string is empty
if(M EQUAL 0 OR N EQUAL 0)
  if(QUIET)
    message(STATUS "0")
  else()
    message(STATUS "Best local alignment score: 0  (empty input)")
  endif()
  return()
endif()

# Allocate flat H matrix initialised to 0
# SW invariant: first row and column are all 0 (local alignment can start anywhere)
set(H "")
math(EXPR TOTAL "${CELLS} - 1")
foreach(I RANGE 0 ${TOTAL})
  list(APPEND H 0)
endforeach()

# Fill H matrix + track global maximum
set(BEST_SCORE 0)
set(BEST_ROW   0)
set(BEST_COL   0)

foreach(I RANGE 1 ${M})
  math(EXPR A_IDX "${I} - 1")
  list(GET CHARS_A ${A_IDX} CH_A)

  foreach(J RANGE 1 ${N})
    math(EXPR B_IDX "${J} - 1")
    list(GET CHARS_B ${B_IDX} CH_B)

    math(EXPR I_PREV "${I} - 1")
    math(EXPR J_PREV "${J} - 1")

    # Diagonal: match or mismatch
    h_get(${I_PREV} ${J_PREV} ${COLS} DIAG)
    if("${CH_A}" STREQUAL "${CH_B}")
      math(EXPR DIAG "${DIAG} + ${SCORE_MATCH}")
    else()
      math(EXPR DIAG "${DIAG} + ${SCORE_MISMATCH}")
    endif()

    # Up: gap in B (delete from A)
    h_get(${I_PREV} ${J} ${COLS} UP)
    math(EXPR UP "${UP} + ${SCORE_GAP}")

    # Left: gap in A (insert into A)
    h_get(${I} ${J_PREV} ${COLS} LEFT)
    math(EXPR LEFT "${LEFT} + ${SCORE_GAP}")

    # SW: max(0, diag, up, left)  — the 0 floor is what makes it *local*
    set(CELL 0)
    if(DIAG GREATER CELL)
      set(CELL ${DIAG})
    endif()
    if(UP GREATER CELL)
      set(CELL ${UP})
    endif()
    if(LEFT GREATER CELL)
      set(CELL ${LEFT})
    endif()

    h_set(${I} ${J} ${COLS} ${CELL})

    if(CELL GREATER BEST_SCORE)
      set(BEST_SCORE ${CELL})
      set(BEST_ROW   ${I})
      set(BEST_COL   ${J})
    endif()
  endforeach()
endforeach()

# ── VERBOSE: dump score matrix ────────────────────────────────────────────────
if(VERBOSE)
  message(STATUS "--- H matrix (row = A prefix, col = B prefix) ---")
  foreach(I RANGE 0 ${M})
    set(ROW_STR "")
    foreach(J RANGE 0 ${N})
      h_get(${I} ${J} ${COLS} V)
      # right-pad single digits for readability
      if(V LESS 10)
        string(APPEND ROW_STR "  ${V}")
      else()
        string(APPEND ROW_STR " ${V}")
      endif()
    endforeach()
    message(STATUS "  [${I}]:${ROW_STR}")
  endforeach()
  message(STATUS "-------------------------------------------------")
endif()

# ── RESULT ────────────────────────────────────────────────────────────────────
if(QUIET)
  message(STATUS "${BEST_SCORE}")
else()
  message(STATUS "Best local alignment score: ${BEST_SCORE}  (at H[${BEST_ROW}][${BEST_COL}])")
endif()
