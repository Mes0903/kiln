# Knight's Tour Solver in CMake
# Usage: cmake -P knights_tour.cmake [-DN=8]
# Algorithm: Warnsdorff's heuristic (greedy, O(N^2) per tour)
#
# Board represented as variables: cell_${r}_${c} = move number
# visited_${r}_${c} = 1 if visited.

cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED N)
    set(N 8)
endif()

# Validate
if(N LESS 5)
    message(FATAL_ERROR "N must be >= 5 for a knight's tour to exist (N=${N})")
endif()

math(EXPR TOTAL "${N} * ${N}")

# Knight move offsets
set(DR_0 -2)
set(DC_0 -1)
set(DR_1 -2)
set(DC_1  1)
set(DR_2 -1)
set(DC_2 -2)
set(DR_3 -1)
set(DC_3  2)
set(DR_4  1)
set(DC_4 -2)
set(DR_5  1)
set(DC_5  2)
set(DR_6  2)
set(DC_6 -1)
set(DR_7  2)
set(DC_7  1)

# Count valid unvisited moves from (r, c)
function(count_degree r c out_var)
    set(_count 0)
    foreach(i RANGE 0 7)
        math(EXPR nr "${r} + ${DR_${i}}")
        math(EXPR nc "${c} + ${DC_${i}}")
        if(nr GREATER_EQUAL 0 AND nr LESS ${N} AND nc GREATER_EQUAL 0 AND nc LESS ${N})
            if(NOT visited_${nr}_${nc})
                math(EXPR _count "${_count} + 1")
            endif()
        endif()
    endforeach()
    set(${out_var} ${_count} PARENT_SCOPE)
endfunction()

# Pick next move from (r, c) using Warnsdorff's rule.
# Tie-break: smallest (row, col) lexicographically.
# Sets next_r, next_c in parent scope. next_r = -1 if stuck.
function(pick_next r c)
    set(best_deg 9)
    set(best_r -1)
    set(best_c -1)
    foreach(i RANGE 0 7)
        math(EXPR nr "${r} + ${DR_${i}}")
        math(EXPR nc "${c} + ${DC_${i}}")
        if(nr GREATER_EQUAL 0 AND nr LESS ${N} AND nc GREATER_EQUAL 0 AND nc LESS ${N})
            if(NOT visited_${nr}_${nc})
                count_degree(${nr} ${nc} deg)
                if(deg LESS best_deg)
                    set(best_deg ${deg})
                    set(best_r ${nr})
                    set(best_c ${nc})
                elseif(deg EQUAL best_deg)
                    if(nr LESS best_r OR (nr EQUAL best_r AND nc LESS best_c))
                        set(best_r ${nr})
                        set(best_c ${nc})
                    endif()
                endif()
            endif()
        endif()
    endforeach()
    set(next_r ${best_r} PARENT_SCOPE)
    set(next_c ${best_c} PARENT_SCOPE)
endfunction()

# --- Solve ---

message(STATUS "Solving Knight's Tour on ${N}x${N} board...")

set(cur_r 0)
set(cur_c 0)
set(visited_0_0 1)
set(cell_0_0 0)

math(EXPR last_move "${TOTAL} - 1")

foreach(move RANGE 1 ${last_move})
    pick_next(${cur_r} ${cur_c})
    if(next_r EQUAL -1)
        message(FATAL_ERROR "Warnsdorff heuristic failed at move ${move}/${TOTAL} from (${cur_r}, ${cur_c}).")
    endif()
    set(cur_r ${next_r})
    set(cur_c ${next_c})
    set(visited_${cur_r}_${cur_c} 1)
    set(cell_${cur_r}_${cur_c} ${move})
endforeach()

message(STATUS "Tour found!\n")

# Field width for alignment
string(LENGTH "${last_move}" fw)
math(EXPR fw "${fw} + 1")

# Print board
math(EXPR N1 "${N} - 1")
foreach(r RANGE 0 ${N1})
    set(row_str "")
    foreach(c RANGE 0 ${N1})
        set(val ${cell_${r}_${c}})
        string(LENGTH "${val}" vlen)
        math(EXPR pad "${fw} - ${vlen}")
        set(spaces "")
        if(pad GREATER 0)
            foreach(_ RANGE 1 ${pad})
                string(APPEND spaces " ")
            endforeach()
        endif()
        string(APPEND row_str "${spaces}${val}")
    endforeach()
    message("${row_str}")
endforeach()

message("")
message(STATUS "Done. ${TOTAL} squares visited.")
